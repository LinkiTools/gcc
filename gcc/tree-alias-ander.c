/* Tree based Andersen points-to analysis
   Copyright (C) 2002 Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dberlin@dberlin.org>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include "config.h"
#include "system.h"
#include "ggc.h"
#include "tree-alias-type.h"
#include "tree-alias-ecr.h"
#include "tree-alias-ander.h"

#include "flags.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "errors.h"
#include "expr.h"
#include "diagnostic.h"
#include "tree.h"
#include "c-common.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "ssa.h"
#include "varray.h"
#include "c-tree.h"
#include "tree-simple.h"
#include "hashtab.h"
#include "splay-tree.h"
#include "engine/util.h"
#include "libcompat/regions.h"
#include "andersen_terms.h"

/* Andersen's interprocedural points-to analysis.
   This is a flow-insensitive, context insensitive algorithm.  It
   also does not distinguish structure fields.
   It works through non-standard type inferencing.
   It does distinguish between direction of assignments.

   By non-standard types, we do not mean "integer, float", etc. The
   types here represent sets of abstract locations, including
   relations between abstract locations (thus modeling the store).

   We then perform type inferencing, which attempts to infer the
   non-standard types involved in each expression, and get the
   points-to sets as a result (Since the types represent the store
   locations). 
*/

/* Todo list:
   * Don't pass alias ops as first argument, just have a global 
     "current_alias_ops".
*/
static unsigned int id_num = 1;
#define ANDERSEN_DEBUG 0
static region andersen_rgn;
static void andersen_simple_assign PARAMS ((struct tree_alias_ops *,
					    alias_typevar, alias_typevar));
static void andersen_addr_assign PARAMS ((struct tree_alias_ops *,
					  alias_typevar, alias_typevar));
static void andersen_ptr_assign PARAMS ((struct tree_alias_ops *,
					 alias_typevar, alias_typevar));
static void andersen_op_assign PARAMS ((struct tree_alias_ops *,
					alias_typevar, varray_type));
static void andersen_heap_assign PARAMS ((struct tree_alias_ops *,
					  alias_typevar));
static void andersen_assign_ptr PARAMS ((struct tree_alias_ops *,
					 alias_typevar, alias_typevar));
static void andersen_function_def PARAMS ((struct tree_alias_ops *,
					   alias_typevar, varray_type,
					   alias_typevar));
static void andersen_function_call PARAMS ((struct tree_alias_ops *,
					    alias_typevar, alias_typevar,
					    varray_type));
static void andersen_init PARAMS ((struct tree_alias_ops *));
static void andersen_cleanup PARAMS ((struct tree_alias_ops *));
static bool andersen_may_alias PARAMS ((struct tree_alias_ops *,
					alias_typevar, alias_typevar));
static alias_typevar andersen_add_var
PARAMS ((struct tree_alias_ops *, tree));
static alias_typevar andersen_add_var_same
PARAMS ((struct tree_alias_ops *, tree, alias_typevar));
static splay_tree ptamap;
static struct tree_alias_ops andersen_ops = {
  andersen_init,
  andersen_cleanup,
  andersen_add_var,
  andersen_add_var_same,
  andersen_simple_assign,
  andersen_addr_assign,
  andersen_ptr_assign,
  andersen_op_assign,
  andersen_heap_assign,
  andersen_assign_ptr,
  andersen_function_def,
  andersen_function_call,
  andersen_may_alias,
  0,				/* data */
  0				/* Currently non-interprocedural */
};
struct tree_alias_ops *andersen_alias_ops = &andersen_ops;

static void term_inclusion PARAMS ((aterm, aterm));
static void pta_init PARAMS ((void));
static void pta_reset PARAMS ((void));
static aterm get_ref PARAMS ((aterm));
static argterm fun_rec_aterm PARAMS ((aterm_list));
static aterm pta_make_lam PARAMS ((const char *, aterm, aterm_list));
static aterm pta_make_ref PARAMS ((const char *));
static aterm pta_bottom PARAMS ((void));
static aterm pta_join PARAMS ((aterm, aterm));
static aterm pta_deref PARAMS ((aterm));
static aterm pta_rvalue PARAMS ((aterm));
static aterm pta_address PARAMS ((aterm));
static void pta_assignment PARAMS ((aterm, aterm));
static aterm pta_make_fun PARAMS ((const char *, aterm, aterm_list));
static aterm pta_application PARAMS ((aterm, aterm_list));

