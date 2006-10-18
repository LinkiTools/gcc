/* Rewrite a program in Normal form into SSA.
   Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

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
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "flags.h"
#include "rtl.h"
#include "tm_p.h"
#include "langhooks.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "expr.h"
#include "function.h"
#include "diagnostic.h"
#include "bitmap.h"
#include "tree-flow.h"
#include "tree-gimple.h"
#include "tree-inline.h"
#include "varray.h"
#include "timevar.h"
#include "hashtab.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "cfgloop.h"
#include "domwalk.h"
#include "ggc.h"
#include "params.h"
#include "vecprim.h"

/* This file builds the SSA form for a function as described in:
   R. Cytron, J. Ferrante, B. Rosen, M. Wegman, and K. Zadeck. Efficiently
   Computing Static Single Assignment Form and the Control Dependence
   Graph. ACM Transactions on Programming Languages and Systems,
   13(4):451-490, October 1991.  */

/* True if the code is in SSA form.  */
bool in_ssa_p;

/* Structure to map a variable VAR to the set of blocks that contain
   definitions for VAR.  */
struct def_blocks_d
{
  /* The variable.  */
  tree var;

  /* Blocks that contain definitions of VAR.  Bit I will be set if the
     Ith block contains a definition of VAR.  */
  bitmap def_blocks;

  /* Blocks that contain a PHI node for VAR.  */
  bitmap phi_blocks;

  /* Blocks where VAR is live-on-entry.  Similar semantics as
     DEF_BLOCKS.  */
  bitmap livein_blocks;
};


/* Each entry in DEF_BLOCKS contains an element of type STRUCT
   DEF_BLOCKS_D, mapping a variable VAR to a bitmap describing all the
   basic blocks where VAR is defined (assigned a new value).  It also
   contains a bitmap of all the blocks where VAR is live-on-entry
   (i.e., there is a use of VAR in block B without a preceding
   definition in B).  The live-on-entry information is used when
   computing PHI pruning heuristics.  */
static htab_t def_blocks;

/* Stack of trees used to restore the global currdefs to its original
   state after completing rewriting of a block and its dominator
   children.  Its elements have the following properties:

   - An SSA_NAME (N) indicates that the current definition of the
     underlying variable should be set to the given SSA_NAME.  If the
     symbol associated with the SSA_NAME is not a GIMPLE register, the
     next slot in the stack must be a _DECL node (SYM).  In this case,
     the name N in the previous slot is the current reaching
     definition for SYM.

   - A _DECL node indicates that the underlying variable has no
     current definition.

   - A NULL node at the top entry is used to mark the last slot
     associated with the current block.  */
static VEC(tree,heap) *block_defs_stack;

/* Set of existing SSA names being replaced by update_ssa.  */
static sbitmap old_ssa_names;

/* Set of new SSA names being added by update_ssa.  Note that both
   NEW_SSA_NAMES and OLD_SSA_NAMES are dense bitmaps because most of
   the operations done on them are presence tests.  */
static sbitmap new_ssa_names;

/* Symbols whose SSA form needs to be updated or created for the first
   time.  */
static bitmap syms_to_rename;

/* Subset of SYMS_TO_RENAME.  Contains all the GIMPLE register symbols
   that have been marked for renaming.  */
static bitmap regs_to_rename;

/* Subset of SYMS_TO_RENAME.  Contains all the memory symbols
   that have been marked for renaming.  */
static bitmap mem_syms_to_rename;

/* Set of SSA names that have been marked to be released after they
   were registered in the replacement table.  They will be finally
   released after we finish updating the SSA web.  */
static bitmap names_to_release;

/* Set of SSA names that have been marked stale by the SSA updater.
   This happens when the LHS of a VDEF operator needs a new SSA name
   (i.e., it used to be a .MEM factored store and got converted into a
   regular store).  When this occurs, other VDEF and VUSE operators
   using the original LHS must stop using it.
   See rewrite_update_stmt_vops.  */
static bitmap stale_ssa_names;

/* For each block, the PHI nodes that need to be rewritten are stored into
   these vectors.  */
typedef VEC(tree, heap) *tree_vec;
DEF_VEC_P (tree_vec);
DEF_VEC_ALLOC_P (tree_vec, heap);

static VEC(tree_vec, heap) *phis_to_rewrite;

/* The bitmap of non-NULL elements of PHIS_TO_REWRITE.  */
static bitmap blocks_with_phis_to_rewrite;

/* Growth factor for NEW_SSA_NAMES and OLD_SSA_NAMES.  These sets need
   to grow as the callers to register_new_name_mapping will typically
   create new names on the fly.  FIXME.  Currently set to 1/3 to avoid
   frequent reallocations but still need to find a reasonable growth
   strategy.  */
#define NAME_SETS_GROWTH_FACTOR	(MAX (3, num_ssa_names / 3))

/* Tuple used to represent replacement mappings.  */
struct repl_map_d
{
  tree name;
  bitmap set;
};

/* NEW -> OLD_SET replacement table.  If we are replacing several
   existing SSA names O_1, O_2, ..., O_j with a new name N_i,
   then REPL_TBL[N_i] = { O_1, O_2, ..., O_j }.  */
static htab_t repl_tbl;

/* true if register_new_name_mapping needs to initialize the data
   structures needed by update_ssa.  */
static bool need_to_initialize_update_ssa_p = true;

/* true if update_ssa needs to update virtual operands.  */
static bool need_to_update_vops_p = false;

/* Statistics kept by update_ssa to use in the virtual mapping
   heuristic.  If the number of virtual mappings is beyond certain
   threshold, the updater will switch from using the mappings into
   renaming the virtual symbols from scratch.  In some cases, the
   large number of name mappings for virtual names causes significant
   slowdowns in the PHI insertion code.  */
struct update_ssa_stats_d
{
  unsigned num_virtual_mappings;
  unsigned num_total_mappings;
  bitmap virtual_symbols;
  unsigned num_virtual_symbols;
};
static struct update_ssa_stats_d update_ssa_stats;

/* Global data to attach to the main dominator walk structure.  */
struct mark_def_sites_global_data
{
  /* This bitmap contains the variables which are set before they
     are used in a basic block.  */
  bitmap kills;

  /* Bitmap of names to rename.  */
  sbitmap names_to_rename;

  /* Set of blocks that mark_def_sites deems interesting for the
     renamer to process.  */
  sbitmap interesting_blocks;
};


/* Information stored for SSA names.  */
struct ssa_name_info
{
  /* The current reaching definition replacing this SSA name.  */
  tree current_def;

  /* This field indicates whether or not the variable may need PHI nodes.
     See the enum's definition for more detailed information about the
     states.  */
  ENUM_BITFIELD (need_phi_state) need_phi_state : 2;

  /* Age of this record (so that info_for_ssa_name table can be cleared
     quicky); if AGE < CURRENT_INFO_FOR_SSA_NAME_AGE, then the fields
     are assumed to be null.  */
  unsigned age;

  /* For .MEM names, this is the set of symbols that are currently
     reached by this name.  This is used when rewriting the arguments
     of factored PHI nodes in replace_factored_phi_argument.  Do not
     try to use it outside that function, as its contents are only
     valid within that context.  */
  bitmap reached_syms;
};

/* The information associated with names.  */
typedef struct ssa_name_info *ssa_name_info_p;
DEF_VEC_P (ssa_name_info_p);
DEF_VEC_ALLOC_P (ssa_name_info_p, heap);

static VEC(ssa_name_info_p, heap) *info_for_ssa_name;
static unsigned current_info_for_ssa_name_age;

/* The set of blocks affected by update_ssa.  */
static bitmap blocks_to_update;

/* The main entry point to the SSA renamer (rewrite_blocks) may be
   called several times to do different, but related, tasks.
   Initially, we need it to rename the whole program into SSA form.
   At other times, we may need it to only rename into SSA newly
   exposed symbols.  Finally, we can also call it to incrementally fix
   an already built SSA web.  */
enum rewrite_mode {
    /* Convert the whole function into SSA form.  */
    REWRITE_ALL,

    /* Incrementally update the SSA web by replacing existing SSA
       names with new ones.  See update_ssa for details.  */
    REWRITE_UPDATE
};


/* Use TREE_VISITED to keep track of which statements we want to
   rename.  When renaming a subset of the variables, not all
   statements will be processed.  This is decided in mark_def_sites.  */
#define REWRITE_THIS_STMT(T)	TREE_VISITED (T)

/* Use the unsigned flag to keep track of which statements we want to
   visit when marking new definition sites.  This is slightly
   different than REWRITE_THIS_STMT: it's used by update_ssa to
   distinguish statements that need to have both uses and defs
   processed from those that only need to have their defs processed.
   Statements that define new SSA names only need to have their defs
   registered, but they don't need to have their uses renamed.  */
#define REGISTER_DEFS_IN_THIS_STMT(T)	(T)->common.unsigned_flag

DEF_VEC_P(bitmap);
DEF_VEC_ALLOC_P(bitmap,heap);

/* Array of sets of memory symbols that already contain a PHI node in
   each basic block.  */
static bitmap *syms_with_phi_in_bb;

/* When a factored PHI node P has arguments with multiple reaching
   definitions it needs to be split into multiple PHI nodes to hold
   the different reaching definitions.  The problem is that the
   sub-tree dominated by the block holding P may have already been
   renamed.  Some statements that are reached by P should really be
   reached by one of the new PHI nodes split from P.

   This problem would not exist if we could guarantee that PHI nodes
   get their arguments filled in before their dominated sub-tree is
   renamed.  However, due to circular references created by loops, it
   is generally not possible to guarantee this ordering.
   
   We solve this problem by post-processing PHI nodes that have been
   split.  For every split PHI node P, we keep a list of PHI nodes
   split from P.  We then traverse the list of immediate uses for P
   and determine whether they should be reached by one of P's children
   instead.  */
struct unfactored_phis_d
{
  /* The PHI node that has been split.  */
  tree phi;

  /* List of PHI nodes created to disambiguate arguments with multiple
     reaching definitions.  */
  VEC(tree, heap) *children;

  /* Next PHI in the list.  */
  struct unfactored_phis_d *next;
};

typedef struct unfactored_phis_d *unfactored_phis_t;

static unfactored_phis_t first_unfactored_phi = NULL;
static unfactored_phis_t last_unfactored_phi = NULL;
static htab_t unfactored_phis = NULL;

/* Last dominance number assigned to an SSA name.  Dominance
   numbers are used to order reaching definitions when fixing UD
   chains for statements reached by split PHI nodes (see
   fixup_unfactored_phis).  */
static unsigned int last_dom_num;


/* Prototypes for debugging functions.  */
extern void dump_tree_ssa (FILE *);
extern void debug_tree_ssa (void);
extern void debug_def_blocks (void);
extern void dump_tree_ssa_stats (FILE *);
extern void debug_tree_ssa_stats (void);
extern void dump_update_ssa (FILE *);
extern void debug_update_ssa (void);
extern void dump_names_replaced_by (FILE *, tree);
extern void debug_names_replaced_by (tree);
extern void dump_def_blocks (FILE *);
extern void debug_def_blocks (void);
extern void dump_defs_stack (FILE *, int);
extern void debug_defs_stack (int);
extern void dump_currdefs (FILE *);
extern void debug_currdefs (void);
extern void dump_syms_with_phi (FILE *);
extern void debug_syms_with_phi (void);
extern void dump_unfactored_phis (FILE *);
extern void debug_unfactored_phis (void);
extern void dump_unfactored_phi (FILE *, tree);
extern void debug_unfactored_phi (tree);

/* Get the information associated with NAME.  */

static inline ssa_name_info_p
get_ssa_name_ann (tree name)
{
  unsigned ver = SSA_NAME_VERSION (name);
  unsigned len = VEC_length (ssa_name_info_p, info_for_ssa_name);
  struct ssa_name_info *info;

  if (ver >= len)
    {
      unsigned new_len = num_ssa_names;

      VEC_reserve (ssa_name_info_p, heap, info_for_ssa_name, new_len);
      while (len++ < new_len)
	{
	  struct ssa_name_info *info = XCNEW (struct ssa_name_info);
	  info->age = current_info_for_ssa_name_age;
	  VEC_quick_push (ssa_name_info_p, info_for_ssa_name, info);
	}
    }

  info = VEC_index (ssa_name_info_p, info_for_ssa_name, ver);
  if (info->age < current_info_for_ssa_name_age)
    {
      info->need_phi_state = 0;
      info->current_def = NULL_TREE;
      info->age = current_info_for_ssa_name_age;
      info->reached_syms = NULL;
    }

  return info;
}


/* Clears info for SSA names.  */

static void
clear_ssa_name_info (void)
{
  current_info_for_ssa_name_age++;
}


/* Return the dominance number associated with STMT.  Dominance numbers
   are computed during renaming.  Given two statements S1 and S2, it
   is guaranteed that if DOM_NUM (S2) > DOM_NUM (S1) then either S2
   post-dominates S1 or S1 and S2 are on unrelated dominance
   sub-trees.  This property is used when post-processing split PHI
   nodes after renaming (see fixup_unfactored_phis).  */

static unsigned int
get_dom_num (tree stmt)
{
  return get_stmt_ann (stmt)->uid;
}

/* Likewise, but for SSA name NAME.  */

static unsigned int
get_name_dom_num (tree name)
{
  tree def_stmt = SSA_NAME_DEF_STMT (name);

  if (IS_EMPTY_STMT (def_stmt))
    return 1;

  return get_dom_num (def_stmt);
}


/* Assign the next dominance number to STMT.  */

static inline void
set_next_dom_num (tree stmt)
{
  get_stmt_ann (stmt)->uid = last_dom_num++;
}


/* Get phi_state field for VAR.  */

static inline enum need_phi_state
get_phi_state (tree var)
{
  if (TREE_CODE (var) == SSA_NAME)
    return get_ssa_name_ann (var)->need_phi_state;
  else
    return var_ann (var)->need_phi_state;
}


/* Sets phi_state field for VAR to STATE.  */

static inline void
set_phi_state (tree var, enum need_phi_state state)
{
  if (TREE_CODE (var) == SSA_NAME)
    get_ssa_name_ann (var)->need_phi_state = state;
  else
    var_ann (var)->need_phi_state = state;
}


/* Return the current definition for VAR.  */

tree
get_current_def (tree var)
{
  if (TREE_CODE (var) == SSA_NAME)
    return get_ssa_name_ann (var)->current_def;
  else
    return var_ann (var)->current_def;
}


/* Sets current definition of VAR to DEF.  */

void
set_current_def (tree var, tree def)
{
  if (TREE_CODE (var) == SSA_NAME)
    get_ssa_name_ann (var)->current_def = def;
  else
    var_ann (var)->current_def = def;
}


/* Compute global livein information given the set of blockx where
   an object is locally live at the start of the block (LIVEIN)
   and the set of blocks where the object is defined (DEF_BLOCKS).

   Note: This routine augments the existing local livein information
   to include global livein (i.e., it modifies the underlying bitmap
   for LIVEIN).  */

void
compute_global_livein (bitmap livein, bitmap def_blocks)
{
  basic_block bb, *worklist, *tos;
  unsigned i;
  bitmap_iterator bi;

  tos = worklist
    = (basic_block *) xmalloc (sizeof (basic_block) * (last_basic_block + 1));

  EXECUTE_IF_SET_IN_BITMAP (livein, 0, i, bi)
    *tos++ = BASIC_BLOCK (i);

  /* Iterate until the worklist is empty.  */
  while (tos != worklist)
    {
      edge e;
      edge_iterator ei;

      /* Pull a block off the worklist.  */
      bb = *--tos;

      /* For each predecessor block.  */
      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  basic_block pred = e->src;
	  int pred_index = pred->index;

	  /* None of this is necessary for the entry block.  */
	  if (pred != ENTRY_BLOCK_PTR
	      && ! bitmap_bit_p (livein, pred_index)
	      && ! bitmap_bit_p (def_blocks, pred_index))
	    {
	      *tos++ = pred;
	      bitmap_set_bit (livein, pred_index);
	    }
	}
    }

  free (worklist);
}


/* Cleans up the REWRITE_THIS_STMT and REGISTER_DEFS_IN_THIS_STMT flags for
   all statements in basic block BB.  */

static void
initialize_flags_in_bb (basic_block bb)
{
  tree phi, stmt;
  block_stmt_iterator bsi;

  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
    {
      REWRITE_THIS_STMT (phi) = 0;
      REGISTER_DEFS_IN_THIS_STMT (phi) = 0;
    }

  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      stmt = bsi_stmt (bsi);
      /* We are going to use the operand cache API, such as
	 SET_USE, SET_DEF, and FOR_EACH_IMM_USE_FAST.  The operand
	 cache for each statement should be up-to-date.  */
      gcc_assert (!stmt_modified_p (stmt));
      REWRITE_THIS_STMT (stmt) = 0;
      REGISTER_DEFS_IN_THIS_STMT (stmt) = 0;
    }
}

/* Mark block BB as interesting for update_ssa.  */

static void
mark_block_for_update (basic_block bb)
{
  gcc_assert (blocks_to_update != NULL);
  if (bitmap_bit_p (blocks_to_update, bb->index))
    return;
  bitmap_set_bit (blocks_to_update, bb->index);
  initialize_flags_in_bb (bb);
}

/* Return the set of blocks where variable VAR is defined and the blocks
   where VAR is live on entry (livein).  If no entry is found in
   DEF_BLOCKS, a new one is created and returned.  */

static inline struct def_blocks_d *
get_def_blocks_for (tree var)
{
  struct def_blocks_d db, *db_p;
  void **slot;

  db.var = var;
  slot = htab_find_slot (def_blocks, (void *) &db, INSERT);
  if (*slot == NULL)
    {
      db_p = XNEW (struct def_blocks_d);
      db_p->var = var;
      db_p->def_blocks = BITMAP_ALLOC (NULL);
      db_p->phi_blocks = BITMAP_ALLOC (NULL);
      db_p->livein_blocks = BITMAP_ALLOC (NULL);
      *slot = (void *) db_p;
    }
  else
    db_p = (struct def_blocks_d *) *slot;

  return db_p;
}


/* Mark block BB as the definition site for variable VAR.  PHI_P is true if
   VAR is defined by a PHI node.  */

static void
set_def_block (tree var, basic_block bb, bool phi_p)
{
  struct def_blocks_d *db_p;
  enum need_phi_state state;

  state = get_phi_state (var);
  db_p = get_def_blocks_for (var);

  /* Set the bit corresponding to the block where VAR is defined.  */
  bitmap_set_bit (db_p->def_blocks, bb->index);
  if (phi_p)
    bitmap_set_bit (db_p->phi_blocks, bb->index);

  /* Keep track of whether or not we may need to insert PHI nodes.

     If we are in the UNKNOWN state, then this is the first definition
     of VAR.  Additionally, we have not seen any uses of VAR yet, so
     we do not need a PHI node for this variable at this time (i.e.,
     transition to NEED_PHI_STATE_NO).

     If we are in any other state, then we either have multiple definitions
     of this variable occurring in different blocks or we saw a use of the
     variable which was not dominated by the block containing the
     definition(s).  In this case we may need a PHI node, so enter
     state NEED_PHI_STATE_MAYBE.  */
  if (state == NEED_PHI_STATE_UNKNOWN)
    set_phi_state (var, NEED_PHI_STATE_NO);
  else
    set_phi_state (var, NEED_PHI_STATE_MAYBE);
}


/* Mark block BB as having VAR live at the entry to BB.  */

static void
set_livein_block (tree var, basic_block bb)
{
  struct def_blocks_d *db_p;
  enum need_phi_state state = get_phi_state (var);

  db_p = get_def_blocks_for (var);

  /* Set the bit corresponding to the block where VAR is live in.  */
  bitmap_set_bit (db_p->livein_blocks, bb->index);

  /* Keep track of whether or not we may need to insert PHI nodes.

     If we reach here in NEED_PHI_STATE_NO, see if this use is dominated
     by the single block containing the definition(s) of this variable.  If
     it is, then we remain in NEED_PHI_STATE_NO, otherwise we transition to
     NEED_PHI_STATE_MAYBE.  */
  if (state == NEED_PHI_STATE_NO && !bitmap_empty_p (db_p->def_blocks))
    {
      int ix = bitmap_first_set_bit (db_p->def_blocks);
      if (!dominated_by_p (CDI_DOMINATORS, bb, BASIC_BLOCK (ix)))
	set_phi_state (var, NEED_PHI_STATE_MAYBE);
    }
  else
    set_phi_state (var, NEED_PHI_STATE_MAYBE);
}


