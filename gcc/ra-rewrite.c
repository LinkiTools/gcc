/* Graph coloring register allocator
   Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
   Contributed by Michael Matz <matz@suse.de>
   and Daniel Berlin <dan@cgsoftware.com>.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software
   Foundation; either version 2, or (at your option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
   details.

   You should have received a copy of the GNU General Public License along
   with GCC; see the file COPYING.  If not, write to the Free Software
   Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "rtl.h"
#include "tm_p.h"
#include "insn-config.h"
#include "recog.h"
#include "function.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "df.h"
#include "expr.h"
#include "output.h"
#include "except.h"
#include "obstack.h"
#include "ra.h"
#include "reload.h"
#include "pre-reload.h"

extern int flag_ra_test;
extern struct df2ra df2ra;

/* This file is part of the graph coloring register allocator, and
   contains the functions to change the insn stream.  I.e. it adds
   spill code, rewrites insns to use the new registers after
   coloring and deletes coalesced moves.  */

struct rewrite_info;
struct rtx_list;

static void spill_coalescing PARAMS ((sbitmap, sbitmap));
static unsigned HOST_WIDE_INT spill_prop_savings PARAMS ((struct web *,
							  sbitmap));
static void spill_prop_insert PARAMS ((struct web *, sbitmap, sbitmap));
static int spill_propagation PARAMS ((sbitmap, sbitmap, sbitmap));
static void spill_coalprop PARAMS ((void));
static void allocate_spill_web PARAMS ((struct web *));
static void choose_spill_colors PARAMS ((void));
static void rewrite_program PARAMS ((bitmap));
static void remember_slot PARAMS ((struct rtx_list **, rtx));
static int slots_overlap_p PARAMS ((rtx, rtx));
static void delete_overlapping_slots PARAMS ((struct rtx_list **, rtx));
static void delete_overlapping_uses PARAMS ((rtx *, void *));
static int slot_member_p PARAMS ((struct rtx_list *, rtx));
static void insert_stores PARAMS ((bitmap));
static int spill_same_color_p PARAMS ((struct web *, struct web *));
static unsigned int is_partly_live_1 PARAMS ((sbitmap, struct web *));
static void update_spill_colors PARAMS ((HARD_REG_SET *, struct web *, int));
static int spill_is_free PARAMS ((HARD_REG_SET *, struct web *));
static void emit_loads PARAMS ((struct rewrite_info *, int, rtx));
static void reloads_to_loads PARAMS ((struct rewrite_info *, struct ref **,
				      unsigned int, struct web **));
static void rewrite_program2 PARAMS ((bitmap));
static void mark_refs_for_checking PARAMS ((struct web *, bitmap));
void detect_web_parts_to_rebuild PARAMS ((void));
static void delete_useless_defs PARAMS ((void));
static void detect_non_changed_webs PARAMS ((void));
static void reset_changed_flag PARAMS ((void));
static void purge_reg_equiv_notes PARAMS ((void));
static void assign_stack_slots PARAMS ((void));
static void assign_stack_slots_1 PARAMS ((void));
static void mark_insn_refs_for_checking PARAMS ((struct ra_insn_info *,
						 sbitmap , bitmap));
static int coalesce_spill_slot PARAMS ((struct web *, struct ref*, rtx));

/* Bitmap used for tracking insns changed in spill pass.
   Very similar to ra_modified_insns.  */
bitmap last_changed_insns;

/* For tracking some statistics, we count the number (and cost)
   of deleted move insns.  */
static unsigned int deleted_move_insns;
static unsigned HOST_WIDE_INT deleted_move_cost;

/* This is the spill coalescing phase.  In SPILLED the IDs of all
   already spilled webs are noted.  In COALESCED the IDs of webs still
   to check for coalescing.  This tries to coalesce two webs, which were
   spilled, are connected by a move, and don't conflict.  Greatly
   reduces memory shuffling.  */

static void
spill_coalescing (coalesce, spilled)
     sbitmap coalesce, spilled;
{
  struct move_list *ml;
  struct move *m;
  for (ml = wl_moves; ml; ml = ml->next)
    if ((m = ml->move) != NULL)
      {
	struct web *s = alias (m->source_web);
	struct web *t = alias (m->target_web);
	if ((TEST_BIT (spilled, s->id) && TEST_BIT (coalesce, t->id))
	    || (TEST_BIT (spilled, t->id) && TEST_BIT (coalesce, s->id)))
	  {
	    struct conflict_link *wl;
	    if (s->pattern || t->pattern
		|| !non_conflicting_for_combine (s, t))
	      continue;

	    deleted_move_insns++;
	    deleted_move_cost += BLOCK_FOR_INSN (m->insn)->frequency + 1;
	    PUT_CODE (m->insn, NOTE);
	    NOTE_LINE_NUMBER (m->insn) = NOTE_INSN_DELETED;
	    df_insn_modify (df, BLOCK_FOR_INSN (m->insn), m->insn);
	    bitmap_set_bit (ra_modified_insns, INSN_UID (m->insn));

	    m->target_web->target_of_spilled_move = 1;
	    if (s == t)
	      /* May be, already coalesced due to a former move.  */
	      continue;
	    /* Merge the nodes S and T in the I-graph.  Beware: the merging
	       of conflicts relies on the fact, that in the conflict list
	       of T all of it's conflicts are noted.  This is currently not
	       the case if T would be the target of a coalesced web, because
	       then (in combine () above) only those conflicts were noted in
	       T from the web which was coalesced into T, which at the time
	       of combine() were not already on the SELECT stack or were
	       itself coalesced to something other.  */
	    if (t->type != SPILLED || s->type != SPILLED)
	      abort ();
	    remove_list (t->dlink, &WEBS(SPILLED));
	    put_web (t, COALESCED);
	    t->alias = s;
	    s->is_coalesced = 1;
	    t->is_coalesced = 1;
	    merge_moves (s, t);
	    
	    /* Now merge the usable_regs together.  */
	    s->use_my_regs = 1;
	    AND_HARD_REG_SET (s->usable_regs, t->usable_regs);
	    s->regclass = reg_class_subunion[s->regclass][t->regclass];
	    
	    for (wl = t->conflict_list; wl; wl = wl->next)
	      {
		struct web *pweb = wl->t;
		if (wl->sub == NULL)
		  record_conflict (s, pweb);
		else
		  {
		    struct sub_conflict *sl;
		    for (sl = wl->sub; sl; sl = sl->next)
		      {
			struct web *sweb = NULL;
			if (SUBWEB_P (sl->s))
			  sweb = find_subweb (s, sl->s->orig_x);
			if (!sweb)
			  sweb = s;
			record_conflict (sweb, sl->t);
		      }
		  }
		/* No decrement_degree here, because we already have colored
		   the graph, and don't want to insert pweb into any other
		   list.  */
		pweb->num_conflicts -= 1 + t->add_hardregs;
	      }
	  }
      }
}

/* Returns the probable saving of coalescing WEB with webs from
   SPILLED, in terms of removed move insn cost.  */

static unsigned HOST_WIDE_INT
spill_prop_savings (web, spilled)
     struct web *web;
     sbitmap spilled;
{
  unsigned HOST_WIDE_INT savings = 0;
  struct move_list *ml;
  struct move *m;
  unsigned int cost;
  if (web->pattern)
    return 0;
  cost = 1 + MEMORY_MOVE_COST (GET_MODE (web->orig_x), web->regclass, 1);
  cost += 1 + MEMORY_MOVE_COST (GET_MODE (web->orig_x), web->regclass, 0);
  for (ml = wl_moves; ml; ml = ml->next)
    if ((m = ml->move) != NULL)
      {
	struct web *s = alias (m->source_web);
	struct web *t = alias (m->target_web);
	if (s != web)
	  {
	    struct web *h = s;
	    s = t;
	    t = h;
	  }
	if (s != web || !TEST_BIT (spilled, t->id) || t->pattern
	    || !non_conflicting_for_combine (s, t))
	  continue;
	savings += BLOCK_FOR_INSN (m->insn)->frequency * cost;
      }
  return savings;
}

/* This add all IDs of colored webs, which are connected to WEB by a move
   to LIST and PROCESSED.  */

static void
spill_prop_insert (web, list, processed)
     struct web *web;
     sbitmap list, processed;
{
  struct move_list *ml;
  struct move *m;
  for (ml = wl_moves; ml; ml = ml->next)
    if ((m = ml->move) != NULL)
      {
	struct web *s = alias (m->source_web);
	struct web *t = alias (m->target_web);
	if (s != web)
	  {
	    struct web *h = s;
	    s = t;
	    t = h;
	  }
	if (s != web || t->type != COLORED || TEST_BIT (processed, t->id))
	  continue;
	SET_BIT (list, t->id);
	SET_BIT (processed, t->id);
      }
}

/* The spill propagation pass.  If we have to spilled webs, the first
   connected through a move to a colored one, and the second also connected
   to that colored one, and this colored web is only used to connect both
   spilled webs, it might be worthwhile to spill that colored one.
   This is the case, if the cost of the removed copy insns (all three webs
   could be placed into the same stack slot) is higher than the spill cost
   of the web.
   TO_PROP are the webs we try to propagate from (i.e. spilled ones),
   SPILLED the set of all spilled webs so far and PROCESSED the set
   of all webs processed so far, so we don't do work twice.  */

static int
spill_propagation (to_prop, spilled, processed)
     sbitmap to_prop, spilled, processed;
{
  int id;
  int again = 0;
  sbitmap list = sbitmap_alloc (num_webs);
  sbitmap_zero (list);

  /* First insert colored move neighbors into the candidate list.  */
  EXECUTE_IF_SET_IN_SBITMAP (to_prop, 0, id,
    {
      spill_prop_insert (ID2WEB (id), list, processed);
    });
  sbitmap_zero (to_prop);

  /* For all candidates, see, if the savings are higher than it's
     spill cost.  */
  while ((id = sbitmap_first_set_bit (list)) >= 0)
    {
      struct web *web = ID2WEB (id);
      RESET_BIT (list, id);
      if (spill_prop_savings (web, spilled) >= web->spill_cost)
	{
	  /* If so, we found a new spilled web.  Insert it's colored
	     move neighbors again, and mark, that we need to repeat the
	     whole mainloop of spillprog/coalescing again.  */
	  remove_web_from_list (web);
	  web->color = -1;
	  put_web (web, SPILLED);
	  SET_BIT (spilled, id);
	  SET_BIT (to_prop, id);
	  spill_prop_insert (web, list, processed);
	  again = 1;
	}
    }
  sbitmap_free (list);
  return again;
}

/* The main phase to improve spill costs.  This repeatedly runs
   spill coalescing and spill propagation, until nothing changes.  */

static void
spill_coalprop ()
{
  sbitmap spilled, processed, to_prop;
  struct dlist *d;
  int again;
  spilled = sbitmap_alloc (num_webs);
  processed = sbitmap_alloc (num_webs);
  to_prop = sbitmap_alloc (num_webs);
  sbitmap_zero (spilled);
  for (d = WEBS(SPILLED); d; d = d->next)
    SET_BIT (spilled, DLIST_WEB (d)->id);
  sbitmap_copy (to_prop, spilled);
  sbitmap_zero (processed);
  do
    {
      spill_coalescing (to_prop, spilled);
      /* XXX Currently (with optimistic coalescing) spill_propagation()
	 doesn't give better code, sometimes it gives worse (but not by much)
	 code.  I believe this is because of slightly wrong cost
	 measurements.  Anyway right now it isn't worth the time it takes,
	 so deactivate it for now.  */
      again = 0 && spill_propagation (to_prop, spilled, processed);
    }
  while (again);
  sbitmap_free (to_prop);
  sbitmap_free (processed);
  sbitmap_free (spilled);
}

/* Allocate a spill slot for a WEB.  Currently we spill to pseudo
   registers, to be able to track also webs for "stack slots", and also
   to possibly colorize them.  These pseudos are sometimes handled
   in a special way, where we know, that they also can represent
   MEM references.  */

static void
allocate_spill_web (web)
     struct web *web;
{
  int regno = web->regno;
  rtx slot;
  unsigned int inherent_size = PSEUDO_REGNO_BYTES (regno);
  /* XXX
     unsigned int total_size = MAX (inherent_size, reg_max_ref_width[i]); */
  unsigned int total_size = MAX (inherent_size, 0);
  if (web->stack_slot)
    return;

/*   GO_IF_HARD_REG_EQUAL (usable_regs[GENERAL_REGS], web->usable_regs, */
/* 			alloc_slot); */
  slot = gen_reg_rtx (PSEUDO_REGNO_MODE (regno));
  bitmap_set_bit (spill_slot_regs, REGNO (slot));
  web->stack_slot = slot;
  return;
  /* If we trying to spill a web which needed in GENERAL_REGS then we don't
     needed in remaining register. So, allocate the stack slot.  */
  /*
 alloc_slot:
  slot = assign_stack_local (PSEUDO_REGNO_MODE (regno), total_size,
			     inherent_size == total_size ? 0 : -1);
  RTX_UNCHANGING_P (slot) = RTX_UNCHANGING_P (regno_reg_rtx[regno]);
  set_mem_alias_set (slot, new_alias_set ());
  web->stack_slot = slot;
  */
}

/* This chooses a color for all SPILLED webs for interference region
   spilling.  The heuristic isn't good in any way.  */

