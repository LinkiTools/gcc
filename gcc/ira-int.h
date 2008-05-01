/* Integrated Register Allocator (IRA) intercommunication header file.
   Copyright (C) 2006, 2007, 2008
   Free Software Foundation, Inc.
   Contributed by Vladimir Makarov <vmakarov@redhat.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "cfgloop.h"
#include "ira.h"
#include "alloc-pool.h"

#ifdef ENABLE_CHECKING
#define ENABLE_IRA_CHECKING
#endif

#ifdef ENABLE_IRA_CHECKING
#define ira_assert(c) gcc_assert (c)
#else
#define ira_assert(c)
#endif

/* Compute register frequency from edge frequency FREQ.  It is
   analogous to REG_FREQ_FROM_BB.  When optimizing for size, or
   profile driven feedback is available and the function is never
   executed, frequency is always equivalent.  Otherwise rescale the
   edge frequency.  */
#define REG_FREQ_FROM_EDGE_FREQ(freq)					      \
  (optimize_size || (flag_branch_probabilities && !ENTRY_BLOCK_PTR->count)    \
   ? REG_FREQ_MAX : (freq * REG_FREQ_MAX / BB_FREQ_MAX)			      \
   ? (freq * REG_FREQ_MAX / BB_FREQ_MAX) : 1)

/* All natural loops.  */
extern struct loops ira_loops;

/* A modified value of flag `-fira-verbose' used internally.  */
extern int internal_flag_ira_verbose;

/* Dump file of the allocator if it is not NULL.  */
extern FILE *ira_dump_file;

/* Typedefs for pointers to allocno live range, allocno, and copy of
   allocnos.  */
typedef struct allocno_live_range *allocno_live_range_t;
typedef struct allocno *allocno_t;
typedef struct allocno_copy *copy_t;

/* Typedef for pointer to the subsequent structure.  */
typedef struct loop_tree_node *loop_tree_node_t;

/* In general case, IRA is a regional allocator.  The regions are
   nested and form a tree.  Currently regions are natural loops.  The
   following structure describes loop tree node (representing basic
   block or loop).  We need such tree because the loop tree from
   cfgloop.h is not convenient for the optimization: basic blocks are
   not a part of the tree from cfgloop.h.  We also use the nodes for
   storing additional information about basic blocks/loops for the
   register allocation purposes.  */
struct loop_tree_node
{
  /* The node represents basic block if children == NULL.  */
  basic_block bb;    /* NULL for loop.  */
  struct loop *loop; /* NULL for BB.  */
  /* The next node on the same father.  */
  loop_tree_node_t next;
  /* The first node immediately inside the node.  */
  loop_tree_node_t children;
  /* The node immediately containing given node.  */
  loop_tree_node_t father;

  /* Loop level in range [0, ira_loop_tree_height).  */
  int level;

  /* All the following members are defined only for nodes representing
     loops.  */

  /* Allocnos in the loop corresponding to their regnos.  If it is
     NULL the loop does not form a separate register allocation region
     (e.g. because it has abnormal enter/exit edges and we can not put
     code for register shuffling on the edges if a different
     allocation is used for a pseudo-register on different sides of
     the edges).  Caps are not in the map (remember we can have more
     one cap with the same regno in a region).  */
  allocno_t *regno_allocno_map;

  /* Maximal register pressure inside loop for given register class
     (defined only for the cover classes).  */
  int reg_pressure[N_REG_CLASSES];

  /* Numbers of allocnos referred in the loop node.  */
  bitmap mentioned_allocnos;

  /* Regnos of pseudos modified in the loop node (including its
     subloops).  */
  bitmap modified_regnos;

  /* Numbers of allocnos living at the loop borders.  */
  bitmap border_allocnos;

  /* Numbers of copies referred in the corresponding loop.  */
  bitmap local_copies;
};

/* The root of the loop tree corresponding to the all function.  */
extern loop_tree_node_t ira_loop_tree_root;

/* Height of the loop tree.  */
extern int ira_loop_tree_height;

/* All nodes representing basic blocks are referred through the
   following array.  We can not use basic block member `aux' for this
   because it is used for insertion of insns on edges.  */
extern loop_tree_node_t ira_bb_nodes;

/* Two access macros to the nodes representing basic blocks.  */
#if defined ENABLE_IRA_CHECKING && (GCC_VERSION >= 2007)
#define IRA_BB_NODE_BY_INDEX(index) __extension__			\
(({ loop_tree_node_t _node = (&ira_bb_nodes[index]);	\
     if (_node->children != NULL || _node->loop != NULL || _node->bb == NULL)\
       {								\
         fprintf (stderr,						\
                  "\n%s: %d: error in %s: it is not a block node\n",	\
                  __FILE__, __LINE__, __FUNCTION__);			\
         gcc_unreachable ();						\
       }								\
     _node; }))
#else
#define IRA_BB_NODE_BY_INDEX(index) (&ira_bb_nodes[index])
#endif

#define IRA_BB_NODE(bb) IRA_BB_NODE_BY_INDEX ((bb)->index)

/* All nodes representing loops are referred through the following
   array.  */
extern loop_tree_node_t ira_loop_nodes;

/* Two access macros to the nodes representing loops.  */
#if defined ENABLE_IRA_CHECKING && (GCC_VERSION >= 2007)
#define IRA_LOOP_NODE_BY_INDEX(index) __extension__			\
(({ loop_tree_node_t const _node = (&ira_loop_nodes[index]);\
     if (_node->children == NULL || _node->bb != NULL || _node->loop == NULL)\
       {								\
         fprintf (stderr,						\
                  "\n%s: %d: error in %s: it is not a loop node\n",	\
                  __FILE__, __LINE__, __FUNCTION__);			\
         gcc_unreachable ();						\
       }								\
     _node; }))
#else
#define IRA_LOOP_NODE_BY_INDEX(index) (&ira_loop_nodes[index])
#endif