/* Return true if symbol SYM is marked for renaming.  */

static inline bool
symbol_marked_for_renaming (tree sym)
{
  return bitmap_bit_p (syms_to_rename, DECL_UID (sym));
}


/* Return true if NAME is in OLD_SSA_NAMES.  */

static inline bool
is_old_name (tree name)
{
  unsigned ver = SSA_NAME_VERSION (name);
  return ver < new_ssa_names->n_bits && TEST_BIT (old_ssa_names, ver);
}


/* Return true if NAME is in NEW_SSA_NAMES.  */

static inline bool
is_new_name (tree name)
{
  unsigned ver = SSA_NAME_VERSION (name);
  return ver < new_ssa_names->n_bits && TEST_BIT (new_ssa_names, ver);
}


/* Hashing and equality functions for REPL_TBL.  */

static hashval_t
repl_map_hash (const void *p)
{
  return htab_hash_pointer ((const void *)((const struct repl_map_d *)p)->name);
}

static int
repl_map_eq (const void *p1, const void *p2)
{
  return ((const struct repl_map_d *)p1)->name
	 == ((const struct repl_map_d *)p2)->name;
}

static void
repl_map_free (void *p)
{
  BITMAP_FREE (((struct repl_map_d *)p)->set);
  free (p);
}


/* Hashing and equality functions for UNFACTORED_PHIS.  */

static hashval_t
unfactored_phis_hash (const void *p)
{
  return htab_hash_pointer ((const void *)
                            ((const struct unfactored_phis_d *)p)->phi);
}

static int
unfactored_phis_eq (const void *p1, const void *p2)
{
  return ((const struct unfactored_phis_d *)p1)->phi
	 == ((const struct unfactored_phis_d *)p2)->phi;
}

static void
unfactored_phis_free (void *p)
{
  VEC_free (tree, heap, ((struct unfactored_phis_d *)p)->children);
  free (p);
}


/* Return the names replaced by NEW (i.e., REPL_TBL[NEW].SET).  */

static inline bitmap
names_replaced_by (tree new)
{
  struct repl_map_d m;
  void **slot;

  m.name = new;
  slot = htab_find_slot (repl_tbl, (void *) &m, NO_INSERT);

  /* If N was not registered in the replacement table, return NULL.  */
  if (slot == NULL || *slot == NULL)
    return NULL;

  return ((struct repl_map_d *) *slot)->set;
}


/* Add OLD to REPL_TBL[NEW].SET.  */

static inline void
add_to_repl_tbl (tree new, tree old)
{
  struct repl_map_d m, *mp;
  void **slot;

  m.name = new;
  slot = htab_find_slot (repl_tbl, (void *) &m, INSERT);
  if (*slot == NULL)
    {
      mp = XNEW (struct repl_map_d);
      mp->name = new;
      mp->set = BITMAP_ALLOC (NULL);
      *slot = (void *) mp;
    }
  else
    mp = (struct repl_map_d *) *slot;

  bitmap_set_bit (mp->set, SSA_NAME_VERSION (old));
}


/* Add a new mapping NEW -> OLD REPL_TBL.  Every entry N_i in REPL_TBL
   represents the set of names O_1 ... O_j replaced by N_i.  This is
   used by update_ssa and its helpers to introduce new SSA names in an
   already formed SSA web.  */

static void
add_new_name_mapping (tree new, tree old)
{
  timevar_push (TV_TREE_SSA_INCREMENTAL);

  /* OLD and NEW must be different SSA names for the same symbol.  */
  gcc_assert (new != old && SSA_NAME_VAR (new) == SSA_NAME_VAR (old));

  /* We may need to grow NEW_SSA_NAMES and OLD_SSA_NAMES because our
     caller may have created new names since the set was created.  */
  if (new_ssa_names->n_bits <= num_ssa_names - 1)
    {
      unsigned int new_sz = num_ssa_names + NAME_SETS_GROWTH_FACTOR;
      new_ssa_names = sbitmap_resize (new_ssa_names, new_sz, 0);
      old_ssa_names = sbitmap_resize (old_ssa_names, new_sz, 0);
    }

  /* If this mapping is for virtual names, we will need to update
     virtual operands.  If this is a mapping for .MEM, then we gather
     the symbols associated with each name.  */
  if (!is_gimple_reg (new))
    {
      tree sym;
      size_t uid;

      need_to_update_vops_p = true;
      update_ssa_stats.num_virtual_mappings++;
      update_ssa_stats.num_virtual_symbols++;

      /* Keep counts of virtual mappings and symbols to use in the
	 virtual mapping heuristic.  If we have large numbers of
	 virtual mappings for a relatively low number of symbols, it
	 will make more sense to rename the symbols from scratch.
	 Otherwise, the insertion of PHI nodes for each of the old
	 names in these mappings will be very slow.  */
      sym = SSA_NAME_VAR (new);
      if (sym != mem_var)
	{
	  uid = DECL_UID (sym);
	  bitmap_set_bit (update_ssa_stats.virtual_symbols, uid);
	}
      else
	{
	  bitmap s;

	  s = get_loads_and_stores (SSA_NAME_DEF_STMT (old))->loads;
	  if (s)
	    bitmap_ior_into (update_ssa_stats.virtual_symbols, s);

	  s = get_loads_and_stores (SSA_NAME_DEF_STMT (old))->stores;
	  if (s)
	    bitmap_ior_into (update_ssa_stats.virtual_symbols, s);

	  s = get_loads_and_stores (SSA_NAME_DEF_STMT (new))->stores;
	  if (s)
	    bitmap_ior_into (update_ssa_stats.virtual_symbols, s);
	}
    }

  /* Update the REPL_TBL table.  */
  add_to_repl_tbl (new, old);

  /* If OLD had already been registered as a new name, then all the
     names that OLD replaces should also be replaced by NEW.  */
  if (is_new_name (old))
    bitmap_ior_into (names_replaced_by (new), names_replaced_by (old));

  /* Register NEW and OLD in NEW_SSA_NAMES and OLD_SSA_NAMES,
     respectively.  */
  SET_BIT (new_ssa_names, SSA_NAME_VERSION (new));
  SET_BIT (old_ssa_names, SSA_NAME_VERSION (old));

  /* Update mapping counter to use in the virtual mapping heuristic.  */
  update_ssa_stats.num_total_mappings++;

  timevar_pop (TV_TREE_SSA_INCREMENTAL);
}


/* Add SYMS to the set of symbols with existing PHI nodes in basic
   block TO.  */

static void
add_syms_with_phi (bitmap syms, unsigned to)
{
  if (syms_with_phi_in_bb[to] == NULL)
    syms_with_phi_in_bb[to] = BITMAP_ALLOC (NULL);

  bitmap_ior_into (syms_with_phi_in_bb[to], syms);

  /* For placing factored PHI nodes, we are only interested in
     considering those symbols that are marked for renaming.
     Otherwise, we will be placing unnecessary factored PHI nodes.  */
  if (!bitmap_empty_p (syms_to_rename))
    bitmap_and_into (syms_with_phi_in_bb[to], syms_to_rename);
}


/* Add SYM to the set of symbols with existing PHI nodes in basic
   block TO.  */

static void
add_sym_with_phi (tree sym, unsigned to)
{
  if (syms_with_phi_in_bb[to] == NULL)
    syms_with_phi_in_bb[to] = BITMAP_ALLOC (NULL);

  bitmap_set_bit (syms_with_phi_in_bb[to], DECL_UID (sym));
}


/* Call back for walk_dominator_tree used to collect definition sites
   for every variable in the function.  For every statement S in block
   BB:

   1- Variables defined by S in the DEFS of S are marked in the bitmap
      WALK_DATA->GLOBAL_DATA->KILLS.

   2- If S uses a variable VAR and there is no preceding kill of VAR,
      then it is marked in the LIVEIN_BLOCKS bitmap associated with VAR.

   This information is used to determine which variables are live
   across block boundaries to reduce the number of PHI nodes
   we create.  */

static void
mark_def_sites (struct dom_walk_data *walk_data, basic_block bb,
		block_stmt_iterator bsi)
{
  struct mark_def_sites_global_data *gd;
  bitmap kills;
  tree stmt, def;
  use_operand_p use_p;
  ssa_op_iter iter;

  stmt = bsi_stmt (bsi);
  update_stmt_if_modified (stmt);

  gd = (struct mark_def_sites_global_data *) walk_data->global_data;
  kills = gd->kills;

  gcc_assert (blocks_to_update == NULL);
  REGISTER_DEFS_IN_THIS_STMT (stmt) = 0;
  REWRITE_THIS_STMT (stmt) = 0;

  /* If a variable is used before being set, then the variable is live
     across a block boundary, so mark it live-on-entry to BB.  */
  FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter, SSA_OP_USE)
    {
      tree sym = USE_FROM_PTR (use_p);
      gcc_assert (DECL_P (sym));
      if (!bitmap_bit_p (kills, DECL_UID (sym)))
	set_livein_block (sym, bb);
      REWRITE_THIS_STMT (stmt) = 1;
    }
  
  /* Now process the defs.  Mark BB as the definition block and add
     each def to the set of killed symbols.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
    {
      gcc_assert (DECL_P (def));
      set_def_block (def, bb, false);
      bitmap_set_bit (kills, DECL_UID (def));
      REGISTER_DEFS_IN_THIS_STMT (stmt) = 1;
    }

  /* If we found the statement interesting then also mark the block BB
     as interesting.  */
  if (REWRITE_THIS_STMT (stmt) || REGISTER_DEFS_IN_THIS_STMT (stmt))
    SET_BIT (gd->interesting_blocks, bb->index);
}

/* Structure used by prune_unused_phi_nodes to record bounds of the intervals
   in the dfs numbering of the dominance tree.  */

struct dom_dfsnum
{
  /* Basic block whose index this entry corresponds to.  */
  unsigned bb_index;

  /* The dfs number of this node.  */
  unsigned dfs_num;
};

/* Compares two entries of type struct dom_dfsnum by dfs_num field.  Callback
   for qsort.  */

static int
cmp_dfsnum (const void *a, const void *b)
{
  const struct dom_dfsnum *da = a;
  const struct dom_dfsnum *db = b;

  return (int) da->dfs_num - (int) db->dfs_num;
}

/* Among the intervals starting at the N points specified in DEFS, find
   the one that contains S, and return its bb_index.  */

static unsigned
find_dfsnum_interval (struct dom_dfsnum *defs, unsigned n, unsigned s)
{
  unsigned f = 0, t = n, m;

  while (t > f + 1)
    {
      m = (f + t) / 2;
      if (defs[m].dfs_num <= s)
	f = m;
      else
	t = m;
    }

  return defs[f].bb_index;
}

/* Clean bits from PHIS for phi nodes whose value cannot be used in USES.
   KILLS is a bitmap of blocks where the value is defined before any use.  */

static void
prune_unused_phi_nodes (bitmap phis, bitmap kills, bitmap uses)
{
  VEC(int, heap) *worklist;
  bitmap_iterator bi;
  unsigned i, b, p, u, top;
  bitmap live_phis;
  basic_block def_bb, use_bb;
  edge e;
  edge_iterator ei;
  bitmap to_remove;
  struct dom_dfsnum *defs;
  unsigned n_defs, adef;

  if (bitmap_empty_p (uses))
    {
      bitmap_clear (phis);
      return;
    }

  /* The phi must dominate a use, or an argument of a live phi.  Also, we
     do not create any phi nodes in def blocks, unless they are also livein.  */
  to_remove = BITMAP_ALLOC (NULL);
  bitmap_and_compl (to_remove, kills, uses);
  bitmap_and_compl_into (phis, to_remove);
  if (bitmap_empty_p (phis))
    {
      BITMAP_FREE (to_remove);
      return;
    }

  /* We want to remove the unnecessary phi nodes, but we do not want to compute
     liveness information, as that may be linear in the size of CFG, and if
     there are lot of different variables to rewrite, this may lead to quadratic
     behavior.

     Instead, we basically emulate standard dce.  We put all uses to worklist,
     then for each of them find the nearest def that dominates them.  If this
     def is a phi node, we mark it live, and if it was not live before, we
     add the predecessors of its basic block to the worklist.
   
     To quickly locate the nearest def that dominates use, we use dfs numbering
     of the dominance tree (that is already available in order to speed up
     queries).  For each def, we have the interval given by the dfs number on
     entry to and on exit from the corresponding subtree in the dominance tree.
     The nearest dominator for a given use is the smallest of these intervals
     that contains entry and exit dfs numbers for the basic block with the use.
     If we store the bounds for all the uses to an array and sort it, we can
     locate the nearest dominating def in logarithmic time by binary search.*/
  bitmap_ior (to_remove, kills, phis);
  n_defs = bitmap_count_bits (to_remove);
  defs = XNEWVEC (struct dom_dfsnum, 2 * n_defs + 1);
  defs[0].bb_index = 1;
  defs[0].dfs_num = 0;
  adef = 1;
  EXECUTE_IF_SET_IN_BITMAP (to_remove, 0, i, bi)
    {
      def_bb = BASIC_BLOCK (i);
      defs[adef].bb_index = i;
      defs[adef].dfs_num = bb_dom_dfs_in (CDI_DOMINATORS, def_bb);
      defs[adef + 1].bb_index = i;
      defs[adef + 1].dfs_num = bb_dom_dfs_out (CDI_DOMINATORS, def_bb);
      adef += 2;
    }
  BITMAP_FREE (to_remove);
  gcc_assert (adef == 2 * n_defs + 1);
  qsort (defs, adef, sizeof (struct dom_dfsnum), cmp_dfsnum);
  gcc_assert (defs[0].bb_index == 1);

  /* Now each DEFS entry contains the number of the basic block to that the
     dfs number corresponds.  Change them to the number of basic block that
     corresponds to the interval following the dfs number.  Also, for the
     dfs_out numbers, increase the dfs number by one (so that it corresponds
     to the start of the following interval, not to the end of the current
     one).  We use WORKLIST as a stack.  */
  worklist = VEC_alloc (int, heap, n_defs + 1);
  VEC_quick_push (int, worklist, 1);
  top = 1;
  n_defs = 1;
  for (i = 1; i < adef; i++)
    {
      b = defs[i].bb_index;
      if (b == top)
	{
	  /* This is a closing element.  Interval corresponding to the top
	     of the stack after removing it follows.  */
	  VEC_pop (int, worklist);
	  top = VEC_index (int, worklist, VEC_length (int, worklist) - 1);
	  defs[n_defs].bb_index = top;
	  defs[n_defs].dfs_num = defs[i].dfs_num + 1;
	}
      else
	{
	  /* Opening element.  Nothing to do, just push it to the stack and move
	     it to the correct position.  */
	  defs[n_defs].bb_index = defs[i].bb_index;
	  defs[n_defs].dfs_num = defs[i].dfs_num;
	  VEC_quick_push (int, worklist, b);
	  top = b;
	}

      /* If this interval starts at the same point as the previous one, cancel
	 the previous one.  */
      if (defs[n_defs].dfs_num == defs[n_defs - 1].dfs_num)
	defs[n_defs - 1].bb_index = defs[n_defs].bb_index;
      else
	n_defs++;
    }
  VEC_pop (int, worklist);
  gcc_assert (VEC_empty (int, worklist));

  /* Now process the uses.  */
  live_phis = BITMAP_ALLOC (NULL);
  EXECUTE_IF_SET_IN_BITMAP (uses, 0, i, bi)
    {
      VEC_safe_push (int, heap, worklist, i);
    }

  while (!VEC_empty (int, worklist))
    {
      b = VEC_pop (int, worklist);
      if (b == ENTRY_BLOCK)
	continue;

      /* If there is a phi node in USE_BB, it is made live.  Otherwise,
	 find the def that dominates the immediate dominator of USE_BB
	 (the kill in USE_BB does not dominate the use).  */
      if (bitmap_bit_p (phis, b))
	p = b;
      else
	{
	  use_bb = get_immediate_dominator (CDI_DOMINATORS, BASIC_BLOCK (b));
	  p = find_dfsnum_interval (defs, n_defs,
				    bb_dom_dfs_in (CDI_DOMINATORS, use_bb));
	  if (!bitmap_bit_p (phis, p))
	    continue;
	}

      /* If the phi node is already live, there is nothing to do.  */
      if (bitmap_bit_p (live_phis, p))
	continue;

      /* Mark the phi as live, and add the new uses to the worklist.  */
      bitmap_set_bit (live_phis, p);
      def_bb = BASIC_BLOCK (p);
      FOR_EACH_EDGE (e, ei, def_bb->preds)
	{
	  u = e->src->index;
	  if (bitmap_bit_p (uses, u))
	    continue;

	  /* In case there is a kill directly in the use block, do not record
	     the use (this is also necessary for correctness, as we assume that
	     uses dominated by a def directly in their block have been filtered
	     out before).  */
	  if (bitmap_bit_p (kills, u))
	    continue;

	  bitmap_set_bit (uses, u);
	  VEC_safe_push (int, heap, worklist, u);
	}
    }

  VEC_free (int, heap, worklist);
  bitmap_copy (phis, live_phis);
  BITMAP_FREE (live_phis);
  free (defs);
}

/* Given a set of blocks with variable definitions (DEF_BLOCKS),
   return a bitmap with all the blocks in the iterated dominance
   frontier of the blocks in DEF_BLOCKS.  DFS contains dominance
   frontier information as returned by compute_dominance_frontiers.

   The resulting set of blocks are the potential sites where PHI nodes
   are needed.  The caller is responsible for freeing the memory
   allocated for the return value.  */

static bitmap
compute_idf (bitmap def_blocks, bitmap *dfs)
{
  bitmap_iterator bi;
  unsigned bb_index, i;
  VEC(int,heap) *work_stack;
  bitmap phi_insertion_points;

  work_stack = VEC_alloc (int, heap, n_basic_blocks);
  phi_insertion_points = BITMAP_ALLOC (NULL);

  /* Seed the work list with all the blocks in DEF_BLOCKS.  We use
     VEC_quick_push here for speed.  This is safe because we know that
     the number of definition blocks is no greater than the number of
     basic blocks, which is the initial capacity of WORK_STACK.  */
  EXECUTE_IF_SET_IN_BITMAP (def_blocks, 0, bb_index, bi)
    VEC_quick_push (int, work_stack, bb_index);

  /* Pop a block off the worklist, add every block that appears in
     the original block's DF that we have not already processed to
     the worklist.  Iterate until the worklist is empty.   Blocks
     which are added to the worklist are potential sites for
     PHI nodes.  */
  while (VEC_length (int, work_stack) > 0)
    {
      bb_index = VEC_pop (int, work_stack);

      /* Since the registration of NEW -> OLD name mappings is done
	 separately from the call to update_ssa, when updating the SSA
	 form, the basic blocks where new and/or old names are defined
	 may have disappeared by CFG cleanup calls.  In this case,
	 we may pull a non-existing block from the work stack.  */
      gcc_assert (bb_index < (unsigned) last_basic_block);

      EXECUTE_IF_AND_COMPL_IN_BITMAP (dfs[bb_index], phi_insertion_points,
	                              0, i, bi)
	{
	  /* Use a safe push because if there is a definition of VAR
	     in every basic block, then WORK_STACK may eventually have
	     more than N_BASIC_BLOCK entries.  */
	  VEC_safe_push (int, heap, work_stack, i);
	  bitmap_set_bit (phi_insertion_points, i);
	}
    }

  VEC_free (int, heap, work_stack);

  return phi_insertion_points;
}


/* Return the set of blocks where variable VAR is defined and the blocks
   where VAR is live on entry (livein).  Return NULL, if no entry is
   found in DEF_BLOCKS.  */

static inline struct def_blocks_d *
find_def_blocks_for (tree var)
{
  struct def_blocks_d dm;
  dm.var = var;
  return (struct def_blocks_d *) htab_find (def_blocks, &dm);
}


