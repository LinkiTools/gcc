/* Control flow functions for trees.
   Copyright (C) 2001 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "errors.h"
#include "flags.h"
#include "expr.h"
#include "ggc.h"
#include "diagnostic.h"
#include "tree-flow.h"

/* Local declarations.  */

/* Initial capacity for the basic block array.  */
static const int initial_cfg_capacity = 20;

/* Dump files and flags.  */
static FILE *dump_file;
static int dump_flags;

/* Basic blocks and flowgraphs.  */
static void make_blocks			PARAMS ((tree, basic_block));
static void make_bind_expr_blocks	PARAMS ((tree, basic_block));
static void make_cond_expr_blocks	PARAMS ((tree, basic_block));
static void make_loop_expr_blocks	PARAMS ((tree, basic_block));
static void make_switch_expr_blocks	PARAMS ((tree, basic_block));
static basic_block create_bb		PARAMS ((tree, basic_block));
static void set_bb_for_stmt		PARAMS ((tree, basic_block));
static void remove_unreachable_blocks	PARAMS ((void));
static void tree_delete_bb		PARAMS ((basic_block));

/* Edges.  */
static void make_edges PARAMS ((void));
static void make_ctrl_stmt_edges	PARAMS ((basic_block));
static void make_exit_edges		PARAMS ((basic_block));
static void make_loop_expr_edges	PARAMS ((basic_block));
static void make_cond_expr_edges	PARAMS ((basic_block));
static void make_goto_expr_edges	PARAMS ((basic_block));
static void make_case_label_edges	PARAMS ((basic_block));

/* Various helpers.  */
static basic_block successor_block	PARAMS ((basic_block));
static basic_block latch_block		PARAMS ((basic_block));
static basic_block first_exec_block	PARAMS ((tree));
static tree first_exec_stmt		PARAMS ((tree));
static bool block_invalidates_loop	PARAMS ((basic_block, struct loop *));
static void remove_bb_ann		PARAMS ((basic_block));
static tree *find_expr_in_tree_helper	PARAMS ((tree, tree, int, int));
static basic_block switch_parent	PARAMS ((basic_block));

/* Remove any COMPOUND_EXPR and WFL container from NODE.  */
#define STRIP_CONTAINERS(NODE)					\
  do {								\
    while (TREE_CODE (NODE) == COMPOUND_EXPR			\
	   || TREE_CODE (NODE) == EXPR_WITH_FILE_LOCATION)	\
      if (TREE_CODE (NODE) == COMPOUND_EXPR)			\
	NODE = TREE_OPERAND (NODE, 0);				\
      else if (TREE_CODE (NODE) == EXPR_WITH_FILE_LOCATION)	\
	NODE = EXPR_WFL_NODE (NODE);				\
  } while (0)


/*---------------------------------------------------------------------------
			      Create basic blocks
---------------------------------------------------------------------------*/

/* Entry point to the CFG builder for trees.  FNBODY is the body of the
   function to process.  */

void
build_tree_cfg (fnbody)
     tree fnbody;
{
  tree first;

  /* Initialize the basic block array.  */
  n_basic_blocks = 0;
  last_basic_block = 0;
  VARRAY_BB_INIT (basic_block_info, initial_cfg_capacity, "basic_block_info");

  /* Create annotations for ENTRY_BLOCK_PTR and EXIT_BLOCK_PTR.  */
  create_bb_ann (ENTRY_BLOCK_PTR);
  create_bb_ann (EXIT_BLOCK_PTR);

  ENTRY_BLOCK_PTR->next_bb = EXIT_BLOCK_PTR;
  EXIT_BLOCK_PTR->prev_bb = ENTRY_BLOCK_PTR;

  /* Find the basic blocks for the flowgraph.  First skip any
     non-executable statements at the start of the function.  Otherwise
     we'll end up with an empty basic block 0, which is useless.  */
  first = first_exec_stmt (fnbody);
  if (first)
    {
      make_blocks (first, NULL);

      if (n_basic_blocks > 0)
	{
	  /* Adjust the size of the array.  */
	  VARRAY_GROW (basic_block_info, n_basic_blocks);

	  /* Create the edges of the flowgraph.  */
	  make_edges ();

	  /* Write the flowgraph to a dot file.  */
	  dump_file = dump_begin (TDI_dot, &dump_flags);
	  if (dump_file)
	    {
	      tree_cfg2dot (dump_file);
	      dump_end (TDI_dot, dump_file);
	    }

	  /* Dump a textual representation of the flowgraph.  */
	  dump_file = dump_begin (TDI_cfg, &dump_flags);
	  if (dump_file)
	    {
	      tree_dump_cfg (dump_file);
	      dump_end (TDI_cfg, dump_file);
	    }
	}
    }
}


/* Build a flowgraph for the tree starting with BODY (assumed to be the
   BIND_EXPR_BODY operand of a BIND_EXPR node).
   
   PARENT_BLOCK is the header block for the control structure
      immediately enclosing the new sub-graph.  */