static void
choose_spill_colors ()
{
  struct dlist *d;
  unsigned HOST_WIDE_INT *costs = (unsigned HOST_WIDE_INT *)
    xmalloc (FIRST_PSEUDO_REGISTER * sizeof (costs[0]));
  for (d = WEBS(SPILLED); d; d = d->next)
    {
      struct web *web = DLIST_WEB (d);
      struct conflict_link *wl;
      int bestc, c;
      HARD_REG_SET avail;
      memset (costs, 0, FIRST_PSEUDO_REGISTER * sizeof (costs[0]));
      for (wl = web->conflict_list; wl; wl = wl->next)
	{
	  struct web *pweb = wl->t;
	  if (pweb->type == COLORED || pweb->type == PRECOLORED)
	    costs[pweb->color] += pweb->spill_cost;
	}

      COPY_HARD_REG_SET (avail, web->usable_regs);
      if (web->crosses_call)
	{
	  /* Add an arbitrary constant cost to colors not usable by
	     call-crossing webs without saves/loads.  */
	  for (c = 0; c < FIRST_PSEUDO_REGISTER; c++)
	    if (TEST_HARD_REG_BIT (call_used_reg_set, c))
	      costs[c] += 1000;
	}
      bestc = -1;
      for (c = 0; c < FIRST_PSEUDO_REGISTER; c++)
	if ((bestc < 0 || costs[bestc] > costs[c])
            && TEST_HARD_REG_BIT (avail, c)
	    && HARD_REGNO_MODE_OK (c, PSEUDO_REGNO_MODE (web->regno)))
	  {
	    int i, size;
	    size = HARD_REGNO_NREGS (c, PSEUDO_REGNO_MODE (web->regno));
	    for (i = 1; i < size
		 && TEST_HARD_REG_BIT (avail, c + i); i++);
	    if (i == size)
	      bestc = c;
	  }
      web->color = bestc;
      ra_debug_msg (DUMP_PROCESS, "choosing color %d for spilled web %d\n",
		 bestc, web->id);
    }

  free (costs);
}

/* For statistics sake we count the number and cost of all new loads,
   stores and emitted rematerializations.  */
static unsigned int emitted_spill_loads;
static unsigned int emitted_spill_stores;
static unsigned int emitted_remat;
static unsigned HOST_WIDE_INT spill_load_cost;
static unsigned HOST_WIDE_INT spill_store_cost;
static unsigned HOST_WIDE_INT spill_remat_cost;

/* In rewrite_program2() we detect if some def us useless, in the sense,
   that the pseudo set is not live anymore at that point.  The REF_IDs
   of such defs are noted here.  */
static bitmap useless_defs;

/* This is the simple and fast version of rewriting the program to
   include spill code.  It spills at every insn containing spilled
   defs or uses.  Loads are added only if flag_ra_spill_every_use is
   nonzero, otherwise only stores will be added.  This doesn't
   support rematerialization. 
   NEW_DEATHS is filled with uids for insns, which probably contain
   deaths.  */

static void
rewrite_program (new_deaths)
     bitmap new_deaths;
{
  unsigned int i;
  struct dlist *d;
  bitmap b = BITMAP_XMALLOC ();

  /* We walk over all webs, over all uses/defs.  For all webs, we need
     to look at spilled webs, and webs coalesced to spilled ones, in case
     their alias isn't broken up, or they got spill coalesced.  */
  for (i = 0; i < 2; i++)
    for (d = (i == 0) ? WEBS(SPILLED) : WEBS(COALESCED); d; d = d->next)
      {
	struct web *web = DLIST_WEB (d);
	struct web *aweb = alias (web);
	unsigned int j;
	rtx slot;

	/* Is trivially true for spilled webs, but not for coalesced ones.  */
	if (aweb->type != SPILLED)
	  continue;

	/* First add loads before every use, if we have to.  */
	if (flag_ra_spill_every_use)
	  {
	    bitmap_clear (b);
	    allocate_spill_web (aweb);
	    slot = aweb->stack_slot;
	    for (j = 0; j < web->num_uses; j++)
	      {
		rtx insns, target, source;
		rtx insn = DF_REF_INSN (web->uses[j]);
		rtx prev = PREV_INSN (insn);
		basic_block bb = BLOCK_FOR_INSN (insn);
		/* Happens when spill_coalescing() deletes move insns.  */
		if (!INSN_P (insn))
		  continue;

		/* Check that we didn't already added a load for this web
		   and insn.  Happens, when the an insn uses the same web
		   multiple times.  */
	        if (bitmap_bit_p (b, INSN_UID (insn)))
		  continue;
	        bitmap_set_bit (b, INSN_UID (insn));
	        target = DF_REF_REG (web->uses[j]);
	        source = slot;
		start_sequence ();
	        if (GET_CODE (target) == SUBREG)
		  source = simplify_gen_subreg (GET_MODE (target), source,
						GET_MODE (source),
						SUBREG_BYTE (target));
		ra_emit_move_insn (target, source);
		insns = get_insns ();
		end_sequence ();
		emit_insn_before (insns, insn);

	        if (bb->head == insn)
		  bb->head = NEXT_INSN (prev);
		for (insn = PREV_INSN (insn); insn != prev;
		     insn = PREV_INSN (insn))
		  {
		    set_block_for_insn (insn, bb);
		    df_insn_modify (df, bb, insn);
		    bitmap_set_bit (ra_modified_insns, INSN_UID (insn));
		    bitmap_set_bit (emitted_by_spill, INSN_UID (insn));
		  }

		emitted_spill_loads++;
		spill_load_cost += bb->frequency + 1;
	      }
	  }

	/* Now emit the stores after each def.
	   If any uses were loaded from stackslots (compared to
	   rematerialized or not reloaded due to IR spilling),
	   aweb->stack_slot will be set.  If not, we don't need to emit
	   any stack stores.  */
	slot = aweb->stack_slot;
	bitmap_clear (b);
	if (slot)
	  for (j = 0; j < web->num_defs; j++)
	    {
	      rtx insns, source, dest;
	      rtx insn = DF_REF_INSN (web->defs[j]);
	      rtx following = NEXT_INSN (insn);
	      basic_block bb = BLOCK_FOR_INSN (insn);
	      /* Happens when spill_coalescing() deletes move insns.  */
	      if (!INSN_P (insn))
		continue;
	      if (bitmap_bit_p (b, INSN_UID (insn)))
		continue;
	      bitmap_set_bit (b, INSN_UID (insn));
	      start_sequence ();
	      source = DF_REF_REG (web->defs[j]);
	      dest = slot;
	      if (GET_CODE (source) == SUBREG)
		dest = simplify_gen_subreg (GET_MODE (source), dest,
					    GET_MODE (dest),
					    SUBREG_BYTE (source));
	      ra_emit_move_insn (dest, source);

	      insns = get_insns ();
	      end_sequence ();
	      if (insns)
		{
		  emit_insn_after (insns, insn);
		  if (bb->end == insn)
		    bb->end = PREV_INSN (following);
		  for (insn = insns; insn != following;
		       insn = NEXT_INSN (insn))
		    {
		      set_block_for_insn (insn, bb);
		      df_insn_modify (df, bb, insn);
		      bitmap_set_bit (ra_modified_insns, INSN_UID (insn));
		      bitmap_set_bit (emitted_by_spill, INSN_UID (insn));
		    }
		}
	      else
		{
		  df_insn_modify (df, bb, insn);
		  bitmap_set_bit (ra_modified_insns, INSN_UID (insn));
		}
	      emitted_spill_stores++;
	      spill_store_cost += bb->frequency + 1;
	      /* XXX we should set new_deaths for all inserted stores
		 whose pseudo dies here.
		 Note, that this isn't the case for _all_ stores.  */
	      /* I.e. the next is wrong, and might cause some spilltemps
		 to be categorized as spilltemp2's (i.e. live over a death),
		 although they aren't.  This might make them spill again,
		 which causes endlessness in the case, this insn is in fact
		 _no_ death.  */
	      bitmap_set_bit (new_deaths, INSN_UID (PREV_INSN (following)));
	    }
      }

  BITMAP_XFREE (b);
}

/* A simple list of rtx's.  */
struct rtx_list
{
  struct rtx_list *next;
  rtx x;
};

/* Adds X to *LIST.  */

static void
remember_slot (list, x)
     struct rtx_list **list;
     rtx x;
{
  struct rtx_list *l;
  /* PRE: X is not already in LIST.  */
  l = (struct rtx_list *) ra_alloc (sizeof (*l));
  l->next = *list;
  l->x = x;
  *list = l;
}

/* Given two rtx' S1 and S2, either being REGs or MEMs (or SUBREGs
   thereof), return nonzero, if they overlap.  REGs and MEMs don't
   overlap, and if they are MEMs they must have an easy address
   (plus (basereg) (const_inst x)), otherwise they overlap.  */

static int
slots_overlap_p (s1, s2)
     rtx s1, s2;
{
  rtx base1, base2;
  HOST_WIDE_INT ofs1 = 0, ofs2 = 0;
  int size1 = GET_MODE_SIZE (GET_MODE (s1));
  int size2 = GET_MODE_SIZE (GET_MODE (s2));
  if (GET_CODE (s1) == SUBREG)
    ofs1 = SUBREG_BYTE (s1), s1 = SUBREG_REG (s1);
  if (GET_CODE (s2) == SUBREG)
    ofs2 = SUBREG_BYTE (s2), s2 = SUBREG_REG (s2);

  if (s1 == s2)
    return 1;

  if (GET_CODE (s1) != GET_CODE (s2))
    return 0;

  if (GET_CODE (s1) == REG && GET_CODE (s2) == REG)
    {
      if (REGNO (s1) != REGNO (s2))
	return 0;
      if (ofs1 >= ofs2 + size2 || ofs2 >= ofs1 + size1)
	return 0;
      return 1;
    }
  if (GET_CODE (s1) != MEM || GET_CODE (s2) != MEM)
    abort ();
  s1 = XEXP (s1, 0);
  s2 = XEXP (s2, 0);
  if (GET_CODE (s1) != PLUS || GET_CODE (XEXP (s1, 0)) != REG
      || GET_CODE (XEXP (s1, 1)) != CONST_INT)
    return 1;
  if (GET_CODE (s2) != PLUS || GET_CODE (XEXP (s2, 0)) != REG
      || GET_CODE (XEXP (s2, 1)) != CONST_INT)
    return 1;
  base1 = XEXP (s1, 0);
  base2 = XEXP (s2, 0);
  if (!rtx_equal_p (base1, base2))
    return 1;
  ofs1 += INTVAL (XEXP (s1, 1));
  ofs2 += INTVAL (XEXP (s2, 1));
  if (ofs1 >= ofs2 + size2 || ofs2 >= ofs1 + size1)
    return 0;
  return 1;
}

/* This deletes from *LIST all rtx's which overlap with X in the sense
   of slots_overlap_p().  */

static void
delete_overlapping_slots (list, x)
     struct rtx_list **list;
     rtx x;
{
  while (*list)
    {
      if (slots_overlap_p ((*list)->x, x))
	*list = (*list)->next;
      else
	list = &((*list)->next);
    }
}

static void
delete_overlapping_uses (px, data)
     rtx *px;
     void *data;
{
  struct rtx_list **list = (struct rtx_list **)data;
  rtx x = *px;
  RTX_CODE code = GET_CODE (x);
  switch (code)
    {
      case REG: case SUBREG: case MEM:
	delete_overlapping_slots (list, x);
      default:
	break;
    }
  {
    const char *fmt = GET_RTX_FORMAT (code);
    int i, j;

    for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
      if (fmt[i] == 'e')
	delete_overlapping_uses (&XEXP (x, i), data);
      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  delete_overlapping_uses (&XVECEXP (x, i, j), data);
  }
}

/* Returns nonzero, of X is member of LIST.  */

static int
slot_member_p (list, x)
     struct rtx_list *list;
     rtx x;
{
  for (;list; list = list->next)
    if (list->x == x
	|| (REG_P (list->x) && GET_CODE (x) == SUBREG
	    && list->x == SUBREG_REG (x)
	    && GET_MODE_SIZE (GET_MODE (list->x)) 
	       >= GET_MODE_SIZE (GET_MODE (x)))
	|| rtx_equal_p (list->x, x))
      return 1;
  return 0;
}

/* A more sophisticated (and slower) method of adding the stores, than
   rewrite_program().  This goes backward the insn stream, adding
   stores as it goes, but only if it hasn't just added a store to the
   same location.  NEW_DEATHS is a bitmap filled with uids of insns
   containing deaths.  */

