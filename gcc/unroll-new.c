/* Loop unrolling and peeling.
   Copyright (C) 2002 Free Software Foundation, Inc.

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
#include "rtl.h"
#include "tm_p.h"
#include "obstack.h"
#include "function.h"
#include "expr.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "insn-config.h"
#include "regs.h"
#include "recog.h"
#include "flags.h"
#include "real.h"
#include "cselib.h"
#include "except.h"
#include "toplev.h"
#include "predict.h"
#include "insn-flags.h"
#include "cfglayout.h"
#include "loop.h"
#include "params.h"
#include "output.h"

static basic_block simple_exit PARAMS ((struct loops *, struct loop *, basic_block *, int *));
static bool simple_condition_p PARAMS ((struct loop *, basic_block *, rtx, struct loop_desc *));
static basic_block simple_increment PARAMS ((struct loops *, struct loop *, basic_block *, struct loop_desc *));
static rtx variable_initial_value PARAMS ((struct loop *, rtx));
static int count_loop_iterations PARAMS ((struct loop_desc *, int *, rtx *));
static int unroll_loop_new PARAMS ((struct loops *, struct loop *, int));
static int peel_loop PARAMS ((struct loops *, struct loop *, int));
static bool simple_loop_p PARAMS ((struct loops *, struct loop *, struct loop_desc *));
static int unroll_simple_loop PARAMS ((struct loops *, struct loop *, int, struct loop_desc *));
static int unroll_loop_constant_iterations PARAMS ((struct loops *, struct loop *, int, struct loop_desc *));
static int unroll_loop_runtime_iterations PARAMS ((struct loops *, struct loop *, int, struct loop_desc *));

/* Unroll LOOPS.  */
void
unroll_loops (loops, unroll_all)
     struct loops *loops;
     int unroll_all;
{
  int i;
  /* Scan the loops, last ones first, since this means inner ones are done
     before outer ones.  Perhaps it is not best to rely on it, as this
     could change, but for now it is true.  */
  for (i = loops->num - 1; i > 0; i--)
    {
      struct loop *loop = loops->parray[i];
      unroll_loop_new (loops, loop, unroll_all);
#ifdef ENABLE_CHECKING
      verify_dominators ();
      verify_loop_structure (loops, VLS_FOR_LOOP_NEW);
#endif
    }
}

/* Peel LOOPS.  */
void
peel_loops (loops, will_unroll)
     struct loops *loops;
     int will_unroll;
{
  int i;
  for (i = loops->num - 1; i > 0; i--)
    {
      struct loop *loop = loops->parray[i];
      peel_loop (loops, loop, will_unroll);
#ifdef ENABLE_CHECKING
      verify_dominators ();
      verify_loop_structure (loops, VLS_FOR_LOOP_NEW);
#endif
    }
}

/* Checks whether LOOP (consisting of BODY -- just not to have to find
   it again and again) have simple exit (i.e. exit is in exactly one block
   that is executed in every iteration exactly once). FALLTRHU_OUT
   is set if the exit edge is fallthru.  Exit block is returned.  */
static basic_block
simple_exit (loops, loop, body, fallthru_out)
     struct loops *loops;
     struct loop *loop;
     basic_block *body;
     int *fallthru_out;
{
  basic_block exit_bb;
  int i;
  edge e;

  /* Loop must have single exit only.  */
  exit_bb = NULL;
  for (i = 0; i < loop->num_nodes; i++)
    for (e = body[i]->succ; e; e = e->succ_next)
      if (!flow_bb_inside_loop_p (loop, e->dest))
	{
	  if (exit_bb)
	    return NULL;
	  else
	    exit_bb = body[i];
	  *fallthru_out = (e->flags & EDGE_FALLTHRU);
	}
  if (!exit_bb)
    return NULL;

  /* And it must be tested once during any iteration.  */
  if (!just_once_each_iteration_p (loops, loop, exit_bb))
    return NULL;

  /* It must end in a simple conditional jump.  */
  if (!any_condjump_p (exit_bb->end))
    return NULL;

  return exit_bb;
}