/* Retrieve or create a default definition for symbol SYM.  */

static inline tree
get_default_def_for (tree sym)
{
  tree ddef = default_def (sym);

  if (ddef == NULL_TREE)
    {
      ddef = make_ssa_name (sym, build_empty_stmt ());
      set_default_def (sym, ddef);
    }

  return ddef;
}


/* Marks phi node PHI in basic block BB for rewrite.  */

static void
mark_phi_for_rewrite (basic_block bb, tree phi)
{
  tree_vec phis;
  unsigned i, idx = bb->index;

  if (REWRITE_THIS_STMT (phi))
    return;

  REWRITE_THIS_STMT (phi) = 1;

  if (!blocks_with_phis_to_rewrite)
    return;

  bitmap_set_bit (blocks_with_phis_to_rewrite, idx);
  VEC_reserve (tree_vec, heap, phis_to_rewrite, last_basic_block + 1);
  for (i = VEC_length (tree_vec, phis_to_rewrite); i <= idx; i++)
    VEC_quick_push (tree_vec, phis_to_rewrite, NULL);

  phis = VEC_index (tree_vec, phis_to_rewrite, idx);
  if (!phis)
    phis = VEC_alloc (tree, heap, 10);

  VEC_safe_push (tree, heap, phis, phi);
  VEC_replace (tree_vec, phis_to_rewrite, idx, phis);
}


/* Insert PHI nodes for variable VAR using the iterated dominance
   frontier given in PHI_INSERTION_POINTS.  If UPDATE_P is true, this
   function assumes that the caller is incrementally updating the
   existing SSA form, in which case VAR may be an SSA name instead of
   a symbol.

   PHI_INSERTION_POINTS is updated to reflect nodes that already had a
   PHI node for VAR.  On exit, only the nodes that received a PHI node
   for VAR will be present in PHI_INSERTION_POINTS.  */

static void
insert_phi_nodes_for (tree var, bitmap phi_insertion_points, bool update_p)
{
  unsigned bb_index;
  edge e;
  tree phi;
  basic_block bb;
  bitmap_iterator bi;
  struct def_blocks_d *def_map;
  bitmap pruned_syms = NULL;

  def_map = find_def_blocks_for (var);
  gcc_assert (def_map);

  /* Remove the blocks where we already have PHI nodes for VAR.  */
  bitmap_and_compl_into (phi_insertion_points, def_map->phi_blocks);

  /* Remove obviously useless phi nodes.  */
  prune_unused_phi_nodes (phi_insertion_points, def_map->def_blocks,
			  def_map->livein_blocks);

  if (var == mem_var)
    pruned_syms = BITMAP_ALLOC (NULL);

  /* And insert the PHI nodes.  */
  EXECUTE_IF_SET_IN_BITMAP (phi_insertion_points, 0, bb_index, bi)
    {
      bb = BASIC_BLOCK (bb_index);
      if (update_p)
	mark_block_for_update (bb);

      phi = NULL_TREE;

      if (TREE_CODE (var) == SSA_NAME)
	{
	  /* If we are rewriting SSA names, create the LHS of the PHI
	     node by duplicating VAR.  This is useful in the case of
	     pointers, to also duplicate pointer attributes (alias
	     information, in particular).  */
	  edge_iterator ei;
	  tree new_lhs;

	  gcc_assert (update_p);
	  if (SSA_NAME_VAR (var) == mem_var)
	    {
	      bitmap s = get_loads_and_stores (SSA_NAME_DEF_STMT (var))->stores;
	      phi = create_factored_phi_node (var, bb, s);
	    }
	  else
	    phi = create_phi_node (var, bb);

	  new_lhs = duplicate_ssa_name (var, phi);
	  SET_PHI_RESULT (phi, new_lhs);
	  add_new_name_mapping (new_lhs, var);

	  /* Add VAR to every argument slot of PHI.  We need VAR in
	     every argument so that rewrite_update_phi_arguments knows
	     which name is this PHI node replacing.  If VAR is a
	     symbol marked for renaming, this is not necessary, the
	     renamer will use the symbol on the LHS to get its
	     reaching definition.  */
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    add_phi_arg (phi, var, e);
	}
      else if (var != mem_var)
	{
	  tree sym = DECL_P (var) ? var : SSA_NAME_VAR (var);
	  phi = create_phi_node (sym, bb);
	}
      else
	{
	  bool use_pruned_p;
	  bitmap syms;

	  /* Initially, a factored PHI node in block BB is associated
	     with all the memory symbols marked for renaming.  If BB
	     already has PHI nodes for some symbols in
	     MEM_SYMS_TO_RENAME, prune this initial set to avoid
	     confusion during renaming.  */
	  if (syms_with_phi_in_bb[bb->index]
	      && bitmap_intersect_p (mem_syms_to_rename,
				     syms_with_phi_in_bb[bb->index]))
	    {
	      bitmap_and_compl (pruned_syms,
		                mem_syms_to_rename,
				syms_with_phi_in_bb[bb->index]);
	      use_pruned_p = true;
	    }
	  else
	    use_pruned_p = false;

	  syms = (use_pruned_p) ? pruned_syms : mem_syms_to_rename;
	  if (bitmap_singleton_p (syms))
	    {
	      tree sym = referenced_var_lookup (bitmap_first_set_bit (syms));
	      phi = create_phi_node (sym, bb);
	    }
	  else
	    phi = create_factored_phi_node (mem_var, bb, syms);
	}

      REGISTER_DEFS_IN_THIS_STMT (phi) = 1;
      mark_phi_for_rewrite (bb, phi);
    }

  BITMAP_FREE (pruned_syms);
}


/* Insert PHI nodes at the dominance frontier of blocks with variable
   definitions.  DFS contains the dominance frontier information for
   the flowgraph.  */

static void
insert_phi_nodes (bitmap *dfs)
{
  referenced_var_iterator rvi;
  tree var;

  timevar_push (TV_TREE_INSERT_PHI_NODES);
  
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      struct def_blocks_d *def_map;
      bitmap idf;

      def_map = find_def_blocks_for (var);
      if (def_map == NULL)
	continue;

      if (get_phi_state (var) != NEED_PHI_STATE_NO)
	{
	  idf = compute_idf (def_map->def_blocks, dfs);
	  insert_phi_nodes_for (var, idf, false);
	  BITMAP_FREE (idf);
	}
    }

  timevar_pop (TV_TREE_INSERT_PHI_NODES);
}


/* Push SYM's current reaching definition into BLOCK_DEFS_STACK and
   register DEF (an SSA_NAME) to be a new definition for SYM.  */

static void
register_new_def (tree def, tree sym)
{
  tree currdef;
   
  /* If this variable is set in a single basic block and all uses are
     dominated by the set(s) in that single basic block, then there is
     no reason to record anything for this variable in the block local
     definition stacks.  Doing so just wastes time and memory.

     This is the same test to prune the set of variables which may
     need PHI nodes.  So we just use that information since it's already
     computed and available for us to use.  */
  if (get_phi_state (sym) == NEED_PHI_STATE_NO)
    {
      set_current_def (sym, def);
      return;
    }

  currdef = get_current_def (sym);

  /* If SYM is not a GIMPLE register, then CURRDEF may be a name whose
     SSA_NAME_VAR is not necessarily SYM.  In this case, also push SYM
     in the stack so that we know which symbol is being defined by
     this SSA name when we unwind the stack.  */
  if (currdef && !is_gimple_reg (sym))
    VEC_safe_push (tree, heap, block_defs_stack, sym);

  /* Push the current reaching definition into BLOCK_DEFS_STACK.  This
     stack is later used by the dominator tree callbacks to restore
     the reaching definitions for all the variables defined in the
     block after a recursive visit to all its immediately dominated
     blocks.  If there is no current reaching definition, then just
     record the underlying _DECL node.  */
  VEC_safe_push (tree, heap, block_defs_stack, currdef ? currdef : sym);

  /* Set the current reaching definition for SYM to be DEF.  */
  set_current_def (sym, def);
}


/* Perform a depth-first traversal of the dominator tree looking for
   variables to rename.  BB is the block where to start searching.
   Renaming is a five step process:

   1- Every definition made by PHI nodes at the start of the blocks is
      registered as the current definition for the corresponding variable.

   2- Every statement in BB is rewritten.  USE and VUSE operands are
      rewritten with their corresponding reaching definition.  DEF and
      VDEF targets are registered as new definitions.
      
   3- All the PHI nodes in successor blocks of BB are visited.  The
      argument corresponding to BB is replaced with its current reaching
      definition.

   4- Recursively rewrite every dominator child block of BB.

   5- Restore (in reverse order) the current reaching definition for every
      new definition introduced in this block.  This is done so that when
      we return from the recursive call, all the current reaching
      definitions are restored to the names that were valid in the
      dominator parent of BB.  */

/* SSA Rewriting Step 1.  Initialization, create a block local stack
   of reaching definitions for new SSA names produced in this block
   (BLOCK_DEFS).  Register new definitions for every PHI node in the
   block.  */

static void
rewrite_initialize_block (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			  basic_block bb)
{
  tree phi;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n\nRenaming block #%d\n\n", bb->index);

  /* Mark the unwind point for this block.  */
  VEC_safe_push (tree, heap, block_defs_stack, NULL_TREE);

  /* Step 1.  Register new definitions for every PHI node in the block.
     Conceptually, all the PHI nodes are executed in parallel and each PHI
     node introduces a new version for the associated variable.  */
  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
    {
      tree result = PHI_RESULT (phi);
      gcc_assert (is_gimple_reg (result));
      register_new_def (result, SSA_NAME_VAR (result));
    }
}


/* Return the current definition for variable VAR.  If none is found,
   create a new SSA name to act as the zeroth definition for VAR.  */

static inline tree
get_reaching_def (tree var)
{
  tree currdef;
  
  /* Lookup the current reaching definition for VAR.  */
  currdef = get_current_def (var);

  /* If there is no reaching definition for VAR, create and register a
     default definition for it (if needed).  */
  if (currdef == NULL_TREE)
    {
      /* If VAR is not a GIMPLE register, use the default definition
	 for .MEM.  */
      tree sym = DECL_P (var) ? var : SSA_NAME_VAR (var);
      sym = (is_gimple_reg (sym)) ? sym : mem_var;
      currdef = get_default_def_for (sym);
      set_current_def (var, currdef);
    }

  /* Return the current reaching definition for VAR, or the default
     definition, if we had to create one.  */
  return currdef;
}


/* SSA Rewriting Step 2.  Rewrite every variable used in each statement in
   the block with its immediate reaching definitions.  Update the current
   definition of a variable when a new real or virtual definition is found.  */

static void
rewrite_stmt (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
	      basic_block bb ATTRIBUTE_UNUSED, block_stmt_iterator si)
{
  tree stmt;
  use_operand_p use_p;
  def_operand_p def_p;
  ssa_op_iter iter;

  stmt = bsi_stmt (si);

  /* If mark_def_sites decided that we don't need to rewrite this
     statement, ignore it.  */
  gcc_assert (blocks_to_update == NULL);
  if (!REWRITE_THIS_STMT (stmt) && !REGISTER_DEFS_IN_THIS_STMT (stmt))
    return;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Renaming statement ");
      print_generic_stmt (dump_file, stmt, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  /* Step 1.  Rewrite USES in the statement.  */
  if (REWRITE_THIS_STMT (stmt))
    FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter, SSA_OP_USE)
      {
	tree var = USE_FROM_PTR (use_p);
	gcc_assert (DECL_P (var));
	SET_USE (use_p, get_reaching_def (var));
      }

  /* Step 2.  Register the statement's DEF operands.  */
  if (REGISTER_DEFS_IN_THIS_STMT (stmt))
    FOR_EACH_SSA_DEF_OPERAND (def_p, stmt, iter, SSA_OP_DEF)
      {
	tree var = DEF_FROM_PTR (def_p);
	gcc_assert (DECL_P (var));
	SET_DEF (def_p, make_ssa_name (var, stmt));
	register_new_def (DEF_FROM_PTR (def_p), var);
      }
}


/* SSA Rewriting Step 3.  Visit all the successor blocks of BB looking for
   PHI nodes.  For every PHI node found, add a new argument containing the
   current reaching definition for the variable and the edge through which
   that definition is reaching the PHI node.  */

static void
rewrite_add_phi_arguments (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			   basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      tree phi;

      for (phi = phi_nodes (e->dest); phi; phi = PHI_CHAIN (phi))
	{
	  tree currdef;
	  currdef = get_reaching_def (SSA_NAME_VAR (PHI_RESULT (phi)));
	  add_phi_arg (phi, currdef, e);
	}
    }
}


/* Called after visiting all the statements in basic block BB and all
   of its dominator children.  Restore CURRDEFS to its original value.  */

static void
rewrite_finalize_block (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			basic_block bb ATTRIBUTE_UNUSED)
{
  /* Restore CURRDEFS to its original state.  */
  while (VEC_length (tree, block_defs_stack) > 0)
    {
      tree tmp = VEC_pop (tree, block_defs_stack);
      tree saved_def, var;

      if (tmp == NULL_TREE)
	break;

      if (TREE_CODE (tmp) == SSA_NAME)
	{
	  /* If we recorded an SSA_NAME, then make the SSA_NAME the
	     current definition of its underlying variable.  Note that
	     if the SSA_NAME is not for a GIMPLE register, the symbol
	     being defined is stored in the next slot in the stack.
	     This mechanism is needed because an SSA name for a
	     non-register symbol may be the definition for more than
	     one symbol (e.g., SFTs, aliased variables, etc).  */
	  saved_def = tmp;
	  var = SSA_NAME_VAR (saved_def);
	  if (!is_gimple_reg (var))
	    var = VEC_pop (tree, block_defs_stack);
	}
      else
	{
	  /* If we recorded anything else, it must have been a _DECL
	     node and its current reaching definition must have been
	     NULL.  */
	  saved_def = NULL;
	  var = tmp;
	}
                                                                                
      set_current_def (var, saved_def);
    }
}


/* Dump bitmap SET (assumed to contain VAR_DECLs) to FILE.  */

void
dump_decl_set (FILE *file, bitmap set)
{
  if (set)
    {
      bitmap_iterator bi;
      unsigned i;

      fprintf (file, "{ ");

      EXECUTE_IF_SET_IN_BITMAP (set, 0, i, bi)
	{
	  print_generic_expr (file, referenced_var (i), 0);
	  fprintf (file, " ");
	}

      fprintf (file, "}\n");
    }
  else
    fprintf (file, "NIL\n");
}


/* Dump bitmap SET (assumed to contain VAR_DECLs) to FILE.  */

void
debug_decl_set (bitmap set)
{
  dump_decl_set (stderr, set);
}


/* Dump the renaming stack (block_defs_stack) to FILE.  Traverse the
   stack up to a maximum of N levels.  If N is -1, the whole stack is
   dumped.  New levels are created when the dominator tree traversal
   used for renaming enters a new sub-tree.  */

void
dump_defs_stack (FILE *file, int n)
{
  int i, j;

  fprintf (file, "\n\nRenaming stack");
  if (n > 0)
    fprintf (file, " (up to %d levels)", n);
  fprintf (file, "\n\n");

  i = 1;
  fprintf (file, "Level %d (current level)\n", i);
  for (j = (int) VEC_length (tree, block_defs_stack) - 1; j >= 0; j--)
    {
      tree name, var;
      
      name = VEC_index (tree, block_defs_stack, j);
      if (name == NULL_TREE)
	{
	  i++;
	  if (n > 0 && i > n)
	    break;
	  fprintf (file, "\nLevel %d\n", i);
	  continue;
	}

      if (DECL_P (name))
	{
	  var = name;
	  name = NULL_TREE;
	}
      else
	{
	  var = SSA_NAME_VAR (name);
	  if (!is_gimple_reg (var))
	    {
	      j--;
	      var = VEC_index (tree, block_defs_stack, j);
	    }
	}

      fprintf (file, "    Previous CURRDEF (");
      print_generic_expr (file, var, 0);
      fprintf (file, ") = ");
      if (name)
	print_generic_expr (file, name, 0);
      else
	fprintf (file, "<NIL>");
      fprintf (file, "\n");
    }
}


/* Dump the renaming stack (block_defs_stack) to stderr.  Traverse the
   stack up to a maximum of N levels.  If N is -1, the whole stack is
   dumped.  New levels are created when the dominator tree traversal
   used for renaming enters a new sub-tree.  */

void
debug_defs_stack (int n)
{
  dump_defs_stack (stderr, n);
}


/* Dump the current reaching definition of every symbol to FILE.  */

void
dump_currdefs (FILE *file)
{
  referenced_var_iterator i;
  tree var;

  fprintf (file, "\n\nCurrent reaching definitions\n\n");
  FOR_EACH_REFERENCED_VAR (var, i)
    if (syms_to_rename == NULL || bitmap_bit_p (syms_to_rename, DECL_UID (var)))
      {
	fprintf (file, "CURRDEF (");
	print_generic_expr (file, var, 0);
	fprintf (file, ") = ");
	if (get_current_def (var))
	  print_generic_expr (file, get_current_def (var), 0);
	else
	  fprintf (file, "<NIL>");
	fprintf (file, "\n");
      }
}


/* Dump the current reaching definition of every symbol to stderr.  */

void
debug_currdefs (void)
{
  dump_currdefs (stderr);
}


/* Dump symbols with PHI nodes on FILE.  */

void
dump_syms_with_phi (FILE *file)
{
  basic_block bb;

  if (syms_with_phi_in_bb == NULL)
    return;

  fprintf (file, "\n\nMemory symbols with existing PHI nodes\n\n");
  FOR_EACH_BB (bb)
    {
      bool newline_p = false;

      if (syms_with_phi_in_bb && syms_with_phi_in_bb[bb->index])
	{
	  fprintf (file, "SYMS_WITH_PHI_IN_BB[%d]  = ", bb->index);
	  dump_decl_set (file, syms_with_phi_in_bb[bb->index]);
	  newline_p = true;
	}

      if (newline_p)
	fprintf (file, "\n");
    }
}


/* Dump symbols with PHI nodes on stderr.  */

void
debug_syms_with_phi (void)
{
  dump_syms_with_phi (stderr);
}


/* Dump unfactored PHI node PHI to stderr.  */

void
debug_unfactored_phi (tree phi)
{
  dump_unfactored_phi (stderr, phi);
}


/* Dump the list of unfactored PHIs to FILE.  */

void
dump_unfactored_phis (FILE *file)
{
  struct unfactored_phis_d *n;
  unsigned i;

  if (unfactored_phis == NULL)
    return;

  fprintf (file, "\n\nUnfactored PHI nodes\n\n");

  for (i = 0, n = first_unfactored_phi; n; n = n->next, i++)
    {
      fprintf (file, "#%d: ", i);
      dump_unfactored_phi (file, n->phi);
    }
}


/* Dump the list of unfactored PHIs to stderr.  */

void
debug_unfactored_phis (void)
{
  dump_unfactored_phis (stderr);
}


/* Dump SSA information to FILE.  */

void
dump_tree_ssa (FILE *file)
{
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);

  fprintf (file, "SSA renaming information for %s\n\n", funcname);

  dump_def_blocks (file);
  dump_defs_stack (file, -1);
  dump_currdefs (file);
  dump_syms_with_phi (file);
  dump_unfactored_phis (file);
  dump_tree_ssa_stats (file);
}


/* Dump SSA information to stderr.  */

void
debug_tree_ssa (void)
{
  dump_tree_ssa (stderr);
}


/* Dump statistics for the hash table HTAB.  */

static void
htab_statistics (FILE *file, htab_t htab)
{
  fprintf (file, "size %ld, %ld elements, %f collision/search ratio\n",
	   (long) htab_size (htab),
	   (long) htab_elements (htab),
	   htab_collisions (htab));
}


/* Dump SSA statistics on FILE.  */

void
dump_tree_ssa_stats (FILE *file)
{
  if (def_blocks || repl_tbl)
    fprintf (file, "\nHash table statistics:\n");

  if (def_blocks)
    {
      fprintf (file, "    def_blocks:   ");
      htab_statistics (file, def_blocks);
    }

  if (repl_tbl)
    {
      fprintf (file, "    repl_tbl:     ");
      htab_statistics (file, repl_tbl);
    }

  if (def_blocks || repl_tbl)
    fprintf (file, "\n");
}


