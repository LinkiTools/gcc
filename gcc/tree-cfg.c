/* Control flow functions for trees.
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "ggc.h"
#include "langhooks.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "timevar.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "toplev.h"
#include "except.h"
#include "cfgloop.h"
#include "cfglayout.h"
#include "tree-ssa-propagate.h"
#include "value-prof.h"
#include "pointer-set.h"
#include "tree-inline.h"

/* This file contains functions for building the Control Flow Graph (CFG)
   for a function tree.  */

/* Local declarations.  */

/* Initial capacity for the basic block array.  */
static const int initial_cfg_capacity = 20;

/* This hash table allows us to efficiently lookup all CASE_LABEL_EXPRs
   which use a particular edge.  The CASE_LABEL_EXPRs are chained together
   via their TREE_CHAIN field, which we clear after we're done with the
   hash table to prevent problems with duplication of GIMPLE_SWITCHes.

   Access to this list of CASE_LABEL_EXPRs allows us to efficiently
   update the case vector in response to edge redirections.

   Right now this table is set up and torn down at key points in the
   compilation process.  It would be nice if we could make the table
   more persistent.  The key is getting notification of changes to
   the CFG (particularly edge removal, creation and redirection).  */

static struct pointer_map_t *edge_to_cases;

/* CFG statistics.  */
struct cfg_stats_d
{
  long num_merged_labels;
};

static struct cfg_stats_d cfg_stats;

/* Nonzero if we found a computed goto while building basic blocks.  */
static bool found_computed_goto;

/* Basic blocks and flowgraphs.  */
static void make_blocks (gimple_seq);
static void factor_computed_gotos (void);

/* Edges.  */
static void make_edges (void);
static void make_cond_expr_edges (basic_block);
static void make_gimple_switch_edges (basic_block);
static void make_goto_expr_edges (basic_block);
/* FIXME tuples.  */
#if 0
static edge tree_redirect_edge_and_branch (edge, basic_block);
static edge tree_try_redirect_by_replacing_jump (edge, basic_block);
#endif
static unsigned int split_critical_edges (void);

/* Various helpers.  */
static inline bool stmt_starts_bb_p (gimple, gimple);
static int tree_verify_flow_info (void);
static void tree_make_forwarder_block (edge);
static void tree_cfg2vcg (FILE *);
static inline void change_bb_for_stmt (gimple, basic_block);

/* Flowgraph optimization and cleanup.  */
static void tree_merge_blocks (basic_block, basic_block);
static bool tree_can_merge_blocks_p (basic_block, basic_block);
static void remove_bb (basic_block);
static edge find_taken_edge_computed_goto (basic_block, tree);
static edge find_taken_edge_cond_expr (basic_block, tree);
static edge find_taken_edge_switch_expr (basic_block, tree);
static tree find_case_label_for_value (gimple, tree);

void
init_empty_tree_cfg (void)
{
  /* Initialize the basic block array.  */
  init_flow ();
  profile_status = PROFILE_ABSENT;
  n_basic_blocks = NUM_FIXED_BLOCKS;
  last_basic_block = NUM_FIXED_BLOCKS;
  basic_block_info = VEC_alloc (basic_block, gc, initial_cfg_capacity);
  VEC_safe_grow_cleared (basic_block, gc, basic_block_info,
			 initial_cfg_capacity);

  /* Build a mapping of labels to their associated blocks.  */
  label_to_block_map = VEC_alloc (basic_block, gc, initial_cfg_capacity);
  VEC_safe_grow_cleared (basic_block, gc, label_to_block_map,
			 initial_cfg_capacity);

  SET_BASIC_BLOCK (ENTRY_BLOCK, ENTRY_BLOCK_PTR);
  SET_BASIC_BLOCK (EXIT_BLOCK, EXIT_BLOCK_PTR);
  ENTRY_BLOCK_PTR->next_bb = EXIT_BLOCK_PTR;
  EXIT_BLOCK_PTR->prev_bb = ENTRY_BLOCK_PTR;
}

/*---------------------------------------------------------------------------
			      Create basic blocks
---------------------------------------------------------------------------*/

/* Entry point to the CFG builder for trees.  SEQ is the sequence of
   statements to be added to the flowgraph.  */

static void
build_gimple_cfg (gimple_seq seq)
{
  /* Register specific gimple functions.  */
  gimple_register_cfg_hooks ();

  memset ((void *) &cfg_stats, 0, sizeof (cfg_stats));

  init_empty_tree_cfg ();

  found_computed_goto = 0;
  make_blocks (seq);

  /* Computed gotos are hell to deal with, especially if there are
     lots of them with a large number of destinations.  So we factor
     them to a common computed goto location before we build the
     edge list.  After we convert back to normal form, we will un-factor
     the computed gotos since factoring introduces an unwanted jump.  */
  if (found_computed_goto)
    factor_computed_gotos ();

  /* Make sure there is always at least one block, even if it's empty.  */
  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    create_empty_bb (ENTRY_BLOCK_PTR);

  /* Adjust the size of the array.  */
  if (VEC_length (basic_block, basic_block_info) < (size_t) n_basic_blocks)
    VEC_safe_grow_cleared (basic_block, gc, basic_block_info, n_basic_blocks);

  /* To speed up statement iterator walks, we first purge dead labels.  */
  cleanup_dead_labels ();

  /* Group case nodes to reduce the number of edges.
     We do this after cleaning up dead labels because otherwise we miss
     a lot of obvious case merging opportunities.  */
  group_case_labels ();

  /* Create the edges of the flowgraph.  */
  make_edges ();
  cleanup_dead_labels ();

  /* Debugging dumps.  */

  /* Write the flowgraph to a VCG file.  */
  {
    int local_dump_flags;
    FILE *vcg_file = dump_begin (TDI_vcg, &local_dump_flags);
    if (vcg_file)
      {
	tree_cfg2vcg (vcg_file);
	dump_end (TDI_vcg, vcg_file);
      }
  }

#ifdef ENABLE_CHECKING
  verify_stmts ();
#endif

  /* Dump a textual representation of the flowgraph.  */
  if (dump_file)
    gimple_dump_cfg (dump_file, dump_flags);
}

static unsigned int
execute_build_cfg (void)
{
  build_gimple_cfg (gimple_body (current_function_decl));
  set_gimple_body (current_function_decl, NULL);
  return 0;
}

struct tree_opt_pass pass_build_cfg =
{
  "cfg",				/* name */
  NULL,					/* gate */
  execute_build_cfg,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_CFG,				/* tv_id */
  0, /* FIXME tuples PROP_gimple_leh, *//* properties_required */
  PROP_cfg,				/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0, /* FIXME tuples TODO_verify_stmts | TODO_cleanup_cfg,*/	/* todo_flags_finish */
  0					/* letter */
  ,1					/* works_with_tuples_p */
};

/* Search the CFG for any computed gotos.  If found, factor them to a
   common computed goto site.  Also record the location of that site so
   that we can un-factor the gotos after we have converted back to
   normal form.  */

static void
factor_computed_gotos (void)
{
  basic_block bb;
  tree factored_label_decl = NULL;
  tree var = NULL;
  gimple factored_computed_goto_label = NULL;
  gimple factored_computed_goto = NULL;

  /* We know there are one or more computed gotos in this function.
     Examine the last statement in each basic block to see if the block
     ends with a computed goto.  */

  FOR_EACH_BB (bb)
    {
      gimple_stmt_iterator *gsi = gsi_last (bb_seq (bb));
      gimple last;

      if (gsi_end_p (gsi))
	continue;
      last = gsi_stmt (gsi);

      /* Ignore the computed goto we create when we factor the original
	 computed gotos.  */
      if (last == factored_computed_goto)
	continue;

      /* If the last statement is a computed goto, factor it.  */
      if (computed_goto_p (last))
	{
	  gimple assignment;

	  /* The first time we find a computed goto we need to create
	     the factored goto block and the variable each original
	     computed goto will use for their goto destination.  */
	  if (!factored_computed_goto)
	    {
	      basic_block new_bb = create_empty_bb (bb);
	      gimple_stmt_iterator *new_gsi = gsi_start_bb (new_bb);

	      /* Create the destination of the factored goto.  Each original
		 computed goto will put its desired destination into this
		 variable and jump to the label we create immediately
		 below.  */
	      var = create_tmp_var (ptr_type_node, "gotovar");

	      /* Build a label for the new block which will contain the
		 factored computed goto.  */
	      factored_label_decl = create_artificial_label ();
	      factored_computed_goto_label
		= build_gimple_label (factored_label_decl);
	      gsi_insert_after (new_gsi, factored_computed_goto_label,
				GSI_NEW_STMT);

	      /* Build our new computed goto.  */
	      factored_computed_goto = build_gimple_goto (var);
	      gsi_insert_after (new_gsi, factored_computed_goto, GSI_NEW_STMT);
	    }

	  /* Copy the original computed goto's destination into VAR.  */
	  assignment = build_gimple_assign (var, gimple_goto_dest (last));
	  gsi_insert_before (gsi, assignment, GSI_SAME_STMT);

	  /* And re-vector the computed goto to the new destination.  */
	  gimple_goto_set_dest (last, factored_label_decl);
	}
    }
}


/* Build a flowgraph for the sequence of stmts SEQ.  */

static void
make_blocks (gimple_seq seq)
{
  gimple_stmt_iterator *i = gsi_start (seq);
  gimple stmt = NULL;
  bool start_new_block = true;
  bool first_stmt_of_seq = true;
  basic_block bb = ENTRY_BLOCK_PTR;

  while (!gsi_end_p (i))
    {
      gimple prev_stmt;

      prev_stmt = stmt;
      stmt = gsi_stmt (i);

      /* If the statement starts a new basic block or if we have determined
	 in a previous pass that we need to create a new block for STMT, do
	 so now.  */
      if (start_new_block || stmt_starts_bb_p (stmt, prev_stmt))
	{
	  if (!first_stmt_of_seq)
	    seq = gsi_split_seq_before (i);
	  bb = create_basic_block (seq, NULL, bb);
	  start_new_block = false;
	}

      /* Now add STMT to BB and create the subgraphs for special statement
	 codes.  */
      set_gimple_bb (stmt, bb);

      if (computed_goto_p (stmt))
	found_computed_goto = true;

      /* If STMT is a basic block terminator, set START_NEW_BLOCK for the
	 next iteration.  */
      if (stmt_ends_bb_p (stmt))
	start_new_block = true;

      gsi_next (i);
      first_stmt_of_seq = false;
    }
}


/* Create and return a new empty basic block after bb AFTER.  */

static basic_block
create_bb (void *h, void *e, basic_block after)
{
  basic_block bb;

  gcc_assert (!e);

  /* Create and initialize a new basic block.  Since alloc_block uses
     ggc_alloc_cleared to allocate a basic block, we do not have to
     clear the newly allocated basic block here.  */
  bb = alloc_block ();

  bb->index = last_basic_block;
  bb->flags = BB_NEW;
  bb->il.gimple = GGC_CNEW (struct gimple_bb_info);
  set_bb_seq (bb, h ? (gimple_seq) h : gimple_seq_alloc ());

  /* Add the new block to the linked list of blocks.  */
  link_block (bb, after);

  /* Grow the basic block array if needed.  */
  if ((size_t) last_basic_block == VEC_length (basic_block, basic_block_info))
    {
      size_t new_size = last_basic_block + (last_basic_block + 3) / 4;
      VEC_safe_grow_cleared (basic_block, gc, basic_block_info, new_size);
    }

  /* Add the newly created block to the array.  */
  SET_BASIC_BLOCK (last_basic_block, bb);

  n_basic_blocks++;
  last_basic_block++;

  return bb;
}


/*---------------------------------------------------------------------------
				 Edge creation
---------------------------------------------------------------------------*/

/* Fold COND_EXPR_COND of each COND_EXPR.  */

void
fold_cond_expr_cond (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      gimple stmt = last_stmt (bb);

      if (stmt && gimple_code (stmt) == GIMPLE_COND)
	{
	  tree cond;
	  bool zerop, onep;

	  fold_defer_overflow_warnings ();
	  cond = fold_binary (gimple_cond_code (stmt), boolean_type_node,
			      gimple_cond_lhs (stmt), gimple_cond_rhs (stmt));
	  if (cond)
	    {
	      zerop = integer_zerop (cond);
	      onep = integer_onep (cond);
	    }
	  else
	    zerop = onep = false;

	  fold_undefer_overflow_warnings (zerop || onep,
					  stmt,
					  WARN_STRICT_OVERFLOW_CONDITIONAL);
	  if (zerop)
	    gimple_cond_make_false (stmt);
	  else if (onep)
	    gimple_cond_make_true (stmt);
	}
    }
}

/* Join all the blocks in the flowgraph.  */

static void
make_edges (void)
{
  basic_block bb;
  /* FIXME tuples.  */
#if 0
  struct omp_region *cur_region = NULL;
#endif

  /* Create an edge from entry to the first block with executable
     statements in it.  */
  make_edge (ENTRY_BLOCK_PTR, BASIC_BLOCK (NUM_FIXED_BLOCKS), EDGE_FALLTHRU);

  /* Traverse the basic block array placing edges.  */
  FOR_EACH_BB (bb)
    {
      gimple last = last_stmt (bb);
      bool fallthru;

      if (last)
	{
	  enum gimple_code code = gimple_code (last);
	  switch (code)
	    {
	    case GIMPLE_GOTO:
	      make_goto_expr_edges (bb);
	      fallthru = false;
	      break;
	    case GIMPLE_RETURN:
	      make_edge (bb, EXIT_BLOCK_PTR, 0);
	      fallthru = false;
	      break;
	    case GIMPLE_COND:
	      make_cond_expr_edges (bb);
	      fallthru = false;
	      break;
	    case GIMPLE_SWITCH:
	      make_gimple_switch_edges (bb);
	      fallthru = false;
	      break;
	    case GIMPLE_RESX:
	      /* FIXME tuples.  */
#if 0
	      make_eh_edges (last);
#else
	      gcc_unreachable ();
#endif
	      fallthru = false;
	      break;

	      /* FIXME tuples: fix all these cases.  */

	    case GIMPLE_CALL:
	      /* If this function receives a nonlocal goto, then we need to
		 make edges from this call site to all the nonlocal goto
		 handlers.  */
	      if (stmt_can_make_abnormal_goto (last))
		make_abnormal_goto_edges (bb, true);

	      /* If this statement has reachable exception handlers, then
		 create abnormal edges to them.  */
	      make_eh_edges (last);

	      /* Some calls are known not to return.  */
	      fallthru = !(gimple_call_flags (last) & ECF_NORETURN);
	      break;

	    case GIMPLE_ASSIGN:
	      fallthru = true;
	      break;

	    case OMP_PARALLEL:
	    case OMP_FOR:
	    case OMP_SINGLE:
	    case OMP_MASTER:
	    case OMP_ORDERED:
	    case OMP_CRITICAL:
	    case OMP_SECTION:
	      /* FIXME tuples.  */
#if 0
	      cur_region = new_omp_region (bb, code, cur_region);
	      fallthru = true;
#else
	      gcc_unreachable ();
#endif
	      break;

	    case OMP_SECTIONS:
	      /* FIXME tuples.  */
#if 0
	      cur_region = new_omp_region (bb, code, cur_region);
	      fallthru = true;
#else
	      gcc_unreachable ();
#endif
	      break;

	    case OMP_SECTIONS_SWITCH:
	      /* FIXME tuples.  */
#if 0
	      fallthru = false;
#else
	      gcc_unreachable ();
#endif
	      break;

	    case OMP_RETURN:
	      /* FIXME tuples.  */
#if 0
	      /* In the case of an OMP_SECTION, the edge will go somewhere
		 other than the next block.  This will be created later.  */
	      cur_region->exit = bb;
	      fallthru = cur_region->type != OMP_SECTION;
	      cur_region = cur_region->outer;
#else
	      gcc_unreachable ();
#endif
	      break;

	    case OMP_CONTINUE:
	      /* FIXME tuples.  */
#if 0
	      cur_region->cont = bb;
	      switch (cur_region->type)
		{
		case OMP_FOR:
		  /* Make the loopback edge.  */
		  make_edge (bb, single_succ (cur_region->entry), 0);
	      
		  /* Create an edge from OMP_FOR to exit, which corresponds to
		     the case that the body of the loop is not executed at
		     all.  */
		  make_edge (cur_region->entry, bb->next_bb, 0);
		  fallthru = true;
		  break;

		case OMP_SECTIONS:
		  /* Wire up the edges into and out of the nested sections.  */
		  {
		    basic_block switch_bb = single_succ (cur_region->entry);

		    struct omp_region *i;
		    for (i = cur_region->inner; i ; i = i->next)
		      {
			gcc_assert (i->type == OMP_SECTION);
			make_edge (switch_bb, i->entry, 0);
			make_edge (i->exit, bb, EDGE_FALLTHRU);
		      }

		    /* Make the loopback edge to the block with
		       OMP_SECTIONS_SWITCH.  */
		    make_edge (bb, switch_bb, 0);

		    /* Make the edge from the switch to exit.  */
		    make_edge (switch_bb, bb->next_bb, 0);
		    fallthru = false;
		  }
		  break;

		default:
		  gcc_unreachable ();
		}
#else
	      gcc_unreachable ();
#endif
	      break;

	    default:
	      gcc_assert (!stmt_ends_bb_p (last));
	      fallthru = true;
	    }
	}
      else
	fallthru = true;

      if (fallthru)
	make_edge (bb, bb->next_bb, EDGE_FALLTHRU);
    }

  /* FIXME tuples.  */
#if 0
  if (root_omp_region)
    free_omp_regions ();
#endif

  /* Fold COND_EXPR_COND of each COND_EXPR.  */
  fold_cond_expr_cond ();
}


/* Create the edges for a GIMPLE_COND starting at block BB.  */

static void
make_cond_expr_edges (basic_block bb)
{
  gimple entry = last_stmt (bb);
  gimple then_stmt, else_stmt;
  basic_block then_bb, else_bb;
  tree then_label, else_label;
  edge e;

  gcc_assert (entry);
  gcc_assert (gimple_code (entry) == GIMPLE_COND);

  /* Entry basic blocks for each component.  */
  then_label = gimple_cond_true_label (entry);
  else_label = gimple_cond_false_label (entry);
  then_bb = label_to_block (then_label);
  else_bb = label_to_block (else_label);
  then_stmt = first_stmt (then_bb);
  else_stmt = first_stmt (else_bb);

  e = make_edge (bb, then_bb, EDGE_TRUE_VALUE);
  e->goto_locus = gimple_locus (then_stmt);

  e = make_edge (bb, else_bb, EDGE_FALSE_VALUE);
  if (e)
    e->goto_locus = gimple_locus (else_stmt);
}


/* Called for each element in the hash table (P) as we delete the
   edge to cases hash table.

   Clear all the TREE_CHAINs to prevent problems with copying of
   SWITCH_EXPRs and structure sharing rules, then free the hash table
   element.  */

static bool
edge_to_cases_cleanup (const void *key ATTRIBUTE_UNUSED, void **value,
		       void *data ATTRIBUTE_UNUSED)
{
  tree t, next;

  for (t = (tree) *value; t; t = next)
    {
      next = TREE_CHAIN (t);
      TREE_CHAIN (t) = NULL;
    }

  *value = NULL;
  return false;
}

/* Start recording information mapping edges to case labels.  */

void
start_recording_case_labels (void)
{
  gcc_assert (edge_to_cases == NULL);
  edge_to_cases = pointer_map_create ();
}

/* FIXME tuples.  */
#if 0
/* Return nonzero if we are recording information for case labels.  */

static bool
recording_case_labels_p (void)
{
  return (edge_to_cases != NULL);
}
#endif

/* Stop recording information mapping edges to case labels and
   remove any information we have recorded.  */
void
end_recording_case_labels (void)
{
  pointer_map_traverse (edge_to_cases, edge_to_cases_cleanup, NULL);
  pointer_map_destroy (edge_to_cases);
  edge_to_cases = NULL;
}

/* FIXME tuples.  */
#if 0
/* If we are inside a {start,end}_recording_cases block, then return
   a chain of CASE_LABEL_EXPRs from T which reference E.

   Otherwise return NULL.  */

static tree
get_cases_for_edge (edge e, gimple t)
{
  void **slot;
  size_t i, n;

  /* If we are not recording cases, then we do not have CASE_LABEL_EXPR
     chains available.  Return NULL so the caller can detect this case.  */
  if (!recording_case_labels_p ())
    return NULL;

  slot = pointer_map_contains (edge_to_cases, e);
  if (slot)
    return (tree) *slot;

  /* If we did not find E in the hash table, then this must be the first
     time we have been queried for information about E & T.  Add all the
     elements from T to the hash table then perform the query again.  */

  n = gimple_switch_num_labels (t);
  for (i = 0; i < n; i++)
    {
      tree elt = gimple_switch_label (t, i);
      tree lab = CASE_LABEL (elt);
      basic_block label_bb = label_to_block (lab);
      edge this_edge = find_edge (e->src, label_bb);

      /* Add it to the chain of CASE_LABEL_EXPRs referencing E, or create
	 a new chain.  */
      slot = pointer_map_insert (edge_to_cases, this_edge);
      TREE_CHAIN (elt) = (tree) *slot;
      *slot = elt;
    }

  return (tree) *pointer_map_contains (edge_to_cases, e);
}
#endif

/* Create the edges for a GIMPLE_SWITCH starting at block BB.  */

static void
make_gimple_switch_edges (basic_block bb)
{
  gimple entry = last_stmt (bb);
  size_t i, n;

  n = gimple_switch_num_labels (entry);

  for (i = 0; i < n; ++i)
    {
      tree lab = CASE_LABEL (gimple_switch_label (entry, i));
      basic_block label_bb = label_to_block (lab);
      make_edge (bb, label_bb, 0);
    }
}


/* Return the basic block holding label DEST.  */

basic_block
label_to_block_fn (struct function *ifun, tree dest)
{
  int uid = LABEL_DECL_UID (dest);

  /* We would die hard when faced by an undefined label.  Emit a label to
     the very first basic block.  This will hopefully make even the dataflow
     and undefined variable warnings quite right.  */
  if ((errorcount || sorrycount) && uid < 0)
    {
      gimple_stmt_iterator *gsi = gsi_start_bb (BASIC_BLOCK (NUM_FIXED_BLOCKS));
      gimple stmt;

      stmt = build_gimple_label (dest);
      gsi_insert_before (gsi, stmt, GSI_NEW_STMT);
      uid = LABEL_DECL_UID (dest);
    }
  if (VEC_length (basic_block, ifun->cfg->x_label_to_block_map)
      <= (unsigned int) uid)
    return NULL;
  return VEC_index (basic_block, ifun->cfg->x_label_to_block_map, uid);
}

/* Create edges for an abnormal goto statement at block BB.  If FOR_CALL
   is true, the source statement is a CALL_EXPR instead of a GOTO_EXPR.  */

void
make_abnormal_goto_edges (basic_block bb, bool for_call)
{
  basic_block target_bb;
  gimple_stmt_iterator *gsi;

  FOR_EACH_BB (target_bb)
    for (gsi = gsi_start_bb (target_bb); !gsi_end_p (gsi); gsi_next (gsi))
      {
	gimple label_stmt = gsi_stmt (gsi);
	tree target;

	if (gimple_code (label_stmt) != GIMPLE_LABEL)
	  break;

	target = gimple_label_label (label_stmt);

	/* Make an edge to every label block that has been marked as a
	   potential target for a computed goto or a non-local goto.  */
	if ((FORCED_LABEL (target) && !for_call)
	    || (DECL_NONLOCAL (target) && for_call))
	  {
	    make_edge (bb, target_bb, EDGE_ABNORMAL);
	    break;
	  }
      }
}

/* Create edges for a goto statement at block BB.  */

static void
make_goto_expr_edges (basic_block bb)
{
  gimple_stmt_iterator *last = gsi_last (bb_seq (bb));
  gimple goto_t = gsi_stmt (last);

  /* A simple GOTO creates normal edges.  */
  if (simple_goto_p (goto_t))
    {
      tree dest = gimple_goto_dest (goto_t);
      edge e = make_edge (bb, label_to_block (dest), EDGE_FALLTHRU);
      e->goto_locus = gimple_locus (goto_t);
      gsi_remove (last, true);
      return;
    }

  /* A computed GOTO creates abnormal edges.  */
  make_abnormal_goto_edges (bb, false);
}


/*---------------------------------------------------------------------------
			       Flowgraph analysis
---------------------------------------------------------------------------*/

/* Cleanup useless labels in basic blocks.  This is something we wish
   to do early because it allows us to group case labels before creating
   the edges for the CFG, and it speeds up block statement iterators in
   all passes later on.
   We rerun this pass after CFG is created, to get rid of the labels that
   are no longer referenced.  After then we do not run it any more, since
   (almost) no new labels should be created.  */

/* A map from basic block index to the leading label of that block.  */
static struct label_record
{
  /* The label.  */
  tree label;

  /* True if the label is referenced from somewhere.  */
  bool used;
} *label_for_bb;

/* Callback for for_each_eh_region.  Helper for cleanup_dead_labels.  */
static void
update_eh_label (struct eh_region *region)
{
  tree old_label = get_eh_region_tree_label (region);
  if (old_label)
    {
      tree new_label;
      basic_block bb = label_to_block (old_label);

      /* ??? After optimizing, there may be EH regions with labels
	 that have already been removed from the function body, so
	 there is no basic block for them.  */
      if (! bb)
	return;

      new_label = label_for_bb[bb->index].label;
      label_for_bb[bb->index].used = true;
      set_eh_region_tree_label (region, new_label);
    }
}


/* Given LABEL return the first label in the same basic block.  */

