/* Basic block reordering routines for the GNU compiler.
   Copyright (C) 2000, 2001 Free Software Foundation, Inc.

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
#include "tree.h"
#include "rtl.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "insn-config.h"
#include "output.h"
#include "function.h"
#include "obstack.h"
#include "cfglayout.h"

/* The contents of the current function definition are allocated
   in this obstack, and all are freed at the end of the function.  */
extern struct obstack flow_obstack;

/* Holds the interesting trailing notes for the function.  */
static rtx function_footer;

static rtx skip_insns_after_block	PARAMS ((basic_block));
static void record_effective_endpoints	PARAMS ((void));
static rtx label_for_bb			PARAMS ((basic_block));
static void fixup_reorder_chain		PARAMS ((void));

static void set_block_levels		PARAMS ((tree, int));
static void change_scope		PARAMS ((rtx, tree, tree));

void verify_insn_chain			PARAMS ((void));
static void fixup_fallthru_exit_predecessor PARAMS ((void));
static void cleanup_unconditional_jumps	PARAMS ((struct loops *));
static rtx unlink_insn_chain PARAMS ((rtx, rtx));
static rtx duplicate_insn_chain PARAMS ((rtx, rtx));

/* Map insn uid to lexical block.  */
static varray_type insn_scopes;

static rtx
unlink_insn_chain (first, last)
     rtx first;
     rtx last;
{
  rtx prevfirst = PREV_INSN (first);
  rtx nextlast = NEXT_INSN (last);

  PREV_INSN (first) = NULL;
  NEXT_INSN (last) = NULL;
  if (prevfirst)
    NEXT_INSN (prevfirst) = nextlast;
  if (nextlast)
    PREV_INSN (nextlast) = prevfirst;
  else
    set_last_insn (prevfirst);
  if (!prevfirst)
    set_first_insn (nextlast);
  return first;
}

/* Skip over inter-block insns occurring after BB which are typically
   associated with BB (e.g., barriers). If there are any such insns,
   we return the last one. Otherwise, we return the end of BB.  */

static rtx
skip_insns_after_block (bb)
     basic_block bb;
{
  rtx insn, last_insn, next_head, prev;

  next_head = NULL_RTX;
  if (bb->index + 1 != n_basic_blocks)
    next_head = BASIC_BLOCK (bb->index + 1)->head;

  for (last_insn = insn = bb->end; (insn = NEXT_INSN (insn)) != 0; )
    {
      if (insn == next_head)
	break;

      switch (GET_CODE (insn))
	{
	case BARRIER:
	  last_insn = insn;
	  continue;

	case NOTE:
	  switch (NOTE_LINE_NUMBER (insn))
	    {
	    case NOTE_INSN_LOOP_END:
	    case NOTE_INSN_BLOCK_END:
	      last_insn = insn;
	      continue;
	    case NOTE_INSN_DELETED:
	    case NOTE_INSN_DELETED_LABEL:
	      continue;

	    default:
	      continue;
	      break;
	    }
	  break;

	case CODE_LABEL:
	  if (NEXT_INSN (insn)
	      && GET_CODE (NEXT_INSN (insn)) == JUMP_INSN
	      && (GET_CODE (PATTERN (NEXT_INSN (insn))) == ADDR_VEC
	          || GET_CODE (PATTERN (NEXT_INSN (insn))) == ADDR_DIFF_VEC))
	    {
	      insn = NEXT_INSN (insn);
	      last_insn = insn;
	      continue;
	    }
          break;

	default:
	  break;
	}

      break;
    }

  /* It is possible to hit contradictory sequence.  For instance:
    
     jump_insn
     NOTE_INSN_LOOP_BEG
     barrier

     Where barrier belongs to jump_insn, but the note does not.  This can be
     created by removing the basic block originally following
     NOTE_INSN_LOOP_BEG.  In such case reorder the notes.  */

  for (insn = last_insn; insn != bb->end; insn = prev)
    {
      prev = PREV_INSN (insn);
      if (GET_CODE (insn) == NOTE)
	switch (NOTE_LINE_NUMBER (insn))
	  {
          case NOTE_INSN_LOOP_END:
          case NOTE_INSN_BLOCK_END:
          case NOTE_INSN_DELETED:
          case NOTE_INSN_DELETED_LABEL:
	    continue;
          default:
	    reorder_insns (insn, insn, last_insn);
        }
    }

  return last_insn;
}

