/* Loop Vectorization
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Contributed by Dorit Naishlos <dorit@il.ibm.com>

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

/* Loop Vectorization Pass.

   This pass tries to vectorize loops. This first implementation focuses on
   simple inner-most loops, with no conditional control flow, and a set of
   simple operations which vector form can be expressed using existing
   tree codes (PLUS, MULT etc).

   For example, the vectorizer transforms the following simple loop:

	short a[N]; short b[N]; short c[N]; int i;

	for (i=0; i<N; i++){
	  a[i] = b[i] + c[i];
	}

   as if it was manually vectorized by rewriting the source code into:

	typedef int __attribute__((mode(V8HI))) v8hi;
	short a[N];  short b[N]; short c[N];   int i;
	v8hi *pa = (v8hi*)a, *pb = (v8hi*)b, *pc = (v8hi*)c;
	v8hi va, vb, vc;

	for (i=0; i<N/8; i++){
	  vb = pb[i];
	  vc = pc[i];
	  va = vb + vc;
	  pa[i] = va;
	}

	The main entry to this pass is vectorize_loops(), in which for each
   the vectorizer applies a set of analyses on a given set of loops,
   followed by the actual vectorization transformation for the loops that
   had successfully passed the analysis phase.

	Throughout this pass we make a distinction between two types of
   data: scalars (which are represented by SSA_NAMES), and data-refs. These
   are handled separately both by the analyzer and the loop-transformer.
   Currently, the vectorizer only supports simple data-refs which are
   limited to ARRAY_REFS that represent one dimensional arrays which base is
   an array (not a pointer), and have a simple (consecutive) access pattern.

   Analysis phase:
   ===============
	The driver for the analysis phase is vect_analyze_loop_nest().
   which applies a set of loop analyses. Some of the analyses rely on the
   monotonic evolution analyzer developed by Sebastian Pop.

	During the analysis phase the vectorizer records some information
   per stmt in a stmt_vec_info which is attached to each stmt in the loop,
   as well as general information about the loop as a whole, which is
   recorded in a loop_vec_info struct attached to each loop.

   Transformation phase:
   =====================
	The loop transformation phase scans all the stmts in the loop, and
   creates a vector stmt (or a sequence of stmts) for each scalar stmt S in
   the loop that needs to be vectorized. It insert the vector code sequence
   just before the scalar stmt S, and records a pointer to the vector code
   in STMT_VINFO_VEC_STMT (stmt_info) (where stmt_info is the stmt_vec_info
   struct that is attached to S). This pointer is used for the vectorization
   of following stmts which use the defs of stmt S. Stmt S is removed
   only if it has side effects (like changing memory). If stmt S does not
   have side effects, we currently rely on dead code elimination for
   removing it.

	For example, say stmt S1 was vectorized into stmt VS1:

   VS1: vb = px[i];
   S1:	b = x[i];    STMT_VINFO_VEC_STMT (stmt_info (S1)) = VS1
   S2:  a = b;

   To vectorize stmt S2, the vectorizer first finds the stmt that defines
   the operand 'b' (S1), and gets the relevant vector def 'vb' from the
   vector stmt VS1 pointed by STMT_VINFO_VEC_STMT (stmt_info (S1)). The
   resulting sequence would be:

   VS1: vb = px[i];
   S1:	b = x[i];	STMT_VINFO_VEC_STMT (stmt_info (S1)) = VS1
   VS2: va = vb;
   S2:  a = b;          STMT_VINFO_VEC_STMT (stmt_info (S2)) = VS2

	Operands that are not SSA_NAMEs, are currently limited to array
   references appearing in load/store operations (like 'x[i]' in S1), and
   are handled differently.

   Target modelling:
   =================
	Currently the only target specific information that is used is the
   size of the vector (in bytes) - "UNITS_PER_SIMD_WORD", and a target hook
   "vectype_for_scalar_type" that for a given (scalar) machine mode returns
   the vector machine_mode to be used. Targets that can support different
   sizes of vectors, for now will need to specify one value for
   "UNITS_PER_SIMD_WORD". More flexibility will be added in the future.

	Since we only vectorize operations which vector form can be
   expressed using existing tree codes, to verify that an operation is
   supported the vectorizer checks the relevant optab at the relevant
   machine_mode (e.g, add_optab->handlers[(int) V8HImode].insn_code).  If
   the value found is CODE_FOR_nothing, then there's no target support, and
   we can't vectorize the stmt. Otherwise - the stmt is transformed.


   For additional information on this project see:
   http://gcc.gnu.org/projects/tree-ssa/vectorization.html
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "target.h"

#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "cfglayout.h"
#include "tree-fold-const.h"
#include "expr.h"
#include "optabs.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-vectorizer.h"
#include "tree-pass.h"

/* CHECKME: check for unnecessary include files.  */

/* Main analysis functions.  */
static loop_vec_info vect_analyze_loop (struct loop *);
static loop_vec_info vect_analyze_loop_form (struct loop *);
static bool vect_analyze_data_refs (loop_vec_info);
static bool vect_mark_stmts_to_be_vectorized (loop_vec_info);
static bool vect_analyze_scalar_cycles (loop_vec_info);
static bool vect_analyze_data_ref_dependences (loop_vec_info);
static bool vect_analyze_data_ref_accesses (loop_vec_info);
static bool vect_analyze_data_refs_alignment (loop_vec_info);
static void vect_compute_data_refs_alignment (loop_vec_info);
static bool vect_analyze_operations (loop_vec_info);

/* Main code transformation functions.  */
static void vect_transform_loop (loop_vec_info, struct loops *);
static void vect_transform_loop_bound (loop_vec_info, tree);
static bool vect_transform_stmt (tree, block_stmt_iterator *);
static tree vect_transform_load (tree, block_stmt_iterator *);
static tree vect_transform_store (tree, block_stmt_iterator *);
static tree vect_transform_op (tree, block_stmt_iterator *);
static tree vect_transform_assignment (tree, block_stmt_iterator *);
static void vect_align_data_ref (tree, tree);
static void vect_enhance_data_refs_alignment (loop_vec_info);

/* Utility functions for the analyses.  */
static bool vect_is_supportable_op (tree);
static bool vect_is_supportable_store (tree);
static bool vect_is_supportable_load (tree);
static bool vect_is_supportable_assignment (tree);
static bool vect_is_simple_use (tree , struct loop *);
static bool exist_non_indexing_operands_for_use_p (tree, tree);
static bool vect_is_simple_iv_evolution (unsigned, tree, tree *, tree *, bool);
static void vect_mark_relevant (varray_type, tree);
static bool vect_stmt_relevant_p (tree, loop_vec_info);
static tree vect_get_loop_niters (struct loop *, int *);
static void vect_compute_data_ref_alignment 
  (struct data_reference *, loop_vec_info);
static bool vect_analyze_data_ref_access (struct data_reference *);
static bool vect_analyze_data_ref_dependence
  (struct data_reference *, struct data_reference *);
static bool vect_get_array_first_index (tree, int *);
static bool vect_force_dr_alignment_p (struct data_reference *);
static bool vect_analyze_loop_with_symbolic_num_of_iters (tree *, struct loop *);

/* Utility functions for the code transformation.  */
static tree vect_create_destination_var (tree, tree);
static tree vect_create_data_ref (tree, tree, block_stmt_iterator *);
static tree vect_create_index_for_array_ref (tree, block_stmt_iterator *);
static tree get_vectype_for_scalar_type (tree);
static tree vect_get_new_vect_var (tree, enum vect_var_kind, const char *);
static tree vect_get_vec_def_for_operand (tree, tree);
static tree vect_init_vector (tree, tree);

/* Utility functions for loop duplication.  */
static bool vect_duplicate_loop (struct loop *loop, struct loops *loops, struct loop **new_loop_p);
static void vect_copy_phi_nodes (struct loop *loop, struct loop *new_loop);
static void vect_rename_variables_in_loop (struct loop *loop);
static basic_block vect_tree_split_edge (edge);
static void vect_update_initial_conditions_of_duplicated_loop (loop_vec_info, tree);
static bool vect_tree_duplicate_loop (struct loop *loop, struct loops *loops);

/* General untility functions (CHECKME: where do they belong).  */
static tree get_array_base (tree);

/* Utilities for creation and deletion of vec_info structs.  */
loop_vec_info new_loop_vec_info (struct loop *loop);
void destroy_loop_vec_info (loop_vec_info);
stmt_vec_info new_stmt_vec_info (tree stmt, struct loop *loop);

/* Define number of arguments for each tree code.  */

#define DEFTREECODE(SYM, STRING, TYPE, NARGS)   NARGS,

int tree_nargs[] = {
#include "tree.def"

};

#undef DEFTREECODE

/* Function new_stmt_vec_info.

   Create and initialize a new stmt_vec_info struct for STMT.  */

stmt_vec_info
new_stmt_vec_info (tree stmt, struct loop *loop)
{
  stmt_vec_info res;
  res = (stmt_vec_info) xcalloc (1, sizeof (struct _stmt_vec_info));

  STMT_VINFO_TYPE (res) = undef_vec_info_type;
  STMT_VINFO_STMT (res) = stmt;
  STMT_VINFO_LOOP (res) = loop;
  STMT_VINFO_RELEVANT_P (res) = 0;
  STMT_VINFO_VECTYPE (res) = NULL;
  STMT_VINFO_VEC_STMT (res) = NULL;
  STMT_VINFO_DATA_REF (res) = NULL;

  return res;
}


/* Function new_loop_vec_info.

   Create and initialize a new loop_vec_info struct for LOOP, as well as
   stmt_vec_info structs for all the stmts in LOOP.  */

loop_vec_info
new_loop_vec_info (struct loop *loop)
{
  loop_vec_info res;
  basic_block *bbs;
  block_stmt_iterator si;
  unsigned int i;

  res = (loop_vec_info) xcalloc (1, sizeof (struct _loop_vec_info));

  bbs = get_loop_body (loop);

  /* Create stmt_info for all stmts in the loop.  */
  for (i = 0; i < loop->num_nodes; i++)
    {
      basic_block bb = bbs[i];
      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
	{
	  tree stmt = bsi_stmt (si);
	  stmt_ann_t ann;

	  get_stmt_operands (stmt);
	  ann = stmt_ann (stmt);
	  set_stmt_info (ann, new_stmt_vec_info (stmt, loop));
	}
    }

  LOOP_VINFO_LOOP (res) = loop;
  LOOP_VINFO_BBS (res) = bbs;
  LOOP_VINFO_EXIT_COND (res) = NULL;
  LOOP_VINFO_NITERS (res) = -1;
  LOOP_VINFO_VECTORIZABLE_P (res) = 0;
  LOOP_VINFO_VECT_FACTOR (res) = 0;
  LOOP_VINFO_SYMB_NUM_OF_ITERS (res) = NULL;
  VARRAY_GENERIC_PTR_INIT (LOOP_VINFO_DATAREF_WRITES (res), 20,
			   "loop_write_datarefs");
  VARRAY_GENERIC_PTR_INIT (LOOP_VINFO_DATAREF_READS (res), 20,
			   "loop_read_datarefs");
  return res;
}


/* Function destroy_loop_vec_info.  */

void
destroy_loop_vec_info (loop_vec_info loop_vinfo)
{
  struct loop *loop;
  basic_block *bbs;
  int nbbs;
  block_stmt_iterator si;
  int j;

  if (!loop_vinfo)
    return;

  loop = LOOP_VINFO_LOOP (loop_vinfo);

  bbs = LOOP_VINFO_BBS (loop_vinfo);
  nbbs = loop->num_nodes;

  for (j = 0; j < nbbs; j++)
    {
      basic_block bb = bbs[j];
      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
	{
	  tree stmt = bsi_stmt (si);
	  stmt_ann_t ann = stmt_ann (stmt);
	  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
	  free (stmt_info);
	  set_stmt_info (ann, NULL);
	}
    }

  free (LOOP_VINFO_BBS (loop_vinfo));
  varray_clear (LOOP_VINFO_DATAREF_WRITES (loop_vinfo));
  varray_clear (LOOP_VINFO_DATAREF_READS (loop_vinfo));

  free (loop_vinfo);
}


/* Function vect_force_dr_alignment_p

   Returned whether the alignment of a certain data structure can be forced. */

static bool
vect_force_dr_alignment_p (struct data_reference *dr)
{
  tree ref = DR_REF (dr);
  tree array_base;

  if (TREE_CODE (ref) != ARRAY_REF)
    return false;

  array_base = get_array_base (ref);

  /* We want to make sure that we can force alignment of
     the data structure that is being accessed, because we do not
     handle misalignment yet.

     CHECKME: Is this a correct check for this purpose?
     CHECKME: This is a very strict check.
     CHECKME: Can we force the alignment of external decls?
   */

  if (TREE_CODE (TREE_TYPE (array_base)) != ARRAY_TYPE
      || TREE_CODE (array_base) != VAR_DECL
      || DECL_EXTERNAL (array_base))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        {
          fprintf (dump_file, "unhandled ptr-based array ref\n");
          if (TREE_CODE (array_base) == VAR_DECL && DECL_EXTERNAL (array_base))
            fprintf (dump_file,"\nextern decl.\n");
        }
      return false;
    }

  return true;
}


/* Function vect_get_new_vect_var.

   Return a name for a new variable.
   The current naming scheme appends the prefix "vect_" or "vect_p" to 
   vectorizer generated variables, and appends that to NAME if given. 

   CHECKME: naming scheme ok?  */

static tree
vect_get_new_vect_var (tree type, enum vect_var_kind var_kind, const char *name)
{
  const char *prefix;
  int prefix_len;
  char *vect_var_name;
  tree new_vect_var;

  if (var_kind == vect_simple_var)
    prefix = "vect_"; 
  else
    prefix = "vect_p";

  prefix_len = strlen (prefix);

  if (name)
    {
      vect_var_name = (char *) xmalloc (strlen (name) + prefix_len + 1);
      sprintf (vect_var_name, "%s%s", prefix, name);
    }
  else
    {
      vect_var_name = (char *) xmalloc (prefix_len + 1);
      sprintf (vect_var_name, "%s", prefix);
    }

  new_vect_var = create_tmp_var (type, vect_var_name);

  free (vect_var_name);
  return new_vect_var;
}