static tree
main_block_label (tree label)
{
  basic_block bb = label_to_block (label);
  tree main_label = label_for_bb[bb->index].label;

  /* label_to_block possibly inserted undefined label into the chain.  */
  if (!main_label)
    {
      label_for_bb[bb->index].label = label;
      main_label = label;
    }

  label_for_bb[bb->index].used = true;
  return main_label;
}

/* Cleanup redundant labels.  This is a three-step process:
     1) Find the leading label for each block.
     2) Redirect all references to labels to the leading labels.
     3) Cleanup all useless labels.  */

void
cleanup_dead_labels (void)
{
  basic_block bb;
  label_for_bb = XCNEWVEC (struct label_record, last_basic_block);

  /* Find a suitable label for each block.  We use the first user-defined
     label if there is one, or otherwise just the first label we see.  */
  FOR_EACH_BB (bb)
    {
      gimple_stmt_iterator *i;

      for (i = gsi_start_bb (bb); !gsi_end_p (i); gsi_next (i))
	{
	  tree label;
	  gimple stmt = gsi_stmt (i);

	  if (gimple_code (stmt) != GIMPLE_LABEL)
	    break;

	  label = gimple_label_label (stmt);

	  /* If we have not yet seen a label for the current block,
	     remember this one and see if there are more labels.  */
	  if (!label_for_bb[bb->index].label)
	    {
	      label_for_bb[bb->index].label = label;
	      continue;
	    }

	  /* If we did see a label for the current block already, but it
	     is an artificially created label, replace it if the current
	     label is a user defined label.  */
	  if (!DECL_ARTIFICIAL (label)
	      && DECL_ARTIFICIAL (label_for_bb[bb->index].label))
	    {
	      label_for_bb[bb->index].label = label;
	      break;
	    }
	}
    }

  /* Now redirect all jumps/branches to the selected label.
     First do so for each block ending in a control statement.  */
  FOR_EACH_BB (bb)
    {
      gimple stmt = last_stmt (bb);
      if (!stmt)
	continue;

      switch (gimple_code (stmt))
	{
	case GIMPLE_COND:
	  {
	    tree new_true_label, new_false_label;

	    new_true_label = main_block_label (gimple_cond_true_label (stmt));
	    new_false_label = main_block_label (gimple_cond_false_label (stmt));

	    if (new_true_label)
	      gimple_cond_set_true_label (stmt, new_true_label);

	    if (new_false_label)
	      gimple_cond_set_false_label (stmt, new_false_label);
	    break;
	  }

	case GIMPLE_SWITCH:
	  {
	    size_t i, n = gimple_switch_num_labels (stmt);

	    /* Replace all destination labels.  */
	    for (i = 0; i < n; ++i)
	      {
		tree case_label = gimple_switch_label (stmt, i);
		tree label = main_block_label (CASE_LABEL (case_label));
		CASE_LABEL (case_label) = label;
	      }
	    break;
	  }

	/* We have to handle gotos until they're removed, and we don't
	   remove them until after we've created the CFG edges.  */
	case GIMPLE_GOTO:
          if (!computed_goto_p (stmt))
	    {
	      tree new_dest = main_block_label (gimple_goto_dest (stmt));
	      gimple_goto_set_dest (stmt, new_dest);
	      break;
	    }

	default:
	  break;
      }
    }

  for_each_eh_region (update_eh_label);

  /* Finally, purge dead labels.  All user-defined labels and labels that
     can be the target of non-local gotos and labels which have their
     address taken are preserved.  */
  FOR_EACH_BB (bb)
    {
      gimple_stmt_iterator *i;
      tree label_for_this_bb = label_for_bb[bb->index].label;

      if (!label_for_this_bb)
	continue;

      /* If the main label of the block is unused, we may still remove it.  */
      if (!label_for_bb[bb->index].used)
	label_for_this_bb = NULL;

      for (i = gsi_start_bb (bb); !gsi_end_p (i); )
	{
	  tree label;
	  gimple stmt = gsi_stmt (i);

	  if (gimple_code (stmt) != GIMPLE_LABEL)
	    break;

	  label = gimple_label_label (stmt);

	  if (label == label_for_this_bb
	      || !DECL_ARTIFICIAL (label)
	      || DECL_NONLOCAL (label)
	      || FORCED_LABEL (label))
	    gsi_next (i);
	  else
	    gsi_remove (i, true);
	}
    }

  free (label_for_bb);
}

/* Look for blocks ending in a multiway branch (a SWITCH_EXPR in GIMPLE),
   and scan the sorted vector of cases.  Combine the ones jumping to the
   same label.
   Eg. three separate entries 1: 2: 3: become one entry 1..3:  */

void
group_case_labels (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      gimple stmt = last_stmt (bb);
      if (stmt && gimple_code (stmt) == GIMPLE_SWITCH)
	{
	  int old_size = gimple_switch_num_labels (stmt);
	  int i, j, new_size = old_size;
	  tree default_case;
	  tree default_label;

	  /* The default label is always the first case in a switch
	     statement after gimplification.  */
	  default_case = gimple_switch_default_label (stmt);
	  default_label = CASE_LABEL (default_case);

	  /* Look for possible opportunities to merge cases.
	     Ignore the first element of the label vector because it
	     is be the default case.  */
          i = 1;
	  while (i < old_size)
	    {
	      tree base_case, base_label, base_high;
	      base_case = gimple_switch_label (stmt, i);

	      gcc_assert (base_case);
	      base_label = CASE_LABEL (base_case);

	      /* Discard cases that have the same destination as the
		 default case.  */
	      if (base_label == default_label)
		{
		  gimple_switch_set_label (stmt, i, NULL_TREE);
		  i++;
		  new_size--;
		  continue;
		}

	      base_high = CASE_HIGH (base_case) ?
		CASE_HIGH (base_case) : CASE_LOW (base_case);
	      i++;
	      /* Try to merge case labels.  Break out when we reach the end
		 of the label vector or when we cannot merge the next case
		 label with the current one.  */
	      while (i < old_size)
		{
		  tree merge_case = gimple_switch_label (stmt, i);
	          tree merge_label = CASE_LABEL (merge_case);
		  tree t = int_const_binop (PLUS_EXPR, base_high,
					    integer_one_node, 1);

		  /* Merge the cases if they jump to the same place,
		     and their ranges are consecutive.  */
		  if (merge_label == base_label
		      && tree_int_cst_equal (CASE_LOW (merge_case), t))
		    {
		      base_high = CASE_HIGH (merge_case) ?
			CASE_HIGH (merge_case) : CASE_LOW (merge_case);
		      CASE_HIGH (base_case) = base_high;
		      gimple_switch_set_label (stmt, i, NULL_TREE);
		      new_size--;
		      i++;
		    }
		  else
		    break;
		}
	    }

	  /* Compress the case labels in the label vector, and adjust the
	     length of the vector.  */
	  for (i = 0, j = 0; i < new_size; i++)
	    {
	      while (! gimple_switch_label (stmt, j))
		j++;
	      gimple_switch_set_label (stmt, i,
				       gimple_switch_label (stmt, j++));
	    }
	  /* FIXME tuples: Should we reallocate a new switch, or just
	     set the number of labels and put up with the few words
	     lost, until we GC the entire GIMPLE_SWITCH out?  */
	  gimple_switch_set_num_labels (stmt, new_size);
	}
    }
}

/* Checks whether we can merge block B into block A.  */

static bool
tree_can_merge_blocks_p (basic_block a, basic_block b)
{
  gimple stmt;
  gimple_stmt_iterator *gsi;
  gimple_seq phis;

  if (!single_succ_p (a))
    return false;

  if (single_succ_edge (a)->flags & EDGE_ABNORMAL)
    return false;

  if (single_succ (a) != b)
    return false;

  if (!single_pred_p (b))
    return false;

  if (b == EXIT_BLOCK_PTR)
    return false;

  /* If A ends by a statement causing exceptions or something similar, we
     cannot merge the blocks.  */
  stmt = last_stmt (a);
  if (stmt && stmt_ends_bb_p (stmt))
    return false;

  /* Do not allow a block with only a non-local label to be merged.  */
  if (stmt
      && gimple_code (stmt) == GIMPLE_LABEL
      && DECL_NONLOCAL (gimple_label_label (stmt)))
    return false;

  /* It must be possible to eliminate all phi nodes in B.  If ssa form
     is not up-to-date, we cannot eliminate any phis; however, if only
     some symbols as whole are marked for renaming, this is not a problem,
     as phi nodes for those symbols are irrelevant in updating anyway.  */
  phis = phi_nodes (b);
  if (!gimple_seq_empty_p (phis))
    {
      gimple_stmt_iterator *i;

      if (name_mappings_registered_p ())
	return false;

      for (i = gsi_start (phis); gsi_end_p (i); gsi_next (i))
	{
	  gimple phi = gsi_stmt (i);

	  if (!is_gimple_reg (gimple_phi_result (phi))
	      && !may_propagate_copy (gimple_phi_result (phi),
				      gimple_phi_arg_def (phi, 0)))
	    return false;
	}
    }

  /* Do not remove user labels.  */
  for (gsi = gsi_start_bb (b); !gsi_end_p (gsi); gsi_next (gsi))
    {
      stmt = gsi_stmt (gsi);
      if (gimple_code (stmt) != GIMPLE_LABEL)
	break;
      if (!DECL_ARTIFICIAL (gimple_label_label (stmt)))
	return false;
    }

  /* Protect the loop latches.  */
  if (current_loops
      && b->loop_father->latch == b)
    return false;

  return true;
}

/* Replaces all uses of NAME by VAL.  */

void
replace_uses_by (tree name, tree val)
{
  imm_use_iterator imm_iter;
  use_operand_p use;
  gimple stmt;
  edge e;

  FOR_EACH_IMM_USE_STMT (stmt, imm_iter, name)
    {
      if (gimple_code (stmt) != GIMPLE_PHI)
	push_stmt_changes (&stmt);

      FOR_EACH_IMM_USE_ON_STMT (use, imm_iter)
        {
	  replace_exp (use, val);

	  if (gimple_code (stmt) == GIMPLE_PHI)
	    {
	      e = gimple_phi_arg_edge (stmt, PHI_ARG_INDEX_FROM_USE (use));
	      if (e->flags & EDGE_ABNORMAL)
		{
		  /* This can only occur for virtual operands, since
		     for the real ones SSA_NAME_OCCURS_IN_ABNORMAL_PHI (name))
		     would prevent replacement.  */
		  gcc_assert (!is_gimple_reg (name));
		  SSA_NAME_OCCURS_IN_ABNORMAL_PHI (val) = 1;
		}
	    }
	}

      if (gimple_code (stmt) != GIMPLE_PHI)
	{
	  size_t i;

	  fold_stmt_inplace (stmt);
	  if (cfgcleanup_altered_bbs)
	    bitmap_set_bit (cfgcleanup_altered_bbs, gimple_bb (stmt)->index);

	  /* FIXME.  This should go in pop_stmt_changes.  */
	  for (i = 0; i < gimple_num_ops (stmt); i++)
	    {
	      tree op = gimple_op (stmt, i);
	      if (TREE_CODE (op) == ADDR_EXPR)
		recompute_tree_invariant_for_addr_expr (op);
	    }

	  maybe_clean_or_replace_eh_stmt (stmt, stmt);

	  pop_stmt_changes (&stmt);
	}
    }

  gcc_assert (has_zero_uses (name));

  /* Also update the trees stored in loop structures.  */
  if (current_loops)
    {
      struct loop *loop;
      loop_iterator li;

      FOR_EACH_LOOP (li, loop, 0)
	{
	  substitute_in_loop_info (loop, name, val);
	}
    }
}

/* Merge block B into block A.  */

static void
tree_merge_blocks (basic_block a, basic_block b)
{
  gimple_stmt_iterator *last, *gsi;
  gimple_seq phis = phi_nodes (b);

  if (dump_file)
    fprintf (dump_file, "Merging blocks %d and %d\n", a->index, b->index);

  /* Remove all single-valued PHI nodes from block B of the form
     V_i = PHI <V_j> by propagating V_j to all the uses of V_i.  */
  gsi = gsi_last (bb_seq (a));
  for (gsi = gsi_start (phis); !gsi_end_p (gsi); gsi_next (gsi))
    {
      gimple phi = gsi_stmt (gsi);
      tree def = gimple_phi_result (phi), use = gimple_phi_arg_def (phi, 0);
      gimple copy;
      bool may_replace_uses = may_propagate_copy (def, use);

      /* In case we maintain loop closed ssa form, do not propagate arguments
	 of loop exit phi nodes.  */
      if (current_loops
	  && loops_state_satisfies_p (LOOP_CLOSED_SSA)
	  && is_gimple_reg (def)
	  && TREE_CODE (use) == SSA_NAME
	  && a->loop_father != b->loop_father)
	may_replace_uses = false;

      if (!may_replace_uses)
	{
	  gcc_assert (is_gimple_reg (def));

	  /* Note that just emitting the copies is fine -- there is no problem
	     with ordering of phi nodes.  This is because A is the single
	     predecessor of B, therefore results of the phi nodes cannot
	     appear as arguments of the phi nodes.  */
	  copy = build_gimple_assign (def, use);
	  gsi_insert_after (gsi, copy, GSI_NEW_STMT);
	  SSA_NAME_DEF_STMT (def) = copy;
          remove_phi_node (phi, false);
	}
      else
        {
          replace_uses_by (def, use);
          remove_phi_node (phi, true);
        }
    }

  /* Ensure that B follows A.  */
  move_block_after (b, a);

  gcc_assert (single_succ_edge (a)->flags & EDGE_FALLTHRU);
  gcc_assert (!last_stmt (a) || !stmt_ends_bb_p (last_stmt (a)));

  /* Remove labels from B and set gimple_bb to A for other statements.  */
  for (gsi = gsi_start_bb (b); !gsi_end_p (gsi);)
    {
      if (gimple_code (gsi_stmt (gsi)) == GIMPLE_LABEL)
	{
	  gimple label = gsi_stmt (gsi);

	  gsi_remove (gsi, false);

	  /* Now that we can thread computed gotos, we might have
	     a situation where we have a forced label in block B
	     However, the label at the start of block B might still be
	     used in other ways (think about the runtime checking for
	     Fortran assigned gotos).  So we can not just delete the
	     label.  Instead we move the label to the start of block A.  */
	  if (FORCED_LABEL (gimple_label_label (label)))
	    {
	      gimple_stmt_iterator *dest_gsi = gsi_start_bb (a);
	      gsi_insert_before (dest_gsi, label, GSI_NEW_STMT);
	    }
	}
      else
	{
	  change_bb_for_stmt (gsi_stmt (gsi), a);
	  gsi_next (gsi);
	}
    }

  /* Merge the chains.  */
  last = gsi_last (bb_seq (a));
  gsi_link_after (last, gimple_seq_first (bb_seq (b)), GSI_NEW_STMT);
  set_bb_seq (b, NULL);

  if (cfgcleanup_altered_bbs)
    bitmap_set_bit (cfgcleanup_altered_bbs, a->index);
}


/* Return the one of two successors of BB that is not reachable by a
   reached by a complex edge, if there is one.  Else, return BB.  We use
   this in optimizations that use post-dominators for their heuristics,
   to catch the cases in C++ where function calls are involved.  */

basic_block
single_noncomplex_succ (basic_block bb)
{
  edge e0, e1;
  if (EDGE_COUNT (bb->succs) != 2)
    return bb;

  e0 = EDGE_SUCC (bb, 0);
  e1 = EDGE_SUCC (bb, 1);
  if (e0->flags & EDGE_COMPLEX)
    return e1->dest;
  if (e1->flags & EDGE_COMPLEX)
    return e0->dest;

  return bb;
}


/* Walk the function tree removing unnecessary statements.

     * Empty statement nodes are removed

     * Unnecessary TRY_FINALLY and TRY_CATCH blocks are removed

     * Unnecessary COND_EXPRs are removed

     * Some unnecessary BIND_EXPRs are removed

   Clearly more work could be done.  The trick is doing the analysis
   and removal fast enough to be a net improvement in compile times.

   Note that when we remove a control structure such as a COND_EXPR
   BIND_EXPR, or TRY block, we will need to repeat this optimization pass
   to ensure we eliminate all the useless code.  */

struct rus_data
{
  tree *last_goto;
  bool repeat;
  bool may_throw;
  bool may_branch;
  bool has_label;
};

/* FIXME tuples.  */
#if 0
static void remove_useless_stmts_1 (tree *, struct rus_data *);
#endif

static bool
remove_useless_stmts_warn_notreached (tree stmt)
{
  if (EXPR_HAS_LOCATION (stmt))
    {
      location_t loc = EXPR_LOCATION (stmt);
      if (LOCATION_LINE (loc) > 0)
	{
	  warning (0, "%Hwill never be executed", &loc);
	  return true;
	}
    }

  switch (TREE_CODE (stmt))
    {
    case STATEMENT_LIST:
      {
	tree_stmt_iterator i;
	for (i = tsi_start (stmt); !tsi_end_p (i); tsi_next (&i))
	  if (remove_useless_stmts_warn_notreached (tsi_stmt (i)))
	    return true;
      }
      break;

    case COND_EXPR:
      if (remove_useless_stmts_warn_notreached (COND_EXPR_COND (stmt)))
	return true;
      if (remove_useless_stmts_warn_notreached (COND_EXPR_THEN (stmt)))
	return true;
      if (remove_useless_stmts_warn_notreached (COND_EXPR_ELSE (stmt)))
	return true;
      break;

    case TRY_FINALLY_EXPR:
    case TRY_CATCH_EXPR:
      if (remove_useless_stmts_warn_notreached (TREE_OPERAND (stmt, 0)))
	return true;
      if (remove_useless_stmts_warn_notreached (TREE_OPERAND (stmt, 1)))
	return true;
      break;

    case CATCH_EXPR:
      return remove_useless_stmts_warn_notreached (CATCH_BODY (stmt));
    case EH_FILTER_EXPR:
      return remove_useless_stmts_warn_notreached (EH_FILTER_FAILURE (stmt));
    case BIND_EXPR:
      return remove_useless_stmts_warn_notreached (BIND_EXPR_BLOCK (stmt));

    default:
      /* Not a live container.  */
      break;
    }

  return false;
}

/* FIXME tuples.  */
#if 0
static void
remove_useless_stmts_cond (tree *stmt_p, struct rus_data *data)
{
  tree then_clause, else_clause, cond;
  bool save_has_label, then_has_label, else_has_label;

  save_has_label = data->has_label;
  data->has_label = false;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&COND_EXPR_THEN (*stmt_p), data);

  then_has_label = data->has_label;
  data->has_label = false;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&COND_EXPR_ELSE (*stmt_p), data);

  else_has_label = data->has_label;
  data->has_label = save_has_label | then_has_label | else_has_label;

  then_clause = COND_EXPR_THEN (*stmt_p);
  else_clause = COND_EXPR_ELSE (*stmt_p);
  cond = fold (COND_EXPR_COND (*stmt_p));

  /* If neither arm does anything at all, we can remove the whole IF.  */
  if (!TREE_SIDE_EFFECTS (then_clause) && !TREE_SIDE_EFFECTS (else_clause))
    {
      *stmt_p = build_empty_stmt ();
      data->repeat = true;
    }

  /* If there are no reachable statements in an arm, then we can
     zap the entire conditional.  */
  else if (integer_nonzerop (cond) && !else_has_label)
    {
      if (warn_notreached)
	remove_useless_stmts_warn_notreached (else_clause);
      *stmt_p = then_clause;
      data->repeat = true;
    }
  else if (integer_zerop (cond) && !then_has_label)
    {
      if (warn_notreached)
	remove_useless_stmts_warn_notreached (then_clause);
      *stmt_p = else_clause;
      data->repeat = true;
    }

  /* Check a couple of simple things on then/else with single stmts.  */
  else
    {
      tree then_stmt = expr_only (then_clause);
      tree else_stmt = expr_only (else_clause);

      /* Notice branches to a common destination.  */
      if (then_stmt && else_stmt
	  && TREE_CODE (then_stmt) == GOTO_EXPR
	  && TREE_CODE (else_stmt) == GOTO_EXPR
	  && (GOTO_DESTINATION (then_stmt) == GOTO_DESTINATION (else_stmt)))
	{
	  *stmt_p = then_stmt;
	  data->repeat = true;
	}

      /* If the THEN/ELSE clause merely assigns a value to a variable or
	 parameter which is already known to contain that value, then
	 remove the useless THEN/ELSE clause.  */
      else if (TREE_CODE (cond) == VAR_DECL || TREE_CODE (cond) == PARM_DECL)
	{
	  if (else_stmt
	      && TREE_CODE (else_stmt) == GIMPLE_MODIFY_STMT
	      && GIMPLE_STMT_OPERAND (else_stmt, 0) == cond
	      && integer_zerop (GIMPLE_STMT_OPERAND (else_stmt, 1)))
	    COND_EXPR_ELSE (*stmt_p) = alloc_stmt_list ();
	}
      else if ((TREE_CODE (cond) == EQ_EXPR || TREE_CODE (cond) == NE_EXPR)
	       && (TREE_CODE (TREE_OPERAND (cond, 0)) == VAR_DECL
		   || TREE_CODE (TREE_OPERAND (cond, 0)) == PARM_DECL)
	       && TREE_CONSTANT (TREE_OPERAND (cond, 1)))
	{
	  tree stmt = (TREE_CODE (cond) == EQ_EXPR
		       ? then_stmt : else_stmt);
	  tree *location = (TREE_CODE (cond) == EQ_EXPR
			    ? &COND_EXPR_THEN (*stmt_p)
			    : &COND_EXPR_ELSE (*stmt_p));

	  if (stmt
	      && TREE_CODE (stmt) == GIMPLE_MODIFY_STMT
	      && GIMPLE_STMT_OPERAND (stmt, 0) == TREE_OPERAND (cond, 0)
	      && GIMPLE_STMT_OPERAND (stmt, 1) == TREE_OPERAND (cond, 1))
	    *location = alloc_stmt_list ();
	}
    }

  /* Protect GOTOs in the arm of COND_EXPRs from being removed.  They
     would be re-introduced during lowering.  */
  data->last_goto = NULL;
}


static void
remove_useless_stmts_tf (tree *stmt_p, struct rus_data *data)
{
  bool save_may_branch, save_may_throw;
  bool this_may_branch, this_may_throw;

  /* Collect may_branch and may_throw information for the body only.  */
  save_may_branch = data->may_branch;
  save_may_throw = data->may_throw;
  data->may_branch = false;
  data->may_throw = false;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&TREE_OPERAND (*stmt_p, 0), data);

  this_may_branch = data->may_branch;
  this_may_throw = data->may_throw;
  data->may_branch |= save_may_branch;
  data->may_throw |= save_may_throw;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&TREE_OPERAND (*stmt_p, 1), data);

  /* If the body is empty, then we can emit the FINALLY block without
     the enclosing TRY_FINALLY_EXPR.  */
  if (!TREE_SIDE_EFFECTS (TREE_OPERAND (*stmt_p, 0)))
    {
      *stmt_p = TREE_OPERAND (*stmt_p, 1);
      data->repeat = true;
    }

  /* If the handler is empty, then we can emit the TRY block without
     the enclosing TRY_FINALLY_EXPR.  */
  else if (!TREE_SIDE_EFFECTS (TREE_OPERAND (*stmt_p, 1)))
    {
      *stmt_p = TREE_OPERAND (*stmt_p, 0);
      data->repeat = true;
    }

  /* If the body neither throws, nor branches, then we can safely
     string the TRY and FINALLY blocks together.  */
  else if (!this_may_branch && !this_may_throw)
    {
      tree stmt = *stmt_p;
      *stmt_p = TREE_OPERAND (stmt, 0);
      append_to_statement_list (TREE_OPERAND (stmt, 1), stmt_p);
      data->repeat = true;
    }
}


static void
remove_useless_stmts_tc (tree *stmt_p, struct rus_data *data)
{
  bool save_may_throw, this_may_throw;
  tree_stmt_iterator i;
  tree stmt;

  /* Collect may_throw information for the body only.  */
  save_may_throw = data->may_throw;
  data->may_throw = false;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&TREE_OPERAND (*stmt_p, 0), data);

  this_may_throw = data->may_throw;
  data->may_throw = save_may_throw;

  /* If the body cannot throw, then we can drop the entire TRY_CATCH_EXPR.  */
  if (!this_may_throw)
    {
      if (warn_notreached)
	remove_useless_stmts_warn_notreached (TREE_OPERAND (*stmt_p, 1));
      *stmt_p = TREE_OPERAND (*stmt_p, 0);
      data->repeat = true;
      return;
    }

  /* Process the catch clause specially.  We may be able to tell that
     no exceptions propagate past this point.  */

  this_may_throw = true;
  i = tsi_start (TREE_OPERAND (*stmt_p, 1));
  stmt = tsi_stmt (i);
  data->last_goto = NULL;

  switch (TREE_CODE (stmt))
    {
    case CATCH_EXPR:
      for (; !tsi_end_p (i); tsi_next (&i))
	{
	  stmt = tsi_stmt (i);
	  /* If we catch all exceptions, then the body does not
	     propagate exceptions past this point.  */
	  if (CATCH_TYPES (stmt) == NULL)
	    this_may_throw = false;
	  data->last_goto = NULL;
	  remove_useless_stmts_1 (&CATCH_BODY (stmt), data);
	}
      break;

    case EH_FILTER_EXPR:
      if (EH_FILTER_MUST_NOT_THROW (stmt))
	this_may_throw = false;
      else if (EH_FILTER_TYPES (stmt) == NULL)
	this_may_throw = false;
      remove_useless_stmts_1 (&EH_FILTER_FAILURE (stmt), data);
      break;

    default:
      /* Otherwise this is a cleanup.  */
      remove_useless_stmts_1 (&TREE_OPERAND (*stmt_p, 1), data);

      /* If the cleanup is empty, then we can emit the TRY block without
	 the enclosing TRY_CATCH_EXPR.  */
      if (!TREE_SIDE_EFFECTS (TREE_OPERAND (*stmt_p, 1)))
	{
	  *stmt_p = TREE_OPERAND (*stmt_p, 0);
	  data->repeat = true;
	}
      break;
    }
  data->may_throw |= this_may_throw;
}