static void
make_blocks (body, parent_block)
     tree body;
     basic_block parent_block;
{
  basic_block bb;
  bool start_new_block;
  gimple_stmt_iterator i;

  if (body == NULL_TREE || body == empty_stmt_node || body == error_mark_node)
    return;

  bb = NULL;
  start_new_block = true;
  for (i = gsi_start (body); !gsi_after_end (i); gsi_step (&i))
    {
      tree stmt;
      enum tree_code code;
      tree container = i.ptr;

      stmt = gsi_stmt (i);
      STRIP_WFL (stmt);

      /* Ignore non-executable statements.  */
      if (stmt == empty_stmt_node)
	continue;

      code = TREE_CODE (stmt);

      /* Note that we check whether we need to start a new block before
	 processing control statements.  This is because after creating the
	 sub-graph for a control statement, we will always need to start a
	 new block, and by then it will be too late to figure it out.  */
      if (stmt_starts_bb_p (stmt))
	start_new_block = true;

      if (code == BIND_EXPR)
	make_bind_expr_blocks (container, parent_block);
      else if (code == COND_EXPR)
	make_cond_expr_blocks (container, parent_block);
      else if (code == LOOP_EXPR)
	make_loop_expr_blocks (container, parent_block);
      else if (code == SWITCH_EXPR)
	make_switch_expr_blocks (container, parent_block);
      else if (code == TRY_FINALLY_EXPR || code == TRY_CATCH_EXPR)
	/* FIXME: These nodes should've been lowered by gimplify.c  */
	abort ();
      else
	{
	  /* For regular statements, add them the current block and move on
	     to the next statement.  */
	  if (start_new_block)
	    {
	      /* Create a new basic block.  */
	      bb = create_bb (container, parent_block);
	      start_new_block = false;
	    }
	  else
	    {
	      /* Add the statement to the existing basic block.  */
	      set_bb_for_stmt (container, bb);
	      bb->end_tree = container;
	    }
	}
    }
}


/* Create the blocks for the BIND_EXPR node BIND.  PARENT_BLOCK is as in
   make_blocks.  */

static void
make_bind_expr_blocks (bind, parent_block)
     tree bind;
     basic_block parent_block;
{
  basic_block entry;

  entry = create_bb (bind, parent_block);
  entry->flags |= BB_CONTROL_ENTRY;
  STRIP_CONTAINERS (bind);
  make_blocks (BIND_EXPR_BODY (bind), entry);
}


/* Create the blocks for the LOOP_EXPR node LOOP.  PARENT_BLOCK is as in
   make_blocks.  */

static void
make_loop_expr_blocks (loop, parent_block)
     tree loop;
     basic_block parent_block;
{
  basic_block entry, latch;
  
  entry = create_bb (loop, parent_block);
  entry->flags |= BB_CONTROL_ENTRY | BB_CONTROL_EXPR | BB_LOOP_CONTROL_EXPR;

  /* Create an empty block to serve as latch to prevent flow_loops_find
     from finding multiple natural loops in the presence of multiple back
     edges.
     
     ??? Note that latch_block assumes that the latch block is the
	 successor of the entry block in the linked list of blocks.  */
  latch = create_bb (empty_stmt_node, parent_block);
  latch->flags |= BB_CONTROL_EXPR | BB_LOOP_CONTROL_EXPR;
  
  STRIP_CONTAINERS (loop);
  make_blocks (LOOP_EXPR_BODY (loop), entry);
}


/* Create the blocks for a COND_EXPR.  PARENT_BLOCK is as in make_blocks.  */

static void
make_cond_expr_blocks (cond, parent_block)
     tree cond;
     basic_block parent_block;
{
  basic_block entry = create_bb (cond, parent_block);
  entry->flags |= BB_CONTROL_ENTRY | BB_CONTROL_EXPR;

  STRIP_CONTAINERS (cond);
  make_blocks (COND_EXPR_THEN (cond), entry);
  make_blocks (COND_EXPR_ELSE (cond), entry);
}


/* Create the blocks for a SWITCH_EXPR.  PARENT_BLOCK is as as in
   make_blocks.  */

static void
make_switch_expr_blocks (switch_e, parent_block)
     tree switch_e;
     basic_block parent_block;
{
  basic_block entry = create_bb (switch_e, parent_block);
  entry->flags |= BB_CONTROL_ENTRY | BB_CONTROL_EXPR;

  STRIP_CONTAINERS (switch_e);
  make_blocks (SWITCH_BODY (switch_e), entry);
}


/* Create and return a new basic block.

   HEAD is the first statement in the block.

   PARENT_BLOCK is the entry block for the control structure containing
      the new block.  */

static basic_block
create_bb (head, parent_block)
     tree head;
     basic_block parent_block;
{
  basic_block bb;

  /* Create and initialize a new basic block.  */
  bb = (basic_block) ggc_alloc (sizeof (*bb));
  memset (bb, 0, sizeof (*bb));

  bb->head_tree = bb->end_tree = head;
  bb->index = last_basic_block;
  bb->flags = BB_NEW;

  /* Create annotations for the block.  */
  create_bb_ann (bb);
  set_parent_block (bb, parent_block);

  /* Add the new block to the linked list of blocks.  */
  if (n_basic_blocks > 0)
    link_block (bb, BASIC_BLOCK (n_basic_blocks - 1));
  else
    link_block (bb, ENTRY_BLOCK_PTR);

  /* Grow the basic block array if needed.  */
  if ((size_t) n_basic_blocks == VARRAY_SIZE (basic_block_info))
    VARRAY_GROW (basic_block_info, n_basic_blocks + (n_basic_blocks + 3) / 4);

  /* Add the newly created block to the array.  */
  BASIC_BLOCK (n_basic_blocks) = bb;
  n_basic_blocks++;
  last_basic_block++;

  /* Associate the newly created block to the head tree.  */
  set_bb_for_stmt (head, bb);

  return bb;
}