#define IRA_LOOP_NODE(loop) IRA_LOOP_NODE_BY_INDEX ((loop)->num)



/* The structure describes program points where a given allocno lives.
   To save memory we store allocno conflicts only for the same cover
   class allocnos which is enough to assign hard registers.  To find
   conflicts for other allocnos (e.g. to assign stack memory slot) we
   use the live ranges.  If the live ranges of two allocnos are
   intersected, the allocnos are in conflict.  */
struct allocno_live_range
{
  /* Allocno whose live range is described by given structure.  */
  allocno_t allocno;
  /* Program point range.  */
  int start, finish;
  /* Next structure describing program points where the allocno
     lives.  */
  allocno_live_range_t next;
  /* Pointer to structures with the same start/finish.  */
  allocno_live_range_t start_next, finish_next;
};

/* Program points are enumerated by number from range 0..MAX_POINT-1.
   There are approximately two times more program points than insns.
   One program points correspond points between subsequent insns and
   other ones correspond to points after usage of input operands but
   before setting the output operands in insns.  */
extern int max_point;

/* Arrays of size MAX_POINT mapping a program point to the allocno
   live ranges with given start/finish point.  */
extern allocno_live_range_t *start_point_ranges, *finish_point_ranges;

/* A structure representing an allocno (allocation entity).  Allocno
   represents a pseudo-register in an allocation region.  If
   pseudo-register does not live in a region but it lives in the
   nested regions, it is represented in the region by special allocno
   called *cap*.  There may be more one cap representing the same
   pseudo-register in region.  It means that the corresponding
   pseudo-register lives in more one non-intersected subregion.  */
struct allocno
{
  /* The allocno order number starting with 0.  Each allocno has an
     unique number and the number is never changed for the
     allocno.  */
  int num;
  /* Regno for allocno or cap.  */
  int regno;
  /* Mode of the allocno which is the mode of the corresponding
     pseudo-register.  */
  enum machine_mode mode;
  /* Final rtx representation of the allocno.  */
  rtx reg;
  /* Hard register assigned to given allocno.  Negative value means
     that memory was allocated to the allocno.  During the reload,
     spilled allocno has value equal to the corresponding stack slot
     number (0, ...) - 2.  Value -1 is used for allocnos spilled by the
     reload (at this point pseudo-register has only one allocno) which
     did not get stack slot yet.  */
  int hard_regno;
  /* Allocnos with the same regno are linked by the following member.
     Allocnos corresponding to inner loops are first in the list (it
     corresponds to depth-first traverse of the loops).  */
  allocno_t next_regno_allocno;
  /* There may be different allocnos with the same regno in different
     regions.  Allocnos are bound to the corresponding loop tree node.
     Pseudo-register may have only one regular allocno with given loop
     tree node but more than one cap (see comments above).  */
  loop_tree_node_t loop_tree_node;
  /* Accumulated usage references of the allocno.  Here and below,
     word 'accumulated' means info for given region and all nested
     subregions.  In this case, 'accumulated' means sum of references
     of the corresponding pseudo-register in this region and in all
     nested subregions recursively. */
  int nrefs;
  /* Accumulated frequency of usage of the allocno.  */
  int freq;
  /* Register class which should be used for allocation for given
     allocno.  NO_REGS means that we should use memory.  */
  enum reg_class cover_class;
  /* Minimal accumulated cost of usage register of the cover class for
     the allocno.  */
  int cover_class_cost;
  /* Minimal accumulated, and updated costs of memory for the allocno.
     At the allocation start, the original and updated costs are
     equal.  The updated cost may be changed after finishing
     allocation in a region and starting allocation in a subregion.
     The change reflects the cost of spill/restore code on the
     subregion border if we assign memory to the pseudo in the
     subregion.  */
  int memory_cost, updated_memory_cost;
  /* Accumulated number of points where the allocno lives and there is
     excess pressure for its class.  Excess pressure for a register
     class at some point means that there are more allocnos of given
     register class living at the point than number of hard-registers
     of the class available for the allocation.  */
  int excess_pressure_points_num;
  /* Copies to other non-conflicting allocnos.  The copies can
     represent move insn or potential move insn usually because of two
     operand insn constraints.  */
  copy_t allocno_copies;
  /* It is a allocno (cap) representing given allocno on upper loop tree
     level.  */
  allocno_t cap;
  /* It is a link to allocno (cap) on lower loop level represented by
     given cap.  Null if given allocno is not a cap.  */
  allocno_t cap_member;
  /* Coalesced allocnos form a cyclic list.  One allocno given by
     FIRST_COALESCED_ALLOCNO represents all coalesced allocnos.  The
     list is chained by NEXT_COALESCED_ALLOCNO.  */
  allocno_t first_coalesced_allocno;
  allocno_t next_coalesced_allocno;
  /* Pointer to structures describing at what program point the
     allocno lives.  We always maintain the list in such way that *the
     ranges in the list are not intersected and ordered by decreasing
     their program points*.  */
  allocno_live_range_t live_ranges;
  /* Before building conflicts the two member values are
     correspondingly minimal and maximal points of the accumulated
     allocno live ranges.  After building conflicts the values are
     correspondingly minimal and maximal conflict ids of allocnos with
     which given allocno can conflict.  */
  int min, max;
  /* The unique member value represents given allocno in conflict bit
     vectors.  */
  int conflict_id;
  /* Vector of accumulated conflicting allocnos with NULL end marker
     (if CONFLICT_VEC_P is true) or conflict bit vector otherwise.
     Only allocnos with the same cover class are in the vector or in
     the bit vector.  */
  void *conflict_allocno_array;
  /* Allocated size of the previous array.  */
  unsigned int conflict_allocno_array_size;
  /* Number of accumulated conflicts in the vector of conflicting
     allocnos.  */
  int conflict_allocnos_num;
  /* Initial and accumulated hard registers conflicting with this
     allocno and as a consequences can not be assigned to the
     allocno.  */
  HARD_REG_SET conflict_hard_regs, total_conflict_hard_regs;
  /* Accumulated frequency of calls which given allocno
     intersects.  */
  int call_freq;
  /* Start index of calls intersected by the allocno in array
     regno_calls[regno].  */
  int calls_crossed_start;
  /* Length of the previous array (number of the intersected calls).  */
  int calls_crossed_num;
  /* Non NULL if we remove restoring value from given allocno to
     MEM_OPTIMIZED_DEST at loop exit (see ira-emit.c) because the
     allocno value is not changed inside the loop.  */
  allocno_t mem_optimized_dest;
  /* TRUE if the allocno assigned to memory was a destination of
     removed move (see ira-emit.c) at loop exit because the value of
     the corresponding pseudo-register is not changed inside the
     loop.  */
  unsigned int mem_optimized_dest_p : 1;
  /* TRUE if the corresponding pseudo-register has disjoint live
     ranges and the other allocnos of the pseudo-register except this
     one changed REG.  */
  unsigned int somewhere_renamed_p : 1;
  /* TRUE if allocno with the same REGNO in a subregion has been
     renamed, in other words, got a new pseudo-register.  */
  unsigned int child_renamed_p : 1;
  /* During the reload, value TRUE means that we should not reassign a
     hard register to the allocno got memory earlier.  It is set up
     when we removed memory-memory move insn before each iteration of
     the reload.  */
  unsigned int dont_reassign_p : 1;
#ifdef STACK_REGS
  /* Set to TRUE if allocno can't be assigned to the stack hard
     register correspondingly in this region and area including the
     region and all its subregions recursively.  */
  unsigned int no_stack_reg_p : 1, total_no_stack_reg_p : 1;
#endif
  /* TRUE value means that the allocno was not removed yet from the
     conflicting graph during colouring.  */
  unsigned int in_graph_p : 1;
  /* TRUE if a hard register or memory has been assigned to the
     allocno.  */
  unsigned int assigned_p : 1;
  /* TRUE if it is put on the stack to make other allocnos
     colorable.  */
  unsigned int may_be_spilled_p : 1;
  /* TRUE if conflicts for given allocno are represented by vector of
     pointers to the conflicting allocnos.  Otherwise, we use a bit
     vector where a bit with given index represents allocno with the
     same number.  */
  unsigned int conflict_vec_p : 1;
  /* Array of usage costs (accumulated and the one updated during
     coloring) for each hard register of the allocno cover class.  The
     member value can be NULL if all costs are the same and equal to
     COVER_CLASS_COST.  For example, the costs of two different hard
     registers can be different if one hard register is callee-saved
     and another one is callee-used and the allocno lives through
     calls.  Another example can be case when for some insn the
     corresponding pseudo-register value should be put in specific
     register class (e.g. AREG for x86) which is a strict subset of
     the allocno cover class (GENERAL_REGS for x86).  We have updated
     costs to reflect the situation when the usage cost of a hard
     register is decreased because the allocno is connected to another
     allocno by a copy and the another allocno has been assigned to
     the hard register.  */
  int *hard_reg_costs, *updated_hard_reg_costs;
  /* Array of decreasing costs (accumulated and the one updated during
     coloring) for allocnos conflicting with given allocno for hard
     regno of the allocno cover class.  The member value can be NULL
     if all costs are the same.  These costs are used to reflect
     preferences of other allocnos not assigned yet during assigning
     to given allocno.  */
  int *conflict_hard_reg_costs, *updated_conflict_hard_reg_costs;
  /* Number of the same cover class allocnos with TRUE in_graph_p
     value and conflicting with given allocno during each point of
     graph coloring.  */
  int left_conflicts_num;
  /* Number of hard registers of the allocno cover class really
     available for the allocno allocation.  */
  int available_regs_num;
  /* Allocnos in a bucket (used in coloring) chained by the following
     two members.  */
  allocno_t next_bucket_allocno;
  allocno_t prev_bucket_allocno;
  /* Used for temporary purposes.  */
  int temp;
};