static void
remove_useless_stmts_bind (tree *stmt_p, struct rus_data *data)
{
  tree block;

  /* First remove anything underneath the BIND_EXPR.  */
  remove_useless_stmts_1 (&BIND_EXPR_BODY (*stmt_p), data);

  /* If the BIND_EXPR has no variables, then we can pull everything
     up one level and remove the BIND_EXPR, unless this is the toplevel
     BIND_EXPR for the current function or an inlined function.

     When this situation occurs we will want to apply this
     optimization again.  */
  block = BIND_EXPR_BLOCK (*stmt_p);
  if (BIND_EXPR_VARS (*stmt_p) == NULL_TREE
      && *stmt_p != DECL_SAVED_TREE (current_function_decl)
      && (! block
	  || ! BLOCK_ABSTRACT_ORIGIN (block)
	  || (TREE_CODE (BLOCK_ABSTRACT_ORIGIN (block))
	      != FUNCTION_DECL)))
    {
      *stmt_p = BIND_EXPR_BODY (*stmt_p);
      data->repeat = true;
    }
}


static void
remove_useless_stmts_goto (tree *stmt_p, struct rus_data *data)
{
  tree dest = GOTO_DESTINATION (*stmt_p);

  data->may_branch = true;
  data->last_goto = NULL;

  /* Record the last goto expr, so that we can delete it if unnecessary.  */
  if (TREE_CODE (dest) == LABEL_DECL)
    data->last_goto = stmt_p;
}


static void
remove_useless_stmts_label (tree *stmt_p, struct rus_data *data)
{
  tree label = LABEL_EXPR_LABEL (*stmt_p);

  data->has_label = true;

  /* We do want to jump across non-local label receiver code.  */
  if (DECL_NONLOCAL (label))
    data->last_goto = NULL;

  else if (data->last_goto && GOTO_DESTINATION (*data->last_goto) == label)
    {
      *data->last_goto = build_empty_stmt ();
      data->repeat = true;
    }

  /* ??? Add something here to delete unused labels.  */
}


/* If the function is "const" or "pure", then clear TREE_SIDE_EFFECTS on its
   decl.  This allows us to eliminate redundant or useless
   calls to "const" functions.

   Gimplifier already does the same operation, but we may notice functions
   being const and pure once their calls has been gimplified, so we need
   to update the flag.  */

static void
update_call_expr_flags (tree call)
{
  tree decl = get_callee_fndecl (call);
  if (!decl)
    return;
  if (call_expr_flags (call) & (ECF_CONST | ECF_PURE))
    TREE_SIDE_EFFECTS (call) = 0;
  if (TREE_NOTHROW (decl))
    TREE_NOTHROW (call) = 1;
}
#endif


/* T is CALL_EXPR.  Set current_function_calls_* flags.  */

void
notice_special_calls (tree t)
{
  int flags = call_expr_flags (t);

  if (flags & ECF_MAY_BE_ALLOCA)
    current_function_calls_alloca = true;
  if (flags & ECF_RETURNS_TWICE)
    current_function_calls_setjmp = true;
}


/* Clear flags set by notice_special_calls.  Used by dead code removal
   to update the flags.  */

void
clear_special_calls (void)
{
  current_function_calls_alloca = false;
  current_function_calls_setjmp = false;
}


/* FIXME tuples.  */
#if 0
static void
remove_useless_stmts_1 (tree *tp, struct rus_data *data)
{
  tree t = *tp, op;

  switch (TREE_CODE (t))
    {
    case COND_EXPR:
      remove_useless_stmts_cond (tp, data);
      break;

    case TRY_FINALLY_EXPR:
      remove_useless_stmts_tf (tp, data);
      break;

    case TRY_CATCH_EXPR:
      remove_useless_stmts_tc (tp, data);
      break;

    case BIND_EXPR:
      remove_useless_stmts_bind (tp, data);
      break;

    case GOTO_EXPR:
      remove_useless_stmts_goto (tp, data);
      break;

    case LABEL_EXPR:
      remove_useless_stmts_label (tp, data);
      break;

    case RETURN_EXPR:
      fold_stmt (tp);
      data->last_goto = NULL;
      data->may_branch = true;
      break;

    case CALL_EXPR:
      fold_stmt (tp);
      data->last_goto = NULL;
      notice_special_calls (t);
      update_call_expr_flags (t);
      if (stmt_could_throw_p (t))
	data->may_throw = true;
      break;

    case MODIFY_EXPR:
      gcc_unreachable ();

    case GIMPLE_MODIFY_STMT:
      data->last_goto = NULL;
      fold_stmt (tp);
      op = get_call_expr_in (t);
      if (op)
	{
	  update_call_expr_flags (op);
	  notice_special_calls (op);
	}
      if (stmt_could_throw_p (t))
	data->may_throw = true;
      break;

    case STATEMENT_LIST:
      {
	tree_stmt_iterator i = tsi_start (t);
	while (!tsi_end_p (i))
	  {
	    t = tsi_stmt (i);
	    if (IS_EMPTY_STMT (t))
	      {
		tsi_delink (&i);
		continue;
	      }

	    remove_useless_stmts_1 (tsi_stmt_ptr (i), data);

	    t = tsi_stmt (i);
	    if (TREE_CODE (t) == STATEMENT_LIST)
	      {
		tsi_link_before (&i, t, TSI_SAME_STMT);
		tsi_delink (&i);
	      }
	    else
	      tsi_next (&i);
	  }
      }
      break;
    case ASM_EXPR:
      fold_stmt (tp);
      data->last_goto = NULL;
      break;

    default:
      data->last_goto = NULL;
      break;
    }
}
#endif

static unsigned int
remove_useless_stmts (void)
{
  /* FIXME tuples.  */
#if 0
  struct rus_data data;

  clear_special_calls ();

  do
    {
      memset (&data, 0, sizeof (data));
      remove_useless_stmts_1 (&DECL_SAVED_TREE (current_function_decl), &data);
    }
  while (data.repeat);
  return 0;
#else
  gcc_unreachable ();
#endif
}


struct tree_opt_pass pass_remove_useless_stmts =
{
  "useless",				/* name */
  NULL,					/* gate */
  remove_useless_stmts,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_gimple_any,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,			/* todo_flags_finish */
  0					/* letter */
  ,0					/* works_with_tuples_p */
};

/* Remove PHI nodes associated with basic block BB and all edges out of BB.  */

static void
remove_phi_nodes_and_edges_for_unreachable_block (basic_block bb)
{
  gimple phi;

  /* Since this block is no longer reachable, we can just delete all
     of its PHI nodes.  */
  phi = gimple_seq_first (phi_nodes (bb));
  while (phi)
    {
      gimple next = gimple_next (phi);
      remove_phi_node (phi, true);
      phi = next;
    }

  set_phi_nodes (bb, NULL);

  /* Remove edges to BB's successors.  */
  while (EDGE_COUNT (bb->succs) > 0)
    remove_edge (EDGE_SUCC (bb, 0));
}


/* Remove statements of basic block BB.  */

static void
remove_bb (basic_block bb)
{
  gimple_stmt_iterator *i;
#ifdef USE_MAPPED_LOCATION
  source_location loc = UNKNOWN_LOCATION;
#else
  location_t loc = UNKNOWN_LOCATION;
#endif

  if (dump_file)
    {
      fprintf (dump_file, "Removing basic block %d\n", bb->index);
      if (dump_flags & TDF_DETAILS)
	{
	  dump_bb (bb, dump_file, 0);
	  fprintf (dump_file, "\n");
	}
    }

  if (current_loops)
    {
      struct loop *loop = bb->loop_father;

      /* If a loop gets removed, clean up the information associated
	 with it.  */
      if (loop->latch == bb
	  || loop->header == bb)
	free_numbers_of_iterations_estimates_loop (loop);
    }

  /* Remove all the instructions in the block.  */
  if (bb_seq (bb) != NULL)
    {
      for (i = gsi_start_bb (bb); !gsi_end_p (i);)
	{
	  gimple stmt = gsi_stmt (i);
	  if (gimple_code (stmt) == GIMPLE_LABEL
	      && (FORCED_LABEL (gimple_label_label (stmt))
		  || DECL_NONLOCAL (gimple_label_label (stmt))))
	    {
	      basic_block new_bb;
	      gimple_stmt_iterator *new_gsi;

	      /* A non-reachable non-local label may still be referenced.
		 But it no longer needs to carry the extra semantics of
		 non-locality.  */
	      if (DECL_NONLOCAL (gimple_label_label (stmt)))
		{
		  DECL_NONLOCAL (gimple_label_label (stmt)) = 0;
		  FORCED_LABEL (gimple_label_label (stmt)) = 1;
		}

	      new_bb = bb->prev_bb;
	      new_gsi = gsi_start_bb (new_bb);
	      gsi_remove (i, false);
	      gsi_insert_before (new_gsi, stmt, GSI_NEW_STMT);
	    }
	  else
	    {
	      /* Release SSA definitions if we are in SSA.  Note that we
		 may be called when not in SSA.  For example,
		 final_cleanup calls this function via
		 cleanup_tree_cfg.  */
	      if (gimple_in_ssa_p (cfun))
		release_defs (stmt);

	      gsi_remove (i, true);
	    }

	  /* Don't warn for removed gotos.  Gotos are often removed due to
	     jump threading, thus resulting in bogus warnings.  Not great,
	     since this way we lose warnings for gotos in the original
	     program that are indeed unreachable.  */
	  if (gimple_code (stmt) != GIMPLE_GOTO)
	    loc = gimple_locus (stmt);
	}
    }

  /* If requested, give a warning that the first statement in the
     block is unreachable.  We walk statements backwards in the
     loop above, so the last statement we process is the first statement
     in the block.  */
#ifdef USE_MAPPED_LOCATION
  if (loc > BUILTINS_LOCATION && LOCATION_LINE (loc) > 0)
    warning (OPT_Wunreachable_code, "%Hwill never be executed", &loc);
#else
  if (IS_LOCATION_EMPTY (loc))
    warning (OPT_Wunreachable_code, "%Hwill never be executed", &loc);
#endif

  remove_phi_nodes_and_edges_for_unreachable_block (bb);
  bb->il.gimple = NULL;
}


/* Given a basic block BB ending with COND_EXPR or SWITCH_EXPR, and a
   predicate VAL, return the edge that will be taken out of the block.
   If VAL does not match a unique edge, NULL is returned.  */

edge
find_taken_edge (basic_block bb, tree val)
{
  gimple stmt;

  stmt = last_stmt (bb);

  gcc_assert (stmt);
  gcc_assert (is_ctrl_stmt (stmt));
  gcc_assert (val);

  if (!is_gimple_min_invariant (val))
    return NULL;

  if (gimple_code (stmt) == GIMPLE_COND)
    return find_taken_edge_cond_expr (bb, val);

  if (gimple_code (stmt) == GIMPLE_SWITCH)
    return find_taken_edge_switch_expr (bb, val);

  if (computed_goto_p (stmt))
    {
      /* Only optimize if the argument is a label, if the argument is
	 not a label then we can not construct a proper CFG.

         It may be the case that we only need to allow the LABEL_REF to
         appear inside an ADDR_EXPR, but we also allow the LABEL_REF to
         appear inside a LABEL_EXPR just to be safe.  */
      if ((TREE_CODE (val) == ADDR_EXPR || TREE_CODE (val) == LABEL_EXPR)
	  && TREE_CODE (TREE_OPERAND (val, 0)) == LABEL_DECL)
	return find_taken_edge_computed_goto (bb, TREE_OPERAND (val, 0));
      return NULL;
    }

  gcc_unreachable ();
}

/* Given a constant value VAL and the entry block BB to a GOTO_EXPR
   statement, determine which of the outgoing edges will be taken out of the
   block.  Return NULL if either edge may be taken.  */

static edge
find_taken_edge_computed_goto (basic_block bb, tree val)
{
  basic_block dest;
  edge e = NULL;

  dest = label_to_block (val);
  if (dest)
    {
      e = find_edge (bb, dest);
      gcc_assert (e != NULL);
    }

  return e;
}

/* Given a constant value VAL and the entry block BB to a COND_EXPR
   statement, determine which of the two edges will be taken out of the
   block.  Return NULL if either edge may be taken.  */

static edge
find_taken_edge_cond_expr (basic_block bb, tree val)
{
  edge true_edge, false_edge;

  extract_true_false_edges_from_block (bb, &true_edge, &false_edge);

  gcc_assert (TREE_CODE (val) == INTEGER_CST);
  return (integer_zerop (val) ? false_edge : true_edge);
}

/* Given an INTEGER_CST VAL and the entry block BB to a SWITCH_EXPR
   statement, determine which edge will be taken out of the block.  Return
   NULL if any edge may be taken.  */

static edge
find_taken_edge_switch_expr (basic_block bb, tree val)
{
  basic_block dest_bb;
  edge e;
  gimple switch_stmt;
  tree taken_case;

  switch_stmt = last_stmt (bb);
  taken_case = find_case_label_for_value (switch_stmt, val);
  dest_bb = label_to_block (CASE_LABEL (taken_case));

  e = find_edge (bb, dest_bb);
  gcc_assert (e);
  return e;
}


/* Return the CASE_LABEL_EXPR that SWITCH_STMT will take for VAL.
   We can make optimal use here of the fact that the case labels are
   sorted: We can do a binary search for a case matching VAL.  */

static tree
find_case_label_for_value (gimple switch_stmt, tree val)
{
  size_t low, high, n = gimple_switch_num_labels (switch_stmt);
  tree default_case = gimple_switch_default_label (switch_stmt);

  for (low = -1, high = n - 1; high - low > 1; )
    {
      size_t i = (high + low) / 2;
      tree t = gimple_switch_label (switch_stmt, i);
      int cmp;

      /* Cache the result of comparing CASE_LOW and val.  */
      cmp = tree_int_cst_compare (CASE_LOW (t), val);

      if (cmp > 0)
	high = i;
      else
	low = i;

      if (CASE_HIGH (t) == NULL)
	{
	  /* A singe-valued case label.  */
	  if (cmp == 0)
	    return t;
	}
      else
	{
	  /* A case range.  We can only handle integer ranges.  */
	  if (cmp <= 0 && tree_int_cst_compare (CASE_HIGH (t), val) >= 0)
	    return t;
	}
    }

  return default_case;
}


/* Dump a basic block on stderr.  */

void
gimple_debug_bb (basic_block bb)
{
  gimple_dump_bb (bb, stderr, 0, TDF_VOPS|TDF_MEMSYMS);
}


/* Dump basic block with index N on stderr.  */

basic_block
gimple_debug_bb_n (int n)
{
  gimple_debug_bb (BASIC_BLOCK (n));
  return BASIC_BLOCK (n);
}


/* Dump the CFG on stderr.

   FLAGS are the same used by the tree dumping functions
   (see TDF_* in tree-pass.h).  */

void
gimple_debug_cfg (int flags)
{
  gimple_dump_cfg (stderr, flags);
}


/* Dump the program showing basic block boundaries on the given FILE.

   FLAGS are the same used by the tree dumping functions (see TDF_* in
   tree.h).  */

void
gimple_dump_cfg (FILE *file, int flags)
{
  if (flags & TDF_DETAILS)
    {
      const char *funcname
	= lang_hooks.decl_printable_name (current_function_decl, 2);

      fputc ('\n', file);
      fprintf (file, ";; Function %s\n\n", funcname);
      fprintf (file, ";; \n%d basic blocks, %d edges, last basic block %d.\n\n",
	       n_basic_blocks, n_edges, last_basic_block);

      brief_dump_cfg (file);
      fprintf (file, "\n");
    }

  if (flags & TDF_STATS)
    dump_cfg_stats (file);

  dump_function_to_file (current_function_decl, file, flags | TDF_BLOCKS);
}


/* Dump CFG statistics on FILE.  */

void
dump_cfg_stats (FILE *file)
{
  static long max_num_merged_labels = 0;
  unsigned long size, total = 0;
  long num_edges;
  basic_block bb;
  const char * const fmt_str   = "%-30s%-13s%12s\n";
  const char * const fmt_str_1 = "%-30s%13d%11lu%c\n";
  const char * const fmt_str_2 = "%-30s%13ld%11lu%c\n";
  const char * const fmt_str_3 = "%-43s%11lu%c\n";
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);


  fprintf (file, "\nCFG Statistics for %s\n\n", funcname);

  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, fmt_str, "", "  Number of  ", "Memory");
  fprintf (file, fmt_str, "", "  instances  ", "used ");
  fprintf (file, "---------------------------------------------------------\n");

  size = n_basic_blocks * sizeof (struct basic_block_def);
  total += size;
  fprintf (file, fmt_str_1, "Basic blocks", n_basic_blocks,
	   SCALE (size), LABEL (size));

  num_edges = 0;
  FOR_EACH_BB (bb)
    num_edges += EDGE_COUNT (bb->succs);
  size = num_edges * sizeof (struct edge_def);
  total += size;
  fprintf (file, fmt_str_2, "Edges", num_edges, SCALE (size), LABEL (size));

  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, fmt_str_3, "Total memory used by CFG data", SCALE (total),
	   LABEL (total));
  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, "\n");

  if (cfg_stats.num_merged_labels > max_num_merged_labels)
    max_num_merged_labels = cfg_stats.num_merged_labels;

  fprintf (file, "Coalesced label blocks: %ld (Max so far: %ld)\n",
	   cfg_stats.num_merged_labels, max_num_merged_labels);

  fprintf (file, "\n");
}


/* Dump CFG statistics on stderr.  Keep extern so that it's always
   linked in the final executable.  */

void
debug_cfg_stats (void)
{
  dump_cfg_stats (stderr);
}


/* Dump the flowgraph to a .vcg FILE.  */

static void
tree_cfg2vcg (FILE *file)
{
  edge e;
  edge_iterator ei;
  basic_block bb;
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);

  /* Write the file header.  */
  fprintf (file, "graph: { title: \"%s\"\n", funcname);
  fprintf (file, "node: { title: \"ENTRY\" label: \"ENTRY\" }\n");
  fprintf (file, "node: { title: \"EXIT\" label: \"EXIT\" }\n");

  /* Write blocks and edges.  */
  FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR->succs)
    {
      fprintf (file, "edge: { sourcename: \"ENTRY\" targetname: \"%d\"",
	       e->dest->index);

      if (e->flags & EDGE_FAKE)
	fprintf (file, " linestyle: dotted priority: 10");
      else
	fprintf (file, " linestyle: solid priority: 100");

      fprintf (file, " }\n");
    }
  fputc ('\n', file);

  FOR_EACH_BB (bb)
    {
      enum gimple_code head_code, end_code;
      const char *head_name, *end_name;
      int head_line = 0;
      int end_line = 0;
      gimple first = first_stmt (bb);
      gimple last = last_stmt (bb);

      if (first)
	{
	  head_code = gimple_code (first);
	  head_name = gimple_code_name[head_code];
	  head_line = get_lineno (first);
	}
      else
	head_name = "no-statement";

      if (last)
	{
	  end_code = gimple_code (last);
	  end_name = gimple_code_name[end_code];
	  end_line = get_lineno (last);
	}
      else
	end_name = "no-statement";

      fprintf (file, "node: { title: \"%d\" label: \"#%d\\n%s (%d)\\n%s (%d)\"}\n",
	       bb->index, bb->index, head_name, head_line, end_name,
	       end_line);

      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (e->dest == EXIT_BLOCK_PTR)
	    fprintf (file, "edge: { sourcename: \"%d\" targetname: \"EXIT\"", bb->index);
	  else
	    fprintf (file, "edge: { sourcename: \"%d\" targetname: \"%d\"", bb->index, e->dest->index);

	  if (e->flags & EDGE_FAKE)
	    fprintf (file, " priority: 10 linestyle: dotted");
	  else
	    fprintf (file, " priority: 100 linestyle: solid");

	  fprintf (file, " }\n");
	}

      if (bb->next_bb != EXIT_BLOCK_PTR)
	fputc ('\n', file);
    }

  fputs ("}\n\n", file);
}



/*---------------------------------------------------------------------------
			     Miscellaneous helpers
---------------------------------------------------------------------------*/

/* Return true if T represents a stmt that always transfers control.  */

bool
is_ctrl_stmt (gimple t)
{
  return gimple_code (t) == GIMPLE_COND
    || gimple_code (t) == GIMPLE_SWITCH
    || gimple_code (t) == GIMPLE_GOTO
    || gimple_code (t) == GIMPLE_RETURN
    || gimple_code (t) == GIMPLE_RESX;
}


/* Return true if T is a statement that may alter the flow of control
   (e.g., a call to a non-returning function).  */

bool
is_ctrl_altering_stmt (gimple t)
{
  gcc_assert (t);

  if (gimple_code (t) == GIMPLE_CALL)
    {
      int flags = gimple_call_flags (t);

      /* A non-pure/const call alters flow control if the current
	 function has nonlocal labels.  */
      if (!(flags & (ECF_CONST | ECF_PURE))
	  && current_function_has_nonlocal_label)
	return true;

      /* A call also alters control flow if it does not return.  */
      if (gimple_call_flags (t) & ECF_NORETURN)
	return true;
    }

  /* OpenMP directives alter control flow.  */
  if (is_gimple_omp (t))
    return true;

  /* If a statement can throw, it alters control flow.  */
  return stmt_can_throw_internal (t);
}


/* Return true if T is a computed goto.  */
/* FIXME tuples: This is unused elsewhere.  It should be a static.  */

bool
computed_goto_p (gimple t)
{
  return (gimple_code (t) == GIMPLE_GOTO
	  && TREE_CODE (gimple_goto_dest (t)) != LABEL_DECL);
}


/* Return true if T is a simple local goto.  */

bool
simple_goto_p (gimple t)
{
  return (gimple_code (t) == GIMPLE_GOTO
	  && TREE_CODE (gimple_goto_dest (t)) == LABEL_DECL);
}


/* Return true if T can make an abnormal transfer of control flow.
   Transfers of control flow associated with EH are excluded.  */

bool
stmt_can_make_abnormal_goto (gimple t)
{
  if (computed_goto_p (t))
    return true;
  if (gimple_code (t) == GIMPLE_CALL)
    return /* FIXME tuples: TREE_SIDE_EFFECTS (t) && */
      current_function_has_nonlocal_label;
  return false;
}


/* Return true if STMT should start a new basic block.  PREV_STMT is
   the statement preceding STMT.  It is used when STMT is a label or a
   case label.  Labels should only start a new basic block if their
   previous statement wasn't a label.  Otherwise, sequence of labels
   would generate unnecessary basic blocks that only contain a single
   label.  */

static inline bool
stmt_starts_bb_p (gimple stmt, gimple prev_stmt)
{
  if (stmt == NULL)
    return false;

  /* Labels start a new basic block only if the preceding statement
     wasn't a label of the same type.  This prevents the creation of
     consecutive blocks that have nothing but a single label.  */
  if (gimple_code (stmt) == GIMPLE_LABEL)
    {
      /* Nonlocal and computed GOTO targets always start a new block.  */
      if (DECL_NONLOCAL (gimple_label_label (stmt))
	  || FORCED_LABEL (gimple_label_label (stmt)))
	return true;

      if (prev_stmt && gimple_code (prev_stmt) == GIMPLE_LABEL)
	{
	  if (DECL_NONLOCAL (gimple_label_label (prev_stmt)))
	    return true;

	  cfg_stats.num_merged_labels++;
	  return false;
	}
      else
	return true;
    }

  return false;
}


/* Return true if T should end a basic block.  */

bool
stmt_ends_bb_p (gimple t)
{
  return is_ctrl_stmt (t) || is_ctrl_altering_stmt (t);
}

/* Remove block annotations and other data structures.  */

void
delete_tree_cfg_annotations (void)
{
  label_to_block_map = NULL;
}


/* Return the first statement in basic block BB.  */

gimple
first_stmt (basic_block bb)
{
  gimple_stmt_iterator *i = gsi_start_bb (bb);
  return !gsi_end_p (i) ? gsi_stmt (i) : NULL;
}

/* Return the last statement in basic block BB.  */

gimple
last_stmt (basic_block bb)
{
  gimple_stmt_iterator *b = gsi_last (bb_seq (bb));
  return !gsi_end_p (b) ? gsi_stmt (b) : NULL;
}

/* Return the last statement of an otherwise empty block.  Return NULL
   if the block is totally empty, or if it contains more than one
   statement.  */

gimple
last_and_only_stmt (basic_block bb)
{
  gimple_stmt_iterator *i = gsi_last (bb_seq (bb));
  gimple last, prev;

  if (gsi_end_p (i))
    return NULL;

  last = gsi_stmt (i);
  gsi_prev (i);
  if (gsi_end_p (i))
    return last;

  /* Empty statements should no longer appear in the instruction stream.
     Everything that might have appeared before should be deleted by
     remove_useless_stmts, and the optimizers should just gsi_remove
     instead of smashing with build_empty_stmt.

     Thus the only thing that should appear here in a block containing
     one executable statement is a label.  */
  prev = gsi_stmt (i);
  if (gimple_code (prev) == GIMPLE_LABEL)
    return last;
  else
    return NULL;
}


/* Faster version of set_bb_for_stmt that assume that statement is being moved
   from one basic block to another.  
   For BB splitting we can run into quadratic case, so performance is quite
   important and knowing that the tables are big enough, change_bb_for_stmt
   can inline as leaf function.  */
