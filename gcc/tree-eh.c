/* Exception handling semantics and decompostition for trees.
   Copyright (C) 2003 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "flags.h"
#include "function.h"
#include "except.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "tree-inline.h"
#include "timevar.h"
#include "errors.h"


/* HACK */
extern int using_eh_for_cleanups_p;

/* Misc functions used in this file.  */

/* Create a new LABEL_DECL.  */
/* ??? Should be moved somewhere generic; possibly tree.c.  */

static tree
make_label (void)
{
  tree lab = build_decl (LABEL_DECL, NULL_TREE, NULL_TREE);
  DECL_CONTEXT (lab) = current_function_decl;
  return lab;
}

/* Compare and hash for any structure which begins with a canonical
   pointer.  Assumes all pointers are interchangable, which is sort
   of already assumed by gcc elsewhere IIRC.  */

static int
struct_ptr_eq (const void *a, const void *b)
{
  const void * const * x = a;
  const void * const * y = b;
  return *x == *y;
}

static hashval_t
struct_ptr_hash (const void *a)
{
  const void * const * x = a;
  return (size_t)*x >> 4;
}


/* Remember and lookup EH region data for arbitrary statements.
   Really this means any statement that could_throw_p.  We could
   stuff this information into the stmt_ann data structure, but:

   (1) We absolutely rely on this information being kept until
   we get to rtl.  Once we're done with lowering here, if we lose
   the information there's no way to recover it!

   (2) There are many more statements that *cannot* throw as 
   compared to those that can.  We should be saving some amount
   of space by only allocating memory for those that can throw.  */

struct throw_stmt_node GTY(())
{
  tree stmt;
  int region_nr;
};

static GTY((param_is (struct throw_stmt_node))) htab_t throw_stmt_table;

static void
record_stmt_eh_region (struct eh_region *region, tree t)
{
  struct throw_stmt_node *n;
  void **slot;

  if (!region)
    return;

  n = xmalloc (sizeof (*n));
  n->stmt = t;
  n->region_nr = get_eh_region_number (region);

  slot = htab_find_slot (throw_stmt_table, n, INSERT);
  if (*slot)
    abort ();
  *slot = n;
}

int
lookup_stmt_eh_region (tree t)
{
  struct throw_stmt_node *p, n;

  if (!throw_stmt_table)
    return -2;

  n.stmt = t;
  p = htab_find (throw_stmt_table, &n);

  return (p ? p->region_nr : -1);
}



/* First pass of EH node decomposition.  Build up a tree of TRY_FINALLY_EXPR
   nodes and LABEL_DECL nodes.  We will use this during the second phase to
   determine if a goto leaves the body of a TRY_FINALLY_EXPR node.  */

struct finally_tree_node
{
  tree child, parent;
};

/* Note that this table is *not* marked GTY.  It is short-lived.  */
static htab_t finally_tree;

static void
record_in_finally_tree (tree child, tree parent)
{
  struct finally_tree_node *n;
  void **slot;

  n = xmalloc (sizeof (*n));
  n->child = child;
  n->parent = parent;

  slot = htab_find_slot (finally_tree, n, INSERT);
  if (*slot)
    abort ();
  *slot = n;
}

static void
collect_finally_tree (tree t, tree region)
{
  switch (TREE_CODE (t))
    {
    case LABEL_EXPR:
      record_in_finally_tree (LABEL_EXPR_LABEL (t), region);
      break;

    case TRY_FINALLY_EXPR:
      record_in_finally_tree (t, region);
      collect_finally_tree (TREE_OPERAND (t, 0), t);
      collect_finally_tree (TREE_OPERAND (t, 1), region);
      break;

    case LOOP_EXPR:
      collect_finally_tree (LOOP_EXPR_BODY (t), region);
      break;
    case COND_EXPR:
      collect_finally_tree (COND_EXPR_THEN (t), region);
      collect_finally_tree (COND_EXPR_ELSE (t), region);
      break;
    case SWITCH_EXPR:
      collect_finally_tree (SWITCH_BODY (t), region);
      break;
    case BIND_EXPR:
      collect_finally_tree (BIND_EXPR_BODY (t), region);
      break;
    case COMPOUND_EXPR:
    case TRY_CATCH_EXPR:
      collect_finally_tree (TREE_OPERAND (t, 0), region);
      collect_finally_tree (TREE_OPERAND (t, 1), region);
      break;
    case CATCH_EXPR:
      collect_finally_tree (CATCH_BODY (t), region);
      break;
    case EH_FILTER_EXPR:
      collect_finally_tree (EH_FILTER_FAILURE (t), region);
      break;

    default:
      /* A type, a decl, or some kind of statement that we're not
	 interested in.  Don't walk them.  */
      break;
    }
}

/* Use the finally tree to determine if a jump from START to TARGET
   would leave the try_finally node that START lives in.  */

static bool
outside_finally_tree (tree start, tree target)
{
  struct finally_tree_node n, *p;

  do
    {
      n.child = start;
      p = htab_find (finally_tree, &n);
      if (!p)
	return true;
      start = p->parent;
    }
  while (start != target);

  return false;
}

/* Second pass of EH node decomposition.  Actually transform the TRY_FINALLY
   and TRY_CATCH nodes into a set of gotos, magic labels, and eh regions.
   The eh region creation is straight-forward, but frobbing all the gotos
   and such into shape isn't.  */

/* State of the world while lowering.  */

struct leh_state
{
  /* What's "current" while constructing the eh region tree.  These 
     correspond to variables of the same name in cfun->eh, which we
     don't have easy access to.  */
  struct eh_region *cur_region;
  struct eh_region *prev_try;

  /* Processing of TRY_FINALLY requires a bit more state.  This is
     split out into a separate structure so that we don't have to
     copy so much when processing other nodes.  */
  struct leh_tf_state *tf;
};

struct leh_tf_state
{
  /* Pointer to the TRY_FINALLY node under discussion.  The try_finally_expr
     is the original TRY_FINALLY_EXPR.  We need to retain this so that 
     outside_finally_tree can reliably reference the tree used in the
     collect_finally_tree data structues.  */
  tree try_finally_expr;
  tree *top_p;

  /* The exception region created for it.  */
  struct eh_region *region;

  /* The GOTO_QUEUE is is an array of GOTO_EXPR and RETURN_EXPR statements
     that are seen to escape this TRY_FINALLY_EXPR node.  */
  struct goto_queue_node {
    tree stmt;
    tree repl_stmt;
    tree cont_stmt;
    int index;
  } *goto_queue;
  size_t goto_queue_size;
  size_t goto_queue_active;