/* Function create_index_for_array_ref.

   Create an offset/index to be used to access a memory location.
   Input:

   STMT: The stmt that contains a data reference to the memory location.

   BSI: The block_stmt_iterator where STMT is. Any new stmts created by this
        function can be added here, or in the loop pre-header.

   Output:

   Return an index that will be used to index an array, using a pointer as 
   a base.

   FORNOW: we are not trying to be efficient, and just creating the code
   sequence each time from scratch, even if the same offset can be reused.
   TODO: record the index in the array_ref_info or the stmt info and reuse
   it.

   FORNOW: We are only handling array accesses with step 1.  */

static tree
vect_create_index_for_array_ref (tree stmt, block_stmt_iterator *bsi)
{
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  struct loop *loop = STMT_VINFO_LOOP (stmt_info);
  struct data_reference *dr = STMT_VINFO_DATA_REF (stmt_info);
  tree expr = DR_REF (dr);
  varray_type access_fns = DR_ACCESS_FNS (dr);
  tree access_fn;
  tree scalar_indx; 
  int init_val, step_val;
  tree init, step;
  loop_vec_info loop_info = loop->aux;
  int vectorization_factor = LOOP_VINFO_VECT_FACTOR (loop_info);
  bool ok;
  int array_first_index;
  int vec_init_val;
  tree indx_before_incr, indx_after_incr;

  if (TREE_CODE (expr) != ARRAY_REF)
    abort ();

  /* FORNOW: handle only one dimensional arrays.
     This restriction will be relaxed in the future.  */ /* CHECKME */
  if (VARRAY_ACTIVE_SIZE (access_fns) != 1)
    abort ();

  access_fn = DR_ACCESS_FN (dr, 0);

  if (!vect_is_simple_iv_evolution (loop_num (loop), access_fn, &init, &step, 
	true))
    abort ();

  if (TREE_CODE (init) != INTEGER_CST || TREE_CODE (step) != INTEGER_CST)
    abort ();	

  if (TREE_INT_CST_HIGH (init) != 0 || TREE_INT_CST_HIGH (step) != 0)
    abort ();

  init_val = TREE_INT_CST_LOW (init); 
  step_val = TREE_INT_CST_LOW (step);


  /** Handle initialization.  **/

  scalar_indx =  TREE_OPERAND (expr, 1); 

  /* The actual index depends on the (mis)alignment of the access.
     FORNOW: we verify that both the array base and the access are
     aligned, so the index in the vectorized access is simply
     init_val/vectorization_factor.  */

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "creating update chain:\n");

  ok = vect_get_array_first_index (expr, &array_first_index);
  if (!ok)
    abort ();
  vec_init_val = array_first_index +
	(init_val - array_first_index)/vectorization_factor;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "vec_init_indx = %d\n", vec_init_val);

  init = build_int_2 (vec_init_val, 0);
  step = integer_one_node;

  /* CHECKME: assuming that bsi_insert is used with BSI_NEW_STMT */

  create_iv (init, step, NULL_TREE, loop, bsi, false, 
	&indx_before_incr, &indx_after_incr); 

  return indx_before_incr;
}


/* Function get_vectype_for_scalar_type.

   Return the vector type corresponding to SCALAR_TYPE as supported
   by the target.  */

static tree
get_vectype_for_scalar_type (tree scalar_type)
{
  enum machine_mode inner_mode;
  enum machine_mode vec_mode;
  int nbytes;
  int nunits;

  /* FORNOW: Only a single vector size per target is expected.  */

  inner_mode = TYPE_MODE (scalar_type);
  nbytes = GET_MODE_SIZE (inner_mode);

  if (nbytes == 0)
    return NULL_TREE;

  nunits = UNITS_PER_SIMD_WORD / nbytes;

  if (GET_MODE_CLASS (inner_mode) == MODE_FLOAT)
    vec_mode = MIN_MODE_VECTOR_FLOAT;
  else
    vec_mode = MIN_MODE_VECTOR_INT;

  /* CHECKME: This duplicates some of the functionality in build_vector_type;
     could have directly called build_vector_type_for_mode if exposed.  */

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nget vectype for scalar type:  ");
      print_generic_expr (dump_file, scalar_type, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  for (; vec_mode != VOIDmode ; vec_mode = GET_MODE_WIDER_MODE (vec_mode))
    if (GET_MODE_NUNITS (vec_mode) == nunits
	&& GET_MODE_INNER (vec_mode) == inner_mode
	&& VECTOR_MODE_SUPPORTED_P (vec_mode))
      return build_vector_type (scalar_type, nunits);

  return NULL_TREE;
}


/* Function vect_align_data_ref

   Handle alignment of a memory accesses.

   FORNOW: Make sure the array is properly aligned. The vectorizer
           currently does not handle unaligned memory accesses.
           This restriction will be relaxed in the future.

   FORNOW: data_ref is an array_ref which alignment can be forced; i.e.,
           the base of the ARRAY_REF is not a pointer but an array.
           This restriction will be relaxed in the future.

   FORNOW: The array is being accessed starting at location 'init';
           We limit vectorization to cases in which init % NUNITS == 0
           (where NUNITS = GET_MODE_NUNITS (TYPE_MODE (vectype))).
           This restriction will be relaxed in the future.  */

static void
vect_align_data_ref (tree ref, tree stmt)
{
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  tree array_base = get_array_base (ref);
  struct data_reference *dr = STMT_VINFO_DATA_REF (stmt_info);

  if (!aligned_access_p (dr))
    abort (); /* FORNOW, can't handle misliagned accesses.  */

  /* The access is aligned, but some accesses are marked alignd under the
     assumption that alignment of the base of the data structure will be 
     forced:  */ 

  if (vect_force_dr_alignment_p (dr))
    {
      if (DECL_ALIGN (array_base) < TYPE_ALIGN (vectype))
        {
          /* CHECKME:
	     - is this the way to force the alignment of an array base?
             - can it be made to also work for extern decls?  */

          if (dump_file && (dump_flags & TDF_DETAILS))
            fprintf (dump_file,
                  "\nforce alignment. before: scalar/vec type_align = %d/%d\n",
                  DECL_ALIGN (array_base), TYPE_ALIGN (vectype));

          DECL_ALIGN (array_base) = TYPE_ALIGN (vectype);
        }
   }
}


/* Function vect_create_data_ref.

   Create a memory reference expression for vector access, to be used in a
   vector load/store stmt.

   Input:
   STMT: the stmt that references memory
         FORNOW: a load/store of the form 'var = a[i]'/'a[i] = var'.
   OP: the operand in STMT that is the memory reference
       FORNOW: an array_ref.
   BSI: the block_stmt_iterator where STMT is. Any new stmts created by this
        function can be added here.

   Output:
   1. Declare a new ptr to vector_type, and have it point to the array base.
      For example, for vector of type V8HI:
      v8hi *p0;
      p0 = (v8hi *)&a;

   3. Return the expression '(*p0)[idx]',
         where idx is the index used for the scalar expr.

   FORNOW: handle only simple array accesses (step 1).  */

static tree
vect_create_data_ref (tree ref, tree stmt, block_stmt_iterator *bsi)
{
  tree new_base;
  tree data_ref;
  tree idx;
  tree vec_stmt;
  tree new_temp;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  tree ptr_type;
  tree array_ptr;
  tree array_base;
  vdef_optype vdefs = STMT_VDEF_OPS (stmt);
  vuse_optype vuses = STMT_VUSE_OPS (stmt);
  int nvuses = 0, nvdefs = 0;
  int i;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "create array_ref of type:\n");
      print_generic_expr (dump_file, vectype, TDF_SLIM);
    }

  vect_align_data_ref (ref, stmt);
  array_base = get_array_base (ref);

  /*** create: vectype *p;  ***/
  ptr_type = build_pointer_type (vectype);
  array_ptr = vect_get_new_vect_var (ptr_type, vect_pointer_var, 
		get_name (array_base));
  add_referenced_tmp_var (array_ptr);
  if (TREE_CODE (array_base) == VAR_DECL)
    {
      get_var_ann (array_ptr)->type_mem_tag = array_base;
      bitmap_set_bit (vars_to_rename, var_ann (array_base)->uid);
    }
  else
    {
      /* FORNOW. This restriction will be relaxed in the future.  */
      abort ();
    }

  /* CHECKME: update name_mem_tag as well?  */

  /* Also mark for renaming all aliased variables:  */ /* CHECKME */
  if (vuses)
    nvuses = NUM_VUSES (vuses);
  if (vdefs)
    nvdefs = NUM_VDEFS (vdefs);
  for (i = 0; i < nvuses; i++)
    {
      tree use = VUSE_OP (vuses, i);;
      if (TREE_CODE (use) == SSA_NAME)
        bitmap_set_bit (vars_to_rename, var_ann (SSA_NAME_VAR (use))->uid);
    }
  for (i = 0; i < nvdefs; i++)
    {
      tree def = VDEF_RESULT (vdefs, i);
      if (TREE_CODE (def) == SSA_NAME)
        bitmap_set_bit (vars_to_rename, var_ann (SSA_NAME_VAR (def))->uid);
    }

  /*** create: p = (vectype *)&a; ***/
  vec_stmt = build (MODIFY_EXPR, void_type_node, array_ptr,
    build1 (NOP_EXPR, ptr_type,
	  build1 (ADDR_EXPR, 
		build_pointer_type (TREE_TYPE (array_base)), array_base)));
  TREE_ADDRESSABLE (array_base) = 1;
  new_temp = make_ssa_name (array_ptr, vec_stmt);
  TREE_OPERAND (vec_stmt, 0) = new_temp;
  bsi_insert_before (bsi, vec_stmt, BSI_SAME_STMT);

  idx = vect_create_index_for_array_ref (stmt, bsi);

  /*** create data ref: '(*p)[idx]' ***/

  new_base = build1 (INDIRECT_REF, build_array_type (vectype, 0), 
		TREE_OPERAND (vec_stmt, 0)); 
  data_ref = build (ARRAY_REF, vectype, new_base, idx);

  if (dump_file && (dump_flags & TDF_DETAILS))
    print_generic_expr (dump_file, data_ref, TDF_SLIM);

  return data_ref;
}


/* Function vect_create_destination_var

   Create a new temporary of type VECTYPE.  */

static tree
vect_create_destination_var (tree scalar_dest, tree vectype)
{
  tree vec_dest;
  const char *new_name;

  if (TREE_CODE (scalar_dest) != SSA_NAME)
    abort ();

  new_name = get_name (scalar_dest);
  if (!new_name)
    new_name = "var_";
  vec_dest = vect_get_new_vect_var (vectype, vect_simple_var, new_name);
  add_referenced_tmp_var (vec_dest);

  /* FIXME: introduce new type.   */
  TYPE_ALIAS_SET (TREE_TYPE (vec_dest)) =
    TYPE_ALIAS_SET (TREE_TYPE (scalar_dest));

  return vec_dest;
}


/* Function vect_init_vector.

   Insert a new stmt (INIT_STMT) that initializes a new vector veriable with
   the vector elements of VECTOR_VAR. Return the DEF of INIT_STMT. It will be
   used in the vectorization of STMT.  */

static tree
vect_init_vector (tree stmt, tree vector_var)
{
  stmt_vec_info stmt_vinfo = vinfo_for_stmt (stmt);
  struct loop *loop = STMT_VINFO_LOOP (stmt_vinfo);
  block_stmt_iterator pre_header_bsi;
  tree new_var;
  tree init_stmt;
  tree vectype = STMT_VINFO_VECTYPE (stmt_vinfo); 
  tree vec_oprnd;
 
  new_var = vect_get_new_vect_var (vectype, vect_simple_var, "cst_");
  add_referenced_tmp_var (new_var); 
  bitmap_set_bit (vars_to_rename, var_ann (new_var)->uid);
 
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      print_generic_expr (dump_file, vector_var, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  init_stmt = build (MODIFY_EXPR, vectype, new_var, vector_var);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      print_generic_expr (dump_file, init_stmt, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  /* CHECKME: Is there a utility for inserting code at the end of a basic block?  */
  pre_header_bsi = bsi_last (loop->pre_header);
  if (!bsi_end_p (pre_header_bsi)
      && is_ctrl_stmt (bsi_stmt (pre_header_bsi)))
    bsi_insert_before (&pre_header_bsi, init_stmt, BSI_NEW_STMT);
  else
    bsi_insert_after (&pre_header_bsi, init_stmt, BSI_NEW_STMT);

  vec_oprnd = TREE_OPERAND (init_stmt, 0);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      print_generic_expr (dump_file, vec_oprnd, TDF_SLIM);
      fprintf (dump_file, "\n");
    }
 
  return vec_oprnd;
}

/* Function vect_get_vec_def_for_operand.

   OP is an operand in STMT. This function returns a (vector) def that will be
   used in the vectorized counterpart of STMT.

   In the case that OP is an SSA_NAME which is defined in the loop, then
   STMT_VINFO_VEC_STMT of the defining stmt holds the relevant def.

   In case OP is an invariant or constant, a new stmt that creates a vector def
   needs to be introduced.  */

static tree
vect_get_vec_def_for_operand (tree op, tree stmt)
{
  tree vec_oprnd;

  if (!op) 
    abort ();

  if (TREE_CODE (op) == SSA_NAME)
    {
      tree vec_stmt;
      tree def_stmt;
      stmt_vec_info def_stmt_info = NULL;

      def_stmt = SSA_NAME_DEF_STMT (op);
      def_stmt_info = vinfo_for_stmt (def_stmt);
      if (dump_file && (dump_flags & TDF_DETAILS))
        {
	  fprintf (dump_file, "vect_get_vec_def_for_operand: def_stmt:\n");
	  print_generic_expr (dump_file, def_stmt, TDF_SLIM);
	}

      if (!def_stmt_info)
	{
	  /* op is defined outside the loop (it is loop invariant).
	     Create 'vec_inv = {inv,inv,..,inv}'  */
	  
	  tree vec_inv;
	  stmt_vec_info stmt_vinfo = vinfo_for_stmt (stmt);
	  tree vectype = STMT_VINFO_VECTYPE (stmt_vinfo);
	  int nunits = GET_MODE_NUNITS (TYPE_MODE (vectype));
	  basic_block bb = bb_for_stmt (def_stmt);
	  struct loop *loop = STMT_VINFO_LOOP (stmt_vinfo);
	  tree t = NULL_TREE;
	  tree def;
	  int i;

	  /* Build a tree with vector elements.  */
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "\nCreate vector_inv.\n");

	  if (TREE_CODE (def_stmt) == PHI_NODE)
	    {
	      if (flow_bb_inside_loop_p (loop, bb))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
           	    fprintf (dump_file, "\nUnsupported reduction.\n");
		  abort ();
		}
	      def = PHI_RESULT (def_stmt);
	    }
	  else if (TREE_CODE (def_stmt) == NOP_EXPR)
	    {
	      tree arg = TREE_OPERAND (def_stmt, 0);	
              if (TREE_CODE (arg) != INTEGER_CST && TREE_CODE (arg) != REAL_CST)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
           	    fprintf (dump_file, "\nUnsupported NOP_EXPR.\n");
		  abort ();
		}
              def = op;
	    }
	  else
	    def = TREE_OPERAND (def_stmt, 0);

	  for (i = nunits - 1; i >= 0; --i)
	    {
	      t = tree_cons (NULL_TREE, def, t);
	    }

	  vec_inv = build_constructor (vectype, t); /* CHECKME */
	  return vect_init_vector (stmt, vec_inv);
	}


      /* op is defined inside the loop. Get the def from the vectorized stmt.
       */
      vec_stmt = STMT_VINFO_VEC_STMT (def_stmt_info);

      if (!vec_stmt)
        abort ();

      /* CHECKME: any cases where the def we want is not TREE_OPERAND 0?  */
      vec_oprnd = TREE_OPERAND (vec_stmt, 0);

      return vec_oprnd;
    }

  if (TREE_CODE (op) == INTEGER_CST
      || TREE_CODE (op) == REAL_CST)
    {
      /* Create 'vect_cst_ = {cst,cst,...,cst}'  */

      tree vec_cst;
      stmt_vec_info stmt_vinfo = vinfo_for_stmt (stmt);
      tree vectype = STMT_VINFO_VECTYPE (stmt_vinfo);
      int nunits = GET_MODE_NUNITS (TYPE_MODE (vectype));
      tree t = NULL_TREE; 
      int i;

      /* Build a tree with vector elements.  */
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "\nCreate vector_cst.\n");
      for (i = nunits - 1; i >= 0; --i)
        { 
	  t = tree_cons (NULL_TREE, op, t);
        }
      vec_cst = build_vector (vectype, t);
      return vect_init_vector (stmt, vec_cst);
    }

  return NULL_TREE;
}