typedef aterm contents_type;
static contents_type pta_get_contents PARAMS ((aterm));
static void pr_ptset_aterm_elem PARAMS ((aterm));
static void pta_pr_ptset PARAMS ((contents_type));
static int pta_get_ptsize PARAMS ((contents_type));
static int flag_print_constraints = 0;

/* Hook for debugging */
static void
term_inclusion (t1, t2)
     aterm t1;
     aterm t2;
{
  if (flag_print_constraints)
    {
      aterm_print (stderr, t1);
      fprintf (stderr, " <= ");
      aterm_print (stderr, t2);
      puts ("");
    }
  aterm_inclusion (t1, t2);
}

static void
pta_init ()
{
  andersen_terms_init ();
}

static void
pta_reset ()
{
  andersen_terms_reset ();
}


static aterm
get_ref (t)
     aterm t;
{
  struct ref_decon r_decon;
  r_decon = ref_decon (t);

  assert (r_decon.f1);

  return r_decon.f1;
}

static argterm
fun_rec_aterm (args)
     aterm_list args;
{
  region scratch;
  int counter = 0;
  argterm rest, result;
  aterm_list_scanner scan;
  aterm temp;
  char field_name[512];
  argterm_map map;

  scratch = newregion ();
  map = new_argterm_map (scratch);
  aterm_list_scan (args, &scan);
  while (aterm_list_next (&scan, &temp))
    {
      snprintf (field_name, 512, "%d", counter++);
      argterm_map_cons (argterm_make_field (field_name, temp), map);
    }

  rest = argterm_wild ();
  /* rest = argterm_fresh(); */

  /*  safe since field_add makes a copy of the string*/
  result = argterm_row (map, rest);

  deleteregion (scratch);

  return result;
}


static aterm
pta_make_lam (id, ret, args)
     const char *id;
     aterm ret;
     aterm_list args;
{
  return lam (label_term_constant (id), fun_rec_aterm (args), ret);
}

static aterm
pta_make_ref (id)
     const char *id;
{

  aterm var = aterm_fresh (id);

  label_term tag = label_term_constant (id);

  return ref (tag, var, var);
}

static aterm
pta_bottom (void)
{
  return aterm_zero ();
}

static aterm
pta_join (t1, t2)
     aterm t1;
     aterm t2;
{
  aterm result;
  region scratch_rgn = newregion ();
  aterm_list list = new_aterm_list (scratch_rgn);

  aterm_list_cons (t1, list);
  aterm_list_cons (t2, list);


  result = aterm_union (list);
  deleteregion (scratch_rgn);

  return result;
}

static aterm
pta_deref (t1)
     aterm t1;
{
  return ref_proj2 (t1);
}

static aterm
pta_rvalue (t1)
     aterm t1;
{
  return pta_deref (t1);
}

static aterm
pta_address (t1)
     aterm t1;
{
  return ref (label_term_one (), aterm_one (), t1);
}

static void
pta_assignment (t1, t2)
     aterm t1;
     aterm t2;
{
  term_inclusion (t1, ref_pat1 (t2));
}

static aterm
pta_make_fun (name, ret, args)
     const char *name;
     aterm ret;
     aterm_list args;
{
  aterm temp;
  aterm_list_scanner scan;
  region scratch_rgn = newregion ();
  aterm_list arg_list = new_aterm_list (scratch_rgn);

  aterm_list_scan (args, &scan);

  while (aterm_list_next (&scan, &temp))
    {
      aterm_list_cons (get_ref (temp), arg_list);
    }

  return pta_make_lam (name, get_ref (ret), arg_list);
}