static inline void
change_bb_for_stmt (gimple t, basic_block bb)
{
  set_gimple_bb (t, bb);
  if (gimple_code (t) == GIMPLE_LABEL)
    VEC_replace (basic_block, label_to_block_map,
		 LABEL_DECL_UID (gimple_label_label (t)), bb);
}


/* FIXME tuples.  */
#if 0
/* Insert the statement pointed-to by GSI into edge E.  Every attempt
   is made to place the statement in an existing basic block, but
   sometimes that isn't possible.  When it isn't possible, the edge is
   split and the statement is added to the new block.

   In all cases, the returned *GSI points to the correct location.  The
   return value is true if insertion should be done after the location,
   or false if it should be done before the location.  If new basic block
   has to be created, it is stored in *NEW_BB.  */

static bool
tree_find_edge_insert_loc (edge e, gimple_stmt_iterator **gsi,
			   basic_block *new_bb)
{
  basic_block dest, src;
  gimple tmp;

  dest = e->dest;
 restart:

  /* If the destination has one predecessor which has no PHI nodes,
     insert there.  Except for the exit block.

     The requirement for no PHI nodes could be relaxed.  Basically we
     would have to examine the PHIs to prove that none of them used
     the value set by the statement we want to insert on E.  That
     hardly seems worth the effort.  */
  if (single_pred_p (dest)
      && ! phi_nodes (dest)
      && dest != EXIT_BLOCK_PTR)
    {
      *gsi = gsi_start_bb (dest);
      if (gsi_end_p (*gsi))
	return true;

      /* Make sure we insert after any leading labels.  */
      tmp = gsi_stmt (*gsi);
      while (gimple_code (tmp) == GIMPLE_LABEL)
	{
	  gsi_next (*gsi);
	  if (gsi_end_p (*gsi))
	    break;
	  tmp = gsi_stmt (*gsi);
	}

      if (gsi_end_p (*gsi))
	{
	  *gsi = gsi_last (bb_seq (dest));
	  return true;
	}
      else
	return false;
    }

  /* If the source has one successor, the edge is not abnormal and
     the last statement does not end a basic block, insert there.
     Except for the entry block.  */
  src = e->src;
  if ((e->flags & EDGE_ABNORMAL) == 0
      && single_succ_p (src)
      && src != ENTRY_BLOCK_PTR)
    {
      *gsi = gsi_last (bb_seq (src));
      if (gsi_end_p (*gsi))
	return true;

      tmp = gsi_stmt (*gsi);
      if (!stmt_ends_bb_p (tmp))
	return true;

      if (gimple_code (tmp) == GIMPLE_RETURN)
        {
	  gsi_prev (*gsi);
	  return true;
        }
    }

  /* Otherwise, create a new basic block, and split this edge.  */
  dest = split_edge (e);
  if (new_bb)
    *new_bb = dest;
  e = single_pred_edge (dest);
  goto restart;
}
#endif


/* This routine will commit all pending edge insertions, creating any new
   basic blocks which are necessary.  */

void
gsi_commit_edge_inserts (void)
{
  basic_block bb;
  edge e;
  edge_iterator ei;

  gsi_commit_one_edge_insert (single_succ_edge (ENTRY_BLOCK_PTR), NULL);

  FOR_EACH_BB (bb)
    FOR_EACH_EDGE (e, ei, bb->succs)
      gsi_commit_one_edge_insert (e, NULL);
}


/* Commit insertions pending at edge E. If a new block is created, set NEW_BB
   to this block, otherwise set it to NULL.  */

void
gsi_commit_one_edge_insert (edge e, basic_block *new_bb)
{
  if (new_bb)
    *new_bb = NULL;

  if (PENDING_STMT (e))
    {
      /* FIXME tuples.  */
#if 0
      gimple_stmt_iterator *gsi;
      gimple stmt = PENDING_STMT (e);

      PENDING_STMT (e) = NULL_TREE;

      if (tree_find_edge_insert_loc (e, &gsi, new_bb))
	gsi_insert_after (gsi, stmt, GSI_NEW_STMT);
      else
	gsi_insert_before (gsi, stmt, GSI_NEW_STMT);
#else
      gcc_unreachable ();
#endif
    }
}


/* Add STMT to the pending list of edge E.  No actual insertion is
   made until a call to gsi_commit_edge_inserts () is made.  */

void
gsi_insert_on_edge (edge e ATTRIBUTE_UNUSED, tree stmt ATTRIBUTE_UNUSED)
{
  /* FIXME tuples.  */
#if 0
  append_to_statement_list (stmt, &PENDING_STMT (e));
#else
  gcc_unreachable ();
#endif
}

/* Similar to gsi_insert_on_edge+gsi_commit_edge_inserts.  If a new
   block has to be created, it is returned.  */

basic_block
gsi_insert_on_edge_immediate (edge e ATTRIBUTE_UNUSED, tree stmt ATTRIBUTE_UNUSED)
{
  /* FIXME tuples.  */
#if 0
  gimple_stmt_iterator gsi;
  basic_block new_bb = NULL;

  gcc_assert (!PENDING_STMT (e));

  if (tree_find_edge_insert_loc (e, &gsi, &new_bb))
    gsi_insert_after (&gsi, stmt, GSI_NEW_STMT);
  else
    gsi_insert_before (&gsi, stmt, GSI_NEW_STMT);

  return new_bb;
#else
  gcc_unreachable ();
#endif
}

/*---------------------------------------------------------------------------
	     Tree specific functions for CFG manipulation
---------------------------------------------------------------------------*/

/* Reinstall those PHI arguments queued in OLD_EDGE to NEW_EDGE.  */

static void
reinstall_phi_args (edge new_edge, edge old_edge)
{
  edge_var_map_vector v;
  edge_var_map *vm;
  int i;
  gimple_stmt_iterator *phis;
  
  v = redirect_edge_var_map_vector (old_edge);
  if (!v)
    return;
  
  for (i = 0, phis = gsi_start (phi_nodes (new_edge->dest));
       VEC_iterate (edge_var_map, v, i, vm) && !gsi_end_p (phis);
       i++, gsi_next (phis))
    {
      gimple phi = gsi_stmt (phis);
      tree result = redirect_edge_var_map_result (vm);
      tree arg = redirect_edge_var_map_def (vm);
 
      gcc_assert (result == gimple_phi_result (phi));
  
      add_phi_arg (phi, arg, new_edge);
    }
  
  redirect_edge_var_map_clear (old_edge);
}

/* Returns the basic block after which the new basic block created
   by splitting edge EDGE_IN should be placed.  Tries to keep the new block
   near its "logical" location.  This is of most help to humans looking
   at debugging dumps.  */

static basic_block
split_edge_bb_loc (edge edge_in)
{
  basic_block dest = edge_in->dest;

  if (dest->prev_bb && find_edge (dest->prev_bb, dest))
    return edge_in->src;
  else
    return dest->prev_bb;
}

/* Split a (typically critical) edge EDGE_IN.  Return the new block.
   Abort on abnormal edges.  */

static basic_block
tree_split_edge (edge edge_in)
{
  basic_block new_bb, after_bb, dest;
  edge new_edge, e;

  /* Abnormal edges cannot be split.  */
  gcc_assert (!(edge_in->flags & EDGE_ABNORMAL));

  dest = edge_in->dest;

  after_bb = split_edge_bb_loc (edge_in);

  new_bb = create_empty_bb (after_bb);
  new_bb->frequency = EDGE_FREQUENCY (edge_in);
  new_bb->count = edge_in->count;
  new_edge = make_edge (new_bb, dest, EDGE_FALLTHRU);
  new_edge->probability = REG_BR_PROB_BASE;
  new_edge->count = edge_in->count;

  e = redirect_edge_and_branch (edge_in, new_bb);
  gcc_assert (e == edge_in);
  reinstall_phi_args (new_edge, e);

  return new_bb;
}

/* Callback for walk_tree, check that all elements with address taken are
   properly noticed as such.  The DATA is an int* that is 1 if TP was seen
   inside a PHI node.  */

/* FIXME tuples.  */
#if 0
static tree
verify_expr (tree *tp, int *walk_subtrees, void *data ATTRIBUTE_UNUSED)
{
  tree t = *tp, x;
  bool in_phi = (data != NULL);

  if (TYPE_P (t))
    *walk_subtrees = 0;

  /* Check operand N for being valid GIMPLE and give error MSG if not.  */
#define CHECK_OP(N, MSG) \
  do { if (!is_gimple_val (TREE_OPERAND (t, N)))		\
       { error (MSG); return TREE_OPERAND (t, N); }} while (0)

  switch (TREE_CODE (t))
    {
    case SSA_NAME:
      if (SSA_NAME_IN_FREE_LIST (t))
	{
	  error ("SSA name in freelist but still referenced");
	  return *tp;
	}
      break;

    case ASSERT_EXPR:
      x = fold (ASSERT_EXPR_COND (t));
      if (x == boolean_false_node)
	{
	  error ("ASSERT_EXPR with an always-false condition");
	  return *tp;
	}
      break;

    case MODIFY_EXPR:
      gcc_unreachable ();

    case GIMPLE_MODIFY_STMT:
      x = GIMPLE_STMT_OPERAND (t, 0);
      if (TREE_CODE (x) == BIT_FIELD_REF
	  && is_gimple_reg (TREE_OPERAND (x, 0)))
	{
	  error ("GIMPLE register modified with BIT_FIELD_REF");
	  return t;
	}
      break;

    case ADDR_EXPR:
      {
	bool old_invariant;
	bool old_constant;
	bool old_side_effects;
	bool new_invariant;
	bool new_constant;
	bool new_side_effects;

        /* ??? tree-ssa-alias.c may have overlooked dead PHI nodes, missing
	   dead PHIs that take the address of something.  But if the PHI
	   result is dead, the fact that it takes the address of anything
	   is irrelevant.  Because we can not tell from here if a PHI result
	   is dead, we just skip this check for PHIs altogether.  This means
	   we may be missing "valid" checks, but what can you do?
	   This was PR19217.  */
        if (in_phi)
	  break;

	old_invariant = TREE_INVARIANT (t);
	old_constant = TREE_CONSTANT (t);
	old_side_effects = TREE_SIDE_EFFECTS (t);

	recompute_tree_invariant_for_addr_expr (t);
	new_invariant = TREE_INVARIANT (t);
	new_side_effects = TREE_SIDE_EFFECTS (t);
	new_constant = TREE_CONSTANT (t);

	if (old_invariant != new_invariant)
	  {
	    error ("invariant not recomputed when ADDR_EXPR changed");
	    return t;
	  }

        if (old_constant != new_constant)
	  {
	    error ("constant not recomputed when ADDR_EXPR changed");
	    return t;
	  }
	if (old_side_effects != new_side_effects)
	  {
	    error ("side effects not recomputed when ADDR_EXPR changed");
	    return t;
	  }

	/* Skip any references (they will be checked when we recurse down the
	   tree) and ensure that any variable used as a prefix is marked
	   addressable.  */
	for (x = TREE_OPERAND (t, 0);
	     handled_component_p (x);
	     x = TREE_OPERAND (x, 0))
	  ;

	if (TREE_CODE (x) != VAR_DECL && TREE_CODE (x) != PARM_DECL)
	  return NULL;
	if (!TREE_ADDRESSABLE (x))
	  {
	    error ("address taken, but ADDRESSABLE bit not set");
	    return x;
	  }
	break;
      }

    case COND_EXPR:
      x = COND_EXPR_COND (t);
      if (!INTEGRAL_TYPE_P (TREE_TYPE (x)))
	{
	  error ("non-integral used in condition");
	  return x;
	}
      if (!is_gimple_condexpr (x))
        {
	  error ("invalid conditional operand");
	  return x;
	}
      break;

    case NOP_EXPR:
    case CONVERT_EXPR:
    case FIX_TRUNC_EXPR:
    case FLOAT_EXPR:
    case NEGATE_EXPR:
    case ABS_EXPR:
    case BIT_NOT_EXPR:
    case NON_LVALUE_EXPR:
    case TRUTH_NOT_EXPR:
      CHECK_OP (0, "invalid operand to unary operator");
      break;

    case REALPART_EXPR:
    case IMAGPART_EXPR:
    case COMPONENT_REF:
    case ARRAY_REF:
    case ARRAY_RANGE_REF:
    case BIT_FIELD_REF:
    case VIEW_CONVERT_EXPR:
      /* We have a nest of references.  Verify that each of the operands
	 that determine where to reference is either a constant or a variable,
	 verify that the base is valid, and then show we've already checked
	 the subtrees.  */
      while (handled_component_p (t))
	{
	  if (TREE_CODE (t) == COMPONENT_REF && TREE_OPERAND (t, 2))
	    CHECK_OP (2, "invalid COMPONENT_REF offset operator");
	  else if (TREE_CODE (t) == ARRAY_REF
		   || TREE_CODE (t) == ARRAY_RANGE_REF)
	    {
	      CHECK_OP (1, "invalid array index");
	      if (TREE_OPERAND (t, 2))
		CHECK_OP (2, "invalid array lower bound");
	      if (TREE_OPERAND (t, 3))
		CHECK_OP (3, "invalid array stride");
	    }
	  else if (TREE_CODE (t) == BIT_FIELD_REF)
	    {
	      CHECK_OP (1, "invalid operand to BIT_FIELD_REF");
	      CHECK_OP (2, "invalid operand to BIT_FIELD_REF");
	    }

	  t = TREE_OPERAND (t, 0);
	}

      if (!CONSTANT_CLASS_P (t) && !is_gimple_lvalue (t))
	{
	  error ("invalid reference prefix");
	  return t;
	}
      *walk_subtrees = 0;
      break;
    case PLUS_EXPR:
    case MINUS_EXPR:
      /* PLUS_EXPR and MINUS_EXPR don't work on pointers, they should be done using
	 POINTER_PLUS_EXPR. */
      if (POINTER_TYPE_P (TREE_TYPE (t)))
	{
	  error ("invalid operand to plus/minus, type is a pointer");
	  return t;
	}
      CHECK_OP (0, "invalid operand to binary operator");
      CHECK_OP (1, "invalid operand to binary operator");
      break;

    case POINTER_PLUS_EXPR:
      /* Check to make sure the first operand is a pointer or reference type. */
      if (!POINTER_TYPE_P (TREE_TYPE (TREE_OPERAND (t, 0))))
	{
	  error ("invalid operand to pointer plus, first operand is not a pointer");
	  return t;
	}
      /* Check to make sure the second operand is an integer with type of
	 sizetype.  */
      if (!useless_type_conversion_p (sizetype,
				     TREE_TYPE (TREE_OPERAND (t, 1))))
	{
	  error ("invalid operand to pointer plus, second operand is not an "
		 "integer with type of sizetype.");
	  return t;
	}
      /* FALLTHROUGH */
    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case UNORDERED_EXPR:
    case ORDERED_EXPR:
    case UNLT_EXPR:
    case UNLE_EXPR:
    case UNGT_EXPR:
    case UNGE_EXPR:
    case UNEQ_EXPR:
    case LTGT_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case TRUNC_MOD_EXPR:
    case CEIL_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case ROUND_MOD_EXPR:
    case RDIV_EXPR:
    case EXACT_DIV_EXPR:
    case MIN_EXPR:
    case MAX_EXPR:
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
    case LROTATE_EXPR:
    case RROTATE_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case BIT_AND_EXPR:
      CHECK_OP (0, "invalid operand to binary operator");
      CHECK_OP (1, "invalid operand to binary operator");
      break;

    case CONSTRUCTOR:
      if (TREE_CONSTANT (t) && TREE_CODE (TREE_TYPE (t)) == VECTOR_TYPE)
	*walk_subtrees = 0;
      break;

    default:
      break;
    }
  return NULL;

#undef CHECK_OP
}
#endif


/* Verify if EXPR is either a GIMPLE ID or a GIMPLE indirect reference.
   Returns true if there is an error, otherwise false.  */

static bool
verify_types_in_gimple_min_lval (tree expr)
{
  tree op;

  if (is_gimple_id (expr))
    return false;

  if (TREE_CODE (expr) != INDIRECT_REF
      && TREE_CODE (expr) != ALIGN_INDIRECT_REF
      && TREE_CODE (expr) != MISALIGNED_INDIRECT_REF)
    {
      error ("invalid expression for min lvalue");
      return true;
    }

  op = TREE_OPERAND (expr, 0);
  if (!is_gimple_val (op))
    {
      error ("invalid operand in indirect reference");
      debug_generic_stmt (op);
      return true;
    }
  if (!useless_type_conversion_p (TREE_TYPE (expr),
				  TREE_TYPE (TREE_TYPE (op))))
    {
      error ("type mismatch in indirect reference");
      debug_generic_stmt (TREE_TYPE (expr));
      debug_generic_stmt (TREE_TYPE (TREE_TYPE (op)));
      return true;
    }

  return false;
}

/* Verify if EXPR is a valid GIMPLE reference expression.  Returns true
   if there is an error, otherwise false.  */

static bool
verify_types_in_gimple_reference (tree expr)
{
  while (handled_component_p (expr))
    {
      tree op = TREE_OPERAND (expr, 0);

      if (TREE_CODE (expr) == ARRAY_REF
	  || TREE_CODE (expr) == ARRAY_RANGE_REF)
	{
	  if (!is_gimple_val (TREE_OPERAND (expr, 1))
	      || (TREE_OPERAND (expr, 2)
		  && !is_gimple_val (TREE_OPERAND (expr, 2)))
	      || (TREE_OPERAND (expr, 3)
		  && !is_gimple_val (TREE_OPERAND (expr, 3))))
	    {
	      error ("invalid operands to array reference");
	      debug_generic_stmt (expr);
	      return true;
	    }
	}

      /* Verify if the reference array element types are compatible.  */
      if (TREE_CODE (expr) == ARRAY_REF
	  && !useless_type_conversion_p (TREE_TYPE (expr),
					 TREE_TYPE (TREE_TYPE (op))))
	{
	  error ("type mismatch in array reference");
	  debug_generic_stmt (TREE_TYPE (expr));
	  debug_generic_stmt (TREE_TYPE (TREE_TYPE (op)));
	  return true;
	}
      if (TREE_CODE (expr) == ARRAY_RANGE_REF
	  && !useless_type_conversion_p (TREE_TYPE (TREE_TYPE (expr)),
					 TREE_TYPE (TREE_TYPE (op))))
	{
	  error ("type mismatch in array range reference");
	  debug_generic_stmt (TREE_TYPE (TREE_TYPE (expr)));
	  debug_generic_stmt (TREE_TYPE (TREE_TYPE (op)));
	  return true;
	}

      if ((TREE_CODE (expr) == REALPART_EXPR
	   || TREE_CODE (expr) == IMAGPART_EXPR)
	  && !useless_type_conversion_p (TREE_TYPE (expr),
					 TREE_TYPE (TREE_TYPE (op))))
	{
	  error ("type mismatch in real/imagpart reference");
	  debug_generic_stmt (TREE_TYPE (expr));
	  debug_generic_stmt (TREE_TYPE (TREE_TYPE (op)));
	  return true;
	}

      if (TREE_CODE (expr) == COMPONENT_REF
	  && !useless_type_conversion_p (TREE_TYPE (expr),
					 TREE_TYPE (TREE_OPERAND (expr, 1))))
	{
	  error ("type mismatch in component reference");
	  debug_generic_stmt (TREE_TYPE (expr));
	  debug_generic_stmt (TREE_TYPE (TREE_OPERAND (expr, 1)));
	  return true;
	}

      /* For VIEW_CONVERT_EXPRs which are allowed here, too, there
	 is nothing to verify.  Gross mismatches at most invoke
	 undefined behavior.  */

      expr = op;
    }

  return verify_types_in_gimple_min_lval (expr);
}


/* Verify that OP is a valid GIMPLE operand.  Return true if theere is
   an error, false otherwise.  */

static bool
verify_types_in_gimple_op (tree op)
{
  if (!is_gimple_val (op) && !is_gimple_lvalue (op))
    {
      error ("Invalid GIMPLE operand");
      debug_generic_expr (op);
      return true;
    }

  return false;
}


/* Verify the contents of a GIMPLE_CALL STMT.  Returns true when there
   is a problem, otherwise false.  */

static bool
verify_types_in_gimple_call (gimple stmt)
{
  bool failed = false;
  unsigned int i;

  if (gimple_call_lhs (stmt))
    failed |= verify_types_in_gimple_op (gimple_call_lhs (stmt));

  failed |= verify_types_in_gimple_op (gimple_call_fn (stmt));

  if (gimple_call_chain (stmt))
    failed |= verify_types_in_gimple_op (gimple_call_chain (stmt));

  for (i = 0; i < gimple_call_nargs (stmt); i++)
    failed |= verify_types_in_gimple_op (gimple_call_arg (stmt,i));

  return failed;
}


/* Verify the contents of a GIMPLE_COND STMT.  Returns true when there
   is a problem, otherwise false.  */

static bool
verify_types_in_gimple_cond (gimple stmt)
{
  bool failed = false;
  
  failed |= verify_types_in_gimple_op (gimple_cond_lhs (stmt));
  failed |= verify_types_in_gimple_op (gimple_cond_rhs (stmt));
  failed |= verify_types_in_gimple_op (gimple_cond_true_label (stmt));
  failed |= verify_types_in_gimple_op (gimple_cond_false_label (stmt));

  return failed;
}


/* Verify the contents of a GIMPLE_ASSIGN STMT.  Returns true when there
   is a problem, otherwise false.

   Verify that the types of the LHS and the RHS operands are
   compatible.  This verification largely depends on what kind of
   operation is done on the RHS of the assignment.  It is not always
   the case that all the types of the operands must match (e.g., 'a =
   (unsigned long) b' or 'ptr = ptr + 1').  */