/* Checks whether CONDITION is a simple comparison in that one of operands
   is register and the other one is invariant in the LOOP. Fills var, lim
   and cond fields in DESC.  */
static bool
simple_condition_p (loop, body, condition, desc)
     struct loop *loop;
     basic_block *body;
     rtx condition;
     struct loop_desc *desc;
{
  rtx op0, op1;
  int i;

  /* Check condition.  */
  switch (GET_CODE (condition))
    {
      case EQ:
      case NE:
      case LE:
      case LT:
      case GE:
      case GT:
      case GEU:
      case GTU:
      case LEU:
      case LTU:
	break;
      default:
	return false;
    }

  /* Of integers or pointers.  */
  if (GET_MODE_CLASS (GET_MODE (XEXP (condition, 0))) != MODE_INT
      && GET_MODE_CLASS (GET_MODE (XEXP (condition, 0))) != MODE_PARTIAL_INT)
    return false;

  /* One of operands must be a simple register.  */
  op0 = XEXP (condition, 0);
  op1 = XEXP (condition, 1);
  
  /* One of operands must be invariant.  */
  for (i = 0; i < loop->num_nodes; i++)
    if (modified_between_p (op0, body[i]->head, NEXT_INSN (body[i]->end)))
      break;
  if (i == loop->num_nodes)
    {
      /* And the other one must be a register.  */
      if (!REG_P (op1))
	return false;
      desc->var = op1;
      desc->lim = op0;
      desc->cond = swap_condition (GET_CODE (condition));
      if (desc->cond == UNKNOWN)
	return false;
      return true;
    }

  /* Check the other operand. */
  for (i = 0; i < loop->num_nodes; i++)
    if (modified_between_p (op1, body[i]->head, NEXT_INSN (body[i]->end)))
      break;
  if (i != loop->num_nodes)
    return false;
  if (!REG_P (op0))
    return false;

  desc->var = op0;
  desc->lim = op1;
  desc->cond = GET_CODE (condition);

  return true;
}

/* Checks whether DESC->var is incremented/decremented exactly once each
   iteration.  Fills in DESC->grow and returns block in that DESC->var is
   modified.  */
static basic_block
simple_increment (loops, loop, body, desc)
     struct loops *loops;
     struct loop *loop;
     basic_block *body;
     struct loop_desc *desc;
{
  rtx mod_insn, insn, set, set_src, set_add;
  basic_block mod_bb;
  int i;

  /* Find insn that modifies var.  */
  mod_insn = NULL;
  for (i = 0; i < loop->num_nodes; i++)
    if (modified_between_p (desc->var, body[i]->head, NEXT_INSN (body[i]->end)))
      {
	for (insn = NEXT_INSN (body[i]->head);
	     insn != NEXT_INSN (body[i]->end);
	     insn = NEXT_INSN (insn))
	  if (modified_between_p (desc->var, PREV_INSN (insn), NEXT_INSN (insn)))
	    {
	      if (mod_insn)
		return NULL;
	      else
		mod_insn = insn;
	    }
	mod_bb = body[i];
      }
  if (!mod_insn)
    return NULL;

  /* Check that it is executed exactly once each iteration.  */
  if (!just_once_each_iteration_p (loops, loop, mod_bb))
    return NULL;

  /* mod_insn must be a simple increment/decrement.  */
  set = single_set (mod_insn);
  if (!set)
    return NULL;
  if (!rtx_equal_p (SET_DEST (set), desc->var))
    return NULL;

  set_src = SET_SRC (set);
  if (GET_CODE (set_src) != PLUS)
    return NULL;
  if (!rtx_equal_p (XEXP (set_src, 0), desc->var))
    return NULL;

  /* Set desc->grow.  */
  set_add = XEXP (set_src, 1);
  if (set_add == const1_rtx)
    desc->grow = 1;
  else if (set_add == constm1_rtx)
    desc->grow = 0;
  else
    return NULL;

  return mod_bb;
}

/* Tries to find initial value of VAR in LOOP (not exactly -- it only works
   if it is constant expression.  */
