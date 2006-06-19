/* Predictive commoning.
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

/* This file implements the predictive commoning optimization.  Predictive
   commoning can be viewed as CSE around a loop, and with some improvements,
   as generalized strength reduction-- i.e., reusing values computed in
   earlier iterations of a loop in the later ones.  So far, the pass only
   handles the most useful case, that is, reusing values of memory references.
   If you think this is all just a special case of PRE, you are sort of right;
   however, concentrating on loops is simpler, and makes it possible to
   incorporate data dependence analysis to detect the opportunities, perform
   loop unrolling to avoid copies together with renaming immediately,
   and if needed, we could also take register pressure into account.

   Let us demonstrate what is done on an example:
   
   for (i = 0; i < 100; i++)
     {
       a[i+2] = a[i] + a[i+1];
       b[10] = b[10] + i;
       c[i] = c[99 - i];
       d[i] = d[i + 1];
     }

   1) We find data references in the loop, and split them to mutually
      independent groups (i.e., we find components of a data dependence
      graph).  We ignore read-read dependences whose distance is not constant.
      (TODO -- we could also ignore antidependences).  In this example, we
      find the following groups:

      a[i]{read}, a[i+1]{read}, a[i+2]{write}
      b[10]{read}, b[10]{write}
      c[99 - i]{read}, c[i]{write}
      d[i + 1]{read}, d[i]{write}

   2) Inside each of the group, we verify several conditions:
      a) all the references must differ in indices only, and the indices
	 must all have the same step
      b) the references must dominate loop latch (and thus, they must be
	 ordered by dominance relation).
      c) the distance of the indices must be a small multiple of the step
      We are then able to compute the difference of the references (# of
      iterations before they point to the same place as the first of them).
      Also, in case there are writes in the loop, we split the groups into
      smaller chains (this keeps the number of registers we need the same,
      but makes the chains shorter, and thus requires lower unrolling factor
      in order to get rid of the copies on the loop latch).
      
      In our example,

      a[i]{read,+0}, a[i+1]{read,-1}, a[i+2]{write,-2}
      b[10]{read,+0}, b[10]{write,+0}
      d[i + 1]{read,+0}, d[i]{write,+1}

   3) For each read, we determine the read or write whose value it reuses,
      together with the distance of this reuse.  I.e. we take the last
      reference before it with distance 0, or the last of the references
      with the smallest positive distance to the read.  Then, we remove
      the references that are not used in any of these chains, discard the
      empty groups, and propagate all the links so that they point to the
      single root reference of the chain (adjusting their distance 
      appropriately).  Some extra care needs to be taken for references with
      step 0.  In our example,

      a[i] --> (*) 2, a[i+1] --> (*) 1, a[i+2] (*)
      b[10] --> (*) 1, b[10] (*)

   4) The chains are combined together if possible.  If the corresponding
      elements of two chains are always combined together with the same
      operator, we remember just the result of this combination, instead
      of remembering the values separately.

   5) For each root reference (end of the chain) R, let N be maximum distance
      of a reference reusing its value.  Variables R0 upto RN are created,
      together with phi nodes that transfer values from R1 .. RN to
      R0 .. R(N-1).
      Initial values are loaded to R0..R(N-1) (in case not all references
      must necessarily be accessed and they may trap, we may fail here;
      TODO sometimes, the loads could be guarded by a check for the number
      of iterations).  Values loaded/stored in roots are also copied to
      RN.  Other reads are replaced with the appropriate variable Ri.
      Everything is put to SSA form.

      As a small improvement, if R0 is dead after the root (i.e., all uses of
      the value with the maximum distance dominate it), we can avoid creating
      RN and use R0 instead.

   On our example, we get (only the parts concerning a and b are shown):
   for (i = 0; i < 100; i++)
     {
       f = phi (a[0], s);
       s = phi (a[1], f);
       x = phi (b[10], x);

       f = f + s;
       a[i+2] = f;
       x = x + i;
       b[10] = x;
     }

   6) Factor F for unrolling is determined as the smallest common multiple of
      (N + 1) for each root reference (N for references for that we avoided
      creating RN).  If F and the loop is small enough, loop is unrolled F
      times.  The stores to RN (R0) in the copies of the loop body are
      periodically replaced with R0, R1, ... (R1, R2, ...), so that they can
      be coalesced and the copies can be eliminated.  In order to prevent
      copy propagation and other optimizations from messing up with these
      SSA names and making the life ranges overlap, we set
      SSA_NAME_OCCURS_IN_ABNORMAL_PHI for them (which is a bit hacky).
      In our case, F = 2 and the (main loop of the) result is

      for (i = 0; i < ...; i += 2)
        {
          f = phi (a[0], f);
          s = phi (a[1], s);
          x = phi (b[10], x);

          f = f + s;
          a[i+2] = f;
          x = x + i;
          b[10] = x;

          s = s + f;
          a[i+3] = s;
          x = x + i;
          b[10] = x;
       }

   TODO -- stores killing other stores can be taken into account, e.g.,
   for (i = 0; i < n; i++)
     {
       a[i] = 1;
       a[i+2] = 2;
     }

   can be replaced with

   t0 = a[0];
   t1 = a[1];
   for (i = 0; i < n; i++)
     {
       a[i] = 1;
       t2 = 2;
       t0 = t1;
       t1 = t2;
     }
   a[n] = t0;
   a[n+1] = t1;

   The interesting part is that this would generalize store motion; still, since
   sm is performed elsewhere, it does not seem that important.

   Predictive commoning can be generalized for arbitrary computations (not
   just memory loads), and also nontrivial transfer functions (e.g., replacing
   i * i with ii_last + 2 * i + 1), to generalize strength reduction.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tm_p.h"
#include "cfgloop.h"
#include "tree-flow.h"
#include "ggc.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-chrec.h"
#include "params.h"
#include "diagnostic.h"
#include "tree-pass.h"
#include "tree-affine.h"

/* The maximum number of iterations between the considered memory
   references.  */

#define MAX_DISTANCE (target_avail_regs < 16 ? 4 : 8)
   
/* Data references.  */

typedef struct dref
{
  /* The reference itself.  */
  struct data_reference *ref;

  /* The statement in that the reference appears.  */
  tree stmt;

  /* Distance of the reference from the root of the chain (in number of
     iterations of the loop).  */
  unsigned distance;

  /* Number of iterations offset from the first reference in the component.  */
  double_int offset;

  /* Number of the reference in a component, in dominance ordering.  */
  unsigned pos;

  /* True if all the memory reference is always executed when the loop is
     entered.  */
  unsigned always_executed : 1;
} *dref;

DEF_VEC_P (dref);
DEF_VEC_ALLOC_P (dref, heap);

/* Type of the chain of the references.  */

enum chain_type
{
  /* The addresses of the references in the chain are constant.  */
  CT_INVARIANT,

  /* There are only loads in the chain.  */
  CT_LOAD,

  /* Root of the chain is store, the rest are loads.  */
  CT_STORE_LOAD,

  /* A combination of two chains.  */
  CT_COMBINATION
};

/* Chains of data references.  */

typedef struct chain
{
  /* Type of the chain.  */
  enum chain_type type;

  /* For combination chains, the operator and the two chains that are
     combined, and the type of the result.  */
  enum tree_code operator;
  tree rslt_type;
  struct chain *ch1, *ch2;

  /* The references in the chain.  */
  VEC(dref,heap) *refs;

  /* The maximum distance of the reference in the chain from the root.  */
  unsigned length;

  /* The variables used to copy the value throughout iterations.  */
  VEC(tree,heap) *vars;

  /* Initializers for the variables.  */
  VEC(tree,heap) *inits;

  /* True if there is a use of a variable with the maximal distance
     that comes after the root in the loop.  */
  unsigned has_max_use_after : 1;

  /* True if all the memory references in the chain are always executed.  */
  unsigned all_always_executed : 1;

  /* True if this chain was combined together with some other chain.  */
  unsigned combined : 1;
} *chain_p;

DEF_VEC_P (chain_p);
DEF_VEC_ALLOC_P (chain_p, heap);

/* Describes the knowledge about the step of the memory references in
   the component.  */