/* Add statement T to basic block BB.  If T is a COMPOUND_EXPR or WFL
   container, also process the nodes inside it.  */

static void
set_bb_for_stmt (t, bb)
     tree t;
     basic_block bb;
{
  tree_ann ann;

  if (t == empty_stmt_node)
    return;

  do
    {
      ann = tree_annotation (t) ? tree_annotation (t) : create_tree_ann (t);
      ann->bb = bb;
      if (TREE_CODE (t) == COMPOUND_EXPR)
	t = TREE_OPERAND (t, 0);
      else if (TREE_CODE (t) == EXPR_WITH_FILE_LOCATION)
	t = EXPR_WFL_NODE (t);
      else
	t = NULL;
    }
  while (t);
}


/* Create a new annotation for basic block BB.  */

bb_ann
create_bb_ann (bb)
     basic_block bb;
{
  bb_ann ann = (bb_ann) ggc_alloc (sizeof (*ann));
  memset ((void *) ann, 0, sizeof (*ann));
  ann->refs = create_ref_list ();
  bb->aux = (void *) ann;

  return ann;
}


/* Remove the annotation from block BB.  */

static void
remove_bb_ann (bb)
     basic_block bb;
{
  bb_ann ann = (bb_ann)bb->aux;

  if (ann)
    {
      ann->parent_block = NULL;
      delete_ref_list (ann->refs);
    }

  bb->aux = NULL;
}


/*---------------------------------------------------------------------------
				 Edge creation
---------------------------------------------------------------------------*/

/* Join all the blocks in the flowgraph.  */

static void
make_edges ()
{
  basic_block bb;

  /* Create an edge from entry to the first block with executable
     statements in it.  */
  make_edge (ENTRY_BLOCK_PTR, BASIC_BLOCK (0), EDGE_FALLTHRU);

  /* Traverse basic block array placing edges.  */
  FOR_EACH_BB (bb)
    {
      tree first = first_stmt (bb);
      tree last = last_stmt (bb);

      /* Edges for control statements.  */
      if (is_ctrl_stmt (first))
	make_ctrl_stmt_edges (bb);

      /* Edges for control flow altering statements (GOTO_EXPR and
	 RETURN_EXPR) need an edge to the corresponding target block.  */
      if (is_ctrl_altering_stmt (last))
	make_exit_edges (bb);

      /* Incoming edges for label blocks in switch statements.  It's easier
         to deal with these bottom-up than top-down.  */
      if (TREE_CODE (first) == CASE_LABEL_EXPR)
	make_case_label_edges (bb);

      /* BIND_EXPR blocks get an edge to the first basic block in the
	 BIND_EXPR_BODY.  */
      if (TREE_CODE (first) == BIND_EXPR)
	{
	  basic_block body_bb = first_exec_block (BIND_EXPR_BODY (first));
	  if (body_bb)
	    make_edge (bb, body_bb, EDGE_FALLTHRU);
	}

      /* Finally, if no edges were created above, this is a regular
         basic block that only needs a fallthru edge.  */
      if (bb->succ == NULL)
	make_edge (bb, successor_block (bb), EDGE_FALLTHRU);
    }

  /* Clean up the graph and warn for unreachable code.  */
  tree_cleanup_cfg ();
}


/* Create edges for control statement at basic block BB.  */

static void
make_ctrl_stmt_edges (bb)
     basic_block bb;
{
  switch (TREE_CODE (first_stmt (bb)))
    {
    case LOOP_EXPR:
      make_loop_expr_edges (bb);
      break;

    case COND_EXPR:
      make_cond_expr_edges (bb);
      break;

    case SWITCH_EXPR:
      /* Nothing to do.  Each label inside the switch statement will create
         its own edge from the switch block.  */
      break;

    default:
      abort ();
    }
}


/* Create exit edges for statements that alter the flow of control
   (BREAK, CONTINUE, GOTO, RETURN and calls to non-returning functions).  */

static void
make_exit_edges (bb)
     basic_block bb;
{
  switch (TREE_CODE (last_stmt (bb)))
    {
    case GOTO_EXPR:
      make_goto_expr_edges (bb);
      break;

      /* A CALL_EXPR node here means that the last statement of the block
	 is a call to a non-returning function.  */
    case CALL_EXPR:
    case RETURN_EXPR:
      make_edge (bb, EXIT_BLOCK_PTR, 0);
      break;

    default:
      abort ();
    }
}


/* Create the edges for a LOOP_EXPR structure starting with bb.  */