static aterm
pta_application (t, actuals)
     aterm t;
     aterm_list actuals;
{
  argterm args = fun_rec_aterm (actuals);

  term_inclusion (t, lam_pat1 (args));
  return pta_address (lam_proj2 (t));
}

static contents_type
pta_get_contents (t)
     aterm t;
{
  struct ref_decon t_decon;
  t_decon = ref_decon (t);

  return t_decon.f1;
}

static void
pr_ptset_aterm_elem (t)
     aterm t;
{
  struct ref_decon ref;
  struct lam_decon lam;

  ref = ref_decon (t);
  lam = lam_decon (t);

  fprintf (stderr, ",");
  if (ref.f0)
    label_term_print (stderr, ref.f0);
  else if (lam.f0)
    label_term_print (stderr, lam.f0);
  /*
     fprintf(stderr, ",");
     aterm_pr(stdout,(aterm)t);
   */
}


static void
pta_pr_ptset (t)
     contents_type t;
{
  int size;
  region scratch_rgn;
  aterm_list ptset;
  scratch_rgn = newregion ();
  ptset = aterm_list_copy (scratch_rgn, aterm_tlb (t));

  size = aterm_list_length (ptset);

  fprintf (stderr, "{");
  if (!aterm_list_empty (ptset))
    {
      struct ref_decon ref;
      struct lam_decon lam;
      ref = ref_decon (aterm_list_head (ptset));
      lam = lam_decon (aterm_list_head (ptset));
      if (ref.f0)
	label_term_print (stderr, ref.f0);
      else if (lam.f0)
	label_term_print (stderr, lam.f0);

      /*      aterm_pr(stdout,aterm_hd(ptset)); */
      ptset = aterm_list_tail (ptset);
    }
  aterm_list_app (ptset, pr_ptset_aterm_elem);
  fprintf (stderr, "}(%d)\n", size);
  deleteregion (scratch_rgn);
}

static int
pta_get_ptsize (t)
     contents_type t;
{
  aterm_list ptset = aterm_tlb (t);
  return aterm_list_length (ptset);
}

/* Initialize Andersen alias analysis.  
   Currently does nothing.  */
static void
andersen_init (ops)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
{
  pta_init ();
  flag_eliminate_cycles = 1;
  flag_merge_projections = 1;
  ptamap = splay_tree_new (splay_tree_compare_pointers, NULL, NULL);
  andersen_rgn = newregion ();
}

static int
print_out_result (node, data)
     splay_tree_node node;
     void *data ATTRIBUTE_UNUSED;
{
  fprintf (stderr, "%s :=",
	   alias_get_name (ALIAS_TVAR_DECL (((alias_typevar) node->value))));
  pta_pr_ptset (pta_get_contents ((aterm) node->key));
  return 0;
}

/* Cleanup after Andersen alias analysis. 
   Currently does nothing.  */
static void
andersen_cleanup (ops)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
{
#if ANDERSEN_DEBUG
  FILE *dot;
  char name[512];
  andersen_terms_stats (stderr);
  snprintf (name, 512, "%s.dot", get_name (current_function_decl));
  dot = fopen (name, "w");
  andersen_terms_print_graph (dot);
  fclose (dot);
  splay_tree_foreach (ptamap, print_out_result, NULL);
#endif
  pta_reset ();
  splay_tree_delete (ptamap);
  deleteregion (andersen_rgn);

}

/* Add decl to the analyzer, and return a typevar for it.  For
   Andersen, we create a new alias typevar for the declaration, and
   return that.  */

static alias_typevar
andersen_add_var (ops, decl)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     tree decl;
{
  alias_typevar ret;
#if ANDERSEN_DEBUG
  fprintf (stderr, "Andersen Adding variable %s\n", alias_get_name (decl));

#endif
  if (alias_get_name (decl) != NULL)
    {
      ret = alias_tvar_new_with_aterm (decl,
				       pta_make_ref (alias_get_name (decl)));
    }
  else
    {
      char *tmp_name;
      ASM_FORMAT_PRIVATE_NAME (tmp_name, "unnamed var", id_num++);
      ret = alias_tvar_new_with_aterm (decl, pta_make_ref (tmp_name));
    }
  splay_tree_insert (ptamap, (splay_tree_key) ALIAS_TVAR_ATERM (ret),
		     (splay_tree_value) ret);
  return ret;
}