static bool
verify_types_in_gimple_assign (gimple stmt)
{
  enum tree_code rhs_code = gimple_assign_subcode (stmt);
  tree lhs = gimple_assign_lhs (stmt);
  tree rhs1 = gimple_assign_rhs1 (stmt);
  tree rhs2 = (gimple_num_ops (stmt) == 3) ? gimple_assign_rhs2 (stmt) : NULL;
  tree lhs_type = TREE_TYPE (lhs);
  tree rhs1_type = TREE_TYPE (rhs1);
  tree rhs2_type = (rhs2) ? TREE_TYPE (rhs2) : NULL;

  /* Special codes we cannot handle via their class.  */
  switch (rhs_code)
    {
    case NOP_EXPR:
    case CONVERT_EXPR:
      {
	if (!is_gimple_val (rhs1))
	  {
	    error ("invalid operand in conversion");
	    return true;
	  }

	/* Allow conversions between integral types and pointers only if
	   there is no sign or zero extension involved.  */
	if (((POINTER_TYPE_P (lhs_type) && INTEGRAL_TYPE_P (rhs1_type))
	     || (POINTER_TYPE_P (rhs1_type) && INTEGRAL_TYPE_P (lhs_type)))
	    && TYPE_PRECISION (lhs_type) == TYPE_PRECISION (rhs1_type))
	  return false;

	/* Allow conversion from integer to offset type and vice versa.  */
	if ((TREE_CODE (lhs_type) == OFFSET_TYPE
	     && TREE_CODE (rhs1_type) == INTEGER_TYPE)
	    || (TREE_CODE (lhs_type) == INTEGER_TYPE
		&& TREE_CODE (rhs1_type) == OFFSET_TYPE))
	  return false;

	/* Otherwise assert we are converting between types of the
	   same kind.  */
	if (INTEGRAL_TYPE_P (lhs_type) != INTEGRAL_TYPE_P (rhs1_type))
	  {
	    error ("invalid types in nop conversion");
	    debug_generic_expr (lhs_type);
	    debug_generic_expr (rhs1_type);
	    return true;
	  }

	return false;
      }

    case FLOAT_EXPR:
      {
	if (!is_gimple_val (rhs1))
	  {
	    error ("invalid operand in int to float conversion");
	    return true;
	  }

	if (!INTEGRAL_TYPE_P (rhs1_type) || !SCALAR_FLOAT_TYPE_P (lhs_type))
	  {
	    error ("invalid types in conversion to floating point");
	    debug_generic_expr (lhs_type);
	    debug_generic_expr (rhs1_type);
	    return true;
	  }

        return false;
      }

    case FIX_TRUNC_EXPR:
      {
	if (!is_gimple_val (rhs1))
	  {
	    error ("invalid operand in float to int conversion");
	    return true;
	  }

	if (!INTEGRAL_TYPE_P (lhs_type) || !SCALAR_FLOAT_TYPE_P (rhs1_type))
	  {
	    error ("invalid types in conversion to integer");
	    debug_generic_expr (lhs_type);
	    debug_generic_expr (rhs1_type);
	    return true;
	  }

        return false;
      }

    case COMPLEX_EXPR:
      {
	if (!is_gimple_val (rhs1) || !is_gimple_val (rhs2))
	  {
	    error ("invalid operands in complex expression");
	    return true;
	  }

	if (!TREE_CODE (lhs_type) == COMPLEX_TYPE
	    || !(TREE_CODE (rhs1_type) == INTEGER_TYPE
	         || SCALAR_FLOAT_TYPE_P (rhs1_type))
	    || !(TREE_CODE (rhs2_type) == INTEGER_TYPE
	         || SCALAR_FLOAT_TYPE_P (rhs2_type)))
	  {
	    error ("type mismatch in complex expression");
	    debug_generic_expr (lhs_type);
	    debug_generic_expr (rhs1_type);
	    debug_generic_expr (rhs2_type);
	    return true;
	  }

	return false;
      }

    case CONSTRUCTOR:
      {
	/* FIXME tuples.  Note that in mainline, this code disallows
	   CONSTRUCTOR expressions for types other than vector types,
	   but in this context we know that we are on the RHS of an
	   assignment, so CONSTRUCTOR operands are OK.  */
	/* FIXME: verify constructor arguments.  */
	return false;
      }

    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
    case LROTATE_EXPR:
    case RROTATE_EXPR:
      {
	if (!is_gimple_val (rhs1) || !is_gimple_val (rhs2))
	  {
	    error ("invalid operands in shift expression");
	    return true;
	  }

	if (!TREE_CODE (rhs1_type) == INTEGER_TYPE
	    || !useless_type_conversion_p (lhs_type, rhs1_type))
	  {
	    error ("type mismatch in shift expression");
	    debug_generic_expr (lhs_type);
	    debug_generic_expr (rhs1_type);
	    debug_generic_expr (rhs2_type);
	    return true;
	  }

	return false;
      }

    case PLUS_EXPR:
    case MINUS_EXPR:
      {
	if (POINTER_TYPE_P (lhs_type)
	    || POINTER_TYPE_P (rhs1_type)
	    || POINTER_TYPE_P (rhs2_type))
	  {
	    error ("invalid (pointer) operands to plus/minus");
	    return true;
	  }

	/* Continue with generic binary expression handling.  */
	break;
      }

    case POINTER_PLUS_EXPR:
      {
      	if (!is_gimple_val (rhs1) || !is_gimple_val (rhs2))
	  {
	    error ("invalid operands in pointer plus expression");
	    return true;
	  }

	if (!POINTER_TYPE_P (rhs1_type)
	    || TREE_CODE (rhs2_type) != INTEGER_TYPE
	    || !useless_type_conversion_p (lhs_type, rhs1_type)
	    || !useless_type_conversion_p (sizetype, rhs2_type))
	  {
	    error ("type mismatch in pointer plus expression");
	    debug_generic_stmt (lhs_type);
	    debug_generic_stmt (rhs1_type);
	    debug_generic_stmt (rhs2_type);
	    return true;
	  }

	return false;
      }

    case ADDR_EXPR:
      {
	tree ptr_type;

	if (!is_gimple_addressable (rhs1))
	  {
	    error ("invalid operand in unary expression");
	    return true;
	  }

	ptr_type = build_pointer_type (rhs1_type);
	if (!useless_type_conversion_p (lhs_type, ptr_type)
	    /* FIXME: a longstanding wart, &a == &a[0].  */
	    && (TREE_CODE (rhs1_type) != ARRAY_TYPE
		|| !useless_type_conversion_p (lhs_type,
			build_pointer_type (TREE_TYPE (rhs1_type)))))
	  {
	    error ("type mismatch in address expression");
	    debug_generic_stmt (lhs_type);
	    debug_generic_stmt (ptr_type);
	    return true;
	  }

	return verify_types_in_gimple_reference (rhs1);
      }

    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case TRUTH_XOR_EXPR:
      {
      	if (!is_gimple_val (rhs1) || !is_gimple_val (rhs2))
	  {
	    error ("invalid operands in truth expression");
	    return true;
	  }

	/* We allow any kind of integral typed argument and result.  */
	if (!INTEGRAL_TYPE_P (rhs1_type)
	    || !INTEGRAL_TYPE_P (rhs2_type)
	    || !INTEGRAL_TYPE_P (lhs_type))
	  {
	    error ("type mismatch in binary truth expression");
	    debug_generic_expr (lhs_type);
	    debug_generic_expr (rhs1_type);
	    debug_generic_expr (rhs2_type);
	    return true;
	  }

	return false;
      }

    case TRUTH_NOT_EXPR:
      {
	if (!is_gimple_val (rhs1))
	  {
	    error ("invalid operand in unary not");
	    return true;
	  }

	/* For TRUTH_NOT_EXPR we can have any kind of integral
	   typed arguments and results.  */
	if (!INTEGRAL_TYPE_P (rhs1_type)
	    || !INTEGRAL_TYPE_P (lhs_type))
	  {
	    error ("type mismatch in not expression");
	    debug_generic_expr (lhs_type);
	    debug_generic_expr (rhs1_type);
	    return true;
	  }

	return false;
      }

    /* After gimplification we should not have any of these.  */
    case ASM_EXPR:
    case BIND_EXPR:
    case CALL_EXPR:
    case COND_EXPR:
    case TREE_LIST:
    case COMPOUND_EXPR:
    case MODIFY_EXPR:
    case GIMPLE_MODIFY_STMT:
    case INIT_EXPR:
    case GOTO_EXPR:
    case LABEL_EXPR:
    case RETURN_EXPR:
    case TRY_FINALLY_EXPR:
    case TRY_CATCH_EXPR:
    case EH_FILTER_EXPR:
    case STATEMENT_LIST:
      {
	error ("tree node that should already be gimple.");
	return true;
      }

    default:;
    }

  /* Generic handling via classes.  */
  switch (TREE_CODE_CLASS (rhs_code))
    {
    case tcc_unary:
    case tcc_binary:
      {
	size_t i;
	for (i = 1; i < gimple_num_ops (stmt); i++)
	  {
	    tree op = gimple_op (stmt, i);

	    if (!useless_type_conversion_p (lhs_type, TREE_TYPE (op)))
	      {
		error ("non-trivial conversion at assignment");
		debug_generic_expr (lhs_type);
		debug_generic_expr (TREE_TYPE (op));
		return true;
	      }
	  }
	break;
      }

    case tcc_reference:
      return verify_types_in_gimple_reference (rhs1);

    case tcc_comparison:
      {
	if (!is_gimple_val (rhs1) || !is_gimple_val (rhs2))
	  {
	    error ("invalid operands in comparison expression");
	    return true;
	  }

	/* For comparisons we do not have the operations type as the
	   effective type the comparison is carried out in.  Instead
	   we require that either the first operand is trivially
	   convertible into the second, or the other way around.
	   The resulting type of a comparison may be any integral type.
	   Because we special-case pointers to void we allow
	   comparisons of pointers with the same mode as well.  */
	if ((!useless_type_conversion_p (rhs1_type, rhs2_type)
	     && !useless_type_conversion_p (rhs2_type, rhs1_type)
	     && (!POINTER_TYPE_P (rhs1_type)
		 || !POINTER_TYPE_P (rhs2_type)
		 || TYPE_MODE (rhs1_type) != TYPE_MODE (rhs2_type)))
	    || !INTEGRAL_TYPE_P (lhs_type))
	  {
	    error ("type mismatch in comparison expression");
	    debug_generic_expr (lhs_type);
	    debug_generic_expr (rhs1_type);
	    debug_generic_expr (rhs2_type);
	    return true;
	  }
        break;
      }

    default:;
    }

  return false;
}


/* Verify the contents of a GIMPLE_RETURN STMT.  Returns true when there
   is a problem, otherwise false.  */

static bool
verify_types_in_gimple_return (gimple stmt)
{
  tree op = gimple_return_retval (stmt);

  if (op == NULL)
    return false;
  
  return verify_types_in_gimple_op (op);
}


/* Verify the contents of a GIMPLE_SWITCH STMT.  Returns true when there
   is a problem, otherwise false.  */

static bool
verify_types_in_gimple_switch (gimple stmt)
{
  if (!is_gimple_val (gimple_switch_index (stmt)))
    {
      error ("invalid operand to switch statement");
      debug_generic_expr (gimple_switch_index (stmt));
      return true;
    }

  return false;
}


/* Verify the contents of a GIMPLE_PHI.  Returns true if there is a problem,
   and false otherwise.  */

static bool
verify_types_in_gimple_phi (gimple stmt)
{
  size_t i;

  if (verify_types_in_gimple_op (gimple_phi_result (stmt)))
    return true;

  for (i = 0; i < gimple_phi_num_args (stmt); i++)
    if (verify_types_in_gimple_op (gimple_phi_arg_def (stmt, i)))
      return true;

  return false;
}


/* Verify the GIMPLE statement STMT.  Returns true if there is an
   error, otherwise false.  */

static bool
verify_types_in_gimple_stmt (gimple stmt)
{
  if (is_gimple_omp (stmt))
    {
      /* OpenMP directives are validated by the FE and never operated
	 on by the optimizers.  Furthermore, OMP_FOR may contain
	 non-gimple expressions when the main index variable has had
	 its address taken.  This does not affect the loop itself
	 because the header of an OMP_FOR is merely used to determine
	 how to setup the parallel iteration.  */
      return false;
    }

  switch (gimple_code (stmt))
    {
    case GIMPLE_ASSIGN:
      return verify_types_in_gimple_assign (stmt);

    case GIMPLE_LABEL:
      return TREE_CODE (gimple_label_label (stmt)) != LABEL_DECL;

    case GIMPLE_CALL:
      return verify_types_in_gimple_call (stmt);

    case GIMPLE_COND:
      return verify_types_in_gimple_cond (stmt);

    case GIMPLE_GOTO:
      return verify_types_in_gimple_op (gimple_goto_dest (stmt));

    case GIMPLE_NOP:
      return false;

    case GIMPLE_SWITCH:
      return verify_types_in_gimple_switch (stmt);

    case GIMPLE_RETURN:
      return verify_types_in_gimple_return (stmt);

    case GIMPLE_ASM:
      return false;

    case GIMPLE_PHI:
      return verify_types_in_gimple_phi (stmt);

    default:
      gcc_unreachable ();
    }
}


/* Verify the GIMPLE statements inside the sequence STMTS.  */

void
verify_types_in_gimple_seq (gimple_seq stmts)
{
  gimple_stmt_iterator* ittr;

  for (ittr = gsi_start (stmts); !gsi_end_p (ittr); gsi_next (ittr))
    {
      gimple stmt = gsi_stmt (ittr);

      switch (gimple_code (stmt))
        {
          case GIMPLE_BIND:
            verify_types_in_gimple_seq (gimple_bind_body (stmt));
            break;

          case GIMPLE_TRY:
            verify_types_in_gimple_seq (gimple_try_eval (stmt));
            verify_types_in_gimple_seq (gimple_try_cleanup (stmt));
            break;

          case EH_FILTER_EXPR:
            verify_types_in_gimple_seq (gimple_eh_filter_failure (stmt));
            break;

          case GIMPLE_CATCH:
             verify_types_in_gimple_seq (gimple_catch_handler (stmt));
             break;

          case GIMPLE_OMP_CRITICAL:
          case GIMPLE_OMP_CONTINUE:
          case GIMPLE_OMP_MASTER:
          case GIMPLE_OMP_ORDERED:
          case GIMPLE_OMP_SECTION:
          case GIMPLE_OMP_FOR:
          case GIMPLE_OMP_PARALLEL:
          case GIMPLE_OMP_SECTIONS:
          case GIMPLE_OMP_SINGLE:
            break;

          /* Tuples that do not have trees.  */
          case GIMPLE_NOP:
          case GIMPLE_RESX:
          case GIMPLE_OMP_RETURN:
            break;

          default:
            if (verify_types_in_gimple_stmt (stmt))
	      {
		error ("verifier found an invalid statement.");
		debug_gimple_stmt (stmt);
		gcc_unreachable ();
	      }
        }
    }
}


/* Verify STMT, return true if STMT is not in GIMPLE form.
   TODO: Implement type checking.  */

/* FIXME tuples.  */
#if 0
static bool
verify_stmt (tree stmt, bool last_in_block)
{
  tree addr;

  if (OMP_DIRECTIVE_P (stmt))
    {
      /* OpenMP directives are validated by the FE and never operated
	 on by the optimizers.  Furthermore, OMP_FOR may contain
	 non-gimple expressions when the main index variable has had
	 its address taken.  This does not affect the loop itself
	 because the header of an OMP_FOR is merely used to determine
	 how to setup the parallel iteration.  */
      return false;
    }

  if (!is_gimple_stmt (stmt))
    {
      error ("is not a valid GIMPLE statement");
      goto fail;
    }

  addr = walk_tree (&stmt, verify_expr, NULL, NULL);
  if (addr)
    {
      debug_generic_stmt (addr);
      return true;
    }

  /* If the statement is marked as part of an EH region, then it is
     expected that the statement could throw.  Verify that when we
     have optimizations that simplify statements such that we prove
     that they cannot throw, that we update other data structures
     to match.  */
  if (lookup_stmt_eh_region (stmt) >= 0)
    {
      if (!stmt_could_throw_p (stmt))
	{
	  error ("statement marked for throw, but doesn%'t");
	  goto fail;
	}
      if (!last_in_block && stmt_can_throw_internal (stmt))
	{
	  error ("statement marked for throw in middle of block");
	  goto fail;
	}
    }

  return false;

 fail:
  debug_generic_stmt (stmt);
  return true;
}
#endif


/* Return true when the T can be shared.  */

/* FIXME tuples.  */
#if 0
static bool
tree_node_can_be_shared (tree t)
{
  if (IS_TYPE_OR_DECL_P (t)
      || is_gimple_min_invariant (t)
      || TREE_CODE (t) == SSA_NAME
      || t == error_mark_node
      || TREE_CODE (t) == IDENTIFIER_NODE)
    return true;

  if (TREE_CODE (t) == CASE_LABEL_EXPR)
    return true;

  while (((TREE_CODE (t) == ARRAY_REF || TREE_CODE (t) == ARRAY_RANGE_REF)
	   && is_gimple_min_invariant (TREE_OPERAND (t, 1)))
	 || TREE_CODE (t) == COMPONENT_REF
	 || TREE_CODE (t) == REALPART_EXPR
	 || TREE_CODE (t) == IMAGPART_EXPR)
    t = TREE_OPERAND (t, 0);

  if (DECL_P (t))
    return true;

  return false;
}
#endif


/* Called via walk_trees.  Verify tree sharing.  */

/* FIXME tuples.  */
#if 0
static tree
verify_node_sharing (tree * tp, int *walk_subtrees, void *data)
{
  struct pointer_set_t *visited = (struct pointer_set_t *) data;

  if (tree_node_can_be_shared (*tp))
    {
      *walk_subtrees = false;
      return NULL;
    }

  if (pointer_set_insert (visited, *tp))
    return *tp;

  return NULL;
}
#endif


/* Helper function for verify_gimple_tuples.  */

/* FIXME tuples.  */
#if 0
static tree
verify_gimple_tuples_1 (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
			 void *data ATTRIBUTE_UNUSED)
{
  switch (TREE_CODE (*tp))
    {
    case MODIFY_EXPR:
      error ("unexpected non-tuple");
      debug_tree (*tp);
      gcc_unreachable ();
      return NULL_TREE;

    default:
      return NULL_TREE;
    }
}
#endif

/* Verify that there are no trees that should have been converted to
   gimple tuples.  Return true if T contains a node that should have
   been converted to a gimple tuple, but hasn't.  */

/* FIXME tuples.  */
#if 0
static bool
verify_gimple_tuples (tree t)
{
  return walk_tree (&t, verify_gimple_tuples_1, NULL, NULL) != NULL;
}
#endif

/* FIXME tuples.  */
#if 0
static bool eh_error_found;
static int
verify_eh_throw_stmt_node (void **slot, void *data)
{
  struct throw_stmt_node *node = (struct throw_stmt_node *)*slot;
  struct pointer_set_t *visited = (struct pointer_set_t *) data;

  if (!pointer_set_contains (visited, node->stmt))
    {
      error ("Dead STMT in EH table");
      debug_generic_stmt (node->stmt);
      eh_error_found = true;
    }
  return 0;
}
#endif

/* Verify the GIMPLE statement chain.  */

void
verify_stmts (void)
{
  /* FIXME tuples.  */
#if 0
  basic_block bb;
  gimple_stmt_iterator gsi;
  bool err = false;
  struct pointer_set_t *visited, *visited_stmts;
  tree addr;

  timevar_push (TV_TREE_STMT_VERIFY);
  visited = pointer_set_create ();
  visited_stmts = pointer_set_create ();

  FOR_EACH_BB (bb)
    {
      tree phi;
      int i;

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  int phi_num_args = PHI_NUM_ARGS (phi);

	  pointer_set_insert (visited_stmts, phi);
	  if (gimple_bb (phi) != bb)
	    {
	      error ("gimple_bb (phi) is set to a wrong basic block");
	      err |= true;
	    }

	  for (i = 0; i < phi_num_args; i++)
	    {
	      tree t = PHI_ARG_DEF (phi, i);
	      tree addr;

	      /* Addressable variables do have SSA_NAMEs but they
		 are not considered gimple values.  */
	      if (TREE_CODE (t) != SSA_NAME
		  && TREE_CODE (t) != FUNCTION_DECL
		  && !is_gimple_val (t))
		{
		  error ("PHI def is not a GIMPLE value");
		  debug_generic_stmt (phi);
		  debug_generic_stmt (t);
		  err |= true;
		}

	      addr = walk_tree (&t, verify_expr, (void *) 1, NULL);
	      if (addr)
		{
		  debug_generic_stmt (addr);
		  err |= true;
		}

	      addr = walk_tree (&t, verify_node_sharing, visited, NULL);
	      if (addr)
		{
		  error ("incorrect sharing of tree nodes");
		  debug_generic_stmt (phi);
		  debug_generic_stmt (addr);
		  err |= true;
		}
	    }
	}

      for (gsi = gsi_start (bb); !gsi_end_p (gsi); )
	{
	  tree stmt = gsi_stmt (gsi);

	  pointer_set_insert (visited_stmts, stmt);
	  err |= verify_gimple_tuples (stmt);

	  if (gimple_bb (stmt) != bb)
	    {
	      error ("gimple_bb (stmt) is set to a wrong basic block");
	      err |= true;
	    }

	  gsi_next (&gsi);
	  err |= verify_stmt (stmt, gsi_end_p (gsi));
	  addr = walk_tree (&stmt, verify_node_sharing, visited, NULL);
	  if (addr)
	    {
	      error ("incorrect sharing of tree nodes");
	      debug_generic_stmt (stmt);
	      debug_generic_stmt (addr);
	      err |= true;
	    }
	}
    }
  eh_error_found = false;
  if (get_eh_throw_stmt_table (cfun))
    htab_traverse (get_eh_throw_stmt_table (cfun),
		   verify_eh_throw_stmt_node,
		   visited_stmts);

  if (err | eh_error_found)
    internal_error ("verify_stmts failed");

  pointer_set_destroy (visited);
  pointer_set_destroy (visited_stmts);
  verify_histograms ();
  timevar_pop (TV_TREE_STMT_VERIFY);
#endif
}


/* Verifies that the flow information is OK.  */

static int
tree_verify_flow_info (void)
{
  int err = 0;
  basic_block bb;
  gimple_stmt_iterator *gsi;
  gimple stmt;
  edge e;
  edge_iterator ei;

  if (ENTRY_BLOCK_PTR->il.gimple)
    {
      error ("ENTRY_BLOCK has IL associated with it");
      err = 1;
    }

  if (EXIT_BLOCK_PTR->il.gimple)
    {
      error ("EXIT_BLOCK has IL associated with it");
      err = 1;
    }

  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
    if (e->flags & EDGE_FALLTHRU)
      {
	error ("fallthru to exit from bb %d", e->src->index);
	err = 1;
      }

  FOR_EACH_BB (bb)
    {
      bool found_ctrl_stmt = false;

      stmt = NULL;

      /* Skip labels on the start of basic block.  */
      for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (gsi))
	{
	  tree label;
	  gimple prev_stmt = stmt;

	  stmt = gsi_stmt (gsi);

	  if (gimple_code (stmt) != GIMPLE_LABEL)
	    break;

	  label = gimple_label_label (stmt);
	  if (prev_stmt && DECL_NONLOCAL (label))
	    {
	      error ("nonlocal label ");
	      print_generic_expr (stderr, label, 0);
	      fprintf (stderr, " is not first in a sequence of labels in bb %d",
		       bb->index);
	      err = 1;
	    }

	  if (label_to_block (label) != bb)
	    {
	      error ("label ");
	      print_generic_expr (stderr, label, 0);
	      fprintf (stderr, " to block does not match in bb %d",
		       bb->index);
	      err = 1;
	    }

	  if (decl_function_context (label)
	      != current_function_decl)
	    {
	      error ("label ");
	      print_generic_expr (stderr, label, 0);
	      fprintf (stderr, " has incorrect context in bb %d",
		       bb->index);
	      err = 1;
	    }
	}

      /* Verify that body of basic block BB is free of control flow.  */
      for (; !gsi_end_p (gsi); gsi_next (gsi))
	{
	  gimple stmt = gsi_stmt (gsi);

	  if (found_ctrl_stmt)
	    {
	      error ("control flow in the middle of basic block %d",
		     bb->index);
	      err = 1;
	    }

	  if (stmt_ends_bb_p (stmt))
	    found_ctrl_stmt = true;

	  if (gimple_code (stmt) == GIMPLE_LABEL)
	    {
	      error ("label ");
	      print_generic_expr (stderr, gimple_label_label (stmt), 0);
	      fprintf (stderr, " in the middle of basic block %d", bb->index);
	      err = 1;
	    }
	}

      gsi = gsi_last (bb_seq (bb));
      if (gsi_end_p (gsi))
	continue;

      stmt = gsi_stmt (gsi);

      /* FIXME tuples.  */
#if 0
      err |= verify_eh_edges (stmt);
#else
      gcc_unreachable ();
#endif

      if (is_ctrl_stmt (stmt))
	{
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->flags & EDGE_FALLTHRU)
	      {
		error ("fallthru edge after a control statement in bb %d",
		       bb->index);
		err = 1;
	      }
	}

      if (gimple_code (stmt) != GIMPLE_COND)
	{
	  /* Verify that there are no edges with EDGE_TRUE/FALSE_FLAG set
	     after anything else but if statement.  */
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->flags & (EDGE_TRUE_VALUE | EDGE_FALSE_VALUE))
	      {
		error ("true/false edge after a non-GIMPLE_COND in bb %d",
		       bb->index);
		err = 1;
	      }
	}

      switch (gimple_code (stmt))
	{
	case GIMPLE_COND:
	  {
	    edge true_edge;
	    edge false_edge;
  
	    extract_true_false_edges_from_block (bb, &true_edge, &false_edge);

	    if (!true_edge
		|| !false_edge
		|| !(true_edge->flags & EDGE_TRUE_VALUE)
		|| !(false_edge->flags & EDGE_FALSE_VALUE)
		|| (true_edge->flags & (EDGE_FALLTHRU | EDGE_ABNORMAL))
		|| (false_edge->flags & (EDGE_FALLTHRU | EDGE_ABNORMAL))
		|| EDGE_COUNT (bb->succs) >= 3)
	      {
		error ("wrong outgoing edge flags at end of bb %d",
		       bb->index);
		err = 1;
	      }
	  }
	  break;

	case GIMPLE_GOTO:
	  if (simple_goto_p (stmt))
	    {
	      error ("explicit goto at end of bb %d", bb->index);
	      err = 1;
	    }
	  else
	    {
	      /* FIXME.  We should double check that the labels in the
		 destination blocks have their address taken.  */
	      FOR_EACH_EDGE (e, ei, bb->succs)
		if ((e->flags & (EDGE_FALLTHRU | EDGE_TRUE_VALUE
				 | EDGE_FALSE_VALUE))
		    || !(e->flags & EDGE_ABNORMAL))
		  {
		    error ("wrong outgoing edge flags at end of bb %d",
			   bb->index);
		    err = 1;
		  }
	    }
	  break;

	case GIMPLE_RETURN:
	  if (!single_succ_p (bb)
	      || (single_succ_edge (bb)->flags
		  & (EDGE_FALLTHRU | EDGE_ABNORMAL
		     | EDGE_TRUE_VALUE | EDGE_FALSE_VALUE)))
	    {
	      error ("wrong outgoing edge flags at end of bb %d", bb->index);
	      err = 1;
	    }
	  if (single_succ (bb) != EXIT_BLOCK_PTR)
	    {
	      error ("return edge does not point to exit in bb %d",
		     bb->index);
	      err = 1;
	    }
	  break;

	case GIMPLE_SWITCH:
	  {
	    tree prev;
	    edge e;
	    size_t i, n;

	    n = gimple_switch_num_labels (stmt);

	    /* Mark all the destination basic blocks.  */
	    for (i = 0; i < n; ++i)
	      {
		tree lab = CASE_LABEL (gimple_switch_label (stmt, i));
		basic_block label_bb = label_to_block (lab);
		gcc_assert (!label_bb->aux || label_bb->aux == (void *)1);
		label_bb->aux = (void *)1;
	      }

	    /* Verify that the case labels are sorted.  */
	    prev = gimple_switch_label (stmt, 0);
	    for (i = 1; i < n; ++i)
	      {
		tree c = gimple_switch_label (stmt, i);
		if (!CASE_LOW (c))
		  {
		    error ("found default case not at the start of case vector");
		    err = 1;
		    continue;
		  }
		if (! tree_int_cst_lt (CASE_LOW (prev), CASE_LOW (c)))
		  {
		    error ("case labels not sorted: ");
		    print_generic_expr (stderr, prev, 0);
		    fprintf (stderr," is greater than ");
		    print_generic_expr (stderr, c, 0);
		    fprintf (stderr," but comes before it.\n");
		    err = 1;
		  }
		prev = c;
	      }

	    if (CASE_LOW (gimple_switch_default_label (stmt)))
	      {
		error ("no default case found at end of case vector");
		err = 1;
	      }

	    FOR_EACH_EDGE (e, ei, bb->succs)
	      {
		if (!e->dest->aux)
		  {
		    error ("extra outgoing edge %d->%d",
			   bb->index, e->dest->index);
		    err = 1;
		  }

		e->dest->aux = (void *)2;
		if ((e->flags & (EDGE_FALLTHRU | EDGE_ABNORMAL
				 | EDGE_TRUE_VALUE | EDGE_FALSE_VALUE)))
		  {
		    error ("wrong outgoing edge flags at end of bb %d",
			   bb->index);
		    err = 1;
		  }
	      }

	    /* Check that we have all of them.  */
	    for (i = 0; i < n; ++i)
	      {
		tree lab = CASE_LABEL (gimple_switch_label (stmt, i));
		basic_block label_bb = label_to_block (lab);

		if (label_bb->aux != (void *)2)
		  {
		    error ("missing edge %i->%i", bb->index, label_bb->index);
		    err = 1;
		  }
	      }

	    FOR_EACH_EDGE (e, ei, bb->succs)
	      e->dest->aux = (void *)0;
	  }

	default: ;
	}
    }

  if (dom_info_state (CDI_DOMINATORS) >= DOM_NO_FAST_QUERY)
    verify_dominators (CDI_DOMINATORS);

  return err;
}


