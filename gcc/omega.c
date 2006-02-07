/* Source code for an implementation of the Omega test, an integer 
   programming algorithm for dependence analysis, by William Pugh, 
   appeared in Supercomputing '91 and CACM Aug 92.

   This code has no license restrictions, and is considered public
   domain.

   Changes copyright (C) 2005, 2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

/* Options:
   
   ELIMINATE_REDUNDANT_CONSTRAINTS
   - use expensive methods to eliminate all redundant constraints
   
   SINGLE_RESULT
   - only produce a single simplified result.

   APROX 
   - approximate inexact reductions omega_verify_simplification (runtime),
   - if TRUE, omega_simplify_problem checks for problem with no
   solutions omega_reduce_with_subs (runtime),
   - if FALSE, convert substitutions back to EQs.
 */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "diagnostic.h"
#include "varray.h"
#include "tree-pass.h"
#include "omega.h"

bool omega_reduce_with_subs = true;
bool omega_verify_simplification = false;

#ifndef APROX
#define APROX 0
#endif

#define KEY_MULT 31

#ifdef SINGLE_RESULT
#define return_single_result  1
#else
static int return_single_result = 0;
#endif

static int may_be_red = 0;
#define HASH_TABLE_SIZE PARAM_VALUE (PARAM_OMEGA_HASH_TABLE_SIZE)
#define MAX_KEYS PARAM_VALUE (PARAM_OMEGA_MAX_KEYS)

static eqn hash_master;
static bool non_convex = false;
static bool do_it_again;
static int conservative = 0;
static int next_key;
static char wild_name[200][40];
static int next_wild_card = 0;
static enum omega_result omega_found_reduction;
static int *packing;
static bool in_approximate_mode = 0;
static bool create_color = false;
static int please_no_equalities_in_simplified_problems = 0;
static int hash_version = 0;

omega_pb no_problem = (omega_pb) 0;
omega_pb original_problem = (omega_pb) 0;

/* Return the integer A divided by B.  */

static inline int
int_div (int a, int b)
{
  if (a > 0)
    return a/b;
  else
    return -((-a + b - 1)/b);
}

/* Return the integer A modulo B.  */

static inline int
int_mod (int a, int b)
{
  return a - b * int_div (a, b);
}

/* For X and Y positive integers, return X multiplied by Y and check
   that the result does not overflow.  */

static inline int
check_pos_mul (int x, int y)
{
  if (x != 0)
    gcc_assert ((INT_MAX) / x > y);

  return x * y;
}

/* Return X multiplied by Y and check that the result does not
   overflow.  */

static inline int
check_mul (int x, int y)
{
  if (x >= 0)
    {
      if (y >= 0)
	return check_pos_mul (x, y);
      else
	return -check_pos_mul (x, -y);
    }
  else if (y >= 0)
    return -check_pos_mul (-x, y);
  else
    return check_pos_mul (-x, -y);
}

/* Set *M to the maximum of *M and X.  */

static inline void
set_max (int *m, int x)
{
  if (*m < x) 
    *m = x;
}

/* Set *M to the minimum of *M and X.  */

static inline void
set_min (int *m, int x)
{
  if (*m > x)
    *m = x;
}

/* Test whether equation E is red.  */

static inline bool
omega_eqn_is_red (eqn e, int desired_res)
{
  return (desired_res == omega_simplify && e->color == omega_red);
}

/* Return a string for VARIABLE.  */

static inline char *
omega_var_to_str (int variable)
{
  if (0 <= variable && variable <= 20)
    return wild_name[variable];

  if (-20 < variable && variable < 0)
    return wild_name[40 + variable];

  /* Collapse all the entries that would have overflowed.  */
  return wild_name[21];
}

/* Return a string for variable I in problem PB.  */

static inline char *
omega_variable_to_str (omega_pb pb, int i)
{
  return omega_var_to_str (pb->var[i]);
}

/* Do nothing function: used for default initializations.  */

void
omega_no_procedure (omega_pb pb ATTRIBUTE_UNUSED)
{
}

void (*omega_when_reduced) (omega_pb) = omega_no_procedure;

/* Compute the greatest common divisor of A and B.  */

static inline int
gcd (int b, int a)
{
  if (b == 1)
    return 1;

  while (b != 0)
    {
      int t = b;
      b = a % b;
      a = t;
    }

  return a;
}

/* Don't use this; instead, use omega_alloc_problem.  This initializes
   static vars for problem PB.  FIXME: remove those static vars.  */

void
omega_initialize_statics (omega_pb pb)
{
  pb->hash_version = hash_version;
}

/* Print to FILE from PB equation E with all its coefficients
   multiplied by C.  */

static void
omega_print_term (FILE *file, omega_pb pb, eqn e, int c)
{
  int i;
  bool first = true;
  int n = pb->num_vars;
  int went_first = -1;

  for (i = 1; i <= n; i++)
    if (c * e->coef[i] > 0)
      {
	first = false;
	went_first = i;

	if (c * e->coef[i] == 1)
	  fprintf (file, "%s", omega_variable_to_str (pb, i));
	else
	  fprintf (file, "%d * %s", c * e->coef[i],
		   omega_variable_to_str (pb, i));
	break;
      }

  for (i = 1; i <= n; i++)
    if (i != went_first && c * e->coef[i] != 0)
      {
	if (!first && c * e->coef[i] > 0)
	  fprintf (file, " + ");

	first = false;

	if (c * e->coef[i] == 1)
	  fprintf (file, "%s", omega_variable_to_str (pb, i));
	else if (c * e->coef[i] == -1)
	  fprintf (file, " - %s", omega_variable_to_str (pb, i));
	else
	  fprintf (file, "%d * %s", c * e->coef[i],
		   omega_variable_to_str (pb, i));
      }

  if (!first && c * e->coef[0] > 0)
    fprintf (file, " + ");

  if (first || c * e->coef[0] != 0)
    fprintf (file, "%d", c * e->coef[0]);
}

/* Print to FILE the equation E of problem PB.  */

void
omega_print_eqn (FILE *file, omega_pb pb, eqn e, bool test, int extra)
{
  int i;
  int n = pb->num_vars + extra;
  bool is_lt = test && e->coef[0] == -1;
  bool first;

  if (test)
    {
      if (e->touched)
	fprintf (file, "!");

      else if (!e->touched && e->key != 0)
	fprintf (file, "%d: ", e->key);
    }

  if (e->color == omega_red)
    fprintf (file, "[");

  first = true;

  for (i = is_lt ? 1 : 0; i <= n; i++)
    if (e->coef[i] < 0)
      {
	if (!first)
	  fprintf (file, " + ");
	else
	  first = false;

	if (i == 0)
	  fprintf (file, "%d", -e->coef[i]);
	else if (e->coef[i] == -1)
	  fprintf (file, "%s", omega_variable_to_str (pb, i));
	else
	  fprintf (file, "%d * %s", -e->coef[i],
		   omega_variable_to_str (pb, i));
      }

  if (first)
    {
      if (is_lt)
	{
	  fprintf (file, "1");
	  is_lt = false;
	}
      else
	fprintf (file, "0");
    }

  if (test == 0)
    fprintf (file, " = ");
  else if (is_lt)
    fprintf (file, " < ");
  else
    fprintf (file, " <= ");

  first = true;

  for (i = 0; i <= n; i++)
    if (e->coef[i] > 0)
      {
	if (!first)
	  fprintf (file, " + ");
	else
	  first = false;

	if (i == 0)
	  fprintf (file, "%d", e->coef[i]);
	else if (e->coef[i] == 1)
	  fprintf (file, "%s", omega_variable_to_str (pb, i));
	else
	  fprintf (file, "%d * %s", e->coef[i],
		   omega_variable_to_str (pb, i));
      }

  if (first)
    fprintf (file, "0");

  if (e->color == omega_red)
    fprintf (file, "]");
}

/* Print to FILE all the variables of problem PB.  */

static void
omega_print_vars (FILE *file, omega_pb pb)
{
  int i;

  fprintf (file, "variables = ");

  if (pb->safe_vars > 0)
    fprintf (file, "(");

  for (i = 1; i <= pb->num_vars; i++)
    {
      fprintf (file, "%s", omega_variable_to_str (pb, i));

      if (i == pb->safe_vars)
	fprintf (file, ")");

      if (i < pb->num_vars)
	fprintf (file, ", ");
    }

  fprintf (file, "\n");
}

/* Print to FILE problem PB.  */

void
omega_print_problem (FILE *file, omega_pb pb)
{
  int e;

  if (!pb->variables_initialized)
    omega_initialize_variables (pb);

  omega_print_vars (file, pb);

  for (e = 0; e < pb->num_eqs; e++)
    {
      omega_print_eq (file, pb, &pb->eqs[e]);
      fprintf (file, "\n");
    }

  fprintf (file, "Done with EQ\n");

  for (e = 0; e < pb->num_geqs; e++)
    {
      omega_print_geq (file, pb, &pb->geqs[e]);
      fprintf (file, "\n");
    }

  fprintf (file, "Done with GEQ\n");

  for (e = 0; e < pb->num_subs; e++)
    {
      eqn eq = &pb->subs[e];

      if (eq->color == omega_red)
	fprintf (file, "[");

      if (eq->key > 0)
	fprintf (file, "%s := ", omega_var_to_str (eq->key));
      else
	fprintf (file, "#%d := ", eq->key);

      omega_print_term (file, pb, eq, 1);

      if (eq->color == omega_red)
	fprintf (file, "]");

      fprintf (file, "\n");
    }
}

/* Return the number of equations in PB tagged omega_red.  */

int
omega_count_red_equations (omega_pb pb)
{
  int e, i;
  int result = 0;

  for (e = 0; e < pb->num_eqs; e++)
    if (pb->eqs[e].color == omega_red)
      {
	for (i = pb->num_vars; i > 0; i--)
	  if (pb->geqs[e].coef[i])
	    break;

	if (i == 0 && pb->geqs[e].coef[0] == 1)
	  return 0;
	else
	  result += 2;
      }

  for (e = 0; e < pb->num_geqs; e++)
    if (pb->geqs[e].color == omega_red)
      result += 1;

  for (e = 0; e < pb->num_subs; e++)
    if (pb->subs[e].color == omega_red)
      result += 2;

  return result;
}

/* Print to FILE all the equations in PB that are tagged omega_red.  */

void
omega_print_red_equations (FILE *file, omega_pb pb)
{
  int e;

  if (!pb->variables_initialized)
    omega_initialize_variables (pb);

  omega_print_vars (file, pb);

  for (e = 0; e < pb->num_eqs; e++)
    if (pb->eqs[e].color == omega_red)
      {
	omega_print_eq (file, pb, &pb->eqs[e]);
	fprintf (file, "\n");
      }

  for (e = 0; e < pb->num_geqs; e++)
    if (pb->geqs[e].color == omega_red)
      {
	omega_print_geq (file, pb, &pb->geqs[e]);
	fprintf (file, "\n");
      }

  for (e = 0; e < pb->num_subs; e++)
    if (pb->subs[e].color == omega_red)
      {
	eqn eq = &pb->subs[e];
	fprintf (file, "[");

	if (eq->key > 0)
	  fprintf (file, "%s := ", omega_var_to_str (eq->key));
	else
	  fprintf (file, "#%d := ", eq->key);

	omega_print_term (file, pb, eq, 1);
	fprintf (file, "]\n");
      }
}

/* Pretty print PB to FILE.  */

void
omega_pretty_print_problem (FILE *file, omega_pb pb)
{
  int e, v, v1, v2, v3, t;
  bool *live = (bool *) (xmalloc (OMEGA_MAX_GEQS * sizeof (bool)));
  int stuffPrinted = 0;
  bool change;

  typedef enum {
    none, le, lt
  } partial_order_type;

  partial_order_type **po = (partial_order_type **) 
    xmalloc (OMEGA_MAX_VARS * OMEGA_MAX_VARS * sizeof (partial_order_type));
  int **po_eq = (int **) xmalloc (OMEGA_MAX_VARS * OMEGA_MAX_VARS * sizeof (int));
  int *last_links = (int *) xmalloc (OMEGA_MAX_VARS * sizeof (int));
  int *first_links = (int *) xmalloc (OMEGA_MAX_VARS * sizeof (int));
  int *chain_length = (int *) xmalloc (OMEGA_MAX_VARS * sizeof (int));
  int *chain = (int *) xmalloc (OMEGA_MAX_VARS * sizeof (int));
  int i, m;
  bool multiprint;

  if (!pb->variables_initialized)
    omega_initialize_variables (pb);

  if (pb->num_vars > 0)
    {
      omega_eliminate_redundant (pb, 0);

      for (e = 0; e < pb->num_eqs; e++)
	{
	  if (stuffPrinted)
	    fprintf (file, "; ");

	  stuffPrinted = 1;
	  omega_print_eq (file, pb, &pb->eqs[e]);
	}

      for (e = 0; e < pb->num_geqs; e++)
	live[e] = true;

      while (1)
	{
	  for (v = 1; v <= pb->num_vars; v++)
	    {
	      last_links[v] = first_links[v] = 0;
	      chain_length[v] = 0;

	      for (v2 = 1; v2 <= pb->num_vars; v2++)
		po[v][v2] = none;
	    }

	  for (e = 0; e < pb->num_geqs; e++)
	    if (live[e])
	      {
		for (v = 1; v <= pb->num_vars; v++)
		  if (pb->geqs[e].coef[v] == 1)
		    first_links[v]++;
		  else if (pb->geqs[e].coef[v] == -1)
		    last_links[v]++;

		v1 = pb->num_vars;

		while (v1 > 0 && pb->geqs[e].coef[v1] == 0)
		  v1--;

		v2 = v1 - 1;

		while (v2 > 0 && pb->geqs[e].coef[v2] == 0)
		  v2--;

		v3 = v2 - 1;

		while (v3 > 0 && pb->geqs[e].coef[v3] == 0)
		  v3--;

		if (pb->geqs[e].coef[0] > 0 || pb->geqs[e].coef[0] < -1
		    || v2 <= 0 || v3 > 0
		    || pb->geqs[e].coef[v1] * pb->geqs[e].coef[v2] != -1)
		  {
		    /* Not a partial order relation.  */
		  }
		else
		  {
		    if (pb->geqs[e].coef[v1] == 1)
		      {
			v3 = v2;
			v2 = v1;
			v1 = v3;
		      }

		    /* Relation is v1 <= v2 or v1 < v2.  */
		    po[v1][v2] = ((pb->geqs[e].coef[0] == 0) ? le : lt);
		    po_eq[v1][v2] = e;
		  }
	      }
	  for (v = 1; v <= pb->num_vars; v++)
	    chain_length[v] = last_links[v];

	  /* Just in case pb->num_vars <= 0.  */
	  change = false;
	  for (t = 0; t < pb->num_vars; t++)
	    {
	      change = false;

	      for (v1 = 1; v1 <= pb->num_vars; v1++)
		for (v2 = 1; v2 <= pb->num_vars; v2++)
		  if (po[v1][v2] != none &&
		      chain_length[v1] <= chain_length[v2])
		    {
		      chain_length[v1] = chain_length[v2] + 1;
		      change = true;
		    }
	    }

	  /* Caught in cycle.  */
	  gcc_assert (!change);

	  for (v1 = 1; v1 <= pb->num_vars; v1++)
	    if (chain_length[v1] == 0)
	      first_links[v1] = 0;

	  v = 1;

	  for (v1 = 2; v1 <= pb->num_vars; v1++)
	    if (chain_length[v1] + first_links[v1] >
		chain_length[v] + first_links[v])
	      v = v1;

	  if (chain_length[v] + first_links[v] == 0)
	    break;

	  if (stuffPrinted)
	    fprintf (file, "; ");

	  stuffPrinted = 1;

	  /* Chain starts at v. */
	  {
	    int tmp;
	    bool first = true;

	    for (e = 0; e < pb->num_geqs; e++)
	      if (live[e] && pb->geqs[e].coef[v] == 1)
		{
		  if (!first)
		    fprintf (file, ", ");

		  tmp = pb->geqs[e].coef[v];
		  pb->geqs[e].coef[v] = 0;
		  omega_print_term (file, pb, &pb->geqs[e], -1);
		  pb->geqs[e].coef[v] = tmp;
		  live[e] = false;
		  first = false;
		}

	    if (!first)
	      fprintf (file, " <= ");
	  }

	  /* Find chain.  */
	  chain[0] = v;
	  m = 1;
	  while (1)
	    {
	      /* Print chain.  */
	      for (v2 = 1; v2 <= pb->num_vars; v2++)
		if (po[v][v2] && chain_length[v] == 1 + chain_length[v2])
		  break;

	      if (v2 > pb->num_vars)
		break;

	      chain[m++] = v2;
	      v = v2;
	    }

	  fprintf (file, "%s", omega_variable_to_str (pb, chain[0]));
	  
	  for (multiprint = false, i = 1; i < m; i++)
	    {
	      v = chain[i - 1];
	      v2 = chain[i];

	      if (po[v][v2] == le)
		fprintf (file, " <= ");
	      else
		fprintf (file, " < ");

	      fprintf (file, "%s", omega_variable_to_str (pb, v2));
	      live[po_eq[v][v2]] = false;

	      if (!multiprint && i < m - 1)
		for (v3 = 1; v3 <= pb->num_vars; v3++)
		  {
		    if (v == v3 || v2 == v3
			|| po[v][v2] != po[v][v3]
			|| po[v2][chain[i + 1]] != po[v3][chain[i + 1]])
		      continue;

		    fprintf (file, ",%s", omega_variable_to_str (pb, v3));
		    live[po_eq[v][v3]] = false;
		    live[po_eq[v3][chain[i + 1]]] = false;
		    multiprint = true;
		  }
	      else
		multiprint = false;
	    }

	  v = chain[m - 1];
	  /* Print last_links.  */
	  {
	    int tmp;
	    bool first = true;

	    for (e = 0; e < pb->num_geqs; e++)
	      if (live[e] && pb->geqs[e].coef[v] == -1)
		{
		  if (!first)
		    fprintf (file, ", ");
		  else
		    fprintf (file, " <= ");

		  tmp = pb->geqs[e].coef[v];
		  pb->geqs[e].coef[v] = 0;
		  omega_print_term (file, pb, &pb->geqs[e], 1);
		  pb->geqs[e].coef[v] = tmp;
		  live[e] = false;
		  first = false;
		}
	  }
	}

      for (e = 0; e < pb->num_geqs; e++)
	if (live[e])
	  {
	    if (stuffPrinted)
	      fprintf (file, "; ");
	    stuffPrinted = 1;
	    omega_print_geq (file, pb, &pb->geqs[e]);
	  }

      for (e = 0; e < pb->num_subs; e++)
	{
	  eqn eq = &pb->subs[e];

	  if (stuffPrinted)
	    fprintf (file, "; ");

	  stuffPrinted = 1;

	  if (eq->key > 0)
	    fprintf (file, "%s := ", omega_var_to_str (eq->key));
	  else
	    fprintf (file, "#%d := ", eq->key);

	  omega_print_term (file, pb, eq, 1);
	}
    }

  free (live);
  free (po);
  free (po_eq);
  free (last_links);
  free (first_links);
  free (chain_length);
  free (chain);
}

/* Assign to variable I in PB the next wildcard name.  The name of a
   wildcard is a negative number.  */

static void
omega_name_wild_card (omega_pb pb, int i)
{
  --next_wild_card;
  if (next_wild_card < -PARAM_VALUE (PARAM_OMEGA_MAX_WILD_CARDS))
    next_wild_card = -1;
  pb->var[i] = next_wild_card;
}

/* Return the index of the last protected (or safe) variable in PB,
   after having added a new wildcard variable.  */

