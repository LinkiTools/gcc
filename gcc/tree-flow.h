/* Data and Control Flow Analysis for Trees.
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

#ifndef _TREE_FLOW_H
#define _TREE_FLOW_H 1

#include "bitmap.h"
#include "basic-block.h"
#include "tree-simple.h"

/*---------------------------------------------------------------------------
			    Types of references

  The compiler tracks references to variables (scalars, arrays and
  structures), indicated with a V_ prefix, and references to expressions
  (function calls, arithmetic expressions, etc), indicated with an E_
  prefix.

  The basic semantics of each referenc type can be altered using the
  modifiers defined below (M_* constants).  These modifiers provide more
  information for optimizers when making transformations.
---------------------------------------------------------------------------*/

/* A V_DEF reference represents a write operation to the associated
   variable.  If no modifier alters the reference, the reference represents
   a killing definition of the associated variable via an assignment
   expression (i.e., all the bits of the variable are modified).  Note that
   unmodified V_DEF references are only allowed for MODIFY_EXPR and
   INIT_EXPR expressions.
   
   In this case, this reference will represent the output value of the
   associated expression.  For instance, 'a = 3' creates a V_DEF reference
   for 'a' and calling output_ref('a = 3') returns this V_DEF.  */
extern const HOST_WIDE_INT V_DEF;

/* A V_USE reference represents a read operation from the associated
   variable.  If no modifier alters the reference, the reference represents
   a full read operation on the variable (i.e., all the bits of the
   variable are read).  */
extern const HOST_WIDE_INT V_USE;

/* A V_PHI represents an SSA PHI operation on the associated variable.  */
extern const HOST_WIDE_INT V_PHI;

/* The following references are akin to the previous types but used
   when building SSA information for expressions instead of variables
   (see tree-ssa-pre.c)  */
extern const HOST_WIDE_INT E_PHI;
extern const HOST_WIDE_INT E_USE;
extern const HOST_WIDE_INT E_KILL;


/*---------------------------------------------------------------------------
			 Reference type modifiers

  These modifiers are OR'd with the basic reference types to alter the
  semantics of the reference.  For instance, V_DEF|M_CLOBBER means that the
  reference modifies the associated variable in ways that the compiler
  cannot determine.
---------------------------------------------------------------------------*/
/* M_DEFAULT is used to modify V_DEF references to indicate a default
   definition.  Default definitions are artificially created in the first
   basic block of the program.  They provide a convenient way of checking
   if a variable is used without being assigned a value first.  Their
   presence is not required, but they save the code from having to consider
   special cases like nil PHI node arguments.  */
extern const HOST_WIDE_INT M_DEFAULT;

/* M_CLOBBER is used to modify V_DEF references to represent an unknown
   modification to the associated variable.  This is used for instructions
   like __asm__ statements where we know that the variable is being
   modified but not how.  Another case is at function call sites.
   Variables that may be accessed by the called function are assumed read
   and clobbered by the call.  */
extern const HOST_WIDE_INT M_CLOBBER;

/* M_MAY is used to represent references that may or may not occur at
   runtime.  It is generated to model variable references in statements or
   expressions that the compiler does not understand (e.g., non-simplified
   tree nodes).

   A may-def and may-use reference are created to all the symbols
   referenced in the expression.  This models the possibility that the
   instruction may use and modify the variable.  */
extern const HOST_WIDE_INT M_MAY;

/* M_PARTIAL is used to model partial references to compound structures
   like arrays, structures and complex numbers.  For instance, given

   		a[4] = 10;

   The compiler creates a V_DEF for 'a[4]' and a V_DEF|M_PARTIAL for 'a'.
   Partial definitions are also known as non-killing definitions in the
   literature.  */
extern const HOST_WIDE_INT M_PARTIAL;

/* M_INITIAL modifies a V_DEF reference to indicate that the definition is
   an initial static value for the variable.  Multiple executions of this
   reference do not produce multiple definitions of the variable.  This is
   used to model static variables in C.  For instance,

   		static int counter = 0;

   An initializing definition is created for variable counter.  */
extern const HOST_WIDE_INT M_INITIAL;

/* M_VOLATILE modifies a V_DEF or V_USE reference to indicate that it is
   accessing a volatile variable.  Therefore, optimizers should not assume
   anything about it.  For instance,

   		volatile int a = 5;
		int b = a;

   In the above code fragment, we cannot assume that 'b' is assigned the
   value 5.  */