enum ref_step_type
{
  /* The step is zero.  */
  RS_INVARIANT,

  /* The step is nonzero.  */
  RS_NONZERO,

  /* The step may or may not be nonzero.  */
  RS_ANY
};

/* Components of the data dependence graph.  */

struct component
{
  /* The references in the component.  */
  VEC(dref,heap) *refs;

  /* What we know about the step of the references in the component.  */
  enum ref_step_type comp_step;

  /* Next component in the list.  */
  struct component *next;
};

/* Dumps data reference REF to FILE.  */

extern void dump_dref (FILE *, dref);
void
dump_dref (FILE *file, dref ref)
{
  if (ref->ref)
    {
      fprintf (file, "    ");
      print_generic_expr (dump_file, DR_REF (ref->ref), TDF_SLIM);
      fprintf (file, " (id %u%s)\n", ref->pos,
	       DR_IS_READ (ref->ref) ? "" : ", write");

      fprintf (file, "      offset ");
      dump_double_int (file, ref->offset, false);
      fprintf (file, "\n");

      fprintf (file, "      distance %u\n", ref->distance);
    }
  else
    {
      fprintf (file, "    combination ref\n");
      fprintf (file, "      in statement ");
      print_generic_expr (dump_file, ref->stmt, TDF_SLIM);
      fprintf (file, "\n");
      fprintf (file, "      distance %u\n", ref->distance);
    }

}

/* Dumps CHAIN to FILE.  */

extern void dump_chain (FILE *, chain_p);
void
dump_chain (FILE *file, chain_p chain)
{
  dref a;
  const char *chain_type;
  unsigned i;
  tree var;

  switch (chain->type)
    {
    case CT_INVARIANT:
      chain_type = "Load motion";
      break;

    case CT_LOAD:
      chain_type = "Loads-only";
      break;

    case CT_STORE_LOAD:
      chain_type = "Store-loads";
      break;

    case CT_COMBINATION:
      chain_type = "Combination";
      break;

    default:
      gcc_unreachable ();
    }

  fprintf (file, "%s chain %p%s\n", chain_type, (void *) chain,
	   chain->combined ? " (combined)" : "");
  if (chain->type != CT_INVARIANT)
    fprintf (file, "  max distance %u%s\n", chain->length,
	     chain->has_max_use_after ? "" : ", may reuse first");

  if (chain->type == CT_COMBINATION)
    {
      fprintf (file, "  equal to %p %s %p in type ",
	       (void *) chain->ch1, op_symbol_code (chain->operator),
	       (void *) chain->ch2);
      print_generic_expr (dump_file, chain->rslt_type, TDF_SLIM);
      fprintf (file, "\n");
    }

  if (chain->vars)
    {
      fprintf (file, "  vars");
      for (i = 0; VEC_iterate (tree, chain->vars, i, var); i++)
	{
	  fprintf (file, " ");
	  print_generic_expr (dump_file, var, TDF_SLIM);
	}
      fprintf (file, "\n");
    }

  if (chain->inits)
    {
      fprintf (file, "  inits");
      for (i = 0; VEC_iterate (tree, chain->inits, i, var); i++)
	{
	  fprintf (file, " ");
	  print_generic_expr (dump_file, var, TDF_SLIM);
	}
      fprintf (file, "\n");
    }

  fprintf (file, "  references:\n");
  for (i = 0; VEC_iterate (dref, chain->refs, i, a); i++)
    dump_dref (file, a);

  fprintf (file, "\n");
}

/* Dumps CHAINS to FILE.  */

extern void dump_chains (FILE *, VEC (chain_p, heap) *);
void
dump_chains (FILE *file, VEC (chain_p, heap) *chains)
{
  chain_p chain;
  unsigned i;

  for (i = 0; VEC_iterate (chain_p, chains, i, chain); i++)
    dump_chain (file, chain);
}

/* Dumps COMP to FILE.  */

extern void dump_component (FILE *, struct component *);
void
dump_component (FILE *file, struct component *comp)
{
  dref a;
  unsigned i;

  fprintf (file, "Component%s:\n",
	   comp->comp_step == RS_INVARIANT ? " (invariant)" : "");
  for (i = 0; VEC_iterate (dref, comp->refs, i, a); i++)
    dump_dref (file, a);
  fprintf (file, "\n");
}

/* Dumps COMPS to FILE.  */

extern void dump_components (FILE *, struct component *);
void
dump_components (FILE *file, struct component *comps)
{
  struct component *comp;

  for (comp = comps; comp; comp = comp->next)
    dump_component (file, comp);
}

/* Frees a chain CHAIN.  */

static void
release_chain (chain_p chain)
{
  dref ref;
  unsigned i;

  if (chain == NULL)
    return;

  for (i = 0; VEC_iterate (dref, chain->refs, i, ref); i++)
    free (ref);

  VEC_free (dref, heap, chain->refs);
  VEC_free (tree, heap, chain->vars);
  VEC_free (tree, heap, chain->inits);

  free (chain);
}

/* Frees CHAINS.  */

static void
release_chains (VEC (chain_p, heap) *chains)
{
  unsigned i;
  chain_p chain;

  for (i = 0; VEC_iterate (chain_p, chains, i, chain); i++)
    release_chain (chain);
  VEC_free (chain_p, heap, chains);
}

/* Frees a component COMP.  */

static void
release_component (struct component *comp)
{
  VEC_free (dref, heap, comp->refs);
  free (comp);
}

/* Frees list of components COMPS.  */

static void
release_components (struct component *comps)
{
  struct component *act, *next;

  for (act = comps; act; act = next)
    {
      next = act->next;
      release_component (act);
    }
}

/* Finds a root of tree given by FATHERS containing A, and performs path
   shortening.  */

static unsigned
component_of (unsigned fathers[], unsigned a)
{
  unsigned root, n;

  for (root = a; root != fathers[root]; root = fathers[root])
    continue;

  for (; a != root; a = n)
    {
      n = fathers[a];
      fathers[a] = root;
    }

  return root;
}

/* Join operation for DFU.  FATHERS gives the tree, SIZES are sizes of the
   components, A and B are components to merge.  */

static void
merge_comps (unsigned fathers[], unsigned sizes[], unsigned a, unsigned b)
{
  unsigned ca = component_of (fathers, a);
  unsigned cb = component_of (fathers, b);

  if (ca == cb)
    return;

  if (sizes[ca] < sizes[cb])
    {
      sizes[cb] += sizes[ca];
      fathers[ca] = cb;
    }
  else
    {
      sizes[ca] += sizes[cb];
      fathers[cb] = ca;
    }
}

/* Similar to operand_equal_p, but handles the case that X and Y are NULL.  */

static bool
operand_eq_p (tree x, tree y)
{
  if (!x)
    return y == NULL_TREE;
  if (!y)
    return false;

  return operand_equal_p (x, y, 0);
}

/* The recursive part of suitable_reference_p.  Returns true if A is a reference
   that is suitable for predictive commoning in LOOP.  REF_STEP is set according to
   the step of the reference A.  */

static bool
suitable_reference_p_1 (struct loop *loop, tree a, enum ref_step_type *ref_step)
{
  enum tree_code code;
  affine_iv iv;
  enum ref_step_type sub_step = RS_INVARIANT;

  if (handled_component_p (a))
    {
      code = TREE_CODE (a);
      if (!suitable_reference_p_1 (loop, TREE_OPERAND (a, 0), &sub_step))
	return false;

      /* To be able to move the initialization of the reference from
	 the loop, the fields that we do not update must be invariant.  */
      if (TREE_CODE (a) == COMPONENT_REF
	  && !expr_invariant_in_loop_p (loop, component_ref_field_offset (a)))
	return false;
      if (TREE_CODE (a) == ARRAY_REF
	  && (!expr_invariant_in_loop_p (loop, array_ref_element_size (a))
	      || !expr_invariant_in_loop_p (loop, array_ref_low_bound (a))))
	return false;

      if (code != ARRAY_REF)
	{
	  *ref_step = sub_step;
	  return true;
	}

      a = TREE_OPERAND (a, 1);
    }
  else if (DECL_P (a))
    return true;
  else if (TREE_CODE (a) == INDIRECT_REF)
    a = TREE_OPERAND (a, 0);
  else
    return false;

  if (!simple_iv (loop, first_stmt (loop->header), a, &iv, true))
    return false;

  if (zero_p (iv.step))
    *ref_step = sub_step;
  else if (nonzero_p (iv.step))
    *ref_step = RS_NONZERO;
  else
    *ref_step = RS_ANY;

  return true;
}