static int
omega_add_new_wild_card (omega_pb pb)
{
  int e;
  int i = ++pb->safe_vars;
  pb->num_vars++;

  /* Make a free place in the protected (safe) variables, by moving
     the non protected variable pointed by "I" at the end, ie. at
     offset pb->num_vars.  */
  if (pb->num_vars != i)
    {
      /* Move "I" for all the inequalities.  */
      for (e = pb->num_geqs - 1; e >= 0; e--)
	{
	  if (pb->geqs[e].coef[i])
	    pb->geqs[e].touched = 1;

	  pb->geqs[e].coef[pb->num_vars] = pb->geqs[e].coef[i];
	}

      /* Move "I" for all the equalities.  */
      for (e = pb->num_eqs - 1; e >= 0; e--)
	pb->eqs[e].coef[pb->num_vars] = pb->eqs[e].coef[i];

      /* Move "I" for all the substitutions.  */
      for (e = pb->num_subs - 1; e >= 0; e--)
	pb->subs[e].coef[pb->num_vars] = pb->subs[e].coef[i];

      /* Move the identifier.  */
      pb->var[pb->num_vars] = pb->var[i];
    }

  /* Initialize at zero all the coefficients  */
  for (e = pb->num_geqs - 1; e >= 0; e--)
    pb->geqs[e].coef[i] = 0;

  for (e = pb->num_eqs - 1; e >= 0; e--)
    pb->eqs[e].coef[i] = 0;

  for (e = pb->num_subs - 1; e >= 0; e--)
    pb->subs[e].coef[i] = 0;

  /* And give it a name.  */
  omega_name_wild_card (pb, i);
  return i;
}

/* Delete inequality E from problem PB that has N_VARS variables.  */

static void
omega_delete_geq (omega_pb pb, int e, int nv)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Deleting %d (last:%d): ", e, pb->num_geqs - 1);
      omega_print_geq (dump_file, pb, &pb->geqs[e]);
      fprintf (dump_file, "\n");
    }

  if (e < pb->num_geqs - 1)
    omega_copy_eqn (&pb->geqs[e], &pb->geqs[pb->num_geqs - 1], nv);

  pb->num_geqs--;
}

/* Delete extra inequality E from problem PB that has N_VARS variables.  */

static void
omega_delete_geq_extra (omega_pb pb, int e, int n_vars)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Deleting %d: ",e);
      omega_print_geq_extra (dump_file, pb, &pb->geqs[e]);
      fprintf (dump_file, "\n");
    }

  if (e < pb->num_geqs - 1)
    omega_copy_eqn (&pb->geqs[e], &pb->geqs[pb->num_geqs - 1], n_vars);

  pb->num_geqs--;
}


/* Remove variable I from problem PB.  */

static void
omega_delete_variable (omega_pb pb, int i)
{
  int n_vars = pb->num_vars;
  int e;

  if (omega_safe_var_p (pb, i))
    {
      int j = pb->safe_vars;

      for (e = pb->num_geqs - 1; e >= 0; e--)
	{
	  pb->geqs[e].touched = 1;
	  pb->geqs[e].coef[i] = pb->geqs[e].coef[j];
	  pb->geqs[e].coef[j] = pb->geqs[e].coef[n_vars];
	}

      for (e = pb->num_eqs - 1; e >= 0; e--)
	{
	  pb->eqs[e].coef[i] = pb->eqs[e].coef[j];
	  pb->eqs[e].coef[j] = pb->eqs[e].coef[n_vars];
	}

      for (e = pb->num_subs - 1; e >= 0; e--)
	{
	  pb->subs[e].coef[i] = pb->subs[e].coef[j];
	  pb->subs[e].coef[j] = pb->subs[e].coef[n_vars];
	}

      pb->var[i] = pb->var[j];
      pb->var[j] = pb->var[n_vars];
    }
  else if (i < n_vars)
    {
      for (e = pb->num_geqs - 1; e >= 0; e--)
	if (pb->geqs[e].coef[n_vars])
	  {
	    pb->geqs[e].coef[i] = pb->geqs[e].coef[n_vars];
	    pb->geqs[e].touched = 1;
	  }

      for (e = pb->num_eqs - 1; e >= 0; e--)
	pb->eqs[e].coef[i] = pb->eqs[e].coef[n_vars];

      for (e = pb->num_subs - 1; e >= 0; e--)
	pb->subs[e].coef[i] = pb->subs[e].coef[n_vars];

      pb->var[i] = pb->var[n_vars];
    }

  if (omega_safe_var_p (pb, i))
    pb->safe_vars--;

  pb->num_vars--;
}

/* Helper function.  */

static inline int
setup_packing (eqn eqn, int num_vars)
{
  int k;
  int *p = &packing[0];

  for (k = num_vars; k >= 0; k--)
    if (eqn->coef[k])
      *(p++) = k;

  return (p - &packing[0]) - 1;
}


/* Helper function.  */

static inline void
omega_substitute_red_1 (eqn eq, eqn sub, int var, int c, bool *found_black,
			int top_var)
{
  int j, k = eq->coef[var];

  if (k != 0)
    {
      if (eq->color == omega_black)
	*found_black = true;
      else
	{
	  eq->coef[var] = 0;
	  for (j = top_var; j >= 0; j--)
	    eq->coef[packing[j]] -= sub->coef[packing[j]] * k * c;
	}
    }
}

/* Substitute in PB variable VAR with "C * SUB".  */

static void
omega_substitute_red (omega_pb pb, eqn sub, int var, int c, bool *found_black)
{
  int e, top_var = setup_packing (sub, pb->num_vars);

  *found_black = false;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      if (sub->color == omega_red)
	fprintf (dump_file, "[");

      fprintf (dump_file, "substituting using %s := ",
	       omega_variable_to_str (pb, var));
      omega_print_term (dump_file, pb, sub, -c);

      if (sub->color == omega_red)
	fprintf (dump_file, "]");

      fprintf (dump_file, "\n");
      omega_print_vars (dump_file, pb);
    }

  for (e = pb->num_eqs - 1; e >= 0; e--)
    {
      eqn eqn = &(pb->eqs[e]);

      omega_substitute_red_1 (eqn, sub, var, c, found_black, top_var);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  omega_print_eq (dump_file, pb, eqn);
	  fprintf (dump_file, "\n");
	}
    }

  for (e = pb->num_geqs - 1; e >= 0; e--)
    {
      eqn eqn = &(pb->geqs[e]);

      omega_substitute_red_1 (eqn, sub, var, c, found_black, top_var);

      if (eqn->coef[var] && eqn->color == omega_red)
	eqn->touched = 1;

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  omega_print_geq (dump_file, pb, eqn);
	  fprintf (dump_file, "\n");
	}
    }

  for (e = pb->num_subs - 1; e >= 0; e--)
    {
      eqn eqn = &(pb->subs[e]);

      omega_substitute_red_1 (eqn, sub, var, c, found_black, top_var);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "%s := ", omega_var_to_str (eqn->key));
	  omega_print_term (dump_file, pb, eqn, 1);
	  fprintf (dump_file, "\n");
	}
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "---\n\n");

  if (omega_safe_var_p (pb, var) && !omega_wildcard_p (pb, var))
    *found_black = true;
}

/* Substitute in PB variable VAR with "C * SUB".  */

static void
omega_substitute (omega_pb pb, eqn sub, int var, int c)
{
  int e, j, j0;
  int top_var = setup_packing (sub, pb->num_vars);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "substituting using %s := ",
	       omega_variable_to_str (pb, var));
      omega_print_term (dump_file, pb, sub, -c);
      fprintf (dump_file, "\n");
      omega_print_vars (dump_file, pb);
    }

  if (top_var < 0)
    {
      for (e = pb->num_eqs - 1; e >= 0; e--)
	pb->eqs[e].coef[var] = 0;

      for (e = pb->num_geqs - 1; e >= 0; e--)
	if (pb->geqs[e].coef[var] != 0)
	  {
	    pb->geqs[e].touched = 1;
	    pb->geqs[e].coef[var] = 0;
	  }

      for (e = pb->num_subs - 1; e >= 0; e--)
	pb->subs[e].coef[var] = 0;

      if (omega_safe_var_p (pb, var) && !omega_wildcard_p (pb, var))
	{
	  int k;
	  eqn eqn = &(pb->subs[pb->num_subs++]);

	  for (k = pb->num_vars; k >= 0; k--)
	    eqn->coef[k] = 0;

	  eqn->key = pb->var[var];
	  eqn->color = omega_black;
	}
    }
  else if (top_var == 0 && packing[0] == 0)
    {
      c = -sub->coef[0] * c;

      for (e = pb->num_eqs - 1; e >= 0; e--)
	{
	  pb->eqs[e].coef[0] += pb->eqs[e].coef[var] * c;
	  pb->eqs[e].coef[var] = 0;
	}

      for (e = pb->num_geqs - 1; e >= 0; e--)
	if (pb->geqs[e].coef[var] != 0)
	  {
	    pb->geqs[e].coef[0] += pb->geqs[e].coef[var] * c;
	    pb->geqs[e].coef[var] = 0;
	    pb->geqs[e].touched = 1;
	  }

      for (e = pb->num_subs - 1; e >= 0; e--)
	{
	  pb->subs[e].coef[0] += pb->subs[e].coef[var] * c;
	  pb->subs[e].coef[var] = 0;
	}

      if (omega_safe_var_p (pb, var) && !omega_wildcard_p (pb, var))
	{
	  int k;
	  eqn eqn = &(pb->subs[pb->num_subs++]);

	  for (k = pb->num_vars; k >= 1; k--)
	    eqn->coef[k] = 0;

	  eqn->coef[0] = c;
	  eqn->key = pb->var[var];
	  eqn->color = omega_black;
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "---\n\n");
	  omega_print_problem (dump_file, pb);
	  fprintf (dump_file, "===\n\n");
	}
    }
  else
    {
      for (e = pb->num_eqs - 1; e >= 0; e--)
	{
	  eqn eqn = &(pb->eqs[e]);
	  int k = eqn->coef[var];

	  if (k != 0)
	    {
	      k = c * k;
	      eqn->coef[var] = 0;

	      for (j = top_var; j >= 0; j--)
		{
		  j0 = packing[j];
		  eqn->coef[j0] -= sub->coef[j0] * k;
		}
	    }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      omega_print_eq (dump_file, pb, eqn);
	      fprintf (dump_file, "\n");
	    }
	}

      for (e = pb->num_geqs - 1; e >= 0; e--)
	{
	  eqn eqn = &(pb->geqs[e]);
	  int k = eqn->coef[var];

	  if (k != 0)
	    {
	      k = c * k;
	      eqn->touched = 1;
	      eqn->coef[var] = 0;

	      for (j = top_var; j >= 0; j--)
		{
		  j0 = packing[j];
		  eqn->coef[j0] -= sub->coef[j0] * k;
		}
	    }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      omega_print_geq (dump_file, pb, eqn);
	      fprintf (dump_file, "\n");
	    }
	}

      for (e = pb->num_subs - 1; e >= 0; e--)
	{
	  eqn eqn = &(pb->subs[e]);
	  int k = eqn->coef[var];

	  if (k != 0)
	    {
	      k = c * k;
	      eqn->coef[var] = 0;

	      for (j = top_var; j >= 0; j--)
		{
		  j0 = packing[j];
		  eqn->coef[j0] -= sub->coef[j0] * k;
		}
	    }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "%s := ", omega_var_to_str (eqn->key));
	      omega_print_term (dump_file, pb, eqn, 1);
	      fprintf (dump_file, "\n");
	    }
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "---\n\n");
	  omega_print_problem (dump_file, pb);
	  fprintf (dump_file, "===\n\n");
	}

      if (omega_safe_var_p (pb, var) && !omega_wildcard_p (pb, var))
	{
	  int k;
	  eqn eqn = &(pb->subs[pb->num_subs++]);
	  c = -c;

	  for (k = pb->num_vars; k >= 0; k--)
	    eqn->coef[k] = c * (sub->coef[k]);

	  eqn->key = pb->var[var];
	  eqn->color = sub->color;
	}
    }
}

/* Solve e = factor alpha for x_j and substitute.  */

static void
omega_do_mod (omega_pb pb, int factor, int e, int j)
{
  int k, i;
  eqn eq = omega_alloc_eqns (0, 1);
  int nfactor;
  bool kill_j = false;

  omega_copy_eqn (eq, &pb->eqs[e], pb->num_vars);

  for (k = pb->num_vars; k >= 0; k--)
    {
      eq->coef[k] = int_mod (eq->coef[k], factor);

      if (2 * eq->coef[k] >= factor)
	eq->coef[k] -= factor;
    }

  nfactor = eq->coef[j];

  if (omega_safe_var_p (pb, j) && !omega_wildcard_p (pb, j))
    {
      i = omega_add_new_wild_card (pb);
      eq->coef[pb->num_vars] = eq->coef[i];
      eq->coef[j] = 0;
      eq->coef[i] = -factor;
      kill_j = true;
    }
  else
    {
      eq->coef[j] = -factor;
      if (!omega_wildcard_p (pb, j))
	omega_name_wild_card (pb, j);
    }

  omega_substitute (pb, eq, j, nfactor);

  for (k = pb->num_vars; k >= 0; k--)
    pb->eqs[e].coef[k] = pb->eqs[e].coef[k] / factor;

  if (kill_j)
    omega_delete_variable (pb, j);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Mod-ing and normalizing produces:\n");
      omega_print_problem (dump_file, pb);
    }

  omega_free_eqns (eq, 1);
}

/* Multiplies by -1 inequality E.  */

void
omega_negate_geq (omega_pb pb, int e)
{
  int i;

  for (i = pb->num_vars; i >= 0; i--)
    pb->geqs[e].coef[i] *= -1;

  pb->geqs[e].coef[0]--;
  pb->geqs[e].touched = 1;
}

/* Returns OMEGA_TRUE when problem PB has a solution.  */

static enum omega_result
verify_omega_pb (omega_pb pb)
{
  enum omega_result result;
  int e;
  bool any_color = false;
  omega_pb tmp_problem = (omega_pb) xmalloc (sizeof (struct omega_pb));

  omega_copy_problem (tmp_problem, pb);
  tmp_problem->safe_vars = 0;
  tmp_problem->num_subs = 0;
  
  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (pb->geqs[e].color == omega_red)
      {
	any_color = true;
	break;
      }

  if (please_no_equalities_in_simplified_problems)
    any_color = true;

  if (any_color)
    original_problem = no_problem;
  else
    original_problem = pb;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "verifying problem");

      if (any_color)
	fprintf (dump_file, " (color mode)");

      fprintf (dump_file, " :\n");
      omega_print_problem (dump_file, pb);
    }

  result = omega_solve_problem (tmp_problem, omega_unknown);
  original_problem = no_problem;
  free (tmp_problem);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      if (result != omega_false)
	fprintf (dump_file, "verified problem\n");
      else
	fprintf (dump_file, "disproved problem\n");
      omega_print_problem (dump_file, pb);
    }

  return result;
}

/* Add a new equality to problem PB at last position E.  */

static void
adding_equality_constraint (omega_pb pb, int e)
{
  int e2, i, j;

  if (original_problem != no_problem && original_problem != pb
      && !conservative)
    {
      e2 = original_problem->num_eqs++;

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "adding equality constraint %d to outer problem\n", e2);
      omega_init_eqn_zero (&original_problem->eqs[e2],
			   original_problem->num_vars);

      for (i = pb->num_vars; i >= 1; i--)
	{
	  for (j = original_problem->num_vars; j >= 1; j--)
	    if (original_problem->var[j] == pb->var[i])
	      break;

	  if (j <= 0)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "retracting\n");
	      original_problem->num_eqs--;
	      return;
	    }
	  original_problem->eqs[e2].coef[j] = pb->eqs[e].coef[i];
	}

      original_problem->eqs[e2].coef[0] = pb->eqs[e].coef[0];

      if (dump_file && (dump_flags & TDF_DETAILS))
	omega_print_problem (dump_file, original_problem);
    }
}

static int *fast_lookup;
static int *fast_lookup_red;

typedef enum {
  normalize_false,
  normalize_uncoupled,
  normalize_coupled
} normalize_return_type;

/* Normalizes PB by removing redundant constraints.  Returns
   normalize_false when the constraints system has no solution,
   otherwise returns normalize_coupled or normalize_uncoupled.  */