/* Dump SSA statistics on stderr.  */

void
debug_tree_ssa_stats (void)
{
  dump_tree_ssa_stats (stderr);
}


/* Hashing and equality functions for DEF_BLOCKS.  */

static hashval_t
def_blocks_hash (const void *p)
{
  return htab_hash_pointer
	((const void *)((const struct def_blocks_d *)p)->var);
}

static int
def_blocks_eq (const void *p1, const void *p2)
{
  return ((const struct def_blocks_d *)p1)->var
	 == ((const struct def_blocks_d *)p2)->var;
}


/* Free memory allocated by one entry in DEF_BLOCKS.  */

static void
def_blocks_free (void *p)
{
  struct def_blocks_d *entry = (struct def_blocks_d *) p;
  BITMAP_FREE (entry->def_blocks);
  BITMAP_FREE (entry->phi_blocks);
  BITMAP_FREE (entry->livein_blocks);
  free (entry);
}


/* Callback for htab_traverse to dump the DEF_BLOCKS hash table.  */

static int
debug_def_blocks_r (void **slot, void *data)
{
  FILE *file = (FILE *) data;
  struct def_blocks_d *db_p = (struct def_blocks_d *) *slot;
  
  fprintf (file, "VAR: ");
  print_generic_expr (file, db_p->var, dump_flags);
  bitmap_print (file, db_p->def_blocks, ", DEF_BLOCKS: { ", "}");
  bitmap_print (file, db_p->livein_blocks, ", LIVEIN_BLOCKS: { ", "}");
  bitmap_print (file, db_p->phi_blocks, ", PHI_BLOCKS: { ", "}\n");

  return 1;
}


/* Dump the DEF_BLOCKS hash table on FILE.  */

void
dump_def_blocks (FILE *file)
{
  fprintf (file, "\n\nDefinition and live-in blocks:\n\n");
  if (def_blocks)
    htab_traverse (def_blocks, debug_def_blocks_r, file);
}


/* Dump the DEF_BLOCKS hash table on stderr.  */

void
debug_def_blocks (void)
{
  dump_def_blocks (stderr);
}


/* Register NEW_NAME to be the new reaching definition for OLD_NAME.  */

static inline void
register_new_update_single (tree new_name, tree old_name)
{
  tree currdef = get_current_def (old_name);

  /* Push the current reaching definition into BLOCK_DEFS_STACK.
     This stack is later used by the dominator tree callbacks to
     restore the reaching definitions for all the variables
     defined in the block after a recursive visit to all its
     immediately dominated blocks.  */
  VEC_reserve (tree, heap, block_defs_stack, 2);
  VEC_quick_push (tree, block_defs_stack, currdef);
  VEC_quick_push (tree, block_defs_stack, old_name);

  /* Set the current reaching definition for OLD_NAME to be
     NEW_NAME.  */
  set_current_def (old_name, new_name);
}


/* Register NEW_NAME to be the new reaching definition for all the
   names in OLD_NAMES.  Used by the incremental SSA update routines to
   replace old SSA names with new ones.  */

static inline void
register_new_update_set (tree new_name, bitmap old_names)
{
  bitmap_iterator bi;
  unsigned i;

  EXECUTE_IF_SET_IN_BITMAP (old_names, 0, i, bi)
    register_new_update_single (new_name, ssa_name (i));
}


/* Initialization of block data structures for the incremental SSA
   update pass.  Create a block local stack of reaching definitions
   for new SSA names produced in this block (BLOCK_DEFS).  Register
   new definitions for every PHI node in the block.  */

static void
rewrite_update_init_block (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
		           basic_block bb)
{
  edge e;
  edge_iterator ei;
  tree phi;
  bool is_abnormal_phi;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n\nRegistering new PHI nodes in block #%d\n\n",
	     bb->index);

  /* Mark the unwind point for this block.  */
  VEC_safe_push (tree, heap, block_defs_stack, NULL_TREE);

  if (!bitmap_bit_p (blocks_to_update, bb->index))
    return;

  /* Mark the LHS if any of the arguments flows through an abnormal
     edge.  */
  is_abnormal_phi = false;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->flags & EDGE_ABNORMAL)
      {
	is_abnormal_phi = true;
	break;
      }

  /* If any of the PHI nodes is a replacement for a name in
     OLD_SSA_NAMES or it's one of the names in NEW_SSA_NAMES, then
     register it as a new definition for its corresponding name.  Also
     register definitions for names whose underlying symbols are
     marked for renaming.  */
  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
    {
      tree lhs, lhs_sym;
      bool old_p, new_p;

      set_next_dom_num (phi);

      if (!REGISTER_DEFS_IN_THIS_STMT (phi))
	continue;
      
      lhs = PHI_RESULT (phi);
      lhs_sym = SSA_NAME_VAR (lhs);

      new_p = is_new_name (lhs);
      old_p = is_old_name (lhs);
      if (new_p || old_p)
	{
	  /* If LHS is a new name, register a new definition for all
	     the names replaced by LHS.  */
	  if (new_p)
	    register_new_update_set (lhs, names_replaced_by (lhs));
	  
	  /* If LHS is an OLD name, register it as a new definition
	     for itself.  */
	  if (old_p)
	    register_new_update_single (lhs, lhs);
	}
      else if (lhs_sym == mem_var && !bitmap_empty_p (syms_to_rename))
	{
	  /* If LHS is a name for .MEM, then PHI becomes the current
	     reaching definition for all the symbols factored in it.  */
	  bitmap_iterator bi;
	  unsigned i;
	  bitmap syms;

	  syms = get_loads_and_stores (phi)->stores;
	  EXECUTE_IF_AND_IN_BITMAP (syms, syms_to_rename, 0, i, bi)
	    register_new_update_single (lhs, referenced_var (i));
	}
      else if (symbol_marked_for_renaming (lhs_sym))
	{
	  /* If LHS is a regular symbol marked for renaming, register
	     LHS as its current reaching definition.  */
	  register_new_update_single (lhs, lhs_sym);
	}

      if (is_abnormal_phi)
	SSA_NAME_OCCURS_IN_ABNORMAL_PHI (lhs) = 1;
    }
}


/* Called after visiting block BB.  Unwind BLOCK_DEFS_STACK to restore
   the current reaching definition of every name re-written in BB to
   the original reaching definition before visiting BB.  This
   unwinding must be done in the opposite order to what is done in
   register_new_update_set.  */

static void
rewrite_update_fini_block (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			   basic_block bb ATTRIBUTE_UNUSED)
{
  while (VEC_length (tree, block_defs_stack) > 0)
    {
      tree var = VEC_pop (tree, block_defs_stack);
      tree saved_def;
      
      /* NULL indicates the unwind stop point for this block (see
	 rewrite_update_init_block).  */
      if (var == NULL)
	return;

      saved_def = VEC_pop (tree, block_defs_stack);
      set_current_def (var, saved_def);
    }
}


/* If the operand pointed to by USE_P is a name in OLD_SSA_NAMES or
   it is a symbol marked for renaming, replace it with USE_P's current
   reaching definition.  */

static inline void
maybe_replace_use (use_operand_p use_p)
{
  tree rdef = NULL_TREE;
  tree use = USE_FROM_PTR (use_p);
  tree sym = DECL_P (use) ? use : SSA_NAME_VAR (use);

  if (TREE_CODE (use) == SSA_NAME && is_old_name (use))
    {
      gcc_assert (!symbol_marked_for_renaming (sym));
      rdef = get_reaching_def (use);
    }
  else if (is_gimple_reg (sym) && symbol_marked_for_renaming (sym))
    {
      /* Note that when renaming naked symbols, we are only interested
	 in handling GIMPLE registers.  Memory operands are updated in
	 rewrite_update_memory_stmt.  */
      rdef = get_reaching_def (sym);
    }

  if (rdef && rdef != use)
    SET_USE (use_p, rdef);
}


/* If the operand pointed to by DEF_P is an SSA name in NEW_SSA_NAMES
   or OLD_SSA_NAMES, or if it is a symbol marked for renaming,
   register it as the current definition for the names replaced by
   DEF_P.  */

static inline void
maybe_register_def (def_operand_p def_p, tree stmt)
{
  tree def = DEF_FROM_PTR (def_p);
  tree sym = DECL_P (def) ? def : SSA_NAME_VAR (def);

  if (TREE_CODE (def) == SSA_NAME && is_new_name (def))
    {
      /* If DEF is a new name, register it as a new definition
	 for all the names replaced by DEF.  */
      gcc_assert (!symbol_marked_for_renaming (sym));
      register_new_update_set (def, names_replaced_by (def));
    }

  if (TREE_CODE (def) == SSA_NAME && is_old_name (def))
    {
      /* If DEF is an old name, register DEF as a new
	 definition for itself.  */
      gcc_assert (!symbol_marked_for_renaming (sym));
      register_new_update_single (def, def);
    }

  /* Note that when renaming naked symbols, we are only interested
     in handling GIMPLE registers.  Memory operands are updated in
     rewrite_update_memory_stmt.  */
  if (is_gimple_reg (sym) && symbol_marked_for_renaming (sym))
    {
      /* If DEF is a naked symbol that needs renaming, create a new
	 name for it.  */
      if (DECL_P (def))
	{
	  def = make_ssa_name (def, stmt);
	  SET_DEF (def_p, def);
	}

      register_new_update_single (def, sym);
    }
}


/* Return true if name N has been marked to be released after the SSA
   form has been updated.  */

static inline bool
name_marked_for_release_p (tree n)
{
  return names_to_release
         && bitmap_bit_p (names_to_release, SSA_NAME_VERSION (n));
}


/* Stale names are those that have been replaced by register_new_vdef_name.
   Since it will sometimes decide to create a new name for the LHS,
   uses of the original LHS on the virtual operands of statements
   downstream should not keep using the original LHS.
   
   This happens when the LHS used to be a .MEM name, which we
   typically try to preserve when updating the RHS of VDEF and VUSE
   operators (see rewrite_update_stmt_vops).  */

static inline void
mark_ssa_name_stale (tree n)
{
  gcc_assert (!need_to_initialize_update_ssa_p);

  if (stale_ssa_names == NULL)
    stale_ssa_names = BITMAP_ALLOC (NULL);

  bitmap_set_bit (stale_ssa_names, SSA_NAME_VERSION (n));
}


/* Return true if name N has been marked stale by the SSA updater.  */

static inline bool
stale_ssa_name_p (tree n)
{
  return stale_ssa_names
         && bitmap_bit_p (stale_ssa_names, SSA_NAME_VERSION (n));
}


/* Given a set of SSA names (RDEFS), add all the names in the set as
   operands to the virtual operator WHICH_VOP for statement STMT.  */

static void
rewrite_vops (tree stmt, bitmap rdefs, int which_vops)
{
  unsigned num_rdefs, i, j;
  bitmap_iterator bi;

  num_rdefs = bitmap_count_bits (rdefs);
  if (which_vops == SSA_OP_VUSE)
    {
      /* STMT should have exactly one VUSE operator.  */
      struct vuse_optype_d *vuses = VUSE_OPS (stmt);
      gcc_assert (vuses && vuses->next == NULL);

      vuses = realloc_vuse (vuses, num_rdefs);
      j = 0;
      EXECUTE_IF_SET_IN_BITMAP (rdefs, 0, i, bi)
	SET_USE (VUSE_OP_PTR (vuses, j++), ssa_name (i));
    }
  else
    {
      tree lhs;
      struct vdef_optype_d *vdefs;

      /* STMT should have exactly one VDEF operator.  */
      vdefs = VDEF_OPS (stmt);
      gcc_assert (vdefs && vdefs->next == NULL);

      /* Preserve the existing LHS to avoid creating SSA names
	 unnecessarily.  */
      lhs = VDEF_RESULT (vdefs);

      vdefs = realloc_vdef (vdefs, num_rdefs);
      j = 0;
      EXECUTE_IF_SET_IN_BITMAP (rdefs, 0, i, bi)
	SET_USE (VDEF_OP_PTR (vdefs, j++), ssa_name (i));

      SET_DEF (VDEF_RESULT_PTR (vdefs), lhs);
    }
}


/* Helper for rewrite_update_memory_stmt.  WHICH_VOPS is either
   SSA_OP_VUSE to update the RHS of a VUSE operator or SSA_OP_VMAYUSE
   to update the RHS of a VDEF operator.  This is done by collecting
   reaching definitions for all the symbols in SYMS and writing a new
   RHS for the virtual operator.

   RDEFS is a scratch bitmap used to store reaching definitions for
   all the symbols in SYMS.  The caller is responsible for allocating
   and freeing it.

   FIXME, change bitmaps to pointer-sets when possible.  */

static void
rewrite_update_stmt_vops (tree stmt, bitmap syms, bitmap rdefs, int which_vops)
{
  unsigned i;
  bitmap_iterator bi;
  bitmap unmarked_syms = NULL;
  
  gcc_assert (which_vops == SSA_OP_VUSE || which_vops == SSA_OP_VMAYUSE);

  /* Collect all the reaching definitions for symbols marked for
     renaming in SYMS.  */
  EXECUTE_IF_SET_IN_BITMAP (syms, 0, i, bi)
    {
      tree sym = referenced_var (i);
      if (symbol_marked_for_renaming (sym))
	{
	  tree rdef = get_reaching_def (sym);
	  bitmap_set_bit (rdefs, SSA_NAME_VERSION (rdef));
	}
      else
	{
	  /* Add SYM to UNMARKED_SYMS so that they can be matched to
	     existing SSA names in WHICH_VOPS.  */
	  if (unmarked_syms == NULL)
	    unmarked_syms = BITMAP_ALLOC (NULL);
	  bitmap_set_bit (unmarked_syms, DECL_UID (sym));
	}
    }

  /* Preserve names from VOPS that are needed for the symbols that
     have not been marked for renaming.  */
  if (unmarked_syms)
    {
      tree name;
      use_operand_p use_p;
      ssa_op_iter iter;
      bitmap old_rdefs;
      unsigned i;
      bitmap_iterator bi;

      old_rdefs = BITMAP_ALLOC (NULL);
      FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter, which_vops)
	{
	  name = USE_FROM_PTR (use_p);
	  bitmap_set_bit (old_rdefs, SSA_NAME_VERSION (name));
	}

      bitmap_and_compl_into (old_rdefs, rdefs);

      /* Determine which of the existing SSA names in VOPS can be
	 discarded.  */
      EXECUTE_IF_SET_IN_BITMAP (old_rdefs, 0, i, bi)
	{
	  tree name = ssa_name (i);

	  if (name_marked_for_release_p (name) || stale_ssa_name_p (name))
	    {
	      /* Names in OLD_RDEFS that are marked for release or
		 stale are discarded.  */
	      continue;
	    }
	  else if (name == default_def (mem_var))
	    {
	      /* .MEM's default definition is always kept.  */
	      bitmap_set_bit (rdefs, i);
	    }
	  else if (is_gimple_reg (name))
	    {
	      /* Names that have been promoted to be GIMPLE registers
		 are discarded, as they clearly do not belong in
		 virtual operands anymore.  */
	      gcc_assert (symbol_marked_for_renaming (SSA_NAME_VAR (name)));
	      continue;
	    }
	  else if (!dominated_by_p (CDI_DOMINATORS, bb_for_stmt (stmt),
		                    bb_for_stmt (SSA_NAME_DEF_STMT (name))))
	    {
	      /* If NAME's definition statement no longer dominates
		 STMT, then it clearly cannot reach it anymore.  */
	      continue;
	    }
	  else
	    {
	      /* If a name in OLD_RDEFS only matches symbols that have
		 been marked for renaming, then those symbols have
		 already been matched above by their current reaching
		 definition (i.e., by one of the names in RDEFS),
		 therefore they need to be discarded.  */
	      bitmap syms;
	      syms = get_loads_and_stores (SSA_NAME_DEF_STMT (name))->stores;

	      if (bitmap_empty_p (syms))
		{
		  /* If NAME factors no symbols, it must be discarded.  */
		  continue;
		}
	      else if (bitmap_intersect_p (syms, syms_to_rename)
		       && !bitmap_intersect_p (syms, unmarked_syms))
		{
		  /* If NAME factors symbols marked for renaming but
		     it does not factor any symbols in UNMARKED_SYMS,
		     then it is not needed because a different name is
		     now the reaching definition for those symbols.  */
		  continue;
		}
	      else
		{
		  /* Otherwise, NAME must be factoring one of the
		     unmarked symbols.  Leave it.  */
		  bitmap_set_bit (rdefs, i);
		}
	    }
	}

      BITMAP_FREE (unmarked_syms);
    }

  /* Rewrite the appropriate virtual operand setting its RHS to RDEFS.  */
  rewrite_vops (stmt, rdefs, which_vops);
}


/* Helper for rewrite_update_memory_stmt.  Register the LHS of the
   VDEF operator in STMT to be the current reaching definition of
   every symbol in the bitmap STORES.  */

static void
register_new_vdef_name (tree stmt, bitmap stores)
{
  tree lhs, new_name;
  struct vdef_optype_d *vdefs;
  bitmap_iterator bi;
  unsigned i;

  /* If needed, create a new name for the LHS.  */
  vdefs = VDEF_OPS (stmt);
  lhs = VDEF_RESULT (vdefs);
  if (DECL_P (lhs))
    {
      /* If there is a single symbol in STORES, use it as the target
	 of the VDEF.  Otherwise factor all the stored symbols into
	 .MEM.  */
      if (bitmap_singleton_p (stores))
	lhs = referenced_var (bitmap_first_set_bit (stores));
      else
	lhs = mem_var;

      new_name = make_ssa_name (lhs, stmt);
    }
  else
    {
      /* If the LHS is already an SSA name, then we may not need to
	 create a new name.  If the underlying symbol for LHS is the
	 same as the symbol we want to use, then re-use it.
	 Otherwise, create a new SSA name for it.  */
      tree new_lhs_sym;

      if (bitmap_singleton_p (stores))
	new_lhs_sym = referenced_var (bitmap_first_set_bit (stores));
      else
	new_lhs_sym = mem_var;

      if (new_lhs_sym == SSA_NAME_VAR (lhs))
	new_name = lhs;
      else
	{
	  /* Create a new SSA name for the LHS and mark the original
	     LHS stale.  This will prevent rewrite_update_stmt_vops
	     from keeping LHS in statements that still use it.  */
	  new_name = make_ssa_name (new_lhs_sym, stmt);
	  mark_ssa_name_stale (lhs);
	}
    }

  /* Set NEW_NAME to be the current reaching definition for every
     symbol on the RHS of the VDEF.  */
  SET_DEF (VDEF_RESULT_PTR (vdefs), new_name);
  EXECUTE_IF_SET_IN_BITMAP (stores, 0, i, bi)
    {
      tree sym = referenced_var (i);
      if (symbol_marked_for_renaming (sym))
	register_new_update_single (new_name, sym);
    }
}


/* Update every SSA memory reference in STMT.  If SET_CURRDEF_P is
   false, no new definitions will be registered for store operations.
   This is used when post-processing unfactored PHI nodes in
   fixup_unfactored_phis.  */

static void
rewrite_update_memory_stmt (tree stmt, bool set_currdef_p)
{
  bitmap rdefs;
  mem_syms_map_t syms;

  syms = get_loads_and_stores (stmt);

  if (syms->loads == NULL && syms->stores == NULL)
    return;

  rdefs = BITMAP_ALLOC (NULL);

  /* Rewrite loaded symbols marked for renaming.  */
  if (syms->loads)
    {
      rewrite_update_stmt_vops (stmt, syms->loads, rdefs, SSA_OP_VUSE);
      bitmap_clear (rdefs);
    }

  if (syms->stores)
    {
      /* Rewrite stored symbols marked for renaming.  */
      rewrite_update_stmt_vops (stmt, syms->stores, rdefs, SSA_OP_VMAYUSE);

      if (set_currdef_p)
	{
	  /* Register the LHS of the VDEF to be the new reaching
	     definition of all the symbols in STORES.  */
	  register_new_vdef_name (stmt, syms->stores);
	}
    }

  BITMAP_FREE (rdefs);
}


