/* Tree lowering pass.  Lowers GIMPLE into unstructured form.

   Copyright (C) 2003 Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "errors.h"
#include "varray.h"
#include "tree-simple.h"
#include "tree-inline.h"
#include "diagnostic.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "tree-flow.h"
#include "timevar.h"
#include "except.h"
#include "hashtab.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "toplev.h"

struct lower_data
{
  /* Block the current statement belongs to.  */
  tree block;

  /* The end of chain of CASE_LABEL_EXPRs in the innermost SWITCH_EXPR it
     belongs to.  */
  tree_stmt_iterator encl_switch_body;
};

static void lower_stmt_body (tree *, struct lower_data *);
static void lower_stmt (tree_stmt_iterator *, struct lower_data *);
static void lower_bind_expr (tree_stmt_iterator *, struct lower_data *);
static void lower_cond_expr (tree_stmt_iterator *, struct lower_data *);
static void lower_switch_expr (tree_stmt_iterator *, struct lower_data *);
static void lower_case_label_expr (tree_stmt_iterator *, struct lower_data *);
static bool simple_goto_p (tree);
static void expand_vars (tree);

/* Lowers the BODY.  */
void
lower_function_body (tree *body)
{
  struct lower_data data;

  if (TREE_CODE (*body) != BIND_EXPR)
    abort ();

  data.block = DECL_INITIAL (current_function_decl);
  BLOCK_SUBBLOCKS (data.block) = NULL_TREE;
  BLOCK_CHAIN (data.block) = NULL_TREE;

  expand_vars (BIND_EXPR_VARS (*body));
  lower_stmt_body (&BIND_EXPR_BODY (*body), &data);

  if (data.block != DECL_INITIAL (current_function_decl))
    abort ();
  BLOCK_SUBBLOCKS (data.block) =
	  blocks_nreverse (BLOCK_SUBBLOCKS (data.block));
}

/* Lowers the EXPR.  Unlike gimplification the statements are not relowered
   when they are changed -- if this has to be done, the lowering routine must
   do it explicitly.  DATA is passed through the recursion.  */

static void
lower_stmt_body (tree *expr, struct lower_data *data)
{
  tree_stmt_iterator tsi;

  for (tsi = tsi_start (expr); !tsi_end_p (tsi); )
    lower_stmt (&tsi, data);
}

/* Lowers statement TSI.  DATA is passed through the recursion.  */
static void
lower_stmt (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree stmt = tsi_stmt (*tsi);

  if (EXPR_LOCUS (stmt))
    TREE_BLOCK (stmt) = data->block;

  switch (TREE_CODE (stmt))
    {
    case BIND_EXPR:
      lower_bind_expr (tsi, data);
      /* Avoid moving the tsi -- it is moved by delinking the statement
	 already.  */
      return;

    case COMPOUND_EXPR:
      abort ();

    case NOP_EXPR:
    case ASM_EXPR:
    case RETURN_EXPR:
    case MODIFY_EXPR:
    case CALL_EXPR:
    case GOTO_EXPR:
    case LABEL_EXPR:
    case VA_ARG_EXPR:
    case RESX_EXPR:
      break;

    case COND_EXPR:
      lower_cond_expr (tsi, data);
      break;

    case SWITCH_EXPR:
      lower_switch_expr (tsi, data);
      break;

    case CASE_LABEL_EXPR:
      lower_case_label_expr (tsi, data);
      /* Avoid moving the tsi -- it is moved by delinking the statement
	 already.  */
      return;

    default:
      print_node_brief (stderr, "", tsi_stmt (*tsi), 0);
      abort ();
    }

  tsi_next (tsi);
}

/* Expands declarations of variables in list VARS.  */

static void
expand_vars (tree vars)
{
  /* Expand the variables.  Copied from expr.c.  Expanding initializers is
     omitted, as it should be expressed explicitly in gimple.  */

  for (; vars; vars = TREE_CHAIN (vars))
    {
      tree var = vars;

      if (DECL_EXTERNAL (var))
	continue;

      if (TREE_STATIC (var))
	/* If this is an inlined copy of a static local variable,
	   look up the original decl.  */
	var = DECL_ORIGIN (var);

      if (TREE_STATIC (var)
	  ? TREE_ASM_WRITTEN (var)
	  : DECL_RTL_SET_P (var))
	continue;

      if (TREE_CODE (var) == VAR_DECL && DECL_DEFER_OUTPUT (var))
	{
	  /* Prepare a mem & address for the decl.  */
	  rtx x;
		    
	  if (TREE_STATIC (var))
	    abort ();

	  x = gen_rtx_MEM (DECL_MODE (var),
			   gen_reg_rtx (Pmode));

	  set_mem_attributes (x, var, 1);
	  SET_DECL_RTL (var, x);
	}
      else if ((*lang_hooks.expand_decl) (var))
	/* OK.  */;
      else if (TREE_CODE (var) == VAR_DECL && !TREE_STATIC (var))
	expand_decl (var);
      else if (TREE_CODE (var) == VAR_DECL && TREE_STATIC (var))
	rest_of_decl_compilation (var, NULL, 0, 0);
      else if (TREE_CODE (var) == TYPE_DECL
	       || TREE_CODE (var) == CONST_DECL
	       || TREE_CODE (var) == FUNCTION_DECL
	       || TREE_CODE (var) == LABEL_DECL)
	/* No expansion needed.  */;
      else
	abort ();
    }
}