  /* The set of unique labels seen as entries in the goto queue.  */
  varray_type dest_array;

  /* A label to be added at the end of the completed transformed
     sequence.  It will be set if may_fallthru was true *at one time*,
     though subsequent transformations may have cleared that flag.  */
  tree fallthru_label;

  /* A label that has been registered with except.c to be the 
     landing pad for this try block.  */
  tree eh_label;

  /* True if it is possible to fall out the bottom of the try block.
     Cleared if the fallthru is converted to a goto.  */
  bool may_fallthru;

  /* True if any entry in goto_queue is a RETURN_EXPR.  */
  bool may_return;

  /* True if the finally block can receive an exception edge.
     Cleared if the exception case is handled by code duplication.  */
  bool may_throw;
};

static void lower_eh_filter (struct leh_state *, tree *);
static void lower_eh_constructs_1 (struct leh_state *, tree *);

/* Comparison function for qsort/bsearch.  We're interested in 
   searching goto queue elements for source statements.  */

static int
goto_queue_cmp (const void *x, const void *y)
{
  tree a = ((const struct goto_queue_node *)x)->stmt;
  tree b = ((const struct goto_queue_node *)y)->stmt;
  return (a == b ? 0 : a < b ? -1 : 1);
}

/* Search for STMT in the goto queue.  Return the replacement,
   or null if the statement isn't in the queue.  */

static tree
find_goto_replacement (struct leh_tf_state *tf, tree stmt)
{
  struct goto_queue_node tmp, *ret;
  tmp.stmt = stmt;
  ret = bsearch (&tmp, tf->goto_queue, tf->goto_queue_active,
		 sizeof (struct goto_queue_node), goto_queue_cmp);
  return (ret ? ret->repl_stmt : NULL);
}

/* Replace all goto queue members.  */
/* ??? This search and replace nonsense wouldn't be necessary
   if we had a reasonable statement connection mechanism.  The
   nature of these COMPOUND_EXPRs is such that we can't store
   a pointer to a statement and hope to be able to replace it
   later, when the tree has been restructured.  */

static tree
replace_goto_queue_1 (tree *tp, int *walk_subtrees, void *data)
{
  struct leh_tf_state *tf = data;
  tree t = *tp, sub;

  switch (TREE_CODE (t))
    {
    case GOTO_EXPR:
    case RETURN_EXPR:
      t = find_goto_replacement (tf, t);
      if (t)
	*tp = t;
      *walk_subtrees = 0;
      break;

    case COMPOUND_EXPR:
      sub = TREE_OPERAND (t, 0);
      if (TREE_CODE (sub) == GOTO_EXPR || TREE_CODE (sub) == RETURN_EXPR)
	{
	  sub = find_goto_replacement (tf, sub);
	  if (sub)
	    {
	      if (TREE_CODE (sub) == COMPOUND_EXPR)
		{
		  tree_stmt_iterator i = tsi_start (tp);
		  tsi_link_chain_before (&i, sub, TSI_SAME_STMT);
		  tsi_delink (&i);
	          walk_tree (tsi_container (i), replace_goto_queue_1, tf, NULL);
		}
	      else
		{
		  TREE_OPERAND (t, 0) = sub;
	          walk_tree (&TREE_OPERAND (t, 1), replace_goto_queue_1,
			     tf, NULL);
		}
	      *walk_subtrees = 0;
	    }
	}
      break;

    case LOOP_EXPR:
    case COND_EXPR:
    case SWITCH_EXPR:
    case BIND_EXPR:
    case TRY_FINALLY_EXPR:
    case TRY_CATCH_EXPR:
    case CATCH_EXPR:
    case EH_FILTER_EXPR:
      /* Only need to look down statement containers.  */
      break;

    default:
      /* These won't have gotos in them.  */
      *walk_subtrees = 0;
      break;
    }

  return NULL;
}

static void
replace_goto_queue (struct leh_tf_state *tf)
{
  /* Note that since we only look through statement containers,
     we cannot possibly see duplicates.  Barring bugs of course.  */
  walk_tree (tf->top_p, replace_goto_queue_1, tf, NULL);
}

/* For any GOTO_EXPR or RETURN_EXPR, decide whether it leaves a try_finally
   node, and if so record that fact in the goto queue associated with that
   try_finally node.  */

static void
maybe_record_in_goto_queue (struct leh_state *state, tree stmt)
{
  struct leh_tf_state *tf = state->tf;
  struct goto_queue_node *q;
  size_t active, size;
  int index;

  if (!tf)
    return;

  switch (TREE_CODE (stmt))
    {
    case GOTO_EXPR:
      {
	tree lab = GOTO_DESTINATION (stmt);

	/* Computed and non-local gotos do not get processed.  Given 
	   their nature we can neither tell whether we've escaped the
	   finally block nor redirect them if we knew.  */
	if (TREE_CODE (lab) != LABEL_DECL)
	  return;

	/* No need to record gotos that don't leave the try block.  */
	if (! outside_finally_tree (lab, tf->try_finally_expr))
	  return;
  
	if (! tf->dest_array)
	  {
	    VARRAY_TREE_INIT (tf->dest_array, 10, "dest_array");
	    VARRAY_PUSH_TREE (tf->dest_array, lab);
	    index = 0;
	  }
	else
	  {
	    int n = VARRAY_ACTIVE_SIZE (tf->dest_array);
	    for (index = 0; index < n; ++index)
	      if (VARRAY_TREE (tf->dest_array, index) == lab)
		break;
	    if (index == n)
	      VARRAY_PUSH_TREE (tf->dest_array, lab);
	  }
      }
      break;

    case RETURN_EXPR:
      tf->may_return = true;
      index = -1;
      break;

    default:
      abort ();
    }

  active = tf->goto_queue_active;
  size = tf->goto_queue_size;
  if (active >= size)
    {
      size = (size ? size * 2 : 32);
      tf->goto_queue_size = size;
      tf->goto_queue
	= xrealloc (tf->goto_queue, size * sizeof (struct goto_queue_node));
    }

  q = &tf->goto_queue[active];
  tf->goto_queue_active = active + 1;
  
  memset (q, 0, sizeof (*q));
  q->stmt = stmt;
  q->index = index;
}

/* Redirect a RETURN_EXPR pointed to by STMT_P to FINLAB.  Place in CONT_P
   whatever is needed to finish the return.  If MOD is non-null, insert it
   before the new branch.  RETURN_VALUE_P is a cache containing a temporary
   variable to be used in manipulating the value returned from the function. */