/* Update every variable used in the statement pointed-to by SI.  The
   statement is assumed to be in SSA form already.  Names in
   OLD_SSA_NAMES used by SI will be updated to their current reaching
   definition.  Names in OLD_SSA_NAMES or NEW_SSA_NAMES defined by SI
   will be registered as a new definition for their corresponding name
   in OLD_SSA_NAMES.  */

static void
rewrite_update_stmt (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
		     basic_block bb ATTRIBUTE_UNUSED,
		     block_stmt_iterator si)
{
  stmt_ann_t ann;
  tree stmt;
  use_operand_p use_p;
  def_operand_p def_p;
  ssa_op_iter iter;

  stmt = bsi_stmt (si);
  ann = stmt_ann (stmt);

  gcc_assert (bitmap_bit_p (blocks_to_update, bb->index));

  set_next_dom_num (stmt);

  /* Only update marked statements.  */
  if (!REWRITE_THIS_STMT (stmt) && !REGISTER_DEFS_IN_THIS_STMT (stmt))
    return;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Updating SSA information for statement ");
      print_generic_stmt (dump_file, stmt, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  /* If there are memory symbols to put in SSA form, process them.  */
  if (need_to_update_vops_p
      && stmt_references_memory_p (stmt)
      && !bitmap_empty_p (syms_to_rename))
    rewrite_update_memory_stmt (stmt, true);

  /* Rewrite USES included in OLD_SSA_NAMES and USES whose underlying
     symbol is marked for renaming.  */
  if (REWRITE_THIS_STMT (stmt))
    FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter, SSA_OP_ALL_USES)
      maybe_replace_use (use_p);

  /* Register definitions of names in NEW_SSA_NAMES and OLD_SSA_NAMES.
     Also register definitions for names whose underlying symbol is
     marked for renaming.  */
  if (REGISTER_DEFS_IN_THIS_STMT (stmt))
    FOR_EACH_SSA_DEF_OPERAND (def_p, stmt, iter, SSA_OP_ALL_DEFS)
      maybe_register_def (def_p, stmt);
}


/* Replace the operand pointed to by USE_P with USE's current reaching
   definition.  */

static inline void
replace_use (use_operand_p use_p, tree use)
{
  tree rdef = get_reaching_def (use);
  if (rdef != use)
    SET_USE (use_p, rdef);
}


/* Add symbol UID to the set of symbols reached by SSA name NAME.  */

static void
add_reached_sym (tree name, unsigned uid)
{
  ssa_name_info_p ann = get_ssa_name_ann (name);

  if (ann->reached_syms == NULL)
    ann->reached_syms = BITMAP_ALLOC (NULL);
  bitmap_set_bit (ann->reached_syms, uid);
}


/* Lookup PHI node PHI in the table of unfactored PHI nodes.  Return
   NULL if PHI is not in the table.  */

static unfactored_phis_t
lookup_unfactored_phi (tree phi)
{
  struct unfactored_phis_d up;
  void **slot;

  if (unfactored_phis == NULL)
    return NULL;

  up.phi = phi;
  slot = htab_find_slot (unfactored_phis, (void *) &up, NO_INSERT);
  if (slot == NULL)
    return NULL;

  return (unfactored_phis_t) *slot;
}


/* Lookup PHI node PHI in the table of unfactored PHI nodes.  Create a
   new entry for PHI if needed.  */

static unfactored_phis_t
get_unfactored_phi (tree phi)
{
  struct unfactored_phis_d up, *up_p;
  void **slot;

  if (unfactored_phis == NULL)
    {
      unfactored_phis = htab_create (20, unfactored_phis_hash,
				     unfactored_phis_eq, unfactored_phis_free);
      gcc_assert (first_unfactored_phi == NULL && last_unfactored_phi == NULL);
    }

  up.phi = phi;
  slot = htab_find_slot (unfactored_phis, (void *) &up, INSERT);
  if (*slot == NULL)
    {
      up_p = XNEW (struct unfactored_phis_d);
      up_p->phi = phi;
      up_p->children = NULL;
      up_p->next = NULL;

      /* Keep the unfactored PHIs in a single linked list.  Since this
	 table is hashed by address, this avoids ordering issues when
	 traversing the hash table in fixup_unfactored_phis.  */
      if (last_unfactored_phi == NULL)
	{
	  first_unfactored_phi = up_p;
	  last_unfactored_phi = up_p;
	}
      else
	{
	  last_unfactored_phi->next = up_p;
	  last_unfactored_phi = up_p;
	}

      *slot = (void *) up_p;
    }
  else
    up_p = (unfactored_phis_t) *slot;

  return up_p;
}


/* Split a factored PHI node PHI with multiple reaching definitions
   for the argument corresponding to edge E.  PHI_SYMS is the set of
   symbols currently factored in PHI, RDEFS is the set of SSA names
   reaching PHI_SYMS through edge E.

   When we initially compute PHI node placements, memory symbols are
   all treated as the single object named .MEM.  Each PHI node is then
   associated with a default set of symbols initialized from the sets
   of memory symbols marked for renaming (MEM_SYMS_TO_RENAME).

   This grouping allows PHI placement to be efficient at the expense
   of accuracy.  For a given incoming edge E, it may happen that not
   all the symbols in PHI_SYMS have the same reaching definition.  For
   instance,

	  # BLOCK 2
	  # .MEM_11 = VDEF <.MEM_10(D)>
	  D.5771_4 = fwrite (&__gcov_var ...);
	  if (D.5771_4 != 1) goto <L0>; else goto <L1>;

	  # BLOCK 3
	  <L0>:;
	  # SFT.124_14 = VDEF <.MEM_11>
	  __gcov_var.error = 1;
	  # SUCC: 4 (fallthru)

	  # BLOCK 4
	  # .MEM_9 = PHI <.MEM_11(2), { .MEM_11, SFT.124_14 }(3)>;
	  <L1>:;

   Initially, .MEM_9 is a factored PHI node for all the call clobbered
   symbols in this program (i.e., all the SFTs for __gcov_var).

   Notice, however, that the second argument for .MEM_9 is reached by
   two names: SFT.124_14 for the single store to SFT.124 in block #3
   and .MEM_11 for all the other call-clobbered variables.  This means
   that we need two PHI nodes to split the second argument:

	  .MEM_9 = PHI <.MEM_11(2), SFT.124_14(3)>
	  .MEM_15 = PHI <.MEM_11(2), .MEM_11(3)>

   (we do not try to optimize the copy-PHI MEM_15 that we just
   created)

   Although the new .MEM_15 PHI is reached by .MEM_11 through both
   edges, it must not be considered a definition for SFT.124.  The
   correct definition for SFT.124 is .MEM_9.

   So, when creating a new PHI node, we explicitly initialize the set
   of symbols stored in it.

   FIXME, it is not always necessary to add a split PHI node to the
   list of PHI nodes to post-process.  If the dominator children for
   BB have not been renamed yet, we can just mark PHI and its children
   as interesting definitions and let the renamer handle everything.
   This will minimize the number of PHI nodes that need to be
   post-processed in fixup_unfactored_phis.  */

static void
split_factored_phi (tree phi, edge e, basic_block bb, bitmap phi_syms,
                    bitmap rdefs)
{
  unsigned i;
  bitmap_iterator bi;
  unfactored_phis_t n;
  tree phi_lhs;
  
  timevar_push (TV_TREE_SSA_PHI_UNFACTOR);

  n = get_unfactored_phi (phi);
  phi_lhs = PHI_RESULT (phi);

  /* Process all the reaching definitions for PHI_SYMS, creating a new
     PHI node for each one.  */
  EXECUTE_IF_SET_IN_BITMAP (rdefs, 0, i, bi)
    {
      edge f;
      edge_iterator fi;
      ssa_name_info_p ann;
      tree rdef, new_phi, rdef_sym;
      use_operand_p arg_p, new_arg_p;

      rdef = ssa_name (i);
      rdef_sym = SSA_NAME_VAR (rdef);
      ann = get_ssa_name_ann (rdef);

      /* Initialize the set of symbols that should be associated
	 with the new PHI node.  Only the symbols reached by RDEF
	 should be associated with NEW_PHI.  */
      /* FIXME, we could probably not use REACHED_SYMS here.  They are
	 implied by the reaching definition MEM_VAR.  */
      if (bitmap_singleton_p (ann->reached_syms))
	{
	  tree sym;
	  sym = referenced_var (bitmap_first_set_bit (ann->reached_syms));
	  new_phi = create_phi_node (sym, bb);
	}
      else
	new_phi = create_factored_phi_node (mem_var, bb, ann->reached_syms);

      get_stmt_ann (new_phi)->uid = get_stmt_ann (phi)->uid;

      /* Set the the argument corresponding to edge E.  */
      new_arg_p = PHI_ARG_DEF_PTR_FROM_EDGE (new_phi, e);
      SET_USE (new_arg_p, rdef);

      /* Set abnormal flags to NEW_PHI and its argument.  */
      if (e->flags & EDGE_ABNORMAL)
	SSA_NAME_OCCURS_IN_ABNORMAL_PHI (rdef) = 1;

      SSA_NAME_OCCURS_IN_ABNORMAL_PHI (PHI_RESULT (new_phi)) = 
	SSA_NAME_OCCURS_IN_ABNORMAL_PHI (phi_lhs);

      /* Add NEW_PHI to the list of PHI nodes to rewrite.  */
      mark_phi_for_rewrite (bb, new_phi);
      REGISTER_DEFS_IN_THIS_STMT (new_phi) = 1;

      /* Add NEW_PHI to the list of nodes unfactored out of PHI.  */
      VEC_safe_push (tree, heap, n->children, new_phi);

      /* Every other argument not coming through E must be copied
	 from the original PHI node.  The only exception are
	 self-referential arguments.  If an argument ARG is the same
	 name as the LHS of the original PHI node, we have to use the
	 LHS of the new child PHI node in its place.  */
      FOR_EACH_EDGE (f, fi, bb->preds)
	if (e != f)
	  {
	    tree arg;
	    arg_p = PHI_ARG_DEF_PTR_FROM_EDGE (phi, f);
	    arg = USE_FROM_PTR (arg_p);
	    new_arg_p = PHI_ARG_DEF_PTR_FROM_EDGE (new_phi, f);
	    if (arg != phi_lhs)
	      SET_USE (new_arg_p, USE_FROM_PTR (arg_p));
	    else
	      SET_USE (new_arg_p, PHI_RESULT (new_phi));
	  }

      /* The symbols reached by RDEF are now factored in NEW_PHI.
	 Therefore, they must be removed from the set of symbols
	 stored by the original PHI node.  */
      bitmap_and_compl_into (phi_syms, ann->reached_syms);
      BITMAP_FREE (ann->reached_syms);
    }

  timevar_pop (TV_TREE_SSA_PHI_UNFACTOR);
}


/* Replace the PHI argument coming through edge E.  BB is the block
   holding PHI.  PHI is assumed to be a factored PHI node (i.e., its
   LHS is an SSA name for .MEM), which means that the argument may
   have more than one reaching definition.  In the presence of multipe
   reaching definitions, PHI will be split up to accommodate the
   multiple reaching defs.  Return true if PHI was split.  Return
   false otherwise.  */

static bool
replace_factored_phi_argument (tree phi, edge e, basic_block bb)
{
  bitmap phi_syms, rdefs;
  bitmap_iterator bi;
  unsigned i;
  tree rdef, rdef_sym, first_rdef, last_rdef;

  rdefs = NULL;
  last_rdef = NULL_TREE;
  first_rdef = NULL_TREE;

  phi_syms = get_loads_and_stores (phi)->stores;
  if (!bitmap_intersect_p (phi_syms, syms_to_rename))
    {
      /* If PHI has no symbols to rename and the argument at E does
	 not exist, it means that we have completely unfactored this
	 PHI node.  In which case, add .MEM's default definition to
	 avoid confusing the verifier later on.  */
      use_operand_p arg_p = PHI_ARG_DEF_PTR_FROM_EDGE (phi, e);
      tree arg = USE_FROM_PTR (arg_p);
      if (arg == NULL)
	SET_USE (arg_p, get_default_def_for (mem_var));

      return false;
    }

  /* Traverse all the symbols factored in PHI to see if we need to
     unfactor it.  If the argument corresponding to edge E has more
     than one reaching definition, then PHI will need to be split to
     accomodate the multiple reaching defs.  */
  EXECUTE_IF_AND_IN_BITMAP (phi_syms, syms_to_rename, 0, i, bi)
    {
      rdef = get_reaching_def (referenced_var (i));
      rdef_sym = SSA_NAME_VAR (rdef);
      add_reached_sym (rdef, i);

      /* Remember the first factored reaching definition we find.  If
	 we need to unfactor PHI, the first factored reaching
	 definition will stay associated to PHI.  If none of the
	 reaching definitions are factored names, then MEM's default
	 definition will be used.  */
      if (first_rdef == NULL_TREE && rdef_sym == mem_var)
	first_rdef = rdef;

      /* If RDEF is different from the previous one, and it's not the
	 name that we had decided to leave in the original PHI, add it
	 to the set of names that will require new PHI nodes.  */
      if ((last_rdef && rdef != last_rdef && rdef != first_rdef)
	  || rdef_sym != mem_var)
	{
	  if (rdefs == NULL)
	    rdefs = BITMAP_ALLOC (NULL);
	  bitmap_set_bit (rdefs, SSA_NAME_VERSION (rdef));
	}

      last_rdef = rdef;
    }

  /* If we did not find any factored reaching definition, then use
     .MEM's default definition as the argument.  Otherwise, we would
     be converting this factored PHI node into a non-factored PHI.
     This will break use-def chains when a subset of symbols are
     marked for renaming.  If all the arguments of this PHI node end
     up being MEM's default definition, then the PHI will be cleaned
     up by DCE.  */
  if (first_rdef == NULL_TREE)
    first_rdef = get_default_def_for (mem_var);

  /* The argument corresponding to edge E is replaced with the first
     reaching definition we found for PHI_SYMS.  */
  SET_USE (PHI_ARG_DEF_PTR_FROM_EDGE (phi, e), first_rdef);
  BITMAP_FREE (get_ssa_name_ann (first_rdef)->reached_syms);

  /* If we found multiple reaching definitions, we have to split PHI
     accordingly.  Register PHI in the list of unfactored PHI nodes so
     that the children PHIs can be post-processed afterwards.  */
  if (rdefs)
    {
      split_factored_phi (phi, e, bb, phi_syms, rdefs);
      BITMAP_FREE (rdefs);
      return true;
    }

  return false;
}


/* Visit all the successor blocks of BB looking for PHI nodes.  For
   every PHI node found, check if any of its arguments is in
   OLD_SSA_NAMES.  If so, and if the argument has a current reaching
   definition, replace it.  */

static void
rewrite_update_phi_arguments (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			      basic_block bb)
{
  edge e;
  edge_iterator ei;
  unsigned i;

  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      tree phi;
      tree_vec phis;

      if (!bitmap_bit_p (blocks_with_phis_to_rewrite, e->dest->index))
	continue;
     
      phis = VEC_index (tree_vec, phis_to_rewrite, e->dest->index);

      /* Note that we cannot use VEC_iterate here because PHIS may
	 grow when calling replace_factored_phi_argument.  */
      for (i = 0; i < VEC_length (tree, phis); i++)
	{
	  tree arg, lhs_sym;
	  use_operand_p arg_p;

	  phi = VEC_index (tree, phis, i);
  	  gcc_assert (REWRITE_THIS_STMT (phi));

	  arg_p = PHI_ARG_DEF_PTR_FROM_EDGE (phi, e);
	  arg = USE_FROM_PTR (arg_p);

	  if (arg && !DECL_P (arg) && TREE_CODE (arg) != SSA_NAME)
	    continue;

	  lhs_sym = SSA_NAME_VAR (PHI_RESULT (phi));

	  if (arg && TREE_CODE (arg) == SSA_NAME && is_old_name (arg))
	    {
	      /* Process old SSA names first.  */
	      replace_use (arg_p, arg);
	    }
	  else if (lhs_sym == mem_var)
	    {
	      /* If this is a factored PHI node, the argument may
		 have multiple reaching definitions, which will
		 require this PHI node to be split up.  */
	      replace_factored_phi_argument (phi, e, e->dest);

	      /* PHIS may grow, so we need to reload it.  */
	      phis = VEC_index (tree_vec, phis_to_rewrite, e->dest->index);
	    }
	  else
	    {
	      tree arg_sym;

	      /* When updating a PHI node for a recently introduced
		 symbol we will find NULL arguments.  That's why we
		 may need to take the symbol from the LHS of the PHI
		 node.  */
	      if (arg == NULL_TREE || SSA_NAME_VAR (arg) == mem_var)
		arg_sym = lhs_sym;
	      else if (DECL_P (arg))
		arg_sym = arg;
	      else
		arg_sym = SSA_NAME_VAR (arg);

	      if (symbol_marked_for_renaming (arg_sym))
		replace_use (arg_p, arg_sym);
	    }

	  if (e->flags & EDGE_ABNORMAL)
	    SSA_NAME_OCCURS_IN_ABNORMAL_PHI (USE_FROM_PTR (arg_p)) = 1;
	}
    }
}


/* Dump unfactored PHI node PHI to FILE.  */

void
dump_unfactored_phi (FILE *file, tree phi)
{
  unsigned j;
  tree child_phi;
  unfactored_phis_t n = lookup_unfactored_phi (phi);

  if (n && n->children)
    {
      dump_loads_and_stores (file, n->phi);

      fprintf (file, "\nChildren PHI nodes:\n");
      for (j = 0; VEC_iterate (tree, n->children, j, child_phi); j++)
	dump_loads_and_stores (file, child_phi);

      fprintf (file, "\n");
    }
}


/* Rewrite the actual blocks, statements, and PHI arguments, to be in SSA
   form.  

   ENTRY indicates the block where to start.  Every block dominated by
      ENTRY will be rewritten.

   WHAT indicates what actions will be taken by the renamer (see enum
      rewrite_mode).

   BLOCKS are the set of interesting blocks for the dominator walker
      to process.  If this set is NULL, then all the nodes dominated
      by ENTRY are walked.  Otherwise, blocks dominated by ENTRY that
      are not present in BLOCKS are ignored.  */

static void
rewrite_blocks (basic_block entry, enum rewrite_mode what, sbitmap blocks)
{
  struct dom_walk_data walk_data;
  
  /* Rewrite all the basic blocks in the program.  */
  timevar_push (TV_TREE_SSA_REWRITE_BLOCKS);

  /* Setup callbacks for the generic dominator tree walker.  */
  memset (&walk_data, 0, sizeof (walk_data));

  walk_data.dom_direction = CDI_DOMINATORS;
  walk_data.interesting_blocks = blocks;

  if (what == REWRITE_ALL)
    walk_data.before_dom_children_before_stmts = rewrite_initialize_block;
  else
    walk_data.before_dom_children_before_stmts = rewrite_update_init_block;

  if (what == REWRITE_ALL)
    walk_data.before_dom_children_walk_stmts = rewrite_stmt;
  else if (what == REWRITE_UPDATE)
    walk_data.before_dom_children_walk_stmts = rewrite_update_stmt;
  else
    gcc_unreachable ();

  if (what == REWRITE_ALL)
    walk_data.before_dom_children_after_stmts = rewrite_add_phi_arguments;
  else if (what == REWRITE_UPDATE)
    walk_data.before_dom_children_after_stmts = rewrite_update_phi_arguments;
  else
    gcc_unreachable ();
  
  if (what == REWRITE_ALL)
    walk_data.after_dom_children_after_stmts =  rewrite_finalize_block;
  else if (what == REWRITE_UPDATE)
    walk_data.after_dom_children_after_stmts = rewrite_update_fini_block;
  else
    gcc_unreachable ();

  block_defs_stack = VEC_alloc (tree, heap, 10);

  /* Initialize the dominator walker.  */
  init_walk_dominator_tree (&walk_data);

  /* Recursively walk the dominator tree rewriting each statement in
     each basic block.  */
  walk_dominator_tree (&walk_data, entry);

  /* Finalize the dominator walker.  */
  fini_walk_dominator_tree (&walk_data);

  /* Debugging dumps.  */
  if (dump_file && (dump_flags & TDF_STATS))
    {
      dump_dfa_stats (dump_file);
      if (def_blocks)
	dump_tree_ssa_stats (dump_file);
    }
  
  VEC_free (tree, heap, block_defs_stack);

  timevar_pop (TV_TREE_SSA_REWRITE_BLOCKS);
}