static void
make_loop_expr_edges (bb)
     basic_block bb;
{
  tree entry = first_stmt (bb);
  basic_block end_bb, body_bb;

#if defined ENABLE_CHECKING
  if (TREE_CODE (entry) != LOOP_EXPR)
    abort ();
#endif

  /* Create the following edges.  The other edges will be naturally created
     by the main loop in create_edges().

         +---> LOOP_EXPR -----+
	 |         |          |
         |         v          |
         |   LOOP_EXPR_BODY   |
	 |                    |
	 |                    |
	 +---- END_WHILE      |
	                      |
		              |
	     Next block <-----+
          
     If the body doesn't exist, we use the header instead.  */

  /* Basic blocks for each component.  */
  end_bb = latch_block (bb);
  body_bb = first_exec_block (LOOP_EXPR_BODY (entry));

  /* Empty loop body?  Use the latch block.  */
  if (body_bb == NULL)
    body_bb = end_bb;

  make_edge (bb, body_bb, 0);
  make_edge (end_bb, bb, 0);
}


/* Create the edges for a COND_EXPR structure starting with BB.  */

static void
make_cond_expr_edges (bb)
     basic_block bb;
{
  tree entry = first_stmt (bb);
  basic_block successor_bb, then_bb, else_bb;
  tree predicate;

#if defined ENABLE_CHECKING
  if (TREE_CODE (entry) != COND_EXPR)
    abort ();
#endif

  /* Entry basic blocks for each component.  */
  then_bb = first_exec_block (COND_EXPR_THEN (entry));
  else_bb = first_exec_block (COND_EXPR_ELSE (entry));
  successor_bb = successor_block (bb);

  /* Create the following edges.

	      COND_EXPR
		/ \
	       /   \
	    THEN   ELSE

     Either clause may be empty.  Linearize the IF statement if the
     conditional can be statically computed.  */
  predicate = COND_EXPR_COND (entry);

  if (simple_cst_equal (predicate, integer_one_node) == 1)
    else_bb = NULL;

  if (simple_cst_equal (predicate, integer_zero_node) == 1)
    then_bb = NULL;

  if (then_bb)
    make_edge (bb, then_bb, EDGE_TRUE_VALUE);

  if (else_bb)
    make_edge (bb, else_bb, EDGE_FALSE_VALUE);

  /* If conditional is missing one of the clauses, make an edge between the
     entry block and the first block outside the conditional.  */
  if (!then_bb || !else_bb)
    make_edge (bb, successor_bb, 0);
}


/* Create edges for a goto statement.  */

static void
make_goto_expr_edges (bb)
     basic_block bb;
{
  tree goto_t, dest;
  basic_block target_bb;

  goto_t = last_stmt (bb);

#if defined ENABLE_CHECKING
  if (goto_t == NULL || TREE_CODE (goto_t) != GOTO_EXPR)
    abort ();
#endif

  dest = GOTO_DESTINATION (goto_t);

  /* Look for the block starting with the destination label.  In the
     case of a computed goto, make an edge to any label block we find
     in the CFG.  */
  FOR_EACH_BB (target_bb)
    {
      tree target = first_stmt (target_bb);

      /* Common case, destination is a single label.  Make the edge
         and leave.  */
      if (TREE_CODE (dest) == LABEL_DECL
	  && TREE_CODE (target) == LABEL_EXPR
	  && LABEL_EXPR_LABEL (target) == dest)
	{
	  make_edge (bb, target_bb, 0);
	  break;
	}

      /* Computed GOTOs.  Make an edge to every label block.  */
      else if (TREE_CODE (dest) != LABEL_DECL
	       && TREE_CODE (target) == LABEL_EXPR)
	make_edge (bb, target_bb, 0);
    }
}


/* Create the edge between CASE_LABEL_EXPR block BB and the block for the
   associated SWITCH_EXPR node.  */

static void
make_case_label_edges (bb)
     basic_block bb;
{
  basic_block switch_bb = switch_parent (bb);

#if defined ENABLE_CHECKING
  if (switch_bb == NULL)
    abort ();
#endif

  make_edge (switch_bb, bb, 0);

  /* If this label is the default label, we need to remove the
     fallthru edge that was created when we processed the entry
     block for the switch() statement.  */
  if (CASE_LOW (first_stmt (bb)) == NULL_TREE)
    {
      edge e;
      basic_block entry_bb, chain_bb;

      entry_bb = parent_block (bb);
      chain_bb = successor_block (entry_bb);
      for (e = entry_bb->succ; e; e = e->succ_next)
	if (e->dest == chain_bb)
	  {
	    remove_edge (e);
	    break;
	  }
    }
}



/*---------------------------------------------------------------------------
			       Flowgraph analysis
---------------------------------------------------------------------------*/

/* Remove unreachable blocks and other miscellaneous clean up work.  */

void
tree_cleanup_cfg ()
{
  remove_unreachable_blocks ();
}


/* Delete all unreachable basic blocks.   */

static void
remove_unreachable_blocks ()
{
  basic_block next_bb, bb;

  find_unreachable_blocks ();

  for (bb = ENTRY_BLOCK_PTR->next_bb; bb != EXIT_BLOCK_PTR; bb = next_bb)
    {
      next_bb = bb->next_bb;

      if (!(bb->flags & BB_REACHABLE))
	tree_delete_bb (bb);
    }
}


/* Remove a block from the flowgraph.  */