static void
do_return_redirection (struct goto_queue_node *q, tree finlab, tree mod,
		       tree *return_value_p)
{
  tree ret_expr = TREE_OPERAND (q->stmt, 0);
  tree_stmt_iterator i;
  tree x;

  i = tsi_start (&q->repl_stmt);
  if (ret_expr)
    {
      /* The nasty part about redirecting the return value is that the
	 return value itself is to be computed before the FINALLY block
	 is executed.  e.g.

		int x;
		int foo (void)
		{
		  x = 0;
		  try {
		    return x;
		  } finally {
		    x++;
		  }
		}

	  should return 0, not 1.  Arrange for this to happen by copying
	  computed the return value into a local temporary.  This also
	  allows us to redirect multiple return statements through the
	  same destination block; whether this is a net win or not really
	  depends, I guess, but it does make generation of the switch in
	  lower_try_finally_switch easier.  */

      if (TREE_CODE (ret_expr) == MODIFY_EXPR)
	{
	  tsi_link_after (&i, ret_expr, TSI_NEW_STMT);
	  ret_expr = TREE_OPERAND (ret_expr, 0);
	}
      if (!*return_value_p)
	{
	  if (TREE_CODE (ret_expr) == RESULT_DECL)
	    *return_value_p = ret_expr;
	  else
	    *return_value_p = create_tmp_var (TREE_TYPE (ret_expr), "rettmp");
	}
      else if (ret_expr != *return_value_p)
	{
	  x = build (MODIFY_EXPR, void_type_node, *return_value_p, ret_expr);
	  tsi_link_after (&i, x, TSI_NEW_STMT);
	}

      q->cont_stmt = build1 (RETURN_EXPR, void_type_node, *return_value_p);
    }
  else
    {
      /* If we don't return a value, all return statements are the same.  */
      q->cont_stmt = q->stmt;
    }

  if (mod)
    tsi_link_after (&i, mod, TSI_NEW_STMT);

  x = build1 (GOTO_EXPR, void_type_node, finlab);
  tsi_link_after (&i, x, TSI_NEW_STMT);
}

/* Similar, but easier, for GOTO_EXPR.  */

static void
do_goto_redirection (struct goto_queue_node *q, tree finlab, tree mod)
{
  tree_stmt_iterator i = tsi_start (&q->repl_stmt);
  tree x;

  q->cont_stmt = q->stmt;
  if (mod)
    tsi_link_after (&i, mod, TSI_NEW_STMT);

  x = build1 (GOTO_EXPR, void_type_node, finlab);
  tsi_link_after (&i, x, TSI_NEW_STMT);
}

/* Try to determine if we can fall out of the bottom of BLOCK.  This guess
   need not be 100% accurate; simply be conservative and return true if we
   don't know.  This is used only to avoid stupidly generating extra code.
   If we're wrong, we'll just delete the extra code later.  */

static bool
block_may_fallthru_last (tree stmt)
{
  switch (TREE_CODE (stmt))
    {
    case GOTO_EXPR:
    case RETURN_EXPR:
    case LOOP_EXPR:
    case RESX_EXPR:
      /* Easy cases.  If the last statement of the block implies 
	 control transfer, then we can't fall through.  */
      return false;

    case MODIFY_EXPR:
      if (TREE_CODE (TREE_OPERAND (stmt, 1)) == CALL_EXPR)
	stmt = TREE_OPERAND (stmt, 1);
      else
	return true;
      /* FALLTHRU */

    case CALL_EXPR:
      /* Functions that do not return do not fall through.  */
      return (call_expr_flags (stmt) & ECF_NORETURN) == 0;

    default:
      /* ??? Could search back through other composite structures.
	 Wouldn't need to check COMPOUND_EXPR because of how
	 tsi_last is implemented.  */
      return true;
    }
}

static bool
block_may_fallthru (tree *block_p)
{
  return block_may_fallthru_last (tsi_stmt (tsi_last (block_p)));
}

/* We want to transform
	try { body; } catch { stuff; }
   to
	body; goto over; lab: stuff; over:

   T is a TRY_FINALLY or TRY_CATCH node.  LAB is the label that
   should be placed before the second operand, or NULL.  OVER is
   an existing label that should be put at the exit, or NULL.  */

static void
frob_into_branch_around (tree *tp, tree lab, tree over)
{
  tree_stmt_iterator i;
  tree x, op1;

  op1 = TREE_OPERAND (*tp, 1);
  *tp = TREE_OPERAND (*tp, 0);
  i = tsi_last (tp);

  if (block_may_fallthru_last (tsi_stmt (i)))
    {
      if (!over)
	over = make_label ();
      x = build1 (GOTO_EXPR, void_type_node, over);
      tsi_link_after (&i, x, TSI_NEW_STMT);
    }

  if (lab)
    {
      x = build1 (LABEL_EXPR, void_type_node, lab);
      tsi_link_after (&i, x, TSI_NEW_STMT);
    }

  tsi_link_chain_after (&i, op1, TSI_CHAIN_END);

  if (over)
    {
      x = build1 (LABEL_EXPR, void_type_node, over);
      tsi_link_after (&i, x, TSI_NEW_STMT);
    }
}

/* A subroutine of lower_try_finally.  If lang_protect_cleanup_actions
   returns non-null, then the language requires that the exception path out
   of a try_finally be treated specially.  To wit: the code within the
   finally block may not itself throw an exception.  We have two choices here.
   First we can duplicate the finally block and wrap it in a must_not_throw
   region.  Second, we can generate code like

	try {
	  finally_block;
	} catch {
	  if (fintmp == eh_edge)
	    protect_cleanup_actions;
	}

   where "fintmp" is the temporary used in the switch statement generation
   alternative considered below.  For the nonce, we always choose the first
   option. 

   THIS_STATE may be null if if this is a try-cleanup, not a try-finally.  */

static void
honor_protect_cleanup_actions (struct leh_state *outer_state,
			       struct leh_state *this_state,
			       struct leh_tf_state *tf)
{
  tree protect_cleanup_actions, finally, x;
  tree_stmt_iterator i;
  bool finally_may_fallthru;

  /* First check for nothing to do.  */
  if (lang_protect_cleanup_actions)
    protect_cleanup_actions = lang_protect_cleanup_actions ();
  else
    protect_cleanup_actions = NULL;

  finally = TREE_OPERAND (*tf->top_p, 1);

  /* If the EH case of the finally block can fall through, this may be a
     structure of the form
	try {
	  try {
	    throw ...;
	  } cleanup {
	    try {
	      throw ...;
	    } catch (...) {
	    }
	  }
	} catch (...) {
	  yyy;
	}
    E.g. with an inline destructor with an embedded try block.  In this
    case we must save the runtime EH data around the nested exception.

    This complication means that any time the previous runtime data might
    be used (via fallthru from the finally) we handle the eh case here,
    whether or not protect_cleanup_actions is active.  */