extern const HOST_WIDE_INT M_VOLATILE;

/* M_RELOCATE modifies a V_DEF of a pointer dereference to indicate that
   the base pointer is now pointing to a different memory location.  This
   definition should reach dereferences of the pointer, but it should not
   reach uses of any aliases (see ref_defines).  */
extern const HOST_WIDE_INT M_RELOCATE;

/* M_ADDRESSOF modifies a V_USE reference to indicate that the address of
   the variable is needed.  This is not a memory load operation, just an
   indication that we need the address of the variable being referenced.  */
extern const HOST_WIDE_INT M_ADDRESSOF;


/*---------------------------------------------------------------------------
		 Doubly linked list of variable references
---------------------------------------------------------------------------*/
union tree_ref_d;

struct ref_list_node
{
  union tree_ref_d *ref;
  struct ref_list_node *prev;
  struct ref_list_node *next;
};

struct ref_list_priv
{
  struct ref_list_node *first;
  struct ref_list_node *last;
};
typedef struct ref_list_priv *ref_list;


/* Iterators for reference lists.  */
#define FOR_REF_BETWEEN(REF, TMP, FROM, TO, DIR)		\
  if (FROM) \
  for (TMP = FROM, REF = TMP->ref;  TMP != TO; TMP = TMP->DIR, REF = (TMP ? TMP->ref : NULL))

#define FOR_EACH_REF(REF, TMP, LIST)				\
  FOR_REF_BETWEEN (REF, TMP, LIST->first, LIST->last->next, next)

#define FOR_EACH_REF_REV(REF, TMP, LIST)			\
  FOR_REF_BETWEEN (REF, TMP, LIST->last, LIST->first->prev, prev)


/*---------------------------------------------------------------------------
			      Variable references
---------------------------------------------------------------------------*/
/* Common features of every variable reference.  */
struct tree_ref_common
{
  /* Reference type.  */
  HOST_WIDE_INT type;

  /* Variable being referenced.  This may be a _DECL or an INDIRECT_REF
     node.  */
  tree var;

  /* Statement containing the reference.  Maybe NULL for special references
     (e.g., default definitions inserted at the start of every function).  */
  tree stmt;

  /* Expression tree containing the reference.  Maybe NULL for special
     references (e.g., default definitions inserted at the start of every
     function).  */
  tree expr;

  /* Pointer to operand of EXPR containing VAR.  Used when substituting the
     operand with some other value in transformations like constant
     propagation.  Maybe NULL for special references (e.g., default
     definitions inserted at the start of every function).  */
  tree *operand_p;

  /* Original value stored in *OPERAND_P.  Used by restore_ref_operand.  */
  tree orig_operand;

  /* Basic block containing the reference.  */
  basic_block bb;

  /* Reference ID.  Unique within a single function.  */
  unsigned long id;
};

/* Generic variable references.  */
struct var_ref
{
  struct tree_ref_common common;

  /* Immediate reaching definition for this reference.  This is applicable
     to both variable definitions and uses because we are interested in
     building def-def chains (for non-killing definitions).  */
  union tree_ref_d *imm_rdef;

  /* Immediate reaching definitions for all the may-aliases of this
     reference.  This array is setup so that the Ith entry corresponds to
     the Ith alias of the variable associated to this reference (i.e., this
     is the Ith entry of the array MAY_ALIASES in struct tree_ann_d).  */
  union tree_ref_d **alias_imm_rdefs;
};

/* Variable definitions.  */
struct var_def
{
  struct var_ref common;

  /* Immediate uses for this definition.  */
  ref_list imm_uses;

  /* Uses reached by this definition.  */
  ref_list reached_uses;
};

/* Variable PHIs.  */
struct var_phi
{
  struct var_def common;

  /* Array of PHI arguments.  The number of arguments to a PHI node is the
     number of incoming edges to the basic block where that PHI node
     resides.  Each member of the array is of type phi_node_arg.  */
  varray_type phi_args;
};

/* Variable uses.  */
struct var_use
{
  struct var_ref common;

  /* Definitions reaching this use.  */
  ref_list rdefs;
};

/* PHI arguments.

   NOTE: These are not regular tree_ref objects!  We used to model them as
   just another tree_ref, but the space overhead for jumpy functions with
   many PHI nodes and arguments was horrible.
   
   All yyparse() functions in the different front ends were causing cc1 to
   grow to the 100-300 Mb range.  Furthermore, the number of references
   would grow into the millions, making the optimizers waste unnecessary
   cycles when traversing all the references in the function.  */