/* Updates phi nodes after creating a forwarder block joined
   by edge FALLTHRU.  */

static void
tree_make_forwarder_block (edge fallthru)
{
  edge e;
  edge_iterator ei;
  basic_block dummy, bb;
  tree var;
  gimple_stmt_iterator *gsi;

  dummy = fallthru->src;
  bb = fallthru->dest;

  if (single_pred_p (bb))
    return;

  /* If we redirected a branch we must create new PHI nodes at the
     start of BB.  */
  for (gsi = gsi_start (phi_nodes (dummy)); !gsi_end_p (gsi); gsi_next (gsi))
    {
      gimple phi, new_phi;
      
      phi = gsi_stmt (gsi);
      var = gimple_phi_result (phi);
      new_phi = create_phi_node (var, bb);
      SSA_NAME_DEF_STMT (var) = new_phi;
      gimple_phi_set_result (phi, make_ssa_name (SSA_NAME_VAR (var), phi));
      add_phi_arg (new_phi, gimple_phi_result (phi), fallthru);
    }

  /* Ensure that the PHI node chain is in the same order.  */
  set_phi_nodes (bb, gimple_seq_reverse (phi_nodes (bb)));

  /* Add the arguments we have stored on edges.  */
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if (e == fallthru)
	continue;

      flush_pending_stmts (e);
    }
}


/* Return a non-special label in the head of basic block BLOCK.
   Create one if it doesn't exist.  */

tree
tree_block_label (basic_block bb ATTRIBUTE_UNUSED)
{
/* FIXME tuples.  */
#if 0
  gimple_stmt_iterator i, s = gsi_start (bb);
  bool first = true;
  tree label, stmt;

  for (i = s; !gsi_end_p (i); first = false, gsi_next (&i))
    {
      stmt = gsi_stmt (i);
      if (TREE_CODE (stmt) != LABEL_EXPR)
	break;
      label = LABEL_EXPR_LABEL (stmt);
      if (!DECL_NONLOCAL (label))
	{
	  if (!first)
	    gsi_move_before (&i, &s);
	  return label;
	}
    }

  label = create_artificial_label ();
  stmt = build1 (LABEL_EXPR, void_type_node, label);
  gsi_insert_before (&s, stmt, GSI_NEW_STMT);
  return label;
#else
  gcc_unreachable ();
#endif
}


/* FIXME tuples.  */
#if 0
/* Attempt to perform edge redirection by replacing a possibly complex
   jump instruction by a goto or by removing the jump completely.
   This can apply only if all edges now point to the same block.  The
   parameters and return values are equivalent to
   redirect_edge_and_branch.  */

static edge
tree_try_redirect_by_replacing_jump (edge e, basic_block target)
{
  basic_block src = e->src;
  gimple_stmt_iterator *i;
  gimple stmt;

  /* We can replace or remove a complex jump only when we have exactly
     two edges.  */
  if (EDGE_COUNT (src->succs) != 2
      /* Verify that all targets will be TARGET.  Specifically, the
	 edge that is not E must also go to TARGET.  */
      || EDGE_SUCC (src, EDGE_SUCC (src, 0) == e)->dest != target)
    return NULL;

  i = gsi_last (bb_seq (src));
  if (gsi_end_p (i))
    return NULL;

  stmt = gsi_stmt (i);

  if (gimple_code (stmt) == GIMPLE_COND || gimple_code (stmt) == SWITCH_EXPR)
    {
      gsi_remove (i, true);
      e = ssa_redirect_edge (e, target);
      e->flags = EDGE_FALLTHRU;
      return e;
    }

  return NULL;
}


/* Redirect E to DEST.  Return NULL on failure.  Otherwise, return the
   edge representing the redirected branch.  */

static edge
tree_redirect_edge_and_branch (edge e, basic_block dest)
{
  basic_block bb = e->src;
  gimple_stmt_iterator *gsi;
  edge ret;
  gimple stmt;

  if (e->flags & EDGE_ABNORMAL)
    return NULL;

  if (e->src != ENTRY_BLOCK_PTR
      && (ret = tree_try_redirect_by_replacing_jump (e, dest)))
    return ret;

  if (e->dest == dest)
    return NULL;

  gsi = gsi_last (bb_seq (bb));
  stmt = gsi_end_p (gsi) ? NULL : gsi_stmt (gsi);

  switch (stmt ? gimple_code (stmt) : ERROR_MARK)
    {
    case GIMPLE_COND:
      /* For COND_EXPR, we only need to redirect the edge.  */
      break;

    case GIMPLE_GOTO:
      /* No non-abnormal edges should lead from a non-simple goto, and
	 simple ones should be represented implicitly.  */
      gcc_unreachable ();

    case GIMPLE_SWITCH:
      {
        tree cases = get_cases_for_edge (e, stmt);
	tree label = tree_block_label (dest);

	/* If we have a list of cases associated with E, then use it
	   as it's a lot faster than walking the entire case vector.  */
	if (cases)
	  {
	    edge e2 = find_edge (e->src, dest);
	    tree last, first;

	    first = cases;
	    while (cases)
	      {
		last = cases;
		CASE_LABEL (cases) = label;
		cases = TREE_CHAIN (cases);
	      }

	    /* If there was already an edge in the CFG, then we need
	       to move all the cases associated with E to E2.  */
	    if (e2)
	      {
		tree cases2 = get_cases_for_edge (e2, stmt);

		TREE_CHAIN (last) = TREE_CHAIN (cases2);
		TREE_CHAIN (cases2) = first;
	      }
	  }
	else
	  {
	    size_t i, n = gimple_switch_num_labels (stmt);

	    for (i = 0; i < n; i++)
	      {
		tree elt = gimple_switch_label (stmt, i);
		if (label_to_block (CASE_LABEL (elt)) == e->dest)
		  CASE_LABEL (elt) = label;
	      }
	  }

	break;
      }

    case GIMPLE_RETURN:
      gsi_remove (gsi, true);
      e->flags |= EDGE_FALLTHRU;
      break;

    case OMP_RETURN:
    case OMP_CONTINUE:
    case OMP_SECTIONS_SWITCH:
    case OMP_FOR:
      /* The edges from OMP constructs can be simply redirected.  */
      break;

    default:
      /* Otherwise it must be a fallthru edge, and we don't need to
	 do anything besides redirecting it.  */
      gcc_assert (e->flags & EDGE_FALLTHRU);
      break;
    }

  /* Update/insert PHI nodes as necessary.  */

  /* Now update the edges in the CFG.  */
  e = ssa_redirect_edge (e, dest);

  return e;
}
#endif

/* Returns true if it is possible to remove edge E by redirecting
   it to the destination of the other edge from E->src.  */

static bool
tree_can_remove_branch_p (const_edge e)
{
  if (e->flags & EDGE_ABNORMAL)
    return false;

  return true;
}

/* Simple wrapper, as we can always redirect fallthru edges.  */

/* FIXME tuples.  */
#if 0
static basic_block
tree_redirect_edge_and_branch_force (edge e, basic_block dest)
{
  e = tree_redirect_edge_and_branch (e, dest);
  gcc_assert (e);

  return NULL;
}
#endif


/* Splits basic block BB after statement STMT (but at least after the
   labels).  If STMT is NULL, BB is split just after the labels.  */

/* FIXME tuples.  */
#if 0
static basic_block
gimple_split_block (basic_block bb, void *stmt)
{
  gimple_stmt_iterator gsi;
  gimple_stmt_iterator *gsi_tgt;
  tree act, list;
  basic_block new_bb;
  edge e;
  edge_iterator ei;

  new_bb = create_empty_bb (bb);

  /* Redirect the outgoing edges.  */
  new_bb->succs = bb->succs;
  bb->succs = NULL;
  FOR_EACH_EDGE (e, ei, new_bb->succs)
    e->src = new_bb;

  if (stmt && TREE_CODE ((tree) stmt) == LABEL_EXPR)
    stmt = NULL;

  /* Move everything from GSI to the new basic block.  */
  for (gsi = gsi_start (bb); !gsi_end_p (gsi); gsi_next (&gsi))
    {
      act = gsi_stmt (gsi);
      if (TREE_CODE (act) == LABEL_EXPR)
	continue;

      if (!stmt)
	break;

      if (stmt == act)
	{
	  gsi_next (&gsi);
	  break;
	}
    }

  if (gsi_end_p (gsi))
    return new_bb;

  /* Split the statement list - avoid re-creating new containers as this
     brings ugly quadratic memory consumption in the inliner.  
     (We are still quadratic since we need to update stmt BB pointers,
     sadly.)  */
  list = gsi_split_seq_before (&gsi.gsi);
  set_bb_seq (new_bb, list);
  for (gsi_tgt = gsi_start (list);
       !gsi_end_p (gsi_tgt); gsi_next (&gsi_tgt))
    change_bb_for_stmt (gsi_stmt (gsi_tgt), new_bb);

  return new_bb;
}
#endif


/* Moves basic block BB after block AFTER.  */

static bool
tree_move_block_after (basic_block bb, basic_block after)
{
  if (bb->prev_bb == after)
    return true;

  unlink_block (bb);
  link_block (bb, after);

  return true;
}


/* Return true if basic_block can be duplicated.  */

static bool
tree_can_duplicate_bb_p (const_basic_block bb ATTRIBUTE_UNUSED)
{
  return true;
}


/* Create a duplicate of the basic block BB.  NOTE: This does not
   preserve SSA form.  */

/* FIXME tuples.  */
#if 0
static basic_block
tree_duplicate_bb (basic_block bb)
{
  basic_block new_bb;
  gimple_stmt_iterator gsi, gsi_tgt;
  tree phi;

  new_bb = create_empty_bb (EXIT_BLOCK_PTR->prev_bb);

  /* Copy the PHI nodes.  We ignore PHI node arguments here because
     the incoming edges have not been setup yet.  */
  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
    {
      tree copy = create_phi_node (PHI_RESULT (phi), new_bb);
      create_new_def_for (PHI_RESULT (copy), copy, PHI_RESULT_PTR (copy));
    }

  /* Keep the chain of PHI nodes in the same order so that they can be
     updated by ssa_redirect_edge.  */
  set_phi_nodes (new_bb, phi_reverse (phi_nodes (new_bb)));

  gsi_tgt = gsi_start (new_bb);
  for (gsi = gsi_start (bb); !gsi_end_p (gsi); gsi_next (&gsi))
    {
      def_operand_p def_p;
      ssa_op_iter op_iter;
      tree stmt, copy;
      int region;

      stmt = gsi_stmt (gsi);
      if (TREE_CODE (stmt) == LABEL_EXPR)
	continue;

      /* Create a new copy of STMT and duplicate STMT's virtual
	 operands.  */
      copy = unshare_expr (stmt);
      gsi_insert_after (&gsi_tgt, copy, GSI_NEW_STMT);
      copy_virtual_operands (copy, stmt);
      region = lookup_stmt_eh_region (stmt);
      if (region >= 0)
	add_stmt_to_eh_region (copy, region);
      gimple_duplicate_stmt_histograms (cfun, copy, cfun, stmt);

      /* Create new names for all the definitions created by COPY and
	 add replacement mappings for each new name.  */
      FOR_EACH_SSA_DEF_OPERAND (def_p, copy, op_iter, SSA_OP_ALL_DEFS)
	create_new_def_for (DEF_FROM_PTR (def_p), copy, def_p);
    }

  return new_bb;
}
#endif

/* Adds phi node arguments for edge E_COPY after basic block duplication.  */

static void
add_phi_args_after_copy_edge (edge e_copy ATTRIBUTE_UNUSED)
{
/* FIXME tuples.  */
#if 0
  basic_block bb, bb_copy = e_copy->src, dest;
  edge e;
  edge_iterator ei;
  tree phi, phi_copy, phi_next, def;

  if (!phi_nodes (e_copy->dest))
    return;

  bb = bb_copy->flags & BB_DUPLICATED ? get_bb_original (bb_copy) : bb_copy;

  if (e_copy->dest->flags & BB_DUPLICATED)
    dest = get_bb_original (e_copy->dest);
  else
    dest = e_copy->dest;

  e = find_edge (bb, dest);
  if (!e)
    {
      /* During loop unrolling the target of the latch edge is copied.
	 In this case we are not looking for edge to dest, but to
	 duplicated block whose original was dest.  */
      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if ((e->dest->flags & BB_DUPLICATED)
	      && get_bb_original (e->dest) == dest)
	    break;
	}

      gcc_assert (e != NULL);
    }

  for (phi = phi_nodes (e->dest), phi_copy = phi_nodes (e_copy->dest);
       phi;
       phi = phi_next, phi_copy = PHI_CHAIN (phi_copy))
    {
      phi_next = PHI_CHAIN (phi);
      def = PHI_ARG_DEF_FROM_EDGE (phi, e);
      add_phi_arg (phi_copy, def, e_copy);
    }
#else
  gcc_unreachable ();
#endif
}


/* Basic block BB_COPY was created by code duplication.  Add phi node
   arguments for edges going out of BB_COPY.  The blocks that were
   duplicated have BB_DUPLICATED set.  */

void
add_phi_args_after_copy_bb (basic_block bb_copy ATTRIBUTE_UNUSED)
{
  /* FIXME tuples.  */
#if 0
  basic_block bb, dest;
  edge e, e_copy;
  edge_iterator ei;
  edge e_copy;

  FOR_EACH_EDGE (e_copy, ei, bb_copy->succs)
    {
      add_phi_args_after_copy_edge (e_copy);
    }
#endif
}

/* Blocks in REGION_COPY array of length N_REGION were created by
   duplication of basic blocks.  Add phi node arguments for edges
   going from these blocks.  If E_COPY is not NULL, also add
   phi node arguments for its destination.*/

void
add_phi_args_after_copy (basic_block *region_copy, unsigned n_region,
			 edge e_copy)
{
  unsigned i;

  for (i = 0; i < n_region; i++)
    region_copy[i]->flags |= BB_DUPLICATED;

  for (i = 0; i < n_region; i++)
    add_phi_args_after_copy_bb (region_copy[i]);
  if (e_copy)
    add_phi_args_after_copy_edge (e_copy);

  for (i = 0; i < n_region; i++)
    region_copy[i]->flags &= ~BB_DUPLICATED;
}

/* Duplicates a REGION (set of N_REGION basic blocks) with just a single
   important exit edge EXIT.  By important we mean that no SSA name defined
   inside region is live over the other exit edges of the region.  All entry
   edges to the region must go to ENTRY->dest.  The edge ENTRY is redirected
   to the duplicate of the region.  SSA form, dominance and loop information
   is updated.  The new basic blocks are stored to REGION_COPY in the same
   order as they had in REGION, provided that REGION_COPY is not NULL.
   The function returns false if it is unable to copy the region,
   true otherwise.  */

bool
tree_duplicate_sese_region (edge entry, edge exit,
			    basic_block *region, unsigned n_region,
			    basic_block *region_copy)
{
  unsigned i;
  bool free_region_copy = false, copying_header = false;
  struct loop *loop = entry->dest->loop_father;
  edge exit_copy;
  VEC (basic_block, heap) *doms;
  edge redirected;
  int total_freq = 0, entry_freq = 0;
  gcov_type total_count = 0, entry_count = 0;

  if (!can_copy_bbs_p (region, n_region))
    return false;

  /* Some sanity checking.  Note that we do not check for all possible
     missuses of the functions.  I.e. if you ask to copy something weird,
     it will work, but the state of structures probably will not be
     correct.  */
  for (i = 0; i < n_region; i++)
    {
      /* We do not handle subloops, i.e. all the blocks must belong to the
	 same loop.  */
      if (region[i]->loop_father != loop)
	return false;

      if (region[i] != entry->dest
	  && region[i] == loop->header)
	return false;
    }

  set_loop_copy (loop, loop);

  /* In case the function is used for loop header copying (which is the primary
     use), ensure that EXIT and its copy will be new latch and entry edges.  */
  if (loop->header == entry->dest)
    {
      copying_header = true;
      set_loop_copy (loop, loop_outer (loop));

      if (!dominated_by_p (CDI_DOMINATORS, loop->latch, exit->src))
	return false;

      for (i = 0; i < n_region; i++)
	if (region[i] != exit->src
	    && dominated_by_p (CDI_DOMINATORS, region[i], exit->src))
	  return false;
    }

  if (!region_copy)
    {
      region_copy = XNEWVEC (basic_block, n_region);
      free_region_copy = true;
    }

  /* FIXME tuples */
#if 0
  gcc_assert (!need_ssa_update_p ());
#else
  gcc_unreachable ();
#endif

  /* Record blocks outside the region that are dominated by something
     inside.  */
  doms = NULL;
  initialize_original_copy_tables ();

  doms = get_dominated_by_region (CDI_DOMINATORS, region, n_region);

  if (entry->dest->count)
    {
      total_count = entry->dest->count;
      entry_count = entry->count;
      /* Fix up corner cases, to avoid division by zero or creation of negative
	 frequencies.  */
      if (entry_count > total_count)
	entry_count = total_count;
    }
  else
    {
      total_freq = entry->dest->frequency;
      entry_freq = EDGE_FREQUENCY (entry);
      /* Fix up corner cases, to avoid division by zero or creation of negative
	 frequencies.  */
      if (total_freq == 0)
	total_freq = 1;
      else if (entry_freq > total_freq)
	entry_freq = total_freq;
    }

  copy_bbs (region, n_region, region_copy, &exit, 1, &exit_copy, loop,
	    split_edge_bb_loc (entry));
  if (total_count)
    {
      scale_bbs_frequencies_gcov_type (region, n_region,
				       total_count - entry_count,
				       total_count);
      scale_bbs_frequencies_gcov_type (region_copy, n_region, entry_count,
				       total_count);
    }
  else
    {
      scale_bbs_frequencies_int (region, n_region, total_freq - entry_freq,
				 total_freq);
      scale_bbs_frequencies_int (region_copy, n_region, entry_freq, total_freq);
    }

  if (copying_header)
    {
      loop->header = exit->dest;
      loop->latch = exit->src;
    }

  /* Redirect the entry and add the phi node arguments.  */
  redirected = redirect_edge_and_branch (entry, get_bb_copy (entry->dest));
  gcc_assert (redirected != NULL);
  flush_pending_stmts (entry);

  /* Concerning updating of dominators:  We must recount dominators
     for entry block and its copy.  Anything that is outside of the
     region, but was dominated by something inside needs recounting as
     well.  */
  set_immediate_dominator (CDI_DOMINATORS, entry->dest, entry->src);
  VEC_safe_push (basic_block, heap, doms, get_bb_original (entry->dest));
  iterate_fix_dominators (CDI_DOMINATORS, doms, false);
  VEC_free (basic_block, heap, doms);

  /* Add the other PHI node arguments.  */
  add_phi_args_after_copy (region_copy, n_region, NULL);

  /* FIXME tuples.  */
#if 0
  /* Update the SSA web.  */
  update_ssa (TODO_update_ssa);
#else
  gcc_unreachable ();
#endif

  if (free_region_copy)
    free (region_copy);

  free_original_copy_tables ();
  return true;
}

/* Duplicates REGION consisting of N_REGION blocks.  The new blocks
   are stored to REGION_COPY in the same order in that they appear
   in REGION, if REGION_COPY is not NULL.  ENTRY is the entry to
   the region, EXIT an exit from it.  The condition guarding EXIT
   is moved to ENTRY.  Returns true if duplication succeeds, false
   otherwise.

   For example, 
 
   some_code;
   if (cond)
     A;
   else
     B;

   is transformed to

   if (cond)
     {
       some_code;
       A;
     }
   else
     {
       some_code;
       B;
     }
*/

bool
tree_duplicate_sese_tail (edge entry ATTRIBUTE_UNUSED, edge exit ATTRIBUTE_UNUSED,
			  basic_block *region ATTRIBUTE_UNUSED, unsigned n_region ATTRIBUTE_UNUSED,
			  basic_block *region_copy ATTRIBUTE_UNUSED)
{
  /* FIXME tuples.  */
#if 0
  unsigned i;
  bool free_region_copy = false;
  struct loop *loop = exit->dest->loop_father;
  struct loop *orig_loop = entry->dest->loop_father;
  basic_block switch_bb, entry_bb, nentry_bb;
  VEC (basic_block, heap) *doms;
  int total_freq = 0, exit_freq = 0;
  gcov_type total_count = 0, exit_count = 0;
  edge exits[2], nexits[2], e;
  block_stmt_iterator bsi;
  tree cond;
  edge sorig, snew;

  gcc_assert (EDGE_COUNT (exit->src->succs) == 2);
  exits[0] = exit;
  exits[1] = EDGE_SUCC (exit->src, EDGE_SUCC (exit->src, 0) == exit);

  if (!can_copy_bbs_p (region, n_region))
    return false;

  /* Some sanity checking.  Note that we do not check for all possible
     missuses of the functions.  I.e. if you ask to copy something weird
     (e.g., in the example, if there is a jump from inside to the middle
     of some_code, or come_code defines some of the values used in cond)
     it will work, but the resulting code will not be correct.  */
  for (i = 0; i < n_region; i++)
    {
      /* We do not handle subloops, i.e. all the blocks must belong to the
	 same loop.  */
      if (region[i]->loop_father != orig_loop)
	return false;

      if (region[i] == orig_loop->latch)
	return false;
    }

  initialize_original_copy_tables ();
  set_loop_copy (orig_loop, loop);

  if (!region_copy)
    {
      region_copy = XNEWVEC (basic_block, n_region);
      free_region_copy = true;
    }

  gcc_assert (!need_ssa_update_p ());

  /* Record blocks outside the region that are dominated by something
     inside.  */
  doms = get_dominated_by_region (CDI_DOMINATORS, region, n_region);

  if (exit->src->count)
    {
      total_count = exit->src->count;
      exit_count = exit->count;
      /* Fix up corner cases, to avoid division by zero or creation of negative
	 frequencies.  */
      if (exit_count > total_count)
	exit_count = total_count;
    }
  else
    {
      total_freq = exit->src->frequency;
      exit_freq = EDGE_FREQUENCY (exit);
      /* Fix up corner cases, to avoid division by zero or creation of negative
	 frequencies.  */
      if (total_freq == 0)
	total_freq = 1;
      if (exit_freq > total_freq)
	exit_freq = total_freq;
    }

  copy_bbs (region, n_region, region_copy, exits, 2, nexits, orig_loop,
	    split_edge_bb_loc (exit));
  if (total_count)
    {
      scale_bbs_frequencies_gcov_type (region, n_region,
				       total_count - exit_count,
				       total_count);
      scale_bbs_frequencies_gcov_type (region_copy, n_region, exit_count,
				       total_count);
    }
  else
    {
      scale_bbs_frequencies_int (region, n_region, total_freq - exit_freq,
				 total_freq);
      scale_bbs_frequencies_int (region_copy, n_region, exit_freq, total_freq);
    }

  /* Create the switch block, and put the exit condition to it.  */
  entry_bb = entry->dest;
  nentry_bb = get_bb_copy (entry_bb);
  if (!last_stmt (entry->src)
      || !stmt_ends_bb_p (last_stmt (entry->src)))
    switch_bb = entry->src;
  else
    switch_bb = split_edge (entry);
  set_immediate_dominator (CDI_DOMINATORS, nentry_bb, switch_bb);

  bsi = bsi_last (switch_bb);
  cond = last_stmt (exit->src);
  gcc_assert (TREE_CODE (cond) == COND_EXPR);
  bsi_insert_after (&bsi, unshare_expr (cond), BSI_NEW_STMT);

  sorig = single_succ_edge (switch_bb);
  sorig->flags = exits[1]->flags;
  snew = make_edge (switch_bb, nentry_bb, exits[0]->flags);

  /* Register the new edge from SWITCH_BB in loop exit lists.  */
  rescan_loop_exit (snew, true, false);

  /* Add the PHI node arguments.  */
  add_phi_args_after_copy (region_copy, n_region, snew);

  /* Get rid of now superfluous conditions and associated edges (and phi node
     arguments).  */
  e = redirect_edge_and_branch (exits[0], exits[1]->dest);
  PENDING_STMT (e) = NULL_TREE;
  e = redirect_edge_and_branch (nexits[1], nexits[0]->dest);
  PENDING_STMT (e) = NULL_TREE;

  /* Anything that is outside of the region, but was dominated by something
     inside needs to update dominance info.  */
  iterate_fix_dominators (CDI_DOMINATORS, doms, false);
  VEC_free (basic_block, heap, doms);

  /* Update the SSA web.  */
  update_ssa (TODO_update_ssa);

  if (free_region_copy)
    free (region_copy);

  free_original_copy_tables ();
  return true;
#else
  gcc_unreachable ();
#endif
}

/*
DEF_VEC_P(basic_block);
DEF_VEC_ALLOC_P(basic_block,heap);
*/

/* Add all the blocks dominated by ENTRY to the array BBS_P.  Stop
   adding blocks when the dominator traversal reaches EXIT.  This
   function silently assumes that ENTRY strictly dominates EXIT.  */