/* Locate or create a label for a given basic block.  */

static rtx
label_for_bb (bb)
     basic_block bb;
{
  rtx label = bb->head;

  if (GET_CODE (label) != CODE_LABEL)
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, "Emitting label for block %d\n", bb->index);

      label = block_label (bb);
    }

  return label;
}

/* Locate the effective beginning and end of the insn chain for each
   block, as defined by skip_insns_after_block above.  */

static void
record_effective_endpoints ()
{
  rtx next_insn = get_insns ();
  int i;
  
  for (i = 0; i < n_basic_blocks; i++)
    {
      basic_block bb = BASIC_BLOCK (i);
      rtx end;

      if (PREV_INSN (bb->head) && next_insn != bb->head)
	RBI (bb)->header = unlink_insn_chain (next_insn,
					      PREV_INSN (bb->head));
      end = skip_insns_after_block (bb);
      if (NEXT_INSN (bb->end) && bb->end != end)
        RBI (bb)->footer = unlink_insn_chain (NEXT_INSN (bb->end), end);
      next_insn = NEXT_INSN (bb->end);
    }

  function_footer = next_insn;
  if (function_footer)
    function_footer = unlink_insn_chain (function_footer, get_last_insn ());
}

/* Build a varray mapping INSN_UID to lexical block.  Return it.  */

void
scope_to_insns_initialize ()
{
  tree block = NULL;
  rtx insn, next;

  VARRAY_TREE_INIT (insn_scopes, get_max_uid (), "insn scopes");

  for (insn = get_insns (); insn; insn = next)
    {
      next = NEXT_INSN (insn);

      if (active_insn_p (insn)
	  && GET_CODE (PATTERN (insn)) != ADDR_VEC
	  && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC)
	VARRAY_TREE (insn_scopes, INSN_UID (insn)) = block;
      else if (GET_CODE (insn) == NOTE)
	{
	  switch (NOTE_LINE_NUMBER (insn))
	    {
	    case NOTE_INSN_BLOCK_BEG:
	      block = NOTE_BLOCK (insn);
	      delete_insn (insn);
	      break;
	    case NOTE_INSN_BLOCK_END:
	      block = BLOCK_SUPERCONTEXT (block);
	      delete_insn (insn);
	      break;
	    default:
	      break;
	    }
	}
    }
}

/* For each lexical block, set BLOCK_NUMBER to the depth at which it is
   found in the block tree.  */

static void
set_block_levels (block, level)
     tree block;
     int level;
{
  while (block)
    {
      BLOCK_NUMBER (block) = level;
      set_block_levels (BLOCK_SUBBLOCKS (block), level + 1);
      block = BLOCK_CHAIN (block);
    }
}

/* Emit lexical block notes needed to change scope from S1 to S2.  */

static void
change_scope (orig_insn, s1, s2)
     rtx orig_insn;
     tree s1, s2;
{
  rtx insn = orig_insn;
  tree com = NULL_TREE;
  tree ts1 = s1, ts2 = s2;
  tree s;

  while (ts1 != ts2)
    {
      if (ts1 == NULL || ts2 == NULL)
	abort ();
      if (BLOCK_NUMBER (ts1) > BLOCK_NUMBER (ts2))
	ts1 = BLOCK_SUPERCONTEXT (ts1);
      else if (BLOCK_NUMBER (ts1) < BLOCK_NUMBER (ts2))
	ts2 = BLOCK_SUPERCONTEXT (ts2);
      else
	{
	  ts1 = BLOCK_SUPERCONTEXT (ts1);
	  ts2 = BLOCK_SUPERCONTEXT (ts2);
	}
    }
  com = ts1;

  /* Close scopes.  */
  s = s1;
  while (s != com)
    {
      rtx note = emit_note_before (NOTE_INSN_BLOCK_END, insn);
      NOTE_BLOCK (note) = s;
      s = BLOCK_SUPERCONTEXT (s);
    }

  /* Open scopes.  */
  s = s2;
  while (s != com)
    {
      insn = emit_note_before (NOTE_INSN_BLOCK_BEG, insn);
      NOTE_BLOCK (insn) = s;
      s = BLOCK_SUPERCONTEXT (s);
    }
}

