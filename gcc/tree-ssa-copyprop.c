/* SSA-Copy propagation for the GNU compiler.
   Copyright (C) 2003 Free Software Foundation, Inc.
   Contributed by Aldy Hernandez <aldy@quesejoda.com>
   and Diego Novillo <dnovillo@redhat.com>
   
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
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "timevar.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-simple.h"
#include "tree-dump.h"

/* Local variables.  */
static FILE *dump_file;
static int dump_flags;

/* Local functions.  */
static void copyprop_stmt (tree);
static void copyprop_phi (tree);
static inline tree get_original (tree);
static void move_var_to_scope (tree, tree, tree);


/* Main entry point to the copy propagator.  The algorithm is a simple
   linear scan of the flowgraph.  For every variable X_i used in the
   function, it retrieves its unique reaching definition.  If X_i's
   definition is a copy (i.e., X_i = Y_j), then X_i is replaced with Y_j.

   PHASE indicates which dump file from the DUMP_FILES array to use when
   dumping debugging information.  */

void
tree_ssa_copyprop (tree fndecl, enum tree_dump_index phase)
{
  basic_block bb;

  timevar_push (TV_TREE_COPYPROP);
  dump_file = dump_begin (phase, &dump_flags);

  /* Traverse every block in the flowgraph propagating copies in each
     statement.  */
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator si;
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	copyprop_phi (phi);

      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
	copyprop_stmt (bsi_stmt (si));
    }

  if (dump_file)
    {
      dump_function_to_file (fndecl, dump_file, dump_flags);
      dump_end (phase, dump_file);
    }

  timevar_pop (TV_TREE_COPYPROP);
}


/* Propagate copies in statement STMT.  If operand X_i in STMT is defined
   by a statement of the form X_i = Y_j, replace the use of X_i with Y_j.  */

static void
copyprop_stmt (tree stmt)
{
  varray_type uses;
  size_t i;
  bool modified;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nPropagating in statement: ");
      print_generic_expr (dump_file, stmt, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  get_stmt_operands (stmt);
  modified = false;

  /* Propagate real uses.  */
  uses = use_ops (stmt);
  for (i = 0; uses && i < VARRAY_ACTIVE_SIZE (uses); i++)
    {
      tree *use_p = VARRAY_TREE_PTR (uses, i);
      tree orig = get_original (*use_p);

      if (orig && may_propagate_copy (*use_p, orig))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\tReplacing ");
	      print_generic_expr (dump_file, *use_p, 0);
	      fprintf (dump_file, " with ");
	      print_generic_expr (dump_file, orig, 0);
	      fprintf (dump_file, "\n");
	    }

	  propagate_copy (use_p, orig, stmt_ann (stmt)->scope);
	  modified = true;
	}
    }

  if (modified)
    modify_stmt (stmt);
}


/* Propagate copies inside PHI node PHI.  If argument X_i of PHI comes from
   a definition of the form X_i = Y_j, replace it with Y_j.  */

static void
copyprop_phi (tree phi)
{
  int i;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nPropagating in PHI node: ");
      print_generic_expr (dump_file, phi, 0);
      fprintf (dump_file, "\n");
    }

  for (i = 0; i < PHI_NUM_ARGS (phi); i++)
    {
      tree arg = PHI_ARG_DEF (phi, i);
      tree orig;

      if (TREE_CODE (arg) != SSA_NAME)
	continue;

      orig = get_original (arg);
      if (orig && may_propagate_copy (arg, orig))
	{
	  if (dump_file && dump_flags & TDF_DETAILS)
	    {
	      fprintf (dump_file, "\tReplacing ");
	      print_generic_expr (dump_file, arg, 0);
	      fprintf (dump_file, " with ");
	      print_generic_expr (dump_file, orig, 0);
	      fprintf (dump_file, "\n");
	    }

	  PHI_ARG_DEF (phi, i) = orig;
	}
    }
}


/* If the unique definition for VAR comes from an assignment of the form
   VAR = ORIG, return ORIG.  Otherwise, return NULL.  */