struct phi_node_arg_d
{
  /* Immediate reaching definition for this argument.  */
  union tree_ref_d *def;

  /* Incoming edge where we are receiving imm_rdef from.  */
  edge e;
};

typedef struct phi_node_arg_d *phi_node_arg;

/*---------------------------------------------------------------------------
			     Expression references
---------------------------------------------------------------------------*/
/* Common feature of all expression references.  */
struct expr_ref_common
{
  struct tree_ref_common common;

  /* SSAPRE: True if expression needs to be saved to a temporary. */
  unsigned int save:1;
  
  /* SSAPRE: True if expression needs to be reloaded from a temporary.  */
  unsigned int reload:1;

  /* SSAPRE: True if expression was inserted as a PHI operand occurrence.  */
  unsigned int inserted:1;
  
  /* SSAPRE: Redundancy class of expression.  */
  unsigned int class;
};



/* Expression PHIs.  */
struct expr_phi
{
  struct expr_ref_common common;
  
  /* Expression PHI operands.  This is an array of size
     last_basic_block, with operands ordered by pre-order index of the
     BB it's from.  */
  varray_type phi_args;

  /* SSAPRE: True if PHI is downsafe.  */
  unsigned int downsafe:1;
  
  /* SSAPRE: True if PHI is can_be_avail.  */
  unsigned int can_be_avail:1;

  /* SSAPRE: True if PHI is later.  */
  unsigned int later:1;

  /* SSAPRE: True if PHI is expression.  */
  unsigned int extraneous:1;

  /* SSAPRE: Bitmap to track which operands we've processed during
     various algorithms.  */
  bitmap processed;
};



/* Expressions uses.  */
struct expr_use
{
  struct expr_ref_common common;

  /* Definition chain.  */
  union tree_ref_d *def;
  
  /* True if this use is a phi operand occurrence. */
  int op_occurrence;

  /* SSAPRE: True if this is an operand, and it has a real use. */
  int has_real_use;
};

#define EXPRUSE_DEF(r) (r)->euse.def
#define EXPRUSE_PHIOP(r) (r)->euse.op_occurrence
#define EXPRUSE_HAS_REAL_USE(r) (r)->euse.has_real_use


/* Generic variable reference structure.  */
union tree_ref_d
{
  struct tree_ref_common common;
  struct var_ref vref;
  struct var_def vdef;
  struct var_phi vphi;
  struct var_use vuse;
  struct expr_ref_common ecommon;
  struct expr_use euse;
  struct expr_phi ephi;
};

typedef union tree_ref_d *tree_ref;


/*---------------------------------------------------------------------------
		    Accessor functions for tree_ref objects.
---------------------------------------------------------------------------*/
/* For tree_ref_common.  */
static inline HOST_WIDE_INT ref_type		PARAMS ((tree_ref));
static inline tree ref_var			PARAMS ((tree_ref));
static inline tree ref_stmt			PARAMS ((tree_ref));
static inline tree ref_expr			PARAMS ((tree_ref));
static inline basic_block ref_bb		PARAMS ((tree_ref));
static inline unsigned long ref_id		PARAMS ((tree_ref));
static inline void replace_ref_operand_with	PARAMS ((tree_ref, tree));
static inline void restore_ref_operand		PARAMS ((tree_ref));


/* For var_ref.  */
static inline ref_list imm_uses			PARAMS ((tree_ref));
static inline ref_list reached_uses		PARAMS ((tree_ref));
static inline tree_ref imm_reaching_def		PARAMS ((tree_ref));
static inline void set_imm_reaching_def		PARAMS ((tree_ref, tree_ref));
static inline tree_ref alias_imm_reaching_def	PARAMS ((tree_ref, size_t));
static inline ref_list reaching_defs		PARAMS ((tree_ref));
static inline varray_type phi_args		PARAMS ((tree_ref));
static inline unsigned int num_phi_args		PARAMS ((tree_ref));
static inline phi_node_arg phi_arg		PARAMS ((tree_ref, unsigned));
static inline void set_phi_arg			PARAMS ((tree_ref, unsigned,
                                                         phi_node_arg));
extern void add_phi_arg				PARAMS ((tree_ref, tree_ref,
                                                         edge));