/* Block initialization routine for mark_def_sites.  Clear the 
   KILLS bitmap at the start of each block.  */

static void
mark_def_sites_initialize_block (struct dom_walk_data *walk_data,
				 basic_block bb ATTRIBUTE_UNUSED)
{
  struct mark_def_sites_global_data *gd;
  gd = (struct mark_def_sites_global_data *) walk_data->global_data;
  bitmap_clear (gd->kills);
}


/* Mark the definition site blocks for each variable, so that we know
   where the variable is actually live.

   INTERESTING_BLOCKS will be filled in with all the blocks that
      should be processed by the renamer.  It is assumed to be
      initialized and zeroed by the caller.  */

static void
mark_def_site_blocks (sbitmap interesting_blocks)
{
  struct dom_walk_data walk_data;
  struct mark_def_sites_global_data mark_def_sites_global_data;

  /* Setup callbacks for the generic dominator tree walker to find and
     mark definition sites.  */
  walk_data.walk_stmts_backward = false;
  walk_data.dom_direction = CDI_DOMINATORS;
  walk_data.initialize_block_local_data = NULL;
  walk_data.before_dom_children_before_stmts = mark_def_sites_initialize_block;
  walk_data.before_dom_children_walk_stmts = mark_def_sites;
  walk_data.before_dom_children_after_stmts = NULL; 
  walk_data.after_dom_children_before_stmts =  NULL;
  walk_data.after_dom_children_walk_stmts =  NULL;
  walk_data.after_dom_children_after_stmts =  NULL;
  walk_data.interesting_blocks = NULL;

  /* Notice that this bitmap is indexed using variable UIDs, so it must be
     large enough to accommodate all the variables referenced in the
     function, not just the ones we are renaming.  */
  mark_def_sites_global_data.kills = BITMAP_ALLOC (NULL);

  /* Create the set of interesting blocks that will be filled by
     mark_def_sites.  */
  mark_def_sites_global_data.interesting_blocks = interesting_blocks;
  walk_data.global_data = &mark_def_sites_global_data;

  /* We do not have any local data.  */
  walk_data.block_local_data_size = 0;

  /* Initialize the dominator walker.  */
  init_walk_dominator_tree (&walk_data);

  /* Recursively walk the dominator tree.  */
  walk_dominator_tree (&walk_data, ENTRY_BLOCK_PTR);

  /* Finalize the dominator walker.  */
  fini_walk_dominator_tree (&walk_data);

  /* We no longer need this bitmap, clear and free it.  */
  BITMAP_FREE (mark_def_sites_global_data.kills);
}


/* Initialize internal data needed during renaming.  */

static void
init_ssa_renamer (void)
{
  tree var;
  referenced_var_iterator rvi;

  in_ssa_p = false;

  /* Allocate memory for the DEF_BLOCKS hash table.  */
  gcc_assert (def_blocks == NULL);
  def_blocks = htab_create (num_referenced_vars, def_blocks_hash,
                            def_blocks_eq, def_blocks_free);

  FOR_EACH_REFERENCED_VAR(var, rvi)
    set_current_def (var, NULL_TREE);

  gcc_assert (syms_with_phi_in_bb == NULL);
  syms_with_phi_in_bb = XCNEWVEC (bitmap, last_basic_block);

  /* Dominance numbers are assigned to memory SSA names and are used
     whenever factored PHI nodes have been split (see
     fixup_unfactored_phis).  Dominance numbering starts at 2.
     Dominance number 1 is reserved for .MEM's default definition.  */
  last_dom_num = 2;

  /* If there are symbols to rename, identify those symbols that are
     GIMPLE registers into the set REGS_TO_RENAME and those that are
     memory symbols into the set MEM_SYMS_TO_RENAME.  */
  if (syms_to_rename)
    {
      unsigned i;
      bitmap_iterator bi;

      EXECUTE_IF_SET_IN_BITMAP (syms_to_rename, 0, i, bi)
	if (is_gimple_reg (referenced_var (i)))
	  bitmap_set_bit (regs_to_rename, i);

      /* Memory symbols are those not in REGS_TO_RENAME.  */
      bitmap_and_compl (mem_syms_to_rename, syms_to_rename, regs_to_rename);
    }
}


/* Deallocate internal data structures used by the renamer.  */

static void
fini_ssa_renamer (void)
{
  basic_block bb;

  if (def_blocks)
    {
      htab_delete (def_blocks);
      def_blocks = NULL;
    }

  if (syms_with_phi_in_bb)
    {
      FOR_EACH_BB (bb)
	BITMAP_FREE (syms_with_phi_in_bb[bb->index]);
      free (syms_with_phi_in_bb);
      syms_with_phi_in_bb = NULL;
    }

  in_ssa_p = true;
}


/* Main entry point into the SSA builder.  The renaming process
   proceeds in four main phases:

   1- Compute dominance frontier and immediate dominators, needed to
      insert PHI nodes and rename the function in dominator tree
      order.

   2- Find and mark all the blocks that define variables
      (mark_def_site_blocks).

   3- Insert PHI nodes at dominance frontiers (insert_phi_nodes).

   4- Rename all the blocks (rewrite_blocks) and statements in the program.

   Steps 3 and 4 are done using the dominator tree walker
   (walk_dominator_tree).  */

static unsigned int
rewrite_into_ssa (void)
{
  bitmap *dfs;
  basic_block bb;
  sbitmap interesting_blocks;
  
  timevar_push (TV_TREE_SSA_OTHER);

  /* Initialize operand data structures.  */
  init_ssa_operands ();

  /* Initialize internal data needed by the renamer.  */
  init_ssa_renamer ();

  /* Initialize the set of interesting blocks.  The callback
     mark_def_sites will add to this set those blocks that the renamer
     should process.  */
  interesting_blocks = sbitmap_alloc (last_basic_block);
  sbitmap_zero (interesting_blocks);

  /* Initialize dominance frontier.  */
  dfs = XNEWVEC (bitmap, last_basic_block);
  FOR_EACH_BB (bb)
    dfs[bb->index] = BITMAP_ALLOC (NULL);

  /* 1- Compute dominance frontiers.  */
  calculate_dominance_info (CDI_DOMINATORS);
  compute_dominance_frontiers (dfs);

  /* 2- Find and mark definition sites.  */
  mark_def_site_blocks (interesting_blocks);

  /* 3- Insert PHI nodes at dominance frontiers of definition blocks.  */
  insert_phi_nodes (dfs);

  /* 4- Rename all the blocks.  */
  rewrite_blocks (ENTRY_BLOCK_PTR, REWRITE_ALL, interesting_blocks);

  /* Free allocated memory.  */
  FOR_EACH_BB (bb)
    BITMAP_FREE (dfs[bb->index]);
  free (dfs);
  sbitmap_free (interesting_blocks);

  fini_ssa_renamer ();

  timevar_pop (TV_TREE_SSA_OTHER);
  return 0;
}


struct tree_opt_pass pass_build_ssa = 
{
  "ssa",				/* name */
  NULL,					/* gate */
  rewrite_into_ssa,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg | PROP_referenced_vars,	/* properties_required */
  PROP_ssa,				/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func
    | TODO_verify_ssa
    | TODO_remove_unused_locals,	/* todo_flags_finish */
  0					/* letter */
};


/* Mark the definition of VAR at STMT and BB as interesting for the
   renamer.  BLOCKS is the set of blocks that need updating.  */

static void
mark_def_interesting (tree var, tree stmt, basic_block bb, bool insert_phi_p)
{
  gcc_assert (bitmap_bit_p (blocks_to_update, bb->index));
  REGISTER_DEFS_IN_THIS_STMT (stmt) = 1;

  if (insert_phi_p)
    {
      bool is_phi_p = TREE_CODE (stmt) == PHI_NODE;

      set_def_block (var, bb, is_phi_p);

      /* If VAR is an SSA name in NEW_SSA_NAMES, this is a definition
	 site for both itself and all the old names replaced by it.  */
      if (TREE_CODE (var) == SSA_NAME && is_new_name (var))
	{
	  bitmap_iterator bi;
	  unsigned i;
	  bitmap set = names_replaced_by (var);
	  if (set)
	    EXECUTE_IF_SET_IN_BITMAP (set, 0, i, bi)
	      set_def_block (ssa_name (i), bb, is_phi_p);
	}
    }
}


/* Mark the use of VAR at STMT and BB as interesting for the
   renamer.  INSERT_PHI_P is true if we are going to insert new PHI
   nodes.  */

static inline void
mark_use_interesting (tree var, tree stmt, basic_block bb, bool insert_phi_p)
{
  basic_block def_bb = bb_for_stmt (stmt);

  mark_block_for_update (def_bb);
  mark_block_for_update (bb);

  if (TREE_CODE (stmt) == PHI_NODE)
    mark_phi_for_rewrite (def_bb, stmt);
  else
    REWRITE_THIS_STMT (stmt) = 1;

  /* If VAR has not been defined in BB, then it is live-on-entry
     to BB.  Note that we cannot just use the block holding VAR's
     definition because if VAR is one of the names in OLD_SSA_NAMES,
     it will have several definitions (itself and all the names that
     replace it).  */
  if (insert_phi_p)
    {
      struct def_blocks_d *db_p = get_def_blocks_for (var);
      if (!bitmap_bit_p (db_p->def_blocks, bb->index))
	set_livein_block (var, bb);
    }
}


/* Do a dominator walk starting at BB processing statements that
   reference symbols in SYMS_TO_RENAME.  This is very similar to
   mark_def_sites, but the scan handles statements whose operands may
   already be SSA names.

   If INSERT_PHI_P is true, mark those uses as live in the
   corresponding block.  This is later used by the PHI placement
   algorithm to make PHI pruning decisions.  */

static void
prepare_block_for_update (basic_block bb, bool insert_phi_p)
{
  basic_block son;
  block_stmt_iterator si;
  tree phi;
  edge e;
  edge_iterator ei;

  mark_block_for_update (bb);

  /* Process PHI nodes marking interesting those that define or use
     the symbols that we are interested in.  */
  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
    {
      tree lhs = PHI_RESULT (phi);
      tree lhs_sym = SSA_NAME_VAR (lhs);

      if (lhs_sym == mem_var)
	{
	  bitmap stores = get_loads_and_stores (phi)->stores;

	  if (bitmap_intersect_p (stores, syms_to_rename))
	    {
	      /* If symbols currently factored by PHI have been promoted
		 to registers, remove them from the set of factored
		 symbols.  */
	      bitmap_and_compl_into (stores, regs_to_rename);

	      add_syms_with_phi (stores, bb->index);
	      mark_use_interesting (mem_var, phi, bb, insert_phi_p);
	      mark_def_interesting (mem_var, phi, bb, insert_phi_p);
	    }
	}
      else if (symbol_marked_for_renaming (lhs_sym))
	{
	  mark_def_interesting (lhs_sym, phi, bb, insert_phi_p);

	  /* Mark the uses in PHI nodes as interesting.  It would be
	     more correct to process the arguments of the PHI nodes of
	     the successor edges of BB at the end of
	     prepare_block_for_update, however, that turns out to be
	     significantly more expensive.  Doing it here is
	     conservatively correct -- it may only cause us to believe
	     a value to be live in a block that also contains its
	     definition, and thus insert a few more PHI nodes for it.  */
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      mark_use_interesting (lhs_sym, phi, e->src, insert_phi_p);

	      if (!is_gimple_reg (lhs_sym))
		{
		  add_sym_with_phi (lhs_sym, bb->index);
		  mark_use_interesting (mem_var, phi, bb, insert_phi_p);
		  mark_def_interesting (mem_var, phi, bb, insert_phi_p);
		}
	    }
	}
    }

  /* Process the statements.  */
  for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
    {
      tree stmt;
      ssa_op_iter i;
      use_operand_p use_p;
      def_operand_p def_p;
      
      stmt = bsi_stmt (si);

      FOR_EACH_SSA_USE_OPERAND (use_p, stmt, i, SSA_OP_USE)
	{
	  tree use = USE_FROM_PTR (use_p);
	  tree sym = DECL_P (use) ? use : SSA_NAME_VAR (use);
	  if (symbol_marked_for_renaming (sym))
	    mark_use_interesting (use, stmt, bb, insert_phi_p);
	}

      FOR_EACH_SSA_DEF_OPERAND (def_p, stmt, i, SSA_OP_DEF)
	{
	  tree def = DEF_FROM_PTR (def_p);
	  tree sym = DECL_P (def) ? def : SSA_NAME_VAR (def);
	  if (symbol_marked_for_renaming (sym))
	    mark_def_interesting (def, stmt, bb, insert_phi_p);
	}

      /* If the statement makes memory references, mark this site as a
	 reference site for .MEM.  At this point we are not interested
	 in the individual symbols loaded/stored by STMT.  We are only
	 interested in computing global live-in information and PHI
	 placement for .MEM.  We will refine what symbols need the PHI
	 node in a later pass.  */
      if (need_to_update_vops_p && stmt_references_memory_p (stmt))
	{
	  mem_syms_map_t syms = get_loads_and_stores (stmt);

	  if (syms->stores
	      && bitmap_intersect_p (syms->stores, syms_to_rename))
	    {
	      mark_use_interesting (mem_var, stmt, bb, insert_phi_p);
	      mark_def_interesting (mem_var, stmt, bb, insert_phi_p);
	    }

	  if (syms->loads
	      && bitmap_intersect_p (syms->loads, syms_to_rename))
	    mark_use_interesting (mem_var, stmt, bb, insert_phi_p);
	}
    }

  /* Now visit all the blocks dominated by BB.  */
  for (son = first_dom_son (CDI_DOMINATORS, bb);
       son;
       son = next_dom_son (CDI_DOMINATORS, son))
    prepare_block_for_update (son, insert_phi_p);
}


/* Helper for prepare_names_to_update.  Mark all the use sites for
   NAME as interesting.  BLOCKS and INSERT_PHI_P are as in
   prepare_names_to_update.  */

static void
prepare_use_sites_for (tree name, bool insert_phi_p)
{
  use_operand_p use_p;
  imm_use_iterator iter;

  FOR_EACH_IMM_USE_FAST (use_p, iter, name)
    {
      tree stmt = USE_STMT (use_p);
      basic_block bb = bb_for_stmt (stmt);

      if (TREE_CODE (stmt) == PHI_NODE)
	{
	  int ix = PHI_ARG_INDEX_FROM_USE (use_p);
	  edge e = PHI_ARG_EDGE (stmt, ix);
	  mark_use_interesting (name, stmt, e->src, insert_phi_p);
	}
      else
	{
	  /* For regular statements, mark this as an interesting use
	     for NAME.  */
	  mark_use_interesting (name, stmt, bb, insert_phi_p);
	}
    }
}


/* Helper for prepare_names_to_update.  Mark the definition site for
   NAME as interesting.  BLOCKS and INSERT_PHI_P are as in
   prepare_names_to_update.  */

static void
prepare_def_site_for (tree name, bool insert_phi_p)
{
  tree stmt;
  basic_block bb;

  gcc_assert (names_to_release == NULL
	      || !bitmap_bit_p (names_to_release, SSA_NAME_VERSION (name)));

  stmt = SSA_NAME_DEF_STMT (name);
  bb = bb_for_stmt (stmt);
  if (bb)
    {
      gcc_assert (bb->index < last_basic_block);
      mark_block_for_update (bb);
      mark_def_interesting (name, stmt, bb, insert_phi_p);
    }
}


/* Mark definition and use sites of names in NEW_SSA_NAMES and
   OLD_SSA_NAMES.  INSERT_PHI_P is true if the caller wants to insert
   PHI nodes for newly created names.  */

static void
prepare_names_to_update (bool insert_phi_p)
{
  unsigned i = 0;
  bitmap_iterator bi;
  sbitmap_iterator sbi;

  /* If a name N from NEW_SSA_NAMES is also marked to be released,
     remove it from NEW_SSA_NAMES so that we don't try to visit its
     defining basic block (which most likely doesn't exist).  Notice
     that we cannot do the same with names in OLD_SSA_NAMES because we
     want to replace existing instances.  */
  if (names_to_release)
    EXECUTE_IF_SET_IN_BITMAP (names_to_release, 0, i, bi)
      RESET_BIT (new_ssa_names, i);

  /* First process names in NEW_SSA_NAMES.  Otherwise, uses of old
     names may be considered to be live-in on blocks that contain
     definitions for their replacements.  */
  EXECUTE_IF_SET_IN_SBITMAP (new_ssa_names, 0, i, sbi)
    prepare_def_site_for (ssa_name (i), insert_phi_p);

  /* If an old name is in NAMES_TO_RELEASE, we cannot remove it from
     OLD_SSA_NAMES, but we have to ignore its definition site.  */
  EXECUTE_IF_SET_IN_SBITMAP (old_ssa_names, 0, i, sbi)
    {
      if (names_to_release == NULL || !bitmap_bit_p (names_to_release, i))
	prepare_def_site_for (ssa_name (i), insert_phi_p);
      prepare_use_sites_for (ssa_name (i), insert_phi_p);
    }
}


/* Dump all the names replaced by NAME to FILE.  */

void
dump_names_replaced_by (FILE *file, tree name)
{
  unsigned i;
  bitmap old_set;
  bitmap_iterator bi;

  print_generic_expr (file, name, 0);
  fprintf (file, " -> { ");

  old_set = names_replaced_by (name);
  EXECUTE_IF_SET_IN_BITMAP (old_set, 0, i, bi)
    {
      print_generic_expr (file, ssa_name (i), 0);
      fprintf (file, " ");
    }

  fprintf (file, "}\n");
}


/* Dump all the names replaced by NAME to stderr.  */

void
debug_names_replaced_by (tree name)
{
  dump_names_replaced_by (stderr, name);
}


/* Dump SSA update information to FILE.  */

void
dump_update_ssa (FILE *file)
{
  unsigned i = 0;
  bitmap_iterator bi;

  if (!need_ssa_update_p ())
    return;

  if (new_ssa_names && sbitmap_first_set_bit (new_ssa_names) >= 0)
    {
      sbitmap_iterator sbi;

      fprintf (file, "\nSSA replacement table\n");
      fprintf (file, "N_i -> { O_1 ... O_j } means that N_i replaces "
	             "O_1, ..., O_j\n\n");

      EXECUTE_IF_SET_IN_SBITMAP (new_ssa_names, 0, i, sbi)
	dump_names_replaced_by (file, ssa_name (i));

      fprintf (file, "\n");
      fprintf (file, "Number of virtual NEW -> OLD mappings: %7u\n",
	       update_ssa_stats.num_virtual_mappings);
      fprintf (file, "Number of real NEW -> OLD mappings:    %7u\n",
	       update_ssa_stats.num_total_mappings
	       - update_ssa_stats.num_virtual_mappings);
      fprintf (file, "Number of total NEW -> OLD mappings:   %7u\n",
	       update_ssa_stats.num_total_mappings);

      fprintf (file, "\nNumber of virtual symbols: %u\n",
	       update_ssa_stats.num_virtual_symbols);
    }

  if (syms_to_rename && !bitmap_empty_p (syms_to_rename))
    {
      fprintf (file, "\n\nSymbols to be put in SSA form\n\n");
      dump_decl_set (file, syms_to_rename);
    }

  if (names_to_release && !bitmap_empty_p (names_to_release))
    {
      fprintf (file, "\n\nSSA names to release after updating the SSA web\n\n");
      EXECUTE_IF_SET_IN_BITMAP (names_to_release, 0, i, bi)
	{
	  print_generic_expr (file, ssa_name (i), 0);
	  fprintf (file, " ");
	}
    }
 
  if (stale_ssa_names && !bitmap_empty_p (stale_ssa_names))
    {
      fprintf (file, "\n\nSSA names marked stale\n\n");
      EXECUTE_IF_SET_IN_BITMAP (stale_ssa_names, 0, i, bi)
	{
	  print_generic_expr (file, ssa_name (i), 0);
	  fprintf (file, " ");
	}
    }

  fprintf (file, "\n\n");
}