static void
insert_stores (new_deaths)
     bitmap new_deaths;
{
  rtx insn;
  rtx last_slot = NULL_RTX;
  struct rtx_list *slots = NULL;

  /* We go simply backwards over basic block borders.  */
  for (insn = get_last_insn (); insn; insn = PREV_INSN (insn))
    {
      int uid = INSN_UID (insn);

      /* If we reach a basic block border, which has more than one
	 outgoing edge, we simply forget all already emitted stores.  */
      if (GET_CODE (insn) == BARRIER
	  || JUMP_P (insn) || can_throw_internal (insn))
	{
	  last_slot = NULL_RTX;
	  slots = NULL;
	}
      if (!INSN_P (insn))
	continue;

      /* If this insn was not just added in this pass.  */
      if (uid < insn_df_max_uid)
	{
	  unsigned int n;
	  struct ra_insn_info info;
	  rtx following = NEXT_INSN (insn);
	  basic_block bb = BLOCK_FOR_INSN (insn);

	  info = insn_df[uid];
	  for (n = 0; n < info.num_defs; n++)
	    {
	      rtx slot, source;
	      struct web *web = def2web[DF_REF_ID (info.defs[n])];
	      struct web *aweb = alias (find_web_for_subweb (web));
	      
	      if (aweb->type != SPILLED || !aweb->stack_slot)
		continue;
	      if (web->pattern || aweb->pattern)
		if (web != aweb)
		  abort ();
	      slot = aweb->stack_slot;
	      source = DF_REF_REG (info.defs[n]);
	      /* adjust_address() might generate code.  */
	      start_sequence ();
	      if (GET_CODE (source) == SUBREG)
		{
		  slot = simplify_gen_subreg (GET_MODE (source), slot,
					      GET_MODE (slot),
					      SUBREG_BYTE (source));
		  source = copy_rtx (source);
		}
	      /* If we have no info about emitted stores, or it didn't
		 contain the location we intend to use soon, then
		 add the store.  */
	      if ((!last_slot || !rtx_equal_p (slot, last_slot))
		  && ! slot_member_p (slots, slot))
		{
		  rtx insns, ni;
		  rtx spill;
		  int has_use;
		  last_slot = slot;
		  remember_slot (&slots, slot);
		  if ((web->pattern || copy_insn_p (insn, NULL, NULL)) 
		      && ra_validate_change (insn, DF_REF_LOC (info.defs[n]),
					     slot, 0))
		    {
		      df_insn_modify (df, bb, insn);
		      bitmap_set_bit (ra_modified_insns, uid);
		      bitmap_set_bit (last_changed_insns, uid);
		      if (!bitmap_bit_p (useless_defs,
					 DF_REF_ID (info.defs[n])))
			ra_emit_move_insn (source, slot);
		    }
		  else
		    ra_emit_move_insn (slot, source);
		  insns = get_insns ();
		  end_sequence ();
		  if (insns)
		    {
		      emit_insn_after (insns, insn);
		      if (bb->end == insn)
			bb->end = PREV_INSN (following);
		      for (ni = insns; ni != following; ni = NEXT_INSN (ni))
			{
			  set_block_for_insn (ni, bb);
			  df_insn_modify (df, bb, ni);
			  bitmap_set_bit (ra_modified_insns, INSN_UID (ni));
			  bitmap_set_bit (emitted_by_spill, INSN_UID (ni));
			}
		    }
		  else
		    {
		      df_insn_modify (df, bb, insn);
		      bitmap_set_bit (ra_modified_insns, INSN_UID (insn));
		    }
		  emitted_spill_stores++;
		  spill_store_cost += bb->frequency + 1;
		  /* Check all uses in insns generated by spill phase in
		     previous passes.  */
		  spill = following;
		  has_use = 0;
		  for (spill = following;
		       (spill && !has_use
			&& bitmap_bit_p (emitted_by_spill,
					 INSN_UID (spill)));
		       spill = NEXT_INSN (spill))
		    {
		      unsigned int i;
		      for (i = 0; i < web->num_uses; ++i)
			{
			  if (DF_REF_INSN (web->uses[i]) == spill)
			    {
			      has_use = 1;
			      break;
			    }
			}
		    }
		  if (!has_use)
		    bitmap_set_bit (new_deaths,
				    INSN_UID (PREV_INSN (following)));
		}
	      else
		{
		  /* Otherwise ignore insns from adjust_address() above.  */
		  end_sequence ();
		}
	    }
	}
      /* If we look at a load generated by the allocator, forget
	 the last emitted slot, and additionally clear all slots
	 overlapping it's source (after all, we need it again).  */
      /* XXX If we emit the stack-ref directly into the using insn the
	 following needs a change, because that is no new insn.  Preferably
	 we would add some notes to the insn, what stackslots are needed
	 for it.  */
      if (uid >= last_max_uid
	  || bitmap_bit_p (ra_modified_insns, uid) 
	  || bitmap_bit_p (last_changed_insns, uid))
	{
	  rtx set = single_set (insn);
	  if (1 || uid >= last_max_uid)
	    last_slot = NULL_RTX;
	  /* If this was no simple set, give up, and forget everything.  */
	  if (!set)
	    slots = NULL;
	  else
	    {
	      /* rtx d = SET_DEST (set); */
	      note_uses_partial (&set, delete_overlapping_uses, (void *)&slots);
	      /*if (1 || GET_CODE (SET_SRC (set)) == MEM)
		delete_overlapping_slots (&slots, SET_SRC (set));*/
	      /*if (REG_P (d) || GET_CODE (d) == MEM
		|| (GET_CODE (d) == SUBREG && REG_P (SUBREG_REG (d))))
		remember_slot (&slots, d);*/
	    }
	}
    }
}

/* Returns 1 if both colored webs have some hardregs in common, even if
   they are not the same width.  */

static int
spill_same_color_p (web1, web2)
     struct web *web1, *web2;
{
  int c1, size1, c2, size2;
  if ((c1 = alias (web1)->color) < 0 || c1 == an_unusable_color)
    return 0;
  if ((c2 = alias (web2)->color) < 0 || c2 == an_unusable_color)
    return 0;

  size1 = web1->type == PRECOLORED
          ? 1 : HARD_REGNO_NREGS (c1, PSEUDO_REGNO_MODE (web1->regno));
  size2 = web2->type == PRECOLORED
          ? 1 : HARD_REGNO_NREGS (c2, PSEUDO_REGNO_MODE (web2->regno));
  if (c1 >= c2 + size2 || c2 >= c1 + size1)
    return 0;
  return 1;
}

/* Given the set of live web IDs LIVE, returns nonzero, if any of WEBs
   subwebs (or WEB itself) is live.  */

static unsigned int
is_partly_live_1 (live, web)
     sbitmap live;
     struct web *web;
{
  do
    if (TEST_BIT (live, web->id))
      return 1;
  while ((web = web->subreg_next));
  return 0;
}

/* Fast version in case WEB has no subwebs.  */
#define is_partly_live(live, web) ((!web->subreg_next || web->parent_web) \
				   ? TEST_BIT (live, web->id)		  \
				   : is_partly_live_1 (live, web))	  \

/* Change the set of currently IN_USE colors according to
   WEB's color.  Either add those colors to the hardreg set (if ADD
   is nonzero), or remove them.  */

static void
update_spill_colors (in_use, web, add)
     HARD_REG_SET *in_use;
     struct web *web;
     int add;
{
  int c, size;
  if ((c = alias (find_web_for_subweb (web))->color) < 0
      || c == an_unusable_color)
    return;
  size = HARD_REGNO_NREGS (c, GET_MODE (web->orig_x));
  if (SUBWEB_P (web))
    {
      c += subreg_regno_offset (c, GET_MODE (SUBREG_REG (web->orig_x)),
				SUBREG_BYTE (web->orig_x),
				GET_MODE (web->orig_x));
    }
  else if (web->type == PRECOLORED)
    size = 1;
  if (add)
    for (; size--;)
      SET_HARD_REG_BIT (*in_use, c + size);
  else
    for (; size--;)
      CLEAR_HARD_REG_BIT (*in_use, c + size);
}

/* Given a set of hardregs currently IN_USE and the color C of WEB,
   return -1 if WEB has no color, 1 of it has the unusable color,
   0 if one of it's used hardregs are in use, and 1 otherwise.
   Generally, if WEB can't be left colorized return 1.  */

static int
spill_is_free (in_use, web)
     HARD_REG_SET *in_use;
     struct web *web;
{
  int c, size;
  if ((c = alias (web)->color) < 0)
    return -1;
  if (c == an_unusable_color)
    return 1;
  size = web->type == PRECOLORED
         ? 1 : HARD_REGNO_NREGS (c, PSEUDO_REGNO_MODE (web->regno));
  for (; size--;)
    if (TEST_HARD_REG_BIT (*in_use, c + size))
      return 0;
  return 1;
}


/* Structure for passing between rewrite_program2() and emit_loads().  */
struct rewrite_info
{
  /* The web IDs which currently would need a reload.  These are
     currently live spilled webs, whose color was still free.  */
  bitmap need_reload;
  /* We need a scratch bitmap, but don't want to allocate one a zillion
     times.  */
  bitmap scratch;
  /* Web IDs of currently live webs.  This are the precise IDs,
     not just those of the superwebs.  If only on part is live, only
     that ID is placed here.  */
  sbitmap live;
  /* An array of webs, which currently need a load added.
     They will be emitted when seeing the first death.  */ 
  struct web **needed_loads;
  /* The current number of entries in needed_loads.  */
  int nl_size;
  /* The number of bits set in need_reload.  */
  int num_reloads;
  /* The current set of hardregs not available.  */
  HARD_REG_SET colors_in_use;
  /* Nonzero, if we just added some spill temps to need_reload or
     needed_loads.  In this case we don't wait for the next death
     to emit their loads.  */
  int any_spilltemps_spilled;
  /* Nonzero, if we currently need to emit the loads.  E.g. when we
     saw an insn containing deaths.  */
  int need_load;
};

/* The needed_loads list of RI contains some webs for which
   we add the actual load insns here.  They are added just before
   their use last seen.  NL_FIRST_RELOAD is the index of the first
   load which is a converted reload, all other entries are normal
   loads.  LAST_BLOCK_INSN is the last insn of the current basic block.  */

static void
emit_loads (ri, nl_first_reload, last_block_insn)
     struct rewrite_info *ri;
     int nl_first_reload;
     rtx last_block_insn;
{
  int j;
  ri->any_spilltemps_spilled = 0;
  for (j = ri->nl_size; j;)
    {
      struct web *web = ri->needed_loads[--j];
      struct web *supweb;
      struct web *aweb;
      rtx ni, slot, reg;
      enum machine_mode innermode;
      rtx before = NULL_RTX, after = NULL_RTX;
      basic_block bb;
      /* When spilltemps were spilled for the last insns, their
	 loads already are emitted, which is noted by setting
	 needed_loads[] for it to 0.  */
      if (!web)
	continue;
      supweb = find_web_for_subweb (web);
      if (SPILL_SLOT_P (supweb->regno))
	abort ();
      /* Check for web being a spilltemp, if we only want to
	 load spilltemps.  Also remember, that we emitted that
	 load, which we don't need to do when we have a death,
	 because then all of needed_loads[] is emptied.  */
      if (!ri->need_load)
	{
	  if (!supweb->spill_temp)
	    continue;
	  else
	    ri->needed_loads[j] = 0;
	}
      web->in_load = 0;
      /* The adding of reloads doesn't depend on liveness.  */
      if (j < nl_first_reload && !is_partly_live (ri->live, web))
	continue;
      aweb = alias (supweb);
      aweb->changed = 1;
      start_sequence ();
      allocate_spill_web (aweb);
      slot = aweb->stack_slot;
      innermode = GET_MODE (slot);
      /* If we don't copy the RTL there might be some SUBREG
	 rtx shared in the next iteration although being in
	 different webs, which leads to wrong code.  */
      reg = copy_rtx (web->orig_x);
      if (GET_CODE (reg) == SUBREG)
	slot = simplify_gen_subreg (GET_MODE (reg), slot, innermode,
				    SUBREG_BYTE (reg));
      if (web->one_load && web->last_use_insn
 	  && copy_insn_p (web->last_use_insn, NULL,NULL)
	  && ra_validate_change (web->last_use_insn,
				 DF_REF_LOC (web->last_use), slot, 0))
	{
	  bb = BLOCK_FOR_INSN (web->last_use_insn);
	  df_insn_modify (df, bb, web->last_use_insn);
	  bitmap_set_bit (ra_modified_insns, INSN_UID (web->last_use_insn));
	  bitmap_set_bit (last_changed_insns, INSN_UID (web->last_use_insn));
	}
      else
	ra_emit_move_insn (reg, slot);
      ni = get_insns ();
      end_sequence ();
      if (ni)
	{
	  before = web->last_use_insn;
	  web->last_use_insn = NULL_RTX;
	  if (!before)
	    {
	      if (JUMP_P (last_block_insn))
		before = last_block_insn;
	      else
		after = last_block_insn;
	    }
	  if (after)
	    {
	      rtx foll = NEXT_INSN (after);
	      bb = BLOCK_FOR_INSN (after);
	      emit_insn_after (ni, after);
	      if (bb->end == after)
		bb->end = PREV_INSN (foll);
	      for (ni = NEXT_INSN (after); ni != foll; ni = NEXT_INSN (ni))
		{
		  set_block_for_insn (ni, bb);
		  df_insn_modify (df, bb, ni);
		  bitmap_set_bit (ra_modified_insns, INSN_UID (ni));
		  bitmap_set_bit (emitted_by_spill, INSN_UID (ni));
		}
	    }
	  else
	    {
	      rtx prev = PREV_INSN (before);
	      bb = BLOCK_FOR_INSN (before);
	      emit_insn_before (ni, before);
	      if (bb->head == before)
		bb->head = NEXT_INSN (prev);
	      for (; ni != before; ni = NEXT_INSN (ni))
		{
		  set_block_for_insn (ni, bb);
		  df_insn_modify (df, bb, ni);
		  bitmap_set_bit (ra_modified_insns, INSN_UID (ni));
		  bitmap_set_bit (emitted_by_spill, INSN_UID (ni));
		}
	    }
	}
      emitted_spill_loads++;
      spill_load_cost += bb->frequency + 1;
      reset_web_live (ri->live, web);
      /* In the special case documented above only emit the reloads and
	 one load.  */
      if (ri->need_load == 2 && j < nl_first_reload)
	break;
    }
  if (ri->need_load)
    ri->nl_size = j;
}

/* Test LIVE for partial WEB live.  */
int
is_partly_dead (live, web)
     sbitmap live;
     struct web *web;
{
  struct web *sweb;
  
  if (web->subreg_next && !web->parent_web)
    {
      for (sweb = web->subreg_next; sweb; sweb = sweb->subreg_next)
	if (!TEST_BIT (live, sweb->id))
	  return 1;
      return 0;
    }
  return !TEST_BIT (live, web->id);
}

/* Set live bit in LIVE for WEB or all his subwebs.  */
void
set_web_live (live, web)
     sbitmap live;
     struct web *web;
{
  struct web *sweb;

  if (web->subreg_next && !web->parent_web)
    for (sweb = web->subreg_next; sweb; sweb = sweb->subreg_next)
      SET_BIT (live, sweb->id);
  else
    SET_BIT (live, web->id);
}

/* Reset live bit in LIVE for WEB or all his subwebs.  */
void
reset_web_live (live, web)
     sbitmap live;
     struct web *web;
{
  struct web *sweb;

  if (web->subreg_next && !web->parent_web)
    for (sweb = web->subreg_next; sweb; sweb = sweb->subreg_next)
      RESET_BIT (live, sweb->id);
  else
    RESET_BIT (live, web->id);
}

/* Given a set of reloads in RI, an array of NUM_REFS references (either
   uses or defs) in REFS, and REF2WEB to translate ref IDs to webs
   (either use2web or def2web) convert some reloads to loads.
   This looks at the webs referenced, and how they change the set of
   available colors.  Now put all still live webs, which needed reloads,
   and whose colors isn't free anymore, on the needed_loads list.  */