/* All members of the allocno structures should be accessed only
   through the following macros.  */
#define ALLOCNO_NUM(A) ((A)->num)
#define ALLOCNO_REGNO(A) ((A)->regno)
#define ALLOCNO_REG(A) ((A)->reg)
#define ALLOCNO_NEXT_REGNO_ALLOCNO(A) ((A)->next_regno_allocno)
#define ALLOCNO_LOOP_TREE_NODE(A) ((A)->loop_tree_node)
#define ALLOCNO_CAP(A) ((A)->cap)
#define ALLOCNO_CAP_MEMBER(A) ((A)->cap_member)
#define ALLOCNO_CONFLICT_ALLOCNO_ARRAY(A) ((A)->conflict_allocno_array)
#define ALLOCNO_CONFLICT_ALLOCNO_ARRAY_SIZE(A) \
  ((A)->conflict_allocno_array_size)
#define ALLOCNO_CONFLICT_ALLOCNOS_NUM(A) \
  ((A)->conflict_allocnos_num)
#define ALLOCNO_CONFLICT_HARD_REGS(A) ((A)->conflict_hard_regs)
#define ALLOCNO_TOTAL_CONFLICT_HARD_REGS(A) ((A)->total_conflict_hard_regs)
#define ALLOCNO_NREFS(A) ((A)->nrefs)
#define ALLOCNO_FREQ(A) ((A)->freq)
#define ALLOCNO_HARD_REGNO(A) ((A)->hard_regno)
#define ALLOCNO_CALL_FREQ(A) ((A)->call_freq)
#define ALLOCNO_CALLS_CROSSED_START(A) ((A)->calls_crossed_start)
#define ALLOCNO_CALLS_CROSSED_NUM(A) ((A)->calls_crossed_num)
#define ALLOCNO_MEM_OPTIMIZED_DEST(A) ((A)->mem_optimized_dest)
#define ALLOCNO_MEM_OPTIMIZED_DEST_P(A) ((A)->mem_optimized_dest_p)
#define ALLOCNO_SOMEWHERE_RENAMED_P(A) ((A)->somewhere_renamed_p)
#define ALLOCNO_CHILD_RENAMED_P(A) ((A)->child_renamed_p)
#define ALLOCNO_DONT_REASSIGN_P(A) ((A)->dont_reassign_p)
#ifdef STACK_REGS
#define ALLOCNO_NO_STACK_REG_P(A) ((A)->no_stack_reg_p)
#define ALLOCNO_TOTAL_NO_STACK_REG_P(A) ((A)->total_no_stack_reg_p)
#endif
#define ALLOCNO_IN_GRAPH_P(A) ((A)->in_graph_p)
#define ALLOCNO_ASSIGNED_P(A) ((A)->assigned_p)
#define ALLOCNO_MAY_BE_SPILLED_P(A) ((A)->may_be_spilled_p)
#define ALLOCNO_CONFLICT_VEC_P(A) ((A)->conflict_vec_p)
#define ALLOCNO_MODE(A) ((A)->mode)
#define ALLOCNO_COPIES(A) ((A)->allocno_copies)
#define ALLOCNO_HARD_REG_COSTS(A) ((A)->hard_reg_costs)
#define ALLOCNO_UPDATED_HARD_REG_COSTS(A) ((A)->updated_hard_reg_costs)
#define ALLOCNO_CONFLICT_HARD_REG_COSTS(A) \
  ((A)->conflict_hard_reg_costs)
