/* Control and data flow functions for trees.
   Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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

#include "config.h"
#include "system.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "expr.h"
#include "diagnostic.h"

/* This should be eventually be generalized to other languages, but
   this would require a shared function-as-trees infrastructure.  */
#include "c-common.h"
#include "c-tree.h"

#include "basic-block.h"
#include "flags.h"
#include "tree-optimize.h"
#include "tree-flow.h"
#include "tree-alias-common.h"
#include "tree-dchain.h"

/* Main entry point to the tree SSA transformation routines.  FNDECL is
   the FUNCTION_DECL node for the function to optimize.  */

void
optimize_function_tree (fndecl)
     tree fndecl;
{
  tree fnbody;
  FILE *dump_file;
  int dump_flags;


  /* Don't bother doing anything if the program has errors.  */
  if (errorcount || sorrycount)
    return;
  
  fnbody = DECL_SAVED_TREE (fndecl);
  if (fnbody == NULL)
    abort ();

  /* Build the doubly-linked lists so that we can delete nodes
     efficiently.  */
  double_chain_stmts (fnbody);

#if 0
  /* Transform BREAK_STMTs, CONTINUE_STMTs, SWITCH_STMTs and GOTO_STMTs.  */
  break_continue_elimination (fndecl);
  goto_elimination (fndecl);
#endif

  /* Build the SSA representation for the function.  */
  build_tree_ssa (fndecl); 

  /* Begin optimization passes.  */
  if (n_basic_blocks > 0 && ! (errorcount || sorrycount))
    {
      if (flag_tree_pre)
	tree_perform_ssapre ();

      if (flag_tree_ccp)
	tree_ssa_ccp (fndecl);

      if (flag_tree_dce)
	tree_ssa_eliminate_dead_code (fndecl);
    }

  /* Wipe out the back-pointes in the statement chain.  */
  double_chain_free (fnbody);

  /* Flush out flow graph and SSA data.  */
  delete_cfg ();
  delete_tree_ssa ();

  /* Debugging dump after optimization.  */
  dump_file = dump_begin (TDI_optimized, &dump_flags);
  if (dump_file)
    {
      tree fnbody;

      /* We never get here if the function body is empty,
	 see simplify_function_tree().  */ 
      fnbody = COMPOUND_BODY (DECL_SAVED_TREE (fndecl)); 
      fprintf (dump_file, "%s()\n", IDENTIFIER_POINTER (DECL_NAME (fndecl)));

      if (dump_flags & TDF_RAW)
	dump_node (fnbody, TDF_SLIM | dump_flags, dump_file);
      else
	print_c_tree (dump_file, fnbody);
      fprintf (dump_file, "\n");

      dump_end (TDI_optimized, dump_file);
    }
}


/* Main entry point to the tree SSA analysis routines.  */

void
build_tree_ssa (fndecl)
     tree fndecl;
{
  /* Initialize flow data.  */
  init_flow ();

  tree_find_basic_blocks (DECL_SAVED_TREE (fndecl));

  if (n_basic_blocks > 0 && ! (errorcount || sorrycount))
    tree_build_ssa ();

  if (flag_tree_points_to)
    create_alias_vars ();
}