  finally_may_fallthru = block_may_fallthru (&finally);
  if (!finally_may_fallthru && !protect_cleanup_actions)
    return;

  /* Duplicate the FINALLY block.  */
  finally = lhd_unsave_expr_now (finally);

  /* Resume execution after the exception.  Adding this now lets
     lower_eh_filter not add unnecessary gotos, as it is clear that
     we never fallthru from this copy of the finally block.  */
  if (finally_may_fallthru)
    {
      tree save_eptr, save_filt;

      save_eptr = create_tmp_var (ptr_type_node, "save_eptr");
      save_filt = create_tmp_var (integer_type_node, "save_filt");

      i = tsi_start (&finally);
      x = build (EXC_PTR_EXPR, ptr_type_node);
      x = build (MODIFY_EXPR, void_type_node, save_eptr, x);
      tsi_link_before (&i, x, TSI_NEW_STMT);

      x = build (FILTER_EXPR, integer_type_node);
      x = build (MODIFY_EXPR, void_type_node, save_filt, x);
      tsi_link_before (&i, x, TSI_NEW_STMT);

      i = tsi_last (&finally);
      x = build (EXC_PTR_EXPR, ptr_type_node);
      x = build (MODIFY_EXPR, void_type_node, x, save_eptr);
      tsi_link_after (&i, x, TSI_NEW_STMT);

      x = build (FILTER_EXPR, integer_type_node);
      x = build (MODIFY_EXPR, void_type_node, x, save_filt);
      tsi_link_after (&i, x, TSI_NEW_STMT);

      x = build1 (RESX_EXPR, void_type_node,
		  build_int_2 (get_eh_region_number (tf->region), 0));
      tsi_link_after (&i, x, TSI_NEW_STMT);
    }

  /* Wrap the block with protect_cleanup_actions as the action.  */
  if (protect_cleanup_actions)
    {
      x = build (EH_FILTER_EXPR, void_type_node, NULL,
		 protect_cleanup_actions);
      EH_FILTER_MUST_NOT_THROW (x) = 1;
      finally = build (TRY_CATCH_EXPR, void_type_node, finally, x);
      lower_eh_filter (outer_state, &finally);
    }
  else
    lower_eh_constructs_1 (outer_state, &finally);

  /* Hook this up to the end of the existing try block.  If we
     previously fell through the end, we'll have to branch around.
     This means adding a new goto, and adding it to the queue.  */

  i = tsi_last (&TREE_OPERAND (*tf->top_p, 0));

  if (tf->may_fallthru)
    {
      if (!tf->fallthru_label)
	tf->fallthru_label = make_label ();
      x = build1 (GOTO_EXPR, void_type_node, tf->fallthru_label);
      tsi_link_after (&i, x, TSI_NEW_STMT);

      if (this_state)
        maybe_record_in_goto_queue (this_state, x);

      tf->may_fallthru = false;
    }

  x = build1 (LABEL_EXPR, void_type_node, tf->eh_label);
  tsi_link_after (&i, x, TSI_NEW_STMT);

  tsi_link_chain_after (&i, finally, TSI_CHAIN_START);

  /* Having now been handled, EH isn't to be considered with
     the rest of the outgoing edges.  */
  tf->may_throw = false;
}

/* A subroutine of lower_try_finally.  We have determined that there is
   no fallthru edge out of the finally block.  This means that there is
   no outgoing edge corresponding to any incomming edge.  Restructure the
   try_finally node for this special case.  */

static void
lower_try_finally_nofallthru (struct leh_state *state, struct leh_tf_state *tf)
{
  tree x, finally, lab, return_val;
  struct goto_queue_node *q, *qe;
  tree_stmt_iterator i;

  if (tf->may_throw)
    lab = tf->eh_label;
  else
    lab = make_label ();

  finally = TREE_OPERAND (*tf->top_p, 1);
  *tf->top_p = TREE_OPERAND (*tf->top_p, 0);

  i = tsi_last (tf->top_p);
  x = build1 (LABEL_EXPR, void_type_node, lab);
  tsi_link_after (&i, x, TSI_NEW_STMT);

  return_val = NULL;
  q = tf->goto_queue;
  qe = q + tf->goto_queue_active;
  for (; q < qe; ++q)
    if (q->index < 0)
      do_return_redirection (q, lab, NULL, &return_val);
    else
      do_goto_redirection (q, lab, NULL);

  replace_goto_queue (tf);

  lower_eh_constructs_1 (state, &finally);
  tsi_link_chain_after (&i, finally, TSI_SAME_STMT);
}

/* A subroutine of lower_try_finally.  We have determined that there is
   exactly one destination of the finally block.  Restructure the
   try_finally node for this special case.  */

static void
lower_try_finally_onedest (struct leh_state *state, struct leh_tf_state *tf)
{
  struct goto_queue_node *q, *qe;
  tree_stmt_iterator i;
  tree x, finally, finally_label;

  finally = TREE_OPERAND (*tf->top_p, 1);
  *tf->top_p = TREE_OPERAND (*tf->top_p, 0);
  i = tsi_last (tf->top_p);

  lower_eh_constructs_1 (state, &finally);

  if (tf->may_throw)
    {
      /* Only reachable via the exception edge.  Add the given label to
         the head of the FINALLY block.  Append a RESX at the end.  */

      x = build1 (LABEL_EXPR, void_type_node, tf->eh_label);
      tsi_link_after (&i, x, TSI_NEW_STMT);

      tsi_link_chain_after (&i, finally, TSI_CHAIN_END);
      
      x = build1 (RESX_EXPR, void_type_node,
		  build_int_2 (get_eh_region_number (tf->region), 0));
      tsi_link_after (&i, x, TSI_NEW_STMT);

      return;
    }

  if (tf->may_fallthru)
    {
      /* Only reachable via the fallthru edge.  Do nothing but let
	 the two blocks run together; we'll fall out the bottom.  */
      tsi_link_chain_after (&i, finally, TSI_SAME_STMT);
      return;
    }

  finally_label = make_label ();
  x = build1 (LABEL_EXPR, void_type_node, finally_label);
  tsi_link_after (&i, x, TSI_NEW_STMT);

  tsi_link_chain_after (&i, finally, TSI_CHAIN_END);

  q = tf->goto_queue;
  qe = q + tf->goto_queue_active;

  if (tf->may_return)
    {
      /* Reachable by return expressions only.  Redirect them.  */
      tree return_val = NULL;
      for (; q < qe; ++q)
	do_return_redirection (q, finally_label, NULL, &return_val);
      replace_goto_queue (tf);
    }
  else
    {
      /* Reachable by goto expressions only.  Redirect them.  */
      for (; q < qe; ++q)
	do_goto_redirection (q, finally_label, NULL);
      replace_goto_queue (tf);
      
      if (VARRAY_TREE (tf->dest_array, 0) == tf->fallthru_label)
	{
	  /* Reachable by goto to fallthru label only.  Redirect it
	     to the new label (already created, sadly), and do not
	     emit the final branch out, or the fallthru label.  */
	  tf->fallthru_label = NULL;
	  return;
	}
    }

  tsi_link_after (&i, tf->goto_queue[0].cont_stmt, TSI_NEW_STMT);
  maybe_record_in_goto_queue (state, tf->goto_queue[0].cont_stmt);
}