static void
gather_blocks_in_sese_region (basic_block entry, basic_block exit,
			      VEC(basic_block,heap) **bbs_p)
{
  basic_block son;

  for (son = first_dom_son (CDI_DOMINATORS, entry);
       son;
       son = next_dom_son (CDI_DOMINATORS, son))
    {
      VEC_safe_push (basic_block, heap, *bbs_p, son);
      if (son != exit)
	gather_blocks_in_sese_region (son, exit, bbs_p);
    }
}

/* Replaces *TP with a duplicate (belonging to function TO_CONTEXT).
   The duplicates are recorded in VARS_MAP.  */

static void
replace_by_duplicate_decl (tree *tp ATTRIBUTE_UNUSED, struct pointer_map_t *vars_map ATTRIBUTE_UNUSED,
			   tree to_context ATTRIBUTE_UNUSED)
{
  /* FIXME tuples.  */
#if 0
  tree t = *tp, new_t;
  struct function *f = DECL_STRUCT_FUNCTION (to_context);
  void **loc;

  if (DECL_CONTEXT (t) == to_context)
    return;

  loc = pointer_map_contains (vars_map, t);

  if (!loc)
    {
      loc = pointer_map_insert (vars_map, t);

      if (SSA_VAR_P (t))
	{
	  new_t = copy_var_decl (t, DECL_NAME (t), TREE_TYPE (t));
	  f->unexpanded_var_list
		  = tree_cons (NULL_TREE, new_t, f->unexpanded_var_list);
	}
      else
	{
	  gcc_assert (TREE_CODE (t) == CONST_DECL);
	  new_t = copy_node (t);
	}
      DECL_CONTEXT (new_t) = to_context;

      *loc = new_t;
    }
  else
    new_t = *loc;

  *tp = new_t;
#else
  gcc_unreachable ();
#endif
}

/* Creates an ssa name in TO_CONTEXT equivalent to NAME.
   VARS_MAP maps old ssa names and var_decls to the new ones.  */

static tree
replace_ssa_name (tree name, struct pointer_map_t *vars_map,
		  tree to_context)
{
  void **loc;
  tree new_name, decl = SSA_NAME_VAR (name);

  gcc_assert (is_gimple_reg (name));

  loc = pointer_map_contains (vars_map, name);

  if (!loc)
    {
      replace_by_duplicate_decl (&decl, vars_map, to_context);

      push_cfun (DECL_STRUCT_FUNCTION (to_context));
      if (gimple_in_ssa_p (cfun))
	add_referenced_var (decl);

      new_name = make_ssa_name (decl, SSA_NAME_DEF_STMT (name));
      if (SSA_NAME_IS_DEFAULT_DEF (name))
	set_default_def (decl, new_name);
      pop_cfun ();

      loc = pointer_map_insert (vars_map, name);
      *loc = new_name;
    }
  else
    new_name = *loc;

  return new_name;
}

struct move_stmt_d
{
  tree block;
  tree from_context;
  tree to_context;
  struct pointer_map_t *vars_map;
  htab_t new_label_map;
  bool remap_decls_p;
};

/* Helper for move_block_to_fn.  Set TREE_BLOCK in every expression
   contained in *TP and change the DECL_CONTEXT of every local
   variable referenced in *TP.  */

static tree
move_stmt_r (tree *tp, int *walk_subtrees, void *data)
{
  struct move_stmt_d *p = (struct move_stmt_d *) data;
  tree t = *tp;

  if (p->block
      && (EXPR_P (t) || GIMPLE_STMT_P (t)))
    TREE_BLOCK (t) = p->block;

  if (OMP_DIRECTIVE_P (t)
      && TREE_CODE (t) != OMP_RETURN
      && TREE_CODE (t) != OMP_CONTINUE)
    {
      /* Do not remap variables inside OMP directives.  Variables
	 referenced in clauses and directive header belong to the
	 parent function and should not be moved into the child
	 function.  */
      bool save_remap_decls_p = p->remap_decls_p;
      p->remap_decls_p = false;
      *walk_subtrees = 0;

      walk_tree (&OMP_BODY (t), move_stmt_r, p, NULL);

      p->remap_decls_p = save_remap_decls_p;
    }
  else if (DECL_P (t) || TREE_CODE (t) == SSA_NAME)
    {
      if (TREE_CODE (t) == SSA_NAME)
	*tp = replace_ssa_name (t, p->vars_map, p->to_context);
      else if (TREE_CODE (t) == LABEL_DECL)
	{
	  if (p->new_label_map)
	    {
	      struct tree_map in, *out;
	      in.base.from = t;
	      out = htab_find_with_hash (p->new_label_map, &in, DECL_UID (t));
	      if (out)
		*tp = t = out->to;
	    }

	  DECL_CONTEXT (t) = p->to_context;
	}
      else if (p->remap_decls_p)
	{
	  /* Replace T with its duplicate.  T should no longer appear in the
	     parent function, so this looks wasteful; however, it may appear
	     in referenced_vars, and more importantly, as virtual operands of
	     statements, and in alias lists of other variables.  It would be
	     quite difficult to expunge it from all those places.  ??? It might
	     suffice to do this for addressable variables.  */
	  if ((TREE_CODE (t) == VAR_DECL
	       && !is_global_var (t))
	      || TREE_CODE (t) == CONST_DECL)
	    replace_by_duplicate_decl (tp, p->vars_map, p->to_context);
	  
	  if (SSA_VAR_P (t)
	      && gimple_in_ssa_p (cfun))
	    {
	      push_cfun (DECL_STRUCT_FUNCTION (p->to_context));
	      add_referenced_var (*tp);
	      pop_cfun ();
	    }
	}
      *walk_subtrees = 0;
    }
  else if (TYPE_P (t))
    *walk_subtrees = 0;

  return NULL_TREE;
}

/* FIXME tuples.  */
#if 0
/* Marks virtual operands of all statements in basic blocks BBS for
   renaming.  */

static void
mark_virtual_ops_in_region (VEC (basic_block,heap) *bbs ATTRIBUTE_UNUSED)
{
  tree phi;
  block_stmt_iterator bsi;
  basic_block bb;
  unsigned i;

  for (i = 0; VEC_iterate (basic_block, bbs, i, bb); i++)
    {
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	mark_virtual_ops_for_renaming (phi);

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	mark_virtual_ops_for_renaming (bsi_stmt (bsi));
    }
}
#endif

/* Move basic block BB from function CFUN to function DEST_FN.  The
   block is moved out of the original linked list and placed after
   block AFTER in the new list.  Also, the block is removed from the
   original array of blocks and placed in DEST_FN's array of blocks.
   If UPDATE_EDGE_COUNT_P is true, the edge counts on both CFGs is
   updated to reflect the moved edges.

   The local variables are remapped to new instances, VARS_MAP is used
   to record the mapping.  */

/* FIXME tuples.  */
#if 0
static void
move_block_to_fn (struct function *dest_cfun, basic_block bb,
		  basic_block after, bool update_edge_count_p,
		  struct pointer_map_t *vars_map, htab_t new_label_map,
		  int eh_offset)
{
  struct control_flow_graph *cfg;
  edge_iterator ei;
  edge e;
  gimple_stmt_iterator si;
  struct move_stmt_d d;
  unsigned old_len, new_len;
  tree phi, next_phi;

  /* Remove BB from dominance structures.  */
  delete_from_dominance_info (CDI_DOMINATORS, bb);
  if (current_loops)
    remove_bb_from_loops (bb);

  /* Link BB to the new linked list.  */
  move_block_after (bb, after);

  /* Update the edge count in the corresponding flowgraphs.  */
  if (update_edge_count_p)
    FOR_EACH_EDGE (e, ei, bb->succs)
      {
	cfun->cfg->x_n_edges--;
	dest_cfun->cfg->x_n_edges++;
      }

  /* Remove BB from the original basic block array.  */
  VEC_replace (basic_block, cfun->cfg->x_basic_block_info, bb->index, NULL);
  cfun->cfg->x_n_basic_blocks--;

  /* Grow DEST_CFUN's basic block array if needed.  */
  cfg = dest_cfun->cfg;
  cfg->x_n_basic_blocks++;
  if (bb->index >= cfg->x_last_basic_block)
    cfg->x_last_basic_block = bb->index + 1;

  old_len = VEC_length (basic_block, cfg->x_basic_block_info);
  if ((unsigned) cfg->x_last_basic_block >= old_len)
    {
      new_len = cfg->x_last_basic_block + (cfg->x_last_basic_block + 3) / 4;
      VEC_safe_grow_cleared (basic_block, gc, cfg->x_basic_block_info,
			     new_len);
    }

  VEC_replace (basic_block, cfg->x_basic_block_info,
               bb->index, bb);

  /* Remap the variables in phi nodes.  */
  for (phi = phi_nodes (bb); phi; phi = next_phi)
    {
      use_operand_p use;
      tree op = PHI_RESULT (phi);
      ssa_op_iter oi;

      next_phi = PHI_CHAIN (phi);
      if (!is_gimple_reg (op))
	{
	  /* Remove the phi nodes for virtual operands (alias analysis will be
	     run for the new function, anyway).  */
          remove_phi_node (phi, NULL, true);
	  continue;
	}

      SET_PHI_RESULT (phi, replace_ssa_name (op, vars_map, dest_cfun->decl));
      FOR_EACH_PHI_ARG (use, phi, oi, SSA_OP_USE)
	{
	  op = USE_FROM_PTR (use);
	  if (TREE_CODE (op) == SSA_NAME)
	    SET_USE (use, replace_ssa_name (op, vars_map, dest_cfun->decl));
	}
    }

  /* The statements in BB need to be associated with a new TREE_BLOCK.
     Labels need to be associated with a new label-to-block map.  */
  memset (&d, 0, sizeof (d));
  d.vars_map = vars_map;
  d.from_context = cfun->decl;
  d.to_context = dest_cfun->decl;
  d.new_label_map = new_label_map;

  for (si = gsi_start (bb); !gsi_end_p (si); gsi_next (&si))
    {
      tree stmt = gsi_stmt (si);
      int region;

      d.remap_decls_p = true;
      if (TREE_BLOCK (stmt))
	d.block = DECL_INITIAL (dest_cfun->decl);

      walk_tree (&stmt, move_stmt_r, &d, NULL);

      if (TREE_CODE (stmt) == LABEL_EXPR)
	{
	  tree label = LABEL_EXPR_LABEL (stmt);
	  int uid = LABEL_DECL_UID (label);

	  gcc_assert (uid > -1);

	  old_len = VEC_length (basic_block, cfg->x_label_to_block_map);
	  if (old_len <= (unsigned) uid)
	    {
	      new_len = 3 * uid / 2;
	      VEC_safe_grow_cleared (basic_block, gc,
				     cfg->x_label_to_block_map, new_len);
	    }

	  VEC_replace (basic_block, cfg->x_label_to_block_map, uid, bb);
	  VEC_replace (basic_block, cfun->cfg->x_label_to_block_map, uid, NULL);

	  gcc_assert (DECL_CONTEXT (label) == dest_cfun->decl);

	  if (uid >= dest_cfun->last_label_uid)
	    dest_cfun->last_label_uid = uid + 1;
	}
      else if (TREE_CODE (stmt) == RESX_EXPR && eh_offset != 0)
	TREE_OPERAND (stmt, 0) =
	  build_int_cst (NULL_TREE,
			 TREE_INT_CST_LOW (TREE_OPERAND (stmt, 0))
			 + eh_offset);

      region = lookup_stmt_eh_region (stmt);
      if (region >= 0)
	{
	  add_stmt_to_eh_region_fn (dest_cfun, stmt, region + eh_offset);
	  remove_stmt_from_eh_region (stmt);
	  gimple_duplicate_stmt_histograms (dest_cfun, stmt, cfun, stmt);
          gimple_remove_stmt_histograms (cfun, stmt);
	}

      /* We cannot leave any operands allocated from the operand caches of
	 the current function.  */
      free_stmt_operands (stmt);
      push_cfun (dest_cfun);
      update_stmt (stmt);
      pop_cfun ();
    }
}
#endif

/* Examine the statements in BB (which is in SRC_CFUN); find and return
   the outermost EH region.  Use REGION as the incoming base EH region.  */

/* FIXME tuples.  */
#if 0
static int
find_outermost_region_in_block (struct function *src_cfun,
				basic_block bb, int region)
{
  gimple_stmt_iterator si;

  for (si = gsi_start (bb); !gsi_end_p (si); gsi_next (&si))
    {
      tree stmt = gsi_stmt (si);
      int stmt_region;

      if (TREE_CODE (stmt) == RESX_EXPR)
	stmt_region = TREE_INT_CST_LOW (TREE_OPERAND (stmt, 0));
      else
	stmt_region = lookup_stmt_eh_region_fn (src_cfun, stmt);
      if (stmt_region > 0)
	{
	  if (region < 0)
	    region = stmt_region;
	  else if (stmt_region != region)
	    {
	      region = eh_region_outermost (src_cfun, stmt_region, region);
	      gcc_assert (region != -1);
	    }
	}
    }

  return region;
}

static tree
new_label_mapper (tree decl, void *data)
{
  htab_t hash = (htab_t) data;
  struct tree_map *m;
  void **slot;

  gcc_assert (TREE_CODE (decl) == LABEL_DECL);

  m = xmalloc (sizeof (struct tree_map));
  m->hash = DECL_UID (decl);
  m->base.from = decl;
  m->to = create_artificial_label ();
  LABEL_DECL_UID (m->to) = LABEL_DECL_UID (decl);

  slot = htab_find_slot_with_hash (hash, m, m->hash, INSERT);
  gcc_assert (*slot == NULL);

  *slot = m;

  return m->to;
}
#endif

/* Move a single-entry, single-exit region delimited by ENTRY_BB and
   EXIT_BB to function DEST_CFUN.  The whole region is replaced by a
   single basic block in the original CFG and the new basic block is
   returned.  DEST_CFUN must not have a CFG yet.

   Note that the region need not be a pure SESE region.  Blocks inside
   the region may contain calls to abort/exit.  The only restriction
   is that ENTRY_BB should be the only entry point and it must
   dominate EXIT_BB.

   All local variables referenced in the region are assumed to be in
   the corresponding BLOCK_VARS and unexpanded variable lists
   associated with DEST_CFUN.  */

basic_block
move_sese_region_to_fn (struct function *dest_cfun ATTRIBUTE_UNUSED, basic_block entry_bb ATTRIBUTE_UNUSED,
		        basic_block exit_bb ATTRIBUTE_UNUSED)
{
  /* FIXME tuples.  */
#if 0
  VEC(basic_block,heap) *bbs;
  basic_block after, bb, *entry_pred, *exit_succ;
  struct function *saved_cfun;
  int *entry_flag, *exit_flag, eh_offset;
  unsigned *entry_prob, *exit_prob;
  unsigned i, num_entry_edges, num_exit_edges;
  edge e;
  edge_iterator ei;
  htab_t new_label_map;
  struct pointer_map_t *vars_map;
  struct loop *loop = entry_bb->loop_father;

  /* If ENTRY does not strictly dominate EXIT, this cannot be an SESE
     region.  */
  gcc_assert (entry_bb != exit_bb
              && (!exit_bb
		  || dominated_by_p (CDI_DOMINATORS, exit_bb, entry_bb)));

  /* Collect all the blocks in the region.  Manually add ENTRY_BB
     because it won't be added by dfs_enumerate_from.  */
  bbs = NULL;
  VEC_safe_push (basic_block, heap, bbs, entry_bb);
  gather_blocks_in_sese_region (entry_bb, exit_bb, &bbs);

  /* The blocks that used to be dominated by something in BBS will now be
     dominated by the new block.  */
  dom_bbs = get_dominated_by_region (CDI_DOMINATORS,
				     VEC_address (basic_block, bbs),
				     VEC_length (basic_block, bbs));

  /* Detach ENTRY_BB and EXIT_BB from CFUN->CFG.  We need to remember
     the predecessor edges to ENTRY_BB and the successor edges to
     EXIT_BB so that we can re-attach them to the new basic block that
     will replace the region.  */
  num_entry_edges = EDGE_COUNT (entry_bb->preds);
  entry_pred = (basic_block *) xcalloc (num_entry_edges, sizeof (basic_block));
  entry_flag = (int *) xcalloc (num_entry_edges, sizeof (int));
  entry_prob = XNEWVEC (unsigned, num_entry_edges);
  i = 0;
  for (ei = ei_start (entry_bb->preds); (e = ei_safe_edge (ei)) != NULL;)
    {
      entry_prob[i] = e->probability;
      entry_flag[i] = e->flags;
      entry_pred[i++] = e->src;
      remove_edge (e);
    }

  if (exit_bb)
    {
      num_exit_edges = EDGE_COUNT (exit_bb->succs);
      exit_succ = (basic_block *) xcalloc (num_exit_edges,
					   sizeof (basic_block));
      exit_flag = (int *) xcalloc (num_exit_edges, sizeof (int));
      exit_prob = XNEWVEC (unsigned, num_exit_edges);
      i = 0;
      for (ei = ei_start (exit_bb->succs); (e = ei_safe_edge (ei)) != NULL;)
	{
	  exit_prob[i] = e->probability;
	  exit_flag[i] = e->flags;
	  exit_succ[i++] = e->dest;
	  remove_edge (e);
	}
    }
  else
    {
      num_exit_edges = 0;
      exit_succ = NULL;
      exit_flag = NULL;
      exit_prob = NULL;
    }

  /* Switch context to the child function to initialize DEST_FN's CFG.  */
  gcc_assert (dest_cfun->cfg == NULL);
  push_cfun (dest_cfun);

  init_empty_tree_cfg ();

  /* Initialize EH information for the new function.  */
  eh_offset = 0;
  new_label_map = NULL;
  if (saved_cfun->eh)
    {
      int region = -1;

      for (i = 0; VEC_iterate (basic_block, bbs, i, bb); i++)
	region = find_outermost_region_in_block (saved_cfun, bb, region);

      init_eh_for_function ();
      if (region != -1)
	{
	  new_label_map = htab_create (17, tree_map_hash, tree_map_eq, free);
	  eh_offset = duplicate_eh_regions (saved_cfun, new_label_mapper,
					    new_label_map, region, 0);
	}
    }

  pop_cfun ();

  /* The ssa form for virtual operands in the source function will have to
     be repaired.  We do not care for the real operands -- the sese region
     must be closed with respect to those.  */
  mark_virtual_ops_in_region (bbs);

  /* Move blocks from BBS into DEST_CFUN.  */
  gcc_assert (VEC_length (basic_block, bbs) >= 2);
  after = dest_cfun->cfg->x_entry_block_ptr;
  vars_map = pointer_map_create ();
  for (i = 0; VEC_iterate (basic_block, bbs, i, bb); i++)
    {
      /* No need to update edge counts on the last block.  It has
	 already been updated earlier when we detached the region from
	 the original CFG.  */
      move_block_to_fn (dest_cfun, bb, after, bb != exit_bb, vars_map,
	                new_label_map, eh_offset);
      after = bb;
    }

  if (new_label_map)
    htab_delete (new_label_map);
  pointer_map_destroy (vars_map);

  /* Rewire the entry and exit blocks.  The successor to the entry
     block turns into the successor of DEST_FN's ENTRY_BLOCK_PTR in
     the child function.  Similarly, the predecessor of DEST_FN's
     EXIT_BLOCK_PTR turns into the predecessor of EXIT_BLOCK_PTR.  We
     need to switch CFUN between DEST_CFUN and SAVED_CFUN so that the
     various CFG manipulation function get to the right CFG.

     FIXME, this is silly.  The CFG ought to become a parameter to
     these helpers.  */
  push_cfun (dest_cfun);
  make_edge (ENTRY_BLOCK_PTR, entry_bb, EDGE_FALLTHRU);
  if (exit_bb)
    make_edge (exit_bb,  EXIT_BLOCK_PTR, 0);
  pop_cfun ();

  /* Back in the original function, the SESE region has disappeared,
     create a new basic block in its place.  */
  bb = create_empty_bb (entry_pred[0]);
  if (current_loops)
    add_bb_to_loop (bb, loop);
  for (i = 0; i < num_entry_edges; i++)
    {
      e = make_edge (entry_pred[i], bb, entry_flag[i]);
      e->probability = entry_prob[i];
    }

  for (i = 0; i < num_exit_edges; i++)
    {
      e = make_edge (bb, exit_succ[i], exit_flag[i]);
      e->probability = exit_prob[i];
    }

  set_immediate_dominator (CDI_DOMINATORS, bb, dom_entry);
  for (i = 0; VEC_iterate (basic_block, dom_bbs, i, abb); i++)
    set_immediate_dominator (CDI_DOMINATORS, abb, bb);
  VEC_free (basic_block, heap, dom_bbs);

  if (exit_bb)
    {
      free (exit_prob);
      free (exit_flag);
      free (exit_succ);
    }
  free (entry_prob);
  free (entry_flag);
  free (entry_pred);
  VEC_free (basic_block, heap, bbs);

  return bb;
#else
  gcc_unreachable ();
#endif
}


/* Dump FUNCTION_DECL FN to file FILE using FLAGS (see TDF_* in tree.h)  */

void
dump_function_to_file (tree fn, FILE *file, int flags)
{
  tree arg, vars, var;
  struct function *dsf;
  bool ignore_topmost_bind = false, any_var = false;
  basic_block bb;
  tree chain;

  fprintf (file, "%s (", lang_hooks.decl_printable_name (fn, 2));

  arg = DECL_ARGUMENTS (fn);
  while (arg)
    {
      print_generic_expr (file, arg, dump_flags);
      if (TREE_CHAIN (arg))
	fprintf (file, ", ");
      arg = TREE_CHAIN (arg);
    }
  fprintf (file, ")\n");

  dsf = DECL_STRUCT_FUNCTION (fn);
  if (dsf && (flags & TDF_DETAILS))
    dump_eh_tree (file, dsf);

  if (flags & TDF_RAW)
    {
      dump_node (fn, TDF_SLIM | flags, file);
      return;
    }

  /* Switch CFUN to point to FN.  */
  push_cfun (DECL_STRUCT_FUNCTION (fn));

  /* When GIMPLE is lowered, the variables are no longer available in
     BIND_EXPRs, so display them separately.  */
  if (cfun && cfun->decl == fn && cfun->unexpanded_var_list)
    {
      ignore_topmost_bind = true;

      fprintf (file, "{\n");
      for (vars = cfun->unexpanded_var_list; vars; vars = TREE_CHAIN (vars))
	{
	  var = TREE_VALUE (vars);

	  print_generic_decl (file, var, flags);
	  fprintf (file, "\n");

	  any_var = true;
	}
    }

  if (cfun && cfun->decl == fn && cfun->cfg && basic_block_info)
    {
      /* If the CFG has been built, emit a CFG-based dump.  */
      check_bb_profile (ENTRY_BLOCK_PTR, file);
      if (!ignore_topmost_bind)
	fprintf (file, "{\n");

      if (any_var && n_basic_blocks)
	fprintf (file, "\n");

      FOR_EACH_BB (bb)
	gimple_dump_bb (bb, file, 2, flags);

      fprintf (file, "}\n");
      check_bb_profile (EXIT_BLOCK_PTR, file);
    }
  else if (DECL_SAVED_TREE (fn) == NULL)
    {
      /* The function is now in GIMPLE form but the CFG has not been
	 built yet.  Emit the single sequence of GIMPLE statements
	 that make up its body.  */
      print_gimple_seq (file, gimple_body (fn), 2, flags);
    }
  else
    {
      int indent;

      /* Make a tree based dump.  */
      chain = DECL_SAVED_TREE (fn);

      if (chain && TREE_CODE (chain) == BIND_EXPR)
	{
	  if (ignore_topmost_bind)
	    {
	      chain = BIND_EXPR_BODY (chain);
	      indent = 2;
	    }
	  else
	    indent = 0;
	}
      else
	{
	  if (!ignore_topmost_bind)
	    fprintf (file, "{\n");
	  indent = 2;
	}

      if (any_var)
	fprintf (file, "\n");

      print_generic_stmt_indented (file, chain, flags, indent);
      if (ignore_topmost_bind)
	fprintf (file, "}\n");
    }

  fprintf (file, "\n\n");

  /* Restore CFUN.  */
  pop_cfun ();
}


/* Dump FUNCTION_DECL FN to stderr using FLAGS (see TDF_* in tree.h)  */

void
debug_function (tree fn, int flags)
{
  dump_function_to_file (fn, stderr, flags);
}


/* Pretty print of the loops intermediate representation.  */
static void print_loop (FILE *, struct loop *, int);
static void print_pred_bbs (FILE *, basic_block bb);
static void print_succ_bbs (FILE *, basic_block bb);


/* Print on FILE the indexes for the predecessors of basic_block BB.  */

static void
print_pred_bbs (FILE *file, basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->preds)
    fprintf (file, "bb_%d ", e->src->index);
}


/* Print on FILE the indexes for the successors of basic_block BB.  */

static void
print_succ_bbs (FILE *file, basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->succs)
    fprintf (file, "bb_%d ", e->dest->index);
}


/* Pretty print LOOP on FILE, indented INDENT spaces.  */