/* Add a variable to the analyzer that is equivalent (as far as
   aliases go) to some existing typevar.  
   For Andersen, we just call a function that does this for us.  */
static alias_typevar
andersen_add_var_same (ops, decl, tv)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     tree decl;
     alias_typevar tv;
{
  alias_typevar ret;
#if ANDERSEN_DEBUG
  fprintf (stderr, "Andersen Adding variable %s same as %s\n",
	   alias_get_name (decl), alias_get_name (ALIAS_TVAR_DECL (tv)));
#endif
  if (alias_get_name (decl) != NULL)
    ret = alias_tvar_new_with_aterm (decl,
				     pta_make_ref (alias_get_name (decl)));
  else
    {
      char *tmp_name;
      ASM_FORMAT_PRIVATE_NAME (tmp_name, "unnamed var", id_num++);
      ret = alias_tvar_new_with_aterm (decl, pta_make_ref (tmp_name));
    }

  pta_join (ALIAS_TVAR_ATERM (tv), ALIAS_TVAR_ATERM (ret));
  splay_tree_insert (ptamap, (splay_tree_key) ALIAS_TVAR_ATERM (ret),
		     (splay_tree_value) ret);
  return ret;
}

/* Inference for simple assignment (lhs = rhs) */
static void
andersen_simple_assign (ops, lhs, rhs)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     alias_typevar lhs;
     alias_typevar rhs;
{
  pta_assignment (ALIAS_TVAR_ATERM (lhs),
		  pta_rvalue (ALIAS_TVAR_ATERM (rhs)));

#if ANDERSEN_DEBUG
  fprintf (stderr, "Andersen simple assignment %s = %s\n",
	   alias_get_name (ALIAS_TVAR_DECL (lhs)),
	   alias_get_name (ALIAS_TVAR_DECL (rhs)));
#endif

}

/* Inference for address assignment (lhs = &addr) */
static void
andersen_addr_assign (ops, lhs, addr)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     alias_typevar lhs;
     alias_typevar addr;
{
  if (addr == NULL)
    return;
  pta_assignment (ALIAS_TVAR_ATERM (lhs),
		  pta_rvalue (pta_address (ALIAS_TVAR_ATERM (addr))));
#if ANDERSEN_DEBUG
  fprintf (stderr, "Andersen address assignment %s = &%s\n",
	   alias_get_name (ALIAS_TVAR_DECL (lhs)),
	   alias_get_name (ALIAS_TVAR_DECL (addr)));
#endif

}


/* Inference for pointer assignment (lhs = *ptr) */
static void
andersen_ptr_assign (ops, lhs, ptr)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     alias_typevar lhs;
     alias_typevar ptr;
{

  if (ptr == NULL)
    return;
#if ANDERSEN_DEBUG
  fprintf (stderr, "Andersen pointer assignment %s = *%s\n",
	   alias_get_name (ALIAS_TVAR_DECL (lhs)),
	   alias_get_name (ALIAS_TVAR_DECL (ptr)));
#endif
  pta_assignment (ALIAS_TVAR_ATERM (lhs),
		  pta_rvalue (pta_deref (ALIAS_TVAR_ATERM (ptr))));

}

/* Inference rule for operations (lhs = operation(operands)) */
static void
andersen_op_assign (ops, lhs, operands)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     alias_typevar lhs;
     varray_type operands;
{
  size_t i;
#if ANDERSEN_DEBUG
  fprintf (stderr, "Andersen op assignment %s = op(...)\n",
	   alias_get_name (ALIAS_TVAR_DECL (lhs)));
#endif

  for (i = 0; i < VARRAY_ACTIVE_SIZE (operands); i++)
    {
      alias_typevar tv = VARRAY_GENERIC_PTR (operands, i);

      if (tv == NULL)
	continue;

      pta_assignment (ALIAS_TVAR_ATERM (lhs),
		      pta_rvalue (ALIAS_TVAR_ATERM (tv)));
/*      pta_join  (ALIAS_TVAR_ATERM (lhs), ALIAS_TVAR_ATERM (tv));*/
    }
}