static rtx
variable_initial_value (loop, var)
     struct loop *loop;
     rtx var;
{
  edge e;
  basic_block bb;
  rtx set, insn;

  /* Go back through cfg.  */
  e = loop_preheader_edge (loop);
  for (bb = e->src; bb->pred; bb = bb->pred->src)
    {
      for (insn = bb->end;
	   insn != bb->head;
	   insn = PREV_INSN (insn))
	if (modified_between_p (var, PREV_INSN (insn), NEXT_INSN (insn)))
	  break;

      if (insn != bb->head)
	{
	  /* We found place where var is set.  */
	  rtx set_dest;
	  set = single_set (insn);
	  if (!set)
	    return NULL;
	  set_dest = SET_DEST (set);
	  if (!rtx_equal_p (set_dest, var))
	    return NULL;
	  return SET_SRC (set);
	}
      if (bb->pred->pred_next)
	return NULL;
    }

  return NULL;
}

/* Tests whether LOOP is simple for loop.  Returns simple loop description
   in DESC.  */
static bool
simple_loop_p (loops, loop, desc)
     struct loops *loops;
     struct loop *loop;
     struct loop_desc *desc;
{
  basic_block exit_bb, *body, mod_bb;
  int fallthru_out;
  rtx condition;

  body = get_loop_body (loop);

  /* There must be only a single exit from loop.  */
  if (!(exit_bb = simple_exit (loops, loop, body, &fallthru_out)))
    goto ret_false;

  /* Condition must be a simple comparison in that one of operands
     is register and the other one is invariant.  */
  if (!(condition = get_condition (exit_bb->end, NULL)))
    goto ret_false;
 
  if (!simple_condition_p (loop, body, condition, desc))
    goto ret_false;
 
  /*  Var must be simply incremented or decremented in exactly one insn that
      is executed just once every iteration.  */
  if (!(mod_bb = simple_increment (loops, loop, body, desc)))
    goto ret_false;

  /* OK, it is simple loop.  Now just fill in remaining info.  */
  desc->postincr = !dominated_by_p (loops->cfg.dom, exit_bb, mod_bb);
  desc->neg = !fallthru_out;

  /* Find initial value of var.  */
  desc->init = variable_initial_value (loop, desc->var);

  /* Find numeric values of bounds.  */
  if (GET_CODE (desc->lim) == CONST_INT)
    desc->lim_n = INTVAL (desc->lim);
  if (desc->init && GET_CODE (desc->init) == CONST_INT)
    desc->init_n = INTVAL (desc->init);

  desc->const_iter = GET_CODE (desc->lim) == CONST_INT
  		     && desc->init
		     && GET_CODE (desc->init) == CONST_INT;

  free (body);

  if (rtl_dump_file)
    {
      fprintf (rtl_dump_file, "; Simple loop %i\n", loop->num);
      if (desc->postincr)
	fprintf (rtl_dump_file,
		 ";  does postincrement after loop exit condition\n");
      if (desc->var)
	{
	  fprintf (rtl_dump_file, ";  Induction variable:");
	  print_simple_rtl (rtl_dump_file, desc->var);
	  fputc ('\n', rtl_dump_file);
	}
      fprintf (rtl_dump_file,
	       desc->grow ? ";  Counter grows\n": ";  Counter decreases\n");
      if (desc->init)
	{
	  fprintf (rtl_dump_file, ";  Initial value:");
	  print_simple_rtl (rtl_dump_file, desc->init);
	  fputc ('\n', rtl_dump_file);
	}
      if (desc->lim)
	{
	  fprintf (rtl_dump_file, ";  Compared with:");
	  print_simple_rtl (rtl_dump_file, desc->lim);
	  fputc ('\n', rtl_dump_file);
	}
      if (desc->cond)
	{
	  fprintf (rtl_dump_file, ";  Exit condtion:");
	  if (desc->neg)
	    fprintf (rtl_dump_file, "(negated)");
	  fprintf (rtl_dump_file, "%s\n", GET_RTX_NAME (desc->cond));
	  fputc ('\n', rtl_dump_file);
	}
    }
  return true;

  ret_false:
  free (body);
  return false;
}