static void
reloads_to_loads (ri, refs, num_refs, ref2web)
     struct rewrite_info *ri;
     struct ref **refs;
     unsigned int num_refs;
     struct web **ref2web;
{
  unsigned int n;
  int num_reloads = ri->num_reloads;
  for (n = 0; n < num_refs && num_reloads; n++)
    {
      struct web *web = ref2web[DF_REF_ID (refs[n])];
      struct web *supweb = find_web_for_subweb (web);
      int j;
      /* Only emit reloads when entering their interference
	 region.  A use of a spilled web never opens an
	 interference region, independent of it's color.  */
      if (alias (supweb)->type == SPILLED)
	continue;
      if (supweb->type == PRECOLORED
	  && TEST_HARD_REG_BIT (never_use_colors, supweb->color))
	continue;
      /* Note, that if web (and supweb) are DEFs, we already cleared
	 the corresponding bits in live.  I.e. is_partly_dead becomes true,
	 which is what we want.  */
      if (is_partly_dead (ri->live, web))
	{
	  int old_num_r = num_reloads;
	  bitmap_clear (ri->scratch);
	  EXECUTE_IF_SET_IN_BITMAP (ri->need_reload, 0, j,
	    {
	      struct web *web2 = ID2WEB (j);
	      struct web *aweb2 = alias (find_web_for_subweb (web2));
	      if (spill_is_free (&(ri->colors_in_use), aweb2) == 0)
		abort ();
	      if (spill_same_color_p (supweb, aweb2)
		  /* && interfere (web, web2) */)
		{
		  if (!web2->in_load)
		    {
		      ri->needed_loads[ri->nl_size++] = web2;
		      web2->in_load = 1;
		    }
		  bitmap_set_bit (ri->scratch, j);
		  num_reloads--;
		}
	    });
	  if (num_reloads != old_num_r)
	    bitmap_operation (ri->need_reload, ri->need_reload, ri->scratch,
			      BITMAP_AND_COMPL);
	}
    }
  ri->num_reloads = num_reloads;
}

/* This adds loads for spilled webs to the program.  It uses a kind of
   interference region spilling.  If flag_ra_ir_spilling is zero it
   only uses improved chaitin spilling (adding loads only at insns
   containing deaths).  */

static void
rewrite_program2 (new_deaths)
     bitmap new_deaths;
{
  basic_block bb;
  int nl_first_reload;
  struct rewrite_info ri;
  rtx insn;
  ri.needed_loads = (struct web **) xmalloc (num_webs * sizeof (struct web *));
  ri.need_reload = BITMAP_XMALLOC ();
  ri.scratch = BITMAP_XMALLOC ();
  ri.live = sbitmap_alloc (num_webs);
  ri.nl_size = 0;
  ri.num_reloads = 0;
  for (insn = get_last_insn (); insn; insn = PREV_INSN (insn))
    {
      basic_block last_bb = NULL;
      rtx last_block_insn;
      int i, j;
      if (!INSN_P (insn))
	insn = prev_real_insn (insn);
      while (insn && !(bb = BLOCK_FOR_INSN (insn)))
	insn = prev_real_insn (insn);
      if (!insn)
	break;
      i = bb->index + 2;
      last_block_insn = insn;

      sbitmap_zero (ri.live);
      CLEAR_HARD_REG_SET (ri.colors_in_use);
      EXECUTE_IF_SET_IN_BITMAP (live_at_end[i - 2], 0, j,
	{
	  struct web *web = use2web[j];
	  struct web *aweb = alias (find_web_for_subweb (web));
	  /* A web is only live at end, if it isn't spilled.  If we wouldn't
	     check this, the last uses of spilled web per basic block
	     wouldn't be detected as deaths, although they are in the final
	     code.  This would lead to cumulating many loads without need,
	     only increasing register pressure.  */
	  /* XXX do add also spilled webs which got a color for IR spilling.
	     Remember to not add to colors_in_use in that case.  */
	  if (aweb->type != SPILLED /*|| aweb->color >= 0*/)
	    {
	      set_web_live (ri.live, web);
	      if (aweb->type != SPILLED)
	        update_spill_colors (&(ri.colors_in_use), web, 1);
	    }
	});

      bitmap_clear (ri.need_reload);
      ri.num_reloads = 0;
      ri.any_spilltemps_spilled = 0;
      if (flag_ra_ir_spilling)
	{
	  struct dlist *d;
	  int pass;
	  /* XXX If we don't add spilled nodes into live above, the following
	     becomes an empty loop.  */
	  for (pass = 0; pass < 2; pass++)
	    for (d = (pass) ? WEBS(SPILLED) : WEBS(COALESCED); d; d = d->next)
	      {
	        struct web *web = DLIST_WEB (d);
		struct web *aweb = alias (web);
		if (aweb->type != SPILLED)
		  continue;
	        if (is_partly_live (ri.live, web)
		    && spill_is_free (&(ri.colors_in_use), web) > 0)
		  {
		    ri.num_reloads++;
	            bitmap_set_bit (ri.need_reload, web->id);
		    /* Last using insn is somewhere in another block.  */
		    web->last_use_insn = NULL_RTX;
		  }
	      }
	}

      last_bb = bb;
      for (; insn; insn = PREV_INSN (insn))
	{
	  struct ra_insn_info info;
	  unsigned int n;
	  HARD_REG_SET earlyclobber_colors;

	  if (INSN_P (insn) && BLOCK_FOR_INSN (insn) != last_bb)
	    {
	      int index = BLOCK_FOR_INSN (insn)->index + 2;
	      EXECUTE_IF_SET_IN_BITMAP (live_at_end[index - 2], 0, j,
		{
		  struct web *web = use2web[j];
		  struct web *aweb = alias (find_web_for_subweb (web));
		  if (aweb->type != SPILLED)
		    {
		      set_web_live (ri.live, web);
		      update_spill_colors (&(ri.colors_in_use), web, 1);
		    }
		});
	      bitmap_clear (ri.scratch);
	      EXECUTE_IF_SET_IN_BITMAP (ri.need_reload, 0, j,
		{
		  struct web *web2 = ID2WEB (j);
		  struct web *supweb2 = find_web_for_subweb (web2);
		  struct web *aweb2 = alias (supweb2);
		  if (spill_is_free (&(ri.colors_in_use), aweb2) <= 0)
		    {
		      if (!web2->in_load)
			{
			  ri.needed_loads[ri.nl_size++] = web2;
			  web2->in_load = 1;
			}
		      bitmap_set_bit (ri.scratch, j);
		      ri.num_reloads--;
		    }
		});
	      bitmap_operation (ri.need_reload, ri.need_reload, ri.scratch,
				BITMAP_AND_COMPL);
	      last_bb = BLOCK_FOR_INSN (insn);
	      last_block_insn = insn;
	      if (!INSN_P (last_block_insn))
	        last_block_insn = prev_real_insn (last_block_insn);
	    }

	  CLEAR_HARD_REG_SET (earlyclobber_colors);
	  ri.need_load = 0;
	  if (INSN_P (insn))
	    info = insn_df[INSN_UID (insn)];

	  if (INSN_P (insn))
	    for (n = 0; n < info.num_defs; n++)
	      {
		struct ref *ref = info.defs[n];
		struct web *web = def2web[DF_REF_ID (ref)];
		struct web *supweb = find_web_for_subweb (web);
		int is_non_def = 0;
		unsigned int n2;

		supweb = find_web_for_subweb (web);
		/* Webs which are defined here, but also used in the same insn
		   are rmw webs, or this use isn't a death because of looping
		   constructs.  In neither case makes this def available it's
		   resources.  Reloads for it are still needed, it's still
		   live and it's colors don't become free.  */
		for (n2 = 0; n2 < info.num_uses; n2++)
		  {
		    struct web *web2 = use2web[DF_REF_ID (info.uses[n2])];
		    if (supweb == find_web_for_subweb (web2))
		      {
			is_non_def = 1;
			break;
		      }
		  }
		if (is_non_def)
		  continue;

		if (!is_partly_live (ri.live, supweb))
		  bitmap_set_bit (useless_defs, DF_REF_ID (ref));

		reset_web_live (ri.live, web);
		if (bitmap_bit_p (ri.need_reload, web->id))
		  {
		    ri.num_reloads--;
		    bitmap_clear_bit (ri.need_reload, web->id);
		  }
		if (web != supweb)
		  {
		    /* XXX subwebs aren't precisely tracked here.  We have
		       everything we need (inverse webs), but the code isn't
		       yet written.  We need to make all completely
		       overlapping web parts non-live here.  */
		    /* If by luck now the whole web isn't live anymore, no
		       reloads for it are needed.  */
		    if (!is_partly_live (ri.live, supweb)
			&& bitmap_bit_p (ri.need_reload, supweb->id))
		      {
			ri.num_reloads--;
			bitmap_clear_bit (ri.need_reload, supweb->id);
		      }
		  }
		else
		  {
		    struct web *sweb;
		    /* The whole web is defined here, so no parts of it are
		       live anymore and no reloads are needed for them.  */
		    for (sweb = supweb->subreg_next; sweb;
			 sweb = sweb->subreg_next)
		      {
			if (bitmap_bit_p (ri.need_reload, sweb->id))
			  {
		            ri.num_reloads--;
		            bitmap_clear_bit (ri.need_reload, sweb->id);
			  }
		      }
		  }
		if (alias (supweb)->type != SPILLED)
		  {
		    /* Colors of early clobber operands don't become
		       free yet.  */
		    if (DF_REF_FLAGS (ref) & DF_REF_EARLYCLOBBER)
		      update_spill_colors (&earlyclobber_colors, web, 1);
		    else
		      update_spill_colors (&(ri.colors_in_use), web, 0);
		  }
	      }

	  nl_first_reload = ri.nl_size;

	  /* CALL_INSNs are not really deaths, but still more registers
	     are free after a call, than before.
	     XXX Note, that sometimes reload barfs when we emit insns between
	     a call and the insn which copies the return register into a
	     pseudo.  */
	  if (GET_CODE (insn) == CALL_INSN)
	    ri.need_load = 1;
	  else if (INSN_P (insn))
	    for (n = 0; n < info.num_uses; n++)
	      {
		struct web *web = use2web[DF_REF_ID (info.uses[n])];
		struct web *supweb = find_web_for_subweb (web);
		if (supweb->type == PRECOLORED
		    && TEST_HARD_REG_BIT (never_use_colors, supweb->color))
		  continue;
		if (is_partly_dead (ri.live, web))
		  {
		    ri.need_load = 1;
		    bitmap_set_bit (new_deaths, INSN_UID (insn));
		    break;
		  }
	      }

	  if (INSN_P (insn) && ri.num_reloads)
	    {
              int old_num_reloads = ri.num_reloads;
	      reloads_to_loads (&ri, info.uses, info.num_uses, use2web);

	      /* If this insn sets a pseudo, which isn't used later
		 (i.e. wasn't live before) it is a dead store.  We need
		 to emit all reloads which have the same color as this def.
		 We don't need to check for non-liveness here to detect
		 the deadness (it anyway is too late, as we already cleared
		 the liveness in the first loop over the defs), because if it
		 _would_ be live here, no reload could have that color, as
		 they would already have been converted to a load.  */
	      if (ri.num_reloads)
		reloads_to_loads (&ri, info.defs, info.num_defs, def2web);
	      if (ri.num_reloads != old_num_reloads && !ri.need_load)
		ri.need_load = 1;
	    }

	  if (ri.nl_size && (ri.need_load || ri.any_spilltemps_spilled))
	    emit_loads (&ri, nl_first_reload, last_block_insn);

	  if (INSN_P (insn) && flag_ra_ir_spilling)
	    for (n = 0; n < info.num_uses; n++)
	      {
		struct web *web = use2web[DF_REF_ID (info.uses[n])];
		struct web *aweb = alias (find_web_for_subweb (web));
		if (aweb->type != SPILLED)
		  {
		    update_spill_colors (&(ri.colors_in_use), web, 1);
		    /* Make sure we don't accidentially remove this color
		       from the in_use set again.  */
		    update_spill_colors (&earlyclobber_colors, web, 0);
		  }
	      }

	  /* Temporarily mark the early clobber hard regs as in use.  */
	  IOR_HARD_REG_SET (ri.colors_in_use, earlyclobber_colors);
			    
	  if (INSN_P (insn))
	    for (n = 0; n < info.num_uses; n++)
	      {
		struct web *web = use2web[DF_REF_ID (info.uses[n])];
		struct web *supweb = find_web_for_subweb (web);
		struct web *aweb = alias (supweb);
		set_web_live (ri.live, web);
		if (aweb->type != SPILLED)
		  continue;
		if (supweb->spill_temp)
		  ri.any_spilltemps_spilled = 1;
		web->last_use_insn = insn;
		web->last_use = info.uses[n];
		if (!web->in_load)
		  {
		    if (spill_is_free (&(ri.colors_in_use), aweb) <= 0
			|| !flag_ra_ir_spilling)
		      {
			ri.needed_loads[ri.nl_size++] = web;
			web->in_load = 1;
			web->one_load = 1;
		      }
		    else if (!bitmap_bit_p (ri.need_reload, web->id))
		      {
		        bitmap_set_bit (ri.need_reload, web->id);
			ri.num_reloads++;
			web->one_load = 1;
		      }
		    else
		      web->one_load = 0;
		  }
		else
		  web->one_load = 0;
	      }

	  /* Now that the effect of this insn are all handled the colors
	     of early clobber operand are free.  */
	  AND_COMPL_HARD_REG_SET (ri.colors_in_use, earlyclobber_colors);
	  if (GET_CODE (insn) == CODE_LABEL)
	    break;
	}

      nl_first_reload = ri.nl_size;
      if (ri.num_reloads)
	{
	  int in_ir = 0;
	  edge e;
	  int num = 0;
	  HARD_REG_SET cum_colors, colors;
	  CLEAR_HARD_REG_SET (cum_colors);
	  for (e = bb->pred; e && num < 5; e = e->pred_next, num++)
	    {
	      int j;
	      CLEAR_HARD_REG_SET (colors);
	      EXECUTE_IF_SET_IN_BITMAP (live_at_end[e->src->index], 0, j,
		{
		  struct web *web = use2web[j];
		  struct web *aweb = alias (find_web_for_subweb (web));
		  if (aweb->type != SPILLED)
		    update_spill_colors (&colors, web, 1);
		});
	      IOR_HARD_REG_SET (cum_colors, colors);
	    }
	  if (num == 5)
	    in_ir = 1;

	  bitmap_clear (ri.scratch);
	  EXECUTE_IF_SET_IN_BITMAP (ri.need_reload, 0, j,
	    {
	      struct web *web2 = ID2WEB (j);
	      struct web *supweb2 = find_web_for_subweb (web2);
	      struct web *aweb2 = alias (supweb2);
	      /* block entry is IR boundary for aweb2?
		 Currently more some tries for good conditions.  */
	      if (((ra_pass > 0 || supweb2->target_of_spilled_move)
		  && (1 || in_ir || spill_is_free (&cum_colors, aweb2) <= 0))
		  || (ra_pass == 1
		      && (in_ir
			  || spill_is_free (&cum_colors, aweb2) <= 0)))
		{
		  if (!web2->in_load)
		    {
		      ri.needed_loads[ri.nl_size++] = web2;
		      web2->in_load = 1;
		    }
		  bitmap_set_bit (ri.scratch, j);
		  ri.num_reloads--;
		}
	    });
	  bitmap_operation (ri.need_reload, ri.need_reload, ri.scratch,
			    BITMAP_AND_COMPL);
	}

      ri.need_load = 1;
      emit_loads (&ri, nl_first_reload, last_block_insn);
      if (ri.nl_size != 0 /*|| ri.num_reloads != 0*/)
	abort ();
      if (!insn)
	break;
    }
  free (ri.needed_loads);
  sbitmap_free (ri.live);
  BITMAP_XFREE (ri.scratch);
  BITMAP_XFREE (ri.need_reload);
}