/* A subroutine of lower_try_finally.  There are multiple edges incoming
   and outgoing from the finally block.  Implement this by duplicating the
   finally block for every destination.  */

static void
lower_try_finally_copy (struct leh_state *state, struct leh_tf_state *tf)
{
  tree finally, new_stmt;
  tree_stmt_iterator i;
  tree x;

  finally = TREE_OPERAND (*tf->top_p, 1);
  *tf->top_p = TREE_OPERAND (*tf->top_p, 0);

  new_stmt = NULL;
  i = tsi_start (&new_stmt);

  if (tf->may_fallthru)
    {
      x = lhd_unsave_expr_now (finally);
      lower_eh_constructs_1 (state, &x);
      tsi_link_chain_after (&i, x, TSI_CHAIN_END);

      if (!tf->fallthru_label)
	tf->fallthru_label = make_label ();
      x = build1 (GOTO_EXPR, void_type_node, tf->fallthru_label);
      tsi_link_after (&i, x, TSI_NEW_STMT);
    }

  if (tf->may_throw)
    {
      x = build1 (LABEL_EXPR, void_type_node, tf->eh_label);
      tsi_link_after (&i, x, TSI_NEW_STMT);

      x = lhd_unsave_expr_now (finally);
      lower_eh_constructs_1 (state, &x);
      tsi_link_chain_after (&i, x, TSI_CHAIN_END);

      x = build1 (RESX_EXPR, void_type_node,
		  build_int_2 (get_eh_region_number (tf->region), 0));
      tsi_link_after (&i, x, TSI_NEW_STMT);
    }

  if (tf->goto_queue)
    {
      struct goto_queue_node *q, *qe;
      tree return_val = NULL;
      int return_index;
      tree *labels;

      if (tf->dest_array)
	return_index = VARRAY_ACTIVE_SIZE (tf->dest_array);
      else
	return_index = 0;
      labels = xcalloc (sizeof (tree), return_index + 1);

      q = tf->goto_queue;
      qe = q + tf->goto_queue_active;
      for (; q < qe; q++)
	{
	  int index = q->index < 0 ? return_index : q->index;
	  tree lab = labels[index];
	  bool build_p = false;

	  if (!lab)
	    {
	      labels[index] = lab = make_label ();
	      build_p = true;
	    }

	  if (index == return_index)
	    do_return_redirection (q, lab, NULL, &return_val);
	  else
	    do_goto_redirection (q, lab, NULL);

	  if (build_p)
	    {
	      x = build1 (LABEL_EXPR, void_type_node, lab);
	      tsi_link_after (&i, x, TSI_NEW_STMT);

	      x = lhd_unsave_expr_now (finally);
	      lower_eh_constructs_1 (state, &x);
	      tsi_link_chain_after (&i, x, TSI_CHAIN_END);

	      tsi_link_after (&i, q->cont_stmt, TSI_NEW_STMT);
	      maybe_record_in_goto_queue (state, q->cont_stmt);
	    }
	}
      replace_goto_queue (tf);
      free (labels);
    }

  /* Need to link new stmts after running replace_goto_queue due
     to not wanting to process the same goto stmts twice.  */
  i = tsi_last (tf->top_p);
  tsi_link_chain_after (&i, new_stmt, TSI_SAME_STMT);
}

/* A subroutine of lower_try_finally.  There are multiple edges incoming
   and outgoing from the finally block.  Implement this by instrumenting
   each incoming edge and creating a switch statement at the end of the
   finally block that branches to the appropriate destination.  */

