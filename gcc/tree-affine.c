/* Operations with affine combinations of trees.
   Copyright (C) 2005 Free Software Foundation, Inc.
   
This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.
   
GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.
   
You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "output.h"
#include "diagnostic.h"
#include "tree-dump.h"
#include "tree-affine.h"
#include "tree-flow.h"

/* Extends CST as appropriate for the affine combinations COMB.  */

double_int
double_int_ext_for_comb (double_int cst, aff_tree *comb)
{
  return double_int_sext (cst, TYPE_PRECISION (comb->type));
}

/* Initializes affine combination COMB so that its value is zero in TYPE.  */

static void
aff_combination_zero (aff_tree *comb, tree type)
{
  comb->type = type;
  comb->offset = double_int_zero;
  comb->n = 0;
  comb->rest = NULL_TREE;
}

/* Sets COMB to CST.  */

void
aff_combination_const (aff_tree *comb, tree type, double_int cst)
{
  aff_combination_zero (comb, type);
  comb->offset = double_int_ext_for_comb (cst, comb);
}

/* Sets COMB to single element ELT.  */

void
aff_combination_elt (aff_tree *comb, tree type, tree elt)
{
  aff_combination_zero (comb, type);

  comb->n = 1;
  comb->elts[0].val = elt;
  comb->elts[0].coef = double_int_one;
}

/* Scales COMB by SCALE.  */

void
aff_combination_scale (aff_tree *comb, double_int scale)
{
  unsigned i, j;

  scale = double_int_ext_for_comb (scale, comb);
  if (double_int_one_p (scale))
    return;

  if (double_int_zero_p (scale))
    {
      aff_combination_zero (comb, comb->type);
      return;
    }

  comb->offset
    = double_int_ext_for_comb (double_int_mul (scale, comb->offset), comb);
  for (i = 0, j = 0; i < comb->n; i++)
    {
      double_int new_coef;

      new_coef
	= double_int_ext_for_comb (double_int_mul (scale, comb->elts[i].coef),
				   comb);
      /* A coefficient may become zero due to overflow.  Remove the zero
	 elements.  */
      if (double_int_zero_p (new_coef))
	continue;
      comb->elts[j].coef = new_coef;
      comb->elts[j].val = comb->elts[i].val;
      j++;
    }
  comb->n = j;

  if (comb->rest)
    {
      if (comb->n < MAX_AFF_ELTS)
	{
	  comb->elts[comb->n].coef = scale;
	  comb->elts[comb->n].val = comb->rest;
	  comb->rest = NULL_TREE;
	  comb->n++;
	}
      else
	comb->rest = fold_build2 (MULT_EXPR, comb->type, comb->rest, 
				  double_int_to_tree (comb->type, scale));
    }
}

/* Returns the element of COMB whose value is VAL, or NULL if no such
   element exists.  If IDX is not NULL, it is set to the index of VAL in
   COMB.  */
	      
static struct aff_comb_elt *
aff_combination_find_elt (aff_tree *comb, tree val, unsigned *idx)
{
  unsigned i;

  for (i = 0; i < comb->n; i++)
    if (operand_equal_p (comb->elts[i].val, val, 0))
      {
	if (idx)
	  *idx = i;

	return &comb->elts[i];
      }

  return NULL;
}

/* Adds ELT * SCALE to COMB.  */

void
aff_combination_add_elt (aff_tree *comb, tree elt, double_int scale)
{
  unsigned i;
  struct aff_comb_elt *oelt;

  scale = double_int_ext_for_comb (scale, comb);
  if (double_int_zero_p (scale))
    return;

  oelt = aff_combination_find_elt (comb, elt, &i);
  if (oelt)
    {
      double_int new_coef;

      new_coef = double_int_add (oelt->coef, scale);
	new_coef = double_int_ext_for_comb (new_coef, comb);
	if (!double_int_zero_p (new_coef))
	  {
	    oelt->coef = new_coef;
	    return;
	  }

	comb->n--;
	comb->elts[i] = comb->elts[comb->n];

	if (comb->rest)
	  {
	    gcc_assert (comb->n == MAX_AFF_ELTS - 1);
	    comb->elts[comb->n].coef = double_int_one;
	    comb->elts[comb->n].val = comb->rest;
	    comb->rest = NULL_TREE;
	    comb->n++;
	  }
	return;
      }
  if (comb->n < MAX_AFF_ELTS)
    {
      comb->elts[comb->n].coef = scale;
      comb->elts[comb->n].val = elt;
      comb->n++;
      return;
    }

  elt = fold_convert (comb->type, elt);
  if (!double_int_one_p (scale))
    elt = fold_build2 (MULT_EXPR, comb->type, elt,
		       double_int_to_tree (comb->type, scale)); 

  if (comb->rest)
    comb->rest = fold_build2 (PLUS_EXPR, comb->type, comb->rest, elt);
  else
    comb->rest = elt;
}

/* Adds COMB2 to COMB1.  */