/* Lowers a bind_expr TSI.  DATA is passed through the recursion.  */

static void
lower_bind_expr (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree old_block = data->block;
  tree stmt = tsi_stmt (*tsi);

  if (BIND_EXPR_BLOCK (stmt))
    {
      data->block = BIND_EXPR_BLOCK (stmt);

      /* Block tree may get clobbered by inlining.  Normally this would be
	 fixed in rest_of_decl_compilation using block notes, but since we
	 are not going to emit them, it is up to us.  */
      BLOCK_CHAIN (data->block) = BLOCK_SUBBLOCKS (old_block);
      BLOCK_SUBBLOCKS (old_block) = data->block;
      BLOCK_SUBBLOCKS (data->block) = NULL_TREE;
      BLOCK_SUPERCONTEXT (data->block) = old_block;
    }

  expand_vars (BIND_EXPR_VARS (stmt));
  
  lower_stmt_body (&BIND_EXPR_BODY (stmt), data);

  if (BIND_EXPR_BLOCK (stmt))
    {
      if (data->block != BIND_EXPR_BLOCK (stmt))
	abort ();

      BLOCK_SUBBLOCKS (data->block) =
	      blocks_nreverse (BLOCK_SUBBLOCKS (data->block));
      data->block = old_block;
    }

  /* The BIND_EXPR no longer carries any useful information, so get rid
     of it.  */
  tsi_link_chain_before (tsi, BIND_EXPR_BODY (stmt), TSI_SAME_STMT);
  tsi_delink (tsi);
}

/* Checks whether EXPR is a simple local goto.  */

static bool
simple_goto_p (tree expr)
{
  return  (TREE_CODE (expr) == GOTO_EXPR
	   && TREE_CODE (GOTO_DESTINATION (expr)) == LABEL_DECL
	   && ! NONLOCAL_LABEL (GOTO_DESTINATION (expr))
	   && (decl_function_context (GOTO_DESTINATION (expr))
	       == current_function_decl));
}

/* Lowers a cond_expr TSI.  DATA is passed through the recursion.  */

static void
lower_cond_expr (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree stmt = tsi_stmt (*tsi);
  bool then_is_goto, else_is_goto;
  tree then_branch, else_branch, then_label, else_label, end_label;
  
  lower_stmt_body (&COND_EXPR_THEN (stmt), data);
  lower_stmt_body (&COND_EXPR_ELSE (stmt), data);

  /* Find out whether the branches are ordinary local gotos.  */
  then_branch = COND_EXPR_THEN (stmt);
  else_branch = COND_EXPR_ELSE (stmt);

  then_is_goto = simple_goto_p (then_branch);
  else_is_goto = simple_goto_p (else_branch);

  if (then_is_goto && else_is_goto)
    return;
 
  /* Replace the cond_expr with explicit gotos.  */
  if (!then_is_goto)
    {
      then_label = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
      COND_EXPR_THEN (stmt) = build_and_jump (&LABEL_EXPR_LABEL (then_label));
    }
  else
    then_label = NULL_TREE;

  if (!else_is_goto)
    {
      else_label = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
      COND_EXPR_ELSE (stmt) = build_and_jump (&LABEL_EXPR_LABEL (else_label));
    }
  else
    else_label = NULL_TREE;

  end_label = NULL_TREE;
  if (then_label)
    {
      tsi_link_after (tsi, then_label, TSI_CONTINUE_LINKING);
      tsi_link_chain_after (tsi, then_branch, TSI_CONTINUE_LINKING);
  
      if (else_label)
	{
	  end_label = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
	  tsi_link_after (tsi, build_and_jump (&LABEL_EXPR_LABEL (end_label)),
			  TSI_CONTINUE_LINKING);
	}
    }
  
  if (else_label)
    {
      tsi_link_after (tsi, else_label, TSI_CONTINUE_LINKING);
      tsi_link_chain_after (tsi, else_branch, TSI_CONTINUE_LINKING);
    }

  if (end_label)
    tsi_link_after (tsi, end_label, TSI_CONTINUE_LINKING);
}