/* Function vect_transfom_assignment.

   STMT performs an assignment (copy). Create a vectorized stmt to replace it,
   and insert it at BSI.  */

static tree
vect_transform_assignment (tree stmt, block_stmt_iterator *bsi)
{
  tree vec_stmt;
  tree vec_dest;
  tree scalar_dest;
  tree op;
  tree vec_oprnd;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  tree new_temp;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "transform assignment\n");

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    abort ();

  /** Handle def. **/

  scalar_dest = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (scalar_dest) != SSA_NAME)
    abort ();
  vec_dest = vect_create_destination_var (scalar_dest, vectype);

  /** Handle use - get the vectorized def from the defining stmt.  **/

  op = TREE_OPERAND (stmt, 1);

  vec_oprnd = vect_get_vec_def_for_operand (op, stmt);
  if (! vec_oprnd)
    abort ();

  /** arguments are ready. create the new vector stmt.  **/

  vec_stmt = build (MODIFY_EXPR, vectype, vec_dest, vec_oprnd);
  new_temp = make_ssa_name (vec_dest, vec_stmt);
  TREE_OPERAND (vec_stmt, 0) = new_temp;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "add new stmt\n");
      print_generic_stmt (dump_file, vec_stmt, TDF_SLIM);
    }
  bsi_insert_before (bsi, vec_stmt, BSI_SAME_STMT);

  return vec_stmt;
}


/* Function vect_transfom_op.

   STMT performs a binary or unary operation. Create a vectorized stmt to
   replace it, and insert it at BSI.  */

static tree
vect_transform_op (tree stmt, block_stmt_iterator *bsi)
{
  tree vec_stmt;
  tree vec_dest;
  tree scalar_dest;
  tree operation;
  tree op0, op1=NULL;
  tree vec_oprnd0, vec_oprnd1=NULL;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  enum tree_code code;
  tree new_temp;
  int op_type;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "transform op\n");

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    abort ();

  operation = TREE_OPERAND (stmt, 1);

  /** Handle def. **/

  scalar_dest = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (scalar_dest) != SSA_NAME)
    abort ();
  vec_dest = vect_create_destination_var (scalar_dest, vectype);

  /** Handle uses - get the vectorized defs from the defining stmts.  **/

  /** Distinguish between binary and unary operations.  **/

  op_type = tree_nargs[TREE_CODE (operation)];
 
  if (op_type != unary_op && op_type != binary_op)
    abort ();

  op0 = TREE_OPERAND (operation, 0);
  if (op_type == binary_op)
    op1 = TREE_OPERAND (operation, 1);

  vec_oprnd0 = vect_get_vec_def_for_operand (op0, stmt);
  if (! vec_oprnd0)
    abort ();

  if(op_type == binary_op)
    {
      vec_oprnd1 = vect_get_vec_def_for_operand (op1, stmt);
      if (! vec_oprnd1)
	abort ();
    }

  /** arguments are ready. create the new vector stmt.  **/

  code = TREE_CODE (operation);
  if (op_type == binary_op)
      vec_stmt = build (MODIFY_EXPR, vectype, vec_dest,
			build (code, vectype, vec_oprnd0, vec_oprnd1));
  else
      vec_stmt = build (MODIFY_EXPR, vectype, vec_dest,
			build1 (code, vectype, vec_oprnd0));

  new_temp = make_ssa_name (vec_dest, vec_stmt);
  TREE_OPERAND (vec_stmt, 0) = new_temp;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "add new stmt\n");
      print_generic_stmt (dump_file, vec_stmt, TDF_SLIM);
    }
  bsi_insert_before (bsi, vec_stmt, BSI_SAME_STMT);

  return vec_stmt;
}


/* Function vect_transfom_store.

   STMT is a store to memory. Create a vectorized stmt to replace it,
   and insert it at BSI.  */

static tree
vect_transform_store (tree stmt, block_stmt_iterator *bsi)
{
  tree scalar_dest;
  tree vec_stmt;
  tree data_ref;
  tree op;
  tree vec_oprnd1;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "transform store\n");

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    abort ();

  /** Handle def.  **/

  scalar_dest = TREE_OPERAND (stmt, 0);

  if (TREE_CODE (scalar_dest) != ARRAY_REF)
    abort ();

  data_ref = vect_create_data_ref (scalar_dest, stmt, bsi);
  if (!data_ref)
    abort ();

  /** Handle use - get the vectorized def from the defining stmt.  **/

  op = TREE_OPERAND (stmt, 1);

  vec_oprnd1 = vect_get_vec_def_for_operand (op, stmt);
  if (! vec_oprnd1)
    abort ();

  /** Arguments are ready. create the new vector stmt.  **/

  vec_stmt = build (MODIFY_EXPR, vectype, data_ref, vec_oprnd1);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "add new stmt\n");
      print_generic_stmt (dump_file, vec_stmt, TDF_SLIM);
    }
  bsi_insert_before (bsi, vec_stmt, BSI_SAME_STMT);

  if (stmt != bsi_stmt (*bsi))
    {
      /* This is expected when an update chain for a data-ref index has been
         created (by vect_create_index_for_array_ref). The current stmt 
	 sequence is as follows:

	 (i)   some stmt
	 (i+1) vec_stmt (with a data_ref that uses index)
	 (i+2) stmt_to_update_index    <-- bsi
	 (i+3) stmt

	 The iterator bsi should be bumped to point to stmt at location (i+3)
	 because this is what the driver vect_transform_loop expects.  */

      if (dump_file && (dump_flags & TDF_DETAILS))
        {
          fprintf (dump_file, "update chain:\n");
          print_generic_stmt (dump_file, bsi_stmt (*bsi), TDF_SLIM);
        }
      bsi_next (bsi);
    }

  /* The driver function vect_transform_loop expects bsi to point the last
     scalar stmt that was vectorized.  */
  if (stmt != bsi_stmt (*bsi))
    abort ();

  return vec_stmt;
}


/* Function vect_transform_load.

   STMT is a load from memory. Create a vectorized stmt to replace it,
   and insert it at BSI.  */

static tree
vect_transform_load (tree stmt, block_stmt_iterator *bsi)
{
  tree vec_stmt;
  tree scalar_dest;
  tree vec_dest = NULL;
  tree data_ref = NULL;
  tree op;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  tree new_temp;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "transform load\n");

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    abort ();

  /** Handle def.  **/

  scalar_dest = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (scalar_dest) != SSA_NAME)
    abort ();
  vec_dest = vect_create_destination_var (scalar_dest, vectype);
  if (!vec_dest)
    abort ();

  /** Handle use.  **/

  op = TREE_OPERAND (stmt, 1);

  if (TREE_CODE (op) != ARRAY_REF)
    abort ();

  data_ref = vect_create_data_ref (op, stmt, bsi);

  if (!data_ref)
    abort ();

  /** Arguments are ready. create the new vector stmt.  **/

  vec_stmt = build (MODIFY_EXPR, vectype, vec_dest, data_ref);
  new_temp = make_ssa_name (vec_dest, vec_stmt);
  TREE_OPERAND (vec_stmt, 0) = new_temp;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "add new stmt\n");
      print_generic_stmt (dump_file, vec_stmt, TDF_SLIM);
    }
  bsi_insert_before (bsi, vec_stmt, BSI_SAME_STMT);

  if (stmt != bsi_stmt (*bsi))
    {
      /* This is expected when an update chain for a data-ref index has been
         created (by vect_create_index_for_array_ref). The current stmt
         sequence is as follows:
  
         (i)   some stmt
         (i+1) vec_stmt (with a data_ref that uses index)
         (i+2) stmt_to_update_index    <-- bsi
         (i+3) stmt 
  
         The iterator bsi should be bumped to point to stmt at location (i+3)
         because this is what the driver vect_transform_loop expects.  */

      if (dump_file && (dump_flags & TDF_DETAILS))
        {
          fprintf (dump_file, "update chain:\n");
          print_generic_stmt (dump_file, bsi_stmt (*bsi), TDF_SLIM);
        }
      bsi_next (bsi);
    }

  /* The driver function vect_transform_loop expects bsi to point the last
     scalar stmt that was vectorized.  */
  if (stmt != bsi_stmt (*bsi))
    abort ();

  return vec_stmt;
}


/* Function vect_transform_stmt.

   Create a vectorized stmt to replace STMT, and insert it at BSI.  */

static bool
vect_transform_stmt (tree stmt, block_stmt_iterator *bsi)
{
  bool is_store = false;
  tree vec_stmt = NULL;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);

  switch (STMT_VINFO_TYPE (stmt_info))
    {
    case op_vec_info_type:
      vec_stmt = vect_transform_op (stmt, bsi);
      break;

    case assignment_vec_info_type:
      vec_stmt = vect_transform_assignment (stmt, bsi);
      break;

    case load_vec_info_type:
      vec_stmt = vect_transform_load (stmt, bsi);
      break;

    case store_vec_info_type:
      vec_stmt = vect_transform_store (stmt, bsi);
      is_store = true;
      break;

    default:
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "stmt not supported\n");
      abort ();
    }

  STMT_VINFO_VEC_STMT (stmt_info) = vec_stmt;

  return is_store;
}

/* This funciton generates stmt 
   
   tmp = n / vf;

   and attachs it to preheader of LOOP.  */

static tree 
vect_build_symbl_bound (tree n, int vf, struct loop * loop)
{
  tree var, stmt, var_name;
  edge pe;
  basic_block new_bb;


  /* create tmporary variable */
  var = create_tmp_var (TREE_TYPE (n), "bnd");
  add_referenced_tmp_var (var);

  var_name = make_ssa_name (var, NULL_TREE);

  stmt = build (MODIFY_EXPR, void_type_node, var_name,
		build (TRUNC_DIV_EXPR, TREE_TYPE (n),
		       n, build_int_2(vf,0)));

  SSA_NAME_DEF_STMT (var_name) = stmt;

  pe = loop_preheader_edge (loop);
  new_bb = bsi_insert_on_edge_immediate (pe, stmt);
  if (new_bb)
    add_bb_to_loop (new_bb, new_bb->pred->src->loop_father);
  else	
    if (dump_file && (dump_flags & TDF_DETAILS))
      fprintf (dump_file, "\nNew bb on preheader edge was not generated.\n");

  return var_name;
}

/* This function update initial conditions of loop copy (second loop).
 
   LOOP_VINFO is vinfo of loop to be vectorized.
   NITERS is a variable that contains number of iteration loop executes 
   before vectorization.

   When loop is vectorized, its IVs not always preseved so 
   that to be used for initialization of loop copy (second loop). 
   Here we use access functions of IVs and number of iteration 
   loop executes in order to bring IVs to correct position.  */

static void 
vect_update_initial_conditions_of_duplicated_loop (loop_vec_info loop_vinfo, 
						   tree niters)
{
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo); 
  /* Preheader edge of duplicated loop.  */
  edge pe;
  edge latch = loop_latch_edge (loop);
  basic_block dloop_header;
  tree phi;
  
  pe = loop->exit_edges[0]->dest->succ;
  dloop_header = pe->dest;

  for (phi = phi_nodes (loop->header); phi; phi = TREE_CHAIN (phi))
    {
      tree access_fn = NULL;
      tree evolution_part;
      tree init_expr;
      tree step_expr;
      tree var, stmt, var_name1, var_name2;
      basic_block new_bb;
      int i, num_elem1, num_elem2;
      tree phi1;


      /* Skip virtual phi's. The data dependences that are associated with
         virtual defs/uses (i.e., memory accesses) are analyzed elsewhere.  */

      if (!is_gimple_reg (SSA_NAME_VAR (PHI_RESULT (phi))))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "virtual phi. skip.\n");
	  continue;
	}

      access_fn = instantiate_parameters
	(loop,
	 analyze_scalar_evolution (loop, PHI_RESULT (phi)));

      evolution_part = evolution_part_in_loop_num (access_fn, loop_num(loop));
      
      /* FORNOW: We do not transform initial conditions of IVs 
	 which evolution functions are a polynomial of degree >= 2 or
	 exponential.  */

      step_expr = evolution_part;
      init_expr = initial_condition (access_fn);

      /* create tmporary variable */
      var = create_tmp_var (TREE_TYPE (step_expr), "tmp1");
      add_referenced_tmp_var (var);

      var_name1 = make_ssa_name (var, NULL_TREE);

      stmt = build (MODIFY_EXPR, void_type_node, var_name1,
		build (MULT_EXPR, TREE_TYPE (niters),
		       niters, step_expr));

      SSA_NAME_DEF_STMT (var_name1) = stmt;
      new_bb = bsi_insert_on_edge_immediate (pe, stmt);

      /* We should not generate new bb here, only use already existing one.  */
      if (new_bb)
	abort ();            
            
      var = create_tmp_var (TREE_TYPE (init_expr), "tmp2");
      add_referenced_tmp_var (var);

      var_name2 = make_ssa_name (var, NULL_TREE);

      stmt = build (MODIFY_EXPR, void_type_node, var_name2,
		build (PLUS_EXPR, TREE_TYPE (init_expr),
		       init_expr, var_name1));
      
      SSA_NAME_DEF_STMT (var_name2) = stmt;
      new_bb = bsi_insert_on_edge_immediate (pe, stmt);

      /* We should not generate new bb here, only use already existing one.  */
      if (new_bb)
	abort ();            

      /* Fix phi expressions in duplicated loop.   */
      num_elem1 = PHI_NUM_ARGS (phi);
      for (i = 0; i < num_elem1; i++)
	if (PHI_ARG_EDGE (phi, i) == latch)
	  {
	    tree def;
	    
	    def = PHI_ARG_DEF (phi, i);
	    for (phi1 = phi_nodes (dloop_header); phi1; phi1 = TREE_CHAIN (phi1))
	      {
		num_elem2 = PHI_NUM_ARGS (phi1);
		for (i = 0; i < num_elem2; i++)
		  if (PHI_ARG_DEF (phi1, i) == def)
		    {
		      PHI_ARG_DEF (phi1, i) = var_name2;
		      PHI_ARG_EDGE (phi1, i) = pe;
		      break;
 		    }		    
	      }
	    break;
	  }
    }
}