void
aff_combination_add (aff_tree *comb1, aff_tree *comb2)
{
  unsigned i;

  comb1->offset
    = double_int_ext_for_comb (double_int_add (comb1->offset, comb2->offset),
			       comb1);
  for (i = 0; i < comb2->n; i++)
    aff_combination_add_elt (comb1, comb2->elts[i].val, comb2->elts[i].coef);
  if (comb2->rest)
    aff_combination_add_elt (comb1, comb2->rest, double_int_one);
}

/* Converts affine combination COMB to TYPE.  */

void
aff_combination_convert (aff_tree *comb, tree type)
{
  unsigned i, j;
  tree comb_type = comb->type;

  gcc_assert (TYPE_PRECISION (type) <= TYPE_PRECISION (comb_type));
  comb->type = type;
  if (comb->rest)
    comb->rest = fold_convert (type, comb->rest);

  if (TYPE_PRECISION (type) == TYPE_PRECISION (comb_type))
    return;

  comb->offset = double_int_ext_for_comb (comb->offset, comb);
  for (i = j = 0; i < comb->n; i++)
    {
      double_int new_coef = double_int_ext_for_comb (comb->elts[i].coef, comb);
      if (double_int_zero_p (new_coef))
	continue;
      comb->elts[j].coef = new_coef;
      comb->elts[j].val = fold_convert (type, comb->elts[i].val);
      j++;
    }

  comb->n = j;
  if (comb->n < MAX_AFF_ELTS && comb->rest)
    {
      comb->elts[comb->n].coef = double_int_one;
      comb->elts[comb->n].val = comb->rest;
      comb->rest = NULL_TREE;
      comb->n++;
    }
}

/* Splits EXPR into an affine combination of parts.  */

void
tree_to_aff_combination (tree expr, tree type, aff_tree *comb)
{
  aff_tree tmp;
  enum tree_code code;
  tree cst, core, toffset;
  HOST_WIDE_INT bitpos, bitsize;
  enum machine_mode mode;
  int unsignedp, volatilep;

  STRIP_NOPS (expr);

  code = TREE_CODE (expr);
  switch (code)
    {
    case INTEGER_CST:
      aff_combination_const (comb, type, tree_to_double_int (expr));
      return;

    case PLUS_EXPR:
    case MINUS_EXPR:
      tree_to_aff_combination (TREE_OPERAND (expr, 0), type, comb);
      tree_to_aff_combination (TREE_OPERAND (expr, 1), type, &tmp);
      if (code == MINUS_EXPR)
	aff_combination_scale (&tmp, double_int_minus_one);
      aff_combination_add (comb, &tmp);
      return;

    case MULT_EXPR:
      cst = TREE_OPERAND (expr, 1);
      if (TREE_CODE (cst) != INTEGER_CST)
	break;
      tree_to_aff_combination (TREE_OPERAND (expr, 0), type, comb);
      aff_combination_scale (comb, tree_to_double_int (cst));
      return;

    case NEGATE_EXPR:
      tree_to_aff_combination (TREE_OPERAND (expr, 0), type, comb);
      aff_combination_scale (comb, double_int_minus_one);
      return;

    case ADDR_EXPR:
      core = get_inner_reference (TREE_OPERAND (expr, 0), &bitsize, &bitpos,
				  &toffset, &mode, &unsignedp, &volatilep,
				  false);
      if (bitpos % BITS_PER_UNIT != 0)
	break;
      aff_combination_const (comb, type,
			     uhwi_to_double_int (bitpos / BITS_PER_UNIT));
      core = build_fold_addr_expr (core);
      if (TREE_CODE (core) == ADDR_EXPR)
	aff_combination_add_elt (comb, core, double_int_one);
      else
	{
	  tree_to_aff_combination (core, type, &tmp);
	  aff_combination_add (comb, &tmp);
	}
      if (toffset)
	{
	  tree_to_aff_combination (toffset, type, &tmp);
	  aff_combination_add (comb, &tmp);
	}
      return;

    default:
      break;
    }

  aff_combination_elt (comb, type, expr);
}

/* Similar to tree_to_aff_combination, but follows SSA name definitions
   and expands them recursively.

   FIXME -- the way this function is now defined, it may have exponential
   time complexity.  */

void
tree_to_aff_combination_expand (tree expr, tree type, aff_tree *comb)
{
  unsigned i;
  aff_tree to_add, current, curre;
  tree e, def, rhs;
  double_int scale;

  tree_to_aff_combination (expr, type, comb);
  aff_combination_zero (&to_add, type);
  for (i = 0; i < comb->n; i++)
    {
      e = comb->elts[i].val;
      if (TREE_CODE (e) != SSA_NAME)
	continue;
      def = SSA_NAME_DEF_STMT (e);
      if (TREE_CODE (def) != MODIFY_EXPR
	  || TREE_OPERAND (def, 0) != e)
	continue;

      rhs = TREE_OPERAND (def, 1);
      if (TREE_CODE (rhs) != SSA_NAME
	  && !EXPR_P (rhs)
	  && !is_gimple_min_invariant (rhs))
	continue;

      /* Accumulate the new terms to TO_ADD, so that we do not modify
	 COMB while traversing it.  */
      scale = comb->elts[i].coef;
      tree_to_aff_combination_expand (rhs, type, &current);
      aff_combination_zero (&curre, type);
      aff_combination_add_elt (&curre, e, double_int_neg (scale));
      aff_combination_scale (&current, scale);
      aff_combination_add (&to_add, &current);
      aff_combination_add (&to_add, &curre);
    }
  aff_combination_add (comb, &to_add);
}