/* Rebuild all the NOTE_INSN_BLOCK_BEG and NOTE_INSN_BLOCK_END notes based
   on the scope tree and the newly reordered instructions.  */

void
scope_to_insns_finalize ()
{
  tree cur_block = DECL_INITIAL (cfun->decl);
  rtx insn, note;

  /* Tag the blocks with a depth number so that change_scope can find
     the common parent easily.  */
  set_block_levels (cur_block, 0);

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      tree this_block;

      if ((size_t) INSN_UID (insn) >= insn_scopes->num_elements)
	continue;
      this_block = VARRAY_TREE (insn_scopes, INSN_UID (insn));
      if (! this_block)
	continue;

      if (this_block != cur_block)
	{
	  change_scope (insn, cur_block, this_block);
	  cur_block = this_block;
	}
    }

  VARRAY_FREE (insn_scopes);

  /* change_scope emits before the insn, not after.  */
  note = emit_note (NULL, NOTE_INSN_DELETED);
  change_scope (note, cur_block, DECL_INITIAL (cfun->decl));
  delete_insn (note);

  reorder_blocks ();
}

/* Given a reorder chain, rearrange the code to match.  */

static void
fixup_reorder_chain ()
{
  basic_block bb, last_bb;
  int index;
  rtx insn = NULL;

  /* First do the bulk reordering -- rechain the blocks without regard to
     the needed changes to jumps and labels.  */

  for (bb = BASIC_BLOCK (0), index = 0;
       bb != 0;
       bb = RBI (bb)->next, index++)
    {
      last_bb = bb;
      if (RBI (bb)->header)
	{
	  if (insn)
	    NEXT_INSN (insn) = RBI (bb)->header;
	  else
	    set_first_insn (RBI (bb)->header);
	  PREV_INSN (RBI (bb)->header) = insn;
	  insn = RBI (bb)->header;
	  while (NEXT_INSN (insn))
	    insn = NEXT_INSN (insn);
	}
      if (insn)
	NEXT_INSN (insn) = bb->head;
      else
	set_first_insn (bb->head);
      PREV_INSN (bb->head) = insn;
      insn = bb->end;
      if (RBI (bb)->footer)
	{
	  NEXT_INSN (insn) = RBI (bb)->footer;
	  PREV_INSN (RBI (bb)->footer) = insn;
	  while (NEXT_INSN (insn))
	    insn = NEXT_INSN (insn);
	}
    }

  if (index != n_basic_blocks)
    abort ();

  NEXT_INSN (insn) = function_footer;
  if (function_footer)
    PREV_INSN (function_footer) = insn;

  while (NEXT_INSN (insn))
    insn = NEXT_INSN (insn);

  set_last_insn (insn);
#ifdef ENABLE_CHECKING
  verify_insn_chain ();
#endif

  /* Now add jumps and labels as needed to match the blocks new
     outgoing edges.  */

  for (bb = BASIC_BLOCK (0); bb ; bb = RBI (bb)->next)
    {
      edge e_fall, e_taken, e;
      rtx bb_end_insn;
      basic_block nb;

      if (bb->succ == NULL)
	continue;

      /* Find the old fallthru edge, and another non-EH edge for
	 a taken jump.  */
      e_taken = e_fall = NULL;
      for (e = bb->succ; e ; e = e->succ_next)
	if (e->flags & EDGE_FALLTHRU)
	  e_fall = e;
	else if (! (e->flags & EDGE_EH))
	  e_taken = e;

      /* In case we need to create edge into exit block,
	 we must ensure empty basic block to be last in the
	 instruction chain and redirect the edge there.  */
      if (e_fall && e_fall->dest == EXIT_BLOCK_PTR
	  && bb != last_bb)
	{
	  if (!forwarder_block_p (last_bb)
	      || last_bb->succ->dest != EXIT_BLOCK_PTR)
	    {
	      edge ne;
	      nb = create_basic_block (n_basic_blocks, NULL_RTX, NULL_RTX);
	      ne = make_edge (nb, EXIT_BLOCK_PTR, EDGE_FALLTHRU);
	      ne->probability = REG_BR_PROB_BASE;
	      alloc_aux_for_block (nb, sizeof (struct reorder_block_def));
	      RBI (nb)->visited = 1;
	      RBI (last_bb)->next = nb;
	      last_bb = nb;
	    }
	  redirect_edge_succ_nodup (e_fall, last_bb);
	  last_bb->frequency += EDGE_FREQUENCY (e_fall);
	  last_bb->count += e_fall->count;
	  last_bb->succ->count += e_fall->count;
	}

      bb_end_insn = bb->end;
      if (GET_CODE (bb_end_insn) == JUMP_INSN)
	{
	  if (any_condjump_p (bb_end_insn))
	    {
	      /* If the old fallthru is still next, nothing to do.  */
	      if (RBI (bb)->next == e_fall->dest
	          || (!RBI (bb)->next
		      && e_fall->dest == EXIT_BLOCK_PTR))
		continue;

	      /* There is one special case: if *neither* block is next,
		 such as happens at the very end of a function, then we'll
		 need to add a new unconditional jump.  Choose the taken
		 edge based on known or assumed probability.  */
	      if (RBI (bb)->next != e_taken->dest)
		{
		  rtx note = find_reg_note (bb_end_insn, REG_BR_PROB, 0);

		  if (note
		      && INTVAL (XEXP (note, 0)) < REG_BR_PROB_BASE / 2
		      && invert_jump (bb_end_insn,
				      label_for_bb (e_fall->dest), 0))
		    {
		      e_fall->flags &= ~EDGE_FALLTHRU;
		      e_taken->flags |= EDGE_FALLTHRU;
		      update_br_prob_note (bb);
		      e = e_fall, e_fall = e_taken, e_taken = e;
		    }
		}

	      /* Otherwise we can try to invert the jump.  This will 
		 basically never fail, however, keep up the pretense.  */
	      else if (invert_jump (bb_end_insn,
				    label_for_bb (e_fall->dest), 0))
		{
		  e_fall->flags &= ~EDGE_FALLTHRU;
		  e_taken->flags |= EDGE_FALLTHRU;
		  update_br_prob_note (bb);
		  continue;
		}
	    }
	  else if (returnjump_p (bb_end_insn))
	    continue;
	  else
	    {
	      /* Otherwise we have some switch or computed jump.  In the
		 99% case, there should not have been a fallthru edge.  */
	      if (! e_fall)
		continue;

#ifdef CASE_DROPS_THROUGH
	      /* Except for VAX.  Since we didn't have predication for the
		 tablejump, the fallthru block should not have moved.  */
	      if (RBI (bb)->next == e_fall->dest)
		continue;
	      bb_end_insn = skip_insns_after_block (bb);
#else
	      abort ();
#endif
	    }
	}
      else
	{
	  /* No fallthru implies a noreturn function with EH edges, or
	     something similarly bizarre.  In any case, we don't need to
	     do anything.  */
	  if (! e_fall)
	    continue;

	  /* If the fallthru block is still next, nothing to do.  */
	  if (RBI (bb)->next == e_fall->dest)
	    continue;

	  /* A fallthru to exit block.  */
	  if (!RBI (bb)->next && e_fall->dest == EXIT_BLOCK_PTR)
	    continue;
	}

      /* We got here if we need to add a new jump insn.  */
      nb = force_nonfallthru (e_fall);
      if (nb)
	{
	  alloc_aux_for_block (nb, sizeof (struct reorder_block_def));
	  RBI (nb)->visited = 1;
	  RBI (nb)->next = RBI (bb)->next;
	  RBI (bb)->next = nb;
	  /* Don't process this new block.  */
	  bb = nb;
	}
    }

  /* Put basic_block_info in the new order.  */

  if (rtl_dump_file)
    {
      fprintf (rtl_dump_file, "Reordered sequence:\n");
      for (bb = BASIC_BLOCK (0), index = 0; bb; bb = RBI (bb)->next, index ++)
	{
	  fprintf (rtl_dump_file, " %i ", index);
	  if (RBI (bb)->original)
	    fprintf (rtl_dump_file, "duplicate of %i ",
		     RBI (bb)->original->index);
	  else if (forwarder_block_p (bb) && GET_CODE (bb->head) != CODE_LABEL)
	    fprintf (rtl_dump_file, "compensation ");
	  else
	    fprintf (rtl_dump_file, "bb %i ", bb->index);
	  fprintf (rtl_dump_file, " [%i]\n", bb->frequency);
	}
    }

  for (bb = BASIC_BLOCK (0), index = 0; bb; bb = RBI (bb)->next, index ++)
    {
      bb->index = index;
      BASIC_BLOCK (index) = bb;
    }
}