/* Returns true if A is a reference that is suitable for predictive commoning
   in LOOP.  REF_STEP is set according to the step of the reference A.  */

static bool
suitable_reference_p (struct loop *loop, tree a, enum ref_step_type *ref_step)
{
  if (!is_gimple_reg_type (TREE_TYPE (a)))
    return false;

  return suitable_reference_p_1 (loop, a, ref_step);
}

/* Determines number of iterations of LOOP before B refers to exactly the
   same location as A and stores it to OFF.  If A and B do not have the same
   step, they never meet, or anything else fails, returns false, otherwise
   returns true.  Both A and B are assumed to satisfy suitable_reference_p.  */

static bool
determine_offset (struct loop *loop, tree a, tree b, double_int *off)
{
  enum tree_code code;
  affine_iv iva, ivb;
  tree type;
  aff_tree diff, baseb, step;
  double_int aoff;
  bool ok;

  *off = double_int_zero;

  if (TREE_TYPE (a) != TREE_TYPE (b))
    return false;

  if (handled_component_p (a))
    {
      code = TREE_CODE (a);
      if (code != TREE_CODE (b))
	return false;

      if (!determine_offset (loop, TREE_OPERAND (a, 0),
			     TREE_OPERAND (b, 0), off))
	return false;

      switch (code)
	{
	case ARRAY_RANGE_REF:
	  if (!operand_eq_p (TREE_OPERAND (a, 3), TREE_OPERAND (b, 3)))
	    return false;

	  /* Fallthru.  */

	case BIT_FIELD_REF:
	case COMPONENT_REF:
	  if (!operand_eq_p (TREE_OPERAND (a, 2), TREE_OPERAND (b, 2)))
	    return false;
	  if (!operand_eq_p (TREE_OPERAND (a, 1), TREE_OPERAND (b, 1)))
	    return false;

	  /* Fallthru.  */

	case VIEW_CONVERT_EXPR:
	case REALPART_EXPR:
	case IMAGPART_EXPR:
	  return true;

	case ARRAY_REF:
	  if (!operand_eq_p (TREE_OPERAND (a, 3), TREE_OPERAND (b, 3)))
	    return false;
	  if (!operand_eq_p (TREE_OPERAND (a, 2), TREE_OPERAND (b, 2)))
	    return false;

	  a = TREE_OPERAND (a, 1);
	  b = TREE_OPERAND (b, 1);
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  else if (DECL_P (a))
    return operand_equal_p (a, b, 0);
  else if (TREE_CODE (a) == INDIRECT_REF && TREE_CODE (b) == INDIRECT_REF)
    {
      a = TREE_OPERAND (a, 0);
      b = TREE_OPERAND (b, 0);
    }
  else
    return false;

  /* Check whether a and b are indices with the same step.  */
  ok = (simple_iv (loop, first_stmt (loop->header), a, &iva, true)
	&& simple_iv (loop, first_stmt (loop->header), b, &ivb, true));
  gcc_assert (ok);

  if (!operand_eq_p (iva.step, ivb.step))
    return false;
  if (operand_equal_p (iva.base, ivb.base, 0))
    return true;
  if (zero_p (iva.step))
    return false;

  type = TREE_TYPE (iva.base);
  tree_to_aff_combination_expand (iva.base, type, &diff);
  tree_to_aff_combination_expand (ivb.base, type, &baseb);
  aff_combination_scale (&baseb, double_int_minus_one);
  aff_combination_add (&diff, &baseb);

  tree_to_aff_combination_expand (iva.step, type, &step);
  if (!aff_combination_constant_multiple_p (&diff, &step, &aoff))
    return false;

  if (!double_int_zero_p (*off) && !double_int_equal_p (*off, aoff))
    return false;

  *off = aoff;
  return true;
}

/* Returns the last basic block in LOOP for that we are sure that
   it is executed whenever the loop is entered.  */

static basic_block
last_always_executed_block (struct loop *loop)
{
  unsigned i, n_exits;
  edge *exits = get_loop_exit_edges (loop, &n_exits);
  basic_block last = loop->latch;

  for (i = 0; i < n_exits; i++)
    last = nearest_common_dominator (CDI_DOMINATORS, last, exits[i]->src);

  return last;
}

/* Splits dependence graph on DATAREFS described by DEPENDS to components.  */

static struct component *
split_data_refs_to_components (struct loop *loop,
			       VEC (data_reference_p, heap) *datarefs,
			       VEC (ddr_p, heap) *depends)
{
  unsigned i, n = VEC_length (data_reference_p, datarefs);
  unsigned ca, ia, ib, bad;
  unsigned *comp_father = XNEWVEC (unsigned, n + 1);
  unsigned *comp_size = XNEWVEC (unsigned, n + 1);
  struct component **comps;
  struct data_reference *dr, *dra, *drb;
  struct data_dependence_relation *ddr;
  struct component *comp_list = NULL, *comp;
  dref dataref;
  basic_block last_always_executed = last_always_executed_block (loop);
 
  for (i = 0; VEC_iterate (data_reference_p, datarefs, i, dr); i++)
    {
      if (!DR_REF (dr))
	{
	  /* A fake reference for call or asm_expr that may clobber memory;
	     just fail.  */
	  goto end;
	}
      dr->aux = i;
      comp_father[i] = i;
      comp_size[i] = 1;
    }

  /* A component reserved for the "bad" data references.  */
  comp_father[n] = n;
  comp_size[n] = 1;

  for (i = 0; VEC_iterate (data_reference_p, datarefs, i, dr); i++)
    {
      enum ref_step_type dummy;

      if (!suitable_reference_p (loop, DR_REF (dr), &dummy))
	{
	  ia = dr->aux;
	  merge_comps (comp_father, comp_size, n, ia);
	}
    }

  for (i = 0; VEC_iterate (ddr_p, depends, i, ddr); i++)
    {
      double_int dummy_off;

      if (DDR_ARE_DEPENDENT (ddr) == chrec_known)
	continue;

      dra = DDR_A (ddr);
      drb = DDR_B (ddr);
      ia = component_of (comp_father, dra->aux);
      ib = component_of (comp_father, drb->aux);
      if (ia == ib)
	continue;

      bad = component_of (comp_father, n);

      /* If both A and B are reads, we may ignore unsuitable dependences.  */
      if (DR_IS_READ (dra) && DR_IS_READ (drb)
	  && (ia == bad || ib == bad
	      || !determine_offset (loop, DR_REF (dra), DR_REF (drb),
				    &dummy_off)))
	continue;
	  
      merge_comps (comp_father, comp_size, ia, ib);
    }

  comps = XCNEWVEC (struct component *, n);
  bad = component_of (comp_father, n);
  for (i = 0; VEC_iterate (data_reference_p, datarefs, i, dr); i++)
    {
      ia = dr->aux;
      ca = component_of (comp_father, ia);
      if (ca == bad)
	continue;

      comp = comps[ca];
      if (!comp)
	{
	  comp = XCNEW (struct component);
	  comp->refs = VEC_alloc (dref, heap, comp_size[ca]);
	  comps[ca] = comp;
	}

      dataref = XCNEW (struct dref);
      dataref->ref = dr;
      dataref->stmt = DR_STMT (dr);
      dataref->offset = double_int_zero;
      dataref->distance = 0;

      dataref->always_executed
	      = dominated_by_p (CDI_DOMINATORS, last_always_executed,
				bb_for_stmt (dataref->stmt));
      dataref->pos = VEC_length (dref, comp->refs);
      VEC_quick_push (dref, comp->refs, dataref);
    }

  for (i = 0; i < n; i++)
    {
      comp = comps[i];
      if (comp)
	{
	  comp->next = comp_list;
	  comp_list = comp;
	}
    }
  free (comps);

end:
  free (comp_father);
  free (comp_size);
  return comp_list;
}

/* Returns true if the component COMP satisfies the conditions
   described in 2) at the begining of this file.  LOOP is the current
   loop.  */
      
static bool
suitable_component_p (struct loop *loop, struct component *comp)
{
  unsigned i;
  dref a;
  basic_block ba, bp = loop->header;
  tree ref;
  bool ok, has_write = false;

  for (i = 0; VEC_iterate (dref, comp->refs, i, a); i++)
    {
      ba = bb_for_stmt (a->stmt);

      if (!just_once_each_iteration_p (loop, ba))
	return false;

      gcc_assert (dominated_by_p (CDI_DOMINATORS, ba, bp));
      bp = ba;

      if (!DR_IS_READ (a->ref))
	has_write = true;
    }

  a = VEC_index (dref, comp->refs, 0);
  ref = DR_REF (a->ref);
  ok = suitable_reference_p (loop, ref, &comp->comp_step);
  gcc_assert (ok);
  a->offset = double_int_zero;

  for (i = 1; VEC_iterate (dref, comp->refs, i, a); i++)
    {
      if (!determine_offset (loop, ref, DR_REF (a->ref), &a->offset))
	return false;

#ifdef ENABLE_CHECKING
      {
	enum ref_step_type a_step;
	ok = suitable_reference_p (loop, ref, &a_step);
	gcc_assert (ok && a_step == comp->comp_step);
      }
#endif
    }

  /* If there is a write inside the component, we must know whether the
     step is nonzero or not -- we would not otherwise be able to recognize
     whether the value accessed by reads comes from the OFFSET-th iteration
     or the previous one.  */
  if (has_write && comp->comp_step == RS_ANY)
    return false;

  return true;
}
      
/* Check the conditions on references inside each of components COMPS,
   and remove the unsuitable components from the list.  The new list
   of components is returned.  The conditions are described in 2) at
   the begining of this file.  LOOP is the current loop.  */

static struct component *
filter_suitable_components (struct loop *loop, struct component *comps)
{
  struct component **comp, *act;

  for (comp = &comps; *comp; )
    {
      act = *comp;
      if (suitable_component_p (loop, act))
	comp = &act->next;
      else
	{
	  *comp = act->next;
	  release_component (act);
	}
    }

  return comps;
}

/* Compares two drefs A and B by their offset and position.  Callback for
   qsort.  */

static int
order_drefs (const void *a, const void *b)
{
  const dref *da = a;
  const dref *db = b;
  int offcmp = double_int_scmp ((*da)->offset, (*db)->offset);

  if (offcmp != 0)
    return offcmp;

  return (*da)->pos - (*db)->pos;
}

/* Returns root of the CHAIN.  */

static inline dref
get_chain_root (chain_p chain)
{
  return VEC_index (dref, chain->refs, 0);
}

/* Adds REF to the chain CHAIN.  */

static void
add_ref_to_chain (chain_p chain, dref ref)
{
  dref root = get_chain_root (chain);
  double_int dist;

  gcc_assert (double_int_scmp (root->offset, ref->offset) <= 0);
  dist = double_int_add (ref->offset, double_int_neg (root->offset));
  if (double_int_ucmp (uhwi_to_double_int (MAX_DISTANCE), dist) <= 0)
    return;
  gcc_assert (double_int_fits_in_uhwi_p (dist));

  VEC_safe_push (dref, heap, chain->refs, ref);

  ref->distance = double_int_to_uhwi (dist);

  if (ref->distance >= chain->length)
    {
      chain->length = ref->distance;
      chain->has_max_use_after = false;
    }

  if (ref->distance == chain->length
      && ref->pos > root->pos)
    chain->has_max_use_after = true;

  chain->all_always_executed &= ref->always_executed;
}

/* Returns the chain for invariant component COMP.  */

static chain_p
make_invariant_chain (struct component *comp)
{
  chain_p chain = XCNEW (struct chain);
  unsigned i;
  dref ref;

  chain->type = CT_INVARIANT;

  chain->all_always_executed = true;

  for (i = 0; VEC_iterate (dref, comp->refs, i, ref); i++)
    {
      VEC_safe_push (dref, heap, chain->refs, ref);
      chain->all_always_executed &= ref->always_executed;
    }

  return chain;
}

/* Make a new chain rooted at REF.  */

static chain_p
make_rooted_chain (dref ref)
{
  chain_p chain = XCNEW (struct chain);

  chain->type = DR_IS_READ (ref->ref) ? CT_LOAD : CT_STORE_LOAD;

  VEC_safe_push (dref, heap, chain->refs, ref);
  chain->all_always_executed = ref->always_executed;

  ref->distance = 0;

  return chain;
}

/* Returns true if CHAIN is not trivial.  */

static bool
nontrivial_chain_p (chain_p chain)
{
  return chain != NULL && VEC_length (dref, chain->refs) > 1;
}

/* Find roots of the values and determine distances in the component COMP.
   The references are redistributed into CHAINS.  */

static void
determine_roots_comp (struct component *comp,
		      VEC (chain_p, heap) **chains)
{
  unsigned i;
  dref a;
  chain_p chain = NULL;

  /* Invariants are handled specially.  */
  if (comp->comp_step == RS_INVARIANT)
    {
      chain = make_invariant_chain (comp);
      VEC_safe_push (chain_p, heap, *chains, chain);
      return;
    }

  qsort (VEC_address (dref, comp->refs), VEC_length (dref, comp->refs),
	 sizeof (dref), order_drefs);

  for (i = 0; VEC_iterate (dref, comp->refs, i, a); i++)
    {
      if (!chain || !DR_IS_READ (a->ref))
	{
	  if (nontrivial_chain_p (chain))
	    VEC_safe_push (chain_p, heap, *chains, chain);
	  else
	    release_chain (chain);
	  chain = make_rooted_chain (a);
	  continue;
	}

      add_ref_to_chain (chain, a);
    }
	  
  if (nontrivial_chain_p (chain))
    VEC_safe_push (chain_p, heap, *chains, chain);
  else
    release_chain (chain);
}

/* Find roots of the values and determine distances in components COMPS, and
   separates the references to CHAINS.  */

static void
determine_roots (struct component *comps, VEC (chain_p, heap) **chains)
{
  struct component *comp;

  for (comp = comps; comp; comp = comp->next)
    determine_roots_comp (comp, chains);
}

/* Replace the reference in statement STMT with temporary variable
   NEW.  If SET is true, NEW is instead initialized to the value of
   the reference in the statement.  IN_LHS is true if the reference
   is in the lhs of STMT, false if it is in rhs.  */

static void
replace_ref_with (tree stmt, tree new, bool set, bool in_lhs)
{
  tree val, new_stmt;
  block_stmt_iterator bsi;

  /* Since the reference is of gimple_reg type, it should only
     appear as lhs or rhs of modify statement.  */
  gcc_assert (TREE_CODE (stmt) == MODIFY_EXPR);

  /* If we do not need to initialize NEW, just replace the use of OLD.  */
  if (!set)
    {
      gcc_assert (!in_lhs);
      TREE_OPERAND (stmt, 1) = new;
      update_stmt (stmt);
      return;
    }

  bsi = bsi_for_stmt (stmt);
  if (in_lhs)
    {
      val = TREE_OPERAND (stmt, 1);

      /* OLD = VAL

	 is transformed to

	 OLD = VAL
	 NEW = VAL

	 (since the reference is of gimple_reg type, VAL is either gimple
	 invariant or ssa name).  */
    }
  else
    {
      val = TREE_OPERAND (stmt, 0);

      /* VAL = OLD

	 is transformed to

	 VAL = OLD
	 NEW = VAL  */
    }

  new_stmt = build2 (MODIFY_EXPR, void_type_node, new, unshare_expr (val));
  bsi_insert_after (&bsi, new_stmt, BSI_NEW_STMT);
  SSA_NAME_DEF_STMT (new) = new_stmt;
}

/* Specializes REF for the ITER-th iteration (ITER may be negative).  */

static void
ref_at_iteration (struct loop *loop, tree ref, int iter)
{
  tree *idx, type, val;
  affine_iv iv;
  bool ok;

  if (handled_component_p (ref))
    ref_at_iteration (loop, TREE_OPERAND (ref, 0), iter);

  if (TREE_CODE (ref) == ARRAY_REF)
    idx = &TREE_OPERAND (ref, 1);
  else if (TREE_CODE (ref) == INDIRECT_REF)
    idx = &TREE_OPERAND (ref, 0);
  else
    return;

  ok = simple_iv (loop, first_stmt (loop->header), *idx, &iv, true);
  iv.base = expand_simple_operations (iv.base);
  gcc_assert (ok);
  if (zero_p (iv.step))
    {
      *idx = unshare_expr (iv.base);
      return;
    }

  type = TREE_TYPE (iv.base);
  val = fold_build2 (MULT_EXPR, type, iv.step,
		     build_int_cst_type (type, iter));
  val = fold_build2 (PLUS_EXPR, type, iv.base, val);
  *idx = unshare_expr (val);
}

/* Get the initialization expression for the INDEX-th temporary variable
   of CHAIN.  */

static tree
get_init_expr (chain_p chain, unsigned index)
{
  if (chain->type == CT_COMBINATION)
    {
      tree e1 = get_init_expr (chain->ch1, index);
      tree e2 = get_init_expr (chain->ch2, index);

      return fold_build2 (chain->operator, chain->rslt_type, e1, e2);
    }
  else
    return VEC_index (tree, chain->inits, index);
}

/* Creates the variables for CHAIN, as well as phi nodes for them and
   initialization on entry to LOOP.  Uids of the newly created
   temporary variables are marked in TMP_VARS.  */

static void
initialize_root_vars (struct loop *loop, chain_p chain, bitmap tmp_vars)
{
  unsigned i;
  unsigned n = chain->length;
  dref root = get_chain_root (chain);
  bool reuse_first = !chain->has_max_use_after;
  tree ref, init, var, next, stmts;
  tree phi;
  edge entry = loop_preheader_edge (loop), latch = loop_latch_edge (loop);

  /* If N == 0, then all the references are within the single iteration.  And
     since this is an nonempty chain, reuse_first cannot be true.  */
  gcc_assert (n > 0 || !reuse_first);

  chain->vars = VEC_alloc (tree, heap, n + 1);

  if (chain->type == CT_COMBINATION)
    ref = TREE_OPERAND (root->stmt, 0);
  else
    ref = DR_REF (root->ref);

  for (i = 0; i < n + (reuse_first ? 0 : 1); i++)
    {
      var = create_tmp_var (TREE_TYPE (ref), get_lsm_tmp_name (ref, i));
      add_referenced_var (var);
      bitmap_set_bit (tmp_vars, DECL_UID (var));
      VEC_quick_push (tree, chain->vars, var);
    }
  if (reuse_first)
    VEC_quick_push (tree, chain->vars, VEC_index (tree, chain->vars, 0));
  
  for (i = 0; VEC_iterate (tree, chain->vars, i, var); i++)
    VEC_replace (tree, chain->vars, i, make_ssa_name (var, NULL_TREE));

  for (i = 0; i < n; i++)
    {
      var = VEC_index (tree, chain->vars, i);
      next = VEC_index (tree, chain->vars, i + 1);
      init = get_init_expr (chain, i);

      init = force_gimple_operand (init, &stmts, true, NULL_TREE);
      if (stmts)
	bsi_insert_on_edge_immediate_loop (entry, stmts);

      phi = create_phi_node (var, loop->header);
      SSA_NAME_DEF_STMT (var) = phi;
      add_phi_arg (phi, init, entry);
      add_phi_arg (phi, next, latch);
    }
}

/* Create the variables and initialization statement for root of chain
   CHAIN.  Uids of the newly created temporary variables are marked
   in TMP_VARS.  */

static void
initialize_root (struct loop *loop, chain_p chain, bitmap tmp_vars)
{
  dref root = get_chain_root (chain);
  bool in_lhs = (chain->type == CT_STORE_LOAD
		 || chain->type == CT_COMBINATION);

  initialize_root_vars (loop, chain, tmp_vars);
  replace_ref_with (root->stmt,
		    VEC_index (tree, chain->vars, chain->length),
		    true, in_lhs);
}

/* Initializes a variable for load motion for ROOT and prepares phi nodes and
   initialization on entry to LOOP if necessary.  The ssa name for the variable
   is stored in VARS.  If WRITTEN is true, also a phi node to copy its value
   around the loop is created.  Uid of the newly created temporary variable
   is marked in TMP_VARS.  INITS is the list containing the (single)
   initializer.  */

static void
initialize_root_vars_lm (struct loop *loop, dref root, bool written,
			 VEC(tree, heap) **vars, VEC(tree, heap) *inits,
			 bitmap tmp_vars)
{
  unsigned i;
  tree ref = DR_REF (root->ref), init, var, next, stmts;
  tree phi;
  edge entry = loop_preheader_edge (loop), latch = loop_latch_edge (loop);

  /* Find the initializer for the variable, and check that it cannot
     trap.  */
  init = VEC_index (tree, inits, 0);

  *vars = VEC_alloc (tree, heap, written ? 2 : 1);
  var = create_tmp_var (TREE_TYPE (ref), get_lsm_tmp_name (ref, 0));
  add_referenced_var (var);
  bitmap_set_bit (tmp_vars, DECL_UID (var));
  VEC_quick_push (tree, *vars, var);
  if (written)
    VEC_quick_push (tree, *vars, VEC_index (tree, *vars, 0));
  
  for (i = 0; VEC_iterate (tree, *vars, i, var); i++)
    VEC_replace (tree, *vars, i, make_ssa_name (var, NULL_TREE));

  var = VEC_index (tree, *vars, 0);
      
  init = force_gimple_operand (init, &stmts, written, NULL_TREE);
  if (stmts)
    bsi_insert_on_edge_immediate_loop (entry, stmts);

  if (written)
    {
      next = VEC_index (tree, *vars, 1);
      phi = create_phi_node (var, loop->header);
      SSA_NAME_DEF_STMT (var) = phi;
      add_phi_arg (phi, init, entry);
      add_phi_arg (phi, next, latch);
    }
  else
    {
      init = build2 (MODIFY_EXPR, void_type_node, var, init);
      SSA_NAME_DEF_STMT (var) = init;
      bsi_insert_on_edge_immediate_loop (entry, init);
    }
}

/* Marks all virtual operands of statement STMT for renaming.  */

static void
mark_virtual_ops_for_renaming (tree stmt)
{
  ssa_op_iter iter;
  tree var;

  FOR_EACH_SSA_TREE_OPERAND (var, stmt, iter, SSA_OP_ALL_VIRTUALS)
    {
      mark_sym_for_renaming (SSA_NAME_VAR (var));
    }
}

/* Execute load motion for references in chain CHAIN.  Uids of the newly
   created temporary variables are marked in TMP_VARS.  */

static void
execute_load_motion (struct loop *loop, chain_p chain, bitmap tmp_vars)
{
  VEC (tree, heap) *vars;
  dref a;
  unsigned n_writes = 0, ridx, i;
  tree var;

  gcc_assert (chain->type == CT_INVARIANT);
  gcc_assert (!chain->combined);
  for (i = 0; VEC_iterate (dref, chain->refs, i, a); i++)
    if (!DR_IS_READ (a->ref))
      n_writes++;
  
  /* If there are no reads in the loop, there is nothing to do.  */
  if (n_writes == VEC_length (dref, chain->refs))
    return;

  initialize_root_vars_lm (loop, get_chain_root (chain), n_writes > 0,
			   &vars, chain->inits, tmp_vars);

  ridx = 0;
  for (i = 0; VEC_iterate (dref, chain->refs, i, a); i++)
    {
      bool is_read = DR_IS_READ (a->ref);
      mark_virtual_ops_for_renaming (a->stmt);

      if (!DR_IS_READ (a->ref))
	{
	  n_writes--;
	  if (n_writes)
	    {
	      var = VEC_index (tree, vars, 0);
	      var = make_ssa_name (SSA_NAME_VAR (var), NULL_TREE);
	      VEC_replace (tree, vars, 0, var);
	    }
	  else
	    ridx = 1;
	}
	  
      replace_ref_with (a->stmt, VEC_index (tree, vars, ridx),
			!is_read, !is_read);
    }

  VEC_free (tree, heap, vars);
}

/* Remove statement STMT, as well as the chain of assignments in that it is
   used.  */

static void
remove_stmt (tree stmt)
{
  tree next, name;
  use_operand_p dummy;
  bool cont;

  while (1)
    {
      block_stmt_iterator bsi = bsi_for_stmt (stmt);

      name = TREE_OPERAND (stmt, 0);
      gcc_assert (TREE_CODE (name) == SSA_NAME);

      cont = single_imm_use (name, &dummy, &next);
      mark_virtual_ops_for_renaming (stmt);
      bsi_remove (&bsi, true);

      if (!cont
	  || TREE_CODE (next) != MODIFY_EXPR
	  || TREE_OPERAND (next, 1) != name)
	return;

      stmt = next;
    }
}

/* Perform the predictive commoning optimization for a chain CHAIN.
   Uids of the newly created temporary variables are marked in TMP_VARS.*/

static void
execute_pred_commoning_chain (struct loop *loop, chain_p chain,
			     bitmap tmp_vars)
{
  unsigned i;
  dref a, root;
  tree var;

  if (chain->combined)
    {
      /* For combined chains, just remove the statements that are used to
	 compute the values of the expression (except for the root one).  */
      for (i = 1; VEC_iterate (dref, chain->refs, i, a); i++)
	remove_stmt (a->stmt);
    }
  else
    {
      /* For non-combined chains, set up the variables that hold its value,
	 and replace the uses of the original references by these
	 variables.  */
      root = get_chain_root (chain);
      mark_virtual_ops_for_renaming (root->stmt);

      initialize_root (loop, chain, tmp_vars);
      for (i = 1; VEC_iterate (dref, chain->refs, i, a); i++)
	{
	  mark_virtual_ops_for_renaming (a->stmt);
	  var = VEC_index (tree, chain->vars, chain->length - a->distance);
	  replace_ref_with (a->stmt, var, false, false);
	}
    }
}

/* Determines the unroll factor necessary to remove as many temporary variable
   copies as possible.  CHAINS is the list of chains that will be
   optimized.  */

static unsigned
determine_unroll_factor (VEC (chain_p, heap) *chains)
{
  chain_p chain;
  unsigned factor = 1, af, nfactor, i;
  unsigned max = PARAM_VALUE (PARAM_MAX_UNROLL_TIMES);

  for (i = 0; VEC_iterate (chain_p, chains, i, chain); i++)
    {
      if (chain->type == CT_INVARIANT || chain->combined)
	continue;

      /* The best unroll factor for this chain is equal to the number of
	 temporary variables that we create for it.  */
      af = chain->length;
      if (chain->has_max_use_after)
	af++;

      nfactor = factor * af / gcd (factor, af);
      if (nfactor <= max)
	factor = nfactor;
    }

  return factor;
}

/* Perform the predictive commoning optimization for CHAINS.
   Uids of the newly created temporary variables are marked in TMP_VARS.  */

static void
execute_pred_commoning (struct loop *loop, VEC (chain_p, heap) *chains,
			bitmap tmp_vars)
{
  chain_p chain;
  unsigned i;

  for (i = 0; VEC_iterate (chain_p, chains, i, chain); i++)
    {
      if (chain->type == CT_INVARIANT)
	execute_load_motion (loop, chain, tmp_vars);
      else
	execute_pred_commoning_chain (loop, chain, tmp_vars);
    }
}

/* Returns true if we can and should unroll LOOP FACTOR times.  Number
   of iterations of the loop is returned in NITER.  */

static bool
should_unroll_loop_p (struct loop *loop, unsigned factor,
		      struct tree_niter_desc *niter)
{
  edge exit;

  if (factor == 1)
    return false;

  /* Check whether unrolling is possible.  We only want to unroll loops
     for that we are able to determine number of iterations.  We also
     want to split the extra iterations of the loop from its end,
     therefore we require that the loop has precisely one
     exit.  */

  exit = single_dom_exit (loop);
  if (!exit)
    return false;

  if (!number_of_iterations_exit (loop, exit, niter, false))
    return false;

  /* And of course, we must be able to duplicate the loop.  */
  if (!can_duplicate_loop_p (loop))
    return false;

  /* The final loop should be small enough.  */
  if (tree_num_loop_insns (loop) * factor
      > (unsigned) PARAM_VALUE (PARAM_MAX_UNROLLED_INSNS))
    return false;

  return true;
}

/* Base NAME and all the names in the chain of phi nodes that use it
   on variable VAR.  The phi nodes are recognized by being in the copies of
   the header of the LOOP.  */

static void
base_names_in_chain_on (struct loop *loop, tree name, tree var)
{
  tree stmt, phi;
  imm_use_iterator iter;
  edge e;

  SSA_NAME_VAR (name) = var;
  SSA_NAME_OCCURS_IN_ABNORMAL_PHI (name) = 1;

  while (1)
    {
      phi = NULL;
      FOR_EACH_IMM_USE_STMT (stmt, iter, name)
	{
	  if (TREE_CODE (stmt) == PHI_NODE
	      && flow_bb_inside_loop_p (loop, bb_for_stmt (stmt)))
	    {
	      phi = stmt;
	      BREAK_FROM_IMM_USE_STMT (iter);
	    }
	}
      if (!phi)
	return;

      if (bb_for_stmt (phi) == loop->header)
	e = loop_latch_edge (loop);
      else
	e = single_pred_edge (bb_for_stmt (stmt));

      name = PHI_RESULT (phi);
      SSA_NAME_VAR (name) = var;

      /* Prevent copy propagation for this SSA name, so that we keep using one
	 variable for it, and avoid copies in loop latch.  */
      SSA_NAME_OCCURS_IN_ABNORMAL_PHI (name) = 1;
    }
}

/* Given an unrolled LOOP after predictive commoning, remove the
   register copies arising from phi nodes by changing the base
   variables of SSA names.  TMP_VARS is the set of the temporary variables
   for those we want to perform this.  */

static void
eliminate_temp_copies (struct loop *loop, bitmap tmp_vars)
{
  edge e;
  tree phi, name, use, var, stmt;

  e = loop_latch_edge (loop);
  for (phi = phi_nodes (loop->header); phi; phi = PHI_CHAIN (phi))
    {
      name = PHI_RESULT (phi);
      var = SSA_NAME_VAR (name);
      if (!bitmap_bit_p (tmp_vars, DECL_UID (var)))
	continue;
      use = PHI_ARG_DEF_FROM_EDGE (phi, e);
      gcc_assert (TREE_CODE (use) == SSA_NAME);

      /* Base all the ssa names in the ud and du chain of NAME on VAR.  */
      stmt = SSA_NAME_DEF_STMT (use);
      while (TREE_CODE (stmt) == PHI_NODE)
	{
	  gcc_assert (single_pred_p (bb_for_stmt (stmt)));
	  use = PHI_ARG_DEF (stmt, 0);
	  stmt = SSA_NAME_DEF_STMT (use);
	}

      base_names_in_chain_on (loop, use, var);
    }
}

/* Returns true if CHAIN is suitable to be combined.  */

static bool
chain_can_be_combined_p (chain_p chain)
{
  return (!chain->combined
	  && (chain->type == CT_LOAD || chain->type == CT_COMBINATION));
}

/* Returns the modify statement that uses NAME.  Skips over assignment
   statements, NAME is replaced with the actual name used in the returned
   statement.  */

static tree
find_use_stmt (tree *name)
{
  tree stmt, rhs, lhs;
  use_operand_p dummy;

  /* Skip over assignments.  */
  while (1)
    {
      if (!single_imm_use (*name, &dummy, &stmt))
	return NULL_TREE;

      if (TREE_CODE (stmt) != MODIFY_EXPR)
	return NULL_TREE;

      lhs = TREE_OPERAND (stmt, 0);
      if (TREE_CODE (lhs) != SSA_NAME)
	return NULL_TREE;

      rhs = TREE_OPERAND (stmt, 1);
      if (rhs != *name)
	break;

      *name = lhs;
    }

  if (!EXPR_P (rhs)
      || REFERENCE_CLASS_P (rhs)
      || TREE_CODE_LENGTH (TREE_CODE (rhs)) != 2)
    return NULL_TREE;

  return stmt;
}

/* Returns true if we may perform reassociation for operation CODE in TYPE.  */

static bool
may_reassociate_p (tree type, enum tree_code code)
{
  if (FLOAT_TYPE_P (type)
      && !flag_unsafe_math_optimizations)
    return false;

  return (commutative_tree_code (code)
	  && associative_tree_code (code));
}

/* If the operation used in STMT is associative and commutative, go through the
   tree of the same operations and returns its root.  Distance to the root
   is stored in DISTANCE.  */

static tree
find_associative_operation_root (tree stmt, unsigned *distance)
{
  tree rhs = TREE_OPERAND (stmt, 1), lhs, next;
  enum tree_code code = TREE_CODE (rhs);
  unsigned dist = 0;

  if (!may_reassociate_p (TREE_TYPE (rhs), code))
    return NULL_TREE;

  while (1)
    {
      lhs = TREE_OPERAND (stmt, 0);
      gcc_assert (TREE_CODE (lhs) == SSA_NAME);

      next = find_use_stmt (&lhs);
      if (!next)
	break;

      rhs = TREE_OPERAND (next, 1);
      if (TREE_CODE (rhs) != code)
	break;

      stmt = next;
      dist++;
    }

  if (distance)
    *distance = dist;
  return stmt;
}

/* Returns the common statement in that NAME1 and NAME2 have a use.  If there
   is no such statement, returns NULL_TREE.  In case the operation used on
   NAME1 and NAME2 is associative and comutative, returns the root of the
   tree formed by this operation instead of the statement that uses NAME1 or
   NAME2.  */

static tree
find_common_use_stmt (tree *name1, tree *name2)
{
  tree stmt1, stmt2;

  stmt1 = find_use_stmt (name1);
  if (!stmt1)
    return NULL_TREE;

  stmt2 = find_use_stmt (name2);
  if (!stmt2)
    return NULL_TREE;

  if (stmt1 == stmt2)
    return stmt1;

  stmt1 = find_associative_operation_root (stmt1, NULL);
  if (!stmt1)
    return NULL_TREE;
  stmt2 = find_associative_operation_root (stmt2, NULL);
  if (!stmt2)
    return NULL_TREE;

  return (stmt1 == stmt2 ? stmt1 : NULL_TREE);
}

/* Checks whether R1 and R2 are combined together using CODE, with the result
   in RSLT_TYPE, in order R1 CODE R2 if SWAP is false and in order R2 CODE R1
   if it is true.  If CODE is ERROR_MARK, set these values instead.  */

static bool
combinable_refs_p (dref r1, dref r2,
		   enum tree_code *code, bool *swap, tree *rslt_type)
{
  enum tree_code acode;
  bool aswap;
  tree atype;
  tree name1, name2, stmt, rhs;

  gcc_assert (TREE_CODE (r1->stmt) == MODIFY_EXPR);
  gcc_assert (TREE_CODE (r2->stmt) == MODIFY_EXPR);
  name1 = TREE_OPERAND (r1->stmt, 0);
  name2 = TREE_OPERAND (r2->stmt, 0);

  gcc_assert (TREE_CODE (name1) == SSA_NAME);
  gcc_assert (TREE_CODE (name2) == SSA_NAME);

  stmt = find_common_use_stmt (&name1, &name2);

  if (!stmt)
    return false;

  rhs = TREE_OPERAND (stmt, 1);
  acode = TREE_CODE (rhs);
  aswap = (!commutative_tree_code (acode)
	   && TREE_OPERAND (rhs, 0) != name1);
  atype = TREE_TYPE (rhs);

  if (*code == ERROR_MARK)
    {
      *code = acode;
      *swap = aswap;
      *rslt_type = atype;
      return true;
    }

  return (*code == acode
	  && *swap == aswap
	  && *rslt_type == atype);
}

/* Remove OP from the operation on rhs of STMT, and replace STMT with
   an assignment of the remaining operand.  */

static void
remove_name_from_operation (tree stmt, tree op)
{
  tree *rhs;

  gcc_assert (TREE_CODE (stmt) == MODIFY_EXPR);

  rhs = &TREE_OPERAND (stmt, 1);
  if (TREE_OPERAND (*rhs, 0) == op)
    *rhs = TREE_OPERAND (*rhs, 1);
  else if (TREE_OPERAND (*rhs, 1) == op)
    *rhs = TREE_OPERAND (*rhs, 0);
  else
    gcc_unreachable ();
  update_stmt (stmt);
}

/* Reassociates the expression in that NAME1 and NAME2 are used so that they
   are combined in a single statement, and returns this statement.  */

static tree
reassociate_to_the_same_stmt (tree name1, tree name2)
{
  tree stmt1, stmt2, root1, root2, r1, r2, s1, s2;
  tree new_stmt, tmp_stmt, new_name, tmp_name, var;
  unsigned dist1, dist2;
  enum tree_code code;
  tree type = TREE_TYPE (name1);
  block_stmt_iterator bsi;

  stmt1 = find_use_stmt (&name1);
  stmt2 = find_use_stmt (&name2);
  root1 = find_associative_operation_root (stmt1, &dist1);
  root2 = find_associative_operation_root (stmt2, &dist2);
  code = TREE_CODE (TREE_OPERAND (stmt1, 1));

  gcc_assert (root1 && root2 && root1 == root2
	      && code == TREE_CODE (TREE_OPERAND (stmt2, 1)));

  /* Find the root of the nearest expression in that both NAME1 and NAME2
     are used.  */
  r1 = name1;
  s1 = stmt1;
  r2 = name2;
  s2 = stmt2;

  while (dist1 > dist2)
    {
      s1 = find_use_stmt (&r1);
      r1 = TREE_OPERAND (s1, 0);
      dist1--;
    }
  while (dist2 > dist1)
    {
      s2 = find_use_stmt (&r2);
      r2 = TREE_OPERAND (s2, 0);
      dist2--;
    }

  while (s1 != s2)
    {
      s1 = find_use_stmt (&r1);
      r1 = TREE_OPERAND (s1, 0);
      s2 = find_use_stmt (&r2);
      r2 = TREE_OPERAND (s2, 0);
    }

  /* Remove NAME1 and NAME2 from the statements in that they are used
     currently.  */
  remove_name_from_operation (stmt1, name1);
  remove_name_from_operation (stmt2, name2);

  /* Insert the new statement combining NAME1 and NAME2 before S1, and
     combine it with the rhs of S1.  */
  var = create_tmp_var (type, "predreastmp");
  add_referenced_var (var);
  new_name = make_ssa_name (var, NULL_TREE);
  new_stmt = fold_build2 (MODIFY_EXPR, void_type_node, new_name,
			  fold_build2 (code, type, name1, name2));
  SSA_NAME_DEF_STMT (new_name) = new_stmt;

  var = create_tmp_var (type, "predreastmp");
  add_referenced_var (var);
  tmp_name = make_ssa_name (var, NULL_TREE);
  tmp_stmt = fold_build2 (MODIFY_EXPR, void_type_node, tmp_name,
			  TREE_OPERAND (s1, 1));
  SSA_NAME_DEF_STMT (tmp_name) = tmp_stmt;

  TREE_OPERAND (s1, 1) = fold_build2 (code, type, new_name, tmp_name);
  update_stmt (s1);

  bsi = bsi_for_stmt (s1);
  bsi_insert_before (&bsi, new_stmt, BSI_SAME_STMT);
  bsi_insert_before (&bsi, tmp_stmt, BSI_SAME_STMT);

  return new_stmt;
}

/* Returns the statement that combines references R1 and R2.  In case R1
   and R2 are not used in the same statement, but they are used with an
   associative and commutative operation in the same expression, reassociate
   the expression so that they are used in the same statement.  */

static tree
stmt_combining_refs (dref r1, dref r2)
{
  tree stmt1, stmt2;
  tree name1 = TREE_OPERAND (r1->stmt, 0);
  tree name2 = TREE_OPERAND (r2->stmt, 0);

  stmt1 = find_use_stmt (&name1);
  stmt2 = find_use_stmt (&name2);
  if (stmt1 == stmt2)
    return stmt1;

  return reassociate_to_the_same_stmt (name1, name2);
}

/* Tries to combine chains CH1 and CH2 together.  If this succeeds, the
   description of the new chain is returned, otherwise we return NULL.  */

static chain_p
combine_chains (chain_p ch1, chain_p ch2)
{
  dref r1, r2, nw;
  enum tree_code op = ERROR_MARK;
  bool swap = false;
  chain_p new_chain;
  unsigned i;
  tree root_stmt;
  tree rslt_type = NULL_TREE;

  if (ch1 == ch2)
    return false;
  if (ch1->length != ch2->length)
    return NULL;

  if (VEC_length (dref, ch1->refs) != VEC_length (dref, ch2->refs))
    return NULL;

  for (i = 0; (VEC_iterate (dref, ch1->refs, i, r1)
	       && VEC_iterate (dref, ch2->refs, i, r2)); i++)
    {
      if (r1->distance != r2->distance)
	return NULL;

      if (!combinable_refs_p (r1, r2, &op, &swap, &rslt_type))
	return NULL;
    }

  if (swap)
    {
      chain_p tmp = ch1;
      ch1 = ch2;
      ch2 = tmp;
    }

  new_chain = XCNEW (struct chain);
  new_chain->type = CT_COMBINATION;
  new_chain->operator = op;
  new_chain->ch1 = ch1;
  new_chain->ch2 = ch2;
  new_chain->rslt_type = rslt_type;
  new_chain->length = ch1->length;

  for (i = 0; (VEC_iterate (dref, ch1->refs, i, r1)
	       && VEC_iterate (dref, ch2->refs, i, r2)); i++)
    {
      nw = XCNEW (struct dref);
      nw->stmt = stmt_combining_refs (r1, r2);
      nw->distance = r1->distance;

      VEC_safe_push (dref, heap, new_chain->refs, nw);
    }

  new_chain->has_max_use_after = false;
  root_stmt = get_chain_root (new_chain)->stmt;
  for (i = 1; VEC_iterate (dref, new_chain->refs, i, nw); i++)
    {
      if (nw->distance == new_chain->length
	  && !stmt_dominates_stmt_p (nw->stmt, root_stmt))
	{
	  new_chain->has_max_use_after = true;
	  break;
	}
    }

  ch1->combined = true;
  ch2->combined = true;
  return new_chain;
}

/* Try to combine the CHAINS.  */

static void
try_combine_chains (VEC (chain_p, heap) **chains)
{
  unsigned i, j;
  chain_p ch1, ch2, cch;
  VEC (chain_p, heap) *worklist = NULL;

  for (i = 0; VEC_iterate (chain_p, *chains, i, ch1); i++)
    if (chain_can_be_combined_p (ch1))
      VEC_safe_push (chain_p, heap, worklist, ch1);

  while (!VEC_empty (chain_p, worklist))
    {
      ch1 = VEC_pop (chain_p, worklist);
      if (!chain_can_be_combined_p (ch1))
	continue;

      for (j = 0; VEC_iterate (chain_p, *chains, j, ch2); j++)
	{
	  if (!chain_can_be_combined_p (ch2))
	    continue;

	  cch = combine_chains (ch1, ch2);
	  if (cch)
	    {
	      VEC_safe_push (chain_p, heap, worklist, cch);
	      VEC_safe_push (chain_p, heap, *chains, cch);
	      break;
	    }
	}
    }
}

/* Prepare initializers for CHAIN in LOOP.  Returns false if this is
   impossible because one of these initializers may trap, true otherwise.  */

static bool
prepare_initializers_chain (struct loop *loop, chain_p chain)
{
  unsigned i, n = (chain->type == CT_INVARIANT) ? 1 : chain->length;
  tree init, ref = DR_REF (get_chain_root (chain)->ref);

  /* Find the initializers for the variables, and check that they cannot
     trap.  */
  chain->inits = VEC_alloc (tree, heap, n);

  for (i = 0; i < n; i++)
    {
      init = unshare_expr (ref);
      ref_at_iteration (loop, init, (int) i - n);
      if (!chain->all_always_executed && tree_could_trap_p (init))
	return false;
      VEC_quick_push (tree, chain->inits, init);
    }

  return true;
}

/* Prepare initializers for CHAINS in LOOP, and free chains that cannot
   be used because the initializers might trap.  */

static void
prepare_initializers (struct loop *loop, VEC (chain_p, heap) *chains)
{
  chain_p chain;
  unsigned i;

  for (i = 0; i < VEC_length (chain_p, chains); )
    {
      chain = VEC_index (chain_p, chains, i);
      if (prepare_initializers_chain (loop, chain))
	i++;
      else
	{
	  release_chain (chain);
	  VEC_unordered_remove (chain_p, chains, i);
	}
    }
}

/* Performs predictive commoning for LOOP.  Returns true if LOOP was
   unrolled.  */

static bool
tree_predictive_commoning_loop (struct loops *loops, struct loop *loop)
{
  VEC (data_reference_p, heap) *datarefs;
  VEC (ddr_p, heap) *dependences;
  struct component *components;
  VEC (chain_p, heap) *chains = NULL;
  unsigned unroll_factor;
  struct tree_niter_desc desc;
  bool unroll;
  edge exit;
  bitmap tmp_vars;

  /* Find the data references and split them into components according to their
     dependence relations.  */
  datarefs = VEC_alloc (data_reference_p, heap, 10);
  dependences = VEC_alloc (ddr_p, heap, 10);
  compute_data_dependences_for_loop (loop, true, &datarefs, &dependences);
  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_data_dependence_relations (dump_file, dependences);

  components = split_data_refs_to_components (loop, datarefs, dependences);
  free_dependence_relations (dependences);
  if (!components)
    {
      free_data_refs (datarefs);
      return false;
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Initial state:\n\n");
      dump_components (dump_file, components);
    }

  /* Find the suitable components and split them into chains.  */
  components = filter_suitable_components (loop, components);

  tmp_vars = BITMAP_ALLOC (NULL);
  determine_roots (components, &chains);
  release_components (components);
  prepare_initializers (loop, chains);

  /* Try to combine the chains that are always worked with together.  */
  try_combine_chains (&chains);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Before commoning:\n\n");
      dump_chains (dump_file, chains);
    }

  /* Determine the unroll factor, and if the loop should be unrolled, ensure
     that its number of iterations is divisible by the factor.  */
  unroll_factor = determine_unroll_factor (chains);
  scev_reset ();
  unroll = should_unroll_loop_p (loop, unroll_factor, &desc);
  exit = single_dom_exit (loop);

  if (unroll)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unrolling %u times.\n", unroll_factor);
      tree_unroll_loop_prepare (loops, loop, unroll_factor, &exit, &desc);
    }

  /* Execute the predictive commoning transformations, and possibly unroll the
     loop.  */
  execute_pred_commoning (loop, chains, tmp_vars);
  if (unroll)
    {
      update_ssa (TODO_update_ssa_only_virtuals);
      tree_unroll_loop_finish (loops, loop, unroll_factor, exit);
      eliminate_temp_copies (loop, tmp_vars);
    }

  release_chains (chains);
  free_data_refs (datarefs);
  BITMAP_FREE (tmp_vars);
  return unroll;
}

/* Runs predictive commoning over LOOPS.  */

void
tree_predictive_commoning (struct loops *loops)
{
  bool unrolled = false;
  struct loop *loop;
  unsigned i, n = loops->num;

  initialize_original_copy_tables ();
  for (i = 1; i < n; i++)
    {
      loop = loops->parray[i];
      if (loop && !loop->inner)
	unrolled |= tree_predictive_commoning_loop (loops, loop);
    }

  if (unrolled)
    {
      scev_reset ();
      cleanup_tree_cfg_loop ();
    }
  free_original_copy_tables ();
}