static normalize_return_type
normalize_omega_problem (omega_pb pb)
{
  int e, i, j, k, n_vars;
  int coupled_subscripts = 0;

  n_vars = pb->num_vars;

  for (e = 0; e < pb->num_geqs; e++)
    {
      if (!pb->geqs[e].touched)
	{
	  if (!single_var_geq (&pb->geqs[e], n_vars))
	    coupled_subscripts = 1;
	}
      else
	{
	  int g, top_var, i0, hashCode;
	  int *p = &packing[0];

	  for (k = 1; k <= n_vars; k++)
	    if (pb->geqs[e].coef[k])
	      *(p++) = k;

	  top_var = (p - &packing[0]) - 1;

	  if (top_var == -1)
	    {
	      if (pb->geqs[e].coef[0] < 0)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      omega_print_geq (dump_file, pb, &pb->geqs[e]);
		      fprintf (dump_file, "\nequations have no solution \n");
		    }
		  return normalize_false;
		}

	      omega_delete_geq (pb, e, n_vars);
	      e--;
	      continue;
	    }
	  else if (top_var == 0)
	    {
	      int singlevar = packing[0];

	      g = pb->geqs[e].coef[singlevar];

	      if (g > 0)
		{
		  pb->geqs[e].coef[singlevar] = 1;
		  pb->geqs[e].key = singlevar;
		}
	      else
		{
		  g = -g;
		  pb->geqs[e].coef[singlevar] = -1;
		  pb->geqs[e].key = -singlevar;
		}

	      if (g > 1)
		pb->geqs[e].coef[0] = int_div (pb->geqs[e].coef[0], g);
	    }
	  else
	    {
	      int g2;

	      coupled_subscripts = 1;
	      i0 = top_var;
	      i = packing[i0--];
	      g = pb->geqs[e].coef[i];
	      hashCode = g * (i + 3);

	      if (g < 0)
		g = -g;

	      for (; i0 >= 0; i0--)
		{
		  int x;

		  i = packing[i0];
		  x = pb->geqs[e].coef[i];
		  hashCode = hashCode * KEY_MULT * (i + 3) + x;

		  if (x < 0)
		    x = -x;

		  if (x == 1)
		    {
		      g = 1;
		      i0--;
		      break;
		    }
		  else
		    g = gcd (x, g);
		}

	      for (; i0 >= 0; i0--)
		{
		  int x;
		  i = packing[i0];
		  x = pb->geqs[e].coef[i];
		  hashCode = hashCode * KEY_MULT * (i + 3) + x;
		}

	      if (g > 1)
		{
		  pb->geqs[e].coef[0] = int_div (pb->geqs[e].coef[0], g);
		  i0 = top_var;
		  i = packing[i0--];
		  pb->geqs[e].coef[i] = pb->geqs[e].coef[i] / g;
		  hashCode = pb->geqs[e].coef[i] * (i + 3);

		  for (; i0 >= 0; i0--)
		    {
		      i = packing[i0];
		      pb->geqs[e].coef[i] = pb->geqs[e].coef[i] / g;
		      hashCode =
			hashCode * KEY_MULT * (i + 3) + pb->geqs[e].coef[i];
		    }
		}

	      g2 = abs (hashCode);

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "Hash code = %d, eqn = ", hashCode);
		  omega_print_geq (dump_file, pb, &pb->geqs[e]);
		  fprintf (dump_file, "\n");
		}

	      j = g2 % HASH_TABLE_SIZE;

	      do {
		eqn proto = &(hash_master[j]);

		if (proto->touched == g2)
		  {
		    if (proto->coef[0] == top_var)
		      {
			if (hashCode >= 0)
			  for (i0 = top_var; i0 >= 0; i0--)
			    {
			      i = packing[i0];

			      if (pb->geqs[e].coef[i] != proto->coef[i])
				break;
			    }
			else
			  for (i0 = top_var; i0 >= 0; i0--)
			    {
			      i = packing[i0];

			      if (pb->geqs[e].coef[i] != -proto->coef[i])
				break;
			    }

			if (i0 < 0)
			  {
			    if (hashCode >= 0)
			      pb->geqs[e].key = proto->key;
			    else
			      pb->geqs[e].key = -proto->key;
			    break;
			  }
		      }
		  }
		else if (proto->touched < 0)
		  {
		    omega_init_eqn_zero (proto, pb->num_vars);
		    if (hashCode >= 0)
		      for (i0 = top_var; i0 >= 0; i0--)
			{
			  i = packing[i0];
			  proto->coef[i] = pb->geqs[e].coef[i];
			}
		    else
		      for (i0 = top_var; i0 >= 0; i0--)
			{
			  i = packing[i0];
			  proto->coef[i] = -pb->geqs[e].coef[i];
			}

		    proto->coef[0] = top_var;
		    proto->touched = g2;

		    if (dump_file && (dump_flags & TDF_DETAILS))
		      fprintf (dump_file, " constraint key = %d\n",
			       next_key);

		    proto->key = next_key++;

		    /* Too many hash keys generated.  */
		    gcc_assert (proto->key <= MAX_KEYS);

		    if (hashCode >= 0)
		      pb->geqs[e].key = proto->key;
		    else
		      pb->geqs[e].key = -proto->key;

		    break;
		  }

		j = (j + 1) % HASH_TABLE_SIZE;
	      } while (1);
	    }

	  pb->geqs[e].touched = 0;
	}

      {
	int eKey = pb->geqs[e].key;
	int e2;
	if (e > 0)
	  {
	    int cTerm = pb->geqs[e].coef[0];
	    e2 = fast_lookup[MAX_KEYS - eKey];

	    if (e2 < e && pb->geqs[e2].key == -eKey
		&& pb->geqs[e2].color == omega_black)
	      {
		if (pb->geqs[e2].coef[0] < -cTerm)
		  {
		    if (dump_file && (dump_flags & TDF_DETAILS))
		      {
			omega_print_geq (dump_file, pb, &pb->geqs[e]);
			fprintf (dump_file, "\n");
			omega_print_geq (dump_file, pb, &pb->geqs[e2]);
			fprintf (dump_file,
				 "\nequations have no solution \n");
		      }
		    return normalize_false;
		  }

		if (pb->geqs[e2].coef[0] == -cTerm
		    && (create_color 
			|| pb->geqs[e].color == omega_black))
		  {
		    omega_copy_eqn (&pb->eqs[pb->num_eqs], &pb->geqs[e],
				    pb->num_vars);
		    if (pb->geqs[e].color == omega_black)
		      adding_equality_constraint (pb, pb->num_eqs);
		    pb->num_eqs++;
		    gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);
		  }
	      }

	    e2 = fast_lookup_red[MAX_KEYS - eKey];

	    if (e2 < e && pb->geqs[e2].key == -eKey
		&& pb->geqs[e2].color == omega_red)
	      {
		if (pb->geqs[e2].coef[0] < -cTerm)
		  {
		    if (dump_file && (dump_flags & TDF_DETAILS))
		      {
			omega_print_geq (dump_file, pb, &pb->geqs[e]);
			fprintf (dump_file, "\n");
			omega_print_geq (dump_file, pb, &pb->geqs[e2]);
			fprintf (dump_file,
				 "\nequations have no solution \n");
		      }
		    return normalize_false;
		  }

		if (pb->geqs[e2].coef[0] == -cTerm && create_color)
		  {
		    omega_copy_eqn (&pb->eqs[pb->num_eqs], &pb->geqs[e],
				    pb->num_vars);
		    pb->eqs[pb->num_eqs].color = omega_red;
		    pb->num_eqs++;
		    gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);
		  }
	      }

	    e2 = fast_lookup[MAX_KEYS + eKey];

	    if (e2 < e && pb->geqs[e2].key == eKey 
		&& pb->geqs[e2].color == omega_black)
	      {
		if (pb->geqs[e2].coef[0] > cTerm)
		  {
		    if (pb->geqs[e].color == omega_black)
		      {
			if (dump_file && (dump_flags & TDF_DETAILS))
			  {
			    fprintf (dump_file,
				     "Removing Redudant Equation: ");
			    omega_print_geq (dump_file, pb, &(pb->geqs[e2]));
			    fprintf (dump_file, "\n");
			    fprintf (dump_file,
				     "[a]      Made Redundant by: ");
			    omega_print_geq (dump_file, pb, &(pb->geqs[e]));
			    fprintf (dump_file, "\n");
			  }
			pb->geqs[e2].coef[0] = cTerm;
			omega_delete_geq (pb, e, n_vars);
			e--;
			continue;
		      }
		  }
		else
		  {
		    if (dump_file && (dump_flags & TDF_DETAILS))
		      {
			fprintf (dump_file, "Removing Redudant Equation: ");
			omega_print_geq (dump_file, pb, &(pb->geqs[e]));
			fprintf (dump_file, "\n");
			fprintf (dump_file, "[b]      Made Redundant by: ");
			omega_print_geq (dump_file, pb, &(pb->geqs[e2]));
			fprintf (dump_file, "\n");
		      }
		    omega_delete_geq (pb, e, n_vars);
		    e--;
		    continue;
		  }
	      }

	    e2 = fast_lookup_red[MAX_KEYS + eKey];

	    if (e2 < e && pb->geqs[e2].key == eKey
		&& pb->geqs[e2].color == omega_red)
	      {
		if (pb->geqs[e2].coef[0] >= cTerm)
		  {
		    if (dump_file && (dump_flags & TDF_DETAILS))
		      {
			fprintf (dump_file, "Removing Redudant Equation: ");
			omega_print_geq (dump_file, pb, &(pb->geqs[e2]));
			fprintf (dump_file, "\n");
			fprintf (dump_file, "[c]      Made Redundant by: ");
			omega_print_geq (dump_file, pb, &(pb->geqs[e]));
			fprintf (dump_file, "\n");
		      }
		    pb->geqs[e2].coef[0] = cTerm;
		    pb->geqs[e2].color = pb->geqs[e].color;
		  }
		else if (pb->geqs[e].color == omega_red)
		  {
		    if (dump_file && (dump_flags & TDF_DETAILS))
		      {
			fprintf (dump_file, "Removing Redudant Equation: ");
			omega_print_geq (dump_file, pb, &(pb->geqs[e]));
			fprintf (dump_file, "\n");
			fprintf (dump_file, "[d]      Made Redundant by: ");
			omega_print_geq (dump_file, pb, &(pb->geqs[e2]));
			fprintf (dump_file, "\n");
		      }
		  }
		omega_delete_geq (pb, e, n_vars);
		e--;
		continue;

	      }
	  }

	if (pb->geqs[e].color == omega_red)
	  fast_lookup_red[MAX_KEYS + eKey] = e;
	else
	  fast_lookup[MAX_KEYS + eKey] = e;
      }
    }

  create_color = false;
  return coupled_subscripts ? normalize_coupled : normalize_uncoupled;
}

/* Divide the coefficients of EQN by their gcd.  */

static inline void
divide_eqn_by_gcd (eqn eqn, int n_vars)
{
  int var, g = 0;

  for (var = n_vars; var >= 0; var--)
    g = gcd (abs (eqn->coef[var]), g);

  if (g)
    for (var = n_vars; var >= 0; var--)
      eqn->coef[var] = eqn->coef[var] / g;
}

/* Rewrite some non-safe variables in function of protected
   wildcard variables.  */

static void
cleanout_wildcards (omega_pb pb)
{
  int e, i, j;
  int n_vars = pb->num_vars;
  bool renormalize = false;

  for (e = pb->num_eqs - 1; e >= 0; e--)
    for (i = n_vars; !omega_safe_var_p (pb, i); i--)
      if (pb->eqs[e].coef[i] != 0)
	{
	  /* i is the last non-zero non-safe variable.  */

	  for (j = i - 1; !omega_safe_var_p (pb, j); j--)
	    if (pb->eqs[e].coef[j] != 0)
	      break;

	  /* j is the next non-zero non-safe variable, or points
	     to a safe variable: it is then a wildcard variable.  */

	  /* Clean it out.  */
	  if (omega_safe_var_p (pb, j))
	    {
	      eqn sub = &(pb->eqs[e]);
	      int c = pb->eqs[e].coef[i];
	      int a = abs (c);
	      int e2;

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file,
			   "Found a single wild card equality: ");
		  omega_print_eq (dump_file, pb, &pb->eqs[e]);
		  fprintf (dump_file, "\n");
		  omega_print_problem (dump_file, pb);
		}

	      for (e2 = pb->num_eqs - 1; e2 >= 0; e2--)
		if (e != e2 && pb->eqs[e2].coef[i]
		    && (pb->eqs[e2].color == omega_red
			|| (pb->eqs[e2].color == omega_black 
			    && pb->eqs[e].color == omega_black)))
		  {
		    eqn eqn = &(pb->eqs[e2]);
		    int var, k;

		    for (var = n_vars; var >= 0; var--)
		      eqn->coef[var] *= a;

		    k = eqn->coef[i];

		    for (var = n_vars; var >= 0; var--)
		      eqn->coef[var] -= sub->coef[var] * k / c;

		    eqn->coef[i] = 0;
		    divide_eqn_by_gcd (eqn, n_vars);
		  }

	      for (e2 = pb->num_geqs - 1; e2 >= 0; e2--)
		if (pb->geqs[e2].coef[i] 
		    && (pb->geqs[e2].color == omega_red
			|| (pb->eqs[e].color == omega_black 
			    && pb->geqs[e2].color == omega_black)))
		  {
		    eqn eqn = &(pb->geqs[e2]);
		    int var, k;

		    for (var = n_vars; var >= 0; var--)
		      eqn->coef[var] *= a;

		    k = eqn->coef[i];

		    for (var = n_vars; var >= 0; var--)
		      eqn->coef[var] -= sub->coef[var] * k / c;

		    eqn->coef[i] = 0;
		    eqn->touched = 1;
		    renormalize = true;
		  }

	      for (e2 = pb->num_subs - 1; e2 >= 0; e2--)
		if (pb->subs[e2].coef[i] 
		    && (pb->subs[e2].color == omega_red
			|| (pb->subs[e2].color == omega_black 
			    && pb->eqs[e].color == omega_black)))
		  {
		    eqn eqn = &(pb->subs[e2]);
		    int var, k;

		    for (var = n_vars; var >= 0; var--)
		      eqn->coef[var] *= a;

		    k = eqn->coef[i];

		    for (var = n_vars; var >= 0; var--)
		      eqn->coef[var] -= sub->coef[var] * k / c;

		    eqn->coef[i] = 0;
		    divide_eqn_by_gcd (eqn, n_vars);
		  }

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "cleaned-out wildcard: ");
		  omega_print_problem (dump_file, pb);
		}
	      break;
	    }
	}

  if (renormalize)
    normalize_omega_problem (pb);
}

/* Swap values contained in I and J.  */

static inline void
swap (int *i, int *j)
{
  int tmp;
  tmp = *i;
  *i = *j;
  *j = tmp;
}

/* Swap values contained in I and J.  */

static inline void
bswap (bool *i, bool *j)
{
  bool tmp;
  tmp = *i;
  *i = *j;
  *j = tmp;
}

/* Helper function.  UNPROTECT might be NULL.  */

static inline void
omega_unprotect_1 (omega_pb pb, int *idx, bool *unprotect)
{
  if (*idx < pb->safe_vars)
    {
      int e, j = pb->safe_vars;

      for (e = pb->num_geqs - 1; e >= 0; e--)
	{
	  pb->geqs[e].touched = 1;
	  swap (&pb->geqs[e].coef[*idx], &pb->geqs[e].coef[j]);
	}

      for (e = pb->num_eqs - 1; e >= 0; e--)
	swap (&pb->eqs[e].coef[*idx], &pb->eqs[e].coef[j]);

      for (e = pb->num_subs - 1; e >= 0; e--)
	swap (&pb->subs[e].coef[*idx], &pb->subs[e].coef[j]);

      if (unprotect)
	bswap (&unprotect[*idx], &unprotect[j]);

      swap (&pb->var[*idx], &pb->var[j]);
      pb->forwarding_address[pb->var[*idx]] = *idx;
      pb->forwarding_address[pb->var[j]] = j;
      (*idx)--;
    }

  pb->safe_vars--;
}

/* During the Fourier-Motzkin elimination some variables are
   substituted with other variables.  This function resurrects the
   substituted variables.  */

static void
resurrect_subs (omega_pb pb)
{
  if (pb->num_subs > 0 
      && please_no_equalities_in_simplified_problems == 0)
    {
      int i, e, n, m;

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
		   "problem reduced, bringing variables back to life\n");
	  omega_print_problem (dump_file, pb);
	}

      for (i = 1; omega_safe_var_p (pb, i); i++)
	if (omega_wildcard_p (pb, i))
	  omega_unprotect_1 (pb, &i, NULL);

      m = pb->num_subs;
      n = MAX (pb->num_vars, pb->safe_vars + m);

      for (e = pb->num_geqs - 1; e >= 0; e--)
	if (single_var_geq (&pb->geqs[e], pb->num_vars))
	  {
	    if (!omega_safe_var_p (pb, abs (pb->geqs[e].key)))
	      pb->geqs[e].key += (pb->geqs[e].key > 0 ? m : -m);
	  }
	else
	  {
	    pb->geqs[e].touched = 1;
	    pb->geqs[e].key = 0;
	  }

      for (i = pb->num_vars; !omega_safe_var_p (pb, i); i--)
	{
	  pb->var[i + m] = pb->var[i];

	  for (e = pb->num_geqs - 1; e >= 0; e--)
	    pb->geqs[e].coef[i + m] = pb->geqs[e].coef[i];

	  for (e = pb->num_eqs - 1; e >= 0; e--)
	    pb->eqs[e].coef[i + m] = pb->eqs[e].coef[i];

	  for (e = pb->num_subs - 1; e >= 0; e--)
	    pb->subs[e].coef[i + m] = pb->subs[e].coef[i];
	}

      for (i = pb->safe_vars + m; !omega_safe_var_p (pb, i); i--)
	{
	  for (e = pb->num_geqs - 1; e >= 0; e--)
	    pb->geqs[e].coef[i] = 0;

	  for (e = pb->num_eqs - 1; e >= 0; e--)
	    pb->eqs[e].coef[i] = 0;

	  for (e = pb->num_subs - 1; e >= 0; e--)
	    pb->subs[e].coef[i] = 0;
	}

      pb->num_vars += m;

      for (e = pb->num_subs - 1; e >= 0; e--)
	{
	  pb->var[pb->safe_vars + 1 + e] = pb->subs[e].key;
	  omega_copy_eqn (&(pb->eqs[pb->num_eqs]), &(pb->subs[e]),
			  pb->num_vars);
	  pb->eqs[pb->num_eqs].coef[pb->safe_vars + 1 + e] = -1;
	  pb->eqs[pb->num_eqs].color = omega_black;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "brought back: ");
	      omega_print_eq (dump_file, pb, &pb->eqs[pb->num_eqs]);
	      fprintf (dump_file, "\n");
	    }

	  pb->num_eqs++;
	  gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);
	}

      pb->safe_vars += m;
      pb->num_subs = 0;

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "variables brought back to life\n");
	  omega_print_problem (dump_file, pb);
	}

      cleanout_wildcards (pb);
    }
}

static inline bool
implies (unsigned int a, unsigned int b)
{
  return (a == (a & b));
}

/* Eliminate redundant equations in PB.  When EXPENSIVE is true, an
   extra step is performed.  Returns omega_false when there exist no
   solution, omega_true otherwise.  */