/* Mark all webs mentioned in insn described by INFO for checking in
   the next allocator pass.
   Layout of webs isn't changed they are only mentioned in changed
   insns. */
static void
mark_insn_refs_for_checking (info, already_webs, uses_as_bitmap)
     struct ra_insn_info *info;
     sbitmap already_webs;
     bitmap uses_as_bitmap;
{
  int i, n;
  int num_refs;
  struct ref **refs;

  for (i = 0, refs = info->uses, num_refs = info->num_uses;
       i < 2;
       i++, refs = info->defs, num_refs = info->num_defs)
    for (n = 0; n < num_refs; n++)
      {
	struct web *web;
	int id = DF_REF_ID (refs[n]);

	/* Insn may be deleted by coalesce_spill_slot.  */
	if (!INSN_P (DF_REF_INSN (refs[n])))
	  return;

	web = DF_REF_REG_USE_P (refs[n]) ? use2web[id]: def2web[id];
	if (TEST_BIT (already_webs, web->id) || web->type == SPILLED)
	  continue;
	SET_BIT (already_webs, web->id);
	mark_refs_for_checking (web, uses_as_bitmap);
      }
}


/* WEBS is a web conflicting with a spilled one.  Prepare it
   to be able to rescan it in the next pass.  Mark all it's uses
   for checking, and clear the some members of their web parts
   (of defs and uses).  Notably don't clear the uplink.  We don't
   change the layout of this web, just it's conflicts.
   Also remember all IDs of its uses in USES_AS_BITMAP.  */

static void
mark_refs_for_checking (web, uses_as_bitmap)
     struct web *web;
     bitmap uses_as_bitmap;
{
  unsigned int i;
  for (i = 0; i < web->num_uses; i++)
    {
      unsigned int id = DF_REF_ID (web->uses[i]);
      SET_BIT (last_check_uses, id);
      if (uses_as_bitmap)
	bitmap_set_bit (uses_as_bitmap, id);
      web_parts[df->def_id + id].spanned_deaths = 0;
      web_parts[df->def_id + id].num_calls = 0;
      web_parts[df->def_id + id].crosses_call = 0;
      web_parts[df->def_id + id].crosses_bb = 0;
      web_parts[df->def_id + id].crosses_memset = 0;
    }
  for (i = 0; i < web->num_defs; i++)
    {
      unsigned int id = DF_REF_ID (web->defs[i]);
      web_parts[id].spanned_deaths = 0;
      web_parts[id].num_calls = 0;
      web_parts[id].crosses_call = 0;
      web_parts[id].crosses_bb = 0;
      web_parts[id].crosses_memset = 0;
    }
}

static bitmap webs_changed_layout;

/* The last step of the spill phase is to set up the structures for
   incrementally rebuilding the interference graph.  We break up
   the web part structure of all spilled webs, mark their uses for
   rechecking, look at their neighbors, and clean up some global
   information, we will rebuild.  */

void
detect_web_parts_to_rebuild ()
{
  bitmap uses_as_bitmap;
  unsigned int i, pass;
  int uid;
  struct dlist *d;
  sbitmap already_webs = sbitmap_alloc (num_webs);

  uses_as_bitmap = BITMAP_XMALLOC ();
  if (last_check_uses)
    sbitmap_free (last_check_uses);
  last_check_uses = sbitmap_alloc (df->use_id);
  sbitmap_zero (last_check_uses);
  sbitmap_zero (already_webs);

  if (webs_changed_layout)
    EXECUTE_IF_SET_IN_BITMAP (webs_changed_layout, 0, i,
      {
        struct web *web = alias (ID2WEB (i));
        if (web->type != PRECOLORED && web->type != SPILLED)
          {
            remove_web_from_list (web);
            put_web (web, SPILLED);
	  }
      });

  /* We need to recheck all uses of all webs involved in spilling (and the
     uses added by spill insns, but those are not analyzed yet).
     Those are the spilled webs themself, webs coalesced to spilled ones,
     and webs conflicting with any of them.  */
#if 0
  for (pass = 0; pass < 3; pass++)
#else
  for (pass = 0; pass < 2; pass++)
#endif
    for (d = (pass == 0) ? WEBS(SPILLED) 
	   : (pass == 1) ? WEBS(COALESCED)
	   : WEBS(COLORED); d; d = d->next)
      {
        struct web *web = DLIST_WEB (d);
	struct conflict_link *wl;
	unsigned int j;
	/* This check is only needed for coalesced nodes, but hey.  */
#if 1
	if (alias (web)->type != SPILLED)
	  continue;
#endif

	/* For the spilled web itself we also need to clear it's
	   uplink, to be able to rebuild smaller webs.  After all
	   spilling has split the web.  */
        for (i = 0; i < web->num_uses; i++)
	  {
	    unsigned int id = DF_REF_ID (web->uses[i]);
	    SET_BIT (last_check_uses, id);
	    bitmap_set_bit (uses_as_bitmap, id);
	    web_parts[df->def_id + id].uplink = NULL;
	    web_parts[df->def_id + id].spanned_deaths = 0;
	    web_parts[df->def_id + id].num_calls = 0;
	    web_parts[df->def_id + id].crosses_call = 0;
	    web_parts[df->def_id + id].crosses_bb = 0;
	    web_parts[df->def_id + id].crosses_memset = 0;
	  }
	for (i = 0; i < web->num_defs; i++)
	  {
	    unsigned int id = DF_REF_ID (web->defs[i]);
	    web_parts[id].uplink = NULL;
	    web_parts[id].spanned_deaths = 0;
	    web_parts[id].num_calls = 0;
	    web_parts[id].crosses_call = 0;
	    web_parts[id].crosses_bb = 0;
	    web_parts[id].crosses_memset = 0;
	  }

	/* Now look at all neighbors of this spilled web.  */
	if (web->have_orig_conflicts)
	  wl = web->orig_conflict_list;
	else
	  wl = web->conflict_list;
	for (; wl; wl = wl->next)
	  {
	    if (TEST_BIT (already_webs, wl->t->id))
	      continue;
	    SET_BIT (already_webs, wl->t->id);
	    mark_refs_for_checking (wl->t, uses_as_bitmap);
	    /* If this web was actually changed, delete the flag which would
	       prevent the conflicting web from becoming spilled.  We now
	       have another chance to actually emit some insns.  */
	    if (0 && !web->changed && alias (wl->t)->type != SPILLED)
	      wl->t->changed = 0;
	  }
	EXECUTE_IF_SET_IN_BITMAP (web->useless_conflicts, 0, j,
	  {
	    struct web *web2 = ID2WEB (j);
	    if (TEST_BIT (already_webs, web2->id))
	      continue;
	    SET_BIT (already_webs, web2->id);
	    mark_refs_for_checking (web2, uses_as_bitmap);
	    if (0 && !web->changed && alias (web2)->type != SPILLED)
	      web2->changed = 0;
	  });
	
      }

  EXECUTE_IF_SET_IN_BITMAP (last_changed_insns, 0, uid,
  {
    if (uid < insn_df_max_uid)
      mark_insn_refs_for_checking (&insn_df[uid], already_webs,
				   uses_as_bitmap);
  });
    
  /* We also recheck unconditionally all uses of any hardregs.  This means
     we _can_ delete all these uses from the live_at_end[] bitmaps.
     And because we sometimes delete insn refering to hardregs (when
     they became useless because they setup a rematerializable pseudo, which
     then was rematerialized), some of those uses will go away with the next
     df_analyse().  This means we even _must_ delete those uses from
     the live_at_end[] bitmaps.  For simplicity we simply delete
     all of them.  */
  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    if (!fixed_regs[i])
      {
	struct df_link *link;
	for (link = df->regs[i].uses; link; link = link->next)
	  if (link->ref)
	    bitmap_set_bit (uses_as_bitmap, DF_REF_ID (link->ref));
      }

  /* The information in live_at_end[] will be rebuilt for all uses
     we recheck, so clear it here (the uses of spilled webs might
     indeed not become member of it again).  */
  live_at_end -= 2;
  for (i = 0; i < (unsigned int) last_basic_block + 2; i++)
    bitmap_operation (live_at_end[i], live_at_end[i], uses_as_bitmap,
		      BITMAP_AND_COMPL);
  live_at_end += 2;

  if (rtl_dump_file && (debug_new_regalloc & DUMP_REBUILD) != 0)
    {
      ra_debug_msg (DUMP_REBUILD, "need to check these uses:\n");
      dump_sbitmap_file (rtl_dump_file, last_check_uses);
    }
  sbitmap_free (already_webs);
  BITMAP_XFREE (uses_as_bitmap);
}

/* Statistics about deleted insns, which are useless now.  */
static unsigned int deleted_def_insns;
static unsigned HOST_WIDE_INT deleted_def_cost;

extern int flag_non_call_exceptions;

static void try_delete_useless_def PARAMS ((rtx, rtx));

static void
try_delete_useless_def (insn, set)
     rtx insn, set;
{
  unsigned int n;
  rtx dest = SET_DEST (set);
  struct ra_insn_info info = insn_df[INSN_UID (insn)];
  while (GET_CODE (dest) == SUBREG
	 || GET_CODE (dest) == STRICT_LOW_PART
	 || GET_CODE (dest) == ZERO_EXTRACT)
    dest = XEXP (dest, 0);
  /* We don't want to delete sets of hardregs.  But clobbers, which are not
     marked specially to normal DEFs, therefore we check if all non-hardreg
     defs are marked useless, and additionally if we don't set a hardreg.  */
  if (!REG_P (dest)
      || REGNO (dest) < FIRST_PSEUDO_REGISTER
      || volatile_refs_p (SET_SRC (set))
      || (flag_non_call_exceptions && may_trap_p (insn)))
    return;
  /* Is every pseudo set in this insn useless?  Note that the useless_defs
     bitmap is too optimistic for spilled webs.  Sometimes some defs of
     spilled webs are marked, although they are needed.  This is the case,
     if they have a stack slot (or pseudo) allocated.  */
  for (n = 0; n < info.num_defs; n++)
    {
      struct web *web = def2web[DF_REF_ID (info.defs[n])];
      if (DF_REF_REGNO (info.defs[n]) >= FIRST_PSEUDO_REGISTER
	  && (!bitmap_bit_p (useless_defs, DF_REF_ID (info.defs[n]))
	      || (alias (find_web_for_subweb (web))->type == SPILLED
		  && alias (find_web_for_subweb (web))->stack_slot != NULL)))
	return;
    }
  for (n = 0; n < info.num_defs; n++)
    bitmap_set_bit (webs_changed_layout,
		    find_web_for_subweb (def2web[DF_REF_ID (info.defs[n])])->id);
  for (n = 0; n < info.num_uses; n++)
    bitmap_set_bit (webs_changed_layout,
		    find_web_for_subweb (use2web[DF_REF_ID (info.uses[n])])->id);
  deleted_def_insns++;
  deleted_def_cost += BLOCK_FOR_INSN (insn)->frequency + 1;
  PUT_CODE (insn, NOTE);
  NOTE_LINE_NUMBER (insn) = NOTE_INSN_DELETED;
  df_insn_modify (df, BLOCK_FOR_INSN (insn), insn);
  bitmap_set_bit (ra_modified_insns, INSN_UID (insn));
}

/* In rewrite_program2() we noticed, when a certain insn set a pseudo
   which wasn't live.  Try to delete all those insns.  */

static void
delete_useless_defs ()
{
  unsigned int i;
  /* If the insn only sets the def without any sideeffect (besides
     clobbers or uses), we can delete it.  single_set() also tests
     for INSN_P(insn).  */
  EXECUTE_IF_SET_IN_BITMAP (useless_defs, 0, i,
    {
      rtx insn = DF_REF_INSN (df->defs[i]);
      rtx set = single_set (insn);
      struct web *web = find_web_for_subweb (def2web[i]);
      if (set && web->type == SPILLED && web->stack_slot == NULL
	  && !can_throw_internal (insn))
        {
	  try_delete_useless_def (insn, set);
	}
    });
}

/* Look for spilled webs, on whose behalf no insns were emitted.
   We inversify (sp?) the changed flag of the webs, so after this function
   a nonzero changed flag means, that this web was not spillable (at least
   in this pass).  */