/* Split edge EDGE_IN.  Return the new block.
   Abort on abnormal edges.  */

static basic_block
vect_tree_split_edge (edge edge_in)
{
  basic_block new_bb, dest, src;
  edge new_edge;
  tree phi;
  int i, num_elem;

  /* Abnormal edges cannot be split.  */
  if (edge_in->flags & EDGE_ABNORMAL)
    abort ();

  src = edge_in->src;
  dest = edge_in->dest;

  new_bb = create_empty_bb (src);
  new_edge = make_edge (new_bb, dest, EDGE_FALLTHRU);

  /* Find all the PHI arguments on the original edge, and change them to
     the new edge.  Do it before redirection, so that the argument does not
     get removed.  */
  for (phi = phi_nodes (dest); phi; phi = TREE_CHAIN (phi))
    {
      num_elem = PHI_NUM_ARGS (phi);
      for (i = 0; i < num_elem; i++)
	if (PHI_ARG_EDGE (phi, i) == edge_in)
	  {
	    PHI_ARG_EDGE (phi, i) = new_edge;
	    break;
	  }
    }

  if (!redirect_edge_and_branch (edge_in, new_bb))
    abort ();

  set_immediate_dominator (CDI_DOMINATORS, new_bb, src);
  set_immediate_dominator (CDI_DOMINATORS, dest, new_bb);
  
  new_bb->loop_father = src->loop_father->outer;

  if (PENDING_STMT (edge_in))
    abort ();

  return new_bb;
}


/* Function vect_transform_loop_bound.

   Create a new exit condition for the loop.  */

static void
vect_transform_loop_bound (loop_vec_info loop_vinfo, tree niters)
{
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  edge exit_edge = loop_exit_edge (loop, 0);
  block_stmt_iterator loop_exit_bsi = bsi_last (exit_edge->src);
  tree indx_before_incr, indx_after_incr;
  tree orig_cond_expr;
  int old_N = 0, vf;
  tree cond_stmt;
  tree new_loop_bound;
  bool symbl_niters;

  if (LOOP_VINFO_NITERS_KNOWN_P (loop_vinfo))
    symbl_niters = false;
  else if (LOOP_VINFO_SYMB_NUM_OF_ITERS (loop_vinfo) != NULL)
    symbl_niters = true;
  else  
    abort ();

  if(!symbl_niters)
      old_N = LOOP_VINFO_NITERS (loop_vinfo);

  vf = LOOP_VINFO_VECT_FACTOR (loop_vinfo);

  /* FORNOW:
     assuming number-of-iterations divides by the vectorization factor.  */
 if (!symbl_niters && old_N % vf)
    abort ();

  orig_cond_expr = LOOP_VINFO_EXIT_COND (loop_vinfo);
  if (!orig_cond_expr)
    abort ();
  if (orig_cond_expr != bsi_stmt (loop_exit_bsi))
    abort ();

  create_iv (integer_zero_node, integer_one_node, NULL_TREE, loop, 
	&loop_exit_bsi, false, &indx_before_incr, &indx_after_incr);

  /* CHECKME: bsi_insert is using BSI_NEW_STMT. We need to bump it back 
     to point to the exit condition. */
  bsi_next (&loop_exit_bsi);
  if (bsi_stmt (loop_exit_bsi) != orig_cond_expr)
    abort ();

  /* new loop exit test:  */
  if(!symbl_niters)
    new_loop_bound = build_int_2 (old_N/vf, 0);
  else
    new_loop_bound = vect_build_symbl_bound (niters, vf, loop); 

  
  cond_stmt = 
	build (COND_EXPR, TREE_TYPE (orig_cond_expr),
	build (LT_EXPR, boolean_type_node, indx_after_incr, new_loop_bound),
	TREE_OPERAND (orig_cond_expr, 1), TREE_OPERAND (orig_cond_expr, 2));

  bsi_insert_before (&loop_exit_bsi, cond_stmt, BSI_SAME_STMT);   

  /* remove old loop exit test:  */
  bsi_remove (&loop_exit_bsi);

  if (dump_file && (dump_flags & TDF_DETAILS))
    print_generic_expr (dump_file, cond_stmt, TDF_SLIM);
}


/* Function vect_transform_loop.

   The analysis phase has determined that the loop is vectorizable.
   Vectorize the loop - created vectorized stmts to replace the scalar
   stmts in the loop, and update the loop exit condition.  */

static void
vect_transform_loop (loop_vec_info loop_vinfo, struct loops *loops)
{
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  basic_block *bbs = LOOP_VINFO_BBS (loop_vinfo);
  int nbbs = loop->num_nodes;
  int vectorization_factor = LOOP_VINFO_VECT_FACTOR (loop_vinfo);
  block_stmt_iterator si;
  int i;
  tree var = NULL, var_name = NULL;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<vec_transform_loop>>\n");

  /* If the loop has symbolic number of iterations 'n' 
     (i.e. it's not a compile time constant), 
     then an epilog loop need to be created. We therefore duplicate 
     the initial loop. The original loop will be vectorized, and will compute
     (n/VF) iterations. The second copy will remain serial and compute 
     the remaining (n%VF) iterations. (VF is the vectorization factor).  */

  if ( LOOP_VINFO_NITERS (loop_vinfo) == -1 && 
       LOOP_VINFO_SYMB_NUM_OF_ITERS (loop_vinfo) != NULL )
    {

      edge ee,pe;
      basic_block new_bb;
      tree stmt, ni;

      vect_tree_duplicate_loop (loop, loops);

      /* FORNOW: Only loops with one exit are handled. */
      ee = loop->exit_edges[0];
      new_bb = vect_tree_split_edge(ee);
      if (new_bb)
	loop->exit_edges[0] = new_bb->pred;
      else
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "\nFailed to generate emply bb after loop.\n");
	  abort ();
	}
      /* Generate temporary variable that contains 
         number of iterations loop executes.  */
      ni = LOOP_VINFO_SYMB_NUM_OF_ITERS(loop_vinfo);
      var = create_tmp_var (TREE_TYPE (ni), "niters");
      add_referenced_tmp_var (var);

      var_name = force_gimple_operand (ni, &stmt, false, var);
      pe = loop_preheader_edge (loop);
      new_bb = bsi_insert_on_edge_immediate (pe, stmt);
      if (new_bb)
	add_bb_to_loop (new_bb, new_bb->pred->src->loop_father);

      /* Update initial conditions of loop copy. */
      vect_update_initial_conditions_of_duplicated_loop (loop_vinfo, var_name);
    }

  /* CHECKME: FORNOW the vectorizer supports only loops which body consist
     of one basic block + header. When the vectorizer will support more
     involved loop forms, the order by which the BBs are traversed need
     to be considered.  */

  for (i = 0; i < nbbs; i++)
    {
      basic_block bb = bbs[i];

      for (si = bsi_start (bb); !bsi_end_p (si);)
	{
	  tree stmt = bsi_stmt (si);
	  stmt_vec_info stmt_info;
	  tree vectype;
	  bool is_store;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\n-----\nvectorizing statement:\n");
	      print_generic_stmt (dump_file, stmt, TDF_SLIM);
	    }	

	  stmt_info = vinfo_for_stmt (stmt);
	  if (!stmt_info)
	    abort ();

	  if (!STMT_VINFO_RELEVANT_P (stmt_info))
	    {
	      bsi_next (&si);
	      continue;
	    }

	  /* FORNOW: Verify that all stmts operate on the same number of
	             units and no inner unrolling is necessary.  */
	  vectype = STMT_VINFO_VECTYPE (stmt_info);
	  if (GET_MODE_NUNITS (TYPE_MODE (vectype)) != vectorization_factor)
	    abort ();

	  /* -------- vectorize statement ------------ */
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "transform statement.\n");

	  is_store = vect_transform_stmt (stmt, &si);

	  if (is_store)
	    {
	      /* free the attched stmt_vec_info and remove the stmt.  */
	      stmt_ann_t ann = stmt_ann (stmt);
	      free (stmt_info);
	      set_stmt_info (ann, NULL);

	      bsi_remove (&si);
	      continue;
	    }

	  bsi_next (&si);

	}			/* stmts in BB */
    }				/* BBs in loop */

  vect_transform_loop_bound (loop_vinfo, var_name);
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<Success! loop vectorized.>>\n");
}


/* Function vect_is_simple_use.

   Return whether the vectorization of a stmt, in LOOP, that uses OPERAND is
   supportable. OPERANDS that can't be vectorized yet are those defined
   by a reduction operation or some other form of recurrence. 
   Other OPERANDS - defined in the loop, constants and invariants - 
   are supported.  */

static bool
vect_is_simple_use (tree operand, struct loop *loop)
{ 
  tree def_stmt;
  basic_block bb;

  if (!operand)
    return false;

  if (TREE_CODE (operand) == SSA_NAME)
    {
      def_stmt = SSA_NAME_DEF_STMT (operand);

      if (def_stmt == NULL_TREE)
        return false;

      if (TREE_CODE (def_stmt) == NOP_EXPR)
	{
	  tree arg = TREE_OPERAND (def_stmt, 0);

	  if (TREE_CODE (arg) == INTEGER_CST || TREE_CODE (arg) == REAL_CST)
	    return true;

	  return false;  
	}

      bb = bb_for_stmt (def_stmt);
      if (TREE_CODE (def_stmt) == PHI_NODE && flow_bb_inside_loop_p (loop, bb))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, 
		"use defined in loop phi - some form of reduction.\n");
	  return false;
	}

      return true;  
    }

  if (TREE_CODE (operand) == INTEGER_CST
      || TREE_CODE (operand) == REAL_CST)
    {
      return true;
    }

  return false;
}


/* Function vect_is_supportable_op.

   Verify that STMT performs an operation that can be vectorized.  */

static bool
vect_is_supportable_op (tree stmt)
{
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree operation;
  enum tree_code code;
  tree op;
  enum machine_mode vec_mode;
  optab optab;
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  struct loop *loop = STMT_VINFO_LOOP (stmt_info);
  int i,op_type;

  /* Is op? */

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  if (TREE_CODE (TREE_OPERAND (stmt, 0)) != SSA_NAME)
    return false;

  operation = TREE_OPERAND (stmt, 1);
  code = TREE_CODE (operation);

  switch (code)
    {
    case PLUS_EXPR:
      optab = add_optab;
      break;
    case MULT_EXPR:
      optab = smul_optab;
      break;
    case MINUS_EXPR:
      optab = sub_optab;
      break;
    case BIT_AND_EXPR:
      optab = and_optab;
      break;
    case BIT_XOR_EXPR:
      optab = xor_optab;
      break;
    case BIT_IOR_EXPR:
      optab = ior_optab;
      break;
    case BIT_NOT_EXPR:
      optab = one_cmpl_optab;
      break;
    default:
      return false;
    }
  
  /* Support only unary or binary operations.  */

  op_type = tree_nargs[code];
  if (op_type != unary_op && op_type != binary_op)
    return false;
  
  for (i = 0; i < op_type; i++)
    {
      op = TREE_OPERAND (operation, i);
      if (!vect_is_simple_use (op, loop))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "use not simple.\n");
	  return false;
	}	
    } 

  /* Supportable by target?  */

  if (!optab)
    return false;

  vec_mode = TYPE_MODE (vectype);

  if (optab->handlers[(int) vec_mode].insn_code == CODE_FOR_nothing)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "op not supported by target\n");
      return false;
    }

  /* FORNOW: Not considering the cost.  */

  STMT_VINFO_TYPE (stmt_info) = op_vec_info_type;

  return true;
}


/* Function vect_is_supportable_store.

   Verify that STMT performs a store to memory operation,
   and can be vectorized.  */

static bool
vect_is_supportable_store (tree stmt)
{
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree scalar_dest;
  tree op;
  struct loop *loop = STMT_VINFO_LOOP (stmt_info);

  /* Is vectorizable store? */

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  scalar_dest = TREE_OPERAND (stmt, 0);

  if (TREE_CODE (scalar_dest) != ARRAY_REF)
    return false;

  op = TREE_OPERAND (stmt, 1);

  if (!vect_is_simple_use (op, loop))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "use not simple.\n");
      return false;
    }

  if (!STMT_VINFO_DATA_REF (stmt_info))
    return false;

  /* Previous analysis steps have already verified that the data ref is
     vectorizable (w.r.t data dependences, access pattern, etc).  */

  /* FORNOW: Not considering the cost.  */

  STMT_VINFO_TYPE (stmt_info) = store_vec_info_type;

  return true;
}


/* Function vect_is_supportable_load.

   Verify that STMT performs a load from memory operation,
   and can be vectorized.  */