enum omega_result
omega_eliminate_redundant (omega_pb pb, bool expensive)
{
  int c, e, e1, e2, e3, p, q, i, k, alpha, alpha1, alpha2, alpha3;
  bool *is_dead = (bool *) xmalloc (OMEGA_MAX_GEQS * sizeof (bool));
  omega_pb tmp_problem;

  /* {P,Z,N}EQS = {Positive,Zero,Negative} Equations.  */
  unsigned int *peqs = (unsigned int *) xmalloc (OMEGA_MAX_GEQS
						 * sizeof (unsigned int));
  unsigned int *zeqs = (unsigned int *) xmalloc (OMEGA_MAX_GEQS
						 * sizeof (unsigned int));
  unsigned int *neqs = (unsigned int *) xmalloc (OMEGA_MAX_GEQS
						 * sizeof (unsigned int));

  /* PP = Possible Positives, PZ = Possible Zeros, PN = Possible Negatives */
  unsigned int pp, pz, pn;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "in eliminate Redudant:\n");
      omega_print_problem (dump_file, pb);
    }

  for (e = pb->num_geqs - 1; e >= 0; e--)
    {
      int tmp = 1;

      is_dead[e] = false;
      peqs[e] = zeqs[e] = neqs[e] = 0;

      for (i = pb->num_vars; i >= 1; i--)
	{
	  if (pb->geqs[e].coef[i] > 0)
	    peqs[e] |= tmp;
	  else if (pb->geqs[e].coef[i] < 0)
	    neqs[e] |= tmp;
	  else
	    zeqs[e] |= tmp;

	  tmp <<= 1;
	}
    }


  for (e1 = pb->num_geqs - 1; e1 >= 0; e1--)
    if (!is_dead[e1])
      for (e2 = e1 - 1; e2 >= 0; e2--)
	if (!is_dead[e2])
	  {
	    for (p = pb->num_vars; p > 1; p--)
	      for (q = p - 1; q > 0; q--)
		if ((alpha = pb->geqs[e1].coef[p] * pb->geqs[e2].coef[q]
		     - pb->geqs[e2].coef[p] * pb->geqs[e1].coef[q]) != 0)
		  goto foundPQ;

	    continue;

	  foundPQ:
	    pz = ((zeqs[e1] & zeqs[e2]) | (peqs[e1] & neqs[e2]) 
		  | (neqs[e1] & peqs[e2]));
	    pp = peqs[e1] | peqs[e2];
	    pn = neqs[e1] | neqs[e2];

	    for (e3 = pb->num_geqs - 1; e3 >= 0; e3--)
	      if (e3 != e1 && e3 != e2)
		{
		  if (!implies (zeqs[e3], pz))
		    goto nextE3;

		  alpha1 = (pb->geqs[e2].coef[q] * pb->geqs[e3].coef[p]
			    - pb->geqs[e2].coef[p] * pb->geqs[e3].coef[q]);
		  alpha2 = -(pb->geqs[e1].coef[q] * pb->geqs[e3].coef[p]
			     - pb->geqs[e1].coef[p] * pb->geqs[e3].coef[q]);
		  alpha3 = alpha;

		  if (alpha1 * alpha2 <= 0)
		    goto nextE3;

		  if (alpha1 < 0)
		    {
		      alpha1 = -alpha1;
		      alpha2 = -alpha2;
		      alpha3 = -alpha3;
		    }

		  if (alpha3 > 0)
		    {
		      /* Trying to prove e3 is redundant.  */
		      if (!implies (peqs[e3], pp) 
			  || !implies (neqs[e3], pn))
			goto nextE3;

		      if (pb->geqs[e3].color == omega_black
			  && (pb->geqs[e1].color == omega_red
			      || pb->geqs[e2].color == omega_red))
			goto nextE3;

		      for (k = pb->num_vars; k >= 1; k--)
			if (alpha3 * pb->geqs[e3].coef[k]
			    != (alpha1 * pb->geqs[e1].coef[k]
				+ alpha2 * pb->geqs[e2].coef[k]))
			  goto nextE3;

		      c = (alpha1 * pb->geqs[e1].coef[0]
			   + alpha2 * pb->geqs[e2].coef[0]);

		      if (c < alpha3 * (pb->geqs[e3].coef[0] + 1))
			{
			  if (dump_file && (dump_flags & TDF_DETAILS))
			    {
			      fprintf (dump_file,
				       "found redundant inequality\n");
			      fprintf (dump_file,
				       "alpha1, alpha2, alpha3 = %d,%d,%d\n",
				       alpha1, alpha2, alpha3);

			      omega_print_geq (dump_file, pb, &(pb->geqs[e1]));
			      fprintf (dump_file, "\n");
			      omega_print_geq (dump_file, pb, &(pb->geqs[e2]));
			      fprintf (dump_file, "\n=> ");
			      omega_print_geq (dump_file, pb, &(pb->geqs[e3]));
			      fprintf (dump_file, "\n\n");
			    }

			  is_dead[e3] = true;
			}
		    }
		  else
		    {
		      /* Trying to prove e3 <= 0 and therefore e3 = 0,
		        or trying to prove e3 < 0, and therefore the
		        problem has no solutions.  */
		      if (!implies (peqs[e3], pn) 
			  || !implies (neqs[e3], pp))
			goto nextE3;

		      if (pb->geqs[e1].color == omega_red
			  || pb->geqs[e2].color == omega_red
			  || pb->geqs[e3].color == omega_red)
			goto nextE3;

		      alpha3 = alpha3;
		      /* verify alpha1*v1+alpha2*v2 = alpha3*v3 */
		      for (k = pb->num_vars; k >= 1; k--)
			if (alpha3 * pb->geqs[e3].coef[k]
			    != (alpha1 * pb->geqs[e1].coef[k]
				+ alpha2 * pb->geqs[e2].coef[k]))
			  goto nextE3;

		      c = (alpha1 * pb->geqs[e1].coef[0]
			   + alpha2 * pb->geqs[e2].coef[0]);

		      if (c < alpha3 * (pb->geqs[e3].coef[0]))
			{
			  /* We just proved e3 < 0, so no solutions exist.  */
			  if (dump_file && (dump_flags & TDF_DETAILS))
			    {
			      fprintf (dump_file,
				       "found implied over tight inequality\n");
			      fprintf (dump_file,
				       "alpha1, alpha2, alpha3 = %d,%d,%d\n",
				       alpha1, alpha2, -alpha3);
			      omega_print_geq (dump_file, pb, &(pb->geqs[e1]));
			      fprintf (dump_file, "\n");
			      omega_print_geq (dump_file, pb, &(pb->geqs[e2]));
			      fprintf (dump_file, "\n=> not ");
			      omega_print_geq (dump_file, pb, &(pb->geqs[e3]));
			      fprintf (dump_file, "\n\n");
			    }
			  return omega_false;
			}
		      else if (c < alpha3 * (pb->geqs[e3].coef[0] - 1))
			{
			  /* We just proved that e3 <=0, so e3 = 0.  */
			  if (dump_file && (dump_flags & TDF_DETAILS))
			    {
			      fprintf (dump_file,
				       "found implied tight inequality\n");
			      fprintf (dump_file,
				       "alpha1, alpha2, alpha3 = %d,%d,%d\n",
				       alpha1, alpha2, -alpha3);
			      omega_print_geq (dump_file, pb, &(pb->geqs[e1]));
			      fprintf (dump_file, "\n");
			      omega_print_geq (dump_file, pb, &(pb->geqs[e2]));
			      fprintf (dump_file, "\n=> inverse ");
			      omega_print_geq (dump_file, pb, &(pb->geqs[e3]));
			      fprintf (dump_file, "\n\n");
			    }

			  omega_copy_eqn (&pb->eqs[pb->num_eqs++], 
					  &pb->geqs[e3], pb->num_vars);
			  gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);
			  adding_equality_constraint (pb, pb->num_eqs - 1);
			  is_dead[e3] = true;
			}
		    }
		nextE3:;
		}
	  }

  /* Delete the inequalities that were marked as dead.  */
  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (is_dead[e])
      omega_delete_geq (pb, e, pb->num_vars);

  if (!expensive)
    goto eliminate_redundant_done;

  tmp_problem = (omega_pb) xmalloc (sizeof (struct omega_pb));
  conservative++;

  for (e = pb->num_geqs - 1; e >= 0; e--)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
		   "checking equation %d to see if it is redundant: ", e);
	  omega_print_geq (dump_file, pb, &(pb->geqs[e]));
	  fprintf (dump_file, "\n");
	}

      omega_copy_problem (tmp_problem, pb);
      omega_negate_geq (tmp_problem, e);
      tmp_problem->safe_vars = 0;
      tmp_problem->variables_freed = false;

      if (omega_solve_problem (tmp_problem, omega_false) == omega_false)
	omega_delete_geq (pb, e, pb->num_vars);
    }

  free (tmp_problem);
  conservative--;

  if (!omega_reduce_with_subs)
    {
      resurrect_subs (pb);
      gcc_assert (please_no_equalities_in_simplified_problems
		  || pb->num_subs == 0);
    }

 eliminate_redundant_done:
  free (is_dead);
  free (peqs);
  free (zeqs);
  free (neqs);
  return omega_true;
}

/* For each inequality that has coefficients bigger than 20, try to
   create a new constraint that cannot be derived from the original
   constraint and that has smaller coefficients.  Add the new
   constraint at the end of geqs.  Return the number of inequalities
   that have been added to PB.  */

static int
smooth_weird_equations (omega_pb pb)
{
  int e1, e2, e3, p, q, k, alpha, alpha1, alpha2, alpha3;
  int c;
  int v;
  int result = 0;

  for (e1 = pb->num_geqs - 1; e1 >= 0; e1--)
    if (pb->geqs[e1].color == omega_black)
      {
	int g = 999999;

	for (v = pb->num_vars; v >= 1; v--)
	  if (pb->geqs[e1].coef[v] != 0 && abs (pb->geqs[e1].coef[v]) < g)
	    g = abs (pb->geqs[e1].coef[v]);

	/* Magic number.  */
	if (g > 20)
	  {
	    e3 = pb->num_geqs;

	    for (v = pb->num_vars; v >= 1; v--)
	      pb->geqs[e3].coef[v] = int_div (6 * pb->geqs[e1].coef[v] + g / 2,
					      g);

	    pb->geqs[e3].color = omega_black;
	    pb->geqs[e3].touched = 1;
	    /* Magic number.  */
	    pb->geqs[e3].coef[0] = 9997;

	    if (dump_file && (dump_flags & TDF_DETAILS))
	      {
		fprintf (dump_file, "Checking to see if we can derive: ");
		omega_print_geq (dump_file, pb, &pb->geqs[e3]);
		fprintf (dump_file, "\n from: ");
		omega_print_geq (dump_file, pb, &pb->geqs[e1]);
		fprintf (dump_file, "\n");
	      }

	    for (e2 = pb->num_geqs - 1; e2 >= 0; e2--)
	      if (e1 != e2 && pb->geqs[e2].color == omega_black)
		{
		  for (p = pb->num_vars; p > 1; p--)
		    {
		      for (q = p - 1; q > 0; q--)
			{
			  alpha =
			    (pb->geqs[e1].coef[p] * pb->geqs[e2].coef[q] -
			     pb->geqs[e2].coef[p] * pb->geqs[e1].coef[q]);
			  if (alpha != 0)
			    goto foundPQ;
			}
		    }
		  continue;

		foundPQ:

		  alpha1 = (pb->geqs[e2].coef[q] * pb->geqs[e3].coef[p]
			    - pb->geqs[e2].coef[p] * pb->geqs[e3].coef[q]);
		  alpha2 = -(pb->geqs[e1].coef[q] * pb->geqs[e3].coef[p]
			     - pb->geqs[e1].coef[p] * pb->geqs[e3].coef[q]);
		  alpha3 = alpha;

		  if (alpha1 * alpha2 <= 0)
		    continue;

		  if (alpha1 < 0)
		    {
		      alpha1 = -alpha1;
		      alpha2 = -alpha2;
		      alpha3 = -alpha3;
		    }

		  if (alpha3 > 0)
		    {
		      /* Try to prove e3 is redundant: verify
			 alpha1*v1 + alpha2*v2 = alpha3*v3.  */
		      for (k = pb->num_vars; k >= 1; k--)
			if (alpha3 * pb->geqs[e3].coef[k]
			    != (alpha1 * pb->geqs[e1].coef[k]
				+ alpha2 * pb->geqs[e2].coef[k]))
			  goto nextE2;

		      c = alpha1 * pb->geqs[e1].coef[0]
			+ alpha2 * pb->geqs[e2].coef[0];

		      if (c < alpha3 * (pb->geqs[e3].coef[0] + 1))
			pb->geqs[e3].coef[0] = int_div (c, alpha3);
		    }
		nextE2:;
		}

	    if (pb->geqs[e3].coef[0] < 9997)
	      {
		result++;
		pb->num_geqs++;

		if (dump_file && (dump_flags & TDF_DETAILS))
		  {
		    fprintf (dump_file,
			     "Smoothing wierd equations; adding:\n");
		    omega_print_geq (dump_file, pb, &pb->geqs[e3]);
		    fprintf (dump_file, "\nto:\n");
		    omega_print_problem (dump_file, pb);
		    fprintf (dump_file, "\n\n");
		  }
	      }
	  }
      }
  return result;
}

/* Replace tuples of inequalities, that define upper and lower half
   spaces, with an equation.  */

static void
coalesce (omega_pb pb)
{
  int e, e2;
  int colors = 0;
  bool *is_dead = (bool *) xmalloc (OMEGA_MAX_GEQS * sizeof (bool));
  int found_something = 0;

  for (e = 0; e < pb->num_geqs; e++)
    if (pb->geqs[e].color == omega_red)
      colors++;

  if (colors < 2)
    return;

  for (e = 0; e < pb->num_geqs; e++)
    is_dead[e] = false;

  for (e = 0; e < pb->num_geqs; e++)
    if (pb->geqs[e].color == omega_red 
	&& !pb->geqs[e].touched)
      for (e2 = e + 1; e2 < pb->num_geqs; e2++)
	if (!pb->geqs[e2].touched 
	    && pb->geqs[e].key == -pb->geqs[e2].key
	    && pb->geqs[e].coef[0] == -pb->geqs[e2].coef[0] 
	    && pb->geqs[e2].color == omega_red)
	  {
	    omega_copy_eqn (&pb->eqs[pb->num_eqs++], &pb->geqs[e],
			    pb->num_vars);
	    gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);
	    found_something++;
	    is_dead[e] = true;
	    is_dead[e2] = true;
	  }

  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (is_dead[e])
      omega_delete_geq (pb, e, pb->num_vars);

  if (dump_file && (dump_flags & TDF_DETAILS) && found_something)
    {
      fprintf (dump_file, "Coalesced pb->geqs into %d EQ's:\n",
	       found_something);
      omega_print_problem (dump_file, pb);
    }

  free (is_dead);
}

/* Eliminate redundant inequalities from PB.  When ELIMINATE_ALL is
   true, continue to eliminate all redundant inequalities.  */

void
omega_eliminate_red (omega_pb pb, bool eliminate_all)
{
  int e, e2, e3, i, j, k, a, alpha1, alpha2;
  int c = 0;
  bool *is_dead = (bool *) xmalloc (OMEGA_MAX_GEQS * sizeof (bool));
  int dead_count = 0;
  int red_found;
  omega_pb tmp_problem;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "in eliminate RED:\n");
      omega_print_problem (dump_file, pb);
    }

  if (pb->num_eqs > 0)
    omega_simplify_problem (pb);

  for (e = pb->num_geqs - 1; e >= 0; e--)
    is_dead[e] = false;

  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (pb->geqs[e].color == omega_black && !is_dead[e])
      for (e2 = e - 1; e2 >= 0; e2--)
	if (pb->geqs[e2].color == omega_black 
	    && !is_dead[e2])
	  {
	    a = 0;

	    for (i = pb->num_vars; i > 1; i--)
	      for (j = i - 1; j > 0; j--)
		if ((a = (pb->geqs[e].coef[i] * pb->geqs[e2].coef[j]
			  - pb->geqs[e2].coef[i] * pb->geqs[e].coef[j])) != 0)
		  goto found_pair;

	    continue;

	  found_pair:
	    if (dump_file && (dump_flags & TDF_DETAILS))
	      {
		fprintf (dump_file,
			 "found two equations to combine, i = %s, ",
			 omega_variable_to_str (pb, i));
		fprintf (dump_file, "j = %s, alpha = %d\n",
			 omega_variable_to_str (pb, j), a);
		omega_print_geq (dump_file, pb, &(pb->geqs[e]));
		fprintf (dump_file, "\n");
		omega_print_geq (dump_file, pb, &(pb->geqs[e2]));
		fprintf (dump_file, "\n");
	      }

	    for (e3 = pb->num_geqs - 1; e3 >= 0; e3--)
	      if (pb->geqs[e3].color == omega_red)
		{
		  alpha1 = (pb->geqs[e2].coef[j] * pb->geqs[e3].coef[i] 
			    - pb->geqs[e2].coef[i] * pb->geqs[e3].coef[j]);
		  alpha2 = -(pb->geqs[e].coef[j] * pb->geqs[e3].coef[i]
			     - pb->geqs[e].coef[i] * pb->geqs[e3].coef[j]);

		  if ((a > 0 && alpha1 > 0 && alpha2 > 0)
		      || (a < 0 && alpha1 < 0 && alpha2 < 0))
		    {
		      if (dump_file && (dump_flags & TDF_DETAILS))
			{
			  fprintf (dump_file,
				   "alpha1 = %d, alpha2 = %d;"
				   "comparing against: ",
				   alpha1, alpha2);
			  omega_print_geq (dump_file, pb, &(pb->geqs[e3]));
			  fprintf (dump_file, "\n");
			}

		      for (k = pb->num_vars; k >= 0; k--)
			{
			  c = (alpha1 * pb->geqs[e].coef[k] 
			       + alpha2 * pb->geqs[e2].coef[k]);

			  if (c != a * pb->geqs[e3].coef[k])
			    break;

			  if (dump_file && (dump_flags & TDF_DETAILS) && k > 0)
			    fprintf (dump_file, " %s: %d, %d\n",
				     omega_variable_to_str (pb, k), c,
				     a * pb->geqs[e3].coef[k]);
			}

		      if (k < 0
			  || (k == 0 &&
			      ((a > 0 && c < a * pb->geqs[e3].coef[k])
			       || (a < 0 && c > a * pb->geqs[e3].coef[k]))))
			{
			  if (dump_file && (dump_flags & TDF_DETAILS))
			    {
			      dead_count++;
			      fprintf (dump_file,
				       "red equation#%d is dead "
				       "(%d dead so far, %d remain)\n",
				       e3, dead_count,
				       pb->num_geqs - dead_count);
			      omega_print_geq (dump_file, pb, &(pb->geqs[e]));
			      fprintf (dump_file, "\n");
			      omega_print_geq (dump_file, pb, &(pb->geqs[e2]));
			      fprintf (dump_file, "\n");
			      omega_print_geq (dump_file, pb, &(pb->geqs[e3]));
			      fprintf (dump_file, "\n");
			    }
			  is_dead[e3] = true;
			}
		    }
		}
	  }

  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (is_dead[e])
      omega_delete_geq (pb, e, pb->num_vars);

  free (is_dead);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "in eliminate RED, easy tests done:\n");
      omega_print_problem (dump_file, pb);
    }

  for (red_found = 0, e = pb->num_geqs - 1; e >= 0; e--)
    if (pb->geqs[e].color == omega_red)
      red_found = 1;

  if (!red_found)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "fast checks worked\n");

      if (!omega_reduce_with_subs)
	gcc_assert (please_no_equalities_in_simplified_problems
		    || pb->num_subs == 0);

      return;
    }

  if (!omega_verify_simplification)
    {
      if (!verify_omega_pb (pb))
	return;
    }

  conservative++;
  tmp_problem = (omega_pb) xmalloc (sizeof (struct omega_pb));

  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (pb->geqs[e].color == omega_red)
      {
	if (dump_file && (dump_flags & TDF_DETAILS))
	  {
	    fprintf (dump_file,
		     "checking equation %d to see if it is redundant: ", e);
	    omega_print_geq (dump_file, pb, &(pb->geqs[e]));
	    fprintf (dump_file, "\n");
	  }

	omega_copy_problem (tmp_problem, pb);
	omega_negate_geq (tmp_problem, e);
	tmp_problem->safe_vars = 0;
	tmp_problem->variables_freed = false;
	tmp_problem->num_subs = 0;

	if (omega_solve_problem (tmp_problem, omega_false) == omega_false)
	  {
	    if (dump_file && (dump_flags & TDF_DETAILS))
	      fprintf (dump_file, "it is redundant\n");
	    omega_delete_geq (pb, e, pb->num_vars);
	  }
	else
	  {
	    if (dump_file && (dump_flags & TDF_DETAILS))
	      fprintf (dump_file, "it is not redundant\n");

	    if (!eliminate_all)
	      {
		if (dump_file && (dump_flags & TDF_DETAILS))
		  fprintf (dump_file, "no need to check other red equations\n");
		break;
	      }
	  }
      }

  conservative--;
  free (tmp_problem);
  /* omega_simplify_problem (pb); */

  if (!omega_reduce_with_subs)
    gcc_assert (please_no_equalities_in_simplified_problems
		|| pb->num_subs == 0);
}

/* Transform some wildcard variables to non-safe variables.  */

static void
chain_unprotect (omega_pb pb)
{
  int i, e;
  bool *unprotect = (bool *) xmalloc (OMEGA_MAX_VARS * sizeof (bool));

  for (i = 1; omega_safe_var_p (pb, i); i++)
    {
      unprotect[i] = omega_wildcard_p (pb, i);

      for (e = pb->num_subs - 1; e >= 0; e--)
	if (pb->subs[e].coef[i])
	  unprotect[i] = false;
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Doing chain reaction unprotection\n");
      omega_print_problem (dump_file, pb);

      for (i = 1; omega_safe_var_p (pb, i); i++)
	if (unprotect[i])
	  fprintf (dump_file, "unprotecting %s\n",
		   omega_variable_to_str (pb, i));
    }

  for (i = 1; omega_safe_var_p (pb, i); i++)
    if (unprotect[i])
      omega_unprotect_1 (pb, &i, unprotect);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "After chain reactions\n");
      omega_print_problem (dump_file, pb);
    }

  free (unprotect);
}

/* Reduce problem PB.  */

static void
omega_problem_reduced (omega_pb pb)
{
  if (omega_verify_simplification)
    {
      int result;
      if (in_approximate_mode)
	result = 1;
      else
	result = verify_omega_pb (pb);
      if (!result)
	return;
      if (pb->num_eqs > 0)
	do_it_again = true;
    }

  /*
    #ifdef eliminateRedundantConstraints
    if (!omega_eliminate_redundant (pb, 1))
    return;
    #endif
  */

  omega_found_reduction = omega_true;

  if (!please_no_equalities_in_simplified_problems)
    coalesce (pb);

  if (omega_reduce_with_subs || please_no_equalities_in_simplified_problems)
    chain_unprotect (pb);
  else
    resurrect_subs (pb);

  if (!return_single_result)
    {
      int i;

      for (i = 1; omega_safe_var_p (pb, i); i++)
	pb->forwarding_address[pb->var[i]] = i;

      for (i = 0; i < pb->num_subs; i++)
	pb->forwarding_address[pb->subs[i].key] = -i - 1;

      (*omega_when_reduced) (pb);
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "-------------------------------------------\n");
      fprintf (dump_file, "problem reduced:\n");
      omega_print_problem (dump_file, pb);
      fprintf (dump_file, "-------------------------------------------\n");
    }
}