/* For phi_node_arg.  */
static inline edge phi_arg_edge			PARAMS ((phi_node_arg));
static inline void set_phi_arg_edge		PARAMS ((phi_node_arg, edge));
static inline tree_ref phi_arg_def		PARAMS ((phi_node_arg));
static inline void set_phi_arg_def		PARAMS ((phi_node_arg,
      							 tree_ref));

/* For exprref. */
static inline void set_exprref_class PARAMS ((tree_ref, unsigned int));
static inline unsigned int exprref_class PARAMS ((tree_ref));
static inline void set_exprref_inserted PARAMS ((tree_ref, unsigned int));
static inline bool exprref_inserted PARAMS ((tree_ref));
static inline void set_exprref_save PARAMS ((tree_ref, unsigned int));
static inline bool exprref_save PARAMS ((tree_ref));
static inline void set_exprref_reload PARAMS ((tree_ref, unsigned int));
static inline bool exprref_reload PARAMS ((tree_ref));

/* For expruse. */
static inline void set_expruse_def PARAMS ((tree_ref, tree_ref));
static inline tree_ref expruse_def PARAMS ((tree_ref));
static inline void set_expruse_phiop PARAMS ((tree_ref, unsigned int));
static inline bool expruse_phiop PARAMS ((tree_ref));
static inline void set_expruse_has_real_use PARAMS ((tree_ref, unsigned int));
static inline bool expruse_has_real_use PARAMS ((tree_ref));

/* For exprphi. */
static inline void set_exprphi_phi_args PARAMS ((tree_ref, varray_type));
static inline varray_type exprphi_phi_args PARAMS ((tree_ref));
static inline void set_exprphi_downsafe PARAMS ((tree_ref, unsigned int));
static inline bool exprphi_downsafe PARAMS ((tree_ref));
static inline void set_exprphi_canbeavail PARAMS ((tree_ref, unsigned int));
static inline bool exprphi_canbeavail PARAMS ((tree_ref));
static inline void set_exprphi_later PARAMS ((tree_ref, unsigned int));
static inline bool exprphi_later PARAMS ((tree_ref));
static inline void set_exprphi_extraneous PARAMS ((tree_ref, unsigned int));
static inline bool exprphi_extraneous PARAMS ((tree_ref));
static inline void set_exprphi_processed PARAMS ((tree_ref, bitmap));
static inline bitmap exprphi_processed PARAMS ((tree_ref));
static inline bool exprphi_willbeavail PARAMS ((tree_ref));


/*---------------------------------------------------------------------------
		   Tree annotations stored in tree_common.aux
---------------------------------------------------------------------------*/
struct tree_ann_d
{
  /* Basic block that contains this tree.  */
  basic_block bb;

  /* For _DECL trees, list of references made to this variable.  For
     statement trees trees, list of references made in this statement.  For
     first-level SIMPLE expressions (i.e., the topmost expression of a
     _STMT node), list of references made in this expression.  */
  ref_list refs;

  /* For _DECL trees this is the most recent definition for this variable.
     Used when placing FUD chains.  */
  tree_ref currdef;

  /* Virtual variable used to represent dereferences to a pointer.  For
     every pointer PTR, this is an INDIRECT_REF tree representing *PTR.
     See the rationale for this in the handler for INDIRECT_REF nodes in
     find_refs_in_expr.  */
  tree indirect_var;

  /* Flags used to mark optimization-dependent state.  See TF_* below.  */
  HOST_WIDE_INT flags;

  /* Output reference.  This is the V_DEF reference at the LHS of
     assignments (MODIFY_EXPR and INIT_EXPR).  */
  tree_ref output_ref;

  /* Set of variables that may be aliases of this variable.  */
  varray_type may_aliases;
};

typedef struct tree_ann_d *tree_ann;

/* Tree flags.  */
enum tree_flags
{
  /* Expression tree should be folded.  */
  TF_FOLDED		= 1 << 0,

  /* This _DECL node has already been referenced in this function.  */
  TF_REFERENCED		= 1 << 1,

  /* This expression is necessary (not dead code).  */
  TF_NECESSARY		= 1 << 2
};