/* Inference for heap assignment (lhs = alloc) */
static void
andersen_heap_assign (ops, lhs)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     alias_typevar lhs;
{
#if 0
  alias_type type1;
  ECR tau;
  type1 = ECR_get_type (alias_tvar_get_ECR (lhs));
  tau = alias_ltype_loc (type1);

  if (ECR_get_type (tau) == alias_bottom)
    ECR_set_type (tau, alias_ltype_new ());
#endif
}

/* Inference for assignment to a pointer (*ptr = rhs) */
static void
andersen_assign_ptr (ops, ptr, rhs)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     alias_typevar ptr;
     alias_typevar rhs;
{

  if (rhs == NULL)
    return;
  pta_assignment (pta_deref (ALIAS_TVAR_ATERM (ptr)),
		  pta_rvalue (ALIAS_TVAR_ATERM (rhs)));
#if ANDERSEN_DEBUG
  fprintf (stderr, "Andersen assignment to pointer  *");
  print_c_node (stderr, ALIAS_TVAR_DECL (ptr));
  fprintf (stderr, " = ");
  print_c_node (stderr, ALIAS_TVAR_DECL (rhs));
  fprintf (stderr, "\n");
#endif
}

/* Inference for a function definition. */

static void
andersen_function_def (ops, func, params, retval)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     alias_typevar func;
     varray_type params;
     alias_typevar retval;
{
  aterm_list args = new_aterm_list (andersen_rgn);
  aterm fun_type;

  size_t l = VARRAY_ACTIVE_SIZE (params);
  size_t i;
  /* Set up the arguments for the new function type. */
  for (i = 0; i < l; i++)
    {
      alias_typevar tv = VARRAY_GENERIC_PTR (params, i);
      aterm_list_cons (ALIAS_TVAR_ATERM (tv), args);
    }
  fun_type = pta_make_fun (get_name (ALIAS_TVAR_DECL (func)),
			   ALIAS_TVAR_ATERM (retval), args);
  pta_assignment (ALIAS_TVAR_ATERM (func), fun_type);
}

/* Inference for a function call assignment */
static void
andersen_function_call (ops, lhs, func, args)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     alias_typevar lhs;
     alias_typevar func;
     varray_type args;
{
  aterm_list actuals = new_aterm_list (andersen_rgn);
  aterm ftype = ALIAS_TVAR_ATERM (func);
  aterm ret = NULL;
  aterm res;
  size_t i;

  if (lhs)
    ret = ALIAS_TVAR_ATERM (lhs);
  for (i = 0; i < VARRAY_ACTIVE_SIZE (args); i++)
    {
      alias_typevar argtv = VARRAY_GENERIC_PTR (args, i);
      aterm arg = ALIAS_TVAR_ATERM (argtv);
      aterm_list_cons (pta_rvalue (arg), actuals);
    }
  aterm_list_reverse (actuals);
  res = pta_application (pta_rvalue (ftype), actuals);
  if (ret)
    pta_assignment (ret, pta_rvalue (res));
}


static aterm stupid_hack;
static bool eq_to_var PARAMS ((const aterm));
static bool
eq_to_var (term)
     const aterm term;
{
  return stupid_hack == term;
}

static bool
andersen_may_alias (ops, ptrtv, vartv)
     struct tree_alias_ops *ops ATTRIBUTE_UNUSED;
     alias_typevar ptrtv;
     alias_typevar vartv;
{
  aterm_list ptset;

  ptset = aterm_tlb (pta_get_contents (ALIAS_TVAR_ATERM (ptrtv)));

  if (aterm_list_empty (ptset))
    return false;

  stupid_hack = ALIAS_TVAR_ATERM (vartv);

  return aterm_list_find (ptset, eq_to_var);
}