static void
detect_non_changed_webs ()
{
  struct dlist *d, *d_next;
  caller_save_needed = 0;
  for (d = WEBS(SPILLED); d; d = d_next)
    {
      struct web *web = DLIST_WEB (d);
      d_next = d->next;
      if (!web->changed)
	{
	  ra_debug_msg (DUMP_PROCESS, "no insns emitted for spilled web %d\n",
		     web->id);
	  remove_web_from_list (web);
	  put_web (web, COLORED);
	  /* Non changed call crossing webs, whose color is call clobbered
	     will be caller saved later.  I.e. ignore them here.  */
	  if (!(web->crosses_call
		&& TEST_HARD_REG_BIT (regs_invalidated_by_call, web->color)))
	    web->changed = 1;
	  else
	    caller_save_needed = 1;
	}
      else
	web->changed = 0;
      /* From now on web->changed is used as the opposite flag.
	 I.e. colored webs, which have changed set were formerly
	 spilled webs for which no insns were emitted.  */
    }
}

static int need_rebuild PARAMS ((void));
static int
need_rebuild ()
{
  struct dlist *d;
  for (d = WEBS(SPILLED); d; d = d->next)
    {
      struct web *web = DLIST_WEB (d);
      if (web->changed
	  || (!web->crosses_call
	      || !TEST_HARD_REG_BIT (regs_invalidated_by_call, web->color)))
	return 1;
    }
  return 0;
}

/* Before spilling we clear the changed flags for all spilled webs.  */

static void
reset_changed_flag ()
{
  struct dlist *d;
  for (d = WEBS(SPILLED); d; d = d->next)
    DLIST_WEB(d)->changed = 0;
}

/* Check all colored webs to detect ones colored by an_unusable_color.
   These webs are spill temporaries and must be substituted by stack slots.
   IMHO(denisc@overta.ru): This check must be supported in different
   manner.  */
int
subst_to_stack_p ()
{
  struct dlist *d;
  for (d = WEBS(COLORED); d; d = d->next)
    {
      struct web *web = DLIST_WEB (d);
      /* Detect dead spilltemp webs and skip them.  */
      if (web->num_uses == 0 && web->num_defs == 1)
	{
	  rtx dead = DF_REF_INSN (web->defs[0]);
	  if (insn_df[INSN_UID (dead)].num_defs == 1
	      && GET_CODE (dead) == INSN)
	    continue;
	}
      if (web->color == an_unusable_color)
	return 1;
    }
  return 0;
}

/* The toplevel function for this file.  Given a colorized graph,
   and lists of spilled, coalesced and colored webs, we add some
   spill code.  This also sets up the structures for incrementally
   building the interference graph in the next pass.  */

int
actual_spill (spill_p)
     int spill_p ATTRIBUTE_UNUSED;
{
  int i;
  int rebuildit = 1;
  bitmap new_deaths;

  /* If we have a webs colored by an_unusable_color (ie we think that they are
     already in frame) we must put such webs to frame.  */
  if (/* !spill_p && */ subst_to_stack_p ())
    /* If you uncomment the SPILL_P usage then you will have a calls to
       assign_stack_slots only at end of allocation process.
       See to the caller of actual_spill.  */
    {
      assign_stack_slots ();
      return rebuildit;
    }
    
  new_deaths = BITMAP_XMALLOC ();
  if (last_changed_insns)
    BITMAP_XFREE (last_changed_insns);
  last_changed_insns = BITMAP_XMALLOC ();
  reset_changed_flag ();
  spill_coalprop ();
  choose_spill_colors ();
  useless_defs = BITMAP_XMALLOC ();
  if (flag_ra_improved_spilling)
    rewrite_program2 (new_deaths);
  else
    rewrite_program (new_deaths);
  insert_stores (new_deaths);
  webs_changed_layout = BITMAP_XMALLOC ();
  delete_useless_defs ();
  BITMAP_XFREE (useless_defs);
  sbitmap_free (insns_with_deaths);
  insns_with_deaths = sbitmap_alloc (get_max_uid ());
  death_insns_max_uid = get_max_uid ();
  sbitmap_zero (insns_with_deaths);
  EXECUTE_IF_SET_IN_BITMAP (new_deaths, 0, i,
    { SET_BIT (insns_with_deaths, i);});
  if (ra_pass > 1)
    {
      rebuildit = need_rebuild ();
      detect_non_changed_webs ();
    }
  detect_web_parts_to_rebuild ();
  BITMAP_XFREE (webs_changed_layout);
  BITMAP_XFREE (new_deaths);
  return rebuildit;
}

static void allocate_stack_slots PARAMS ((void));
static void
allocate_stack_slots ()
{
  unsigned int *stack_color, *max_size, *need_align;
  rtx *slots;
  unsigned int max_color;
  unsigned int i, max_num;
  bitmap conflicts = BITMAP_XMALLOC ();

  if (BYTES_BIG_ENDIAN)
    abort();

  max_num = num_webs - num_subwebs;
  stack_color = (unsigned int *) xcalloc (max_num, sizeof (int));
  max_size = (unsigned int *) xcalloc (max_num, sizeof (int));
  need_align = (unsigned int *) xcalloc (max_num, sizeof (int));
  max_color = 0;
  for (i = 0; i < max_num; i++)
    if (SPILL_SLOT_P (id2web[i]->regno)
	&& id2web[i]->type == COLORED
       	&& id2web[i]->color == an_unusable_color)
      {
	struct web *web = ID2WEB (i);
	struct conflict_link *wl;
	unsigned int j, this_color;
	bitmap_clear (conflicts);
	for (wl = web->conflict_list; wl; wl = wl->next)
	  if (stack_color[wl->t->id])
	    bitmap_set_bit (conflicts, stack_color[wl->t->id]);
	EXECUTE_IF_SET_IN_BITMAP (web->useless_conflicts, 0, j,
	  {
	    if (stack_color[j])
	      bitmap_set_bit (conflicts, stack_color[j]);
	  });
	for (this_color = 1; bitmap_bit_p (conflicts, this_color);
	     this_color++) ;
	stack_color[i] = this_color;
	if (this_color > max_color)
	  max_color = this_color;
	if (PSEUDO_REGNO_BYTES (web->regno) > max_size[this_color])
	  {
	    /* If we change size we need big alignment.  */
	    if (max_size[this_color])
	      need_align[this_color] = 1;
	    max_size[this_color] = PSEUDO_REGNO_BYTES (web->regno);
	  }
      }

  slots = (rtx *) xcalloc (max_color + 1, sizeof (rtx));
  for (i = 1; i <= max_color; i++)
    {
      enum machine_mode mode;
      rtx place;
      mode = mode_for_size (max_size[i] * BITS_PER_UNIT, MODE_INT, 1);
      place = assign_stack_local (mode, max_size[i], need_align[i] ? -1 : 0);
      /* XXX do something with RTX_UNCHANGING_P ?  */
      set_mem_alias_set (place, new_alias_set ());
      slots[i] = place;
    }
  for (i = 0; i < max_num; i++)
    if (SPILL_SLOT_P (id2web[i]->regno)
	&& id2web[i]->type == COLORED
       	&& id2web[i]->color == an_unusable_color)
      {
	struct web *web = ID2WEB (i);
	unsigned int c = stack_color[i];
	int adjust = 0;
	rtx new = adjust_address_nv (slots[c], GET_MODE (web->orig_x),
				     adjust);
	/* We might want to set something like DECL_RTL later, so unshare
	   the memref.  */
	if (new == slots[c])
	  new = copy_rtx (new);
	web->reg_rtx = new;
      }

  BITMAP_XFREE (conflicts);
  free (slots);
  free (need_align);
  free (max_size);
  free (stack_color);
}

/* Remove all REG_EQUIV notes found in the insn chain.  */

static void
purge_reg_equiv_notes ()
{
  rtx insn, note;
  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    while ((note = find_reg_note (insn, REG_EQUIV, 0)) != NULL)
      remove_note (insn, note);
}

/* Allocate and assign stack slots to all webs colored by
   an_unusable_color.
   Replace all uses and defs to stack slots in all possible cases.  */

static void
assign_stack_slots ()
{
  int i;
  struct dlist *d, *d_next;
  
  if (last_changed_insns)
    BITMAP_XFREE (last_changed_insns);
  last_changed_insns = BITMAP_XMALLOC ();

  /* Cleanup SPILLED list. I hope to colorize all these webs in next pass
     after substitution webs colored by an_unusable_color to stack slots. */
  for (d = WEBS(SPILLED); d; d = d_next)
    {
      struct web *web = DLIST_WEB (d);
      d_next = d->next;
      remove_list (web->dlink, &WEBS(SPILLED));
      put_web (web, INITIAL);
    }

  /* Allocate slots in stack frame and modify all insns which use slots.  */
  assign_stack_slots_1 ();

  if (death_insns_max_uid < get_max_uid ())
    {
      sbitmap old_deaths = insns_with_deaths;
      insns_with_deaths = sbitmap_alloc (get_max_uid ());
      sbitmap_zero (insns_with_deaths);
      EXECUTE_IF_SET_IN_SBITMAP (old_deaths, 0, i,
      { SET_BIT (insns_with_deaths, i);});
      sbitmap_free (old_deaths);
    }
  death_insns_max_uid = get_max_uid ();
  detect_web_parts_to_rebuild ();
}

/* If the WEB connected with a small web referred by REF then substitute
   all refs of a small web to stack slot PLACE.
   Remove dead move insns.
   This is the same as coalesce and substitute. */
static int
coalesce_spill_slot (web, ref, place)
     struct web *web;
     struct ref *ref;
     rtx place;
{
  rtx source;
  struct web *dweb;
  struct web *s;
  struct web *t;
  struct ref **refs;
  int num_refs;
  int i,j;
  rtx move_insn;
  rtx back_move = NULL;
  rtx insn = DF_REF_INSN (ref);

  if (copy_insn_p (insn, NULL, NULL))
    {
      struct ra_insn_info *info = &insn_df[INSN_UID (insn)];
      if (!(info->num_defs == 1 && info->num_uses == 1))
	return 0;
      s = use2web[DF_REF_ID (info->uses[0])];
      t = def2web[DF_REF_ID (info->defs[0])];
    }
  else
    return 0;
  
  if (s == web)
    dweb = t;
  else if (t == web)
    dweb = s;
  else
    return 0;

  if (dweb->type != COLORED || !dweb->spill_temp || dweb->crosses_bb
      || dweb->is_coalesced || dweb->color == an_unusable_color)
    return 0;

  if (TEST_BIT (sup_igraph, s->id * num_webs + t->id)
      || TEST_BIT (sup_igraph, t->id * num_webs + s->id))
    return 0;

  move_insn = insn;
  
  /* Replace all web refs to stack spill slot.  */
  for (i = 0, refs = dweb->uses, num_refs = dweb->num_uses;
       i < 2;
       refs = dweb->defs, num_refs = dweb->num_defs, i++)
    for (j = 0; j < num_refs; j++)
      {
	rtx target;
	rtx insns;

	insn = DF_REF_INSN (refs[j]);
	if (insn == move_insn)
	  continue;
	if (i == 0		/* Is this a use ? */
	    && copy_insn_p (insn, NULL, NULL))
	  {
	    struct ra_insn_info *info = &insn_df[INSN_UID (insn)];
	    if (info->num_defs == 1 && info->num_uses == 1
		&& def2web[DF_REF_ID (info->defs[0])] == web)
	      {
		if (back_move)
		  abort ();
		back_move = insn;
		continue;
	      }
	  }
	  
	target = DF_REF_REG (refs[j]);

	/* Happens when move was deleted before.  */
	if (!INSN_P (insn))
	  continue;

	source = place;
	start_sequence ();
	if (GET_CODE (target) == SUBREG)
	  source = simplify_gen_subreg (GET_MODE (target), place,
					GET_MODE (place),
					SUBREG_BYTE (target));
	insns = get_insns ();
	end_sequence ();
	if (insns)
	  {
	    ra_cancel_changes (0);
	    return 0;
	  }
	ra_validate_change (insn, DF_REF_LOC (refs[j]), source, 1);
      }
  if (!ra_apply_change_group ())
    return 0;
    
  remove_list (dweb->dlink, &WEBS(COLORED));
  put_web (dweb, SPILLED);
  PUT_CODE (move_insn, NOTE);
  NOTE_LINE_NUMBER (move_insn) = NOTE_INSN_DELETED;
  RESET_BIT (insns_with_deaths, INSN_UID (move_insn));
  deleted_move_insns++;
  deleted_move_cost += BLOCK_FOR_INSN (move_insn)->frequency + 1;

  if (back_move)
    {
      PUT_CODE (back_move, NOTE);
      NOTE_LINE_NUMBER (back_move) = NOTE_INSN_DELETED;
      RESET_BIT (insns_with_deaths, INSN_UID (back_move));
      deleted_move_insns++;
      deleted_move_cost += BLOCK_FOR_INSN (back_move)->frequency + 1;
    }
  /* Mark all changed insns.  */
  for (i = 0, refs = dweb->uses, num_refs = dweb->num_uses;
       i < 2;
       refs = dweb->defs, num_refs = dweb->num_defs, i++)
    for (j = 0; j < num_refs; j++)
      {
	insn = DF_REF_INSN (refs[j]);
	df_insn_modify (df, BLOCK_FOR_INSN (insn), insn);
	bitmap_set_bit (ra_modified_insns, INSN_UID (insn));
	if (insn != back_move && insn != move_insn)
	  bitmap_set_bit (last_changed_insns, INSN_UID (insn));
      }
  return 1;
}

/* Allocate and assign stack slots to all refs of spill slot web.
   Replace spill slot web to stack slot in all insns.
   This function also perform a simple elimination of dead insns.  */