#define ALLOCNO_UPDATED_CONFLICT_HARD_REG_COSTS(A) \
  ((A)->updated_conflict_hard_reg_costs)
#define ALLOCNO_LEFT_CONFLICTS_NUM(A) ((A)->left_conflicts_num)
#define ALLOCNO_COVER_CLASS(A) ((A)->cover_class)
#define ALLOCNO_COVER_CLASS_COST(A) ((A)->cover_class_cost)
#define ALLOCNO_MEMORY_COST(A) ((A)->memory_cost)
#define ALLOCNO_UPDATED_MEMORY_COST(A) ((A)->updated_memory_cost)
#define ALLOCNO_EXCESS_PRESSURE_POINTS_NUM(A) ((A)->excess_pressure_points_num)
#define ALLOCNO_AVAILABLE_REGS_NUM(A) ((A)->available_regs_num)
#define ALLOCNO_NEXT_BUCKET_ALLOCNO(A) ((A)->next_bucket_allocno)
#define ALLOCNO_PREV_BUCKET_ALLOCNO(A) ((A)->prev_bucket_allocno)
#define ALLOCNO_TEMP(A) ((A)->temp)
#define ALLOCNO_FIRST_COALESCED_ALLOCNO(A) ((A)->first_coalesced_allocno)
#define ALLOCNO_NEXT_COALESCED_ALLOCNO(A) ((A)->next_coalesced_allocno)
#define ALLOCNO_LIVE_RANGES(A) ((A)->live_ranges)
#define ALLOCNO_MIN(A) ((A)->min)
#define ALLOCNO_MAX(A) ((A)->max)
#define ALLOCNO_CONFLICT_ID(A) ((A)->conflict_id)

/* Map regno -> allocnos with given regno (see comments for 
   allocno member `next_regno_allocno').  */
extern allocno_t *regno_allocno_map;

/* Array of references to all allocnos.  The order number of the
   allocno corresponds to the index in the array.  Removed allocnos
   have NULL element value.  */
extern allocno_t *allocnos;

/* Sizes of the previous array.  */
extern int allocnos_num;

/* Map conflict id -> allocno with given conflict id (see comments for
   allocno member `conflict_id').  */
extern allocno_t *conflict_id_allocno_map;

/* The following structure represents a copy of two allocnos.  The
   copies represent move insns or potential move insns usually because
   of two operand insn constraints. */
struct allocno_copy
{
  /* The unique order number of the copy node starting with 0.  */
  int num;
  /* Allocnos connected by the copy.  The first allocno should have
     smaller order number than the second one.  */
  allocno_t first, second;
  /* Execution frequency of the copy.  */
  int freq;
  /* It is an insn which is an origin of the copy.  It may be a move
     insn or insn whose operand should be the same as the result
     (2-operand insns).  To remove register shuffle, we create copies
     between allocno which is output of an insn and allocno becoming
     dead in the insn.  The member value for the copy created to
     remove register shuffle is NULL and the frequency is smaller than
     the corresponding insn execution frequency.  */
  rtx insn;
  /* All copies with the same allocno as FIRST are linked by the two
     following members.  */
  copy_t prev_first_allocno_copy, next_first_allocno_copy;
  /* All copies with the same allocno as SECOND are linked by the two
     following members.  */
  copy_t prev_second_allocno_copy, next_second_allocno_copy;
  /* Region from which given copy is originated.  */
  loop_tree_node_t loop_tree_node;
};

/* Array of references to all copies.  The order number of the copy
   corresponds to the index in the array.  Removed copies have NULL
   element value.  */
extern copy_t *copies;

/* Size of the previous array.  */
extern int copies_num;

/* The following structure describes a stack slot used for spilled
   pseudo-registers.  */
struct spilled_reg_stack_slot
{
  /* pseudo-registers assigned to the stack slot.  */
  regset_head spilled_regs;
  /* RTL representation of the stack slot.  */
  rtx mem;
  /* Size of the stack slot.  */
  unsigned int width;
};

/* The number of elements in the following array.  */
extern int spilled_reg_stack_slots_num;

/* The following array contains info about spilled pseudo-registers
   stack slots used in current function so far.  */
extern struct spilled_reg_stack_slot *spilled_reg_stack_slots;

