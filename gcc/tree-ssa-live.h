/* Routines for liveness in SSA trees.
   Copyright (C) 2003 Free Software Foundation, Inc.
   Contributed by Andrew MacLeod  <amacleod@redhat.com>

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


#ifndef _TREE_SSA_LIVE_H__
#define _TREE_SSA_LIVE_H 1

/* Used to create the variable mapping when we go out of SSA form.  */
typedef struct _var_map
{
  /* The partition of all variables.  */
  partition var_partition;

  /* Vector for compacting partitions.  */
  int *partition_to_compact;
  int *compact_to_partition;

  /* Mapping of partition numbers to vars.  */
  tree *partition_to_var;

  /* Current number of partitions.  */
  unsigned int num_partitions;

  /* Original partition size.  */
  unsigned int partition_size;
} *var_map;

#define VAR_ANN_PARTITION(ann) (ann->partition)
#define VAR_ANN_ROOT_INDEX(ann) (ann->root_index)

#define NO_PARTITION		-1

/* Flags to pass to compact_var_map  */

#define VARMAP_NORMAL		0
#define VARMAP_NO_SINGLE_DEFS	1

extern var_map init_var_map (int);
extern void delete_var_map (var_map);
extern void dump_var_map (FILE *, var_map);
extern int var_union (var_map, tree, tree);
extern void change_partition_var (var_map, tree, int);
extern var_map create_ssa_var_map (void);
extern void compact_var_map (var_map, int);

static inline int num_var_partitions (var_map);
static inline tree var_to_partition_to_var (var_map, tree);
static inline tree partition_to_var (var_map, int);
static inline int var_to_partition (var_map, tree);

/* Number of partitions.  */

static inline int 
num_var_partitions (var_map map)
{
  return map->num_partitions;
}

 
/* Given a partition number, return the variable which represents that 
   partition.  */
 
static inline tree
partition_to_var (var_map map, int i)
{
  if (map->compact_to_partition)
    i = map->compact_to_partition[i];
  i = partition_find (map->var_partition, i);
  return map->partition_to_var[i];
}

/* Given a variable, return the partition number which contains it.  
   NO_PARTITION is returned if its not in any partition.  */

static inline int
var_to_partition (var_map map, tree var)
{
  var_ann_t ann;
  int part;

  if (TREE_CODE (var) == SSA_NAME)
    {
      part = partition_find (map->var_partition, SSA_NAME_VERSION (var));
      if (map->partition_to_compact)
	part = map->partition_to_compact[part];
    }
  else
    {
      ann = var_ann (var);
      if (ann->out_of_ssa_tag)
	part = VAR_ANN_PARTITION (ann);
      else
        part = NO_PARTITION;
    }
  return part;
}

/* Given a variable, return the variable which represents the entire partition
   the specified one is a member of.  */

static inline tree
var_to_partition_to_var (var_map map, tree var)
{
  int part;

  part = var_to_partition (map, var);
  if (part == NO_PARTITION)
    return NULL_TREE;
  return partition_to_var (map, part);
}

/*  ---------------- live on entry/exit info ------------------------------  

    This structure is used to represent live range information on SSA based
    trees. A partition map must be provided, and based on the active partitions,
    live-on-entry information and live-on-exit information can be calculated.
    As well, partitions are marked as to whether they are global (live 
    outside the basic block they are defined in).

    The live-on-entry information is per variable. It provide a bitmap for 
    each variable which has a bit set for each basic block that the variable
    is live on entry to that block.

    The live-on-exit information is per block. It provides a bitmap for each
    block indicating which partitions are live on exit from the block.

    For the purposes of this implementation, we treat the elements of a PHI 
    as follows:

       Uses in a PHI are considered LIVE-ON-EXIT to the block from which they
       originate. They are *NOT* considered live on entry to the block
       containing the PHI node.

       The Def of a PHI node is *not* considered live on entry to the block.
       It is considered to be "define early" in the block. Picture it as each
       block having a stmt (or block-preheader) before the first real stmt in 
       the block which defines all the variables that are defined by PHIs.
   
    -----------------------------------------------------------------------  */