/* Eliminates all the free variables for problem PB, that is all the
   variables from FV to PB->NUM_VARS.  */

static void
omega_free_eliminations (omega_pb pb, int fv)
{
  bool try_again = true;
  int i, e, e2;
  int n_vars = pb->num_vars;

  while (try_again)
    {
      try_again = false;

      for (i = n_vars; i > fv; i--)
	{
	  for (e = pb->num_geqs - 1; e >= 0; e--)
	    if (pb->geqs[e].coef[i])
	      break;

	  if (e < 0)
	    e2 = e;
	  else if (pb->geqs[e].coef[i] > 0)
	    {
	      for (e2 = e - 1; e2 >= 0; e2--)
		if (pb->geqs[e2].coef[i] < 0)
		  break;
	    }
	  else
	    {
	      for (e2 = e - 1; e2 >= 0; e2--)
		if (pb->geqs[e2].coef[i] > 0)
		  break;
	    }

	  if (e2 < 0)
	    {
	      int e3;
	      for (e3 = pb->num_subs - 1; e3 >= 0; e3--)
		if (pb->subs[e3].coef[i])
		  break;

	      if (e3 >= 0)
		continue;

	      for (e3 = pb->num_eqs - 1; e3 >= 0; e3--)
		if (pb->eqs[e3].coef[i])
		  break;

	      if (e3 >= 0)
		continue;

	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "a free elimination of %s\n",
			 omega_variable_to_str (pb, i));

	      if (e >= 0)
		{
		  omega_delete_geq (pb, e, n_vars);

		  for (e--; e >= 0; e--)
		    if (pb->geqs[e].coef[i])
		      omega_delete_geq (pb, e, n_vars);

		  try_again = (i < n_vars);
		}

	      omega_delete_variable (pb, i);
	      n_vars = pb->num_vars;
	    }
	}
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nafter free eliminations:\n");
      omega_print_problem (dump_file, pb);
      fprintf (dump_file, "\n");
    }
}

/* Do free red eliminations.  */

static void
free_red_eliminations (omega_pb pb)
{
  bool try_again = true;
  int i, e, e2;
  int n_vars = pb->num_vars;
  bool *is_red_var = (bool *) xmalloc (OMEGA_MAX_VARS * sizeof (bool));
  bool *is_dead_var = (bool *) xmalloc (OMEGA_MAX_VARS * sizeof (bool));
  bool *is_dead_geq = (bool *) xmalloc (OMEGA_MAX_GEQS * sizeof (bool));

  for (i = n_vars; i > 0; i--)
    {
      is_red_var[i] = false;
      is_dead_var[i] = false;
    }

  for (e = pb->num_geqs - 1; e >= 0; e--)
    {
      is_dead_geq[e] = false;

      if (pb->geqs[e].color == omega_red)
	for (i = n_vars; i > 0; i--)
	  if (pb->geqs[e].coef[i] != 0)
	    is_red_var[i] = true;
    }

  while (try_again)
    {
      try_again = false;
      for (i = n_vars; i > 0; i--)
	if (!is_red_var[i] && !is_dead_var[i])
	  {
	    for (e = pb->num_geqs - 1; e >= 0; e--)
	      if (!is_dead_geq[e] && pb->geqs[e].coef[i])
		break;

	    if (e < 0)
	      e2 = e;
	    else if (pb->geqs[e].coef[i] > 0)
	      {
		for (e2 = e - 1; e2 >= 0; e2--)
		  if (!is_dead_geq[e2] && pb->geqs[e2].coef[i] < 0)
		    break;
	      }
	    else
	      {
		for (e2 = e - 1; e2 >= 0; e2--)
		  if (!is_dead_geq[e2] && pb->geqs[e2].coef[i] > 0)
		    break;
	      }

	    if (e2 < 0)
	      {
		int e3;
		for (e3 = pb->num_subs - 1; e3 >= 0; e3--)
		  if (pb->subs[e3].coef[i])
		    break;

		if (e3 >= 0)
		  continue;

		for (e3 = pb->num_eqs - 1; e3 >= 0; e3--)
		  if (pb->eqs[e3].coef[i])
		    break;

		if (e3 >= 0)
		  continue;

		if (dump_file && (dump_flags & TDF_DETAILS))
		  fprintf (dump_file, "a free red elimination of %s\n",
			   omega_variable_to_str (pb, i));

		for (; e >= 0; e--)
		  if (pb->geqs[e].coef[i])
		    is_dead_geq[e] = true;

		try_again = true;
		is_dead_var[i] = true;
	      }
	  }
    }

  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (is_dead_geq[e])
      omega_delete_geq (pb, e, n_vars);

  for (i = n_vars; i > 0; i--)
    if (is_dead_var[i])
      omega_delete_variable (pb, i);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nafter free red eliminations:\n");
      omega_print_problem (dump_file, pb);
      fprintf (dump_file, "\n");
    }

  free (is_red_var);
  free (is_dead_var);
  free (is_dead_geq);
}

/* For equation EQ of the form "0 = EQN", insert in PB two
   inequalities "0 <= EQN" and "0 <= -EQN".  */

void
omega_convert_eq_to_geqs (omega_pb pb, int eq)
{
  int i;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Converting Eq to Geqs\n");

  /* Insert "0 <= EQN".  */
  omega_copy_eqn (&pb->geqs[pb->num_geqs], &pb->eqs[eq], pb->num_vars);
  pb->geqs[pb->num_geqs].touched = 1;
  pb->num_geqs++;

  /* Insert "0 <= -EQN".  */
  omega_copy_eqn (&pb->geqs[pb->num_geqs], &pb->eqs[eq], pb->num_vars);
  pb->geqs[pb->num_geqs].touched = 1;

  for (i = 0; i <= pb->num_vars; i++)
    pb->geqs[pb->num_geqs].coef[i] *= -1;

  pb->num_geqs++;

  if (dump_file && (dump_flags & TDF_DETAILS))
    omega_print_problem (dump_file, pb);
}

/* Eliminates variable I from PB.  */

static void
omega_do_elimination (omega_pb pb, int e, int i)
{
  eqn sub = omega_alloc_eqns (0, 1);
  int c;
  int n_vars = pb->num_vars;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "eliminating variable %s\n",
	     omega_variable_to_str (pb, i));

  omega_copy_eqn (sub, &pb->eqs[e], pb->num_vars);
  c = sub->coef[i];
  sub->coef[i] = 0;
  if (c == 1 || c == -1)
    {
      if (pb->eqs[e].color == omega_red)
	{
	  bool fB;
	  omega_substitute_red (pb, sub, i, c, &fB);
	  if (fB)
	    omega_convert_eq_to_geqs (pb, e);
	  else
	    omega_delete_variable (pb, i);
	}
      else
	{
	  omega_substitute (pb, sub, i, c);
	  omega_delete_variable (pb, i);
	}
    }
  else
    {
      int a = abs (c);
      int e2 = e;

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "performing non-exact elimination, c = %d\n", c);

      for (e = pb->num_eqs - 1; e >= 0; e--)
	if (pb->eqs[e].coef[i])
	  {
	    eqn eqn = &(pb->eqs[e]);
	    int j, k;
	    for (j = n_vars; j >= 0; j--)
	      eqn->coef[j] *= a;
	    k = eqn->coef[i];
	    eqn->coef[i] = 0;
	    eqn->color |= sub->color;
	    for (j = n_vars; j >= 0; j--)
	      eqn->coef[j] -= sub->coef[j] * k / c;
	  }

      for (e = pb->num_geqs - 1; e >= 0; e--)
	if (pb->geqs[e].coef[i])
	  {
	    eqn eqn = &(pb->geqs[e]);
	    int j, k;

	    if (sub->color == omega_red)
	      eqn->color = omega_red;

	    for (j = n_vars; j >= 0; j--)
	      eqn->coef[j] *= a;

	    eqn->touched = 1;
	    k = eqn->coef[i];
	    eqn->coef[i] = 0;

	    for (j = n_vars; j >= 0; j--)
	      eqn->coef[j] -= sub->coef[j] * k / c;

	  }

      for (e = pb->num_subs - 1; e >= 0; e--)
	if (pb->subs[e].coef[i])
	  {
	    eqn eqn = &(pb->subs[e]);
	    int j, k;
	    gcc_assert (0);
	    gcc_assert (sub->color == omega_black);
	    for (j = n_vars; j >= 0; j--)
	      eqn->coef[j] *= a;
	    k = eqn->coef[i];
	    eqn->coef[i] = 0;
	    for (j = n_vars; j >= 0; j--)
	      eqn->coef[j] -= sub->coef[j] * k / c;
	  }

      if (in_approximate_mode)
	omega_delete_variable (pb, i);
      else
	omega_convert_eq_to_geqs (pb, e2);
    }

  omega_free_eqns (sub, 1);
}

/* Helper function for printing "sorry, no solution".  */

static inline enum omega_result
omega_problem_has_no_solution (void)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\nequations have no solution \n");

  return omega_false;
}

/* Helper function: solve equations one at a time.  */

static enum omega_result
omega_solve_eq (omega_pb pb, enum omega_result desired_res)
{
  int i, j, e;
  int g, g2;
  g = 0;


  if (dump_file && (dump_flags & TDF_DETAILS) && pb->num_eqs > 0)
    {
      fprintf (dump_file, "\nomega_solve_eq (%d, %d)\n",
	       desired_res, may_be_red);
      omega_print_problem (dump_file, pb);
      fprintf (dump_file, "\n");
    }

  if (may_be_red)
    {
      i = 0;
      j = pb->num_eqs - 1;

      while (1)
	{
	  eqn eq;

	  while (i <= j && pb->eqs[i].color == omega_red)
	    i++;

	  while (i <= j && pb->eqs[j].color == omega_black)
	    j--;

	  if (i >= j)
	    break;

	  eq = omega_alloc_eqns (0, 1);
	  omega_copy_eqn (eq, &pb->eqs[i], pb->num_vars);
	  omega_copy_eqn (&pb->eqs[i], &pb->eqs[j], pb->num_vars);
	  omega_copy_eqn (&pb->eqs[j], eq, pb->num_vars);
	  omega_free_eqns (eq, 1);
	  i++;
	  j--;
	}
    }

  /* Eliminate all EQ equations */
  for (e = pb->num_eqs - 1; e >= 0; e--)
    {
      eqn eqn = &(pb->eqs[e]);
      int sv;

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "----\n");

      for (i = pb->num_vars; i > 0; i--)
	if (eqn->coef[i])
	  break;

      g = eqn->coef[i];

      for (j = i - 1; j > 0; j--)
	if (eqn->coef[j])
	  break;

      /* i is the position of last non-zero coefficient,
	 g is the coefficient of i,
	 j is the position of next non-zero coefficient.  */

      if (j == 0)
	{
	  if (eqn->coef[0] % g != 0)
	    return omega_problem_has_no_solution ();

	  eqn->coef[0] = eqn->coef[0] / g;
	  eqn->coef[i] = 1;
	  pb->num_eqs--;
	  omega_do_elimination (pb, e, i);
	  continue;
	}

      else if (j == -1)
	{
	  if (eqn->coef[0] != 0)
	    return omega_problem_has_no_solution ();

	  pb->num_eqs--;
	  continue;
	}

      if (g < 0)
	g = -g;

      if (g == 1)
	{
	  pb->num_eqs--;
	  omega_do_elimination (pb, e, i);
	}

      else
	{
	  int k = j;
	  bool promotion_possible =
	    (omega_safe_var_p (pb, j)
	     && pb->safe_vars + 1 == i
	     && !omega_eqn_is_red (eqn, desired_res)
	     && !in_approximate_mode);

	  if (dump_file && (dump_flags & TDF_DETAILS) && promotion_possible)
	    fprintf (dump_file, " Promotion possible\n");

	normalizeEQ:
	  if (!omega_safe_var_p (pb, j))
	    {
	      for (; g != 1 && !omega_safe_var_p (pb, j); j--)
		g = gcd (abs (eqn->coef[j]), g);
	      g2 = g;
	    }
	  else if (!omega_safe_var_p (pb, i))
	    g2 = g;
	  else
	    g2 = 0;

	  for (; g != 1 && j > 0; j--)
	    g = gcd (abs (eqn->coef[j]), g);

	  if (g > 1)
	    {
	      if (eqn->coef[0] % g != 0)
		return omega_problem_has_no_solution ();

	      for (j = 0; j <= pb->num_vars; j++)
		eqn->coef[j] /= g;

	      g2 = g2 / g;
	    }

	  if (g2 > 1)
	    {
	      int e2;

	      for (e2 = e - 1; e2 >= 0; e2--)
		if (pb->eqs[e2].coef[i])
		  break;

	      if (e2 == -1)
		for (e2 = pb->num_geqs - 1; e2 >= 0; e2--)
		  if (pb->geqs[e2].coef[i])
		    break;

	      if (e2 == -1)
		for (e2 = pb->num_subs - 1; e2 >= 0; e2--)
		  if (pb->subs[e2].coef[i])
		    break;

	      if (e2 == -1)
		{
		  bool change = false;

		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "Ha! We own it! \n");
		      omega_print_eq (dump_file, pb, eqn);
		      fprintf (dump_file, " \n");
		    }

		  g = eqn->coef[i];
		  g = abs (g);

		  for (j = i - 1; j >= 0; j--)
		    {
		      int t = int_mod (eqn->coef[j], g);

		      if (2 * t >= g)
			t -= g;

		      if (t != eqn->coef[j])
			{
			  eqn->coef[j] = t;
			  change = true;
			}
		    }

		  if (!change)
		    {
		      if (dump_file && (dump_flags & TDF_DETAILS))
			fprintf (dump_file, "So what?\n");
		    }

		  else
		    {
		      omega_name_wild_card (pb, i);

		      if (dump_file && (dump_flags & TDF_DETAILS))
			{
			  omega_print_eq (dump_file, pb, eqn);
			  fprintf (dump_file, " \n");
			}

		      e++;
		      continue;
		    }
		}
	    }

	  if (promotion_possible)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "promoting %s to safety\n",
			   omega_variable_to_str (pb, i));
		  omega_print_vars (dump_file, pb);
		}

	      pb->safe_vars++;

	      if (!omega_wildcard_p (pb, i))
		omega_name_wild_card (pb, i);

	      promotion_possible = false;
	      j = k;
	      goto normalizeEQ;
	    }

	  if (g2 > 1 && !in_approximate_mode)
	    {
	      if (pb->eqs[e].color == omega_red)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "handling red equality\n");

		  pb->num_eqs--;
		  omega_do_elimination (pb, e, i);
		  continue;
		}

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file,
			   "adding equation to handle safe variable \n");
		  omega_print_eq (dump_file, pb, eqn);
		  fprintf (dump_file, "\n----\n");
		  omega_print_problem (dump_file, pb);
		  fprintf (dump_file, "\n----\n");
		  fprintf (dump_file, "\n----\n");
		}

	      i = omega_add_new_wild_card (pb);
	      pb->num_eqs++;
	      gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);
	      omega_init_eqn_zero (&pb->eqs[e + 1], pb->num_vars);
	      omega_copy_eqn (&pb->eqs[e + 1], eqn, pb->safe_vars);

	      for (j = pb->num_vars; j >= 0; j--)
		{
		  pb->eqs[e + 1].coef[j] = int_mod (pb->eqs[e + 1].coef[j], g2);

		  if (2 * pb->eqs[e + 1].coef[j] >= g2)
		    pb->eqs[e + 1].coef[j] -= g2;
		}

	      pb->eqs[e + 1].coef[i] = g2;
	      e += 2;

	      if (dump_file && (dump_flags & TDF_DETAILS))
		omega_print_problem (dump_file, pb);

	      continue;
	    }

	  sv = pb->safe_vars;
	  if (g2 == 0)
	    sv = 0;

	  /* Find variable to eliminate.  */
	  if (g2 > 1)
	    {
	      gcc_assert (in_approximate_mode);

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "non-exact elimination: ");
		  omega_print_eq (dump_file, pb, eqn);
		  fprintf (dump_file, "\n");
		  omega_print_problem (dump_file, pb);
		}

	      for (i = pb->num_vars; i > sv; i--)
		if (pb->eqs[e].coef[i] != 0)
		  break;
	    }
	  else
	    for (i = pb->num_vars; i > sv; i--)
	      if (pb->eqs[e].coef[i] == 1 || pb->eqs[e].coef[i] == -1)
		break;

	  if (i > sv)
	    {
	      pb->num_eqs--;
	      omega_do_elimination (pb, e, i);

	      if (dump_file && (dump_flags & TDF_DETAILS) && g2 > 1)
		{
		  fprintf (dump_file, "result of non-exact elimination:\n");
		  omega_print_problem (dump_file, pb);
		}
	    }
	  else
	    {
	      int factor = (INT_MAX);
	      j = 0;

	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "doing moding\n");

	      for (i = pb->num_vars; i != sv; i--)
		if ((pb->eqs[e].coef[i] & 1) != 0)
		  {
		    j = i;
		    i--;

		    for (; i != sv; i--)
		      if ((pb->eqs[e].coef[i] & 1) != 0)
			break;

		    break;
		  }

	      if (j != 0 && i == sv)
		{
		  omega_do_mod (pb, 2, e, j);
		  e++;
		  continue;
		}

	      j = 0;
	      for (i = pb->num_vars; i != sv; i--)
		if (pb->eqs[e].coef[i] != 0 
		    && factor > abs (pb->eqs[e].coef[i]) + 1)
		  {
		    factor = abs (pb->eqs[e].coef[i]) + 1;
		    j = i;
		  }

	      if (j == sv)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "should not have happened\n");
		  gcc_assert (0);
		}

	      omega_do_mod (pb, factor, e, j);
	      /* Go back and try this equation again.  */
	      e++;
	    }
	}
    }

  pb->num_eqs = 0;
  return omega_unknown;
}

/* */

static enum omega_result
parallel_splinter (omega_pb pb, int e, int diff,
		   enum omega_result desired_res)
{
  omega_pb tmp_problem;
  int i;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Using parallel splintering\n");
      omega_print_problem (dump_file, pb);
    }

  tmp_problem = (omega_pb) xmalloc (sizeof (struct omega_pb));
  omega_copy_eqn (&pb->eqs[0], &pb->geqs[e], pb->num_vars);
  pb->num_eqs = 1;

  for (i = 0; i <= diff; i++)
    {
      omega_copy_problem (tmp_problem, pb);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Splinter # %i\n", i);
	  omega_print_problem (dump_file, pb);
	}

      if (omega_solve_problem (tmp_problem, desired_res) == omega_true)
	{
	  free (tmp_problem);
	  return omega_true;
	}

      pb->eqs[0].coef[0]--;
    }

  free (tmp_problem);
  return omega_false;
}

/* Helper function: solve equations one at a time.  */