static void
tree_delete_bb (bb)
     basic_block bb;
{
  tree t;

  dump_file = dump_begin (TDI_cfg, &dump_flags);
  if (dump_file)
    {
      fprintf (dump_file, "Removed unreachable basic block %d\n", bb->index);
      tree_dump_bb (dump_file, "", bb, 0);
      fprintf (dump_file, "\n");
      dump_end (TDI_cfg, dump_file);
    }

  /* Unmap all the instructions in the block.  */
  t = first_stmt (bb);
  while (t)
    {
      set_bb_for_stmt (t, NULL);
      if (t == last_stmt (bb))
	break;
      t = TREE_CHAIN (t);
    }

  /* Remove the edges into and out of this block.  */
  while (bb->pred != NULL)
    remove_edge (bb->pred);

  while (bb->succ != NULL)
    remove_edge (bb->succ);

  bb->pred = NULL;
  bb->succ = NULL;

  /* Remove the basic block from the array.  */
  expunge_block (bb);
}


/* Scan all the loops in the flowgraph verifying their validity.   A valid
   loop L contains no calls to user functions, no returns, no jumps out of
   the loop and non-local gotos.  */

void
validate_loops (loops)
     struct loops *loops;
{
  int i;

  for (i = 0; i < loops->num; i++)
    {
      struct loop *loop = &(loops->array[i]);
      sbitmap nodes = loop->nodes;
      int n;

      EXECUTE_IF_SET_IN_SBITMAP (nodes, 0, n,
	  {
	    if (block_invalidates_loop (BASIC_BLOCK (n), loop))
	      {
		loop->invalid = 1;
		break;
	      }
	  });
    }
}


/* Return true if the basic block BB makes the LOOP invalid.  This occurs if
   the block contains a call to a user function, a return, a jump out of the
   loop or a non-local goto.  */

static bool
block_invalidates_loop (bb, loop)
     basic_block bb;
     struct loop *loop;
{
  tree_ref ref;
  struct ref_list_node *tmp;

  /* Valid loops cannot contain a return statement.  */
  if (TREE_CODE (last_stmt (bb)) == RETURN_EXPR)
    return true;

  /* If the destination node of a goto statement is not in the loop, mark it
     invalid.  */
  if (TREE_CODE (last_stmt (bb)) == GOTO_EXPR
      && ! TEST_BIT (loop->nodes, bb->succ->dest->index))
    return true;

  /* If the node contains a non-pure function call, mark it invalid.  A
     non-pure function call is marked by the presence of a clobbering
     definition of GLOBAL_VAR.  */
  FOR_EACH_REF (ref, tmp, bb_refs (bb))
    if (ref_var (ref) == global_var && (ref_type (ref) & (V_DEF | M_CLOBBER)))
      return true;

  return false;
}



/*---------------------------------------------------------------------------
			 Code insertion and replacement
---------------------------------------------------------------------------*/

/* Insert statement STMT before tree WHERE in basic block BB.  */

void
insert_stmt_before (stmt, where, bb)
     tree stmt ATTRIBUTE_UNUSED;
     tree where ATTRIBUTE_UNUSED;
     basic_block bb ATTRIBUTE_UNUSED;
{
}


/* Replace expression EXPR in tree T with NEW_EXPR.  */

void
replace_expr_in_tree (t, old_expr, new_expr)
     tree t;
     tree old_expr;
     tree new_expr;
{
  tree *old_expr_p = find_expr_in_tree (t, old_expr);

  dump_file = dump_begin (TDI_cfg, &dump_flags);
  if (dump_file)
    {
      if (old_expr_p)
	{
	  fprintf (dump_file, "\nRequested expression: ");
	  print_c_node_brief (dump_file, old_expr);

	  fprintf (dump_file, "\nReplaced expression:  ");
	  print_c_node_brief (dump_file, *old_expr_p);

	  fprintf (dump_file, "\nWith expression:      ");
	  print_c_node_brief (dump_file, new_expr);
	}
      else
	{
	  fprintf (dump_file, "\nCould not find expression: ");
	  print_c_node_brief (dump_file, old_expr);
	}

      fprintf (dump_file, "\nIn statement:        ");
      print_c_node_brief (dump_file, t);

      fprintf (dump_file, "\nBasic block:         ");
      fprintf (dump_file, "%d", bb_for_stmt (t)->index);

      fprintf (dump_file, "\n");

      dump_end (TDI_cfg, dump_file);
    }

  if (old_expr_p)
    *old_expr_p = new_expr;
}

/* Returns the location of expression EXPR in T.  If SUBSTATE is
   true, the search will search sub-statements.  If SUBSTATE is
   false,  the search is guaranteed to not go outside statement
   nodes, only their sub-expressions are searched.  LEVEL is an
   internal parameter used to track the recursion level, external
   users should pass 0.  */

