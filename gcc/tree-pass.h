/* Definitions for describing one tree-ssa optimization pass.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>

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
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */


#ifndef GCC_TREE_PASS_H
#define GCC_TREE_PASS_H 1

/* Global variables used to communicate with passes.  */
extern FILE *tree_dump_file;
extern int tree_dump_flags;

extern struct bitmap_head_def *vars_to_rename;

/* Describe one pass.  */
struct tree_opt_pass
{
  /* Terse name of the pass used as a fragment of the dump file name.  */
  const char *name;

  /* If non-null, this pass and all sub-passes are executed only if
     the function returns true.  */
  bool (*gate) (void);

  /* This is the code to run.  If null, then there should be sub-passes
     otherwise this pass does nothing.  */
  void (*execute) (void);

  /* A list of sub-passes to run, dependent on gate predicate.  */
  struct tree_opt_pass *sub;

  /* Next in the list of passes to run, independent of gate predicate.  */
  struct tree_opt_pass *next;

  /* Static pass number, used as a fragment of the dump file name.  */
  unsigned int static_pass_number;

  /* The timevar id associated with this pass.  */
  /* ??? Ideally would be dynamically assigned.  */
  unsigned int tv_id;

  /* Sets of properties input and output from this pass.  */
  unsigned int properties_required;
  unsigned int properties_provided;
  unsigned int properties_destroyed;

  /* Flags indicating common sets things to do before and after.  */
  unsigned int todo_flags_start;
  unsigned int todo_flags_finish;
};

/* Pass properties.  */
#define PROP_gimple_any		(1 << 0)	/* entire gimple grammar */
#define PROP_gimple_lcf		(1 << 1)	/* lowered control flow */
#define PROP_gimple_leh		(1 << 2)	/* lowered eh */
#define PROP_cfg		(1 << 3)
#define PROP_referenced_vars	(1 << 4)
#define PROP_pta		(1 << 5)
#define PROP_ssa		(1 << 6)
#define PROP_no_crit_edges      (1 << 7)
#define PROP_scev               (1 << 8)
/* To-do flags.  */
#define TODO_dump_func		(1 << 0)	/* pass doesn't dump itself */
#define TODO_rename_vars	(1 << 1)	/* rewrite new vars to ssa */
#define TODO_redundant_phi	(1 << 2)	/* kill_redundant_phi_nodes */
#define TODO_ggc_collect	(1 << 3)	/* run the collector */
#define TODO_verify_ssa		(1 << 4)
#define TODO_verify_flow	(1 << 5)
#define TODO_verify_stmts	(1 << 6)

#define TODO_verify_all \
  (TODO_verify_ssa | TODO_verify_flow | TODO_verify_stmts)


extern struct tree_opt_pass pass_mudflap_1;
extern struct tree_opt_pass pass_mudflap_2;
extern struct tree_opt_pass pass_remove_useless_stmts;
extern struct tree_opt_pass pass_lower_cf;
extern struct tree_opt_pass pass_lower_eh;
extern struct tree_opt_pass pass_build_cfg;
extern struct tree_opt_pass pass_referenced_vars;
extern struct tree_opt_pass pass_build_pta;
extern struct tree_opt_pass pass_del_pta;
extern struct tree_opt_pass pass_sra;
extern struct tree_opt_pass pass_tail_recursion;
extern struct tree_opt_pass pass_tail_calls;
extern struct tree_opt_pass pass_loop;
extern struct tree_opt_pass pass_scev;
extern struct tree_opt_pass pass_scev_init;
extern struct tree_opt_pass pass_scev_anal;
extern struct tree_opt_pass pass_scev_depend;
extern struct tree_opt_pass pass_scev_vectorize;
extern struct tree_opt_pass pass_scev_done;
extern struct tree_opt_pass pass_ddg;
extern struct tree_opt_pass pass_delete_ddg;
extern struct tree_opt_pass pass_ch;
extern struct tree_opt_pass pass_ccp;
extern struct tree_opt_pass pass_build_ssa;
extern struct tree_opt_pass pass_del_ssa;
extern struct tree_opt_pass pass_dominator;
extern struct tree_opt_pass pass_dce;
extern struct tree_opt_pass pass_may_alias;
extern struct tree_opt_pass pass_split_crit_edges;
extern struct tree_opt_pass pass_pre;
extern struct tree_opt_pass pass_profile;
extern struct tree_opt_pass pass_lower_complex;
extern struct tree_opt_pass pass_fold_builtins;


#endif /* GCC_TREE_PASS_H */
