/* Natural loop functions
   Copyright (C) 1987, 1997, 1998, 1999, 2000, 2001, 2002, 2003
   Free Software Foundation, Inc.

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

/* Structure to hold decision about unrolling/peeling.  */
enum lpt_dec
{
  LPT_NONE,
  LPT_PEEL_COMPLETELY,
  LPT_PEEL_SIMPLE,
  LPT_UNROLL_CONSTANT,
  LPT_UNROLL_RUNTIME,
  LPT_UNROLL_STUPID
};

struct lpt_decision
{
  enum lpt_dec decision;
  unsigned times;
};

/* Description of loop for simple loop unrolling.  */
struct loop_desc
{
  int postincr;		/* 1 if increment/decrement is done after loop exit condition.  */
  rtx stride;		/* Value added to VAR in each iteration.  */
  rtx var;		/* Loop control variable.  */
  enum machine_mode inner_mode;
			/* The mode from that it is extended.  */
  enum rtx_code extend;	/* With this extend.  */
  rtx var_alts;		/* List of definitions of its initial value.  */
  rtx lim;		/* Expression var is compared with.  */
  rtx lim_alts;		/* List of definitions of its initial value.  */
  bool const_iter;      /* True if it iterates constant number of times.  */
  unsigned HOST_WIDE_INT niter;
			/* Number of iterations if it is constant.  */
  bool may_be_zero;     /* If we cannot determine that the first iteration will pass.  */
  enum rtx_code cond;	/* Exit condition.  */
  int neg;		/* Set to 1 if loop ends when condition is satisfied.  */
  edge out_edge;	/* The exit edge.  */
  edge in_edge;		/* And the other one.  */
  int n_branches;	/* Number of branches inside the loop.  */
};

/* Structure to hold information for each natural loop.  */
struct loop
{
  /* Index into loops array.  */
  int num;

  /* Basic block of loop header.  */
  basic_block header;

  /* Basic block of loop latch.  */
  basic_block latch;

  /* Basic block of loop preheader or NULL if it does not exist.  */
  basic_block pre_header;

  /* For loop unrolling/peeling decision.  */
  struct lpt_decision lpt_decision;

  /* Simple loop description.  */
  int simple;
  struct loop_desc desc;
  int has_desc;

  /* Number of loop insns.  */
  unsigned ninsns;

  /* Average number of executed insns per iteration.  */
  unsigned av_ninsns;

  /* Array of edges along the preheader extended basic block trace.
     The source of the first edge is the root node of preheader
     extended basic block, if it exists.  */
  edge *pre_header_edges;

  /* Number of edges along the pre_header extended basic block trace.  */
  int num_pre_header_edges;

  /* The first block in the loop.  This is not necessarily the same as
     the loop header.  */
  basic_block first;

  /* The last block in the loop.  This is not necessarily the same as
     the loop latch.  */
  basic_block last;

  /* Bitmap of blocks contained within the loop.  */
  sbitmap nodes;

  /* Number of blocks contained within the loop.  */
  unsigned num_nodes;

  /* Array of edges that enter the loop.  */
  edge *entry_edges;

  /* Number of edges that enter the loop.  */
  int num_entries;

  /* Array of edges that exit the loop.  */
  edge *exit_edges;

  /* Number of edges that exit the loop.  */
  int num_exits;

  /* Bitmap of blocks that dominate all exits of the loop.  */
  sbitmap exits_doms;

  /* The loop nesting depth.  */
  int depth;

  /* Superloops of the loop.  */
  struct loop **pred;

  /* The height of the loop (enclosed loop levels) within the loop
     hierarchy tree.  */
  int level;

  /* The outer (parent) loop or NULL if outermost loop.  */
  struct loop *outer;

  /* The first inner (child) loop or NULL if innermost loop.  */
  struct loop *inner;

  /* Link to the next (sibling) loop.  */
  struct loop *next;

  /* Loop that is copy of this loop.  */
  struct loop *copy;

  /* Nonzero if the loop is invalid (e.g., contains setjmp.).  */
  int invalid;

  /* Auxiliary info specific to a pass.  */
  void *aux;

  /* The following are currently used by loop.c but they are likely to
     disappear as loop.c is converted to use the CFG.  */

  /* Nonzero if the loop has a NOTE_INSN_LOOP_VTOP.  */
  rtx vtop;

  /* Nonzero if the loop has a NOTE_INSN_LOOP_CONT.
     A continue statement will generate a branch to NEXT_INSN (cont).  */
  rtx cont;

  /* The dominator of cont.  */
  rtx cont_dominator;

  /* The NOTE_INSN_LOOP_BEG.  */
  rtx start;

  /* The NOTE_INSN_LOOP_END.  */
  rtx end;

  /* For a rotated loop that is entered near the bottom,
     this is the label at the top.  Otherwise it is zero.  */
  rtx top;

  /* Place in the loop where control enters.  */
  rtx scan_start;

  /* The position where to sink insns out of the loop.  */
  rtx sink;