static bool
vect_is_supportable_load (tree stmt)
{
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree scalar_dest;
  tree op;

  /* Is vectorizable load? */

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  scalar_dest = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (scalar_dest) != SSA_NAME)
    return false;

  op = TREE_OPERAND (stmt, 1);

  if (TREE_CODE (op) != ARRAY_REF)
    return false;

  if (!STMT_VINFO_DATA_REF (stmt_info))
    return false;

  /* Previous analysis steps have already verified that the data ref is
     vectorizable (w.r.t data dependences, access pattern, etc).  */

  /* FORNOW: Not considering the cost.  */

  STMT_VINFO_TYPE (stmt_info) = load_vec_info_type;

  return true;
}


/* Function vect_is_supportable_assignment.

   Verify that STMT performs an assignment, and can be vectorized.  */

static bool
vect_is_supportable_assignment (tree stmt)
{
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree scalar_dest;
  tree op;
  struct loop *loop = STMT_VINFO_LOOP (stmt_info);

  /* Is vectorizable assignment? */

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  scalar_dest = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (scalar_dest) != SSA_NAME)
    return false;

  op = TREE_OPERAND (stmt, 1);

  if (!vect_is_simple_use (op, loop))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "use not simple.\n");
      return false;
    }

  STMT_VINFO_TYPE (stmt_info) = assignment_vec_info_type;

  return true;
}


/* Function vect_analyze_operations.

   Scan the loop stmts and make sure they are all vectorizable.  */

static bool
vect_analyze_operations (loop_vec_info loop_vinfo)
{
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  basic_block *bbs = LOOP_VINFO_BBS (loop_vinfo);
  int nbbs = loop->num_nodes;
  block_stmt_iterator si;
  int vectorization_factor = 0;
  int i;
  bool ok;
  tree scalar_type;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<vect_analyze_operations>>\n");

  for (i = 0; i < nbbs; i++)
    {
      basic_block bb = bbs[i];

      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
	{
	  tree stmt = bsi_stmt (si);
	  int nunits;
	  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
	  tree vectype;
	  dataflow_t df;
	  int j, num_uses;
	  vdef_optype vdefs;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\n-------\nexamining statement:\n");
	      print_generic_stmt (dump_file, stmt, TDF_SLIM);
	    }

	  if (!stmt_info)
	    abort ();

	  /* skip stmts which do not need to be vectorized.
	     this is expected to include:
	     - the COND_EXPR which is the loop exit condition
	     - any LABEL_EXPRs in the loop
	     - computations that are used only for array indexing or loop
	     control  */

	  if (!STMT_VINFO_RELEVANT_P (stmt_info))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
	        fprintf (dump_file, "irrelevant.\n");
	      continue;
	    }

	  /* FORNOW: Make sure that the def of this stmt is not used out
             side the loop. This restriction will be relaxed in the future.  */
          vdefs = STMT_VDEF_OPS (stmt);
          if (!vdefs)  /* CHECKME */
            {
              df = get_immediate_uses (stmt);
              num_uses = num_immediate_uses (df);
              for (j = 0; j < num_uses; j++)
                {
                  tree use = immediate_use (df, j);
                  basic_block bb = bb_for_stmt (use);
                  if (!flow_bb_inside_loop_p (loop, bb))
                    {
                      if (dump_file && (dump_flags & TDF_DETAILS))
                        {
                          fprintf (dump_file, "def used out of loop:\n");
                          print_generic_stmt (dump_file, use, TDF_SLIM);
                        }
                      return false;
                    }
                }
            }

	  if (VECTOR_MODE_P (TYPE_MODE (TREE_TYPE (stmt))))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "vector stmt in loop!\n");
		  print_generic_stmt (dump_file, stmt, TDF_SLIM);
		}
	      return false;
	    }

          if (STMT_VINFO_DATA_REF (stmt_info))
            scalar_type = TREE_TYPE (DR_REF (STMT_VINFO_DATA_REF (stmt_info)));    
          else
	    scalar_type = TREE_TYPE (stmt);
	  vectype = get_vectype_for_scalar_type (scalar_type);
	  if (!vectype)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "no vectype for stmt.\n");
		  print_generic_stmt (dump_file, stmt, TDF_SLIM);
		}
	      return false;
	    }
	  STMT_VINFO_VECTYPE (stmt_info) = vectype;

	  ok = (vect_is_supportable_op (stmt)
		|| vect_is_supportable_assignment (stmt)
		|| vect_is_supportable_load (stmt)
		|| vect_is_supportable_store (stmt));

	  if (!ok)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "stmt not supported.\n");
		  print_generic_stmt (dump_file, stmt, TDF_SLIM);
		}
	      return false;
	    }

	  nunits = GET_MODE_NUNITS (TYPE_MODE (vectype));
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "nunits = %d\n", nunits);

	  if (vectorization_factor)
	    {
	      /* FORNOW: don't allow mixed units.
	         This restriction will be relaxed in the future.  */
	      if (nunits != vectorization_factor)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "mixed types unsupported.\n");
		      print_generic_stmt (dump_file, stmt, TDF_SLIM);
		    }
		  return false;
		}
	    }
	  else
	    vectorization_factor = nunits;
	}
    }

  /* TODO: Analayze cost. Decide if worth while to vectorize.  */

  LOOP_VINFO_VECT_FACTOR (loop_vinfo) = vectorization_factor;

  /* FORNOW: handle only cases where the loop bound divides by the
     vectorization factor.  */

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "vectorization_factor = %d, niters = %d\n",
	vectorization_factor, LOOP_VINFO_NITERS (loop_vinfo));

  if (vectorization_factor == 0
      || (!LOOP_VINFO_NITERS_KNOWN_P (loop_vinfo) && 
	  !LOOP_VINFO_SYMB_NUM_OF_ITERS(loop_vinfo)))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, 
		"Complicate loop bound.\n");
      return false;
    }

  if (LOOP_VINFO_NITERS_KNOWN_P (loop_vinfo) &&
	   LOOP_VINFO_NITERS (loop_vinfo) % vectorization_factor != 0)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, 
		"loop bound does not divided by %d.\n",
		 vectorization_factor);
      return false;
    }

  return true;
}


/* Function exist_non_indexing_operands_for_use_p 

   USE is one of the uses attached to STMT. Check if USE is 
   used in STMT for anything other than indexing an array.  */

static bool
exist_non_indexing_operands_for_use_p (tree use, tree stmt)
{
  tree operand;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
 
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "exist_non_indexing_operands_for_use_p?:\n");
      print_generic_stmt (dump_file, stmt, TDF_SLIM);
    }

  /* USE corresponds to some operand in STMT. If there is no data
     reference in STMT, then any operand that corresponds to USE
     is not indexing an array.  */
  if (!STMT_VINFO_DATA_REF (stmt_info))
    return true;
 
  /* STMT has a data_ref. FORNOW this means that its of one of
     the following forms:
     -1- ARRAY_REF = var
     -2- var = ARRAY_REF
     (This should have been verified in analyze_data_refs).

     'var' in the second case corresponds to a def, not a use,
     so USE cannot correspond to any operands that are not used 
     for array indexing.

     Therefore, all we need to check is if STMT falls into the
     first case, and whether var corresponds to USE.  */
 
  if (TREE_CODE (TREE_OPERAND (stmt, 0)) == SSA_NAME)
    return false;

  operand = TREE_OPERAND (stmt, 1);

  if (TREE_CODE (operand) != SSA_NAME)
    return false;

  if (operand == use)
    return true;

  return false;
}


/* Function vect_is_simple_iv_evolution.

   FORNOW: A simple evolution of an induction variables in the loop is
   considered a polynomial evolution with step 1.  */

static bool
vect_is_simple_iv_evolution (unsigned loop_nb, tree access_fn, tree * init, 
				tree * step, bool strict)
{
  tree init_expr;
  tree step_expr;
  
  tree evolution_part = evolution_part_in_loop_num (access_fn, loop_nb);

  /* When there is no evolution in this loop, the evolution function
     is not "simple".  */  
  if (evolution_part == NULL_TREE)
    return false;
  
  /* When the evolution is a polynomial of degree >= 2 or
     exponential, the evolution function is not "simple".  */
  if (TREE_CODE (evolution_part) == POLYNOMIAL_CHREC
      || TREE_CODE (evolution_part) == EXPONENTIAL_CHREC)
    return false;
  
  step_expr = evolution_part;
  init_expr = initial_condition (access_fn);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nstep: ");
      print_generic_expr (dump_file, step_expr, TDF_SLIM);
      fprintf (dump_file, "\ninit: ");
      print_generic_expr (dump_file, init_expr, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  *init = init_expr;
  *step = step_expr;

  if (TREE_CODE (step_expr) != INTEGER_CST)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "\nstep unknown.\n");
      return false;
    }

  if (strict)
    if (!integer_onep (step_expr))
      {
        if (dump_file && (dump_flags & TDF_DETAILS))
	  print_generic_expr (dump_file, step_expr, TDF_SLIM);
        return false;
      }

  return true;
}


/* Function vect_analyze_scalar_cycles.

   Examine the cross iteration def-use cycles of scalar variables, by
   analyzing the loop (scalar) PHIs; verify that the cross iteration def-use
   cycles that they represent do not impede vectorization.

   FORNOW: Reduction as in the following loop, is not supported yet:
              loop1:
              for (i=0; i<N; i++)
                 sum += a[i];
	   The cross-iteration cycle corresponding to variable 'sum' will be
	   considered too complicated and will impede vectorization.

   FORNOW: Induction as in the following loop, is not supported yet:
              loop2:
              for (i=0; i<N; i++)
                 a[i] = i;

           However, the following loop *is* vectorizable:
              loop3:
              for (i=0; i<N; i++)
                 a[i] = b[i];

           In both loops there exists a def-use cycle for the variable i:
              loop: i_2 = PHI (i_0, i_1)
                    a[i_2] = ...;
                    i_1 = i_2 + 1;
                    GOTO loop;

           The evolution of the above cycle is considered simple enough,
	   however, we also check that the cycle does not need to be
	   vectorized, i.e - we check that the variable that this cycle
	   defines is only used for array indexing or in stmts that do not
	   need to be vectorized. This is not the case in loop2, but it
	   *is* the case in loop3.  */

static bool
vect_analyze_scalar_cycles (loop_vec_info loop_vinfo)
{
  tree phi;
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  basic_block bb = loop->header;
  tree dummy;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<vect_analyze_scalar_evolutions>>\n");

  for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
    {
#if 0
      int i;
      int num_uses;
      dataflow_t df;
#endif
      tree access_fn = NULL;

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
          fprintf (dump_file, "Analyze phi\n");
          print_generic_expr (dump_file, phi, TDF_SLIM);
	}

      /* Skip virtual phi's. The data dependences that are associated with
         virtual defs/uses (i.e., memory accesses) are analyzed elsewhere.  */

      if (!is_gimple_reg (SSA_NAME_VAR (PHI_RESULT (phi))))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "virtual phi. skip.\n");
	  continue;
	}

      /* Analyze the evolution function.  */

      /* FORNOW: The only scalar cross-iteration cycles that we allow are
         those of the loop induction variable;
         Furthermore, if that induction variable is used in an operation
         that needs to be vectorized (i.e, is not solely used to index
         arrays and check the exit condition) - we do not support its
         vectorization Yet.  */

      /* 1. Verify that it is an IV with a simple enough access pattern.  */

      if (dump_file && (dump_flags & TDF_DETAILS)) 
        fprintf (dump_file, "analyze cycles: call monev analyzer!\n");

      access_fn = instantiate_parameters
	(loop,
	 analyze_scalar_evolution (loop, PHI_RESULT (phi)));

      if (!access_fn)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "No Access function.");
	  return false;
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
        {
           fprintf (dump_file, "Access function of PHI: ");
           print_generic_expr (dump_file, access_fn, TDF_SLIM);
        }

      if (!vect_is_simple_iv_evolution (loop_num (loop), access_fn, &dummy, 
					&dummy, false))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "unsupported cross iter cycle.\n");
	  return false;
	}

#if 0 /* following check is now performed in "vect_is_simple_use" */

      /* 2. Verify that this variable is only used in stmts that do not need
         to be vectorized.  
	 FIXME: the following checks should be applied to other defs in
	 this def-use cycle (not just to the phi result).  */

      df = get_immediate_uses (phi);
      num_uses = num_immediate_uses (df);
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "num uses = %d\n", num_uses);
      for (i = 0; i < num_uses; i++)
	{
	  tree use = immediate_use (df, i);
	  stmt_vec_info stmt_info = vinfo_for_stmt (use);

	  if (!stmt_info)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "\nused out side the loop??\n");
		  print_generic_expr (dump_file, use, TDF_SLIM);
		}
	      return false;
	    }

	  if (STMT_VINFO_RELEVANT_P (stmt_info)
	      && exist_non_indexing_operands_for_use_p (PHI_RESULT (phi), use))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file,
			   "\ninduction vectorization. Unsupported.\n");
		  print_generic_expr (dump_file, use, TDF_SLIM);
		}
	      return false;
	    }
	}

#endif

    }

  return true;
}


/* Function get_array_base.

   Return the base of the array_ref EXPR.  */

static tree
get_array_base (tree expr)
{
  tree expr1;
  if (TREE_CODE (expr) != ARRAY_REF)
    abort ();

  expr1 = TREE_OPERAND (expr, 0);
  while (TREE_CODE (expr1) == ARRAY_REF)
    expr1 = TREE_OPERAND (expr1, 0);

  return expr1;
}


/* Function vect_analyze_data_ref_dependence.

   Return TRUE if there (might) exist a dependence between a memory-reference
   DRA and a memory-reference DRB.  */

static bool
vect_analyze_data_ref_dependence (struct data_reference *dra,
				  struct data_reference *drb)
{
  /* FORNOW: use most trivial and conservative test.  */

  /* CHECKME: this test holds only if the array base is not a pointer.
     This had been verified by analyze_data_refs.
     This restriction will be relaxed in the future.  */

  if (!array_base_name_differ_p (dra, drb))
    {
      enum data_dependence_direction ddd =
	ddg_direction_between_stmts (DR_STMT (dra), DR_STMT (drb), 
				      loop_num (loop_of_stmt (DR_STMT (dra))));

      if (ddd == dir_independent)
	return true;

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, 
		"vect_analyze_data_ref_dependence: same base\n");
      return false;
    }

  return true;
}


/* Function vect_analyze_data_ref_dependences.

   Examine all the data references in the loop, and make sure there do not
   exist any data dependences between them.

   FORNOW: We do not construct a data dependence graph and try to deal with
           dependences, but fail at the first data dependence that we
	   encounter.

   FORNOW: We only handle array references.

   FORNOW: We apply a trivial conservative dependence test.  */