static void
assign_stack_slots_1 ()
{
  unsigned int j, i, n, webs_count;
  struct ref **refs;
  unsigned int num_refs;

  ra_debug_msg (DUMP_COLORIZE, "Allocate stack spill slots for webs:\n");

  webs_count = num_webs - num_subwebs;
  for (n = 0; n < webs_count; ++n)
    {
      unsigned int inherent_size;
      unsigned int total_size;
      rtx place;
      enum machine_mode innermode;
      struct web *web = id2web[n];
      
      if (web->type != COLORED || web->color != an_unusable_color)
	continue;

      if (web->stack_slot && !REG_P (web->stack_slot))
	abort ();

      /* Detect dead spilltemp webs and skip them.  */
      if (web->num_uses == 0 && web->num_defs == 1)
	{
	  rtx dead = DF_REF_INSN (web->defs[0]);
	  if (insn_df[INSN_UID (dead)].num_defs == 1
	      && GET_CODE (dead) == INSN)
	    continue;
	}

      if (web->pattern)
	{
	  place = web->pattern;
	  innermode = GET_MODE (web->orig_x);
	}
      else
	{
	  innermode = PSEUDO_REGNO_MODE (web->regno);
	  inherent_size = PSEUDO_REGNO_BYTES (web->regno);
	  total_size = MAX (inherent_size, 0);
	  place = assign_stack_local (innermode,
				      total_size,
				      inherent_size == total_size ? 0: -1);
	  RTX_UNCHANGING_P (place) =
	    RTX_UNCHANGING_P (regno_reg_rtx[web->regno]);
	  set_mem_alias_set (place, new_alias_set ());
	}
      ra_debug_msg (DUMP_COLORIZE, "\t%3d(%d) insns: ",
		    web->id, web->regno);
	  
      web->stack_slot = place;

      /* Replace all web refs to stack spill slot.  */
	  
      for (i = 0, refs = web->uses, num_refs = web->num_uses;
	   i < 2;
	   refs = web->defs, num_refs = web->num_defs, i++)
	for (j = 0; j < num_refs; j++)
	  {
	    rtx target, source;
	    rtx insns;
	    rtx insn = DF_REF_INSN (refs[j]);
	    rtx aux_insn = (i == 0 ? PREV_INSN (insn): NEXT_INSN (insn));
	    basic_block bb = BLOCK_FOR_INSN (insn);

	    /* Happens when spill_coalescing() deletes move insns.  */
	    if (!INSN_P (insn))
	      continue;

	    ra_debug_msg (DUMP_COLORIZE, " %d(%c%d)", INSN_UID (insn),
			  i == 0 ? 'u': 'd',
			  DF_REF_ID (refs[j]));

	    if (i == 1 && web->pattern &&
		insn_df[INSN_UID (insn)].num_defs == 1) /* This is a def.  */
	      {
		/* Remove the def of the rematerialized web.  */
		PUT_CODE (insn, NOTE);
		NOTE_LINE_NUMBER (insn) = NOTE_INSN_DELETED;
		RESET_BIT (insns_with_deaths, INSN_UID (insn));
		deleted_move_insns++;
		deleted_move_cost += BLOCK_FOR_INSN (insn)->frequency + 1;
		df_insn_modify (df, bb, insn);
		bitmap_set_bit (ra_modified_insns, INSN_UID (insn));
		bitmap_set_bit (last_changed_insns, INSN_UID (insn));
		continue;
	      }
	    
	    /* Trying to substitute this use to corresponding web.  */
	    if (coalesce_spill_slot (web, refs[j], place))
	      continue;
	      
	    target = DF_REF_REG (refs[j]);
	    source = place;

	    start_sequence ();
	    if (GET_CODE (target) == SUBREG)
	      source = simplify_gen_subreg (GET_MODE (target), source,
					    innermode,
					    SUBREG_BYTE (target));
	    if (ra_validate_change (insn, DF_REF_LOC (refs[j]), source, 0))
	      {
		df_insn_modify (df, bb, insn);
		bitmap_set_bit (ra_modified_insns, INSN_UID (insn));
		bitmap_set_bit (last_changed_insns, INSN_UID (insn));
	      }
	    else
	      {
		if (i == 0) /* Insn for use.  */
		  ra_emit_move_insn (copy_rtx (target), source);
		else
		  ra_emit_move_insn (source, copy_rtx (target));
	      }
	    insns = get_insns ();
	    end_sequence ();
	    if (insns && i == 0) /* Is this a use?  */
	      {
		rtx pi;
		emit_insn_before (insns, insn);
		if (bb->head == insn)
		  bb->head = NEXT_INSN (aux_insn);
		for (pi = PREV_INSN (insn); pi != aux_insn;
		     pi = PREV_INSN (pi))
		  {
		    set_block_for_insn (pi, bb);
		    df_insn_modify (df, bb, pi);
		    bitmap_set_bit (ra_modified_insns, INSN_UID (pi));
		    bitmap_set_bit (emitted_by_spill, INSN_UID (pi));
		  }
	      }
	    else if (insns)
	      {
		rtx ni;
		emit_insn_after (insns, insn);
		if (bb->end == insn)
		  bb->end = PREV_INSN (aux_insn);
		for (ni = insns; ni != aux_insn; ni = NEXT_INSN (ni))
		  {
		    set_block_for_insn (ni, bb);
		    df_insn_modify (df, bb, ni);
		    bitmap_set_bit (ra_modified_insns, INSN_UID (ni));
		    bitmap_set_bit (emitted_by_spill, INSN_UID (ni));
		  }
	      }
	  }
      ra_debug_msg (DUMP_COLORIZE, "\n");
      remove_list (web->dlink, &WEBS(COLORED));
      put_web (web, SPILLED);
      bitmap_clear_bit (spill_slot_regs, web->regno);
    }

  ra_debug_msg (DUMP_COLORIZE, "\n");

  /* A very simple dead insns elimination.  */
  for (n = 0; n < webs_count; ++n)
    {
      struct web *web = id2web[n];
      
      if (web->type == COLORED && web->num_uses == 0 && web->num_defs == 1)
	{
	  unsigned int i;
	  rtx dead = DF_REF_INSN (web->defs[0]);
	  struct ra_insn_info *info = &insn_df[INSN_UID (dead)];

	  if (info->num_defs != 1 || GET_CODE (dead) != INSN
	      || can_throw_internal (dead))
	    continue;
	  for (i = 0; i < info->num_uses; ++i)
	    {
	      struct web *web1
		= find_web_for_subweb (use2web[DF_REF_ID (info->uses[i])]);
	      if (web1->type == COLORED)
		{
		  remove_list (web1->dlink, &WEBS(COLORED));
		  put_web (web1, SPILLED);
		}
	    }
	  remove_list (web->dlink, &WEBS(COLORED));
	  put_web (web, SPILLED);
	  PUT_CODE (dead, NOTE);
	  NOTE_LINE_NUMBER (dead) = NOTE_INSN_DELETED;
	  RESET_BIT (insns_with_deaths, INSN_UID (dead));
	  df_insn_modify (df, BLOCK_FOR_INSN (dead), dead);
	  bitmap_set_bit (ra_modified_insns, INSN_UID (dead));
	  /* This is not fully correct because not only a move insn can be
	     deleted.  */
	  deleted_move_insns++;
	  deleted_move_cost += BLOCK_FOR_INSN (dead)->frequency + 1;
	}
    }
}

/* A bitmap of pseudo reg numbers which are coalesced directly
   to a hardreg.  Set in emit_colors(), used and freed in
   remove_suspicious_death_notes().  */
static bitmap regnos_coalesced_to_hardregs;

/* Create new pseudos for each web we colored, change insns to
   use those pseudos and set up ra_reg_renumber.  */

void
emit_colors (df)
     struct df *df;
{
  unsigned int i, num, max_num;
  int si;
  struct web *web;
  struct web **order2web;
  int old_max_regno = max_reg_num ();
  regset old_regs;
  basic_block bb;

  /* This bitmap is freed in remove_suspicious_death_notes(),
     which is also the user of it.  */
  regnos_coalesced_to_hardregs = BITMAP_XMALLOC ();

  /* We want to assign stack slots in the order of increasing costs,
     or if we optimize for size in the order of increasing number of
     references.  By that we ensure, that the most often used webs
     have the smallest offsets from the frame pointer, and are encoded in
     possibly fewer bytes.  But the new pseudo regs for colored web we
     want to assign in the order of ID.  */
  max_num = num_webs - num_subwebs;
  order2web = (struct web **) xmalloc (max_num * sizeof (order2web[0]));

  if (BYTES_BIG_ENDIAN)
    {
      for (i = 0, num = 0; i < max_num; i++)
	if (SPILL_SLOT_P (id2web[i]->regno)
	    && id2web[i]->type == COLORED
	    && id2web[i]->color == an_unusable_color)
	  order2web[num++] = id2web[i];
      if (num)
	{
	  qsort (order2web, num, sizeof (order2web[0]), comp_webs_maxcost);
	  for (i = 0; i < num; i++)
	    {
	      struct web *web = order2web[i];
	      unsigned int inherent_size = PSEUDO_REGNO_BYTES (web->regno);
	      unsigned int total_size = MAX (inherent_size, 0);
	      rtx place = assign_stack_local (PSEUDO_REGNO_MODE (web->regno),
					      total_size,
					      inherent_size == total_size ? 0: -1);
	      RTX_UNCHANGING_P (place) =
		  RTX_UNCHANGING_P (regno_reg_rtx[web->regno]);
	      set_mem_alias_set (place, new_alias_set ());
	      web->reg_rtx = place;
	    }
	}
    }
  else
    allocate_stack_slots ();
  free (order2web);

  /* First create the (REG xx) rtx's for all webs, as we need to know
     the number, to make sure, flow has enough memory for them in the
     various tables.  */
  for (i = 0; i < num_webs - num_subwebs; i++)
    {
      web = ID2WEB (i);
      if (web->type != COLORED && web->type != COALESCED)
	continue;
      if (web->crosses_call
	  && web->color >= 0
	  && TEST_HARD_REG_BIT (regs_invalidated_by_call, web->color))
	caller_save_needed = 1;

      if (web->type == COALESCED && alias (web)->type == COLORED)
	continue;
      if (web->regno < FIRST_PSEUDO_REGISTER)
	abort ();

      if (SPILL_SLOT_P (web->regno))
	{
	  if (web->color == an_unusable_color)
	    {
	      if (!web->reg_rtx)
		abort ();
	    }
	  else
	    web->reg_rtx = gen_reg_rtx (PSEUDO_REGNO_MODE (web->regno));
	}
      else
	{
	  /* Special case for i386 'fix_truncdi_nomemory' insn.
	     We must choose mode from insns not from PSEUDO_REGNO_MODE.
	     Actual only for clobbered register.  */
	  if (web->num_uses == 0 && web->num_defs == 1)
	    web->reg_rtx = gen_reg_rtx (GET_MODE (DF_REF_REAL_REG (web->defs[0])));
	  else
	    web->reg_rtx = gen_reg_rtx (PSEUDO_REGNO_MODE (web->regno));
	  /* Remember the different parts directly coalesced to a hardreg.  */
	  if (web->type == COALESCED)
	    bitmap_set_bit (regnos_coalesced_to_hardregs, REGNO (web->reg_rtx));
	}
    }
  ra_max_regno = max_regno = max_reg_num ();
  allocate_reg_info (max_regno, FALSE, FALSE);
  ra_reg_renumber = (short *) xmalloc (max_regno * sizeof (short));
  for (si = 0; si < max_regno; si++)
    ra_reg_renumber[si] = -1;

  /* Then go through all references, and replace them by a new
     pseudoreg for each web.  All uses.  */
  /* XXX
     Beware: The order of replacements (first uses, then defs) matters only
     for read-mod-write insns, where the RTL expression for the REG is
     shared between def and use.  For normal rmw insns we connected all such
     webs, i.e. both the use and the def (which are the same memory)
     there get the same new pseudo-reg, so order would not matter.
     _However_ we did not connect webs, were the read cycle was an
     uninitialized read.  If we now would first replace the def reference
     and then the use ref, we would initialize it with a REG rtx, which
     gets never initialized, and yet more wrong, which would overwrite
     the definition of the other REG rtx.  So we must replace the defs last.
   */
  for (i = 0; i < df->use_id; i++)
    if (df->uses[i])
      {
	regset rs = DF_REF_BB (df->uses[i])->global_live_at_start;
	rtx regrtx;
	web = use2web[i];
	web = find_web_for_subweb (web);
	if (web->type != COLORED && web->type != COALESCED)
	  continue;
	regrtx = alias (web)->reg_rtx;
	if (!regrtx)
	  regrtx = web->reg_rtx;
	*DF_REF_REAL_LOC (df->uses[i]) = regrtx;
	if (REGNO_REG_SET_P (rs, web->regno) && REG_P (regrtx))
	  {
	    /*CLEAR_REGNO_REG_SET (rs, web->regno);*/
	    SET_REGNO_REG_SET (rs, REGNO (regrtx));
	  }
      }

  /* And all defs.  */
  for (i = 0; i < df->def_id; i++)
    {
      regset rs;
      rtx regrtx;
      if (!df->defs[i])
	continue;
      rs = DF_REF_BB (df->defs[i])->global_live_at_start;
      web = def2web[i];
      web = find_web_for_subweb (web);
      if (web->type != COLORED && web->type != COALESCED)
	continue;
      regrtx = alias (web)->reg_rtx;
      if (!regrtx)
	regrtx = web->reg_rtx;
      *DF_REF_REAL_LOC (df->defs[i]) = regrtx;
      if (REGNO_REG_SET_P (rs, web->regno) && REG_P (regrtx))
	{
	  /* Don't simply clear the current regno, as it might be
	     replaced by two webs.  */
          /*CLEAR_REGNO_REG_SET (rs, web->regno);*/
          SET_REGNO_REG_SET (rs, REGNO (regrtx));
	}
    }

  /* And now set up the ra_reg_renumber array for reload with all the new
     pseudo-regs.  */
  for (i = 0; i < num_webs - num_subwebs; i++)
    {
      web = ID2WEB (i);
      if (web->reg_rtx && REG_P (web->reg_rtx))
	{
	  int r = REGNO (web->reg_rtx);
          ra_reg_renumber[r] = web->color;
          ra_debug_msg (DUMP_COLORIZE, "Renumber pseudo %d (== web %d) to %d\n",
		     r, web->id, ra_reg_renumber[r]);
	}
    }