/* Counts number of iterations described by DESC or emits sequence to count it.  */
static int
count_loop_iterations (desc, niter, rniter)
     struct loop_desc *desc;
     int *niter;
     rtx *rniter;
{
  int delta;
  HOST_WIDE_INT abs_diff = 0;

  if (desc->grow)
    {
      if (rniter)
	{
	  *rniter = expand_simple_binop (GET_MODE (desc->var), MINUS,
			  copy_rtx (desc->lim), copy_rtx (desc->var),
			  NULL_RTX, 0, OPTAB_LIB_WIDEN);
	}
      if (niter)
	{
          abs_diff = desc->lim_n - desc->init_n;
	}
    }
  else
    {
      desc->cond = swap_condition (desc->cond);
      if (desc->cond == UNKNOWN)
	return 0;
      if (rniter)
	*rniter = expand_simple_binop (GET_MODE (desc->var), MINUS,
			copy_rtx (desc->var), copy_rtx (desc->lim),
			NULL_RTX, 0, OPTAB_LIB_WIDEN);
      if (niter)
	{
	  abs_diff = desc->init_n - desc->lim_n;
	}
    }

  /* Given that iteration_var is going to iterate over its own mode,
     not HOST_WIDE_INT, disregard higher bits that might have come
     into the picture due to sign extension of initial and final
     values.  */
  abs_diff &= ((unsigned HOST_WIDE_INT) 1
	       << (GET_MODE_BITSIZE (GET_MODE (desc->var)) - 1)
	       << 1) - 1;
      
  if (desc->neg)
    {
      desc->cond = reverse_condition (desc->cond);
      if (desc->cond == UNKNOWN)
	return 0;
    }

  delta = 0;
  if (!desc->postincr)
    delta--;

  switch (desc->cond)
    {
      case NE:
      case LT:
	break;
      case LE:
	delta++;
	break;
      case GE:
      case GT:
      case EQ:
	/* These cases are a bit strange.  Better not to play with
	   something I don't understand.  */
	return 0;
      default:
	abort ();
    }

  if (niter)
    {
      *niter = abs_diff + delta;
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";  Number of iterations: %i\n", *niter);
    }

  if (rniter && delta)
    *rniter = expand_simple_binop (GET_MODE (desc->var), PLUS,
		*rniter,
		GEN_INT (delta),
		NULL_RTX, 0, OPTAB_LIB_WIDEN);

  return 1;
}

/* Unroll LOOP with constant number of iterations described by DESC.
   MAX_UNROLL is maximal number of allowed unrollings.  */
static int
unroll_loop_constant_iterations (loops, loop, max_unroll, desc)
     struct loops *loops;
     struct loop *loop;
     int max_unroll;
     struct loop_desc *desc;
{
  int niter, exit_mod;
  sbitmap wont_exit;

  /* Normalization.  */
  if (!count_loop_iterations (desc, &niter, NULL))
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not unrolling loop, can't count loop iterations\n");
      return 0;
    }  

  if (niter < 0)
    {
      if (desc->cond == NE)
	{
	  int i;
	  /* We must count with overflow.  */
	  for (i = 1; 2 * i <= max_unroll + 1; i *= 2);
	  max_unroll = i - 1;
	  niter = niter % i + 256;
	}
      else
	niter = 0;
    }

  if (niter <= max_unroll)
    {
      /* We may peel the loop completely, and remove all
	 exit edges but the last one.  */
      wont_exit = sbitmap_alloc (niter + 2);
      sbitmap_ones (wont_exit);
      RESET_BIT (wont_exit, 0);
      RESET_BIT (wont_exit, niter + 1);

      if (!duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
		loops, niter + 1, wont_exit, DLTHE_FLAG_ALL))
	abort ();

      free (wont_exit);
      if (rtl_dump_file)
        fprintf (rtl_dump_file, ";; Unrolled loop %d times\n",niter);
      return 1;
    }

  wont_exit = sbitmap_alloc (max_unroll + 1);
  sbitmap_ones (wont_exit);
  exit_mod = niter % (max_unroll + 1);

  if (desc->postincr)
    {
      /* Counter is incremented after the exit test; leave exit test
	 in the first copy.  */

      if (rtl_dump_file)
        fprintf (rtl_dump_file, ";; Condition on beginning of loop.\n");

      /* Peel exit_mod iterations.  */
      RESET_BIT (wont_exit, 0);

      if (exit_mod
	  && !duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
		loops, exit_mod, wont_exit, DLTHE_FLAG_ALL))
	abort ();
    }
  else
    {
      /* Leave exit test in last copy.  */

      if (rtl_dump_file)
        fprintf (rtl_dump_file, ";; Condition on end of loop.\n");

      /* We know that niter >= max_unroll + 1; so we do not need to care of
	 case when we would exit before reaching the loop.  So just peel
	 exit_mod + 1 iterations.
	 */
      if (exit_mod != max_unroll)
	{
	  RESET_BIT (wont_exit, 0);

	  if (!duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
		loops, exit_mod + 1, wont_exit, DLTHE_FLAG_ALL))
	    abort ();

	  SET_BIT (wont_exit, 0);
	}

      RESET_BIT (wont_exit, max_unroll);
    }

  /* Now unroll the loop.  */
  if (!duplicate_loop_to_header_edge (loop, loop_latch_edge (loop),
		loops, max_unroll, wont_exit, DLTHE_FLAG_ALL))
    abort ();

  free (wont_exit);
  if (rtl_dump_file)
    fprintf (rtl_dump_file, ";; Unrolled loop %d times\n",max_unroll);
  
  return 1;
}