static tree *
find_expr_in_tree_helper (t, expr, level, substate)
     tree t;
     tree expr;
     int level;
     int substate;
{
  int i;
  tree *loc;

  if (t == NULL || t == error_mark_node
      || expr == NULL || expr == error_mark_node)
    return NULL;

  /* Deal with special trees first.  */
  switch (TREE_CODE (t))
    {
      case COMPLEX_CST:
      case INTEGER_CST:
      case LABEL_DECL:
      case REAL_CST:
      case RESULT_DECL:
      case STRING_CST:
      case IDENTIFIER_NODE:
	return NULL;

      case TREE_LIST:
	  {
	    tree op;

	    /* Try the list elements.  */
	    for (op = t; op; op = TREE_CHAIN (op))
	      if (TREE_VALUE (op) == expr)
		return &(TREE_VALUE (op));

	    /* Not there?  Recurse into each of the list elements.  */
	    for (op = t; op; op = TREE_CHAIN (op))
	      {
		loc = find_expr_in_tree_helper (TREE_VALUE (op), expr, level++, substate);
		if (loc)
		  return loc;
	      }

	    return NULL;
	  }

      default:
	break;
    }

  /* Try the immediate operands.  */
  for (i = 0; i < TREE_CODE_LENGTH (TREE_CODE (t)); i++)
    if (TREE_OPERAND (t, i) == expr)
      return &(TREE_OPERAND (t, i));

  /* If we still haven't found it, recurse into each sub-expression of T.  */
  for (i = 0; i < TREE_CODE_LENGTH (TREE_CODE (t)); i++)
    {
      loc = find_expr_in_tree_helper (TREE_OPERAND (t, i), expr, level++, substate);
      if (loc)
	return loc;
    }

  /* Finally, recurse into its chain (this limits the search to a single
     statement).  */
  if (substate && level != 0)
    {
      loc = find_expr_in_tree_helper (TREE_CHAIN (t), expr, level, substate);
      if (loc)
	return loc;
    }

  return NULL;
}

tree *
find_expr_in_tree (t, expr)
	tree t;
	tree expr;
{
	return find_expr_in_tree_helper (t, expr, 0, true);
}


/* Insert basic block NEW_BB before BB.  */

void
insert_bb_before (new_bb, bb)
     basic_block new_bb;
     basic_block bb;
{
  edge e;

  /* Reconnect BB's predecessors to NEW_BB.  */
  for (e = bb->pred; e; e = e->pred_next)
    redirect_edge_succ (e, new_bb);

  /* Create the edge NEW_BB -> BB.  */
  make_edge (new_bb, bb, 0);
}



/*---------------------------------------------------------------------------
			      Debugging functions
---------------------------------------------------------------------------*/

/* Dump a basic block to a file.  */

void
tree_dump_bb (outf, prefix, bb, indent)
     FILE *outf;
     const char *prefix;
     basic_block bb;
     int indent;
{
  edge e;
  tree head = bb->head_tree;
  tree end = bb->end_tree;
  char *s_indent;
  int lineno;

  s_indent = (char *) alloca ((size_t) indent + 1);
  memset ((void *) s_indent, ' ', (size_t) indent);
  s_indent[indent] = '\0';

  fprintf (outf, "%s%sBasic block %d\n", s_indent, prefix, bb->index);

  fprintf (outf, "%s%sPredecessors: ", s_indent, prefix);
  for (e = bb->pred; e; e = e->pred_next)
    dump_edge_info (outf, e, 0);
  putc ('\n', outf);

  fprintf (outf, "%s%sSuccessors: ", s_indent, prefix);
  for (e = bb->succ; e; e = e->succ_next)
    dump_edge_info (outf, e, 1);
  putc ('\n', outf);

  fprintf (outf, "%s%sHead: ", s_indent, prefix);
  if (head)
    {
      lineno = get_lineno (head);
      print_c_node_brief (outf, head);
      fprintf (outf, " (line: %d)\n", lineno);
    }
  else
    fputs ("nil\n", outf);

  fprintf (outf, "%s%sEnd: ", s_indent, prefix);
  if (end)
    {
      lineno = get_lineno (end);
      print_c_node_brief (outf, end);
      fprintf (outf, " (line: %d)\n", lineno);
    }
  else
    fputs ("nil\n", outf);

  fprintf (outf, "%s%sParent block: ", s_indent, prefix);
  if (parent_block (bb))
    fprintf (outf, "%d\n", parent_block (bb)->index);
  else
    fputs ("nil\n", outf);

  fprintf (outf, "%s%sLoop depth: %d\n", s_indent, prefix, bb->loop_depth);

  fprintf (outf, "%s%sNext block: ", s_indent, prefix);
  if (bb->next_bb)
    fprintf (outf, "%d\n", bb->next_bb->index);
  else
    fprintf (outf, "nil\n");

  fprintf (outf, "%s%sPrevious block: ", s_indent, prefix);
  if (bb->prev_bb)
    fprintf (outf, "%d\n", bb->prev_bb->index);
  else
    fprintf (outf, "nil\n");
}


/* Dump a basic block on stderr.  */

void
tree_debug_bb (bb)
     basic_block bb;
{
  tree_dump_bb (stderr, "", bb, 0);
}


/* Dump the CFG on stderr.  */

void
tree_debug_cfg ()
{
  tree_dump_cfg (stderr);
}


/* Dump the CFG on the given FILE.  */