static bool
vect_analyze_data_ref_dependences (loop_vec_info loop_vinfo)
{
  unsigned int i, j;
  varray_type loop_write_refs = LOOP_VINFO_DATAREF_WRITES (loop_vinfo);
  varray_type loop_read_refs = LOOP_VINFO_DATAREF_READS (loop_vinfo);

  /* examine store-store (output) dependences */

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "compare all store-store pairs\n");

  for (i = 0; i < VARRAY_ACTIVE_SIZE (loop_write_refs); i++)
    {
      for (j = i + 1; j < VARRAY_ACTIVE_SIZE (loop_write_refs); j++)
	{
	  struct data_reference *dra =
	    VARRAY_GENERIC_PTR (loop_write_refs, i);
	  struct data_reference *drb =
	    VARRAY_GENERIC_PTR (loop_write_refs, j);
	  bool ok = vect_analyze_data_ref_dependence (dra, drb);
	  if (!ok)
	    return false;
	}
    }

  /* examine load-store (true/anti) dependences */

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "compare all load-store pairs\n");

  for (i = 0; i < VARRAY_ACTIVE_SIZE (loop_read_refs); i++)
    {
      for (j = 0; j < VARRAY_ACTIVE_SIZE (loop_write_refs); j++)
	{
	  struct data_reference *dra = VARRAY_GENERIC_PTR (loop_read_refs, i);
	  struct data_reference *drb =
	    VARRAY_GENERIC_PTR (loop_write_refs, j);
	  bool ok = vect_analyze_data_ref_dependence (dra, drb);
	  if (!ok)
	    return false;
	}
    }

  return true;
}


/* Function vect_get_array_first_index.

   REF is an array reference. Find the lower bound of the array dimension and
   return it in ARRAY_FIRST_INDEX (e.g, 0 in C arrays, 1 in Fortran arrays 
   (unless defined otherwise). At the moment, gfortran arrays are represented
   with a poiner which points to one element lower than the array base, so
   ARRAY_FIRST_INDEX is currently 0 also for Fortran arrays).
   Return TRUE if such lower bound was found, and FLASE otherwise.  */

static bool
vect_get_array_first_index (tree ref, int *array_first_index)
{
  tree array_start;
  tree array_base_type;
  int array_start_val;

  array_base_type = TREE_TYPE (TREE_OPERAND (ref, 0));
  if (! TYPE_DOMAIN (array_base_type))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        {
          fprintf (dump_file, "no domain for array base type\n");
          print_generic_expr (dump_file, array_base_type, TDF_DETAILS);
        }
      return false;
    }

  array_start = TYPE_MIN_VALUE (TYPE_DOMAIN (array_base_type));
  if (TREE_CODE (array_start) != INTEGER_CST)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        {
          fprintf (dump_file, "array min val not integer cst\n");
          print_generic_expr (dump_file, array_start, TDF_DETAILS);
        }
      return false;
    }

  if (TREE_INT_CST_HIGH (array_start) != 0)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "array min val CST_HIGH != 0\n");
      return false;
    }

  array_start_val = TREE_INT_CST_LOW (array_start);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      print_generic_expr (dump_file, array_start, TDF_DETAILS);
      fprintf (dump_file, "\narray min val = %d\n", array_start_val);
    }

  *array_first_index = array_start_val;

  return true;
}


/* Function vect_compute_data_ref_alignment

   Compute the mislignment of the data reference DR.

   FOR NOW: No analysis is actually performed. Misalignment is calculated
   only for trivial cases. TODO.  */

static void
vect_compute_data_ref_alignment (struct data_reference *dr, 
				 loop_vec_info loop_vinfo ATTRIBUTE_UNUSED)
{
  tree stmt = DR_STMT (dr);
  tree ref = DR_REF (dr);
  tree vectype;
  tree access_fn = DR_ACCESS_FN (dr, 0); /* CHECKME */
  tree init;
  int init_val;
  tree scalar_type;
  int misalign;
  int array_start_val;
  bool ok;

  /* Initialize misalignment to unknown.  */
  DR_MISALIGNMENT (dr) = -1;


  /* In the special case of an array which alignment can be forced, we may be
     able to compute more informative information.  */

  if (!vect_force_dr_alignment_p (dr))
    return;

  init = initial_condition (access_fn);

  /* FORNOW: In order to simplify the handling of alignment, we make sure 
     that the first location at which the array is accessed ('init') is on an 
     'NUNITS' boundary, since we are assuming here that the alignment of the
     'array base' is aligned. This is too conservative, since we require that 
     both {'array_base' is a multiple of NUNITS} && {'init' is a multiple of 
     NUNITS}, instead of just {('array_base' + 'init') is a multiple of NUNITS}.
     This should be relaxed in the future.  */

  if (init && TREE_CODE (init) != INTEGER_CST)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "init not INTEGER_CST\n");
      return;
    }

  /* CHECKME */
  if (TREE_INT_CST_HIGH (init) != 0)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "init CST_HIGH != 0\n");
      return;
    }

  init_val = TREE_INT_CST_LOW (init);

  scalar_type = TREE_TYPE (ref);
  vectype = get_vectype_for_scalar_type (scalar_type);
  if (!vectype)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        {
          fprintf (dump_file, "no vectype for stmt: ");
          print_generic_expr (dump_file, stmt, TDF_SLIM);
          fprintf (dump_file, "\nscalar_type: ");
          print_generic_expr (dump_file, scalar_type, TDF_DETAILS);
          fprintf (dump_file, "\n");
        }
      return;
    }

  ok = vect_get_array_first_index (ref, &array_start_val);
  if (!ok)
    return;

  misalign = (init_val - array_start_val) % 
		GET_MODE_NUNITS (TYPE_MODE (vectype));

  DR_MISALIGNMENT (dr) = misalign;

  return;
}


/* Function vect_compute_data_refs_alignment

   Compute the mislignment of data references in the loop.
   This pass may take place at function granularity instead of at loop
   granularity.

   FOR NOW: No analysis is actually performed. Misalignment is calculated
   only for trivial cases. TODO.  */

static void
vect_compute_data_refs_alignment (loop_vec_info loop_vinfo)
{
  varray_type loop_write_datarefs = LOOP_VINFO_DATAREF_WRITES (loop_vinfo);
  varray_type loop_read_datarefs = LOOP_VINFO_DATAREF_READS (loop_vinfo);
  unsigned int i;
  
  for (i = 0; i < VARRAY_ACTIVE_SIZE (loop_write_datarefs); i++)
    {
      struct data_reference *dr = VARRAY_GENERIC_PTR (loop_write_datarefs, i);
      vect_compute_data_ref_alignment (dr, loop_vinfo);
    }

  for (i = 0; i < VARRAY_ACTIVE_SIZE (loop_read_datarefs); i++)
    {
      struct data_reference *dr = VARRAY_GENERIC_PTR (loop_read_datarefs, i);
      vect_compute_data_ref_alignment (dr, loop_vinfo);
    }

  return; 
}


/* Function vect_enhance_data_refs_alignment

   This pass will use loop versioning and loop peeling in order to enhance
   the alignment of data references in the loop.

   FOR NOW: we assume that whatever versioning/peeling takes place, only the
   original loop is to be vectorized; Any other loops that are created by
   the transformations performed in this pass - are not supposed to be
   vectorized. This restriction will be relaxed.

   FOR NOW: No transformation is actually performed. TODO.  */

static void
vect_enhance_data_refs_alignment (loop_vec_info loop_vinfo ATTRIBUTE_UNUSED)
{
  /*
     This pass will require a cost model to guide it whether to apply peeling 
     or versioning or a combination of the two. For example, the scheme that
     intel uses when given a loop with several memory accesses, is as follows:
     choose one memory access ('p') which alignment you want to force by doing 
     peeling. Then, either (1) generate a loop in which 'p' is aligned and all 
     other accesses are not necessarily aligned, or (2) use loop versioning to 
     generate one loop in which all accesses are aligned, and another loop in 
     which only 'p' is necessarily aligned. 

     ("Automatic Intra-Register Vectorization for the Intel Architecture",
      Aart J.C. Bik, Milind Girkar, Paul M. Grey and Ximmin Tian, International
      Journal of Parallel Programming, Vol. 30, No. 2, April 2002.)	

     Devising a cost model is the most critical aspect of this work. It will 
     guide us on which access to peel for, whether to use loop versioning, how 
     many versions to create, etc. The cost model will probably consist of 
     generic considerations as well as target specific considerations (on 
     powerpc for example, misaligned stores are more painful than misaligned 
     loads). 

     Here is the general steps involved in alignment enhancements:
    
     -- original loop, before alignment analysis:
	for (i=0; i<N; i++){
	  x = q[i];			# DR_MISALIGNMENT(q) = unknown
	  p[i] = y;			# DR_MISALIGNMENT(p) = unknown
	}

     -- After vect_compute_data_refs_alignment:
	for (i=0; i<N; i++){
	  x = q[i];			# DR_MISALIGNMENT(q) = 3
	  p[i] = y;			# DR_MISALIGNMENT(p) = unknown
	}

     -- Possibility 1: we do loop versioning:
     if (p is aligned) {
	for (i=0; i<N; i++){	# loop 1A
	  x = q[i];			# DR_MISALIGNMENT(q) = 3
	  p[i] = y;			# DR_MISALIGNMENT(p) = 0
	}
     } 
     else {
	for (i=0; i<N; i++){	# loop 1B
	  x = q[i];			# DR_MISALIGNMENT(q) = 3
	  p[i] = y;			# DR_MISALIGNMENT(p) = unaligned
	}
     }
   
     -- Possibility 2: we do loop peeling:
     for (i = 0; i < 3; i++){	# (scalar loop, not to be vectorized).
	x = q[i];
	p[i] = y;
     }
     for (i = 3; i < N; i++){	# loop 2A
	x = q[i];			# DR_MISALIGNMENT(q) = 0
	p[i] = y;			# DR_MISALIGNMENT(p) = unknown
     }

     -- Possibility 3: combination of loop peeling and versioning:
     for (i = 0; i < 3; i++){	# (scalar loop, not to be vectorized).
	x = q[i];
	p[i] = y;
     }
     if (p is aligned) {
	for (i = 3; i<N; i++){  # loop 3A
	  x = q[i];			# DR_MISALIGNMENT(q) = 0
	  p[i] = y;			# DR_MISALIGNMENT(p) = 0
	}
     } 
     else {
	for (i = 3; i<N; i++){	# loop 3B
	  x = q[i];			# DR_MISALIGNMENT(q) = 0
	  p[i] = y;			# DR_MISALIGNMENT(p) = unaligned
	}
     }

     These loops are later passed to loop_transform to be vectorized. The 
     vectorizer will use the alignment information to guide the transformation 
     (whether to generate regular loads/stores, or with special handling for 
     misalignment). 
   */

  return;  
}


/* Function vect_analyze_data_refs_alignment

   Analyze the alignment of the data-references in the loop.
   FOR NOW: Until support fot misliagned accesses is in place, only if all
   accesses are aligned can the loop be vectorized. This restruction will be 
   relaxed.  */ 

static bool
vect_analyze_data_refs_alignment (loop_vec_info loop_vinfo)
{
  varray_type loop_write_datarefs = LOOP_VINFO_DATAREF_WRITES (loop_vinfo);
  varray_type loop_read_datarefs = LOOP_VINFO_DATAREF_READS (loop_vinfo);
  unsigned int i;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<vect_analyze_data_refs_alignment>>\n");


  /* This pass may take place at function granularity instead of at loop
     granularity.  */

  vect_compute_data_refs_alignment (loop_vinfo);


  /* This pass will use loop versioning and loop peeling in order to enhance
     the alignment of data references in the loop.
     FOR NOW: we assume that whatever versioning/peeling took place, the 
     original loop is to be vectorized. Any other loops that were created by
     the transformations performed in this pass - are not supposed to be 
     vectorized. This restriction will be relaxed.  */

  vect_enhance_data_refs_alignment (loop_vinfo);


  /* Finally, check that loop can be vectorized. 
     FOR NOW: Until support fot misliagned accesses is in place, only if all
     accesses are aligned can the loop be vectorized. This restruction will be 
     relaxed.  */

  for (i = 0; i < VARRAY_ACTIVE_SIZE (loop_write_datarefs); i++)
    {
      struct data_reference *dr = VARRAY_GENERIC_PTR (loop_write_datarefs, i);
      if (!aligned_access_p (dr))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
            fprintf (dump_file, "first access not aligned.\n");
	  return false;
	}
    }

  for (i = 0; i < VARRAY_ACTIVE_SIZE (loop_read_datarefs); i++)
    {
      struct data_reference *dr = VARRAY_GENERIC_PTR (loop_read_datarefs, i);
      if (!aligned_access_p (dr))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
            fprintf (dump_file, "first access not aligned.\n");
	  return false;
	}
    }

  return true;
}


/* Function vect_analyze_data_ref_access.

   Analyze the access pattern of the data-reference DR. For now, a data access
   has to consecutive and aligned to be considered vectorizable.  */

static bool
vect_analyze_data_ref_access (struct data_reference *dr)
{
  varray_type access_fns = DR_ACCESS_FNS (dr);
  tree access_fn;
  tree init, step;

  /* FORNOW: handle only one dimensional arrays.
     This restriction will be relaxed in the future.  */
  if (VARRAY_ACTIVE_SIZE (access_fns) != 1)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "multi dimensional array reference.\n");
      return false;
    }
  access_fn = DR_ACCESS_FN (dr, 0);

  if (!vect_is_simple_iv_evolution (loop_num (loop_of_stmt (DR_STMT (dr))), 
	access_fn, &init, &step, true))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "too complicated access function\n");
	  print_generic_expr (dump_file, access_fn, TDF_SLIM);
	}
      return false;
    }

  return true;
}


/* Function vect_analyze_data_ref_accesses.

   Analyze the access pattern of all the data references in the loop.

   FORNOW: the only access pattern that is considered vectorizable is a
	   simple step 1 (consecutive) access.

   FORNOW: handle only one dimensional arrays.  */

static bool
vect_analyze_data_ref_accesses (loop_vec_info loop_vinfo)
{
  unsigned int i;
  varray_type loop_write_datarefs = LOOP_VINFO_DATAREF_WRITES (loop_vinfo);
  varray_type loop_read_datarefs = LOOP_VINFO_DATAREF_READS (loop_vinfo);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<vect_analyze_data_ref_accesses>>\n");

  for (i = 0; i < VARRAY_ACTIVE_SIZE (loop_write_datarefs); i++)
    {
      struct data_reference *dr = VARRAY_GENERIC_PTR (loop_write_datarefs, i);
      bool ok = vect_analyze_data_ref_access (dr);
      if (!ok)
	return false;
    }

  for (i = 0; i < VARRAY_ACTIVE_SIZE (loop_read_datarefs); i++)
    {
      struct data_reference *dr = VARRAY_GENERIC_PTR (loop_read_datarefs, i);
      bool ok = vect_analyze_data_ref_access (dr);
      if (!ok)
	return false;
    }

  return true;
}