/* Creates EXPR + ELT * SCALE in TYPE.  EXPR is taken from affine
   combination COMB.  */

static tree
add_elt_to_tree (tree expr, tree type, tree elt, double_int scale,
		 aff_tree *comb)
{
  enum tree_code code;

  scale = double_int_ext_for_comb (scale, comb);
  elt = fold_convert (type, elt);

  if (double_int_one_p (scale))
    {
      if (!expr)
	return elt;

      return fold_build2 (PLUS_EXPR, type, expr, elt);
    }

  if (double_int_minus_one_p (scale))
    {
      if (!expr)
	return fold_build1 (NEGATE_EXPR, type, elt);

      return fold_build2 (MINUS_EXPR, type, expr, elt);
    }

  if (!expr)
    return fold_build2 (MULT_EXPR, type, elt,
			double_int_to_tree (type, scale));

  if (double_int_negative_p (scale))
    {
      code = MINUS_EXPR;
      scale = double_int_neg (scale);
    }
  else
    code = PLUS_EXPR;

  elt = fold_build2 (MULT_EXPR, type, elt,
		     double_int_to_tree (type, scale));
  return fold_build2 (code, type, expr, elt);
}

/* Makes tree from the affine combination COMB.  */

tree
aff_combination_to_tree (aff_tree *comb)
{
  tree type = comb->type;
  tree expr = comb->rest;
  unsigned i;
  double_int off, sgn;

  gcc_assert (comb->n == MAX_AFF_ELTS || comb->rest == NULL_TREE);

  for (i = 0; i < comb->n; i++)
    expr = add_elt_to_tree (expr, type, comb->elts[i].val, comb->elts[i].coef,
			    comb);

  /* Ensure that we get x - 1, not x + (-1) or x + 0xff..f if x is
     unsigned.  */
  if (double_int_negative_p (comb->offset))
    {
      off = double_int_neg (comb->offset);
      sgn = double_int_minus_one;
    }
  else
    {
      off = comb->offset;
      sgn = double_int_one;
    }
  return add_elt_to_tree (expr, type, double_int_to_tree (type, off), sgn,
			  comb);
}

/* Copies the tree elements of COMB to ensure that they are not shared.  */

void
unshare_aff_combination (aff_tree *comb)
{
  unsigned i;

  for (i = 0; i < comb->n; i++)
    comb->elts[i].val = unshare_expr (comb->elts[i].val);
  if (comb->rest)
    comb->rest = unshare_expr (comb->rest);
}

/* Remove M-th element from COMB.  */

void
aff_combination_remove_elt (aff_tree *comb, unsigned m)
{
  comb->n--;
  if (m <= comb->n)
    comb->elts[m] = comb->elts[comb->n];
  if (comb->rest)
    {
      comb->elts[comb->n].coef = double_int_one;
      comb->elts[comb->n].val = comb->rest;
      comb->rest = NULL_TREE;
      comb->n++;
    }
}

/* If VAL != CST * DIV for any constant CST, returns false.
   Otherwise, if VAL != 0 (and hence CST != 0), and *MULT_SET is true,
   additionally compares CST and MULT, and if they are different,
   returns false.  Finally, if neither of these two cases occcur,
   true is returned, and if CST != 0, CST is stored to MULT and
   MULT_SET is set to true.  */

static bool
double_int_constant_multiple_p (double_int val, double_int div,
				bool *mult_set, double_int *mult)
{
  double_int rem, cst;

  if (double_int_zero_p (val))
    return true;

  if (double_int_zero_p (div))
    return false;

  cst = double_int_sdiv (val, div, FLOOR_DIV_EXPR, &rem);
  if (!double_int_zero_p (rem))
    return false;

  if (*mult_set && !double_int_equal_p (*mult, cst))
    return false;

  *mult_set = true;
  *mult = cst;
  return true;
}

/* Returns true if VAL = X * DIV for some constant X.  If this is the case,
   X is stored to MULT.  */

bool
aff_combination_constant_multiple_p (aff_tree *val, aff_tree *div,
				     double_int *mult)
{
  bool mult_set = false;
  unsigned i;

  if (val->n == 0 && double_int_zero_p (val->offset))
    {
      *mult = double_int_zero;
      return true;
    }
  if (val->n != div->n)
    return false;

  if (val->rest || div->rest)
    return false;

  if (!double_int_constant_multiple_p (val->offset, div->offset,
				       &mult_set, mult))
    return false;

  for (i = 0; i < div->n; i++)
    {
      struct aff_comb_elt *elt
	      = aff_combination_find_elt (val, div->elts[i].val, NULL);
      if (!elt)
	return false;
      if (!double_int_constant_multiple_p (elt->coef, div->elts[i].coef,
					   &mult_set, mult))
	return false;
    }

  gcc_assert (mult_set);
  return true;
}