static inline tree_ann tree_annotation	PARAMS ((tree));
static inline basic_block bb_for_stmt	PARAMS ((tree));
static inline tree_ref currdef_for	PARAMS ((tree));
static inline void set_currdef_for	PARAMS ((tree, tree_ref));
static inline ref_list tree_refs	PARAMS ((tree));
static inline void add_tree_ref		PARAMS ((tree, tree_ref));
static inline void remove_tree_ref	PARAMS ((tree, tree_ref));
static inline void set_tree_flag	PARAMS ((tree, enum tree_flags));
static inline void clear_tree_flag	PARAMS ((tree, enum tree_flags));
static inline enum tree_flags tree_flags PARAMS ((tree));
static inline void reset_tree_flags	PARAMS ((tree));
static inline tree_ref output_ref	PARAMS ((tree));
static inline void set_output_ref	PARAMS ((tree, tree_ref));
static inline tree indirect_var		PARAMS ((tree));
static inline void set_indirect_var	PARAMS ((tree, tree));
static inline tree may_alias		PARAMS ((tree, size_t));
static inline size_t num_may_alias	PARAMS ((tree));
static inline int get_lineno		PARAMS ((tree));

/*---------------------------------------------------------------------------
		  Block annotations stored in basic_block.aux
---------------------------------------------------------------------------*/
struct bb_ann_def
{
  /* Control flow parent.  This is the entry block to the control structure
     to which this block belongs to.  */
  basic_block parent_block;

  /* List of references made in this block.  */
  ref_list refs;
};

typedef struct bb_ann_def *bb_ann;

/* Accessors for basic block annotations.  */
static inline bb_ann bb_annotation	PARAMS ((basic_block));
static inline basic_block parent_block	PARAMS ((basic_block));
static inline void set_parent_block	PARAMS ((basic_block, basic_block));
static inline ref_list bb_refs		PARAMS ((basic_block));
static inline void remove_bb_ref	PARAMS ((basic_block, tree_ref));

/* Some basic blocks are nothing but markers used to give structure to the
   flow graph (see make_while_stmt_blocks).  They contain no useful
   instructions.  */
static inline bool bb_empty_p		PARAMS ((basic_block));
static inline bool
bb_empty_p (b)
     basic_block b;
{
  return b->head_tree == empty_stmt_node;
}


/* Counters updated every time we allocate a new object.  Used to compare
   against the counts collected by collect_dfa_stats.  */
struct dfa_counts_d
{
  unsigned long num_phi_args;
  unsigned long num_may_alias;
  unsigned long num_alias_imm_rdefs;
};


/* Iterator object for statements inside a basic block.  See
   gimple_stmt_iterator in tree-simple.h.  */
static inline void gsi_step_bb PARAMS ((gimple_stmt_iterator *));
static inline gimple_stmt_iterator gsi_start_bb	PARAMS ((basic_block));

#if 0
/* FIXME Not implemented yet.  */
extern void gsi_insert_before (tree stmt, gimple_stmt_iterator, basic_block);
extern void gsi_insert_after (tree stmt, gimple_stmt_iterator, basic_block);
extern void gsi_delete (gimple_stmt_iterator, basic_block);
extern void gsi_replace (tree stmt, gimple_stmt_iterator, basic_block);
#endif


/* Global declarations.  */

/* Nonzero to warn about variables used before they are initialized.  */
extern int tree_warn_uninitialized;

/* Array of all variables referenced in the function.  */
extern varray_type referenced_vars;

/* List of all call sites in the current function.  */
extern ref_list call_sites;

/* Next unique reference ID to be assigned by create_ref().  */
extern unsigned long next_tree_ref_id;

/* Artificial variable used to model the effects of function calls.  */
extern tree global_var;

/* Accessors for the referenced_vars array.  */
extern unsigned long num_referenced_vars;

static inline tree referenced_var PARAMS ((size_t));
static inline tree
referenced_var (i)
     size_t i;
{
  return VARRAY_TREE (referenced_vars, i);
}


/* In tree-cfg.c  */
extern void build_tree_cfg		PARAMS ((tree));
extern void delete_tree_cfg		PARAMS ((void));
extern bool is_ctrl_stmt		PARAMS ((tree));
extern bool is_ctrl_altering_stmt	PARAMS ((tree));
extern bool is_loop_stmt		PARAMS ((tree));
extern bool is_computed_goto		PARAMS ((tree));
extern tree loop_body			PARAMS ((tree));
extern void set_loop_body		PARAMS ((tree, tree));
extern bool stmt_starts_bb_p		PARAMS ((tree));
extern bb_ann create_bb_ann 		PARAMS ((basic_block));
extern void tree_dump_bb		PARAMS ((FILE *, const char *,
						 basic_block, int));