static void
lower_try_finally_switch (struct leh_state *state, struct leh_tf_state *tf)
{
  struct goto_queue_node *q, *qe;
  tree return_val = NULL;
  tree finally, finally_tmp, finally_label;
  tree_stmt_iterator i, i2;
  int return_index, eh_index, fallthru_index;
  int nlabels, ndests, j, last_case_index;
  tree case_label_vec, switch_stmt, last_case;
  tree x;

  /* Mash the TRY block to the head of the chain.  */
  finally = TREE_OPERAND (*tf->top_p, 1);
  *tf->top_p = TREE_OPERAND (*tf->top_p, 0);
  i = tsi_last (tf->top_p);

  /* Lower the finally block itself.  */
  lower_eh_constructs_1 (state, &finally);

  /* Prepare for switch statement generation.  */
  if (tf->dest_array)
    nlabels = VARRAY_ACTIVE_SIZE (tf->dest_array);
  else
    nlabels = 0;
  return_index = nlabels;
  eh_index = return_index + tf->may_return;
  fallthru_index = eh_index + tf->may_throw;
  ndests = fallthru_index + tf->may_fallthru;

  finally_tmp = create_tmp_var (integer_type_node, "finally_tmp");
  finally_label = make_label ();

  case_label_vec = make_tree_vec (ndests);
  switch_stmt = build (SWITCH_EXPR, integer_type_node, finally_tmp,
		       NULL_TREE, case_label_vec);
  i2 = tsi_start (&SWITCH_BODY (switch_stmt));
  last_case = NULL;
  last_case_index = 0;

  /* Begin insertting code for getting to the finally block.  Things
     are done in this order to correspond to the sequence the code is
     layed out.  */

  if (tf->may_fallthru)
    {
      x = build (MODIFY_EXPR, void_type_node, finally_tmp,
		 build_int_2 (fallthru_index, 0));
      tsi_link_after (&i, x, TSI_NEW_STMT);

      if (tf->may_throw)
	{
	  x = build1 (GOTO_EXPR, void_type_node, finally_label);
	  tsi_link_after (&i, x, TSI_NEW_STMT);
	}

      if (!tf->fallthru_label)
	tf->fallthru_label = make_label ();

      last_case = build (CASE_LABEL_EXPR, void_type_node,
			 build_int_2 (fallthru_index, 0), NULL, make_label ());
      TREE_VEC_ELT (case_label_vec, last_case_index) = last_case;
      last_case_index++;

      tsi_link_after (&i2, last_case, TSI_NEW_STMT);
      x = build1 (GOTO_EXPR, void_type_node, tf->fallthru_label);
      tsi_link_after (&i2, x, TSI_NEW_STMT);
    }

  if (tf->may_throw)
    {
      x = build1 (LABEL_EXPR, void_type_node, tf->eh_label);
      tsi_link_after (&i, x, TSI_NEW_STMT);

      x = build (MODIFY_EXPR, void_type_node, finally_tmp,
		 build_int_2 (eh_index, 0));
      tsi_link_after (&i, x, TSI_NEW_STMT);

      last_case = build (CASE_LABEL_EXPR, void_type_node,
			 build_int_2 (eh_index, 0), NULL, make_label ());
      TREE_VEC_ELT (case_label_vec, last_case_index) = last_case;
      last_case_index++;

      tsi_link_after (&i2, last_case, TSI_NEW_STMT);
      x = build1 (RESX_EXPR, void_type_node,
		  build_int_2 (get_eh_region_number (tf->region), 0));
      tsi_link_after (&i2, x, TSI_NEW_STMT);
    }

  x = build1 (LABEL_EXPR, void_type_node, finally_label);
  tsi_link_after (&i, x, TSI_NEW_STMT);

  tsi_link_chain_after (&i, finally, TSI_CHAIN_END);

  /* Redirect each incomming goto edge.  */
  q = tf->goto_queue;
  qe = q + tf->goto_queue_active;
  j = last_case_index + tf->may_return;
  last_case_index += nlabels;
  for (; q < qe; ++q)
    {
      tree mod;
      int switch_id, case_index;

      if (q->index < 0)
	{
	  mod = build (MODIFY_EXPR, void_type_node, finally_tmp,
		       build_int_2 (return_index, 0));
	  do_return_redirection (q, finally_label, mod, &return_val);
	  switch_id = return_index;
	}
      else
	{
	  mod = build (MODIFY_EXPR, void_type_node, finally_tmp,
		       build_int_2 (q->index, 0));
	  do_goto_redirection (q, finally_label, mod);
	  switch_id = q->index;
	}

      case_index = j + q->index;
      if (!TREE_VEC_ELT (case_label_vec, case_index))
	{
	  last_case = build (CASE_LABEL_EXPR, void_type_node,
			     build_int_2 (switch_id, 0), NULL, make_label ());
	  TREE_VEC_ELT (case_label_vec, case_index) = last_case;

	  tsi_link_after (&i2, last_case, TSI_NEW_STMT);
	  tsi_link_after (&i2, q->cont_stmt, TSI_NEW_STMT);
	  maybe_record_in_goto_queue (state, q->cont_stmt);
	}
    }
  replace_goto_queue (tf);
  last_case_index += nlabels;

  /* Need to link switch_stmt after running replace_goto_queue due
     to not wanting to process the same goto stmts twice.  */
  tsi_link_after (&i, switch_stmt, TSI_NEW_STMT);

  /* Make sure that we have a default label, so that we don't 
     confuse flow analysis.  */
  CASE_LOW (last_case) = NULL;
}

/* Decide whether or not we are going to duplicate the finally block.
   There are several considerations.

   First, if this is Java, then the finally block contains code
   written by the user.  It has line numbers associated with it,
   so duplicating the block means it's difficult to set a breakpoint.
   Since controling code generation via -g is verboten, we simply
   never duplicate code without optimization.

   Second, we'd like to prevent egregious code growth.  One way to
   do this is to estimate the size of the finally block, multiply
   that by the number of copies we'd need to make, and compare against
   the estimate of the size of the switch machinery we'd have to add.  */

static bool
decide_copy_try_finally (int ndests ATTRIBUTE_UNUSED,
			 tree finally ATTRIBUTE_UNUSED)
{
  if (!optimize)
    return false;

  /* ??? Should actually estimate the size of the finally block here.  */

  /* ??? Arbitrarily say -O1 does switch and -O2 does copy, so that both
     code paths get executed.  */
  return optimize > 1;
}

/* A subroutine of lower_eh_constructs_1.  Lower a TRY_FINALLY_EXPR nodes
   to a sequence of labels and blocks, plus the exception region trees
   that record all the magic.  This is complicated by the need to 
   arrange for the FINALLY block to be executed on all exits.  */

static void
lower_try_finally (struct leh_state *state, tree *tp)
{
  struct leh_tf_state this_tf;
  struct leh_state this_state;
  int ndests;

  /* Process the try block.  */

  memset (&this_tf, 0, sizeof (this_tf));
  this_tf.try_finally_expr = *tp;
  this_tf.top_p = tp;
  if (using_eh_for_cleanups_p)
    this_tf.region = gen_eh_region_cleanup (state->cur_region, state->prev_try);
  else
    this_tf.region = NULL;

  this_state.cur_region = this_tf.region;
  this_state.prev_try = state->prev_try;
  this_state.tf = &this_tf;

  lower_eh_constructs_1 (&this_state, &TREE_OPERAND (*tp, 0));

  /* Determine if the try block is escaped through the bottom.  */
  this_tf.may_fallthru = block_may_fallthru (&TREE_OPERAND (*tp, 0));

  /* Determine if any exceptions are possible within the try block.  */
  if (using_eh_for_cleanups_p)
    this_tf.may_throw = get_eh_region_may_contain_throw (this_tf.region);
  if (this_tf.may_throw)
    {
      this_tf.eh_label = make_label ();
      set_eh_region_tree_label (this_tf.region, this_tf.eh_label);
      honor_protect_cleanup_actions (state, &this_state, &this_tf);
    }

  /* Sort the goto queue for efficient searching later.  */
  if (this_tf.goto_queue_active > 1)
    qsort (this_tf.goto_queue, this_tf.goto_queue_active,
	   sizeof (struct goto_queue_node), goto_queue_cmp);

  /* Determine how many edges (still) reach the finally block.  Or rather,
     how many destinations are reached by the finally block.  Use this to
     determine how we process the finally block itself.  */

  if (this_tf.dest_array)
    ndests = VARRAY_ACTIVE_SIZE (this_tf.dest_array);
  else
    ndests = 0;
  ndests += this_tf.may_fallthru;
  ndests += this_tf.may_return;
  ndests += this_tf.may_throw;

  /* If the FINALLY block is not reachable, dike it out.  */
  if (ndests == 0)
    *tp = TREE_OPERAND (*tp, 0);

  /* If the finally block doesn't fall through, then any destination
     we might try to impose there isn't reached either.  There may be
     some minor amount of cleanup and redirection still needed.  */
  else if (!block_may_fallthru (&TREE_OPERAND (*tp, 1)))
    lower_try_finally_nofallthru (state, &this_tf);

  /* We can easily special-case redirection to a single destination.  */
  else if (ndests == 1)
    lower_try_finally_onedest (state, &this_tf);

  else if (decide_copy_try_finally (ndests, TREE_OPERAND (*tp, 1)))
    lower_try_finally_copy (state, &this_tf);
  else
    lower_try_finally_switch (state, &this_tf);

  /* If someone requested we add a label at the end of the transformed
     block, do so.  */
  if (this_tf.fallthru_label)
    {
      tree_stmt_iterator i = tsi_last (tp);
      tree x = build1 (LABEL_EXPR, void_type_node, this_tf.fallthru_label);
      tsi_link_after (&i, x, TSI_NEW_STMT);
    }

  if (this_tf.goto_queue)
    free (this_tf.goto_queue);
}