void
tree_dump_cfg (file)
     FILE *file;
{
  basic_block bb;

  fputc ('\n', file);
  fprintf (file, "Function %s\n\n", get_name (current_function_decl));

  fprintf (file, "\n%d basic blocks, %d edges, last basic block %d.\n",
           n_basic_blocks, n_edges, last_basic_block);
  FOR_EACH_BB (bb)
    {
      tree_dump_bb (file, "", bb, 0);
      fputc ('\n', file);
    }
}


/* Dump the flowgraph to a .dot FILE.  */

void
tree_cfg2dot (file)
     FILE *file;
{
  edge e;
  basic_block bb;

  /* Write the file header.  */
  fprintf (file, "digraph %s\n{\n", get_name (current_function_decl));

  /* Write blocks and edges.  */
  for (e = ENTRY_BLOCK_PTR->succ; e; e = e->succ_next)
    {
      fprintf (file, "\tENTRY -> %d", e->dest->index);

      if (e->flags & EDGE_FAKE)
	fprintf (file, " [weight=0, style=dotted]");

      fprintf (file, ";\n");
    }
  fputc ('\n', file);

  FOR_EACH_BB (bb)
    {
      enum tree_code head_code, end_code;
      const char *head_name, *end_name;
      int head_line, end_line;

      head_code = TREE_CODE (first_stmt (bb));
      end_code = TREE_CODE (last_stmt (bb));

      head_name = tree_code_name[head_code];
      end_name = tree_code_name[end_code];

      /* Must access head_tree and end_tree directly to get line number
	 information because first_stmt and last_stmt strip it out.  */
      head_line = get_lineno (bb->head_tree);
      end_line = get_lineno (bb->end_tree);

      fprintf (file, "\t%d [label=\"#%d\\n%s (%d)\\n%s (%d)\"];\n",
	       bb->index, bb->index, head_name, head_line, end_name,
	       end_line);

      for (e = bb->succ; e; e = e->succ_next)
	{
	  if (e->dest == EXIT_BLOCK_PTR)
	    fprintf (file, "\t%d -> EXIT", bb->index);
	  else
	    fprintf (file, "\t%d -> %d", bb->index, e->dest->index);

	  if (e->flags & EDGE_FAKE)
	    fprintf (file, " [weight=0, style=dotted]");

	  fprintf (file, ";\n");
	}

      if (bb->next_bb != EXIT_BLOCK_PTR)
	fputc ('\n', file);
    }

  fputs ("}\n\n", file);
}



/*---------------------------------------------------------------------------
		 Miscellaneous helper functions and predicates
---------------------------------------------------------------------------*/

/* Return the successor block for BB.  If the block has no successors we
   try the enclosing control structure until we find one.  If we reached
   nesting level 0, return the exit block.  */

static basic_block
successor_block (bb)
     basic_block bb;
{
  basic_block succ_bb, parent_bb;
  gimple_stmt_iterator i;

#if defined ENABLE_CHECKING
  if (bb == NULL)
    abort ();
#endif

  /* By default, the successor block will be the block for the statement
     following BB's last statement.  Note that this will skip over any
     blocks created for BIND_EXPR nodes.
     
     This is intentional.  The only reason why we create basic blocks for
     BIND_EXPR nodes is to be able to find the successor block in cases
     like this:

	    1	a = 10;
	    2	b = 5;
	    3	{
	    4	  int j;
	    5	  {
	    6	    int k;
	    7	    {
	    8	      int l;
	    9	
	    10	      l = a + k;
	    11	    }
	    12	  }
	    13	}
	    14	b = a + b;

     If we didn't create a basic block for the braces at lines 3, 5 and 7,
     we would not realize that the successor block for line 10 is the block
     at line 14.
     
     However, since blocks for BIND_EXPR nodes do not really contribute
     anything to the flowgraph, we don't want to create edges to them,
     leaving them unreachable.  The post-processing cleanup will remove
     them from the graph.  */
  i = gsi_start (bb->end_tree);
  gsi_step (&i);
  succ_bb = first_exec_block (gsi_stmt (i));
  if (succ_bb)
    return succ_bb;

  /* We couldn't find a successor for BB.  Walk up the control structure to
     see if our parent has a successor. Iterate until we find one or we
     reach nesting level 0.  */
  parent_bb = parent_block (bb);
  while (parent_bb)
    {
      /* If BB is the last block inside a loop body, return the condition
         block for the loop structure.  */
      if (is_loop_stmt (first_stmt (parent_bb)))
	return latch_block (parent_bb);

      /* Otherwise, If BB's control parent has a successor, return its
         block.  */
      i = gsi_start (parent_bb->end_tree);
      gsi_step (&i);
      succ_bb = first_exec_block (gsi_stmt (i));
      if (succ_bb)
	return succ_bb;

      /* None of the above.  Keeping going up the control parent chain.  */
      parent_bb = parent_block (parent_bb);
    }

  /* We reached nesting level 0.  Return the exit block.  */
  return EXIT_BLOCK_PTR;
}


/* Return true if T represents a control statement.  */

bool
is_ctrl_stmt (t)
     tree t;
{
#if defined ENABLE_CHECKING
  if (t == NULL)
    abort ();
#endif

  STRIP_WFL (t);
  return (TREE_CODE (t) == COND_EXPR
	  || TREE_CODE (t) == LOOP_EXPR
	  || TREE_CODE (t) == SWITCH_EXPR);
}