  /* Coalesced webs will get the same pseudo in RTL.  That combined pseudo
     most probably has more definitions than it's parts.  If one part has
     associated REG_EQUIV notes this would create the wrong picture, so
     we need to remove those invalid REG_EQUIV notes.  For the time being
     we simply delete _all_ REG_EQUIV notes.  */
  purge_reg_equiv_notes ();

  old_regs = BITMAP_XMALLOC ();
  for (si = FIRST_PSEUDO_REGISTER; si < old_max_regno; si++)
    SET_REGNO_REG_SET (old_regs, si);
  FOR_EACH_BB (bb)
    {
      AND_COMPL_REG_SET (bb->global_live_at_start, old_regs);
      AND_COMPL_REG_SET (bb->global_live_at_end, old_regs);
    }
  BITMAP_XFREE (old_regs);
}

/* Delete some coalesced moves from the insn stream.  */

void
delete_moves ()
{
  struct move_list *ml;
  struct web *s, *t;
  /* XXX Beware: We normally would test here each copy insn, if
     source and target got the same color (either by coalescing or by pure
     luck), and then delete it.
     This will currently not work.  One problem is, that we don't color
     the regs ourself, but instead defer to reload.  So the colorization
     is only a kind of suggestion, which reload doesn't have to follow.
     For webs which are coalesced to a normal colored web, we only have one
     new pseudo, so in this case we indeed can delete copy insns involving
     those (because even if reload colors them different from our suggestion,
     it still has to color them the same, as only one pseudo exists).  But for
     webs coalesced to precolored ones, we have not a single pseudo, but
     instead one for each coalesced web.  This means, that we can't delete
     copy insns, where source and target are webs coalesced to precolored
     ones, because then the connection between both webs is destroyed.  Note
     that this not only means copy insns, where one side is the precolored one
     itself, but also those between webs which are coalesced to one color.
     Also because reload we can't delete copy insns which involve any
     precolored web at all.  These often have also special meaning (e.g.
     copying a return value of a call to a pseudo, or copying pseudo to the
     return register), and the deletion would confuse reload in thinking the
     pseudo isn't needed.  One of those days reload will get away and we can
     do everything we want.
     In effect because of the later reload, we can't base our deletion on the
     colors itself, but instead need to base them on the newly created
     pseudos.  */
  for (ml = wl_moves; ml; ml = ml->next)
    /* The real condition we would ideally use is: s->color == t->color.
       Additionally: s->type != PRECOLORED && t->type != PRECOLORED, in case
       we want to prevent deletion of "special" copies.  */
    if (ml->move
	&& (s = alias (ml->move->source_web))->reg_rtx
	    == (t = alias (ml->move->target_web))->reg_rtx
	&& s->type != PRECOLORED && t->type != PRECOLORED)
      {
	basic_block bb = BLOCK_FOR_INSN (ml->move->insn);
	df_insn_delete (df, bb, ml->move->insn);
	deleted_move_insns++;
	deleted_move_cost += bb->frequency + 1;
      }
}

/* Due to resons documented elsewhere we create different pseudos
   for all webs coalesced to hardregs.  For these parts life_analysis()
   might have added REG_DEAD notes without considering, that only this part
   but not the whole coalesced web dies.  The RTL is correct, there is no
   coalescing yet.  But if later reload's alter_reg() substitutes the
   hardreg into the REG rtx it looks like that particular hardreg dies here,
   although (due to coalescing) it still is live.  This might make different
   places of reload think, it can use that hardreg for reload regs,
   accidentally overwriting it.  So we need to remove those REG_DEAD notes.
   (Or better teach life_analysis() and reload about our coalescing, but
   that comes later) Bah.  */

void
remove_suspicious_death_notes ()
{
  rtx insn;
  for (insn = get_insns(); insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      {
	rtx *pnote = &REG_NOTES (insn);
	while (*pnote)
	  {
	    rtx note = *pnote;
	    if ((REG_NOTE_KIND (note) == REG_DEAD
		 || REG_NOTE_KIND (note) == REG_UNUSED)
		&& (GET_CODE (XEXP (note, 0)) == REG
		    && bitmap_bit_p (regnos_coalesced_to_hardregs,
				     REGNO (XEXP (note, 0)))))
	      *pnote = XEXP (note, 1);
	    else
	      pnote = &XEXP (*pnote, 1);
	  }
      }
  BITMAP_XFREE (regnos_coalesced_to_hardregs);
  regnos_coalesced_to_hardregs = NULL;
}

/* Allocate space for max_reg_num() pseudo registers, and
   fill reg_renumber[] from ra_reg_renumber[].  If FREE_IT
   is nonzero, also free ra_reg_renumber and reset ra_max_regno.  */

void
setup_renumber (free_it)
     int free_it;
{
  int i;
  max_regno = max_reg_num ();
  allocate_reg_info (max_regno, FALSE, TRUE);
  for (i = 0; i < max_regno; i++)
    {
      reg_renumber[i] = (i < ra_max_regno) ? ra_reg_renumber[i] : -1;
    }
  if (free_it)
    {
      free (ra_reg_renumber);
      ra_reg_renumber = NULL;
      ra_max_regno = 0;
    }
}

static struct web * get_aliased_aequivalent PARAMS ((struct web *));
static struct web *
get_aliased_aequivalent (web)
     struct web *web;
{
  struct web *supweb = find_web_for_subweb (web);
  struct web *aweb = alias (supweb);
  /* Go to the alias web, except if that's a precolored web
     (indicated by not having a reg_rtx), in which case we have
     created the new pseudo for the web itself.  */
  if (supweb != aweb && aweb->reg_rtx)
    {
      struct web *oweb = web;
      if (SUBWEB_P (web))
	web = find_subweb (aweb, web->orig_x);
      else
	web = aweb;
      /* XXX We need to make this not happen anymore.  We anyway need
	 to change the whole handling of subregs.  */
      /*if (!web)
	abort ();*/
      if (!web)
	web = oweb;
    }
  return web;
}

/* Insert top level clobbers so the conservative life information
   functions are not confused by partial sets, which _we_ know are the
   initial defines.  */
void
create_flow_barriers ()
{
  basic_block bb;
  sbitmap live;
  bitmap partly_defined = BITMAP_XMALLOC ();
  live = sbitmap_alloc (num_webs);
  FOR_EACH_BB (bb)
    {
      int j;
      rtx insn, prev_insn;
      sbitmap_zero (live);
      EXECUTE_IF_SET_IN_BITMAP (live_at_end[bb->index], 0, j,
	{
	  set_web_live (live, get_aliased_aequivalent (use2web[j]));
	});
      for (insn = bb->end; insn; insn = prev_insn)
	{
	  prev_insn = PREV_INSN (insn);

	  if (INSN_P (insn))
	    {
	      unsigned int n;
	      struct ra_insn_info info = insn_df[INSN_UID (insn)];

	      bitmap_zero (partly_defined);
	      for (n = 0; n < info.num_defs; n++)
		{
		  struct web *web = def2web[DF_REF_ID (info.defs[n])];
		  web = get_aliased_aequivalent (web);
		  if (SUBWEB_P (web))
		    bitmap_set_bit (partly_defined,
				    find_web_for_subweb (web)->id);
		  reset_web_live (live, web);
		}

	      for (n = 0; n < info.num_uses; n++)
		{
		  struct web *web = use2web[DF_REF_ID (info.uses[n])];
		  web = get_aliased_aequivalent (web);
		  set_web_live (live, web);
		}

	      EXECUTE_IF_SET_IN_BITMAP (partly_defined, 0, j,
		{
		  struct web *web = ID2WEB (j);
		  if (web->type != PRECOLORED
		      /* If we wouldn't need to have the XXX hack in
			 get_aliased_aequivalent() we could be sure to
			 have web->reg_rtx set.  */
		      && web->reg_rtx
		      && REG_P (web->reg_rtx) && !is_partly_live (live, web))
		    {
		      emit_insn_before (gen_rtx_CLOBBER (VOIDmode,
							 web->reg_rtx), insn);
		    }
		});
	    }

	  if (insn == bb->head)
	    break;
	}
    }
  sbitmap_free (live);
  BITMAP_XFREE (partly_defined);
}

/* The WEB can't have a single color. The REF is a constraining ref.
   The REF will be spilled out from the WEB.  */
void
web_class_spill_ref (web, ref)
     struct web *web;
     struct ref *ref;
{
  rtx insns;
  rtx insn = DF_REF_INSN (ref);

  if (DF_REF_REG_USE_P (ref))
    {
      int num_refs;
      int i, j;
      rtx source, target;
      struct ref **refs;
      rtx def_dst;
      rtx def_src = NULL;
      rtx reg = gen_reg_rtx (PSEUDO_REGNO_MODE (web->regno));
      basic_block bb = BLOCK_FOR_INSN (insn);

      for (i = 0, refs = web->uses, num_refs = web->num_uses;
	   i < 2;
	   refs = web->defs, num_refs = web->num_defs, i++)
	for (j = 0; j < num_refs; j++)
	  {
	    if (DF_REF_INSN (refs[j]) != insn)
	      continue;
	      
	    target = DF_REF_REG (refs[j]);
	    source = reg;

	    if (GET_CODE (target) == SUBREG)
	      source = simplify_gen_subreg (GET_MODE (target), source,
					    GET_MODE (source),
					    SUBREG_BYTE (target));
	    ra_validate_change (insn, DF_REF_LOC (refs[j]), source, 1);
	    if (i == 1) /* This is a def.  */
	      {
		if (def_src)
		  abort ();
		def_src = source;
		def_dst = DF_REF_REG (refs[j]);
	      }
	  }
      if (!ra_apply_change_group ())
	abort ();

      df_insn_modify (df, bb, insn);
      bitmap_set_bit (ra_modified_insns, INSN_UID (insn));

      start_sequence ();
      ra_emit_move_insn (reg, web->orig_x);
      insns = get_insns ();
      end_sequence ();
      if (insns)
	{
	  rtx pi;
	  rtx aux_insn = PREV_INSN (insn);
	  emit_insn_before (insns, insn);
	  if (bb->head == insn)
	    bb->head = NEXT_INSN (aux_insn);
	  for (pi = PREV_INSN (insn); pi != aux_insn;
	       pi = PREV_INSN (pi))
	    {
	      set_block_for_insn (pi, bb);
	      df_insn_modify (df, bb, pi);
	      bitmap_set_bit (ra_modified_insns, INSN_UID (pi));
	    }
	}

      if (def_src)
	{
	  start_sequence ();
	  ra_emit_move_insn (def_dst, copy_rtx (def_src));
	  insns = get_insns ();
	  end_sequence ();
	  if (insns)
	    {
	      rtx ni;
	      rtx aux_insn = NEXT_INSN (insn);
	      emit_insn_after (insns, insn);
	      if (bb->end == insn)
		bb->end = PREV_INSN (aux_insn);
	      for (ni = insns; ni != aux_insn; ni = NEXT_INSN (ni))
		{
		  set_block_for_insn (ni, bb);
		  df_insn_modify (df, bb, ni);
		  bitmap_set_bit (ra_modified_insns, INSN_UID (ni));
		}
	    }
	}
    }
  else if (DF_REF_REG_DEF_P (ref))
    {
      rtx aux_insn = NEXT_INSN (insn);
      rtx reg = gen_reg_rtx (GET_MODE (DF_REF_REG (ref)));
      basic_block bb = BLOCK_FOR_INSN (insn);
      
      if (ra_validate_change (insn, DF_REF_LOC (ref), reg, 0))
	{
	  df_insn_modify (df, bb, insn);
	  bitmap_set_bit (ra_modified_insns, INSN_UID (insn));
	}
      else
	abort ();

      start_sequence ();
      ra_emit_move_insn (DF_REF_REG (ref), reg);
      insns = get_insns ();
      end_sequence ();
      if (insns)
	{
	  rtx ni;
	  emit_insn_after (insns, insn);
	  if (bb->end == insn)
	    bb->end = PREV_INSN (aux_insn);
	  for (ni = insns; ni != aux_insn; ni = NEXT_INSN (ni))
	    {
	      set_block_for_insn (ni, bb);
	      df_insn_modify (df, bb, ni);
	      bitmap_set_bit (ra_modified_insns, INSN_UID (ni));
	    }
	}
    }
}

/* Dump the costs and savings due to spilling, i.e. of added spill insns
   and removed moves or useless defs.  */

void
dump_cost (level)
     unsigned int level;
{
  ra_debug_msg (level, "Instructions for spilling\n added:\n");
  ra_debug_msg (level, "  loads =%d cost=", emitted_spill_loads);
  ra_debug_msg (level, HOST_WIDE_INT_PRINT_UNSIGNED, spill_load_cost);
  ra_debug_msg (level, "\n  stores=%d cost=", emitted_spill_stores);
  ra_debug_msg (level, HOST_WIDE_INT_PRINT_UNSIGNED, spill_store_cost);
  ra_debug_msg (level, "\n  remat =%d cost=", emitted_remat);
  ra_debug_msg (level, HOST_WIDE_INT_PRINT_UNSIGNED, spill_remat_cost);
  ra_debug_msg (level, "\n removed:\n  moves =%d cost=", deleted_move_insns);
  ra_debug_msg (level, HOST_WIDE_INT_PRINT_UNSIGNED, deleted_move_cost);
  ra_debug_msg (level, "\n  others=%d cost=", deleted_def_insns);
  ra_debug_msg (level, HOST_WIDE_INT_PRINT_UNSIGNED, deleted_def_cost);
  ra_debug_msg (level, "\n");
}

/* Initialization of the rewrite phase.  */

void
ra_rewrite_init ()
{
  emitted_spill_loads = 0;
  emitted_spill_stores = 0;
  emitted_remat = 0;
  spill_load_cost = 0;
  spill_store_cost = 0;
  spill_remat_cost = 0;
  deleted_move_insns = 0;
  deleted_move_cost = 0;
  deleted_def_insns = 0;
  deleted_def_cost = 0;
}

/*
vim:cinoptions={.5s,g0,p5,t0,(0,^-0.5s,n-0.5s:tw=78:cindent:sw=4:
*/