static void
print_loop (FILE *file, struct loop *loop, int indent)
{
  char *s_indent;
  basic_block bb;

  if (loop == NULL)
    return;

  s_indent = (char *) alloca ((size_t) indent + 1);
  memset ((void *) s_indent, ' ', (size_t) indent);
  s_indent[indent] = '\0';

  /* Print the loop's header.  */
  fprintf (file, "%sloop_%d\n", s_indent, loop->num);

  /* Print the loop's body.  */
  fprintf (file, "%s{\n", s_indent);
  FOR_EACH_BB (bb)
    if (bb->loop_father == loop)
      {
	/* Print the basic_block's header.  */
	fprintf (file, "%s  bb_%d (preds = {", s_indent, bb->index);
	print_pred_bbs (file, bb);
	fprintf (file, "}, succs = {");
	print_succ_bbs (file, bb);
	fprintf (file, "})\n");

	/* Print the basic_block's body.  */
	fprintf (file, "%s  {\n", s_indent);
	gimple_dump_bb (bb, file, indent + 4, TDF_VOPS|TDF_MEMSYMS);
	fprintf (file, "%s  }\n", s_indent);
      }

  print_loop (file, loop->inner, indent + 2);
  fprintf (file, "%s}\n", s_indent);
  print_loop (file, loop->next, indent);
}


/* Follow a CFG edge from the entry point of the program, and on entry
   of a loop, pretty print the loop structure on FILE.  */

void
print_loop_ir (FILE *file)
{
  basic_block bb;

  bb = BASIC_BLOCK (NUM_FIXED_BLOCKS);
  if (bb && bb->loop_father)
    print_loop (file, bb->loop_father, 0);
}


/* Debugging loops structure at tree level.  */

void
debug_loop_ir (void)
{
  print_loop_ir (stderr);
}


/* Return true if BB ends with a call, possibly followed by some
   instructions that must stay with the call.  Return false,
   otherwise.  */

/* FIXME tuples.  */
#if 0
static bool
tree_block_ends_with_call_p (basic_block bb)
{
  gimple_stmt_iterator gsi = gsi_last (bb);
  return get_call_expr_in (gsi_stmt (gsi)) != NULL;
}
#endif


/* Return true if BB ends with a conditional branch.  Return false,
   otherwise.  */

/* FIXME tuples.  */
#if 0
static bool
tree_block_ends_with_condjump_p (const_basic_block bb)
{
  tree stmt = last_stmt (bb);
  return (stmt && TREE_CODE (stmt) == COND_EXPR);
}


/* Return true if we need to add fake edge to exit at statement T.
   Helper function for tree_flow_call_edges_add.  */

static bool
need_fake_edge_p (tree t)
{
  tree call;

  /* NORETURN and LONGJMP calls already have an edge to exit.
     CONST and PURE calls do not need one.
     We don't currently check for CONST and PURE here, although
     it would be a good idea, because those attributes are
     figured out from the RTL in mark_constant_function, and
     the counter incrementation code from -fprofile-arcs
     leads to different results from -fbranch-probabilities.  */
  call = get_call_expr_in (t);
  if (call
      && !(call_expr_flags (call) & ECF_NORETURN))
    return true;

  if (TREE_CODE (t) == ASM_EXPR
       && (ASM_VOLATILE_P (t) || ASM_INPUT_P (t)))
    return true;

  return false;
}
#endif


/* Add fake edges to the function exit for any non constant and non
   noreturn calls, volatile inline assembly in the bitmap of blocks
   specified by BLOCKS or to the whole CFG if BLOCKS is zero.  Return
   the number of blocks that were split.

   The goal is to expose cases in which entering a basic block does
   not imply that all subsequent instructions must be executed.  */

/* FIXME tuples.  */
#if 0
static int
tree_flow_call_edges_add (sbitmap blocks)
{
  int i;
  int blocks_split = 0;
  int last_bb = last_basic_block;
  bool check_last_block = false;

  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    return 0;

  if (! blocks)
    check_last_block = true;
  else
    check_last_block = TEST_BIT (blocks, EXIT_BLOCK_PTR->prev_bb->index);

  /* In the last basic block, before epilogue generation, there will be
     a fallthru edge to EXIT.  Special care is required if the last insn
     of the last basic block is a call because make_edge folds duplicate
     edges, which would result in the fallthru edge also being marked
     fake, which would result in the fallthru edge being removed by
     remove_fake_edges, which would result in an invalid CFG.

     Moreover, we can't elide the outgoing fake edge, since the block
     profiler needs to take this into account in order to solve the minimal
     spanning tree in the case that the call doesn't return.

     Handle this by adding a dummy instruction in a new last basic block.  */
  if (check_last_block)
    {
      basic_block bb = EXIT_BLOCK_PTR->prev_bb;
      gimple_stmt_iterator gsi = gsi_last (bb);
      tree t = NULL_TREE;
      if (!gsi_end_p (gsi))
	t = gsi_stmt (gsi);

      if (t && need_fake_edge_p (t))
	{
	  edge e;

	  e = find_edge (bb, EXIT_BLOCK_PTR);
	  if (e)
	    {
	      gsi_insert_on_edge (e, build_empty_stmt ());
	      gsi_commit_edge_inserts ();
	    }
	}
    }

  /* Now add fake edges to the function exit for any non constant
     calls since there is no way that we can determine if they will
     return or not...  */
  for (i = 0; i < last_bb; i++)
    {
      basic_block bb = BASIC_BLOCK (i);
      gimple_stmt_iterator gsi;
      tree stmt, last_stmt;

      if (!bb)
	continue;

      if (blocks && !TEST_BIT (blocks, i))
	continue;

      gsi = gsi_last (bb);
      if (!gsi_end_p (gsi))
	{
	  last_stmt = gsi_stmt (gsi);
	  do
	    {
	      stmt = gsi_stmt (gsi);
	      if (need_fake_edge_p (stmt))
		{
		  edge e;
		  /* The handling above of the final block before the
		     epilogue should be enough to verify that there is
		     no edge to the exit block in CFG already.
		     Calling make_edge in such case would cause us to
		     mark that edge as fake and remove it later.  */
#ifdef ENABLE_CHECKING
		  if (stmt == last_stmt)
		    {
		      e = find_edge (bb, EXIT_BLOCK_PTR);
		      gcc_assert (e == NULL);
		    }
#endif

		  /* Note that the following may create a new basic block
		     and renumber the existing basic blocks.  */
		  if (stmt != last_stmt)
		    {
		      e = split_block (bb, stmt);
		      if (e)
			blocks_split++;
		    }
		  make_edge (bb, EXIT_BLOCK_PTR, EDGE_FAKE);
		}
	      gsi_prev (&gsi);
	    }
	  while (!gsi_end_p (gsi));
	}
    }

  if (blocks_split)
    verify_flow_info ();

  return blocks_split;
}
#endif

/* Purge dead abnormal call edges from basic block BB.  */

bool
tree_purge_dead_abnormal_call_edges (basic_block bb ATTRIBUTE_UNUSED)
{
  /* FIXME tuples.  */
#if 0
  bool changed = tree_purge_dead_eh_edges (bb);

  if (current_function_has_nonlocal_label)
    {
      tree stmt = last_stmt (bb);
      edge_iterator ei;
      edge e;

      if (!(stmt && stmt_can_make_abnormal_goto (stmt)))
	for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
	  {
	    if (e->flags & EDGE_ABNORMAL)
	      {
		remove_edge (e);
		changed = true;
	      }
	    else
	      ei_next (&ei);
	  }

      /* See tree_purge_dead_eh_edges below.  */
      if (changed)
	free_dominance_info (CDI_DOMINATORS);
    }

  return changed;
#else
  gcc_unreachable ();
#endif
}

/* Stores all basic blocks dominated by BB to DOM_BBS.  */

static void
get_all_dominated_blocks (basic_block bb, VEC (basic_block, heap) **dom_bbs)
{
  basic_block son;

  VEC_safe_push (basic_block, heap, *dom_bbs, bb);
  for (son = first_dom_son (CDI_DOMINATORS, bb);
       son;
       son = next_dom_son (CDI_DOMINATORS, son))
    get_all_dominated_blocks (son, dom_bbs);
}

/* Removes edge E and all the blocks dominated by it, and updates dominance
   information.  The IL in E->src needs to be updated separately.
   If dominance info is not available, only the edge E is removed.*/

void
remove_edge_and_dominated_blocks (edge e)
{
  VEC (basic_block, heap) *bbs_to_remove = NULL;
  VEC (basic_block, heap) *bbs_to_fix_dom = NULL;
  bitmap df, df_idom;
  edge f;
  edge_iterator ei;
  bool none_removed = false;
  unsigned i;
  basic_block bb, dbb;
  bitmap_iterator bi;

  if (!dom_info_available_p (CDI_DOMINATORS))
    {
      remove_edge (e);
      return;
    }

  /* No updating is needed for edges to exit.  */
  if (e->dest == EXIT_BLOCK_PTR)
    {
      if (cfgcleanup_altered_bbs)
	bitmap_set_bit (cfgcleanup_altered_bbs, e->src->index);
      remove_edge (e);
      return;
    }

  /* First, we find the basic blocks to remove.  If E->dest has a predecessor
     that is not dominated by E->dest, then this set is empty.  Otherwise,
     all the basic blocks dominated by E->dest are removed.

     Also, to DF_IDOM we store the immediate dominators of the blocks in
     the dominance frontier of E (i.e., of the successors of the
     removed blocks, if there are any, and of E->dest otherwise).  */
  FOR_EACH_EDGE (f, ei, e->dest->preds)
    {
      if (f == e)
	continue;

      if (!dominated_by_p (CDI_DOMINATORS, f->src, e->dest))
	{
	  none_removed = true;
	  break;
	}
    }

  df = BITMAP_ALLOC (NULL);
  df_idom = BITMAP_ALLOC (NULL);

  if (none_removed)
    bitmap_set_bit (df_idom,
		    get_immediate_dominator (CDI_DOMINATORS, e->dest)->index);
  else
    {
      get_all_dominated_blocks (e->dest, &bbs_to_remove);
      for (i = 0; VEC_iterate (basic_block, bbs_to_remove, i, bb); i++)
	{
	  FOR_EACH_EDGE (f, ei, bb->succs)
	    {
	      if (f->dest != EXIT_BLOCK_PTR)
		bitmap_set_bit (df, f->dest->index);
	    }
	}
      for (i = 0; VEC_iterate (basic_block, bbs_to_remove, i, bb); i++)
	bitmap_clear_bit (df, bb->index);

      EXECUTE_IF_SET_IN_BITMAP (df, 0, i, bi)
	{
	  bb = BASIC_BLOCK (i);
	  bitmap_set_bit (df_idom,
			  get_immediate_dominator (CDI_DOMINATORS, bb)->index);
	}
    }

  if (cfgcleanup_altered_bbs)
    {
      /* Record the set of the altered basic blocks.  */
      bitmap_set_bit (cfgcleanup_altered_bbs, e->src->index);
      bitmap_ior_into (cfgcleanup_altered_bbs, df);
    }

  /* Remove E and the cancelled blocks.  */
  if (none_removed)
    remove_edge (e);
  else
    {
      for (i = 0; VEC_iterate (basic_block, bbs_to_remove, i, bb); i++)
	delete_basic_block (bb);
    }

  /* Update the dominance information.  The immediate dominator may change only
     for blocks whose immediate dominator belongs to DF_IDOM:
   
     Suppose that idom(X) = Y before removal of E and idom(X) != Y after the
     removal.  Let Z the arbitrary block such that idom(Z) = Y and
     Z dominates X after the removal.  Before removal, there exists a path P
     from Y to X that avoids Z.  Let F be the last edge on P that is
     removed, and let W = F->dest.  Before removal, idom(W) = Y (since Y
     dominates W, and because of P, Z does not dominate W), and W belongs to
     the dominance frontier of E.  Therefore, Y belongs to DF_IDOM.  */ 
  EXECUTE_IF_SET_IN_BITMAP (df_idom, 0, i, bi)
    {
      bb = BASIC_BLOCK (i);
      for (dbb = first_dom_son (CDI_DOMINATORS, bb);
	   dbb;
	   dbb = next_dom_son (CDI_DOMINATORS, dbb))
	VEC_safe_push (basic_block, heap, bbs_to_fix_dom, dbb);
    }

  iterate_fix_dominators (CDI_DOMINATORS, bbs_to_fix_dom, true);

  BITMAP_FREE (df);
  BITMAP_FREE (df_idom);
  VEC_free (basic_block, heap, bbs_to_remove);
  VEC_free (basic_block, heap, bbs_to_fix_dom);
}

/* Purge dead EH edges from basic block BB.  */

bool
tree_purge_dead_eh_edges (basic_block bb ATTRIBUTE_UNUSED)
{
  bool changed = false;
  edge e;
  edge_iterator ei;
  gimple stmt = last_stmt (bb);

  if (stmt && stmt_can_throw_internal (stmt))
    return false;

  for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
    {
      if (e->flags & EDGE_EH)
	{
	  remove_edge_and_dominated_blocks (e);
	  changed = true;
	}
      else
	ei_next (&ei);
    }

  return changed;
}

bool
tree_purge_all_dead_eh_edges (const_bitmap blocks)
{
  bool changed = false;
  unsigned i;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (blocks, 0, i, bi)
    {
      changed |= tree_purge_dead_eh_edges (BASIC_BLOCK (i));
    }

  return changed;
}

/* This function is called whenever a new edge is created or
   redirected.  */

static void
tree_execute_on_growing_pred (edge e)
{
  basic_block bb = e->dest;

  if (phi_nodes (bb))
    reserve_phi_args_for_new_edge (bb);
}

/* This function is called immediately before edge E is removed from
   the edge vector E->dest->preds.  */

static void
tree_execute_on_shrinking_pred (edge e)
{
  if (phi_nodes (e->dest))
    remove_phi_args (e);
}

/*---------------------------------------------------------------------------
  Helper functions for Loop versioning
  ---------------------------------------------------------------------------*/

/* Adjust phi nodes for 'first' basic block.  'second' basic block is a copy
   of 'first'. Both of them are dominated by 'new_head' basic block. When
   'new_head' was created by 'second's incoming edge it received phi arguments
   on the edge by split_edge(). Later, additional edge 'e' was created to
   connect 'new_head' and 'first'. Now this routine adds phi args on this
   additional edge 'e' that new_head to second edge received as part of edge
   splitting.  */

/* FIXME tuples.  */
#if 0
static void
tree_lv_adjust_loop_header_phi (basic_block first, basic_block second,
				basic_block new_head, edge e)
{
  tree phi1, phi2;
  edge e2 = find_edge (new_head, second);

  /* Because NEW_HEAD has been created by splitting SECOND's incoming
     edge, we should always have an edge from NEW_HEAD to SECOND.  */
  gcc_assert (e2 != NULL);

  /* Browse all 'second' basic block phi nodes and add phi args to
     edge 'e' for 'first' head. PHI args are always in correct order.  */

  for (phi2 = phi_nodes (second), phi1 = phi_nodes (first);
       phi2 && phi1;
       phi2 = PHI_CHAIN (phi2),  phi1 = PHI_CHAIN (phi1))
    {
      tree def = PHI_ARG_DEF (phi2, e2->dest_idx);
      add_phi_arg (phi1, def, e);
    }
}

/* Adds a if else statement to COND_BB with condition COND_EXPR.
   SECOND_HEAD is the destination of the THEN and FIRST_HEAD is
   the destination of the ELSE part.  */

static void
tree_lv_add_condition_to_bb (basic_block first_head ATTRIBUTE_UNUSED,
			     basic_block second_head ATTRIBUTE_UNUSED,
			     basic_block cond_bb, void *cond_e)
{
  gimple_stmt_iterator gsi;
  tree new_cond_expr = NULL_TREE;
  tree cond_expr = (tree) cond_e;
  edge e0;

  /* Build new conditional expr */
  new_cond_expr = build3 (COND_EXPR, void_type_node, cond_expr,
			  NULL_TREE, NULL_TREE);

  /* Add new cond in cond_bb.  */
  gsi = gsi_start (cond_bb);
  gsi_insert_after (&gsi, new_cond_expr, GSI_NEW_STMT);
  /* Adjust edges appropriately to connect new head with first head
     as well as second head.  */
  e0 = single_succ_edge (cond_bb);
  e0->flags &= ~EDGE_FALLTHRU;
  e0->flags |= EDGE_FALSE_VALUE;
}
#endif

struct cfg_hooks gimple_cfg_hooks = {
  "gimple",
  tree_verify_flow_info,
  gimple_dump_bb,		/* dump_bb  */
  create_bb,			/* create_basic_block  */
  0 /* FIXME tuples tree_redirect_edge_and_branch */,/* redirect_edge_and_branch  */
  0 /* FIXME tuples tree_redirect_edge_and_branch_force */,/* redirect_edge_and_branch_force  */
  tree_can_remove_branch_p,	/* can_remove_branch_p  */
  remove_bb,			/* delete_basic_block  */
  0 /* FIXME tuples gimple_split_block */,		/* split_block  */
  tree_move_block_after,	/* move_block_after  */
  tree_can_merge_blocks_p,	/* can_merge_blocks_p  */
  tree_merge_blocks,		/* merge_blocks  */
  tree_predict_edge,		/* predict_edge  */
  tree_predicted_by_p,		/* predicted_by_p  */
  tree_can_duplicate_bb_p,	/* can_duplicate_block_p  */
  0 /* FIXME tuples tree_duplicate_bb */,		/* duplicate_block  */
  tree_split_edge,		/* split_edge  */
  tree_make_forwarder_block,	/* make_forward_block  */
  NULL,				/* tidy_fallthru_edge  */
  0 /* FIXME tuples tree_block_ends_with_call_p */,	/* block_ends_with_call_p */
  0 /* FIXME tuples tree_block_ends_with_condjump_p */, /* block_ends_with_condjump_p */
  0 /* FIXME tuples tree_flow_call_edges_add */,     /* flow_call_edges_add */
  tree_execute_on_growing_pred,	/* execute_on_growing_pred */
  tree_execute_on_shrinking_pred, /* execute_on_shrinking_pred */
  tree_duplicate_loop_to_header_edge, /* duplicate loop for trees */
  0 /* FIXME tuples tree_lv_add_condition_to_bb */, /* lv_add_condition_to_bb */
  0 /* FIXME tuples tree_lv_adjust_loop_header_phi */, /* lv_adjust_loop_header_phi*/
  extract_true_false_edges_from_block, /* extract_cond_bb_edges */
  flush_pending_stmts		/* flush_pending_stmts */
};


/* Split all critical edges.  */

static unsigned int
split_critical_edges (void)
{
  basic_block bb;
  edge e;
  edge_iterator ei;

  /* split_edge can redirect edges out of SWITCH_EXPRs, which can get
     expensive.  So we want to enable recording of edge to CASE_LABEL_EXPR
     mappings around the calls to split_edge.  */
  start_recording_case_labels ();
  FOR_ALL_BB (bb)
    {
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (EDGE_CRITICAL_P (e) && !(e->flags & EDGE_ABNORMAL))
	  {
	    split_edge (e);
	  }
    }
  end_recording_case_labels ();
  return 0;
}

struct tree_opt_pass pass_split_crit_edges =
{
  "crited",                          /* name */
  NULL,                          /* gate */
  split_critical_edges,          /* execute */
  NULL,                          /* sub */
  NULL,                          /* next */
  0,                             /* static_pass_number */
  TV_TREE_SPLIT_EDGES,           /* tv_id */
  PROP_cfg,                      /* properties required */
  PROP_no_crit_edges,            /* properties_provided */
  0,                             /* properties_destroyed */
  0,                             /* todo_flags_start */
  TODO_dump_func,                /* todo_flags_finish */
  0                              /* letter */
  ,0				 /* works_with_tuples_p */
};


/* Return EXP if it is a valid GIMPLE rvalue, else gimplify it into
   a temporary, make sure and register it to be renamed if necessary,
   and finally return the temporary.  Put the statements to compute
   EXP before the current statement in GSI.  */

tree
gimplify_val (gimple_stmt_iterator *gsi ATTRIBUTE_UNUSED, tree type ATTRIBUTE_UNUSED, tree exp ATTRIBUTE_UNUSED)
{
  /* FIXME tuples  */
#if 0
  tree t, new_stmt, orig_stmt;

  if (is_gimple_val (exp))
    return exp;

  t = make_rename_temp (type, NULL);
  new_stmt = build_gimple_modify_stmt (t, exp);

  orig_stmt = gsi_stmt (*gsi);
  SET_EXPR_LOCUS (new_stmt, EXPR_LOCUS (orig_stmt));
  TREE_BLOCK (new_stmt) = TREE_BLOCK (orig_stmt);

  gsi_insert_before (gsi, new_stmt, GSI_SAME_STMT);
  if (gimple_in_ssa_p (cfun))
    mark_symbols_for_renaming (new_stmt);

  return t;
#else
  gcc_unreachable ();
#endif
}

/* Build a ternary operation and gimplify it.  Emit code before GSI.
   Return the gimple_val holding the result.  */

tree
gimplify_build3 (gimple_stmt_iterator *gsi, enum tree_code code,
		 tree type, tree a, tree b, tree c)
{
  tree ret;

  ret = fold_build3 (code, type, a, b, c);
  STRIP_NOPS (ret);

  return gimplify_val (gsi, type, ret);
}

/* Build a binary operation and gimplify it.  Emit code before GSI.
   Return the gimple_val holding the result.  */

tree
gimplify_build2 (gimple_stmt_iterator *gsi, enum tree_code code,
		 tree type, tree a, tree b)
{
  tree ret;

  ret = fold_build2 (code, type, a, b);
  STRIP_NOPS (ret);

  return gimplify_val (gsi, type, ret);
}

/* Build a unary operation and gimplify it.  Emit code before GSI.
   Return the gimple_val holding the result.  */

tree
gimplify_build1 (gimple_stmt_iterator *gsi, enum tree_code code, tree type,
		 tree a)
{
  tree ret;

  ret = fold_build1 (code, type, a);
  STRIP_NOPS (ret);

  return gimplify_val (gsi, type, ret);
}



/* Emit return warnings.  */

/* FIXME tuples  */
#if 0
static unsigned int
execute_warn_function_return (void)
{
#ifdef USE_MAPPED_LOCATION
  source_location location;
#else
  location_t *locus;
#endif
  tree last;
  edge e;
  edge_iterator ei;

  /* If we have a path to EXIT, then we do return.  */
  if (TREE_THIS_VOLATILE (cfun->decl)
      && EDGE_COUNT (EXIT_BLOCK_PTR->preds) > 0)
    {
#ifdef USE_MAPPED_LOCATION
      location = UNKNOWN_LOCATION;
#else
      locus = NULL;
#endif
      FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
	{
	  last = last_stmt (e->src);
	  if (TREE_CODE (last) == RETURN_EXPR
#ifdef USE_MAPPED_LOCATION
	      && (location = EXPR_LOCATION (last)) != UNKNOWN_LOCATION)
#else
	      && (locus = EXPR_LOCUS (last)) != NULL)
#endif
	    break;
	}
#ifdef USE_MAPPED_LOCATION
      if (location == UNKNOWN_LOCATION)
	location = cfun->function_end_locus;
      warning (0, "%H%<noreturn%> function does return", &location);
#else
      if (!locus)
	locus = &cfun->function_end_locus;
      warning (0, "%H%<noreturn%> function does return", locus);
#endif
    }

  /* If we see "return;" in some basic block, then we do reach the end
     without returning a value.  */
  else if (warn_return_type
	   && !TREE_NO_WARNING (cfun->decl)
	   && EDGE_COUNT (EXIT_BLOCK_PTR->preds) > 0
	   && !VOID_TYPE_P (TREE_TYPE (TREE_TYPE (cfun->decl))))
    {
      FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
	{
	  tree last = last_stmt (e->src);
	  if (TREE_CODE (last) == RETURN_EXPR
	      && TREE_OPERAND (last, 0) == NULL
	      && !TREE_NO_WARNING (last))
	    {
#ifdef USE_MAPPED_LOCATION
	      location = EXPR_LOCATION (last);
	      if (location == UNKNOWN_LOCATION)
		  location = cfun->function_end_locus;
	      warning (0, "%Hcontrol reaches end of non-void function", &location);
#else
	      locus = EXPR_LOCUS (last);
	      if (!locus)
		locus = &cfun->function_end_locus;
	      warning (0, "%Hcontrol reaches end of non-void function", locus);
#endif
	      TREE_NO_WARNING (cfun->decl) = 1;
	      break;
	    }
	}
    }
  return 0;
}
#endif


/* Given a basic block B which ends with a conditional and has
   precisely two successors, determine which of the edges is taken if
   the conditional is true and which is taken if the conditional is
   false.  Set TRUE_EDGE and FALSE_EDGE appropriately.  */

void
extract_true_false_edges_from_block (basic_block b,
				     edge *true_edge,
				     edge *false_edge)
{
  edge e = EDGE_SUCC (b, 0);

  if (e->flags & EDGE_TRUE_VALUE)
    {
      *true_edge = e;
      *false_edge = EDGE_SUCC (b, 1);
    }
  else
    {
      *false_edge = e;
      *true_edge = EDGE_SUCC (b, 1);
    }
}

struct tree_opt_pass pass_warn_function_return =
{
  NULL,					/* name */
  NULL,					/* gate */
  0 /* FIXME tuples execute_warn_function_return */,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,					/* todo_flags_finish */
  0					/* letter */
  ,0					/* works_with_tuples_p */
};

/* Emit noreturn warnings.  */

static unsigned int
execute_warn_function_noreturn (void)
{
  if (warn_missing_noreturn
      && !TREE_THIS_VOLATILE (cfun->decl)
      && EDGE_COUNT (EXIT_BLOCK_PTR->preds) == 0
      && !lang_hooks.function.missing_noreturn_ok_p (cfun->decl))
    warning (OPT_Wmissing_noreturn, "%Jfunction might be possible candidate "
	     "for attribute %<noreturn%>",
	     cfun->decl);
  return 0;
}

struct tree_opt_pass pass_warn_function_noreturn =
{
  NULL,					/* name */
  NULL,					/* gate */
  execute_warn_function_noreturn,	/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,					/* todo_flags_finish */
  0					/* letter */
  ,0					/* works_with_tuples_p */
};