static enum omega_result
omega_solve_geq (omega_pb pb, enum omega_result desired_res)
{
  int i, e;
  int n_vars, fv;
  enum omega_result result;
  bool coupled_subscripts = false;
  bool smoothed = false;
  bool eliminate_again;
  bool tried_eliminating_redundant = false;

  if (desired_res != omega_simplify)
    {
      pb->num_subs = 0;
      pb->safe_vars = 0;
    }

 solve_geq_start:
  do {
    gcc_assert (desired_res == omega_simplify || pb->num_subs == 0);

    /* Verify that there are not too many inequalities.  */
    gcc_assert (pb->num_geqs <= OMEGA_MAX_GEQS);

    if (dump_file && (dump_flags & TDF_DETAILS))
      {
	fprintf (dump_file, "\nomega_solve_geq (%d,%d):\n",
		 desired_res, please_no_equalities_in_simplified_problems);
	omega_print_problem (dump_file, pb);
	fprintf (dump_file, "\n");
      }

    n_vars = pb->num_vars;

    if (n_vars == 1)
      {
	enum omega_eqn_color u_color = omega_black;
	enum omega_eqn_color l_color = omega_black;
	int upper_bound = pos_infinity;
	int lower_bound = neg_infinity;

	for (e = pb->num_geqs - 1; e >= 0; e--)
	  {
	    int a = pb->geqs[e].coef[1];
	    int c = pb->geqs[e].coef[0];

	    /* Our equation is ax + c >= 0, or ax >= -c, or c >= -ax.  */
	    if (a == 0)
	      {
		if (c < 0)
		  return omega_problem_has_no_solution ();
	      }
	    else if (a > 0)
	      {
		if (a != 1)
		  c = int_div (c, a);

		if (lower_bound < -c
		    || (lower_bound == -c
			&& !omega_eqn_is_red (&pb->geqs[e], desired_res)))
		  {
		    lower_bound = -c;
		    l_color = pb->geqs[e].color;
		  }
	      }
	    else
	      {
		if (a != -1)
		  c = int_div (c, -a);

		if (upper_bound > c
		    || (upper_bound == c 
			&& !omega_eqn_is_red (&pb->geqs[e], desired_res)))
		  {
		    upper_bound = c;
		    u_color = pb->geqs[e].color;
		  }
	      }
	  }

	if (dump_file && (dump_flags & TDF_DETAILS))
	  {
	    fprintf (dump_file, "upper bound = %d\n", upper_bound);
	    fprintf (dump_file, "lower bound = %d\n", lower_bound);
	  }

	if (lower_bound > upper_bound)
	  return omega_problem_has_no_solution ();

	if (desired_res == omega_simplify)
	  {
	    pb->num_geqs = 0;
	    if (pb->safe_vars == 1)
	      {

		if (lower_bound == upper_bound
		    && u_color == omega_black
		    && l_color == omega_black)
		  {
		    pb->eqs[0].coef[0] = -lower_bound;
		    pb->eqs[0].coef[1] = 1;
		    pb->eqs[0].color = omega_black;
		    pb->num_eqs = 1;
		    return omega_solve_problem (pb, desired_res);
		  }
		else
		  {
		    if (lower_bound > neg_infinity)
		      {
			pb->geqs[0].coef[0] = -lower_bound;
			pb->geqs[0].coef[1] = 1;
			pb->geqs[0].key = 1;
			pb->geqs[0].color = l_color;
			pb->geqs[0].touched = 0;
			pb->num_geqs = 1;
		      }

		    if (upper_bound < pos_infinity)
		      {
			pb->geqs[pb->num_geqs].coef[0] = upper_bound;
			pb->geqs[pb->num_geqs].coef[1] = -1;
			pb->geqs[pb->num_geqs].key = -1;
			pb->geqs[pb->num_geqs].color = u_color;
			pb->geqs[pb->num_geqs].touched = 0;
			pb->num_geqs++;
		      }
		  }
	      }
	    else
	      pb->num_vars = 0;

	    omega_problem_reduced (pb);
	    return omega_false;
	  }

	if (original_problem != no_problem
	    && l_color == omega_black
	    && u_color == omega_black
	    && !conservative
	    && lower_bound == upper_bound)
	  {
	    pb->eqs[0].coef[0] = -lower_bound;
	    pb->eqs[0].coef[1] = 1;
	    pb->num_eqs = 1;
	    adding_equality_constraint (pb, 0);
	  }

	return omega_true;
      }

    if (!pb->variables_freed)
      {
	pb->variables_freed = true;

	if (desired_res != omega_simplify)
	  omega_free_eliminations (pb, 0);
	else
	  omega_free_eliminations (pb, pb->safe_vars);

	n_vars = pb->num_vars;

	if (n_vars == 1)
	  continue;
      }

    switch (normalize_omega_problem (pb))
      {
      case normalize_false:
	return omega_false;
	break;

      case normalize_coupled:
	coupled_subscripts = true;
	break;

      case normalize_uncoupled:
	coupled_subscripts = false;
	break;

      default:
	gcc_unreachable ();
      }

    n_vars = pb->num_vars;

    if (dump_file && (dump_flags & TDF_DETAILS))
      {
	fprintf (dump_file, "\nafter normalization:\n");
	omega_print_problem (dump_file, pb);
	fprintf (dump_file, "\n");
	fprintf (dump_file, "eliminating variable using Fourier-Motzkin.\n");
      }

    do {
      int parallel_difference = INT_MAX;
      int best_parallel_eqn = -1;
      int minC, maxC, minCj = 0;
      int lower_bound_count = 0;
      int e2, Le = 0, Ue;
      bool possible_easy_int_solution;
      int max_splinters = 1;
      bool exact = false;
      bool lucky_exact = false;
      int neweqns = 0;
      int best = (INT_MAX);
      int j = 0, jLe = 0, jLowerBoundCount = 0;


      eliminate_again = false;

      if (pb->num_eqs > 0)
	return omega_solve_problem (pb, desired_res);

      if (!coupled_subscripts)
	{
	  if (pb->safe_vars == 0)
	    pb->num_geqs = 0;
	  else
	    for (e = pb->num_geqs - 1; e >= 0; e--)
	      if (!omega_safe_var_p (pb, abs (pb->geqs[e].key)))
		omega_delete_geq (pb, e, n_vars);

	  pb->num_vars = pb->safe_vars;

	  if (desired_res == omega_simplify)
	    {
	      omega_problem_reduced (pb);
	      return omega_false;
	    }

	  return omega_true;
	}

      if (desired_res != omega_simplify)
	fv = 0;
      else
	fv = pb->safe_vars;

      if (pb->num_geqs == 0)
	{
	  if (desired_res == omega_simplify)
	    {
	      pb->num_vars = pb->safe_vars;
	      omega_problem_reduced (pb);
	      return omega_false;
	    }
	  return omega_true;
	}

      if (desired_res == omega_simplify && n_vars == pb->safe_vars)
	{
	  omega_problem_reduced (pb);
	  return omega_false;
	}

      if (pb->num_geqs > OMEGA_MAX_GEQS - 30
	  || pb->num_geqs > 2 * n_vars * n_vars + 4 * n_vars + 10)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file,
		     "TOO MANY EQUATIONS; "
		     "%d equations, %d variables, "
		     "ELIMINATING REDUNDANT ONES\n",
		     pb->num_geqs, n_vars);

	  if (!omega_eliminate_redundant (pb, 0))
	    return omega_false;

	  n_vars = pb->num_vars;

	  if (pb->num_eqs > 0)
	    return omega_solve_problem (pb, desired_res);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "END ELIMINATION OF REDUNDANT EQUATIONS\n");
	}

      if (desired_res != omega_simplify)
	fv = 0;
      else
	fv = pb->safe_vars;

      for (i = n_vars; i != fv; i--)
	{
	  int score;
	  int ub = -2;
	  int lb = -2;
	  bool lucky = false;
	  int upper_bound_count = 0;

	  lower_bound_count = 0;
	  minC = maxC = 0;

	  for (e = pb->num_geqs - 1; e >= 0; e--)
	    if (pb->geqs[e].coef[i] < 0)
	      {
		set_min (&minC, pb->geqs[e].coef[i]);
		upper_bound_count++;
		if (pb->geqs[e].coef[i] < -1)
		  {
		    if (ub == -2)
		      ub = e;
		    else
		      ub = -1;
		  }
	      }
	    else if (pb->geqs[e].coef[i] > 0)
	      {
		set_max (&maxC, pb->geqs[e].coef[i]);
		lower_bound_count++;
		Le = e;
		if (pb->geqs[e].coef[i] > 1)
		  {
		    if (lb == -2)
		      lb = e;
		    else
		      lb = -1;
		  }
	      }

	  if (lower_bound_count == 0 || upper_bound_count == 0)
	    {
	      lower_bound_count = 0;
	      break;
	    }

	  if (ub >= 0 && lb >= 0
	      && pb->geqs[lb].key == -pb->geqs[ub].key)
	    {
	      int Lc = pb->geqs[lb].coef[i];
	      int Uc = -pb->geqs[ub].coef[i];
	      int diff =
		Lc * pb->geqs[ub].coef[0] + Uc * pb->geqs[lb].coef[0];
	      lucky = (diff >= (Uc - 1) * (Lc - 1));
	    }

	  if (maxC == 1 || minC == -1 || lucky || APROX
	      || in_approximate_mode)
	    {
	      neweqns = score = upper_bound_count * lower_bound_count;

	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file,
			 "For %s, exact, score = %d*%d, range = %d ... %d, \nlucky = %d, APROX = %d, in_approximate_mode=%d \n",
			 omega_variable_to_str (pb, i),
			 upper_bound_count,
			 lower_bound_count, minC, maxC, lucky, APROX,
			 in_approximate_mode);

	      if (!exact || score < best)
		{

		  best = score;
		  j = i;
		  minCj = minC;
		  jLe = Le;
		  jLowerBoundCount = lower_bound_count;
		  exact = true;
		  lucky_exact = lucky;
		  if (score == 1)
		    break;
		}
	    }
	  else if (!exact)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file,
			 "For %s, non-exact, score = %d*%d, range = %d ... %d \n",
			 omega_variable_to_str (pb, i),
			 upper_bound_count,
			 lower_bound_count, minC, maxC);

	      neweqns = upper_bound_count * lower_bound_count;
	      score = maxC - minC;

	      if (best > score)
		{
		  best = score;
		  j = i;
		  minCj = minC;
		  jLe = Le;
		  jLowerBoundCount = lower_bound_count;
		}
	    }
	}

      if (lower_bound_count == 0)
	{
	  omega_free_eliminations (pb, pb->safe_vars);
	  n_vars = pb->num_vars;
	  eliminate_again = true;
	  continue;
	}

      i = j;
      minC = minCj;
      Le = jLe;
      lower_bound_count = jLowerBoundCount;

      for (e = pb->num_geqs - 1; e >= 0; e--)
	if (pb->geqs[e].coef[i] > 0)
	  {
	    if (pb->geqs[e].coef[i] == -minC)
	      max_splinters += -minC - 1;
	    else
	      max_splinters +=
		check_pos_mul ((pb->geqs[e].coef[i] - 1),
			       (-minC - 1)) / (-minC) + 1;
	  }

      /* #ifdef Omega3 */
      /* Trying to produce exact elimination by finding redundant
	 constraints.  */
      if (!exact && !tried_eliminating_redundant)
	{
	  omega_eliminate_redundant (pb, 0);
	  tried_eliminating_redundant = true;
	  eliminate_again = true;
	  continue;
	}
      tried_eliminating_redundant = false;
      /* #endif */

      if (return_single_result && desired_res == omega_simplify && !exact)
	{
	  non_convex = true;
	  omega_problem_reduced (pb);
	  return omega_true;
	}

      /* #ifndef Omega3 */
      /* Trying to produce exact elimination by finding redundant
	 constraints.  */
      if (!exact && !tried_eliminating_redundant)
	{
	  omega_eliminate_redundant (pb, 0);
	  tried_eliminating_redundant = true;
	  continue;
	}
      tried_eliminating_redundant = false;
      /* #endif */

      if (!exact)
	{
	  int e1, e2;

	  for (e1 = pb->num_geqs - 1; e1 >= 0; e1--)
	    if (pb->geqs[e1].color == omega_black)
	      for (e2 = e1 - 1; e2 >= 0; e2--)
		if (pb->geqs[e2].color == omega_black
		    && pb->geqs[e1].key == -pb->geqs[e2].key
		    && ((pb->geqs[e1].coef[0] + pb->geqs[e2].coef[0])
			* (3 - single_var_geq (&pb->geqs[e1], pb->num_vars))
			/ 2 < parallel_difference))
		  {
		    parallel_difference =
		      (pb->geqs[e1].coef[0] + pb->geqs[e2].coef[0])
		      * (3 - single_var_geq (&pb->geqs[e1], pb->num_vars))
		      / 2;
		    best_parallel_eqn = e1;
		  }

	  if (dump_file && (dump_flags & TDF_DETAILS)
	      && best_parallel_eqn >= 0)
	    {
	      fprintf (dump_file,
		       "Possible parallel projection, diff = %d, in ",
		       parallel_difference);
	      omega_print_geq (dump_file, pb, &(pb->geqs[best_parallel_eqn]));
	      fprintf (dump_file, "\n");
	      omega_print_problem (dump_file, pb);
	    }
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "going to eliminate %s, (%d,%d,%d)\n",
		   omega_variable_to_str (pb, i), i, minC,
		   lower_bound_count);
	  omega_print_problem (dump_file, pb);

	  if (lucky_exact)
	    fprintf (dump_file, "(a lucky exact elimination)\n");

	  else if (exact)
	    fprintf (dump_file, "(an exact elimination)\n");

	  fprintf (dump_file, "Max # of splinters = %d\n", max_splinters);
	}

      gcc_assert (max_splinters >= 1);

      if (!exact && desired_res == omega_simplify && best_parallel_eqn >= 0
	  && parallel_difference <= max_splinters)
	return parallel_splinter (pb, best_parallel_eqn, parallel_difference,
				  desired_res);

      smoothed = false;

      if (i != n_vars)
	{
	  int t;
	  int j = pb->num_vars;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Swapping %d and %d\n", i, j);
	      omega_print_problem (dump_file, pb);
	    }

	  swap (&pb->var[i], &pb->var[j]);

	  for (e = pb->num_geqs - 1; e >= 0; e--)
	    if (pb->geqs[e].coef[i] != pb->geqs[e].coef[j])
	      {
		pb->geqs[e].touched = 1;
		t = pb->geqs[e].coef[i];
		pb->geqs[e].coef[i] = pb->geqs[e].coef[j];
		pb->geqs[e].coef[j] = t;
	      }

	  for (e = pb->num_subs - 1; e >= 0; e--)
	    if (pb->subs[e].coef[i] != pb->subs[e].coef[j])
	      {
		t = pb->subs[e].coef[i];
		pb->subs[e].coef[i] = pb->subs[e].coef[j];
		pb->subs[e].coef[j] = t;
	      }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Swapping complete \n");
	      omega_print_problem (dump_file, pb);
	      fprintf (dump_file, "\n");
	    }

	  i = j;
	}

      else if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "No swap needed\n");
	  omega_print_problem (dump_file, pb);
	}

      pb->num_vars--;
      n_vars = pb->num_vars;

      if (exact)
	{
	  if (n_vars == 1)
	    {
	      int upper_bound = pos_infinity;
	      int lower_bound = neg_infinity;
	      enum omega_eqn_color ub_color = omega_black;
	      enum omega_eqn_color lb_color = omega_black;
	      int topeqn = pb->num_geqs - 1;
	      int Lc = pb->geqs[Le].coef[i];

	      for (Le = topeqn; Le >= 0; Le--)
		if ((Lc = pb->geqs[Le].coef[i]) == 0)
		  {
		    if (pb->geqs[Le].coef[1] == 1)
		      {
			int constantTerm = -pb->geqs[Le].coef[0];

			if (constantTerm > lower_bound ||
			    (constantTerm == lower_bound &&
			     !omega_eqn_is_red (&pb->geqs[Le], desired_res)))
			  {
			    lower_bound = constantTerm;
			    lb_color = pb->geqs[Le].color;
			  }

			if (dump_file && (dump_flags & TDF_DETAILS))
			  {
			    if (pb->geqs[Le].color == omega_black)
			      fprintf (dump_file, " :::=> %s >= %d\n",
				       omega_variable_to_str (pb, 1),
				       constantTerm);
			    else
			      fprintf (dump_file,
				       " :::=> [%s >= %d]\n",
				       omega_variable_to_str (pb, 1),
				       constantTerm);
			  }
		      }
		    else
		      {
			int constantTerm = pb->geqs[Le].coef[0];
			if (constantTerm < upper_bound ||
			    (constantTerm == upper_bound
			     && !omega_eqn_is_red (&pb->geqs[Le],
						   desired_res)))
			  {
			    upper_bound = constantTerm;
			    ub_color = pb->geqs[Le].color;
			  }

			if (dump_file && (dump_flags & TDF_DETAILS))
			  {
			    if (pb->geqs[Le].color == omega_black)
			      fprintf (dump_file, " :::=> %s <= %d\n",
				       omega_variable_to_str (pb, 1),
				       constantTerm);
			    else
			      fprintf (dump_file,
				       " :::=> [%s <= %d]\n",
				       omega_variable_to_str (pb, 1),
				       constantTerm);
			  }
		      }
		  }
		else if (Lc > 0)
		  for (Ue = topeqn; Ue >= 0; Ue--)
		    if (pb->geqs[Ue].coef[i] < 0
			&& pb->geqs[Le].key != -pb->geqs[Ue].key)
		      {
			int Uc = -pb->geqs[Ue].coef[i];
			int coefficient = pb->geqs[Ue].coef[1] * Lc
			  + pb->geqs[Le].coef[1] * Uc;
			int constantTerm = pb->geqs[Ue].coef[0] * Lc
			  + pb->geqs[Le].coef[0] * Uc;

			if (dump_file && (dump_flags & TDF_DETAILS))
			  {
			    omega_print_geq_extra (dump_file, pb,
						   &(pb->geqs[Ue]));
			    fprintf (dump_file, "\n");
			    omega_print_geq_extra (dump_file, pb,
						   &(pb->geqs[Le]));
			    fprintf (dump_file, "\n");
			  }

			if (coefficient > 0)
			  {
			    constantTerm = -int_div (constantTerm, coefficient);

			    if (constantTerm > lower_bound 
				|| (constantTerm == lower_bound 
				    && (desired_res != omega_simplify 
					|| (pb->geqs[Ue].color == omega_black
					    && pb->geqs[Le].color == omega_black))))
			      {
				lower_bound = constantTerm;
				lb_color = (pb->geqs[Ue].color == omega_red
					    || pb->geqs[Le].color == omega_red)
				  ? omega_red : omega_black;
			      }

			    if (dump_file && (dump_flags & TDF_DETAILS))
			      {
				if (pb->geqs[Ue].color == omega_red
				    || pb->geqs[Le].color == omega_red)
				  fprintf (dump_file,
					   " ::=> [%s >= %d]\n",
					   omega_variable_to_str (pb, 1),
					   constantTerm);
				else
				  fprintf (dump_file,
					   " ::=> %s >= %d\n",
					   omega_variable_to_str (pb, 1),
					   constantTerm);
			      }
			  }
			else
			  {
			    constantTerm = int_div (constantTerm, -coefficient);
			    if (constantTerm < upper_bound
				|| (constantTerm == upper_bound
				    && pb->geqs[Ue].color == omega_black
				    && pb->geqs[Le].color == omega_black))
			      {
				upper_bound = constantTerm;
				ub_color = (pb->geqs[Ue].color == omega_red
					    || pb->geqs[Le].color == omega_red)
				  ? omega_red : omega_black;
			      }

			    if (dump_file
				&& (dump_flags & TDF_DETAILS))
			      {
				if (pb->geqs[Ue].color == omega_red
				    || pb->geqs[Le].color == omega_red)
				  fprintf (dump_file,
					   " ::=> [%s <= %d]\n",
					   omega_variable_to_str (pb, 1),
					   constantTerm);
				else
				  fprintf (dump_file,
					   " ::=> %s <= %d\n",
					   omega_variable_to_str (pb, 1),
					   constantTerm);
			      }
			  }
		      }

	      pb->num_geqs = 0;

	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file,
			 " therefore, %c%d <= %c%s%c <= %d%c\n",
			 lb_color == omega_red ? '[' : ' ', lower_bound,
			 (lb_color == omega_red && ub_color == omega_black)
			 ? ']' : ' ',
			 omega_variable_to_str (pb, 1),
			 (lb_color == omega_black && ub_color == omega_red)
			 ? '[' : ' ',
			 upper_bound, ub_color == omega_red ? ']' : ' ');

	      if (lower_bound > upper_bound)
		return omega_false;

	      if (pb->safe_vars == 1)
		{
		  if (upper_bound == lower_bound
		      && !(ub_color == omega_red || lb_color == omega_red)
		      && !please_no_equalities_in_simplified_problems)
		    {
		      pb->num_eqs++;
		      pb->eqs[0].coef[1] = -1;
		      pb->eqs[0].coef[0] = upper_bound;

		      if (ub_color == omega_red
			  || lb_color == omega_red)
			pb->eqs[0].color = omega_red;

		      if (desired_res == omega_simplify
			  && pb->eqs[0].color == omega_black)
			return omega_solve_problem (pb, desired_res);
		    }

		  if (upper_bound != pos_infinity)
		    {
		      pb->geqs[0].coef[1] = -1;
		      pb->geqs[0].coef[0] = upper_bound;
		      pb->geqs[0].color = ub_color;
		      pb->geqs[0].key = -1;
		      pb->geqs[0].touched = 0;
		      pb->num_geqs++;
		    }

		  if (lower_bound != neg_infinity)
		    {
		      pb->geqs[pb->num_geqs].coef[1] = 1;
		      pb->geqs[pb->num_geqs].coef[0] = -lower_bound;
		      pb->geqs[pb->num_geqs].color = lb_color;
		      pb->geqs[pb->num_geqs].key = 1;
		      pb->geqs[pb->num_geqs].touched = 0;
		      pb->num_geqs++;
		    }
		}

	      if (desired_res == omega_simplify)
		{
		  omega_problem_reduced (pb);
		  return omega_false;
		}
	      else
		{
		  if (!conservative &&
		      (desired_res != omega_simplify ||
		       (lb_color == omega_black && ub_color == omega_black))
		      && original_problem != no_problem
		      && lower_bound == upper_bound)
		    {
		      for (i = original_problem->num_vars; i >= 0; i--)
			if (original_problem->var[i] == pb->var[1])
			  break;

		      if (i == 0)
			break;

		      e = original_problem->num_eqs++;
		      omega_init_eqn_zero (&original_problem->eqs[e],
					   original_problem->num_vars);
		      original_problem->eqs[e].coef[i] = -1;
		      original_problem->eqs[e].coef[0] = upper_bound;

		      if (dump_file && (dump_flags & TDF_DETAILS))
			{
			  fprintf (dump_file,
				   "adding equality %d to outer problem\n",
				   e);
			  omega_print_problem (dump_file, original_problem);
			}
		    }
		  return omega_true;
		}
	    }

	  eliminate_again = true;

	  if (lower_bound_count == 1)
	    {
	      eqn lbeqn = omega_alloc_eqns (0, 1);
	      int Lc = pb->geqs[Le].coef[i];

	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "an inplace elimination\n");

	      omega_copy_eqn (lbeqn, &pb->geqs[Le], (n_vars + 1));
	      omega_delete_geq_extra (pb, Le, n_vars + 1);

	      for (Ue = pb->num_geqs - 1; Ue >= 0; Ue--)
		if (pb->geqs[Ue].coef[i] < 0)
		  {
		    if (lbeqn->key == -pb->geqs[Ue].key)
		      omega_delete_geq_extra (pb, Ue, n_vars + 1);
		    else
		      {
			int k;
			int Uc = -pb->geqs[Ue].coef[i];
			pb->geqs[Ue].touched = 1;
			eliminate_again = false;

			if (lbeqn->color == omega_red)
			  pb->geqs[Ue].color = omega_red;

			for (k = 0; k <= n_vars; k++)
			  pb->geqs[Ue].coef[k] =
			    check_mul (pb->geqs[Ue].coef[k], Lc) +
			    check_mul (lbeqn->coef[k], Uc);

			if (dump_file && (dump_flags & TDF_DETAILS))
			  {
			    omega_print_geq (dump_file, pb,
					     &(pb->geqs[Ue]));
			    fprintf (dump_file, "\n");
			  }
		      }
		  }

	      omega_free_eqns (lbeqn, 1);
	      continue;
	    }
	  else
	    {
	      int *dead_eqns = (int *) xmalloc (OMEGA_MAX_GEQS * sizeof (int));
	      bool *is_dead = (bool *) xmalloc (OMEGA_MAX_GEQS * sizeof (int));
	      int num_dead = 0;
	      int top_eqn = pb->num_geqs - 1;
	      lower_bound_count--;

	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "lower bound count = %d\n",
			 lower_bound_count);

	      for (Le = top_eqn; Le >= 0; Le--)
		if (pb->geqs[Le].coef[i] > 0)
		  {
		    int Lc = pb->geqs[Le].coef[i];
		    for (Ue = top_eqn; Ue >= 0; Ue--)
		      if (pb->geqs[Ue].coef[i] < 0)
			{
			  if (pb->geqs[Le].key != -pb->geqs[Ue].key)
			    {
			      int k;
			      int Uc = -pb->geqs[Ue].coef[i];

			      if (num_dead == 0)
				e2 = pb->num_geqs++;
			      else
				e2 = dead_eqns[--num_dead];

			      gcc_assert (e2 < OMEGA_MAX_GEQS);

			      if (dump_file && (dump_flags & TDF_DETAILS))
				{
				  fprintf (dump_file,
					   "Le = %d, Ue = %d, gen = %d\n",
					   Le, Ue, e2);
				  omega_print_geq_extra (dump_file, pb,
							 &(pb->geqs[Le]));
				  fprintf (dump_file, "\n");
				  omega_print_geq_extra (dump_file, pb,
							 &(pb->geqs[Ue]));
				  fprintf (dump_file, "\n");
				}

			      eliminate_again = false;

			      for (k = n_vars; k >= 0; k--)
				pb->geqs[e2].coef[k] =
				  check_mul (pb->geqs[Ue].coef[k], Lc) +
				  check_mul (pb->geqs[Le].coef[k], Uc);

			      pb->geqs[e2].coef[n_vars + 1] = 0;
			      pb->geqs[e2].touched = 1;

			      if (pb->geqs[Ue].color == omega_red 
				  || pb->geqs[Le].color == omega_red)
				pb->geqs[e2].color = omega_red;
			      else
				pb->geqs[e2].color = omega_black;

			      if (dump_file && (dump_flags & TDF_DETAILS))
				{
				  omega_print_geq (dump_file, pb,
						   &(pb->geqs[e2]));
				  fprintf (dump_file, "\n");
				}
			    }

			  if (lower_bound_count == 0)
			    {
			      dead_eqns[num_dead++] = Ue;

			      if (dump_file && (dump_flags & TDF_DETAILS))
				fprintf (dump_file, "Killed %d\n", Ue);
			    }
			}

		    lower_bound_count--;
		    dead_eqns[num_dead++] = Le;

		    if (dump_file && (dump_flags & TDF_DETAILS))
		      fprintf (dump_file, "Killed %d\n", Le);
		  }

	      for (e = pb->num_geqs - 1; e >= 0; e--)
		is_dead[e] = false;

	      while (num_dead > 0)
		is_dead[dead_eqns[--num_dead]] = true;

	      for (e = pb->num_geqs - 1; e >= 0; e--)
		if (is_dead[e])
		  omega_delete_geq_extra (pb, e, n_vars + 1);

	      free (dead_eqns);
	      free (is_dead);
	      continue;
	    }
	}
      else
	{
	  omega_pb rS, iS;

	  rS = omega_alloc_problem (0, 0);
	  iS = omega_alloc_problem (0, 0);
	  e2 = 0;
	  possible_easy_int_solution = true;

	  for (e = 0; e < pb->num_geqs; e++)
	    if (pb->geqs[e].coef[i] == 0)
	      {
		omega_copy_eqn (&(rS->geqs[e2]), &pb->geqs[e],
				pb->num_vars);
		omega_copy_eqn (&(iS->geqs[e2]), &pb->geqs[e],
				pb->num_vars);

		if (dump_file && (dump_flags & TDF_DETAILS))
		  {
		    int t;
		    fprintf (dump_file, "Copying (%d, %d): ", i,
			     pb->geqs[e].coef[i]);
		    omega_print_geq_extra (dump_file, pb, &pb->geqs[e]);
		    fprintf (dump_file, "\n");
		    for (t = 0; t <= n_vars + 1; t++)
		      fprintf (dump_file, "%d ", pb->geqs[e].coef[t]);
		    fprintf (dump_file, "\n");

		  }
		e2++;
		gcc_assert (e2 < OMEGA_MAX_GEQS);
	      }

	  for (Le = pb->num_geqs - 1; Le >= 0; Le--)
	    if (pb->geqs[Le].coef[i] > 0)
	      for (Ue = pb->num_geqs - 1; Ue >= 0; Ue--)
		if (pb->geqs[Ue].coef[i] < 0)
		  {
		    int k;
		    int Lc = pb->geqs[Le].coef[i];
		    int Uc = -pb->geqs[Ue].coef[i];

		    if (pb->geqs[Le].key != -pb->geqs[Ue].key)
		      {

			rS->geqs[e2].touched = iS->geqs[e2].touched = 1;

			if (dump_file && (dump_flags & TDF_DETAILS))
			  {
			    fprintf (dump_file, "---\n");
			    fprintf (dump_file,
				     "Le(Lc) = %d(%d_, Ue(Uc) = %d(%d), gen = %d\n",
				     Le, Lc, Ue, Uc, e2);
			    omega_print_geq_extra (dump_file, pb, &pb->geqs[Le]);
			    fprintf (dump_file, "\n");
			    omega_print_geq_extra (dump_file, pb, &pb->geqs[Ue]);
			    fprintf (dump_file, "\n");
			  }

			if (Uc == Lc)
			  {
			    for (k = n_vars; k >= 0; k--)
			      iS->geqs[e2].coef[k] = rS->geqs[e2].coef[k] =
				pb->geqs[Ue].coef[k] + pb->geqs[Le].coef[k];

			    iS->geqs[e2].coef[0] -= (Uc - 1);
			  }
			else
			  {
			    for (k = n_vars; k >= 0; k--)
			      iS->geqs[e2].coef[k] = rS->geqs[e2].coef[k] =
				check_mul (pb->geqs[Ue].coef[k], Lc) +
				check_mul (pb->geqs[Le].coef[k], Uc);

			    iS->geqs[e2].coef[0] -= (Uc - 1) * (Lc - 1);
			  }

			if (pb->geqs[Ue].color == omega_red
			    || pb->geqs[Le].color == omega_red)
			  iS->geqs[e2].color = rS->geqs[e2].color = omega_red;
			else
			  iS->geqs[e2].color = rS->geqs[e2].color = omega_black;

			if (dump_file && (dump_flags & TDF_DETAILS))
			  {
			    omega_print_geq (dump_file, pb, &(rS->geqs[e2]));
			    fprintf (dump_file, "\n");
			  }

			e2++;
			gcc_assert (e2 < OMEGA_MAX_GEQS);
		      }
		    else if (pb->geqs[Ue].coef[0] * Lc +
			     pb->geqs[Le].coef[0] * Uc -
			     (Uc - 1) * (Lc - 1) < 0)
		      possible_easy_int_solution = false;
		  }

	  iS->variables_initialized = rS->variables_initialized = true;
	  iS->num_vars = rS->num_vars = pb->num_vars;
	  iS->num_geqs = rS->num_geqs = e2;
	  iS->num_eqs = rS->num_eqs = 0;
	  iS->num_subs = rS->num_subs = pb->num_subs;
	  iS->safe_vars = rS->safe_vars = pb->safe_vars;

	  for (e = n_vars; e >= 0; e--)
	    rS->var[e] = pb->var[e];

	  for (e = n_vars; e >= 0; e--)
	    iS->var[e] = pb->var[e];

	  for (e = pb->num_subs - 1; e >= 0; e--)
	    {
	      omega_copy_eqn (&(rS->subs[e]), &(pb->subs[e]), pb->num_vars);
	      omega_copy_eqn (&(iS->subs[e]), &(pb->subs[e]), pb->num_vars);
	    }

	  pb->num_vars++;
	  n_vars = pb->num_vars;

	  if (desired_res != omega_true)
	    {
	      if (original_problem == no_problem)
		{
		  original_problem = pb;
		  result = omega_solve_geq (rS, omega_false);
		  original_problem = no_problem;
		}
	      else
		result = omega_solve_geq (rS, omega_false);

	      if (result == omega_false)
		{
		  free (rS);
		  free (iS);
		  return result;
		}

	      if (pb->num_eqs > 0)
		{
		  /* An equality constraint must have been found */
		  free (rS);
		  free (iS);
		  return omega_solve_problem (pb, desired_res);
		}
	    }

	  if (desired_res != omega_false)
	    {
	      int j;
	      int lower_bounds = 0;
	      int *lower_bound = (int *) xmalloc (OMEGA_MAX_GEQS * sizeof (int));

	      if (possible_easy_int_solution)
		{
		  conservative++;
		  result = omega_solve_geq (iS, desired_res);
		  conservative--;

		  if (result != omega_false)
		    {
		      free (rS);
		      free (iS);
		      free (lower_bound);
		      return result;
		    }
		}

	      if (!exact && best_parallel_eqn >= 0
		  && parallel_difference <= max_splinters)
		{
		  free (rS);
		  free (iS);
		  free (lower_bound);
		  return parallel_splinter (pb, best_parallel_eqn,
					    parallel_difference,
					    desired_res);
		}

	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "have to do exact analysis\n");

	      conservative++;

	      for (e = 0; e < pb->num_geqs; e++)
		if (pb->geqs[e].coef[i] > 1)
		  lower_bound[lower_bounds++] = e;

	      /* Sort array LOWER_BOUND.  */
	      for (j = 0; j < lower_bounds; j++)
		{
		  int k, smallest = j;

		  for (k = j + 1; k < lower_bounds; k++)
		    if (pb->geqs[lower_bound[smallest]].coef[i] >
			pb->geqs[lower_bound[k]].coef[i])
		      smallest = k;

		  k = lower_bound[smallest];
		  lower_bound[smallest] = lower_bound[j];
		  lower_bound[j] = k;
		}

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "lower bound coeeficients = ");

		  for (j = 0; j < lower_bounds; j++)
		    fprintf (dump_file, " %d",
			     pb->geqs[lower_bound[j]].coef[i]);

		  fprintf (dump_file, "\n");
		}

	      for (j = 0; j < lower_bounds; j++)
		{
		  int max_incr;
		  int c;
		  int worst_lower_bound_constant = -minC;

		  e = lower_bound[j];
		  max_incr = (((pb->geqs[e].coef[i] - 1) *
			       (worst_lower_bound_constant - 1) - 1)
			      / worst_lower_bound_constant);
		  /* max_incr += 2; */

		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "for equation ");
		      omega_print_geq (dump_file, pb, &pb->geqs[e]);
		      fprintf (dump_file,
			       "\ntry decrements from 0 to %d\n",
			       max_incr);
		      omega_print_problem (dump_file, pb);
		    }

		  if (max_incr > 50 && !smoothed
		      && smooth_weird_equations (pb))
		    {
		      conservative--;
		      free (rS);
		      free (iS);
		      smoothed = true;
		      goto solve_geq_start;
		    }

		  omega_copy_eqn (&pb->eqs[0], &pb->geqs[e],
				  pb->num_vars);
		  pb->eqs[0].color = omega_black;
		  omega_init_eqn_zero (&pb->geqs[e], pb->num_vars);
		  pb->geqs[e].touched = 1;
		  pb->num_eqs = 1;

		  for (c = max_incr; c >= 0; c--)
		    {
		      if (dump_file && (dump_flags & TDF_DETAILS))
			{
			  fprintf (dump_file,
				   "trying next decrement of %d\n",
				   max_incr - c);
			  omega_print_problem (dump_file, pb);
			}

		      omega_copy_problem (rS, pb);

		      if (dump_file && (dump_flags & TDF_DETAILS))
			omega_print_problem (dump_file, rS);

		      result = omega_solve_problem (rS, desired_res);

		      if (result == omega_true)
			{
			  free (rS);
			  free (iS);
			  free (lower_bound);
			  conservative--;
			  return omega_true;
			}

		      pb->eqs[0].coef[0]--;
		    }

		  if (j + 1 < lower_bounds)
		    {
		      pb->num_eqs = 0;
		      omega_copy_eqn (&pb->geqs[e], &pb->eqs[0],
				      pb->num_vars);
		      pb->geqs[e].touched = 1;
		      pb->geqs[e].color = omega_black;
		      omega_copy_problem (rS, pb);

		      if (dump_file && (dump_flags & TDF_DETAILS))
			fprintf (dump_file,
				 "exhausted lower bound, "
				 "checking if still feasible ");

		      result = omega_solve_problem (rS, omega_false);

		      if (result == omega_false)
			break;
		    }
		}

	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "fall-off the end\n");

	      free (rS);
	      free (iS);
	      free (lower_bound);
	      conservative--;
	      return omega_false;
	    }

	  free (rS);
	  free (iS);
	}
      return omega_unknown;
    } while (eliminate_again);
  } while (1);
}