typedef struct tree_live_info_d
{
  /* Var map this relates to.  */
  var_map map;

  /* Bitmap indicating which partitions are global.  */
  sbitmap global;

  /* Bitmap of live on entry blocks for partition elements.  */
  sbitmap *livein;

  /* Number of basic blocks when live on exit calculated.  */
  int num_blocks;

  /* Bitmap of what variables are live on exit for a basic blocks.  */
  sbitmap *liveout;
} *tree_live_info_p;


extern tree_live_info_p calculate_live_on_entry (var_map);
extern void calculate_live_on_exit (tree_live_info_p);
extern void delete_tree_live_info (tree_live_info_p);


static inline int partition_is_global (tree_live_info_p, int);
static inline sbitmap live_entry_blocks (tree_live_info_p, int);
static inline sbitmap live_on_exit (tree_live_info_p, basic_block);


static inline int
partition_is_global (tree_live_info_p live, int p)
{
  if (!live->global)
    abort ();

  return TEST_BIT (live->global, p);
}

static inline sbitmap
live_entry_blocks (tree_live_info_p live, int p)
{
  if (!live->livein)
    abort ();

  return live->livein[p];
}

static inline sbitmap
live_on_exit (tree_live_info_p live, basic_block bb)
{
  if (!live->liveout)
    abort();

  if (bb == ENTRY_BLOCK_PTR || bb == EXIT_BLOCK_PTR)
    abort ();
  
  return live->liveout[bb->index];
}


/* Once a var_map has been created and compressed, a complimentary root_var
   object can be built.  This creates a list of all the root variables from
   which ssa version names are derived.  Each root variable has a list of 
   which partitions are versions of that root.  

   A varray of tree elements represent each distinct root variable.
   A parallel array of ints represent a partition number that is a version
     of the root variable.
   This partition number is then used as in index into the next_partition
   array, which returns the index of the next partition which is a version
   of the root var. ROOT_VAR_NONE indicates the end of the list.  

   ************************************************************************/


typedef struct root_var_d
{
  varray_type root_var;
  varray_type first_partition;
  int *next_partition;
  int num_root_vars;
  var_map map;
} *root_var_p;

static inline tree root_var (root_var_p, int);
static inline int first_root_var_partition (root_var_p, int);
static inline int next_root_var_partition (root_var_p, int);
static inline int num_root_vars (root_var_p);
static inline int find_root_var (root_var_p, int);
extern root_var_p init_root_var (var_map);
extern void delete_root_var (root_var_p);
extern void dump_root_var (FILE *, root_var_p);
extern void remove_root_var_partition (root_var_p, int, int);

/* Value returned when there are no more partitions associated with a root
   variable.  */
#define ROOT_VAR_NONE		-1

/* Number of distinct root variables.  */
static inline int 
num_root_vars (root_var_p rv)
{
  return rv->num_root_vars;
}

/* A specific root variable.  */
static inline tree
root_var (root_var_p rv, int i)
{
  return VARRAY_TREE (rv->root_var, i);
}

/* First partition belonging to a root variable version.  */
static inline int
first_root_var_partition (root_var_p rv, int i)
{
  return VARRAY_INT (rv->first_partition, i);
}

/* Next partition belonging to a root variable partition list.  */
static inline int
next_root_var_partition (root_var_p rv, int i)
{
  return rv->next_partition[i];
}

/* Find the root_var index for a specific partition.  */
static inline int
find_root_var (root_var_p rv, int i)
{
  tree t;
  var_ann_t ann;

  t = partition_to_var (rv->map, i);
  if (TREE_CODE (t) == SSA_NAME)
    t = SSA_NAME_VAR (t);
  ann = var_ann (t);
  return (VAR_ANN_ROOT_INDEX (ann));
}

#endif /* _TREE_SSA_LIVE_H  */