static inline tree
get_original (tree var)
{
  tree def_stmt;

  def_stmt = SSA_NAME_DEF_STMT (var);

  /* If VAR is not the LHS of its defining statement, it means that VAR is
     defined by a VDEF node.  This implies aliasing or structure updates.
     For instance,

     		# a_2 = VDEF <a_1>
     		a.b = tmp_3;
		return a_2;

     If we allow tmp_3 to propagate into the 'return' statement, we would
     be changing the return type of the function.  */
  if (TREE_CODE (def_stmt) == MODIFY_EXPR
      && TREE_OPERAND (def_stmt, 0) == var
      && TREE_CODE (TREE_OPERAND (def_stmt, 1)) == SSA_NAME)
    return TREE_OPERAND (def_stmt, 1);

  return NULL_TREE;
}


/* Replace the operand pointed to by OP_P with variable VAR.  If *OP_P is a
   pointer, copy the memory tag used originally by *OP_P into VAR.  This is
   needed in cases where VAR had never been dereferenced in the program.
   
   SCOPE is the bind block in that the *OP_P occurs.  */
   
void
propagate_copy (tree *op_p, tree var, tree scope)
{
#if defined ENABLE_CHECKING
  if (!may_propagate_copy (*op_p, var))
    abort ();
#endif

  /* If VAR doesn't have a memory tag, copy the one from the original
     operand.  */
  if (POINTER_TYPE_P (TREE_TYPE (*op_p)))
    {
      var_ann_t new_ann = var_ann (SSA_NAME_VAR (var));
      var_ann_t orig_ann = var_ann (SSA_NAME_VAR (*op_p));

      if (new_ann->mem_tag == NULL_TREE)
	new_ann->mem_tag = orig_ann->mem_tag;
      else if (orig_ann->mem_tag == NULL_TREE)
	orig_ann->mem_tag = new_ann->mem_tag;
      else if (new_ann->mem_tag != orig_ann->mem_tag)
	abort ();
    }

  *op_p = var;
 
  fixup_var_scope (var, scope);
}

/* Fixes scope of variable VAR if it does not currently belong to SCOPE.  */
void
fixup_var_scope (tree var, tree scope)
{
  tree old_scope = var_ann (SSA_NAME_VAR (var))->scope;

  /* If there is no old_scope, it is a newly created temporary, i.e. it is
     in the topmost bind_expr and we have nothing to do.  */
  if (old_scope)
    {
      if (!scope)
	scope = DECL_SAVED_TREE (current_function_decl);
      else
	{
	  while (stmt_ann (scope)->scope_level
		 > stmt_ann (old_scope)->scope_level)
	    scope = stmt_ann (scope)->scope;
	}
      if (scope != old_scope)
	move_var_to_scope (SSA_NAME_VAR (var), old_scope,
			   DECL_SAVED_TREE (current_function_decl));
    }
}

/* Moves variable VAR from OLD_SCOPE to SCOPE.  */
static void
move_var_to_scope (tree var, tree old_scope, tree scope)
{
  tree avar, prev;
  tree block = BIND_EXPR_BLOCK (old_scope);

  prev = NULL_TREE;
  for (avar = BIND_EXPR_VARS (old_scope);
       avar;
       prev = avar, avar = TREE_CHAIN (avar))
    if (avar == var)
      break;
  if (!avar)
    abort ();

  if (block)
    remove_decl (avar, block);
  else
    remove_decl (avar, DECL_INITIAL (current_function_decl));

  if (prev)
    TREE_CHAIN (prev) = TREE_CHAIN (avar);
  else
    BIND_EXPR_VARS (old_scope) = TREE_CHAIN (avar);

  TREE_CHAIN (var) = BIND_EXPR_VARS (scope);
  BIND_EXPR_VARS (scope) = var;
  var_ann (var)->scope = scope;

  /* Dwarf2out ices (in add_abstract_origin_attribute) when it encounters
     variable that is not declared, but has DECL_ABSTRACT_ORIGIN set.  */
  DECL_ABSTRACT_ORIGIN (var) = NULL_TREE;
}