/* Correspondingly overall cost of the allocation, cost of the
   allocnos assigned to hard-registers, cost of the allocnos assigned
   to memory, cost of loads, stores and register move insns generated
   for pseudo-register live range splitting (see ira-emit.c).  */
extern int overall_cost;
extern int reg_cost, mem_cost;
extern int load_cost, store_cost, shuffle_cost;
extern int move_loops_num, additional_jumps_num;

/* Map: register class x machine mode -> number of hard registers of
   given class needed to store value of given mode.  If the number for
   some hard-registers of the register class is different, the size
   will be negative.  */
extern int reg_class_nregs[N_REG_CLASSES][MAX_MACHINE_MODE];

/* Maximal value of the previous array elements.  */
extern int max_nregs;

/* The number of bits in each element of array used to implement a bit
   vector of allocnos and what type that element has.  We use the
   largest integer format on the host machine.  */
#define INT_BITS HOST_BITS_PER_WIDE_INT
#define INT_TYPE HOST_WIDE_INT

/* Set, clear or test bit number I in R, a bit vector of elements with
   minimal index and maximal index equal correspondingly to MIN and
   MAX.  */
#if defined ENABLE_IRA_CHECKING && (GCC_VERSION >= 2007)

#define SET_ALLOCNO_SET_BIT(R, I, MIN, MAX) __extension__	        \
  (({ int _min = (MIN), _max = (MAX), _i = (I);				\
     if (_i < _min || _i > _max)					\
       {								\
         fprintf (stderr,						\
                  "\n%s: %d: error in %s: %d not in range [%d,%d]\n",   \
                  __FILE__, __LINE__, __FUNCTION__, _i, _min, _max);	\
         gcc_unreachable ();						\
       }								\
     ((R)[(unsigned) (_i - _min) / INT_BITS]				\
      |= ((INT_TYPE) 1 << ((unsigned) (_i - _min) % INT_BITS))); }))
  

#define CLEAR_ALLOCNO_SET_BIT(R, I, MIN, MAX) __extension__	        \
  (({ int _min = (MIN), _max = (MAX), _i = (I);				\
     if (_i < _min || _i > _max)					\
       {								\
         fprintf (stderr,						\
                  "\n%s: %d: error in %s: %d not in range [%d,%d]\n",   \
                  __FILE__, __LINE__, __FUNCTION__, _i, _min, _max);	\
         gcc_unreachable ();						\
       }								\
     ((R)[(unsigned) (_i - _min) / INT_BITS]				\
      &= ~((INT_TYPE) 1 << ((unsigned) (_i - _min) % INT_BITS))); }))

#define TEST_ALLOCNO_SET_BIT(R, I, MIN, MAX) __extension__	        \
  (({ int _min = (MIN), _max = (MAX), _i = (I);				\
     if (_i < _min || _i > _max)					\
       {								\
         fprintf (stderr,						\
                  "\n%s: %d: error in %s: %d not in range [%d,%d]\n",   \
                  __FILE__, __LINE__, __FUNCTION__, _i, _min, _max);	\
         gcc_unreachable ();						\
       }								\
     ((R)[(unsigned) (_i - _min) / INT_BITS]				\
      & ((INT_TYPE) 1 << ((unsigned) (_i - _min) % INT_BITS))); }))

#else

#define SET_ALLOCNO_SET_BIT(R, I, MIN, MAX)			\
  ((R)[(unsigned) ((I) - (MIN)) / INT_BITS]			\
   |= ((INT_TYPE) 1 << ((unsigned) ((I) - (MIN)) % INT_BITS)))

#define CLEAR_ALLOCNO_SET_BIT(R, I, MIN, MAX)			\
  ((R)[(unsigned) ((I) - (MIN)) / INT_BITS]			\
   &= ~((INT_TYPE) 1 << ((unsigned) ((I) - (MIN)) % INT_BITS)))

#define TEST_ALLOCNO_SET_BIT(R, I, MIN, MAX)			\
  ((R)[(unsigned) ((I) - (MIN)) / INT_BITS]			\
   & ((INT_TYPE) 1 << ((unsigned) ((I) - (MIN)) % INT_BITS)))

#endif

/* The iterator for allocno set implemented ed as allocno bit
   vector.  */
typedef struct {

  /* Array containing the allocno bit vector.  */
  INT_TYPE *vec;

  /* The number of the current element in the vector.  */
  unsigned int word_num;

  /* The number of bits in the bit vector.  */
  unsigned int nel;

  /* The current bit index of the bit vector.  */
  unsigned int bit_num;

  /* Index corresponding to the 1st bit of the bit vector.   */
  int start_val;

  /* The word of the bit vector currently visited.  */
  unsigned INT_TYPE word;
} allocno_set_iterator;

/* Initialize the iterator I for allocnos bit vector VEC containing
   minimal and maximal values MIN and MAX.  */
static inline void
allocno_set_iter_init (allocno_set_iterator *i, INT_TYPE *vec, int min, int max)
{
  i->vec = vec;
  i->word_num = 0;
  i->nel = max < min ? 0 : max - min + 1;
  i->start_val = min;
  i->bit_num = 0;
  i->word = i->nel == 0 ? 0 : vec[0];
}

/* Return TRUE if we have more allocnos to visit, in which case *N is
   set to the allocno number to be visited.  Otherwise, return
   FALSE.  */
static inline int
allocno_set_iter_cond (allocno_set_iterator *i, int *n)
{
  /* Skip words that are zeros.  */
  for (; i->word == 0; i->word = i->vec[i->word_num])
    {
      i->word_num++;
      i->bit_num = i->word_num * INT_BITS;
      
      /* If we have reached the end, break.  */
      if (i->bit_num >= i->nel)
	return FALSE;
    }
  
  /* Skip bits that are zero.  */
  for (; (i->word & 1) == 0; i->word >>= 1)
    i->bit_num++;
  
  *n = (int) i->bit_num + i->start_val;
  
  return TRUE;
}