  /* List of all LABEL_REFs which refer to code labels outside the
     loop.  Used by routines that need to know all loop exits, such as
     final_biv_value and final_giv_value.

     This does not include loop exits due to return instructions.
     This is because all bivs and givs are pseudos, and hence must be
     dead after a return, so the presence of a return does not affect
     any of the optimizations that use this info.  It is simpler to
     just not include return instructions on this list.  */
  rtx exit_labels;

  /* The number of LABEL_REFs on exit_labels for this loop and all
     loops nested inside it.  */
  int exit_count;

  /* The number of probable iterations.  This is either an INTERVAL_CHREC
     or an INTEGER_CST.  */
  tree nb_iterations;
};

/* Flags for state of loop structure.  */
enum
{
  LOOPS_HAVE_PREHEADERS = 1,
  LOOPS_HAVE_SIMPLE_LATCHES = 2,
  LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS = 4
};

/* Structure to hold CFG information about natural loops within a function.  */
struct loops
{
  /* Number of natural loops in the function.  */
  unsigned num;

  /* Maximum nested loop level in the function.  */
  unsigned levels;

  /* Array of natural loop descriptors (scanning this array in reverse order
     will find the inner loops before their enclosing outer loops).  */
  struct loop *array;

  /* The above array is unused in new loop infrastructure and is kept only for
     purposes of the old loop optimizer.  Instead we store just pointers to
     loops here.  */
  struct loop **parray;

  /* Pointer to root of loop hierarchy tree.  */
  struct loop *tree_root;

  /* Information derived from the CFG.  */
  struct cfg
  {
    /* The ordering of the basic blocks in a depth first search.  */
    int *dfs_order;

    /* The reverse completion ordering of the basic blocks found in a
       depth first search.  */
    int *rc_order;
  } cfg;

  /* Headers shared by multiple loops that should be merged.  */
  sbitmap shared_headers;

  /* State of loops.  */
  int state;
};

/* Flags for loop discovery.  */

#define LOOP_TREE		1	/* Build loop hierarchy tree.  */
#define LOOP_PRE_HEADER		2	/* Analyze loop preheader.  */
#define LOOP_ENTRY_EDGES	4	/* Find entry edges.  */
#define LOOP_EXIT_EDGES		8	/* Find exit edges.  */
#define LOOP_EDGES		(LOOP_ENTRY_EDGES | LOOP_EXIT_EDGES)
#define LOOP_ALL	       15	/* All of the above  */

/* Loop recognition.  */
extern int flow_loops_find (struct loops *, int flags);
extern int flow_loops_update (struct loops *, int flags);
extern void flow_loops_free (struct loops *);
extern void flow_loops_dump (const struct loops *, FILE *,
			     void (*)(const struct loop *, FILE *, int), int);
extern void flow_loop_dump (const struct loop *, FILE *,
			    void (*)(const struct loop *, FILE *, int), int);
extern int flow_loop_scan (struct loop *, int);
extern void flow_loop_free (struct loop *);
void mark_irreducible_loops (struct loops *);
extern void create_loop_notes (void);

/* Loop data structure manipulation/querying.  */
extern void flow_loop_tree_node_add (struct loop *, struct loop *);
extern void flow_loop_tree_node_remove (struct loop *);
extern bool flow_loop_outside_edge_p (const struct loop *, edge);
extern bool flow_loop_nested_p	(const struct loop *, const struct loop *);
extern bool flow_bb_inside_loop_p (const struct loop *, const basic_block);
extern struct loop * find_common_loop (struct loop *, struct loop *);
struct loop *superloop_at_depth (struct loop *, unsigned);
extern int num_loop_insns (struct loop *);
extern int average_num_loop_insns (struct loop *);
extern unsigned get_loop_level (const struct loop *);

/* Loops & cfg manipulation.  */
extern basic_block *get_loop_body (const struct loop *);
extern basic_block *get_loop_body_in_dom_order (const struct loop *);
extern edge *get_loop_exit_edges (const struct loop *, unsigned *);

extern edge loop_preheader_edge (const struct loop *);
extern edge loop_latch_edge (const struct loop *);

extern void add_bb_to_loop (basic_block, struct loop *);
extern void remove_bb_from_loops (basic_block);

extern void cancel_loop (struct loops *, struct loop *);
extern void cancel_loop_tree (struct loops *, struct loop *);

extern basic_block loop_split_edge_with (edge, rtx);
extern int fix_loop_placement (struct loop *);

enum
{
  CP_SIMPLE_PREHEADERS = 1
};

extern void create_preheaders (struct loops *, int);
extern void force_single_succ_latches (struct loops *);

extern void verify_loop_structure (struct loops *);

/* Loop analysis.  */
extern bool simple_loop_p (struct loop *, struct loop_desc *);
extern rtx count_loop_iterations (struct loop_desc *, rtx, rtx);
extern bool just_once_each_iteration_p (struct loop *, basic_block);
extern unsigned expected_loop_iterations (const struct loop *);