/* Unroll LOOP for that we are able to count number of iterations in runtime.
   MAX_UNROLL is maximal number of allowed unrollings.  DESC describes the loop.  */
static int
unroll_loop_runtime_iterations (loops, loop, max_unroll, desc)
     struct loops *loops;
     struct loop *loop;
     int max_unroll;
     struct loop_desc *desc;
{
  rtx niter, init_code, branch_code;
  rtx loop_beg_label;
  int i;
  basic_block fake, preheader, *body, dom;
  edge e;
  sbitmap wont_exit;
  int may_exit_copy, n_peel;
  int expected_niter;

  expected_niter = expected_loop_iterations (loop);
  if (expected_niter < max_unroll && flag_branch_probabilities)
    max_unroll = expected_niter;

  if (max_unroll <= 1)
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not unrolling loop, expected number of iteration is low\n");
      return 0;
    }

  /* Force max_unroll + 1 to be power of 2.  */
  for (i = 1; 2 * i <= max_unroll + 1; i *= 2);
  max_unroll = i - 1;

  /* Normalization.  */
  start_sequence ();
  if (!count_loop_iterations (desc, NULL, &niter))
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not unrolling loop, can't count loop iterations\n");      
      end_sequence ();
      return 0;
    }

  /* Count modulo by ANDing it with max_unroll.  */
  niter = expand_simple_binop (GET_MODE (desc->var), AND,
		niter,
		GEN_INT (max_unroll),
		NULL_RTX, 0, OPTAB_LIB_WIDEN);

  if (desc->postincr)
    {
      /* Leave exit in first copy.  */
      may_exit_copy = 0;
      n_peel = max_unroll;
    }
  else
    {
      /* Leave exit in last copy.  */
      may_exit_copy = max_unroll;
      niter = expand_simple_binop (GET_MODE (desc->var), PLUS,
		niter,
		const1_rtx,
		NULL_RTX, 0, OPTAB_LIB_WIDEN);
      n_peel = max_unroll + 1;
      /* First check for zero is obviously unnecessary now; it might seem
	 we could do better by increasing it before AND; but we must have
	 guaranteed that exit condition will be checked in first iteration,
	 so that we won't miscompile loop with negative number of iterations.  */
    }

  niter = expand_simple_binop (GET_MODE (desc->var), PLUS,
		niter,
		const1_rtx,
		NULL_RTX, 0, OPTAB_LIB_WIDEN);

  init_code = gen_sequence ();
  end_sequence ();

  /* Precondition the loop.  */
  loop_split_edge_with (loop_preheader_edge (loop), init_code, loops);

  /* Fake block, to record edges we need to redirect.  */
  fake = create_basic_block (n_basic_blocks, NULL, NULL);
  loop_beg_label = block_label (fake);

  for (i = 0; i < n_peel; i++)
    {
      start_sequence ();
      niter = expand_simple_binop (GET_MODE (desc->var), MINUS,
			niter, const1_rtx,
			NULL_RTX, 0, OPTAB_LIB_WIDEN);
      do_compare_rtx_and_jump (copy_rtx (niter), const0_rtx, EQ, 0, GET_MODE (desc->var),
			       NULL_RTX, NULL_RTX, loop_beg_label);
      JUMP_LABEL (get_last_insn ()) = loop_beg_label;
      LABEL_NUSES (loop_beg_label)++;
      branch_code = gen_sequence ();
      end_sequence ();

      preheader = loop_split_edge_with (loop_preheader_edge (loop), branch_code, loops);
      make_edge (preheader, fake, 0);

      wont_exit = sbitmap_alloc (2);
      sbitmap_zero (wont_exit);
      /* We must be a bit careful here, as we might have negative
	 number of iterations.  Also, in case of postincrement we do
	 not know whether we should not exit before reaching the loop.  */
      if (desc->postincr
	  && (i || desc->cond == NE))
        SET_BIT (wont_exit, 1);

      if (!duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
		loops, 1, wont_exit, DLTHE_FLAG_ALL))
        abort ();
      free (wont_exit);
    }

  /* Now redirect the edges from fake.  */
  preheader = loop_split_edge_with (loop_preheader_edge (loop), NULL_RTX, loops);
  loop_beg_label = block_label (preheader);

  for (e = fake->pred; e; e = fake->pred)
    {
      if (!redirect_edge_and_branch (e, preheader))
	abort ();
    }

  dom = recount_dominator (loops->cfg.dom, preheader);
  set_immediate_dominator (loops->cfg.dom, preheader, dom);

  if (desc->cond != NE || !desc->postincr)
    {
      /* Recount dominators for outer blocks.  */
      body = get_loop_body (loop);
      for (i = 0; i < loop->num_nodes; i++)
	for (e = body[i]->succ; e; e = e->succ_next)
	  {
	    if (flow_bb_inside_loop_p (loop, e->dest))
	      continue;
	    set_immediate_dominator (loops->cfg.dom, e->dest,
	      nearest_common_dominator (loops->cfg.dom, e->dest, dom));
	  }
      free (body);
    }

  /* Get rid of fake.  */
  flow_delete_block (fake);

  /* And unroll loop.  */

  wont_exit = sbitmap_alloc (max_unroll + 1);
  sbitmap_ones (wont_exit);
  RESET_BIT (wont_exit, may_exit_copy);

  if (!duplicate_loop_to_header_edge (loop, loop_latch_edge (loop),
	loops, max_unroll, wont_exit, DLTHE_FLAG_ALL))
    abort ();

  free (wont_exit);
  if (rtl_dump_file)
    fprintf (rtl_dump_file, ";; Unrolled loop %d times\n", max_unroll);
  
  return 1;
}