/* Perform sanity checks on the insn chain.
   1. Check that next/prev pointers are consistent in both the forward and
      reverse direction.
   2. Count insns in chain, going both directions, and check if equal.
   3. Check that get_last_insn () returns the actual end of chain.  */

void
verify_insn_chain ()
{
  rtx x, prevx, nextx;
  int insn_cnt1, insn_cnt2;

  for (prevx = NULL, insn_cnt1 = 1, x = get_insns ();
       x != 0;
       prevx = x, insn_cnt1++, x = NEXT_INSN (x))
    if (PREV_INSN (x) != prevx)
      abort ();

  if (prevx != get_last_insn ())
    abort ();

  for (nextx = NULL, insn_cnt2 = 1, x = get_last_insn ();
       x != 0;
       nextx = x, insn_cnt2++, x = PREV_INSN (x))
    if (NEXT_INSN (x) != nextx)
      abort ();

  if (insn_cnt1 != insn_cnt2)
    abort ();
}

/* Remove any unconditional jumps and forwarder block creating fallthru
   edges instead.  During BB reordering fallthru edges are not required
   to target next basic block in the linear CFG layout, so the unconditional
   jumps are not needed.  If LOOPS is not null, also update loop structure &
   dominators.  */

static void
cleanup_unconditional_jumps (loops)
     struct loops *loops;
{
  int i;
  for (i = 0; i < n_basic_blocks; i++)
    {
      basic_block bb = BASIC_BLOCK (i);

      if (!bb->succ)
	continue;
      if (bb->succ->flags & EDGE_FALLTHRU)
	continue;
      if (!bb->succ->succ_next)
	{
	  rtx insn;
	  if (GET_CODE (bb->head) != CODE_LABEL && forwarder_block_p (bb) && i)
	    {
	      basic_block prev = BASIC_BLOCK (--i);

	      if (rtl_dump_file)
		fprintf (rtl_dump_file, "Removing forwarder BB %i\n",
			 bb->index);

	      if (loops)
		{
		  /* bb cannot be loop header, as it only has one entry
		     edge.  It could be a loop latch.  */
		  if (bb->loop_father->header == bb)
		    abort ();

		  if (bb->loop_father->latch == bb)
		    bb->loop_father->latch = bb->pred->src;

		  if (get_immediate_dominator
		      (loops->cfg.dom, bb->succ->dest) == bb)
		    set_immediate_dominator
		      (loops->cfg.dom, bb->succ->dest, bb->pred->src);

		  remove_bb_from_loops (bb);
		}

	      redirect_edge_succ (bb->pred, bb->succ->dest);
	      flow_delete_block (bb);
	      bb = prev;
	    }
	  else if (simplejump_p (bb->end))
	    {
	      rtx jump = bb->end;

	      if (rtl_dump_file)
		fprintf (rtl_dump_file, "Removing jump %i in BB %i\n",
			 INSN_UID (jump), bb->index);
	      delete_insn (jump);
	      bb->succ->flags |= EDGE_FALLTHRU;
	    }
	  else
	    continue;

	  /* Cleanup barriers and delete ADDR_VECs in a way as they are belonging
             to removed tablejump anyway.  */
	  insn = NEXT_INSN (bb->end);
	  while (insn
		 && (GET_CODE (insn) != NOTE
		     || NOTE_LINE_NUMBER (insn) != NOTE_INSN_BASIC_BLOCK))
	    {
	      rtx next = NEXT_INSN (insn);

	      if (GET_CODE (insn) == BARRIER)
		delete_barrier (insn);
	      else if (GET_CODE (insn) == JUMP_INSN)
		delete_insn_chain (PREV_INSN (insn), insn);
	      else if (GET_CODE (insn) == CODE_LABEL)
		;
	      else if (GET_CODE (insn) != NOTE)
		abort ();

	      insn = next;
	    }
	}
    }
}