/* Lowers a switch_expr TSI.  DATA is passed through the recursion.  */

static void
lower_switch_expr (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree stmt = tsi_stmt (*tsi);
  tree body = SWITCH_BODY (stmt);
  tree case_label = NULL_TREE;
  tree label_end;
  tree_stmt_iterator encl_switch_body = data->encl_switch_body;
  tree_stmt_iterator tsi_tmp;

  /* The body of the switch serves as a list to that CASE_LABEL_EXPRs
     add new GOTO_EXPR entries.  Add a default alternative to the list
     (if there is some in the switch body, it will replace it).  */
  SWITCH_BODY (stmt) = NULL_TREE;
  data->encl_switch_body = tsi_start (&SWITCH_BODY (stmt));
  tsi_link_after (&data->encl_switch_body,
		  build1 (GOTO_EXPR, void_type_node, NULL_TREE),
		  TSI_NEW_STMT);
  tsi_link_before (&data->encl_switch_body,
		   build (CASE_LABEL_EXPR, void_type_node,
			  NULL_TREE, NULL_TREE, NULL_TREE),
		   TSI_NEW_STMT);

  lower_stmt_body (&body, data);

  /* Now we have a chain of CASE_LABEL_EXPR + GOTOs in SWITCH_BODY, and
     body contains the chain of statements we want to add after the
     SWITCH_EXPR.  If there was not a default alternative, add a corresponding
     goto.  */

  tsi_tmp = data->encl_switch_body;
  tsi_next (&tsi_tmp);
  if (!GOTO_DESTINATION (tsi_stmt (tsi_tmp)))
    {
      label_end = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
      *tsi_stmt_ptr (tsi_tmp) = build_and_jump (&LABEL_EXPR_LABEL (label_end));
      case_label = build_new_label ();
      CASE_LABEL (tsi_stmt (data->encl_switch_body)) = case_label;

      /* Add the new entry to SWITCH_LABELS (is it really needed???)  */
      if (SWITCH_LABELS (stmt))
	{
	  tree switch_labels = SWITCH_LABELS (stmt);
	  tree new_switch_labels =
		  make_tree_vec (TREE_VEC_LENGTH (switch_labels) + 1);
	  int i;

	  for (i = 0; i < TREE_VEC_LENGTH (switch_labels); i++)
	    TREE_VEC_ELT (new_switch_labels, i) =
		    TREE_VEC_ELT (switch_labels, i);
	  TREE_VEC_ELT (new_switch_labels, i) = case_label;
	  SWITCH_LABELS (stmt) = new_switch_labels;
	}
    }
  else
    label_end = NULL_TREE;

  tsi_link_chain_after (tsi, body, TSI_CONTINUE_LINKING);
  if (label_end)
    tsi_link_after (tsi, label_end, TSI_CONTINUE_LINKING);

  data->encl_switch_body = encl_switch_body;
}

/* Replace the CASE_LABEL_EXPR at TSI with an ordinary label, and place the
   goto to this label to the enclosing SWITCH_EXPR body; this position is
   taken from DATA passed through recursion.  */

static void
lower_case_label_expr (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree stmt = tsi_stmt (*tsi);
  tree_stmt_iterator tsi_tmp, tsi_nxt;
  tree goto_expr, label;
 
  tsi_nxt = *tsi;
  tsi_next (&tsi_nxt);
  if (!tsi_end_p (tsi_nxt) && simple_goto_p (tsi_stmt (tsi_nxt)))
    {
      /* Reuse the label.  */
      label = GOTO_DESTINATION (tsi_stmt (tsi_nxt));
      goto_expr = build1 (GOTO_EXPR, void_type_node, label);
    }
  else
    {
      /* Create a new goto, and add the label.  */
      label = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
      tsi_link_after (tsi, label, TSI_SAME_STMT);
      goto_expr = build_and_jump (&LABEL_EXPR_LABEL (label));
    }
  
  if (CASE_LOW (stmt) == NULL_TREE)
    {
      /* Replace the prepared default entry.  */
      tsi_tmp = data->encl_switch_body;
      *tsi_stmt_ptr (tsi_tmp) = stmt;
      tsi_next (&tsi_tmp);
      *tsi_stmt_ptr (tsi_tmp) = goto_expr;
    }
  else
    {
      /* Add a new entry.  */
      tsi_link_before (&data->encl_switch_body, stmt, TSI_SAME_STMT);
      tsi_link_before (&data->encl_switch_body, goto_expr, TSI_SAME_STMT);
    }

  tsi_delink (tsi);
}