/* Loop manipulation.  */
extern bool can_duplicate_loop_p (struct loop *loop);

#define DLTHE_FLAG_UPDATE_FREQ	1	/* Update frequencies in
					   duplicate_loop_to_header_edge.  */

extern int duplicate_loop_to_header_edge (struct loop *, edge, struct loops *,
					  unsigned, sbitmap, edge, edge *,
					  unsigned *, int);
extern struct loop *loopify (struct loops *, edge, edge, basic_block);
extern void unloop (struct loops *, struct loop *);
extern bool remove_path (struct loops *, edge);
extern edge split_loop_bb (basic_block, rtx);

/* Induction variable analysis.  */

struct rtx_iv
{
  bool analysed;

  enum machine_mode mode;
  rtx base, step;
};

/* This should replace struct loop_desc.  We need to handle this in unrolling,
   so leave it this way for now.  */

struct niter_desc
{
  /* The edge out of the loop.  */
  edge out_edge;

  /* The other edge leading from the condition.  */
  edge in_edge;

  /* True if we are able to say anything about number of iterations of the
     loop.  */
  bool simple_p;

  /* True if the loop iterates the constant number of times.  */
  bool const_iter;

  /* Number of iterations if constant.  */
  unsigned HOST_WIDEST_INT niter;

  /* Upper bound on the number of iterations.  */
  unsigned HOST_WIDEST_INT niter_max;

  /* Assumptions under that the rest of the information is valid.  */
  rtx assumptions;

  /* Assumptions under that the loop ends before reaching the latch,
     even if value of niter_expr says otherwise.  */

  rtx noloop_assumptions;

  /* Condition under that the loop is infinite.  */
  rtx infinite;

  /* Whether the comparison is signed.  */
  bool signed_p;

  /* The mode in that niter_expr should be computed.  */
  enum machine_mode mode;

  /* The number of iterations of the loop.  */
  rtx niter_expr;
};

extern void iv_analysis_loop_init (struct loop *);
extern rtx iv_get_reaching_def (rtx, rtx);
extern bool iv_analyse (rtx, rtx, struct rtx_iv *);
extern void find_simple_exit (struct loop *, struct niter_desc *);
extern void iv_number_of_iterations (struct loop *, rtx, rtx,
				     struct niter_desc *);
extern void iv_analysis_done (void);

/* Loop optimizer initialization.  */
extern struct loops *loop_optimizer_init (FILE *);
extern void loop_optimizer_finalize (struct loops *, FILE *);

/* Optimization passes.  */
extern void unswitch_loops (struct loops *);

enum
{
  UAP_PEEL = 1,		/* Enables loop peeling.  */
  UAP_UNROLL = 2,	/* Enables peeling of loops if it seems profitable.  */
  UAP_UNROLL_ALL = 4	/* Enables peeling of all loops.  */
};

extern void unroll_and_peel_loops (struct loops *, int);
extern void doloop_optimize_loops (struct loops *);

static inline struct loop *loop_from_num (struct loops *, unsigned);
static inline struct loop *outer_loop (struct loop *);
static inline struct loop *inner_loop (struct loop *);
static inline struct loop *next_loop (struct loop *);
static inline unsigned loop_num (struct loop *);
static inline unsigned loop_depth (struct loop *);
static inline basic_block loop_header (struct loop *);
static inline tree loop_nb_iterations (struct loop *);
static inline unsigned loop_num_exits (struct loop *);
static inline edge *loop_exit_edges (struct loop *);
static inline edge loop_exit_edge (struct loop *, unsigned);

static inline struct loop *
loop_from_num (struct loops *loops, 
	       unsigned num)
{
  return loops->parray[num];
}

/* Returns the outer loop.  */

static inline struct loop *
outer_loop (struct loop *loop)
{
  return loop->outer;
}

/* Returns the inner loop.  */

static inline struct loop *
inner_loop (struct loop *loop)
{
  return loop->inner;
}

/* Returns the next loop.  */

static inline struct loop *
next_loop (struct loop *loop)
{
  return loop->next;
}

/* Returns the number of a loop.  */

static inline unsigned
loop_num (struct loop *loop)
{
  return loop->num;
}

/* Returns the depth of a loop.  */

static inline unsigned
loop_depth (struct loop *loop)
{
  return loop->depth;
}

/* Returns the header basic block of the loop.  */

static inline basic_block
loop_header (struct loop *loop)
{
  return loop->header;
}

/* Returns the number of iterations in the loop.  */

static inline tree
loop_nb_iterations (struct loop *loop)
{
  return loop->nb_iterations;
}

/* Returns the number of exit edges of the loop.  */

static inline unsigned
loop_num_exits (struct loop *loop)
{
  return loop->num_exits;
}

/* Returns the exit edges of the loop.  */

static inline edge *
loop_exit_edges (struct loop *loop)
{
  return loop->exit_edges;
}

/* Returns the n-th exit edge of the loop.  */

static inline edge 
loop_exit_edge (struct loop *loop, unsigned n)
{
  return loop->exit_edges[n];
}