extern void tree_debug_bb		PARAMS ((basic_block));
extern void tree_dump_cfg		PARAMS ((FILE *));
extern void tree_debug_cfg		PARAMS ((void));
extern void tree_cfg2dot		PARAMS ((FILE *));
extern void validate_loops		PARAMS ((struct loops *));
extern void insert_stmt_before		PARAMS ((tree, tree, basic_block));
extern void insert_stmt_after		PARAMS ((tree, tree, basic_block));
extern void replace_expr_in_tree	PARAMS ((tree, tree, tree));
extern tree *find_expr_in_tree		PARAMS ((tree, tree));
extern void insert_bb_before		PARAMS ((basic_block, basic_block));
extern void tree_cleanup_cfg		PARAMS ((void));
extern tree first_stmt			PARAMS ((basic_block));
extern tree last_stmt			PARAMS ((basic_block));


/* In tree-dfa.c  */
extern void tree_find_refs		PARAMS ((void));
extern void find_refs_in_stmt           PARAMS ((tree, basic_block));
extern void remove_tree_ann		PARAMS ((tree));
extern tree_ann create_tree_ann 	PARAMS ((tree));
extern tree_ref create_ref		PARAMS ((tree, HOST_WIDE_INT,
						 basic_block, tree, tree,
						 tree *, int));
extern void debug_ref			PARAMS ((tree_ref));
extern void dump_ref			PARAMS ((FILE *, const char *, tree_ref,
      						 int, int));
extern void debug_ref_list		PARAMS ((ref_list));
extern void debug_ref_array		PARAMS ((varray_type));
extern void debug_phi_args		PARAMS ((varray_type));
extern void dump_ref_list		PARAMS ((FILE *, const char *, ref_list,
      						 int, int));
extern void dump_ref_array		PARAMS ((FILE *, const char *,
                                                 varray_type, int, int));
extern void dump_phi_args		PARAMS ((FILE *, const char *,
      						 varray_type, int, int));
extern void dump_dfa_stats		PARAMS ((FILE *));
extern void debug_dfa_stats		PARAMS ((void));
extern void debug_referenced_vars	PARAMS ((void));
extern void dump_referenced_vars	PARAMS ((FILE *));
extern void dump_variable		PARAMS ((FILE *, tree));
extern void debug_variable		PARAMS ((tree));
extern int function_may_recurse_p	PARAMS ((void));
extern ref_list create_ref_list		PARAMS ((void));
extern void empty_ref_list		PARAMS ((ref_list));
extern void delete_ref_list		PARAMS ((ref_list));
extern void add_ref_to_list_end		PARAMS ((ref_list, tree_ref));
extern void add_ref_to_list_begin	PARAMS ((ref_list, tree_ref));
extern void add_ref_to_list_after	PARAMS ((ref_list,
						 struct ref_list_node *,
					         tree_ref));
extern void add_list_to_list_end        PARAMS ((ref_list, ref_list));
extern void add_list_to_list_begin      PARAMS ((ref_list, ref_list));
extern void remove_ref_from_list	PARAMS ((ref_list, tree_ref));
extern struct ref_list_node *find_list_node PARAMS ((ref_list, tree_ref));
extern const char *ref_type_name	PARAMS ((HOST_WIDE_INT));
extern bool validate_ref_type		PARAMS ((HOST_WIDE_INT));
extern bool ref_defines			PARAMS ((tree_ref, tree));
extern bool is_killing_def		PARAMS ((tree_ref, tree_ref));
extern int get_alias_index		PARAMS ((tree, tree));


/* In tree-ssa.c  */
extern void build_tree_ssa		PARAMS ((tree));
extern void delete_tree_ssa		PARAMS ((tree));
extern void tree_compute_rdefs		PARAMS ((void));
extern void analyze_rdefs		PARAMS ((void));
extern void tree_ssa_remove_phi_alternative PARAMS ((tree_ref, basic_block));
extern void dump_reaching_defs		PARAMS ((FILE *));
extern void debug_reaching_defs		PARAMS ((void));
extern void dump_tree_ssa		PARAMS ((FILE *));
extern void debug_tree_ssa		PARAMS ((void));


/* In tree-alias-steen.c  */
extern void create_alias_vars		PARAMS ((void));


/* In tree-ssa-pre.c  */
extern void tree_perform_ssapre PARAMS ((void));


/* In tree-ssa-ccp.c  */
void tree_ssa_ccp PARAMS ((tree));


/* In tree-ssa-dce.c  */
void tree_ssa_eliminate_dead_code PARAMS ((tree));

#include "tree-flow-inline.h"

#endif /* _TREE_FLOW_H  */