/* A subroutine of lower_eh_constructs_1.  Lower a TRY_CATCH_EXPR with a
   list of CATCH_EXPR nodes to a sequence of labels and blocks, plus the 
   exception region trees that record all the magic.  */

static void
lower_catch (struct leh_state *state, tree *tp)
{
  struct eh_region *try_region;
  struct leh_state this_state;
  tree_stmt_iterator i;
  tree out_label;

  try_region = gen_eh_region_try (state->cur_region);
  this_state.cur_region = try_region;
  this_state.prev_try = try_region;
  this_state.tf = state->tf;

  lower_eh_constructs_1 (&this_state, &TREE_OPERAND (*tp, 0));

  if (!get_eh_region_may_contain_throw (try_region))
    {
      *tp = TREE_OPERAND (*tp, 0);
      return;
    }

  out_label = NULL;
  for (i = tsi_start (&TREE_OPERAND (*tp, 1)); !tsi_end_p (i); )
    {
      struct eh_region *catch_region;
      tree_stmt_iterator j;
      tree catch, x, eh_label;

      catch = tsi_stmt (i);
      catch_region = gen_eh_region_catch (try_region, CATCH_TYPES (catch));

      lower_eh_constructs_1 (state, &CATCH_BODY (catch));

      eh_label = make_label ();
      set_eh_region_tree_label (catch_region, eh_label);

      j = tsi_start (&CATCH_BODY (catch));
      x = build1 (LABEL_EXPR, void_type_node, eh_label);
      tsi_link_before (&j, x, TSI_SAME_STMT);

      if (block_may_fallthru (&CATCH_BODY (catch)))
	{
	  if (!out_label)
	    out_label = make_label ();

	  j = tsi_last (&CATCH_BODY (catch));
	  x = build1 (GOTO_EXPR, void_type_node, out_label);
	  tsi_link_after (&j, x, TSI_SAME_STMT);
	}

      tsi_link_chain_before (&i, CATCH_BODY (catch), TSI_SAME_STMT);
      tsi_delink (&i);
    }

  frob_into_branch_around (tp, NULL, out_label);
}

/* A subroutine of lower_eh_constructs_1.  Lower a TRY_CATCH_EXPR with a
   EH_FILTER_EXPR to a sequence of labels and blocks, plus the exception
   region trees that record all the magic.  */

static void
lower_eh_filter (struct leh_state *state, tree *tp)
{
  struct leh_state this_state;
  struct eh_region *this_region;
  tree inner = TREE_OPERAND (*tp, 1);
  tree eh_label;
  
  if (EH_FILTER_MUST_NOT_THROW (inner))
    this_region = gen_eh_region_must_not_throw (state->cur_region);
  else
    this_region = gen_eh_region_allowed (state->cur_region,
					 EH_FILTER_TYPES (inner));
  this_state = *state;
  this_state.cur_region = this_region;
  
  lower_eh_constructs_1 (&this_state, &TREE_OPERAND (*tp, 0));

  if (!get_eh_region_may_contain_throw (this_region))
    {
      *tp = TREE_OPERAND (*tp, 0);
      return;
    }

  lower_eh_constructs_1 (state, &EH_FILTER_FAILURE (inner));
  TREE_OPERAND (*tp, 1) = EH_FILTER_FAILURE (inner);

  eh_label = make_label ();
  set_eh_region_tree_label (this_region, eh_label);

  frob_into_branch_around (tp, eh_label, NULL);
}

/* Implement a cleanup expression.  This is similar to try-finally,
   except that we only execute the cleanup block for exception edges.  */

static void
lower_cleanup (struct leh_state *state, tree *tp)
{
  struct leh_state this_state;
  struct eh_region *this_region;
  struct leh_tf_state fake_tf;

  /* If not using eh, then exception-only cleanups are no-ops.  */
  if (!flag_exceptions)
    {
      *tp = TREE_OPERAND (*tp, 0);
      lower_eh_constructs_1 (state, tp);
      return;
    }

  this_region = gen_eh_region_cleanup (state->cur_region, state->prev_try);
  this_state = *state;
  this_state.cur_region = this_region;

  lower_eh_constructs_1 (&this_state, &TREE_OPERAND (*tp, 0));

  if (!get_eh_region_may_contain_throw (this_region))
    {
      *tp = TREE_OPERAND (*tp, 0);
      return;
    }

  /* Build enough of a try-finally state so that we can reuse
     honor_protect_cleanup_actions.  */
  memset (&fake_tf, 0, sizeof (fake_tf));
  fake_tf.top_p = tp;
  fake_tf.region = this_region;
  fake_tf.may_fallthru = block_may_fallthru (&TREE_OPERAND (*tp, 0));
  fake_tf.may_throw = true;

  fake_tf.eh_label = make_label ();
  set_eh_region_tree_label (this_region, fake_tf.eh_label);

  honor_protect_cleanup_actions (state, NULL, &fake_tf);

  if (fake_tf.may_throw)
    {
      /* In this case honor_protect_cleanup_actions had nothing to do,
	 and we should process this normally.  */
      lower_eh_constructs_1 (state, &TREE_OPERAND (*tp, 1));
      frob_into_branch_around (tp, fake_tf.eh_label, fake_tf.fallthru_label);
    }
  else
    {
      /* In this case honor_protect_cleanup_actions did nearly all of
	 the work.  All we have left is to append the fallthru_label.  */

      *tp = TREE_OPERAND (*tp, 0);
      if (fake_tf.fallthru_label)
	{
	  tree_stmt_iterator i = tsi_last (tp);
	  tree x = build1 (LABEL_EXPR, void_type_node, fake_tf.fallthru_label);
	  tsi_link_after (&i, x, TSI_NEW_STMT);
	}
    }
}