/* The block falling through to exit must be the last one in the
   reordered chain.  Ensure that this condition is met.  */
static void
fixup_fallthru_exit_predecessor ()
{
  edge e;
  basic_block bb = NULL;

  for (e = EXIT_BLOCK_PTR->pred; e; e = e->pred_next)
    if (e->flags & EDGE_FALLTHRU)
      bb = e->src;

  if (bb && RBI (bb)->next)
    {
      basic_block c = BASIC_BLOCK (0);

      while (RBI (c)->next != bb)
	c = RBI (c)->next;

      RBI (c)->next = RBI (bb)->next;
      while (RBI (c)->next)
	c = RBI (c)->next;

      RBI (c)->next = bb;
      RBI (bb)->next = NULL;
    }
}

/* Return true in case it is possible to duplicate the basic block BB.  */

bool
cfg_layout_can_duplicate_bb_p (bb)
     basic_block bb;
{
  rtx next;
  edge s;

  if (bb == EXIT_BLOCK_PTR || bb == ENTRY_BLOCK_PTR)
    return false;

  /* Duplicating fallthru block to exit would require adding an jump
     and splitting the real last BB.  */
  for (s = bb->succ; s; s = s->succ_next)
    if (s->dest == EXIT_BLOCK_PTR && (s->flags & EDGE_FALLTHRU))
       return false;

  /* Do not attempt to duplicate tablejumps, as we need to unshare
     the dispatch table.  This is dificult to do, as the instructions
     computing jump destination may be hoisted outside the basic block.  */
  if (GET_CODE (bb->end) == JUMP_INSN && JUMP_LABEL (bb->end)
      && (next = next_nonnote_insn (JUMP_LABEL (bb->end)))
      && GET_CODE (next) == JUMP_INSN
      && (GET_CODE (PATTERN (next)) == ADDR_VEC
	  || GET_CODE (PATTERN (next)) == ADDR_DIFF_VEC))
    return false;
  return true;
}