/* Function vect_analyze_data_refs.

   Find all the data references in the loop.

   FORNOW: Handle only one dimensional ARRAY_REFs which base is really an
           array (not a pointer) which alignment can be forced. This
	   restriction will be relaxed.   */

static bool
vect_analyze_data_refs (loop_vec_info loop_vinfo)
{
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  basic_block *bbs = LOOP_VINFO_BBS (loop_vinfo);
  int nbbs = loop->num_nodes;
  block_stmt_iterator si;
  int j;
  struct data_reference *dr;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<vect_analyze_data_refs>>\n");

  for (j = 0; j < nbbs; j++)
    {
      basic_block bb = bbs[j];
      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
	{
	  bool is_read = false;
	  tree stmt = bsi_stmt (si);
	  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
	  vdef_optype vdefs = STMT_VDEF_OPS (stmt);
	  vuse_optype vuses = STMT_VUSE_OPS (stmt);
	  varray_type *datarefs = NULL;
	  int nvuses = 0, nvdefs = 0;
	  tree ref = NULL;
	  tree array_base;

	  /* CHECKME: Relying on the fact that there exists a data-ref
	     in stmt, if and only if it has vuses/vdefs.  */

	  if (!vuses && !vdefs)
	    continue;

	  if (vuses)
	    nvuses = NUM_VUSES (vuses);
	  if (vdefs)
	    nvdefs = NUM_VDEFS (vdefs);

	  if (nvuses + nvdefs != 1)
	    {
	      /* CHECKME: multiple vdefs/vuses in a GIMPLE stmt are
	         assumed to indicate a non vectorizable stmt (e.g, ASM,
	         CALL_EXPR) or the presence of an aliasing problem. The
	         first case is ruled out during vect_analyze_operations;
	         As for the second case, currently the vuses/vdefs are
	         meaningless as they are too conservative. We therefore
	         ignore them.  */

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
	          fprintf (dump_file, "Warning: multiple vops!\n");
	          print_generic_stmt (dump_file, stmt, 
			~(TDF_RAW | TDF_SLIM | TDF_LINENO));
		}
	    }

	  if (TREE_CODE (stmt) != MODIFY_EXPR)
	    {
	      /* CHECKME: a vdef/vuse in a GIMPLE stmt is assumed to
	         appear only in a MODIFY_EXPR.  */

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "unexpected vops in stmt\n");
		  print_generic_stmt (dump_file, stmt, TDF_SLIM);
		}
	      return false;
	    }

	  if (vuses)
	    {
	      if (TREE_CODE (TREE_OPERAND (stmt, 1)) == ARRAY_REF)
		{
		  ref = TREE_OPERAND (stmt, 1);
		  datarefs = &(LOOP_VINFO_DATAREF_READS (loop_vinfo));
		  is_read = true;
		}
	    }

	  if (vdefs)
	    {
	      if (TREE_CODE (TREE_OPERAND (stmt, 0)) == ARRAY_REF)
		{
		  ref = TREE_OPERAND (stmt, 0);
		  datarefs = &(LOOP_VINFO_DATAREF_WRITES (loop_vinfo));
		  is_read = false;
		}
	    }

	  if (!ref)
	    {
	      /* A different type of data reference (pointer?, struct?)
	         FORNOW: Do not attempt to handle.  */
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "unhandled non-array data ref\n");
		  print_generic_stmt (dump_file, stmt, TDF_SLIM);
		}
	      return false;
	    }

	  dr = analyze_array (stmt, ref, is_read);

	  array_base = TREE_OPERAND (ref, 0);

	  /* FORNOW: make sure that the array is one dimensional.
	     This restriction will be relaxed in the future.  */
	  if (TREE_CODE (array_base) == ARRAY_REF)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "unhandled 2D-array data ref\n");
		  print_generic_stmt (dump_file, stmt, TDF_SLIM);
		}
	      return false;
	    }

	  VARRAY_PUSH_GENERIC_PTR (*datarefs, dr);
	  STMT_VINFO_DATA_REF (stmt_info) = dr;
	}
    }

  return true;
}


/* Utility functions used by vect_mark_stmts_to_be_vectorized.
   Implementation inspired by tree-ssa-dce.c.  */

/* Function vect_mark_relevant.

   Mark STMT as "relevant for vectorization" and add it to WORKLIST.  */

static void
vect_mark_relevant (varray_type worklist, tree stmt)
{
  stmt_vec_info stmt_info;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "mark relevant.\n");

  if (TREE_CODE (stmt) == PHI_NODE)
    {
      VARRAY_PUSH_TREE (worklist, stmt);
      return;
    }

  stmt_info = vinfo_for_stmt (stmt);

  if (!stmt_info)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "mark relevant: no stmt info!!\n");
	  print_generic_expr (dump_file, stmt, TDF_SLIM);
	}
      return;
    }

  if (STMT_VINFO_RELEVANT_P (stmt_info))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "already marked relevant.\n");
      return;
    }

  STMT_VINFO_RELEVANT_P (stmt_info) = 1;
  VARRAY_PUSH_TREE (worklist, stmt);
}


/* Function vect_stmt_relevant_p.

   Return true if STMT in loop that is represented by LOOP_VINFO is
   "relevant for vectorization".

   A stmt is considered "relevant for vectorization" if:
   - it has uses outside the loop.
   - it has vdefs (it alters memory).
   - control stmts in the loop (except for the exit condition).

   CHECKME: what other side effects would the vectorizer allow?  */

static bool
vect_stmt_relevant_p (tree stmt, loop_vec_info loop_vinfo)
{
  vdef_optype vdefs;
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  int i;
  dataflow_t df;
  int num_uses;

  /* cond stmt other than loop exit cond.  */
  if (is_ctrl_stmt (stmt) && (stmt != LOOP_VINFO_EXIT_COND (loop_vinfo)))
    return true;

  /* changing memory.  */
  vdefs = STMT_VDEF_OPS (stmt);
  if (vdefs)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "vec_stmt_relevant_p: stmt has vdefs:\n");
      return true;
    }

  /* uses outside the loop.  */
  df = get_immediate_uses (stmt);
  num_uses = num_immediate_uses (df);
  for (i = 0; i < num_uses; i++)
    {
      tree use = immediate_use (df, i);
      basic_block bb = bb_for_stmt (use);
      if (!flow_bb_inside_loop_p (loop, bb))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, 
		"vec_stmt_relevant_p: used out of loop:\n");
	  return true;
	}
    }

  return false;
}


/* Function vect_mark_stmts_to_be_vectorized.

   Not all stmts in the loop need to be vectorized. For example:

     for i...
       for j...
   1.    T0 = i + j
   2.	 T1 = a[T0]

   3.    j = j + 1

   Stmt 1 and 3 do not need to be vectorized, because loop control and
   addressing of vectorized data-refs are handled differently.

   This pass detects such stmts.  */

static bool
vect_mark_stmts_to_be_vectorized (loop_vec_info loop_vinfo)
{
  varray_type worklist;
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  basic_block *bbs = LOOP_VINFO_BBS (loop_vinfo);
  unsigned int nbbs = loop->num_nodes;
  block_stmt_iterator si;
  tree stmt;
  stmt_ann_t ann;
  unsigned int i;
  int j;
  use_optype use_ops;
  stmt_vec_info stmt_info;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<vect_mark_stmts_to_be_vectorized>>\n");

  VARRAY_TREE_INIT (worklist, 64, "work list");

  /* 1. Init worklist.  */

  for (i = 0; i < nbbs; i++)
    {
      basic_block bb = bbs[i];
      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
	{
	  stmt = bsi_stmt (si);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "init: stmt relevant?\n");
	      print_generic_stmt (dump_file, stmt, TDF_SLIM);
	    } 

	  stmt_info = vinfo_for_stmt (stmt);
	  STMT_VINFO_RELEVANT_P (stmt_info) = 0;

	  if (vect_stmt_relevant_p (stmt, loop_vinfo))
	    vect_mark_relevant (worklist, stmt);
	}
    }


  /* 2. Process_worklist */

  while (VARRAY_ACTIVE_SIZE (worklist) > 0)
    {
      stmt = VARRAY_TOP_TREE (worklist);
      VARRAY_POP (worklist);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
          fprintf (dump_file, "worklist: examine stmt:\n");
          print_generic_stmt (dump_file, stmt, TDF_SLIM);
	}

      /* Examine the USES in this statement. Mark all the statements which
         feed this statement's uses as "relevant", unless the USE is used as
         an array index.  */

      if (TREE_CODE (stmt) == PHI_NODE)
	{
	  /* follow the def-use chain inside the loop.  */
	  for (j = 0; j < PHI_NUM_ARGS (stmt); j++)
	    {
	      tree arg = PHI_ARG_DEF (stmt, j);
	      if (TREE_CODE (arg) == SSA_NAME)
		{
		  tree def_stmt = NULL_TREE;
		  basic_block bb;

		  if (TREE_CODE (arg) == SSA_NAME)
		    def_stmt = SSA_NAME_DEF_STMT (arg);

		  if (def_stmt == NULL_TREE )
		    {
		      if (dump_file && (dump_flags & TDF_DETAILS))
		        fprintf (dump_file, "\nworklist: no def_stmt!\n");
		      varray_clear (worklist);
		      return false;
		    }

		  if (TREE_CODE (def_stmt) == NOP_EXPR)
		    {
          	      tree arg = TREE_OPERAND (def_stmt, 0);
		      if (TREE_CODE (arg) != INTEGER_CST
			  && TREE_CODE (arg) != REAL_CST)
			{
		          if (dump_file && (dump_flags & TDF_DETAILS))
		            fprintf (dump_file, "\nworklist: NOP def_stmt?\n");
		          varray_clear (worklist);
		          return false;
			}
		      continue;	
		    }

		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "\nworklist: def_stmt:\n");
		      print_generic_expr (dump_file, def_stmt, TDF_SLIM);
		    }

		  bb = bb_for_stmt (def_stmt);
		  if (flow_bb_inside_loop_p (loop, bb))
		    vect_mark_relevant (worklist, def_stmt);
		}
	    } 

	  continue;
	} 

      ann = stmt_ann (stmt);
      use_ops = USE_OPS (ann);

      for (i = 0; i < NUM_USES (use_ops); i++)
	{
	  tree use = USE_OP (use_ops, i);
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\nworklist: examine use %d:\n", i);
	      print_generic_expr (dump_file, use, TDF_SLIM);
	    }

	  if (exist_non_indexing_operands_for_use_p (use, stmt))
	    {
	      tree def_stmt = NULL_TREE;
	      basic_block bb;

	      if (TREE_CODE (use) == SSA_NAME)
		def_stmt = SSA_NAME_DEF_STMT (use);

	      if (def_stmt == NULL_TREE)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "\nworklist: no def_stmt!\n");
		  varray_clear (worklist);
		  return false;
		}

              if (TREE_CODE (def_stmt) == NOP_EXPR)
                {
                  tree arg = TREE_OPERAND (def_stmt, 0);
                  if (TREE_CODE (arg) != INTEGER_CST
                      && TREE_CODE (arg) != REAL_CST)
                    {
                      if (dump_file && (dump_flags & TDF_DETAILS))
                        fprintf (dump_file, "\nworklist: NOP def_stmt?\n");
                      varray_clear (worklist);
                      return false;
                    }
                  continue;
                }

	      if (dump_file && (dump_flags & TDF_DETAILS))	
		{
	          fprintf (dump_file, "\nworklist: def_stmt:\n");
	          print_generic_expr (dump_file, def_stmt, TDF_SLIM);
		}

	      bb = bb_for_stmt (def_stmt);
	      if (flow_bb_inside_loop_p (loop, bb))
		vect_mark_relevant (worklist, def_stmt);
	    }
	}

    }				/* while worklist */

  varray_clear (worklist);
  return true;
}

/* This function analyze number of iteration LOOP executes in case 
   the it is unknown number in compile time. 
   
   The vectoririze solution in this case is to duplicate loop so that 
   first loop will be vectorized, while its copy (second loop) won't.
   Initial conditions of loop copy (second loop) need to be updated.
   
   FORNOW: only loops with IVs which access functions are linear 
           can be duplicated.  */

static bool 
vect_analyze_loop_with_symbolic_num_of_iters (tree *symb_num_of_iters, 
					      struct loop *loop)
{
  tree niters;
  basic_block bb = loop->header;
  tree phi;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<vect_analyze_loop_with_symbolic_num_of_iters>>\n");
  
  niters = number_of_iterations_in_loop (loop);

  if (niters == chrec_top)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
          fprintf (dump_file, "\nInfinite number of iterations.\n");
      return false;
    }

  if (!niters)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
          fprintf (dump_file, "\nniters is NULL poiter.\n");
      return false;
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nSymbolic number of iterations is ");
      print_generic_expr (dump_file, niters, TDF_DETAILS);
    }

  if (chrec_contains_intervals (niters))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
          fprintf (dump_file, "\nniters contains interval.\n");
      return false;
    }

  /* debug_tree(niters); */ 
   
  /* Analyze phi functions of the loop header.  */

  for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
    {
      tree access_fn = NULL;
      tree evolution_part;

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
          fprintf (dump_file, "\nAnalyze phi\n");
          print_generic_expr (dump_file, phi, TDF_SLIM);
	}

      /* Skip virtual phi's. The data dependences that are associated with
         virtual defs/uses (i.e., memory accesses) are analyzed elsewhere.  */

      if (!is_gimple_reg (SSA_NAME_VAR (PHI_RESULT (phi))))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "virtual phi. skip.\n");
	  continue;
	}

      /* Analyze the evolution function.  */

      access_fn = instantiate_parameters
	(loop,
	 analyze_scalar_evolution (loop, PHI_RESULT (phi)));

      if (!access_fn)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "No Access function.");
	  return false;
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
        {
           fprintf (dump_file, "Access function of PHI: ");
           print_generic_expr (dump_file, access_fn, TDF_SLIM);
        }

      evolution_part = evolution_part_in_loop_num (access_fn, loop_num(loop));
      
      if (evolution_part == NULL_TREE)
	return false;
  
      /* FORNOW: We do not transform initial conditions of IVs 
	 which evolution functions are a polynomial of degree >= 2 or
	 exponential.  */

      if (TREE_CODE (evolution_part) == POLYNOMIAL_CHREC
	  || TREE_CODE (evolution_part) == EXPONENTIAL_CHREC)
	return false;  
    }

  *symb_num_of_iters = niters;
  return  true;
}