/* Advance to the next allocno in the set.  */
static inline void
allocno_set_iter_next (allocno_set_iterator *i)
{
  i->word >>= 1;
  i->bit_num++;
}

/* Loop over all elements of allocno set given by bit vector VEC and
   their minimal and maximal values MIN and MAX.  In each iteration, N
   is set to the number of next allocno.  ITER is an instance of
   allocno_set_iterator used to iterate the allocnos in the set.  */
#define FOR_EACH_ALLOCNO_IN_SET(VEC, MIN, MAX, N, ITER)		\
  for (allocno_set_iter_init (&(ITER), (VEC), (MIN), (MAX));	\
       allocno_set_iter_cond (&(ITER), &(N));			\
       allocno_set_iter_next (&(ITER)))

/* ira.c: */

/* Hard regsets whose all bits are correspondingly zero or one.  */
extern HARD_REG_SET zero_hard_reg_set;
extern HARD_REG_SET one_hard_reg_set;

/* Map: hard regs X modes -> set of hard registers for storing value
   of given mode starting with given hard register.  */
extern HARD_REG_SET reg_mode_hard_regset
                    [FIRST_PSEUDO_REGISTER][NUM_MACHINE_MODES];

/* Arrays analogous to macros MEMORY_MOVE_COST and
   REGISTER_MOVE_COST.  */
extern short memory_move_cost[MAX_MACHINE_MODE][N_REG_CLASSES][2];
extern move_table *register_move_cost[MAX_MACHINE_MODE];

/* Similar to may_move_in_cost but it is calculated in IRA instead of
   regclass.  Another difference we take only available hard registers
   into account to figure out that one register class is a subset of
   the another one.  */
extern move_table *register_may_move_in_cost[MAX_MACHINE_MODE];

/* Similar to may_move_out_cost but it is calculated in IRA instead of
   regclass.  Another difference we take only available hard registers
   into account to figure out that one register class is a subset of
   the another one.  */
extern move_table *register_may_move_out_cost[MAX_MACHINE_MODE];

/* Register class subset relation: TRUE if the first class is a subset
   of the second one considering only hard registers available for the
   allocation.  */
extern int class_subset_p[N_REG_CLASSES][N_REG_CLASSES];

/* Array of number of hard registers of given class which are
   available for the allocation.  The order is defined by the
   allocation order.  */
extern short class_hard_regs[N_REG_CLASSES][FIRST_PSEUDO_REGISTER];

/* The number of elements of the above array for given register
   class.  */
extern int class_hard_regs_num[N_REG_CLASSES];

/* Index (in class_hard_regs) for given register class and hard
   register (in general case a hard register can belong to several
   register classes).  The index is negative for hard registers
   unavailable for the allocation. */
extern short class_hard_reg_index[N_REG_CLASSES][FIRST_PSEUDO_REGISTER];

/* Function specific hard registers can not be used for the register
   allocation.  */
extern HARD_REG_SET no_alloc_regs;

/* Number of given class hard registers available for the register
   allocation for given classes.  */
extern int available_class_regs[N_REG_CLASSES];

/* Array whose values are hard regset of hard registers available for
   the allocation of given register class whose HARD_REGNO_MODE_OK
   values for given mode are zero.  */
extern HARD_REG_SET prohibited_class_mode_regs
                    [N_REG_CLASSES][NUM_MACHINE_MODES];

/* Array whose values are hard regset of hard registers for which
   move of the hard register in given mode into itself is
   prohibited.  */
extern HARD_REG_SET prohibited_mode_move_regs[NUM_MACHINE_MODES];

/* Number of cover classes.  Cover classes is non-intersected register
   classes containing all hard-registers available for the
   allocation.  */
extern int reg_class_cover_size;

/* The array containing cover classes (see also comments for macro
   IRA_COVER_CLASSES).  Only first REG_CLASS_COVER_SIZE elements are
   used for this.  */
extern enum reg_class reg_class_cover[N_REG_CLASSES];

/* The value is number of elements in the subsequent array.  */
extern int important_classes_num;

/* The array containing non-empty classes (including non-empty cover
   classes) which are subclasses of cover classes.  Such classes is
   important for calculation of the hard register usage costs.  */
extern enum reg_class important_classes[N_REG_CLASSES];

/* The array containing indexes of important classes in the previous
   array.  The array elements are defined only for important
   classes.  */
extern int important_class_nums[N_REG_CLASSES];

/* Map of all register classes to corresponding cover class containing
   the given class.  If given class is not a subset of a cover class,
   we translate it into the cheapest cover class.  */
extern enum reg_class class_translate[N_REG_CLASSES];

/* The biggest important class inside of intersection of the two
   classes (that is calculated taking only hard registers available
   for allocation into account).  If the both classes contain no hard
   registers available for allocation, the value is calculated with
   taking all hard-registers including fixed ones into account.  */
extern enum reg_class reg_class_intersect[N_REG_CLASSES][N_REG_CLASSES];

/* The biggest important class inside of union of the two classes
   (that is calculated taking only hard registers available for
   allocation into account).  If the both classes contain no hard
   registers available for allocation, the value is calculated with
   taking all hard-registers including fixed ones into account.  In
   other words, the value is the corresponding reg_class_subunion
   value.  */
extern enum reg_class reg_class_union[N_REG_CLASSES][N_REG_CLASSES];

extern void set_non_alloc_regs (int);
extern void *ira_allocate (size_t);
extern void *ira_reallocate (void *, size_t);
extern void ira_free (void *addr);
extern bitmap ira_allocate_bitmap (void);
extern void ira_free_bitmap (bitmap);
extern void print_disposition (FILE *);
extern void debug_disposition (void);
extern void debug_class_cover (void);
extern void init_register_move_cost (enum machine_mode);

/* The length of the two following arrays.  */
extern int reg_equiv_len;