/* Because the omega solver is recursive, this counter limits the
   recursion depth.  */
static int omega_solve_depth = 0;

/* Return omega_true when the problem PB has a solution following the
   DESIRED_RES.  */

enum omega_result
omega_solve_problem (omega_pb pb, enum omega_result desired_res)
{
  enum omega_result result;

  gcc_assert (pb->num_vars >= pb->safe_vars);
  omega_solve_depth++;

  if (desired_res != omega_simplify)
    pb->safe_vars = 0;

  if (omega_solve_depth > 50)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Solve depth = %d, inApprox = %d, aborting\n",
		   omega_solve_depth, in_approximate_mode);
	  omega_print_problem (dump_file, pb);
	}
      gcc_assert (0);
    }

  do
    {
      do_it_again = false;

      if (omega_solve_eq (pb, desired_res) == omega_false)
	{
	  omega_solve_depth--;
	  return omega_false;
	}

      if (in_approximate_mode && !pb->num_geqs)
	{
	  result = omega_true;
	  pb->num_vars = pb->safe_vars;
	  omega_problem_reduced (pb);
	  break;
	}
      else
	result = omega_solve_geq (pb, desired_res);
    } while (do_it_again && desired_res == omega_simplify);

  omega_solve_depth--;

  if (!omega_reduce_with_subs)
    {
      resurrect_subs (pb);
      gcc_assert (please_no_equalities_in_simplified_problems 
		  || !result || pb->num_subs == 0);
    }

  return result;
}

/* Return true if red equations constrain the set of possible solutions.
   We assume that there are solutions to the black equations by
   themselves, so if there is no solution to the combined problem, we
   return true.  */

bool
omega_problem_has_red_equations (omega_pb pb)
{
  bool result;
  int e;
  int i;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Checking for red equations:\n");
      omega_print_problem (dump_file, pb);
    }

  please_no_equalities_in_simplified_problems++;
  may_be_red++;

#ifndef SINGLE_RESULT
  return_single_result++;
#endif

  create_color = true;
  result = (omega_simplify_problem (pb) == omega_false);