/* Function vect_get_loop_niters.

   Determine how many iterations the loop is executed.  */

static tree
vect_get_loop_niters (struct loop *loop, int *number_of_iterations)
{
  tree niters;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<get_loop_niters>>\n");

  niters = number_of_iterations_in_loop (loop);

  if (niters != NULL_TREE
      && TREE_CODE (niters) == INTEGER_CST)
    {
      *number_of_iterations = TREE_INT_CST_LOW (niters);

      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "get_loop_niters: %d.\n",
		*number_of_iterations);
    }

  return get_loop_exit_condition (loop);
}


/* Function vect_analyze_loop_form.

   Verify the following restrictions:
   Some of these maybe relaxed in the future.

   - it's an inner-most loop
   - number of BBs = 2 (which are the loop header and the latch)
   - the loop has a pre header
   - the loop has a single entry and exit
   - the loop exit condition is simple enough  */

static loop_vec_info
vect_analyze_loop_form (struct loop *loop)
{
  loop_vec_info loop_vinfo;
  tree loop_cond;
  int number_of_iterations = -1;
  tree symb_num_of_iters = NULL_TREE;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n<<vect_analyze_loop_form>>\n");

  if (loop->level > 1		/* FORNOW: inner-most loop (CHECKME)  */
      || loop->num_exits > 1 || loop->num_entries > 1 || loop->num_nodes != 2
      || !loop->pre_header || !loop->header || !loop->latch)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
	   "loop_analyzer: bad loop form (entry/exit, nbbs, level...)\n");
	  flow_loop_dump (loop, dump_file, NULL, 1);
	}

      return NULL;
    }

  loop_cond = vect_get_loop_niters (loop, &number_of_iterations);
  if (!loop_cond)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Complicated exit condition.\n");
      return NULL;
    }

  if (number_of_iterations < 0)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Can't determine num iters.\n");

      /* Treat loops with unknown loop bounds.  */
      if(!vect_analyze_loop_with_symbolic_num_of_iters (&symb_num_of_iters, loop))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "Can't determine loop bound.\n");
	  return NULL;
	}
    }

  /* CHECKME: check monev analyzer.  */
  if (number_of_iterations == 0)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "0 iterations??\n");
      return NULL;
    }

  loop_vinfo = new_loop_vec_info (loop);

  LOOP_VINFO_EXIT_COND (loop_vinfo) = loop_cond;
  LOOP_VINFO_NITERS (loop_vinfo) = number_of_iterations;
  LOOP_VINFO_SYMB_NUM_OF_ITERS(loop_vinfo) = symb_num_of_iters;

  return loop_vinfo;
}


/* Function vect_analyze_loop.

   Apply a set of analyses on LOOP, and create a loop_vec_info struct
   for it. The different analyses will record information in the
   loop_vec_info struct.  */

static loop_vec_info
vect_analyze_loop (struct loop *loop)
{
  bool ok;
  loop_vec_info loop_vinfo;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n\n\n<<<<<<< analyze_loop_nest >>>>>>>\n");

  /* Check the CFG characteristics of the loop (nesting, entry/exit, etc.  */

  loop_vinfo = vect_analyze_loop_form (loop);
  if (!loop_vinfo)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "loop_analyzer: bad loop form.\n");
      return NULL;
    }

  /* Find all data references in the loop (which correspond to vdefs/vuses)
     and analyze their evolution in the loop.

     FORNOW: Handle only simple, one-dimensional, array references, which
     alignment can be forced.  */

  ok = vect_analyze_data_refs (loop_vinfo);
  if (!ok)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "loop_analyzer: bad data references.\n");
      destroy_loop_vec_info (loop_vinfo);
      return NULL;
    }


  /* Data-flow analysis to detect stmts that do not need to be vectorized.  */

  ok = vect_mark_stmts_to_be_vectorized (loop_vinfo);
  if (!ok)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "loop_analyzer: unexpected pattern.\n");
      destroy_loop_vec_info (loop_vinfo);
      return NULL;
    }


  /* Check that all cross-iteration scalar data-flow cycles are OK.
     Cross-iteration cycles caused by virtual phis are analyzed separately.  */

  ok = vect_analyze_scalar_cycles (loop_vinfo);
  if (!ok)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "loop_analyzer: bad scalar cycle.\n");
      destroy_loop_vec_info (loop_vinfo);
      return NULL;
    }


  /* Analyze data dependences between the data-refs in the loop.
     FORNOW: We do not construct a data dependence graph and try to deal
     with dependences, but fail at the first data dependence that
     we encounter.  */

  ok = vect_analyze_data_ref_dependences (loop_vinfo);

  /* TODO: May want to generate run time pointer aliasing checks and
     loop versioning.  */

  /* TODO: May want to perform loop transformations to break dependence
     cycles.  */

  if (!ok)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "loop_analyzer: bad data dependence.\n");
      destroy_loop_vec_info (loop_vinfo);
      return NULL;
    }


  /* Analyze the access patterns of the data-refs in the loop (consecutive,
     complex, etc.). FORNOW: Only handle consecutive access pattern.  */

  ok = vect_analyze_data_ref_accesses (loop_vinfo);
  if (!ok)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "loop_analyzer: bad data access.\n");
      destroy_loop_vec_info (loop_vinfo);
      return NULL;
    }


  /* Analyze the alignment of the data-refs in the loop.
     FORNOW: Only aligned accesses are handled.  */

  ok = vect_analyze_data_refs_alignment (loop_vinfo);
  if (!ok)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "loop_analyzer: bad data alignment.\n");
      destroy_loop_vec_info (loop_vinfo);
      return NULL;
    }


  /* Scan all the operations in the loop and make sure they are
     vectorizable.  */

  ok = vect_analyze_operations (loop_vinfo);
  if (!ok)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "loop_analyzer: bad operations.\n");
      destroy_loop_vec_info (loop_vinfo);
      return NULL;
    }

  /* TODO: May want to collapse conditional code and loop versioning.   */

  /* TODO: Alignment: May want to perform loop peeling and/or run time
     tests and loop versioning.  */

  LOOP_VINFO_VECTORIZABLE_P (loop_vinfo) = 1;

  return loop_vinfo;
}


/* Function indicating whether we ought to include information for 'var'
   when calculating immediate uses.  For this pass we only want use
   information for non-virtual variables.  */

static bool
need_imm_uses_for (tree var)
{
  return is_gimple_reg (var);
}

/* Renames variables in new generated LOOP.  */

static void
vect_rename_variables_in_loop (struct loop *loop)
{
  unsigned i;
  basic_block *bbs;

  bbs = get_loop_body (loop);

  for (i = 0; i < loop->num_nodes; i++)
    {
      rename_variables_in_bb (bbs[i]);
    }

  free (bbs);
}

/* This function copies phis from loop to new_loop 
   as they were not generated by duplication of bbs.  */

static void
vect_copy_phi_nodes (struct loop *loop, struct loop *new_loop)
{
  tree phi, new_phi, def;
  edge new_e;
  edge latch = loop_latch_edge (loop);
  tree nlist;

     
  for (phi = phi_nodes (loop->header); phi; phi = TREE_CHAIN (phi))
    {
      new_phi = create_phi_node (PHI_RESULT (phi), new_loop->header);
      new_e = new_loop->header->pred;
      def = phi_element_for_edge (phi, latch)->def;
      add_phi_arg (&new_phi, def, new_e);
    }

  /* neverse phi nodes to keep them in original order.  */
  nlist = nreverse (phi_nodes (new_loop->header));
  set_phi_nodes (new_loop->header, nlist);

}

/* This funtion generate pure copy of LOOP 
   and locate it immediately after given LOOP.
   It fixes phis of copy loop so that they inherite 
   one of thier values from exit edge of initil LOOP.  */

static bool
vect_tree_duplicate_loop (struct loop *loop, struct loops *loops)
{
  struct loop *new_loop = NULL;
  tree definitions;
  edge pred;
  tree *new_names, new_var;
  tree phi, def;
  unsigned first_new_block;
  ssa_name_ann_t ann;

  definitions = collect_defs (loop);

  first_new_block = last_basic_block;
  if(!vect_duplicate_loop (loop, loops, &new_loop))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "vect_duplicate_loop is failed.\n");
      return false;
    }

  if(!new_loop)
    abort();


  allocate_new_names (definitions, 1, false);

  /* Copy phis from loop->header to new_loop->header.  */
  vect_copy_phi_nodes (loop, new_loop);

  /* Rename the variables.  */
  vect_rename_variables_in_loop (new_loop);

  /* Fix phis to inherit values from loop exit edge.  */
  for (phi = phi_nodes (new_loop->header); phi; phi = TREE_CHAIN (phi))
    {
      pred = new_loop->header->pred;
      def = phi_element_for_edge (phi, pred)->def;

      if (TREE_CODE (def) != SSA_NAME)
	continue;

      ann = ssa_name_ann (def);
      new_names = ann ? ann->common.aux : NULL;

      /* Something defined outside of the loop.  */
      if (!new_names)
	continue;

      /* An ordinary ssa name defined in the loop.  */
      new_var = new_names[new_loop->header->rbi->copy_number];
      
      add_phi_arg (&phi, new_var, loop_latch_edge(new_loop));
    }


  free_new_names (definitions, false);

  return true;
}

/* This function: 
   - copies basic blocks of the loop LOOP;
   - locate them at the only exit of LOOP; 
   - redirect edges so that two loops are produced: 
             initial LOOP and newly generated;
   - update dominators;
   - returns pointer to new loop in NEW_LOOP_P.

   FORNOW: only innermost loops with 
           1 exit are handled. 
*/

static bool
vect_duplicate_loop (struct loop *loop, struct loops *loops, struct loop **new_loop_p)
{
  struct loop *target;
  basic_block latch = loop->latch; 
  basic_block *new_bbs, *bbs;
  edge latch_edge;

  unsigned i; 
  unsigned  n = loop->num_nodes;
  struct loop *new_loop;
  basic_block exit_dest; 

  bbs = get_loop_body (loop);

  /* Check whether duplication is possible.  */
  if (!can_copy_bbs_p (bbs, loop->num_nodes))
    {
      free (bbs);
      return false;
    }
  new_bbs = xmalloc (sizeof (basic_block) * loop->num_nodes);

  /* Find edge from latch.  */
  latch_edge = loop_latch_edge (loop);

  /* We duplicate only innermost loops */
  if(loop->inner)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "Loop duplication failed. Loop is not innermost.\n");
      free (bbs);
      return false;
    }

  /* FORNOW: only loops with 1 exit. */
  if(loop->num_exits != 1)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "More than one exit from loop.\n");
      return false;
    }    

  /* Loop the new bbs will belong to.  */
  target = loop->outer;
  if(!target)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "Loop is outer-most loop.\n");
      return false;
    }    
    
  /* Generate new loop structure. */
  new_loop = duplicate_loop (loops, loop, target); 
  if(!new_loop)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "duplicate_loop returns NULL.\n");
      return false;

    }
  /* FIXME: Should we copy contents of the loop structure
     to the new loop?  */

  copy_bbs (bbs, n, new_bbs, NULL, 0, NULL, NULL);
  for (i = 0; i < n; i++)
    new_bbs[i]->rbi->copy_number = 1;

  /* Redirect the special edges.  */
  exit_dest = loop->exit_edges[0]->dest;
  if(!exit_dest)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "First exit basic block of loop is NULL.\n");
      return false;
    }
    
  
  redirect_edge_and_branch_force (loop->exit_edges[0],
				  new_bbs[0]);
  set_immediate_dominator (CDI_DOMINATORS, new_bbs[0], latch);
  set_immediate_dominator (CDI_DOMINATORS, exit_dest, new_loop->latch);
  
  free (new_bbs);
  free (bbs);


  *new_loop_p = new_loop;
  return true;

}

/* Function vectorize_loops.
   Entry Point to loop vectorization phase.  */

void
vectorize_loops (struct loops *loops)
{
  unsigned int i, loops_num;
  unsigned int num_vectorized_loops = 0;

  /* Does the target support SIMD?  */
  /* FORNOW: until more sophisticated machine modelling is in place.  */
  if (!UNITS_PER_SIMD_WORD)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "vectorizer: target vector size is not defined.\n");
      return;
    }

  compute_immediate_uses (TDFA_USE_OPS, need_imm_uses_for);

  /*  ----------- Analyze loops. -----------  */
  /* CHECKME */

  /* If some loop was duplicated, it gets bigger number 
     than all previously defined loops. This fuct allows us to run 
     only over intial loops skipping newly generated ones.  */
  loops_num = loops->num;
  for (i = 1; i < loops_num; i++)
    {
      loop_vec_info loop_vinfo;
      struct loop *loop = loops->parray[i];

      flow_loop_scan (loop, LOOP_ALL);

      loop_vinfo = vect_analyze_loop (loop);
      loop->aux = loop_vinfo;

#ifndef ANALYZE_ALL_THEN_VECTORIZE_ALL
      if (!loop_vinfo || !LOOP_VINFO_VECTORIZABLE_P (loop_vinfo))
	continue;

	vect_transform_loop (loop_vinfo, loops); 
      num_vectorized_loops++;
#endif
    }

#ifdef ANALYZE_ALL_THEN_VECTORIZE_ALL
  for (i = 1; i < loops_num; i++)
    {
      struct loop *loop = loops->parray[i];
      loop_vec_info loop_vinfo = loop->aux;

      if (!loop_vinfo || !LOOP_VINFO_VECTORIZABLE_P (loop_vinfo))
	continue;

      vect_transform_loop (loop_vinfo,loops);
      num_vectorized_loops++;
    }
#endif

  if (dump_file && (dump_flags & TDF_STATS))
    fprintf (dump_file, "vectorized %u loops in function.\n",
	     num_vectorized_loops);

  /*  ----------- Finialize. -----------  */

  free_df ();
  for (i = 1; i < loops_num; i++)
    {
      struct loop *loop = loops->parray[i];
      loop_vec_info loop_vinfo = loop->aux;
      destroy_loop_vec_info (loop_vinfo);
      loop->aux = NULL;
    }
}