/* The element value is TRUE if the corresponding regno value is
   invariant.  */
extern int *reg_equiv_invariant_p;

/* The element value is equiv constant of given pseudo-register or
   NULL_RTX.  */
extern rtx *reg_equiv_const;

/* ira-build.c */

/* The current loop tree node and its regno allocno map.  */
extern loop_tree_node_t ira_curr_loop_tree_node;
extern allocno_t *ira_curr_regno_allocno_map;

/* Array of vectors containing calls given pseudo-register lives
   through.  */
extern VEC(rtx, heap) **regno_calls;

extern int add_regno_call (int, rtx);

extern void debug_allocno_copies (allocno_t);

extern void traverse_loop_tree (int, loop_tree_node_t,
				void (*) (loop_tree_node_t),
				void (*) (loop_tree_node_t));
extern allocno_t create_allocno (int, int, loop_tree_node_t);
extern int conflict_vector_profitable_p (allocno_t, int);
extern void allocate_allocno_conflict_vec (allocno_t, int);
extern void allocate_allocno_conflicts (allocno_t, int);
extern void add_allocno_conflict (allocno_t, allocno_t);
extern void print_expanded_allocno (allocno_t);
extern allocno_live_range_t create_allocno_live_range (allocno_t, int, int,
						       allocno_live_range_t);
extern void finish_allocno_live_range (allocno_live_range_t);
extern void free_allocno_updated_costs (allocno_t);
extern copy_t create_copy (allocno_t, allocno_t, int, rtx, loop_tree_node_t);
extern void add_allocno_copy_to_list (copy_t);
extern void swap_allocno_copy_ends_if_necessary (copy_t);
extern void remove_allocno_copy_from_list (copy_t);
extern copy_t add_allocno_copy (allocno_t, allocno_t, int, rtx,
				loop_tree_node_t);

extern int *allocate_cost_vector (enum reg_class);
extern void free_cost_vector (int *, enum reg_class);

extern void ira_flattening (int, int);
extern int ira_build (int);
extern void ira_destroy (void);

/* ira-costs.c */
extern void init_ira_costs_once (void);
extern void init_ira_costs (void);
extern void finish_ira_costs_once (void);
extern void ira_costs (void);
extern void tune_allocno_costs_and_cover_classes (void);

/* ira-lives.c */

extern void rebuild_start_finish_chains (void);
extern void print_live_range_list (FILE *, allocno_live_range_t);
extern void debug_live_range_list (allocno_live_range_t);
extern void debug_allocno_live_ranges (allocno_t);
extern void debug_live_ranges (void);
extern void create_allocno_live_ranges (void);
extern void finish_allocno_live_ranges (void);

/* ira-conflicts.c */
extern int allocno_live_ranges_intersect_p (allocno_t, allocno_t);
extern int pseudo_live_ranges_intersect_p (int, int);
extern void debug_conflicts (int);
extern void ira_build_conflicts (void);

/* ira-color.c */
extern int loop_edge_freq (loop_tree_node_t, int, int);
extern void reassign_conflict_allocnos (int);
extern void initiate_ira_assign (void);
extern void finish_ira_assign (void);
extern void ira_color (void);

/* ira-emit.c */
extern void ira_emit (int);



/* The iterator for all allocnos.  */
typedef struct {
  /* The number of the current element in ALLOCNOS.  */
  int n;
} allocno_iterator;

/* Initialize the iterator I.  */
static inline void
allocno_iter_init (allocno_iterator *i)
{
  i->n = 0;
}

/* Return TRUE if we have more allocnos to visit, in which case *A is
   set to the allocno to be visited.  Otherwise, return FALSE.  */
static inline int
allocno_iter_cond (allocno_iterator *i, allocno_t *a)
{
  int n;

  for (n = i->n; n < allocnos_num; n++)
    if (allocnos[n] != NULL)
      {
	*a = allocnos[n];
	i->n = n + 1;
	return TRUE;
      }
  return FALSE;
}

/* Loop over all allocnos.  In each iteration, A is set to the next
   allocno.  ITER is an instance of allocno_iterator used to iterate
   the allocnos.  */
#define FOR_EACH_ALLOCNO(A, ITER)			\
  for (allocno_iter_init (&(ITER));			\
       allocno_iter_cond (&(ITER), &(A));)




/* The iterator for copies.  */
typedef struct {
  /* The number of the current element in COPIES.  */
  int n;
} copy_iterator;

/* Initialize the iterator I.  */
static inline void
copy_iter_init (copy_iterator *i)
{
  i->n = 0;
}

/* Return TRUE if we have more copies to visit, in which case *CP is
   set to the copy to be visited.  Otherwise, return FALSE.  */
static inline int
copy_iter_cond (copy_iterator *i, copy_t *cp)
{
  int n;

  for (n = i->n; n < copies_num; n++)
    if (copies[n] != NULL)
      {
	*cp = copies[n];
	i->n = n + 1;
	return TRUE;
      }
  return FALSE;
}

/* Loop over all copies.  In each iteration, C is set to the next
   copy.  ITER is an instance of copy_iterator used to iterate
   the copies.  */
#define FOR_EACH_COPY(C, ITER)				\
  for (copy_iter_init (&(ITER));			\
       copy_iter_cond (&(ITER), &(C));)




/* The iterator for allocno conflicts.  */
typedef struct {

  /* TRUE if the conflicts are represented by vector of allocnos.  */
  int allocno_conflict_vec_p;

  /* The conflict vector or conflict bit vector.  */
  void *vec;

  /* The number of the current element in the vector (of type
     allocno_t or INT_TYPE).  */
  unsigned int word_num;

  /* The bit vector size.  It is defined only if
     ALLOCNO_CONFLICT_VEC_P is FALSE.  */
  unsigned int size;

  /* The current bit index of bit vector.  It is defined only if
     ALLOCNO_CONFLICT_VEC_P is FALSE.  */
  unsigned int bit_num;

  /* Allocno conflict id corresponding to the 1st bit of the bit
     vector.  It is defined only if ALLOCNO_CONFLICT_VEC_P is
     FALSE.  */
  int base_conflict_id;

  /* The word of bit vector currently visited.  It is defined only if
     ALLOCNO_CONFLICT_VEC_P is FALSE.  */
  unsigned INT_TYPE word;
} allocno_conflict_iterator;