#ifndef SINGLE_RESULT
  return_single_result--;
#endif

  may_be_red--;
  please_no_equalities_in_simplified_problems--;

  if (result)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
      	fprintf (dump_file, "Gist is FALSE\n");

      pb->num_subs = 0;
      pb->num_geqs = 0;
      pb->num_eqs = 1;
      pb->eqs[0].color = omega_red;

      for (i = pb->num_vars; i > 0; i--)
	pb->eqs[0].coef[i] = 0;

      pb->eqs[0].coef[0] = 1;
      return true;
    }

  free_red_eliminations (pb);
  gcc_assert (pb->num_eqs == 0);

  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (pb->geqs[e].color == omega_red)
      result = true;

  if (!result)
    return false;

  for (i = pb->safe_vars; i >= 1; i--)
    {
      int ub = 0;
      int lb = 0;

      for (e = pb->num_geqs - 1; e >= 0; e--)
	{
	  if (pb->geqs[e].coef[i])
	    {
	      if (pb->geqs[e].coef[i] > 0)
		lb |= (1 + (pb->geqs[e].color == omega_red ? 1 : 0));

	      else
		ub |= (1 + (pb->geqs[e].color == omega_red ? 1 : 0));
	    }
	}

      if (ub == 2 || lb == 2)
	{

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "checks for upper/lower bounds worked!\n");

	  if (!omega_reduce_with_subs)
	    {
	      resurrect_subs (pb);
	      gcc_assert (pb->num_subs == 0);
	    }

	  return true;
	}
    }


  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file,
	     "*** Doing potentially expensive elimination tests "
	     "for red equations\n");

  please_no_equalities_in_simplified_problems++;
  omega_eliminate_red (pb, 1);
  please_no_equalities_in_simplified_problems--;

  result = false;
  gcc_assert (pb->num_eqs == 0);

  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (pb->geqs[e].color == omega_red)
      result = true;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      if (!result)
	fprintf (dump_file,
		 "******************** Redudant Red Equations eliminated!!\n");
      else
	fprintf (dump_file,
		 "******************** Red Equations remain\n");

      omega_print_problem (dump_file, pb);
    }

  if (!omega_reduce_with_subs)
    {
      normalize_return_type r;

      resurrect_subs (pb);
      r = normalize_omega_problem (pb);
      gcc_assert (r != normalize_false);

      coalesce (pb);
      cleanout_wildcards (pb);
      gcc_assert (pb->num_subs == 0);
    }

  return result;
}

/* Calls omega_simplify_problem in approximate mode.  */

enum omega_result
omega_simplify_approximate (omega_pb pb)
{
  enum omega_result result;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(Entering approximate mode\n");

  in_approximate_mode = true;
  result = omega_simplify_problem (pb);
  in_approximate_mode = false;

  gcc_assert (pb->num_vars == pb->safe_vars);
  if (!omega_reduce_with_subs)
    gcc_assert (pb->num_subs == 0);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Leaving approximate mode)\n");

  return result;
}

/* Simplifies problem PB by eliminating redundant constraints and
   reducing the constraints system to a minimal form.  Returns
   omega_true when the problem was successfully reduced, omega_unknown
   when the solver is unable to determine an answer.  */

enum omega_result
omega_simplify_problem (omega_pb pb)
{
  int i;
  omega_found_reduction = omega_false;

  if (!pb->variables_initialized)
    omega_initialize_variables (pb);

  /* 
     #ifdef clearForwardingPointers 
     for (i = 1; i <= pb->num_vars; i++)
     if (!omega_wildcard_p (pb, i))
     pb->forwarding_address[pb->var[i]] = 0; 
     #endif
  */

  if (next_key * 3 > MAX_KEYS)
    {
      int e;

      hash_version++;
      next_key = OMEGA_MAX_VARS + 1;

      for (e = pb->num_geqs - 1; e >= 0; e--)
	pb->geqs[e].touched = 1;

      for (i = 0; i < HASH_TABLE_SIZE; i++)
	hash_master[i].touched = -1;

      pb->hash_version = hash_version;
    }

  else if (pb->hash_version != hash_version)
    {
      int e;

      for (e = pb->num_geqs - 1; e >= 0; e--)
	pb->geqs[e].touched = 1;

      pb->hash_version = hash_version;
    }

  non_convex = false;

  if (pb->num_vars > pb->num_eqs + 3 * pb->safe_vars)
    omega_free_eliminations (pb, pb->safe_vars);

  if (!may_be_red && pb->num_subs == 0 && pb->safe_vars == 0)
    {
      omega_found_reduction = omega_solve_problem (pb, omega_unknown);

      if (omega_found_reduction != omega_false
	  && !return_single_result)
	{
	  pb->num_geqs = 0;
	  pb->num_eqs = 0;
	  (*omega_when_reduced) (pb);
	}

      return omega_found_reduction;
    }

  omega_solve_problem (pb, omega_simplify);

  if (omega_found_reduction != omega_false)
    {
      for (i = 1; omega_safe_var_p (pb, i); i++)
	pb->forwarding_address[pb->var[i]] = i;

      for (i = 0; i < pb->num_subs; i++)
	pb->forwarding_address[pb->subs[i].key] = -i - 1;
    }

  if (!omega_reduce_with_subs)
    gcc_assert (please_no_equalities_in_simplified_problems
		|| omega_found_reduction == omega_false
		|| pb->num_subs == 0);

  return omega_found_reduction;
}

/* Make variable VAR unprotected: it then can be eliminated.  */

void
omega_unprotect_variable (omega_pb pb, int var)
{
  int e, idx;
  idx = pb->forwarding_address[var];

  if (idx < 0)
    {
      idx = -1 - idx;
      pb->num_subs--;

      if (idx < pb->num_subs)
	{
	  omega_copy_eqn (&pb->subs[idx], &pb->subs[pb->num_subs],
			  pb->num_vars);
	  pb->forwarding_address[pb->subs[idx].key] = -idx - 1;
	}
    }
  else
    {
      int *bring_to_life = (int *) xmalloc (OMEGA_MAX_VARS * sizeof (int));
      int e2;

      for (e = pb->num_subs - 1; e >= 0; e--)
	bring_to_life[e] = (pb->subs[e].coef[idx] != 0);

      for (e2 = pb->num_subs - 1; e2 >= 0; e2--)
	if (bring_to_life[e2])
	  {
	    pb->num_vars++;
	    pb->safe_vars++;

	    if (pb->safe_vars < pb->num_vars)
	      {
		for (e = pb->num_geqs - 1; e >= 0; e--)
		  {
		    pb->geqs[e].coef[pb->num_vars] = 
		      pb->geqs[e].coef[pb->safe_vars];

		    pb->geqs[e].coef[pb->safe_vars] = 0;
		  }

		for (e = pb->num_eqs - 1; e >= 0; e--)
		  {
		    pb->eqs[e].coef[pb->num_vars] =
		      pb->eqs[e].coef[pb->safe_vars];

		    pb->eqs[e].coef[pb->safe_vars] = 0;
		  }

		for (e = pb->num_subs - 1; e >= 0; e--)
		  {
		    pb->subs[e].coef[pb->num_vars] =
		      pb->subs[e].coef[pb->safe_vars];

		    pb->subs[e].coef[pb->safe_vars] = 0;
		  }

		pb->var[pb->num_vars] = pb->var[pb->safe_vars];
		pb->forwarding_address[pb->var[pb->num_vars]] =
		  pb->num_vars;
	      }
	    else
	      {
		for (e = pb->num_geqs - 1; e >= 0; e--)
		  pb->geqs[e].coef[pb->safe_vars] = 0;

		for (e = pb->num_eqs - 1; e >= 0; e--)
		  pb->eqs[e].coef[pb->safe_vars] = 0;

		for (e = pb->num_subs - 1; e >= 0; e--)
		  pb->subs[e].coef[pb->safe_vars] = 0;
	      }

	    pb->var[pb->safe_vars] = pb->subs[e2].key;
	    pb->forwarding_address[pb->subs[e2].key] = pb->safe_vars;

	    omega_copy_eqn (&(pb->eqs[pb->num_eqs]), &(pb->subs[e2]),
			    pb->num_vars);
	    pb->eqs[pb->num_eqs++].coef[pb->safe_vars] = -1;
	    gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);

	    if (e2 < pb->num_subs - 1)
	      omega_copy_eqn (&(pb->subs[e2]), &(pb->subs[pb->num_subs - 1]),
			      pb->num_vars);

	    pb->num_subs--;
	  }

      omega_unprotect_1 (pb, &idx, NULL);
      free (bring_to_life);
    }

  chain_unprotect (pb);
}

/* Unprotects VAR and simplifies PB.  */

enum omega_result
omega_constrain_variable_sign (omega_pb pb, enum omega_eqn_color color,
			       int var, int sign)
{
  int n_vars = pb->num_vars;
  int e, k, j;

  k = pb->forwarding_address[var];
  if (k < 0)
    {
      k = -1 - k;

      if (sign != 0)
	{
	  e = pb->num_geqs++;
	  omega_copy_eqn (&pb->geqs[e], &pb->subs[k], pb->num_vars);

	  for (j = 0; j <= n_vars; j++)
	    pb->geqs[e].coef[j] *= sign;

	  pb->geqs[e].coef[0]--;
	  pb->geqs[e].touched = 1;
	  pb->geqs[e].color = color;
	}
      else
	{
	  e = pb->num_eqs++;
	  gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);
	  omega_copy_eqn (&pb->eqs[e], &pb->subs[k], pb->num_vars);
	  pb->eqs[e].color = color;
	}
    }
  else if (sign != 0)
    {
      e = pb->num_geqs++;
      omega_init_eqn_zero (&pb->geqs[e], pb->num_vars);
      pb->geqs[e].coef[k] = sign;
      pb->geqs[e].coef[0] = -1;
      pb->geqs[e].touched = 1;
      pb->geqs[e].color = color;
    }
  else
    {
      e = pb->num_eqs++;
      gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);
      omega_init_eqn_zero (&pb->eqs[e], pb->num_vars);
      pb->eqs[e].coef[k] = 1;
      pb->eqs[e].color = color;
    }

  omega_unprotect_variable (pb, var);
  return omega_simplify_problem (pb);
}

/* Add an equation "VAR = VALUE" with COLOR to PB.  */

void
omega_constrain_variable_value (omega_pb pb, enum omega_eqn_color color,
				int var, int value)
{
  int e;
  int k = pb->forwarding_address[var];

  if (k < 0)
    {
      k = -1 - k;
      e = pb->num_eqs++;
      gcc_assert (pb->num_eqs <= OMEGA_MAX_EQS);
      omega_copy_eqn (&pb->eqs[e], &pb->subs[k], pb->num_vars);
      pb->eqs[e].coef[0] -= value;
    }
  else
    {
      e = pb->num_eqs++;
      omega_init_eqn_zero (&pb->eqs[e], pb->num_vars);
      pb->eqs[e].coef[k] = 1;
      pb->eqs[e].coef[0] = -value;
    }

  pb->eqs[e].color = color;
}

/* Return true when the .  Initialize the bounds LOWER_BOUND and UPPER_BOUND for
   the values of variable I.  */

bool
omega_query_variable (omega_pb pb, int i, int *lower_bound, int *upper_bound)
{
  int n_vars = pb->num_vars;
  int e, j;
  bool is_simple;
  bool coupled = false;

  *lower_bound = neg_infinity;
  *upper_bound = pos_infinity;
  i = pb->forwarding_address[i];

  if (i < 0)
    {
      i = -i - 1;

      for (j = 1; j <= n_vars; j++)
	if (pb->subs[i].coef[j] != 0)
	  return true;

      *upper_bound = *lower_bound = pb->subs[i].coef[0];
      return false;
    }

  for (e = pb->num_subs - 1; e >= 0; e--)
    if (pb->subs[e].coef[i] != 0)
      coupled = true;

  for (e = pb->num_eqs - 1; e >= 0; e--)
    if (pb->eqs[e].coef[i] != 0)
      {
	is_simple = true;

	for (j = 1; j <= n_vars; j++)
	  if (i != j && pb->eqs[e].coef[j] != 0)
	    {
	      is_simple = false;
	      coupled = true;
	      break;
	    }

	if (!is_simple)
	  continue;
	else
	  {
	    *lower_bound = *upper_bound = 
	      -pb->eqs[e].coef[i] * pb->eqs[e].coef[0];
	    return false;
	  }
      }

  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (pb->geqs[e].coef[i] != 0)
      {
	if (pb->geqs[e].key == i)
	  set_max (lower_bound, -pb->geqs[e].coef[0]);

	else if (pb->geqs[e].key == -i)
	  set_min (upper_bound, pb->geqs[e].coef[0]);

	else
	  coupled = true;
      }

  return coupled;
}

/* Sets the lower bound L and upper bound U for the values of variable
   I, and sets COULD_BE_ZERO to true if variable I might take value
   zero.  LOWER_BOUND and UPPER_BOUND are bounds on the values of
   variable I.  */

static void
query_coupled_variable (omega_pb pb, int i, int *l, int *u,
			bool *could_be_zero, int lower_bound, int upper_bound)
{
  int e, b1, b2;
  eqn eqn;
  int sign;
  int v;

  /* Preconditions.  */
  gcc_assert (abs (pb->forwarding_address[i]) == 1
	      && pb->num_vars + pb->num_subs == 2
	      && pb->num_eqs + pb->num_subs == 1);

  /* Define variable I in terms of variable V.  */
  if (pb->forwarding_address[i] == -1)
    {
      eqn = &pb->subs[0];
      sign = 1;
      v = 1;
    }
  else
    {
      eqn = &pb->eqs[0];
      sign = -eqn->coef[1];
      v = 2;
    }

  for (e = pb->num_geqs - 1; e >= 0; e--)
    if (pb->geqs[e].coef[v] != 0)
      {
	if (pb->geqs[e].coef[v] == 1)
	  set_max (&lower_bound, -pb->geqs[e].coef[0]);

	else
	  set_min (&upper_bound, pb->geqs[e].coef[0]);
      }

  if (lower_bound > upper_bound)
    {
      *l = pos_infinity;
      *u = neg_infinity;
      *could_be_zero = 0;
      return;
    }

  if (lower_bound == neg_infinity)
    {
      if (eqn->coef[v] > 0)
	b1 = sign * neg_infinity;

      else
	b1 = -sign * neg_infinity;
    }
  else
    b1 = sign * (eqn->coef[0] + eqn->coef[v] * lower_bound);

  if (upper_bound == pos_infinity)
    {
      if (eqn->coef[v] > 0)
	b2 = sign * pos_infinity;

      else
	b2 = -sign * pos_infinity;
    }
  else
    b2 = sign * (eqn->coef[0] + eqn->coef[v] * upper_bound);

  set_max (l, b1 <= b2 ? b1 : b2);
  set_min (u, b1 <= b2 ? b2 : b1);

  *could_be_zero = (*l <= 0 && 0 <= *u
		    && int_mod (eqn->coef[0], abs (eqn->coef[v])) == 0);
}

/* Return false when a lower bound L and an upper bound U for variable
   I in problem PB have been initialized.  */

bool
omega_query_variable_bounds (omega_pb pb, int i, int *l, int *u)
{
  *l = neg_infinity;
  *u = pos_infinity;

  if (!omega_query_variable (pb, i, l, u)
      || (pb->num_vars == 1 && pb->forwarding_address[i] == 1))
    return false;

  if (abs (pb->forwarding_address[i]) == 1 
      && pb->num_vars + pb->num_subs == 2
      && pb->num_eqs + pb->num_subs == 1)
    {
      bool could_be_zero;
      query_coupled_variable (pb, i, l, u, &could_be_zero, neg_infinity,
			      pos_infinity);
      return false;
    }

  return true;
}

/* For problem PB, return an integer that represents the classic data
   dependence direction in function of the DD_LT, DD_EQ and DD_GT bit
   masks that are added to the result.  When DIST_KNOWN is true, DIST
   is set to the classic data dependence distance.  LOWER_BOUND and
   UPPER_BOUND are bounds on the value of variable I, for example, it
   is possible to narrow the iteration domain with safe approximations
   of loop counts, and thus discard some data dependences that cannot
   occur.  */

int
omega_query_variable_signs (omega_pb pb, int i, int dd_lt,
			    int dd_eq, int dd_gt, int lower_bound,
			    int upper_bound, bool *dist_known, int *dist)
{
  int result;
  int l, u;
  bool could_be_zero;

  l = neg_infinity;
  u = pos_infinity;

  omega_query_variable (pb, i, &l, &u);
  query_coupled_variable (pb, i, &l, &u, &could_be_zero, lower_bound,
			  upper_bound);
  result = 0;

  if (l < 0)
    result |= dd_gt;

  if (u > 0)
    result |= dd_lt;

  if (could_be_zero)
    result |= dd_eq;

  if (l == u)
    {
      *dist_known = true;
      *dist = l;
    }
  else
    *dist_known = false;

  return result;
}

/* Keeps the state of the initialization.  */
static bool omega_initialized = false;

/* Initialization of the Omega solver.  */

void
omega_initialize (void)
{
  int i;

  if (omega_initialized)
    return;

  next_wild_card = 0;
  next_key = OMEGA_MAX_VARS + 1;
  packing = (int *) (xcalloc (OMEGA_MAX_VARS, sizeof (int)));
  fast_lookup = (int *) (xcalloc (MAX_KEYS * 2, sizeof (int)));
  fast_lookup_red = (int *) (xcalloc (MAX_KEYS * 2, sizeof (int)));
  hash_master = omega_alloc_eqns (0, HASH_TABLE_SIZE);

  for (i = 0; i < HASH_TABLE_SIZE; i++)
    hash_master[i].touched = -1;

  sprintf (wild_name[0], "1");
  sprintf (wild_name[1], "a");
  sprintf (wild_name[2], "b");
  sprintf (wild_name[3], "c");
  sprintf (wild_name[4], "d");
  sprintf (wild_name[5], "e");
  sprintf (wild_name[6], "f");
  sprintf (wild_name[7], "g");
  sprintf (wild_name[8], "h");
  sprintf (wild_name[9], "i");
  sprintf (wild_name[10], "j");
  sprintf (wild_name[11], "k");
  sprintf (wild_name[12], "l");
  sprintf (wild_name[13], "m");
  sprintf (wild_name[14], "n");
  sprintf (wild_name[15], "o");
  sprintf (wild_name[16], "p");
  sprintf (wild_name[17], "q");
  sprintf (wild_name[18], "r");
  sprintf (wild_name[19], "s");
  sprintf (wild_name[20], "t");
  sprintf (wild_name[40 - 1], "alpha");
  sprintf (wild_name[40 - 2], "beta");
  sprintf (wild_name[40 - 3], "gamma");
  sprintf (wild_name[40 - 4], "delta");
  sprintf (wild_name[40 - 5], "tau");
  sprintf (wild_name[40 - 6], "sigma");
  sprintf (wild_name[40 - 7], "chi");
  sprintf (wild_name[40 - 8], "omega");
  sprintf (wild_name[40 - 9], "pi");
  sprintf (wild_name[40 - 10], "ni");
  sprintf (wild_name[40 - 11], "Alpha");
  sprintf (wild_name[40 - 12], "Beta");
  sprintf (wild_name[40 - 13], "Gamma");
  sprintf (wild_name[40 - 14], "Delta");
  sprintf (wild_name[40 - 15], "Tau");
  sprintf (wild_name[40 - 16], "Sigma");
  sprintf (wild_name[40 - 17], "Chi");
  sprintf (wild_name[40 - 18], "Omega");
  sprintf (wild_name[40 - 19], "xxx");

  omega_initialized = true;
}