/* Unroll a simple for LOOP described by DESC.  MAX_UNROLL is the maximal
   number of unrollings allowed.  */
static int
unroll_simple_loop (loops, loop, max_unroll, desc)
     struct loops *loops;
     struct loop *loop;
     int max_unroll;
     struct loop_desc *desc;
{
  if (!can_duplicate_loop_p (loop))
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not unrolling loop, can't duplicate\n");
      return 0;
    }

  switch (desc->cond)
    {
      case GEU:
      case GTU:
      case LEU:
      case LTU:
	if (rtl_dump_file)
	  fprintf (rtl_dump_file, ";;  Not unrolling loop, GEU/GTU/LEU/LTU condition\n");
	/* I'm not brave enough to cope with this.  */
	return 0;
      default:
	break;
    }

  if (desc->const_iter)
    {
      /* Simple case -- we know number of iterations exactly.  */
      return unroll_loop_constant_iterations (loops, loop, max_unroll, desc);
    }
  else
    {
      /* Try to calculate it in runtime.  */
      return unroll_loop_runtime_iterations (loops, loop, max_unroll, desc);
    }
  return 1;
}

/* Peel a LOOP.  Returs 0 if impossible, 1 otherwise.  */
static int
peel_loop (loops, loop, will_unroll)
     struct loops *loops;
     struct loop *loop;
     int will_unroll;
{
  int ninsns = 0, npeel;
  int niter;
  struct loop_desc desc;
  sbitmap wont_exit;

  if (!can_duplicate_loop_p (loop))
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not peeling loop, can't duplicate\n");
      return 0;
    }

  /* Do not peel cold areas.  */
  if (!maybe_hot_bb_p (loop->header))
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not peeling loop, header is cold area\n");
      return 1;
    }

  /* Only peel innermost loops.  */
  if (loop->inner)
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not peeling loop, not innermost loop\n");
      return 1;
    }
      
  ninsns = num_loop_insns (loop);

  npeel = PARAM_VALUE (PARAM_MAX_PEELED_INSNS) / ninsns - 1;
  if (npeel > PARAM_VALUE (PARAM_MAX_PEEL_TIMES))
    npeel = PARAM_VALUE (PARAM_MAX_PEEL_TIMES);

  /* Do not peel loops that roll too much.  */
  niter = expected_loop_iterations (loop);
  if (niter > npeel - 1)
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not peeling loop, rolls too much (%d iterations > %d [maximum peelings - 1])\n", niter, npeel - 1);
      return 1;
    }
  npeel = niter;
  
  /* Neither big loops.  */
  if (npeel <= 0)
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not peeling loop, is too big\n");
      return 1;
    }

  wont_exit = sbitmap_alloc (npeel + 1);
  sbitmap_zero (wont_exit);

  if (!duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
		loops, npeel, wont_exit, DLTHE_FLAG_ALL))
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Peeling unsuccessful\n");
      return 0;
    }

  free (wont_exit);

  if (rtl_dump_file)
    fprintf (rtl_dump_file, ";; Peeling loop %d times\n", npeel);

  return 1;
}