/* Initialize the iterator I with ALLOCNO conflicts.  */
static inline void
allocno_conflict_iter_init (allocno_conflict_iterator *i, allocno_t allocno)
{
  i->allocno_conflict_vec_p = ALLOCNO_CONFLICT_VEC_P (allocno);
  i->vec = ALLOCNO_CONFLICT_ALLOCNO_ARRAY (allocno);
  i->word_num = 0;
  if (i->allocno_conflict_vec_p)
    i->size = i->bit_num = i->base_conflict_id = i->word = 0;
  else
    {
      if (ALLOCNO_MIN (allocno) > ALLOCNO_MAX (allocno))
	i->size = 0;
      else
	i->size = ((ALLOCNO_MAX (allocno) - ALLOCNO_MIN (allocno) + INT_BITS)
		   / INT_BITS) * sizeof (INT_TYPE);
      i->bit_num = 0;
      i->base_conflict_id = ALLOCNO_MIN (allocno);
      i->word = (i->size == 0 ? 0 : ((INT_TYPE *) i->vec)[0]);
    }
}

/* Return TRUE if we have more conflicting allocnos to visit, in which
   case *A is set to the allocno to be visited.  Otherwise, return
   FALSE.  */
static inline int
allocno_conflict_iter_cond (allocno_conflict_iterator *i, allocno_t *a)
{
  allocno_t conflict_allocno;

  if (i->allocno_conflict_vec_p)
    {
      conflict_allocno = ((allocno_t *) i->vec)[i->word_num];
      if (conflict_allocno == NULL)
	return FALSE;
      *a = conflict_allocno;
      return TRUE;
    }
  else
    {
      /* Skip words that are zeros.  */
      for (; i->word == 0; i->word = ((INT_TYPE *) i->vec)[i->word_num])
	{
	  i->word_num++;
	  
	  /* If we have reached the end, break.  */
	  if (i->word_num * sizeof (INT_TYPE) >= i->size)
	    return FALSE;
	  
	  i->bit_num = i->word_num * INT_BITS;
	}
      
      /* Skip bits that are zero.  */
      for (; (i->word & 1) == 0; i->word >>= 1)
	i->bit_num++;
      
      *a = conflict_id_allocno_map[i->bit_num + i->base_conflict_id];
      
      return TRUE;
    }
}

/* Advance to the next conflicting allocno.  */
static inline void
allocno_conflict_iter_next (allocno_conflict_iterator *i)
{
  if (i->allocno_conflict_vec_p)
    i->word_num++;
  else
    {
      i->word >>= 1;
      i->bit_num++;
    }
}

/* Loop over all allocnos conflicting with ALLOCNO.  In each
   iteration, A is set to the next conflicting allocno.  ITER is an
   instance of allocno_conflict_iterator used to iterate the
   conflicts.  */
#define FOR_EACH_ALLOCNO_CONFLICT(ALLOCNO, A, ITER)			\
  for (allocno_conflict_iter_init (&(ITER), (ALLOCNO));			\
       allocno_conflict_iter_cond (&(ITER), &(A));			\
       allocno_conflict_iter_next (&(ITER)))



/* The function returns TRUE if hard registers starting with
   HARD_REGNO and containing value of MODE are not in set
   HARD_REGSET.  */
static inline int
hard_reg_not_in_set_p (int hard_regno, enum machine_mode mode,
		       HARD_REG_SET hard_regset)
{
  int i;

  ira_assert (hard_regno >= 0);
  for (i = hard_regno_nregs[hard_regno][mode] - 1; i >= 0; i--)
    if (TEST_HARD_REG_BIT (hard_regset, hard_regno + i))
      return FALSE;
  return TRUE;
}



/* To save memory we use a lazy approach for allocation and
   initialization of the cost vectors.  We do this only when it is
   really necessary.  */

/* Allocate cost vector *VEC for hard registers of COVER_CLASS and
   initialize the elements by VAL if it is necessary */
static inline void
allocate_and_set_costs (int **vec, enum reg_class cover_class, int val)
{
  int i, *reg_costs;
  int len;

  if (*vec != NULL)
    return;
  *vec = reg_costs = allocate_cost_vector (cover_class);
  len = class_hard_regs_num[cover_class];
  for (i = 0; i < len; i++)
    reg_costs[i] = val;
}

/* Allocate cost vector *VEC for hard registers of COVER_CLASS and
   copy values of vector SRC into the vector if it is necessary */
static inline void
allocate_and_copy_costs (int **vec, enum reg_class cover_class, int *src)
{
  int len;

  if (*vec != NULL || src == NULL)
    return;
  *vec = allocate_cost_vector (cover_class);
  len = class_hard_regs_num[cover_class];
  memcpy (*vec, src, sizeof (int) * len);
}

/* Allocate cost vector *VEC for hard registers of COVER_CLASS and
   copy values of vector SRC into the vector or initialize it by VAL
   (if SRC is null).  */
static inline void
allocate_and_set_or_copy_costs (int **vec, enum reg_class cover_class,
				int val, int *src)
{
  int i, *reg_costs;
  int len;

  if (*vec != NULL)
    return;
  *vec = reg_costs = allocate_cost_vector (cover_class);
  len = class_hard_regs_num[cover_class];
  if (src != NULL)
    memcpy (reg_costs, src, sizeof (int) * len);
  else
    {
      for (i = 0; i < len; i++)
	reg_costs[i] = val;
    }
}