/* Dump SSA update information to stderr.  */

void
debug_update_ssa (void)
{
  dump_update_ssa (stderr);
}


/* Initialize data structures used for incremental SSA updates.  */

static void
init_update_ssa (void)
{
  /* Reserve more space than the current number of names.  The calls to
     add_new_name_mapping are typically done after creating new SSA
     names, so we'll need to reallocate these arrays.  */
  old_ssa_names = sbitmap_alloc (num_ssa_names + NAME_SETS_GROWTH_FACTOR);
  sbitmap_zero (old_ssa_names);

  new_ssa_names = sbitmap_alloc (num_ssa_names + NAME_SETS_GROWTH_FACTOR);
  sbitmap_zero (new_ssa_names);

  repl_tbl = htab_create (20, repl_map_hash, repl_map_eq, repl_map_free);
  need_to_initialize_update_ssa_p = false;
  need_to_update_vops_p = false;
  syms_to_rename = BITMAP_ALLOC (NULL);
  regs_to_rename = BITMAP_ALLOC (NULL);
  mem_syms_to_rename = BITMAP_ALLOC (NULL);
  names_to_release = NULL;
  stale_ssa_names = NULL;
  memset (&update_ssa_stats, 0, sizeof (update_ssa_stats));
  update_ssa_stats.virtual_symbols = BITMAP_ALLOC (NULL);
  gcc_assert (unfactored_phis == NULL);
}


/* Deallocate data structures used for incremental SSA updates.  */

void
delete_update_ssa (void)
{
  unsigned i;
  bitmap_iterator bi;

  sbitmap_free (old_ssa_names);
  old_ssa_names = NULL;

  sbitmap_free (new_ssa_names);
  new_ssa_names = NULL;

  htab_delete (repl_tbl);
  repl_tbl = NULL;

  need_to_initialize_update_ssa_p = true;
  need_to_update_vops_p = false;
  BITMAP_FREE (syms_to_rename);
  BITMAP_FREE (regs_to_rename);
  BITMAP_FREE (mem_syms_to_rename);
  BITMAP_FREE (update_ssa_stats.virtual_symbols);
  BITMAP_FREE (stale_ssa_names);

  if (names_to_release)
    {
      EXECUTE_IF_SET_IN_BITMAP (names_to_release, 0, i, bi)
	release_ssa_name (ssa_name (i));
      BITMAP_FREE (names_to_release);
    }

  clear_ssa_name_info ();

  fini_ssa_renamer ();

  if (unfactored_phis)
    {
      htab_delete (unfactored_phis);
      unfactored_phis = NULL;
      first_unfactored_phi = NULL;
      last_unfactored_phi = NULL;
    }

  if (blocks_with_phis_to_rewrite)
    EXECUTE_IF_SET_IN_BITMAP (blocks_with_phis_to_rewrite, 0, i, bi)
      {
	tree_vec phis = VEC_index (tree_vec, phis_to_rewrite, i);

	VEC_free (tree, heap, phis);
	VEC_replace (tree_vec, phis_to_rewrite, i, NULL);
      }

  BITMAP_FREE (blocks_with_phis_to_rewrite);
  BITMAP_FREE (blocks_to_update);
}


/* Create a new name for OLD_NAME in statement STMT and replace the
   operand pointed to by DEF_P with the newly created name.  Return
   the new name and register the replacement mapping <NEW, OLD> in
   update_ssa's tables.  */

tree
create_new_def_for (tree old_name, tree stmt, def_operand_p def)
{
  tree new_name = duplicate_ssa_name (old_name, stmt);

  SET_DEF (def, new_name);

  if (TREE_CODE (stmt) == PHI_NODE)
    {
      edge e;
      edge_iterator ei;
      basic_block bb = bb_for_stmt (stmt);

      /* If needed, mark NEW_NAME as occurring in an abnormal PHI node. */
      FOR_EACH_EDGE (e, ei, bb->preds)
	if (e->flags & EDGE_ABNORMAL)
	  {
	    SSA_NAME_OCCURS_IN_ABNORMAL_PHI (new_name) = 1;
	    break;
	  }
    }

  register_new_name_mapping (new_name, old_name);

  /* For the benefit of passes that will be updating the SSA form on
     their own, set the current reaching definition of OLD_NAME to be
     NEW_NAME.  */
  set_current_def (old_name, new_name);

  return new_name;
}


/* Register name NEW to be a replacement for name OLD.  This function
   must be called for every replacement that should be performed by
   update_ssa.  */

void
register_new_name_mapping (tree new, tree old)
{
  if (need_to_initialize_update_ssa_p)
    init_update_ssa ();

  add_new_name_mapping (new, old);
}


/* Register symbol SYM to be renamed by update_ssa.  */

void
mark_sym_for_renaming (tree sym)
{
  /* .MEM is not a regular symbol, it is a device for factoring
     multiple stores, much like a PHI function factors multiple
     control flow paths.  */
  gcc_assert (sym != mem_var);

#if 0
  /* Variables with sub-variables should have their sub-variables
     marked separately.  */
  gcc_assert (get_subvars_for_var (sym) == NULL);
#endif

  if (need_to_initialize_update_ssa_p)
    init_update_ssa ();

#if 1
  /* HACK.  Caller should be responsible for this.  */
  {
    subvar_t svars;
    if (var_can_have_subvars (sym) && (svars = get_subvars_for_var (sym)))
      {
	subvar_t sv;
	for (sv = svars; sv; sv = sv->next)
	  bitmap_set_bit (syms_to_rename, DECL_UID (sv->var));
      }
  }
#endif

  bitmap_set_bit (syms_to_rename, DECL_UID (sym));

  if (!is_gimple_reg (sym))
    need_to_update_vops_p = true;
}


/* Register all the symbols in SET to be renamed by update_ssa.  */

void
mark_set_for_renaming (bitmap set)
{
  bitmap_iterator bi;
  unsigned i;

#if 0
  /* Variables with sub-variables should have their sub-variables
     marked separately.  */
  EXECUTE_IF_SET_IN_BITMAP (set, 0, i, bi)
    gcc_assert (get_subvars_for_var (referenced_var (i)) == NULL);
#endif

  if (set == NULL || bitmap_empty_p (set))
    return;

  if (need_to_initialize_update_ssa_p)
    init_update_ssa ();

#if 1
  /* HACK.  Caller should be responsible for this.  */
  EXECUTE_IF_SET_IN_BITMAP (set, 0, i, bi)
    {
      subvar_t svars;
      tree var = referenced_var (i);
      if (var_can_have_subvars (var) && (svars = get_subvars_for_var (var)))
	{
	  subvar_t sv;
	  for (sv = svars; sv; sv = sv->next)
	    bitmap_set_bit (syms_to_rename, DECL_UID (sv->var));
	}
    }
#endif

  bitmap_ior_into (syms_to_rename, set);

  if (!need_to_update_vops_p)
    EXECUTE_IF_SET_IN_BITMAP (set, 0, i, bi)
      if (!is_gimple_reg (referenced_var (i)))
	{
	  need_to_update_vops_p = true;
	  break;
	}
}


/* Return true if there is any work to be done by update_ssa.  */

bool
need_ssa_update_p (void)
{
  return syms_to_rename || old_ssa_names || new_ssa_names;
}


/* Return true if name N has been registered in the replacement table.  */

bool
name_registered_for_update_p (tree n)
{
  if (!need_ssa_update_p ())
    return false;

  return is_new_name (n)
         || is_old_name (n)
	 || symbol_marked_for_renaming (SSA_NAME_VAR (n));
}


/* Return the set of all the SSA names marked to be replaced.  */

bitmap
ssa_names_to_replace (void)
{
  unsigned i = 0;
  bitmap ret;
  sbitmap_iterator sbi;
  
  ret = BITMAP_ALLOC (NULL);
  EXECUTE_IF_SET_IN_SBITMAP (old_ssa_names, 0, i, sbi)
    bitmap_set_bit (ret, i);

  return ret;
}


/* Mark NAME to be released after update_ssa has finished.  */

void
release_ssa_name_after_update_ssa (tree name)
{
  if (need_to_initialize_update_ssa_p)
    init_update_ssa ();

  if (names_to_release == NULL)
    names_to_release = BITMAP_ALLOC (NULL);

  bitmap_set_bit (names_to_release, SSA_NAME_VERSION (name));
}


/* Insert new PHI nodes to replace VAR.  DFS contains dominance
   frontier information.  BLOCKS is the set of blocks to be updated.

   This is slightly different than the regular PHI insertion
   algorithm.  The value of UPDATE_FLAGS controls how PHI nodes for
   real names (i.e., GIMPLE registers) are inserted:
 
   - If UPDATE_FLAGS == TODO_update_ssa, we are only interested in PHI
     nodes inside the region affected by the block that defines VAR
     and the blocks that define all its replacements.  All these
     definition blocks are stored in DEF_BLOCKS[VAR]->DEF_BLOCKS.

     First, we compute the entry point to the region (ENTRY).  This is
     given by the nearest common dominator to all the definition
     blocks. When computing the iterated dominance frontier (IDF), any
     block not strictly dominated by ENTRY is ignored.

     We then call the standard PHI insertion algorithm with the pruned
     IDF.

   - If UPDATE_FLAGS == TODO_update_ssa_full_phi, the IDF for real
     names is not pruned.  PHI nodes are inserted at every IDF block.  */

static void
insert_updated_phi_nodes_for (tree var, bitmap *dfs, bitmap blocks,
                              unsigned update_flags)
{
  basic_block entry;
  struct def_blocks_d *db;
  bitmap idf, pruned_idf;
  bitmap_iterator bi;
  unsigned i;

#if defined ENABLE_CHECKING
  if (TREE_CODE (var) == SSA_NAME)
    gcc_assert (is_old_name (var));
  else
    gcc_assert (var == mem_var || symbol_marked_for_renaming (var));
#endif

  /* Get all the definition sites for VAR.  */
  db = find_def_blocks_for (var);

  /* No need to do anything if there were no definitions to VAR.  */
  if (db == NULL || bitmap_empty_p (db->def_blocks))
    return;

  /* Compute the initial iterated dominance frontier.  */
  idf = compute_idf (db->def_blocks, dfs);
  pruned_idf = BITMAP_ALLOC (NULL);

  if (TREE_CODE (var) == SSA_NAME)
    {
      if (update_flags == TODO_update_ssa)
	{
	  /* If doing regular SSA updates for GIMPLE registers, we are
	     only interested in IDF blocks dominated by the nearest
	     common dominator of all the definition blocks.  */
	  entry = nearest_common_dominator_for_set (CDI_DOMINATORS,
						    db->def_blocks);
	  if (entry != ENTRY_BLOCK_PTR)
	    EXECUTE_IF_SET_IN_BITMAP (idf, 0, i, bi)
	      if (BASIC_BLOCK (i) != entry
		  && dominated_by_p (CDI_DOMINATORS, BASIC_BLOCK (i), entry))
		bitmap_set_bit (pruned_idf, i);
	}
      else
	{
	  /* Otherwise, do not prune the IDF for VAR.  */
	  gcc_assert (update_flags == TODO_update_ssa_full_phi);
	  bitmap_copy (pruned_idf, idf);
	}
    }
  else
    {
      /* Otherwise, VAR is a symbol that needs to be put into SSA form
	 for the first time, so we need to compute the full IDF for
	 it.  */
      bitmap_copy (pruned_idf, idf);
    }

  if (!bitmap_empty_p (pruned_idf))
    {
      /* Make sure that PRUNED_IDF blocks and all their feeding blocks
	 are included in the region to be updated.  The feeding blocks
	 are important to guarantee that the PHI arguments are renamed
	 properly.  */

      /* FIXME, this is not needed if we are updating symbols.  We are
	 already starting at the ENTRY block anyway.  */
      bitmap_ior_into (blocks, pruned_idf);
      EXECUTE_IF_SET_IN_BITMAP (pruned_idf, 0, i, bi)
	{
	  edge e;
	  edge_iterator ei;
	  basic_block bb = BASIC_BLOCK (i);

	  FOR_EACH_EDGE (e, ei, bb->preds)
	    if (e->src->index >= 0)
	      bitmap_set_bit (blocks, e->src->index);
	}

      insert_phi_nodes_for (var, pruned_idf, true);
    }

  BITMAP_FREE (pruned_idf);
  BITMAP_FREE (idf);
}


/* Heuristic to determine whether SSA name mappings for virtual names
   should be discarded and their symbols rewritten from scratch.  When
   there is a large number of mappings for virtual names, the
   insertion of PHI nodes for the old names in the mappings takes
   considerable more time than if we inserted PHI nodes for the
   symbols instead.

   Currently the heuristic takes these stats into account:

   	- Number of mappings for virtual SSA names.
	- Number of distinct virtual symbols involved in those mappings.

   If the number of virtual mappings is much larger than the number of
   virtual symbols, then it will be faster to compute PHI insertion
   spots for the symbols.  Even if this involves traversing the whole
   CFG, which is what happens when symbols are renamed from scratch.  */

static bool
switch_virtuals_to_full_rewrite_p (void)
{
  if (update_ssa_stats.num_virtual_mappings < (unsigned) MIN_VIRTUAL_MAPPINGS)
    return false;

  if (update_ssa_stats.num_virtual_mappings
      > (unsigned) VIRTUAL_MAPPINGS_TO_SYMS_RATIO
        * update_ssa_stats.num_virtual_symbols)
    return true;

  return false;
}


/* Remove every virtual mapping and mark all the affected virtual
   symbols for renaming.  */

static void
switch_virtuals_to_full_rewrite (void)
{
  unsigned i = 0;
  sbitmap_iterator sbi;

  if (dump_file)
    {
      fprintf (dump_file, "\nEnabled virtual name mapping heuristic.\n");
      fprintf (dump_file, "\tNumber of virtual mappings:       %7u\n",
	       update_ssa_stats.num_virtual_mappings);
      fprintf (dump_file, "\tNumber of unique virtual symbols: %7u\n",
	       update_ssa_stats.num_virtual_symbols);
      fprintf (dump_file, "Updating FUD-chains from top of CFG will be "
	                  "faster than processing\nthe name mappings.\n\n");
    }

  /* Remove all virtual names from NEW_SSA_NAMES and OLD_SSA_NAMES.
     Note that it is not really necessary to remove the mappings from
     REPL_TBL, that would only waste time.  */
  EXECUTE_IF_SET_IN_SBITMAP (new_ssa_names, 0, i, sbi)
    if (!is_gimple_reg (ssa_name (i)))
      RESET_BIT (new_ssa_names, i);

  EXECUTE_IF_SET_IN_SBITMAP (old_ssa_names, 0, i, sbi)
    if (!is_gimple_reg (ssa_name (i)))
      RESET_BIT (old_ssa_names, i);

  bitmap_ior_into (syms_to_rename, update_ssa_stats.virtual_symbols);
}


/* If there are any .MEM names that are marked to be released, we
   need to replace their immediate uses with the default definition
   for .MEM.  Consider this 

      	    struct { ... } x;
      	    if (i_12 > 10)
      		# .MEM_39 = VDEF <.MEM_4(D)>
      		x = y;
      	    else
      		# .MEM_15 = VDEF <.MEM_4(D)>
      		x = z;
      	    endif
      	    # .MEM_59 = PHI <.MEM_15, .MEM_39>

   After scalarization

      	    struct { ... } x;
      	    if (i_12 > 10)
      		x$a_40 = y$a_39;
      		x$b_41 = y$b_38;
      	    else
      		x$a_45 = y$a_35;
      		x$b_46 = y$b_34;
      	    endif
                  # .MEM_59 = PHI <.MEM_15, .MEM_39>
                  # x$a_60 = PHI <x$a_40, x$a_45>
      	    # x$b_61 = PHI <x$b_41, x$b_46>

   Both .MEM_15 and .MEM_39 have disappeared and have been marked
   for removal.  But since .MEM is not a symbol that can be marked
   for renaming, the PHI node for it remains in place.  Moreover,
   because 'x' has been scalarized, there will be no uses of .MEM_59
   downstream.  However, the SSA verifier will see uses of .MEM_15
   and .MEM_39 and trigger an ICE.  By replacing both of them with
   .MEM's default definition, we placate the verifier and maintain
   the removability of this PHI node.  */

static void
replace_stale_ssa_names (void)
{
  unsigned i;
  bitmap_iterator bi;
  tree new_name;

  if (names_to_release)
    {
      new_name = get_default_def_for (mem_var);
      EXECUTE_IF_SET_IN_BITMAP (names_to_release, 0, i, bi)
	{
	  /* The replacement name for every stale SSA name is the new
	     LHS of the VDEF operator in the original defining
	     statement.  */
	  tree use_stmt, old_name;
	  imm_use_iterator iter;

	  old_name = ssa_name (i);

	  /* We only care about .MEM.  All other symbols should've
	     been marked for renaming.  */
	  if (SSA_NAME_VAR (old_name) != mem_var)
	    continue;

	  FOR_EACH_IMM_USE_STMT (use_stmt, iter, old_name)
	    {
	      use_operand_p use_p;
	      FOR_EACH_IMM_USE_ON_STMT (use_p, iter)
		SET_USE (use_p, new_name);
	    }
	}
    }


  /* Replace every stale name with the new name created for the VDEF
     of its original defining statement.  */
  if (stale_ssa_names)
    EXECUTE_IF_SET_IN_BITMAP (stale_ssa_names, 0, i, bi)
      {
	/* The replacement name for every stale SSA name is the new
	   LHS of the VDEF operator in the original defining
	   statement.  */
	tree use_stmt, old_name, new_name;
	imm_use_iterator iter;

	old_name = ssa_name (i);
	new_name = VDEF_RESULT (VDEF_OPS (SSA_NAME_DEF_STMT (old_name)));

	FOR_EACH_IMM_USE_STMT (use_stmt, iter, old_name)
	  {
	    use_operand_p use_p;

	    FOR_EACH_IMM_USE_ON_STMT (use_p, iter)
	      SET_USE (use_p, new_name);
	  }

	release_ssa_name_after_update_ssa (old_name);
      }
}


/* Add STMT to *PHI_QUEUE_P or *STMT_QUEUE_P accordingly.
   STMTS_ADDED is the set of statements that have already been added
   to one of the queues.  */

static void
add_to_fixup_queues (tree stmt, VEC(tree, heap) **phi_queue_p,
		     VEC(tree, heap) **stmt_queue_p, htab_t stmts_added)
{
  void **slot;

  slot = htab_find_slot (stmts_added, stmt, INSERT);
  if (*slot == NULL)
    {
      if (TREE_CODE (stmt) == PHI_NODE)
	VEC_safe_push (tree, heap, *phi_queue_p, stmt);
      else
	VEC_safe_push (tree, heap, *stmt_queue_p, stmt);

      *slot = stmt;
    }
}

/* Helper for fixup_unfactored_phis.  Add all the immediate uses for
   SSA name PHI_LHS to *PHI_QUEUE_P or *STMT_QUEUE_P accordingly.
   STMTS_ADDED is the set of statements that have already been added
   to one of the queues.  */

static void
add_imm_uses_to_fixup_queues (tree phi_lhs,
			      VEC(tree, heap) **phi_queue_p,
			      VEC(tree, heap) **stmt_queue_p,
			      htab_t stmts_added)
{
  imm_use_iterator imm_iter;
  tree stmt;

  FOR_EACH_IMM_USE_STMT (stmt, imm_iter, phi_lhs)
    add_to_fixup_queues (stmt, phi_queue_p, stmt_queue_p, stmts_added);
}


/* Helper for fixup_unfactored_phis.  Set CURRDEF for all the symbols
   factored in NAME's defining statement.  If NAME is created by an
   unfactored PHI node, recursively inspect its children.  */