/* Main loop for lowering eh constructs.  */

static void
lower_eh_constructs_1 (struct leh_state *state, tree *top_p)
{
  tree_stmt_iterator i, j;

  for (i = tsi_start (top_p); !tsi_end_p (i); tsi_next (&i))
    {
      tree *tp = tsi_stmt_ptr (i);
      tree t = *tp;

      switch (TREE_CODE (t))
	{
	case LOOP_EXPR:
	  lower_eh_constructs_1 (state, &LOOP_EXPR_BODY (t));
	  break;
	case COND_EXPR:
	  lower_eh_constructs_1 (state, &COND_EXPR_THEN (t));
	  lower_eh_constructs_1 (state, &COND_EXPR_ELSE (t));
	  break;
	case SWITCH_EXPR:
	  lower_eh_constructs_1 (state, &SWITCH_BODY (t));
	  break;
	case BIND_EXPR:
	  lower_eh_constructs_1 (state, &BIND_EXPR_BODY (t));
	  break;

	case CALL_EXPR:
	  /* Look for things that can throw exceptions, and record them.  */
	  if (state->cur_region && tree_could_throw_p (t))
	    {
	      record_stmt_eh_region (state->cur_region, t);
	      note_eh_region_may_contain_throw (state->cur_region);
	    }
	  break;

	case MODIFY_EXPR:
	  /* Look for things that can throw exceptions, and record them.  */
	  if (state->cur_region && tree_could_throw_p (t))
	    {
	      record_stmt_eh_region (state->cur_region, t);
	      note_eh_region_may_contain_throw (state->cur_region);

	      /* ??? For the benefit of calls.c, converting all this to rtl, 
		 we need to record the call expression, not just the outer
		 modify statement.  */
	      if (TREE_CODE (TREE_OPERAND (t, 1)) == CALL_EXPR)
		record_stmt_eh_region (state->cur_region, TREE_OPERAND (t, 1));
	    }
	  break;

	case GOTO_EXPR:
	case RETURN_EXPR:
	  maybe_record_in_goto_queue (state, t);
	  break;

	case TRY_FINALLY_EXPR:
	  lower_try_finally (state, tp);
	  goto cleanup;

	case TRY_CATCH_EXPR:
	  j = tsi_start (&TREE_OPERAND (t, 1));
	  switch (TREE_CODE (tsi_stmt (j)))
	    {
	    case CATCH_EXPR:
	      lower_catch (state, tp);
	      break;
	    case EH_FILTER_EXPR:
	      lower_eh_filter (state, tp);
	      break;
	    default:
	      lower_cleanup (state, tp);
	      break;
	    }

	cleanup:
	  /* The last right-hand node of a compound_expr, once lowered,
	     would look like more code.  We could notice this case by
	     doing tsi_next before replacement, but this seems cheaper.  */
	  if (tsi_container (i) == tp)
	    return;

	  /* Need to make sure that the compound_exprs are righted.  */
	  if (TREE_CODE (*tp) == COMPOUND_EXPR)
	    {
	      t = *tp;
	      tsi_delink (&i);
	      tsi_link_chain_before (&i, t, TSI_CHAIN_END);
	    }
	  break;

	default:
	  /* A type, a decl, or some kind of statement that we're not
	     interested in.  Don't walk them.  */
	  break;
	}
    }
}

void
lower_eh_constructs (tree *tp)
{
  struct leh_state null_state;

  timevar_push (TV_TREE_EH);

  finally_tree = htab_create (31, struct_ptr_hash, struct_ptr_eq, free);
  throw_stmt_table = htab_create (31, struct_ptr_hash, struct_ptr_eq, free);

  collect_finally_tree (*tp, NULL);

  memset (&null_state, 0, sizeof (null_state));
  lower_eh_constructs_1 (&null_state, tp);

  htab_delete (finally_tree);

  collect_eh_region_array ();

  {
    int flags;
    FILE *file = dump_begin (TDI_eh, &flags);
    if (file)
      {
	dump_function_to_file (current_function_decl, file, flags|TDF_BLOCKS);
        dump_end (TDI_eh, file);
      }
  }

  timevar_pop (TV_TREE_EH);
}



/* Construct EH edges for STMT.  */

static void
make_eh_edge (struct eh_region *region, void *data)
{
  tree stmt, lab;
  basic_block src, dst;

  stmt = data;
  lab = get_eh_region_tree_label (region);

  src = bb_for_stmt (stmt);
  dst = label_to_block (lab);

  make_edge (src, dst, EDGE_ABNORMAL | EDGE_EH);
}
  
void
make_eh_edges (tree stmt)
{
  int region_nr;
  bool is_resx;

  if (TREE_CODE (stmt) == RESX_EXPR)
    {
      region_nr = TREE_INT_CST_LOW (TREE_OPERAND (stmt, 0));
      is_resx = true;
    }
  else
    {
      region_nr = lookup_stmt_eh_region (stmt);
      if (region_nr < 0)
	return;
      is_resx = false;
    }

  foreach_reachable_handler (region_nr, is_resx, make_eh_edge, stmt);
}



/* Return true if the expr can trap, as in dereferencing an
   invalid pointer location.  */

bool
tree_could_trap_p (tree expr)
{
  return (TREE_CODE (expr) == INDIRECT_REF
	  || (TREE_CODE (expr) == COMPONENT_REF
	      && (TREE_CODE (TREE_OPERAND (expr, 0)) == INDIRECT_REF)));
}


bool
tree_could_throw_p (tree t)
{
  if (!flag_exceptions)
    return false;
  if (TREE_CODE (t) == MODIFY_EXPR)
    {
      tree sub = TREE_OPERAND (t, 1);
      if (TREE_CODE (sub) == CALL_EXPR)
	t = sub;
      else
	{
	  if (flag_non_call_exceptions)
	    {
	      if (tree_could_trap_p (sub))
		return true;
	      return tree_could_trap_p (TREE_OPERAND (t, 0));
	    }
	  return false;
	}
    }

  if (TREE_CODE (t) == CALL_EXPR)
    return (call_expr_flags (t) & ECF_NOTHROW) == 0;

  return false;
}

bool
tree_can_throw_internal (tree stmt)
{
  int region_nr = lookup_stmt_eh_region (stmt);
  if (region_nr < 0)
    return false;
  return can_throw_internal_1 (region_nr);
}

bool
tree_can_throw_external (tree stmt)
{
  int region_nr = lookup_stmt_eh_region (stmt);
  if (region_nr < 0)
    return false;
  return can_throw_external_1 (region_nr);
}