/* Unroll a LOOP.  Returs 0 if impossible, 1 otherwise.  */
static int
unroll_loop_new (loops, loop, unroll_all)
     struct loops *loops;
     struct loop *loop;
     int unroll_all;
{
  int ninsns = 0, nunroll, niter;
  struct loop_desc desc;
  sbitmap wont_exit;

  /* Do not unroll cold areas.  */
  if (!maybe_hot_bb_p (loop->header))
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not unrolling loop, cold area\n");
      return 1;
    }

  if (!can_duplicate_loop_p (loop))
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not unrolling loop, can't duplicate\n");
      return 0;
    }

  ninsns = num_loop_insns (loop);

  nunroll = PARAM_VALUE (PARAM_MAX_UNROLLED_INSNS) / ninsns - 1;
  if (nunroll > PARAM_VALUE (PARAM_MAX_UNROLL_TIMES))
    nunroll = PARAM_VALUE (PARAM_MAX_UNROLL_TIMES);

  /* Neither big loops.  */
  if (nunroll <= 0)
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not unrolling loop, is too big\n");
      return 1;
    }

  if (simple_loop_p (loops, loop, &desc))
    {
      /* Simple for loop.  */
      if (unroll_simple_loop (loops, loop, nunroll, &desc))
	return 1;
    }
  
  if (!unroll_all)
    return 1;
  
  /* Do not unroll loops that do not roll.  */
  niter = expected_loop_iterations (loop);
  if (niter < 2 * nunroll && flag_branch_probabilities)
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";; Not unrolling loop, doesn't roll\n");
      return 1;
    }

  /* Some hard case; try stupid unrolling anyway.  */
  wont_exit = sbitmap_alloc (nunroll + 1);
  sbitmap_zero (wont_exit);

  if (!duplicate_loop_to_header_edge (loop, loop_latch_edge (loop),
		loops, nunroll, wont_exit, DLTHE_FLAG_ALL))
    {
      if (rtl_dump_file)
	fprintf (rtl_dump_file, ";;  Not unrolling loop, can't duplicate\n");
      return 0;
    }

  free (wont_exit);
  if (rtl_dump_file)
    fprintf (rtl_dump_file, ";; Unrolled loop %d times\n", nunroll);
	  
  return 1;
}