static void
compute_currdefs_for (tree name)
{
  bitmap syms;
  bitmap_iterator bi;
  unsigned i;
  unfactored_phis_t n;
  tree sym, child, stmt;

  /* The default definition for .MEM is a catchall name that only
     reaches symbols that have not been defined otherwise.  */
  if (name == default_def (mem_var))
    return;

  /* The name for a regular memory symbols only reaches that symbol.  */
  sym = SSA_NAME_VAR (name);
  if (sym != mem_var)
    {
      bitmap_set_bit (syms_to_rename, DECL_UID (sym));
      set_current_def (sym, name);
      return;
    }

  /* Otherwise, get all the symbols associated to this .MEM name.  */
  stmt = SSA_NAME_DEF_STMT (name);
  syms = get_loads_and_stores (stmt)->stores;
  bitmap_ior_into (syms_to_rename, syms);
  EXECUTE_IF_SET_IN_BITMAP (syms, 0, i, bi)
    set_current_def (referenced_var (i), name);

  /* If the defining statement is an unfactored PHI node, examine its
     children PHI nodes.  */
  if ((n = lookup_unfactored_phi (stmt)) != NULL)
    for (i = 0; VEC_iterate (tree, n->children, i, child); i++)
      compute_currdefs_for (PHI_RESULT (child));
}


/* For every unfactored PHI node P, process every immediate use
   through the renamer to account for the unfactoring.  Given

   	.MEM_10 = PHI <.MEM_3, ???>	{ a, b, c, d }

   Suppose that the second argument of .MEM_10 is reached by three
   different names: .MEM_5, c_8 and d_7.  This would have caused the
   following splitting of .MEM_10:

   	.MEM_10 = PHI <.MEM_3, .MEM_5>	{ a, b }
	c_11 = PHI <.MEM_3, c_8>	{ c }
	d_12 = PHI <.MEM_3, d_7>	{ d }

   Now, suppose that one of .MEM_10's immediate uses is the statement:

   	x_32 = *p_1			{ a, b, c, d }

   If x_32 was renamed *before* .MEM_10 was split, the renamer would
   have created:

   	# VUSE <.MEM_10>
	x_32 = *p_1

   But given the subsequent split, this is wrong because .MEM_10 only
   factors symbols { a, b }.  Therefore, we traverse all the immediate
   uses for unfactored PHI nodes and pass them through the renamer one
   more time.  This way, x_32 can be renamed to:

   	# VUSE <.MEM_10, c_11, d_12>
	x_32 = *p_1

   Note that this process is only ever done for PHI nodes whose
   immediate uses were renamed *before* the PHI node was split.  If we
   had managed to split the PHI node before renaming all its immediate
   uses, we wouldn't need this post-processing.

   Notice that, in general, it is not possible to guarantee the order
   in which basic blocks will be renamed.  When doing the dominator
   walk, we will sometimes visit the children of a given basic block
   before the block itself.  This is particularly true in the case of
   loops.  */

static void
fixup_unfactored_phis (void)
{
  unfactored_phis_t n;
  htab_t stmts_added;
  unsigned stmt_ix;
  VEC(tree, heap) *stmt_queue = NULL;
  VEC(tree, heap) *phi_queue = NULL;

  timevar_push (TV_TREE_SSA_FIX_UNFACTORED_UD);

  /* Add immediate uses for every unfactored PHI node to STMT_QUEUE or
     PHI_QUEUE accordingly.  */
  stmts_added = htab_create (50, htab_hash_pointer, htab_eq_pointer, NULL);

  for (n = first_unfactored_phi; n; n = n->next)
    add_imm_uses_to_fixup_queues (PHI_RESULT (n->phi), &phi_queue, &stmt_queue,
			          stmts_added);

  /* PHI nodes in PHI_QUEUE may need to be split, and they may also
     cause more PHI nodes to be split in turn.  */
  for (stmt_ix = 0; stmt_ix < VEC_length (tree, phi_queue); stmt_ix++)
    {
      int j;
      tree phi = VEC_index (tree, phi_queue, stmt_ix);
      tree phi_lhs = PHI_RESULT (phi);
      bool split_p;

      /* One or more arguments of PHI will be an unfactored PHI
	 node.  Compute CURRDEF for all the symbols stored by that
	 argument (and its children PHI nodes), and rewrite
	 PHI's argument.  */
      split_p = false;
      for (j = 0; j < PHI_NUM_ARGS (phi); j++)
	{
	  tree arg, arg_phi;
	  use_operand_p arg_p;
	  edge e;

	  e = PHI_ARG_EDGE (phi, j);
	  arg = PHI_ARG_DEF (phi, j);
	  arg_p = PHI_ARG_DEF_PTR (phi, j);

	  /* Ignore self-referential arguments.  */
	  if (arg == phi_lhs)
	    continue;

	  arg_phi = SSA_NAME_DEF_STMT (arg);
	  if (TREE_CODE (arg_phi) == PHI_NODE
	      && lookup_unfactored_phi (arg_phi))
	    {
	      /* If ARG is an unfactored PHI, its set of factored
		 symbols may have changed after this argument was
		 added by the renamer.  We need to recompute the
		 reaching definitions for all the symbols factored in
		 PHI and see if that causes PHI to be unfactored.  */
	      tree sym;

	      bitmap_clear (syms_to_rename);
	      compute_currdefs_for (arg);
	      sym = SSA_NAME_VAR (PHI_RESULT (phi));
	      if (sym == mem_var)
		split_p |= replace_factored_phi_argument (phi, e, e->dest);
	      else if (symbol_marked_for_renaming (sym))
		replace_use (arg_p, sym);

	      /* Set abnormal flags for ARG.  */
	      if (e->flags & EDGE_ABNORMAL)
		SSA_NAME_OCCURS_IN_ABNORMAL_PHI (USE_FROM_PTR (arg_p)) = 1;
	    }
	}

      /* If we had to split PHI while examining its arguments, add
	 PHI's immediate uses to the fixup queues.  */
      if (split_p)
	{
	  unfactored_phis_t n;
	  unsigned i;

	  add_imm_uses_to_fixup_queues (PHI_RESULT (phi), &phi_queue,
	                                &stmt_queue, stmts_added);

	  n = lookup_unfactored_phi (phi);
	  for (i = 0; i < VEC_length (tree, n->children); i++)
	    add_to_fixup_queues (VEC_index (tree, n->children, i), &phi_queue,
		                 NULL, stmts_added);
	}

      /* Allow PHI to be added to the fixup queues again.  In the case
	 of loops, two or more PHI nodes could be in a dependency
	 cycle.  Each will need to be visited twice before the
	 splitting stabilizes.  FIXME, prove.  */
      htab_remove_elt (stmts_added, (PTR) phi);
    }

  /* Once all the PHI nodes have been split, rewrite the operands of
     every affected statement.  */
  for (stmt_ix = 0; stmt_ix < VEC_length (tree, stmt_queue); stmt_ix++)
    {
      ssa_op_iter iter;
      tree stmt, use, *sorted_names;
      bool rewrite_p;
      int i, last, num_vops;

      bitmap_clear (syms_to_rename);

      stmt = VEC_index (tree, stmt_queue, stmt_ix);

      /* Sort VOPS in dominance numbering order.  This way, we
	 guarantee that CURRDEFs will be computed in the right order.
	 Suppose that STMT was originally:

	 	# VUSE <.MEM_14, .MEM_16>
		x_1 = *p_3

	 and both MEM_14 and MEM_16 are factored PHI nodes that were
	 split after STMT had been renamed.  We now need to replace
	 MEM_14 and MEM_16 with their respective children PHI nodes,
	 but since both names are factoring the same symbols, we have
	 to process them in dominator order.

	 This is were the dominance number is used.  (1) Since both
	 MEM_14 and MEM_16 reach STMT, we know that they must be on
	 the same dominance sub-tree (otherwise, they would have both
	 been merged into a PHI node), (2) so, if the dominance number
	 of MEM_14 is greater than the dominance number of MEM_16, it
	 means that MEM_14 post-dominates MEM_16.

	 Therefore, all the definitions made by MEM_14 occur *after*
	 those made by MEM_16.  So, before computing current
	 definitions, we sort the names by ascending dominance number.
	 This way, symbols will be assigned a CURRDEF in the correct
	 dominator ordering.  */
      num_vops = num_ssa_operands (stmt, SSA_OP_VIRTUAL_USES);
      sorted_names = XCNEWVEC (tree, num_vops);
      last = -1;
      FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_VIRTUAL_USES)
	{
	  unsigned int dn = get_name_dom_num (use);
	  gcc_assert (dn > 0);

	  i = last;
	  while (i >= 0 && get_name_dom_num (sorted_names[i]) > dn)
	    {
	      sorted_names[i + 1] = sorted_names[i];
	      i--;
	    }
	  sorted_names[i + 1] = use;
	  last++;
	}

      /* Now traverse the sorted list computing CURRDEFs for all the
	 reaching names.  */
      rewrite_p = false;
      for (i = 0; i < num_vops; i++)
	{
	  tree use = sorted_names[i];
	  tree def_stmt = SSA_NAME_DEF_STMT (use);

	  compute_currdefs_for (use);

	  /* We only need to rewrite STMT's operands if DEF_STMT is an
	     unfactored PHI node.  */
	  if (TREE_CODE (def_stmt) == PHI_NODE
	      && lookup_unfactored_phi (def_stmt))
	    rewrite_p = true;
	}

      free (sorted_names);

      if (rewrite_p)
	rewrite_update_memory_stmt (stmt, false);
    }

  VEC_free (tree, heap, phi_queue);
  VEC_free (tree, heap, stmt_queue);
  htab_delete (stmts_added);

  timevar_pop (TV_TREE_SSA_FIX_UNFACTORED_UD);
}


/* Given a set of newly created SSA names (NEW_SSA_NAMES) and a set of
   existing SSA names (OLD_SSA_NAMES), update the SSA form so that:

   1- The names in OLD_SSA_NAMES dominated by the definitions of
      NEW_SSA_NAMES are all re-written to be reached by the
      appropriate definition from NEW_SSA_NAMES.

   2- If needed, new PHI nodes are added to the iterated dominance
      frontier of the blocks where each of NEW_SSA_NAMES are defined.

   The mapping between OLD_SSA_NAMES and NEW_SSA_NAMES is setup by
   calling register_new_name_mapping for every pair of names that the
   caller wants to replace.

   The caller identifies the new names that have been inserted and the
   names that need to be replaced by calling register_new_name_mapping
   for every pair <NEW, OLD>.  Note that the function assumes that the
   new names have already been inserted in the IL.

   For instance, given the following code:

     1	L0:
     2	x_1 = PHI (0, x_5)
     3	if (x_1 < 10)
     4	  if (x_1 > 7)
     5	    y_2 = 0
     6	  else
     7	    y_3 = x_1 + x_7
     8	  endif
     9	  x_5 = x_1 + 1
     10   goto L0;
     11	endif

   Suppose that we insert new names x_10 and x_11 (lines 4 and 8).

     1	L0:
     2	x_1 = PHI (0, x_5)
     3	if (x_1 < 10)
     4	  x_10 = ...
     5	  if (x_1 > 7)
     6	    y_2 = 0
     7	  else
     8	    x_11 = ...
     9	    y_3 = x_1 + x_7
     10	  endif
     11	  x_5 = x_1 + 1
     12	  goto L0;
     13	endif

   We want to replace all the uses of x_1 with the new definitions of
   x_10 and x_11.  Note that the only uses that should be replaced are
   those at lines 5, 9 and 11.  Also, the use of x_7 at line 9 should
   *not* be replaced (this is why we cannot just mark symbol 'x' for
   renaming).

   Additionally, we may need to insert a PHI node at line 11 because
   that is a merge point for x_10 and x_11.  So the use of x_1 at line
   11 will be replaced with the new PHI node.  The insertion of PHI
   nodes is optional.  They are not strictly necessary to preserve the
   SSA form, and depending on what the caller inserted, they may not
   even be useful for the optimizers.  UPDATE_FLAGS controls various
   aspects of how update_ssa operates, see the documentation for
   TODO_update_ssa*.  */

void
update_ssa (unsigned update_flags)
{
  basic_block bb, start_bb;
  bitmap_iterator bi;
  unsigned i = 0;
  sbitmap tmp;
  bool insert_phi_p;
  sbitmap_iterator sbi;

  if (!need_ssa_update_p ())
    return;

  timevar_push (TV_TREE_SSA_INCREMENTAL);

  /* Initialize internal data needed by the renamer.  */
  init_ssa_renamer ();

  blocks_with_phis_to_rewrite = BITMAP_ALLOC (NULL);
  if (!phis_to_rewrite)
    phis_to_rewrite = VEC_alloc (tree_vec, heap, last_basic_block);
  blocks_to_update = BITMAP_ALLOC (NULL);

  /* Ensure that the dominance information is up-to-date.  */
  calculate_dominance_info (CDI_DOMINATORS);

  /* Only one update flag should be set.  */
  gcc_assert (update_flags == TODO_update_ssa
              || update_flags == TODO_update_ssa_no_phi
	      || update_flags == TODO_update_ssa_full_phi
	      || update_flags == TODO_update_ssa_only_virtuals);

  /* If we only need to update virtuals, remove all the mappings for
     real names before proceeding.  The caller is responsible for
     having dealt with the name mappings before calling update_ssa.  */
  if (update_flags == TODO_update_ssa_only_virtuals)
    {
      sbitmap_zero (old_ssa_names);
      sbitmap_zero (new_ssa_names);
      htab_empty (repl_tbl);
    }

  insert_phi_p = (update_flags != TODO_update_ssa_no_phi);

  if (insert_phi_p)
    {
      /* If the caller requested PHI nodes to be added, initialize
	 live-in information data structures (DEF_BLOCKS).  */

      /* For each SSA name N, the DEF_BLOCKS table describes where the
	 name is defined, which blocks have PHI nodes for N, and which
	 blocks have uses of N (i.e., N is live-on-entry in those
	 blocks).  */
      def_blocks = htab_create (num_ssa_names, def_blocks_hash,
				def_blocks_eq, def_blocks_free);
    }
  else
    {
      def_blocks = NULL;
    }

  /* Heuristic to avoid massive slow downs when the replacement
     mappings include lots of virtual names.  */
  if (insert_phi_p && switch_virtuals_to_full_rewrite_p ())
    switch_virtuals_to_full_rewrite ();

  /* If there are names defined in the replacement table, prepare
     definition and use sites for all the names in NEW_SSA_NAMES and
     OLD_SSA_NAMES.  */
  if (sbitmap_first_set_bit (new_ssa_names) >= 0)
    {
      prepare_names_to_update (insert_phi_p);

      /* If all the names in NEW_SSA_NAMES had been marked for
	 removal, and there are no symbols to rename, then there's
	 nothing else to do.  */
      if (sbitmap_first_set_bit (new_ssa_names) < 0
	  && bitmap_empty_p (syms_to_rename))
	goto done;
    }

  /* Next, determine the block at which to start the renaming process.  */
  if (!bitmap_empty_p (syms_to_rename))
    {
      /* If we have to rename some symbols from scratch, we need to
	 start the process at the root of the CFG.  FIXME, it should
	 be possible to determine the nearest block that had a
	 definition for each of the symbols that are marked for
	 updating.  For now this seems more work than it's worth.  */
      start_bb = ENTRY_BLOCK_PTR;

      /* Traverse the CFG looking for existing definitions and uses of
	 symbols in SYMS_TO_RENAME.  Mark interesting blocks and
	 statements and set local live-in information for the PHI
	 placement heuristics.  */
      prepare_block_for_update (start_bb, insert_phi_p);
    }
  else
    {
      /* Otherwise, the entry block to the region is the nearest
	 common dominator for the blocks in BLOCKS.  */
      start_bb = nearest_common_dominator_for_set (CDI_DOMINATORS,
						   blocks_to_update);
    }

  /* If requested, insert PHI nodes at the iterated dominance frontier
     of every block, creating new definitions for names in OLD_SSA_NAMES
     and for symbols in SYMS_TO_RENAME.  */
  if (insert_phi_p)
    {
      bitmap *dfs;

      /* If the caller requested PHI nodes to be added, compute
	 dominance frontiers.  */
      dfs = XNEWVEC (bitmap, last_basic_block);
      FOR_EACH_BB (bb)
	dfs[bb->index] = BITMAP_ALLOC (NULL);
      compute_dominance_frontiers (dfs);

      if (sbitmap_first_set_bit (old_ssa_names) >= 0)
	{
	  sbitmap_iterator sbi;

	  /* insert_update_phi_nodes_for will call add_new_name_mapping
	     when inserting new PHI nodes, so the set OLD_SSA_NAMES
	     will grow while we are traversing it (but it will not
	     gain any new members).  Copy OLD_SSA_NAMES to a temporary
	     for traversal.  */
	  sbitmap tmp = sbitmap_alloc (old_ssa_names->n_bits);
	  sbitmap_copy (tmp, old_ssa_names);
	  EXECUTE_IF_SET_IN_SBITMAP (tmp, 0, i, sbi)
	    insert_updated_phi_nodes_for (ssa_name (i), dfs, blocks_to_update,
	                                  update_flags);
	  sbitmap_free (tmp);
	}

      /* When updating virtual operands, insert PHI nodes for .MEM.
	 If needed, they will be split into individual symbol PHI
	 nodes during renaming.  */
      if (need_to_update_vops_p)
	insert_updated_phi_nodes_for (mem_var, dfs, blocks_to_update,
				      update_flags);

      EXECUTE_IF_SET_IN_BITMAP (syms_to_rename, 0, i, bi)
	{
	  /* We don't need to process virtual symbols here, as they
	     have been all handled by the .MEM PHI nodes above.  */
	  tree sym = referenced_var (i);
	  if (is_gimple_reg (sym))
	    insert_updated_phi_nodes_for (referenced_var (i), dfs,
					  blocks_to_update, update_flags);
	}

      FOR_EACH_BB (bb)
	BITMAP_FREE (dfs[bb->index]);
      free (dfs);

      /* Insertion of PHI nodes may have added blocks to the region.
	 We need to re-compute START_BB to include the newly added
	 blocks.  */
      if (start_bb != ENTRY_BLOCK_PTR)
	start_bb = nearest_common_dominator_for_set (CDI_DOMINATORS,
						     blocks_to_update);
    }

  /* Reset the current definition for name and symbol before renaming
     the sub-graph.  */
  EXECUTE_IF_SET_IN_SBITMAP (old_ssa_names, 0, i, sbi)
    set_current_def (ssa_name (i), NULL_TREE);

  EXECUTE_IF_SET_IN_BITMAP (syms_to_rename, 0, i, bi)
    set_current_def (referenced_var (i), NULL_TREE);

  /* Now start the renaming process at START_BB.  */
  tmp = sbitmap_alloc (last_basic_block);
  sbitmap_zero (tmp);
  EXECUTE_IF_SET_IN_BITMAP (blocks_to_update, 0, i, bi)
    SET_BIT (tmp, i);

  rewrite_blocks (start_bb, REWRITE_UPDATE, tmp);

  sbitmap_free (tmp);

  /* Debugging dumps.  */
  if (dump_file)
    {
      int c;
      unsigned i;

      dump_update_ssa (dump_file);

      fprintf (dump_file, "Incremental SSA update started at block: %d\n\n",
	       start_bb->index);

      c = 0;
      EXECUTE_IF_SET_IN_BITMAP (blocks_to_update, 0, i, bi)
	c++;
      fprintf (dump_file, "Number of blocks in CFG: %d\n", last_basic_block);
      fprintf (dump_file, "Number of blocks to update: %d (%3.0f%%)\n\n",
	       c, PERCENT (c, last_basic_block));

      if (dump_flags & TDF_DETAILS)
	{
	  fprintf (dump_file, "Affected blocks: ");
	  EXECUTE_IF_SET_IN_BITMAP (blocks_to_update, 0, i, bi)
	    fprintf (dump_file, "%u ", i);
	  fprintf (dump_file, "\n");
	}

      fprintf (dump_file, "\n\n");
    }

  /* If the update process generated stale SSA names, their immediate
     uses need to be replaced with the new name that was created in
     their stead.  */
  if (names_to_release || stale_ssa_names)
    replace_stale_ssa_names ();

  /* If the renamer had to split factored PHI nodes, we need to adjust
     the immediate uses for the split PHI nodes.  */
  if (unfactored_phis)
    fixup_unfactored_phis ();

  /* Free allocated memory.  */
done:
  in_ssa_p = true;
  delete_update_ssa ();

  timevar_pop (TV_TREE_SSA_INCREMENTAL);
}