/* Return true if T alters the flow of control (i.e., T is BREAK, GOTO,
   CONTINUE or RETURN)  */

bool
is_ctrl_altering_stmt (t)
     tree t;
{
#if defined ENABLE_CHECKING
  if (t == NULL)
    abort ();
#endif

  STRIP_WFL (t);
  if (TREE_CODE (t) == GOTO_EXPR || TREE_CODE (t) == RETURN_EXPR)
    return true;

  /* Calls to non-returning functions also alter the flow of control.  */
  if (TREE_CODE (t) == CALL_EXPR)
    {
      int flags;
      tree decl = get_callee_fndecl (t);

      if (decl)
	flags = flags_from_decl_or_type (decl);
      else
	{
	  t = TREE_OPERAND (t, 0);
	  flags = flags_from_decl_or_type (TREE_TYPE (TREE_TYPE (t)));
	}
	
      return flags & ECF_NORETURN;
    }

  return false;
}


/* Return true if T represent a loop statement.  */

bool
is_loop_stmt (t)
     tree t;
{
  STRIP_WFL (t);
  return (TREE_CODE (t) == LOOP_EXPR);
}


/* Return true if T is a computed goto.  */

bool
is_computed_goto (t)
     tree t;
{
  STRIP_WFL (t);
  return (TREE_CODE (t) == GOTO_EXPR
          && TREE_CODE (GOTO_DESTINATION (t)) != LABEL_DECL);
}


/* Return true if T should start a new basic block.  */

bool
stmt_starts_bb_p (t)
     tree t;
{
  STRIP_WFL (t);
  return (TREE_CODE (t) == CASE_LABEL_EXPR
	  || TREE_CODE (t) == LABEL_EXPR
	  || TREE_CODE (t) == RETURN_EXPR
	  || TREE_CODE (t) == BIND_EXPR
	  || is_ctrl_stmt (t));
}


/* Remove all the blocks and edges that make up the flowgraph.  */

void
delete_tree_cfg ()
{
  basic_block bb;

  if (basic_block_info == NULL)
    return;

  FOR_EACH_BB (bb)
    remove_bb_ann (bb);

  remove_bb_ann (ENTRY_BLOCK_PTR);
  remove_bb_ann (EXIT_BLOCK_PTR);

  clear_edges ();
  VARRAY_FREE (basic_block_info);
}


/* Returns the block marking the end of the loop body.  This is the block
   that contains the back edge to the start of the loop.  */

static basic_block
latch_block (loop_bb)
     basic_block loop_bb;
{
  tree loop = first_stmt (loop_bb);

#if defined ENABLE_CHECKING
  if (TREE_CODE (loop) != LOOP_EXPR)
    abort ();

  if (loop_bb->next_bb == NULL
      || loop_bb->next_bb->head_tree != empty_stmt_node
      || loop_bb->next_bb->end_tree != empty_stmt_node)
    abort ();
#endif

  /* The latch block for a LOOP_EXPR is guaranteed to be the next block in
     the linked list (see make_loop_expr_blocks).  */
  return loop_bb->next_bb;
}


/* Return the first executable statement starting at ENTRY.  */

static tree
first_exec_stmt (entry)
     tree entry;
{
  gimple_stmt_iterator i;
  tree stmt;
  
  for (i = gsi_start (entry); !gsi_after_end (i); gsi_step (&i))
    {
      stmt = gsi_stmt (i);
      STRIP_WFL (stmt);

      /* Dive into BIND_EXPR bodies.  */
      if (TREE_CODE (stmt) == BIND_EXPR)
	return first_exec_stmt (BIND_EXPR_BODY (stmt));

      /* Don't consider empty_stmt_node to be executable.  Note that we
	 actually return the container for the executable statement, not
	 the statement itself.  This is to allow the caller to start
	 iterating from this point.  */
      else if (stmt != empty_stmt_node)
	return gsi_container (i);
    }

  return NULL_TREE;
}


/* Return the first basic block with executable statements in it, starting
   at ENTRY.  */

static basic_block
first_exec_block (entry)
     tree entry;
{
  tree exec;

  if (entry == empty_stmt_node || entry == NULL_TREE)
    return NULL;

  exec = first_exec_stmt (entry);
  return (exec) ? bb_for_stmt (exec) : NULL;
}


/* Returns the header block for the innermost switch statement containing
   BB.  It returns NULL if BB is not inside a switch statement.  */

static basic_block
switch_parent (bb)
     basic_block bb;
{
  do
    bb = parent_block (bb);
  while (bb && TREE_CODE (first_stmt (bb)) != SWITCH_EXPR);

  return bb;
}


/* Return the first statement in basic block BB.  */

tree
first_stmt (bb)
     basic_block bb;
{
  gimple_stmt_iterator i;
  tree t;

  i = gsi_start_bb (bb);
  t = gsi_stmt (i);
  STRIP_WFL (t);
  return t;
}


/* Return the last statement in basic block BB.  */

tree
last_stmt (bb)
     basic_block bb;
{
  gimple_stmt_iterator i;
  tree t;
  
  i = gsi_start (bb->end_tree);
  t = gsi_stmt (i);
  STRIP_WFL (t);
  return t;
}