static rtx
duplicate_insn_chain (from, to)
     rtx from, to;
{
  rtx insn, last;

  /* Avoid updating of boundaries of previous basic block.  The
     note will get removed from insn stream in fixup.  */
  last = emit_note (NULL, NOTE_INSN_DELETED);

  /* Create copy at the end of INSN chain.  The chain will
     be reordered later.  */
  for (insn = from; insn != NEXT_INSN (to); insn = NEXT_INSN (insn))
    {
      rtx new;
      switch (GET_CODE (insn))
	{
	case INSN:
	case CALL_INSN:
	case JUMP_INSN:
	  /* Avoid copying of dispatch tables.  We never duplicate
	     tablejumps, so this can hit only in case the table got
	     moved far from original jump.  */
	  if (GET_CODE (PATTERN (insn)) == ADDR_VEC
	      || GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC)
	    break;
	  new = emit_copy_of_insn_after (insn, get_last_insn ());
	  /* Record the INSN_SCOPE.  */
	  VARRAY_GROW (insn_scopes, INSN_UID (new) + 1);
	  VARRAY_TREE (insn_scopes, INSN_UID (new))
	    = VARRAY_TREE (insn_scopes, INSN_UID (insn));
	  break;

	case CODE_LABEL:
	  break;

	case BARRIER:
	  emit_barrier ();
	  break;

	case NOTE:
	  switch (NOTE_LINE_NUMBER (insn))
	    {
	      /* In case prologue is empty and function contain label
	         in first BB, we may want to copy the block.  */
	    case NOTE_INSN_PROLOGUE_END:

	    case NOTE_INSN_LOOP_VTOP:
	    case NOTE_INSN_LOOP_CONT:
	    case NOTE_INSN_LOOP_BEG:
	    case NOTE_INSN_LOOP_END:
	      /* Strip down the loop notes - we don't really want to keep
	         them consistent in loop copies.  */
	    case NOTE_INSN_DELETED:
	    case NOTE_INSN_DELETED_LABEL:
	      /* No problem to strip these.  */
	    case NOTE_INSN_EPILOGUE_BEG:
	    case NOTE_INSN_FUNCTION_END:
	      /* Debug code expect these notes to exist just once.
	         Keep them in the master copy.
	         ??? It probably makes more sense to duplicate them for each
	         epilogue copy.  */
	    case NOTE_INSN_FUNCTION_BEG:
	      /* There is always just single entry to function.  */
	    case NOTE_INSN_BASIC_BLOCK:
	      break;

	      /* There is no purpose to duplicate prologue.  */
	    case NOTE_INSN_BLOCK_BEG:
	    case NOTE_INSN_BLOCK_END:
	      /* The BLOCK_BEG/BLOCK_END notes should be eliminated when BB
	         reordering is in the progress.  */
	    case NOTE_INSN_EH_REGION_BEG:
	    case NOTE_INSN_EH_REGION_END:
	    case NOTE_INSN_RANGE_BEG:
	    case NOTE_INSN_RANGE_END:
	      /* Should never exist at BB duplication time.  */
	      abort ();
	      break;
	    case NOTE_INSN_REPEATED_LINE_NUMBER:
	      emit_note (NOTE_SOURCE_FILE (insn), NOTE_LINE_NUMBER (insn));
	      break;

	    default:
	      if (NOTE_LINE_NUMBER (insn) < 0)
		abort ();
	      /* It is possible that no_line_number is set and the note
	         won't be emitted.  */
	      emit_note (NOTE_SOURCE_FILE (insn), NOTE_LINE_NUMBER (insn));
	    }
	  break;
	default:
	  abort ();
	}
    }
  insn = NEXT_INSN (last);
  delete_insn (last);
  return insn;
}

/* Redirect Edge to DEST.  */
void
cfg_layout_redirect_edge (e, dest)
     edge e;
     basic_block dest;
{
  int old_index = dest->index;
  basic_block src = e->src;

  /* Avoid redirect_edge_and_branch from overactive optimizing.  */
  dest->index = n_basic_blocks + 1;
  if (e->flags & EDGE_FALLTHRU)
    {
      /* In case we are redirecting fallthru edge to the branch edge
         of conditional jump, remove it.  */
      if (src->succ->succ_next
	  && !src->succ->succ_next->succ_next)
	{
	  edge s = e->succ_next ? e->succ_next : src->succ;
	  if (s->dest == dest
	      && any_condjump_p (src->end)
	      && onlyjump_p (src->end))
	    delete_insn (src->end);
	}
      redirect_edge_succ_nodup (e, dest);
    }
  else
    redirect_edge_and_branch (e, dest);

  /* We don't want simplejumps in the insn stream during cfglayout.  */
  if (simplejump_p (src->end))
    {
      delete_insn (src->end);
      delete_barrier (NEXT_INSN (src->end));
      src->succ->flags |= EDGE_FALLTHRU;
    }
  dest->index = old_index;
}

/* Create an duplicate of the basic block BB and redirect edge E into it.  */

basic_block
cfg_layout_duplicate_bb (bb, e)
     basic_block bb;
     edge e;
{
  rtx insn;
  edge s, n;
  basic_block new_bb;
  gcov_type new_count = e ? e->count : 0;

  if (bb->count < new_count)
    new_count = bb->count;
  if (!bb->pred)
    abort ();
#ifdef ENABLE_CHECKING
  if (!cfg_layout_can_duplicate_bb_p (bb))
    abort ();
#endif

  insn = duplicate_insn_chain (bb->head, bb->end);
  new_bb = create_basic_block (n_basic_blocks, insn,
		 	       insn ? get_last_insn () : NULL);
  alloc_aux_for_block (new_bb, sizeof (struct reorder_block_def));

  if (RBI (bb)->header)
    {
      insn = RBI (bb)->header;
      while (NEXT_INSN (insn))
	insn = NEXT_INSN (insn);
      insn = duplicate_insn_chain (RBI (bb)->header, insn);
      if (insn)
	RBI (new_bb)->header = unlink_insn_chain (insn, get_last_insn ());
    }

  if (RBI (bb)->footer)
    {
      insn = RBI (bb)->footer;
      while (NEXT_INSN (insn))
	insn = NEXT_INSN (insn);
      insn = duplicate_insn_chain (RBI (bb)->footer, insn);
      if (insn)
	RBI (new_bb)->footer = unlink_insn_chain (insn, get_last_insn ());
    }

  if (bb->global_live_at_start)
    {
      new_bb->global_live_at_start = OBSTACK_ALLOC_REG_SET (&flow_obstack);
      new_bb->global_live_at_end = OBSTACK_ALLOC_REG_SET (&flow_obstack);
      COPY_REG_SET (new_bb->global_live_at_start, bb->global_live_at_start);
      COPY_REG_SET (new_bb->global_live_at_end, bb->global_live_at_end);
    }

  new_bb->loop_depth = bb->loop_depth;
  new_bb->flags = bb->flags;
  for (s = bb->succ; s; s = s->succ_next)
    {
      n = make_edge (new_bb, s->dest, s->flags);
      n->probability = s->probability;
      if (new_count)
	/* Take care for overflows!  */
	n->count = s->count * (new_count * 10000 / bb->count) / 10000;
      else
	n->count = 0;
      s->count -= n->count;
    }

  new_bb->count = new_count;
  bb->count -= new_count;

  if (e)
   {
     new_bb->frequency = EDGE_FREQUENCY (e);
     bb->frequency -= EDGE_FREQUENCY (e);

     cfg_layout_redirect_edge (e, new_bb);
   }

  if (bb->count < 0)
    bb->count = 0;
  if (bb->frequency < 0)
    bb->frequency = 0;

  RBI (new_bb)->original = bb;
  RBI (bb)->copy = new_bb;
  return new_bb;
}

/* Main entry point to this module - initialize the datastructures for
   CFG layout changes.  It keeps LOOPS up-to-date if not null.  */

void
cfg_layout_initialize (loops)
     struct loops *loops;
{
  /* Our algorithm depends on fact that there are now dead jumptables
     around the code.  */
  alloc_aux_for_blocks (sizeof (struct reorder_block_def));
  cleanup_unconditional_jumps (loops);
  scope_to_insns_initialize ();
  record_effective_endpoints ();
  verify_insn_chain ();
}

/* Finalize the changes: reorder insn list according to the sequence, enter
   compensation code, rebuild scope forest.  */

void
cfg_layout_finalize ()
{
  fixup_fallthru_exit_predecessor ();
  fixup_reorder_chain ();

#ifdef ENABLE_CHECKING
  verify_insn_chain ();
#endif

  scope_to_insns_finalize ();

  free_aux_for_blocks ();

#ifdef ENABLE_CHECKING
  verify_flow_info ();
#endif
}
