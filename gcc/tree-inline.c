/* Control and data flow functions for trees.
   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.
   Contributed by Alexandre Oliva <aoliva@redhat.com>

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "toplev.h"
#include "tree.h"
#include "tree-inline.h"
#include "rtl.h"
#include "expr.h"
#include "flags.h"
#include "params.h"
#include "input.h"
#include "insn-config.h"
#include "integrate.h"
#include "varray.h"
#include "hashtab.h"
#include "splay-tree.h"
#include "langhooks.h"
#include "cgraph.h"

/* I'm not real happy about this, but we need to handle gimple and
   non-gimple trees.  */
#include "tree-iterator.h"
#include "tree-simple.h"

/* 0 if we should not perform inlining.
   1 if we should expand functions calls inline at the tree level.
   2 if we should consider *all* functions to be inline
   candidates.  */

int flag_inline_trees = 0;

/* To Do:

   o In order to make inlining-on-trees work, we pessimized
     function-local static constants.  In particular, they are now
     always output, even when not addressed.  Fix this by treating
     function-local static constants just like global static
     constants; the back-end already knows not to output them if they
     are not needed.

   o Provide heuristics to clamp inlining of recursive template
     calls?  */

/* Data required for function inlining.  */

typedef struct inline_data
{
  /* A stack of the functions we are inlining.  For example, if we are
     compiling `f', which calls `g', which calls `h', and we are
     inlining the body of `h', the stack will contain, `h', followed
     by `g', followed by `f'.  The first few elements of the stack may
     contain other functions that we know we should not recurse into,
     even though they are not directly being inlined.  */
  varray_type fns;
  /* The index of the first element of FNS that really represents an
     inlined function.  */
  unsigned first_inlined_fn;
  /* The label to jump to when a return statement is encountered.  If
     this value is NULL, then return statements will simply be
     remapped as return statements, rather than as jumps.  */
  tree ret_label;
  /* The VAR_DECL for the return value.  */
  tree retvar;
  /* The map from local declarations in the inlined function to
     equivalents in the function into which it is being inlined.  */
  splay_tree decl_map;
  /* Nonzero if we are currently within the cleanup for a
     TARGET_EXPR.  */
  int in_target_cleanup_p;
  /* A list of the functions current function has inlined.  */
  varray_type inlined_fns;
  /* The approximate number of instructions we have inlined in the
     current call stack.  */
  int inlined_insns;
  /* We use the same mechanism to build clones that we do to perform
     inlining.  However, there are a few places where we need to
     distinguish between those two situations.  This flag is true if
     we are cloning, rather than inlining.  */
  bool cloning_p;
  /* Hash table used to prevent walk_tree from visiting the same node
     umpteen million times.  */
  htab_t tree_pruner;
  /* Decl of function we are inlining into.  */
  tree decl;
  tree current_decl;
  /* Statement iterator.  We need this so we can keep the tree in
     gimple form when we insert the inlined function.   It is not
     used when we are not dealing with gimple trees.  */
  tree_stmt_iterator tsi;
} inline_data;

/* Prototypes.  */

/* The approximate number of instructions per statement.  This number
   need not be particularly accurate; it is used only to make
   decisions about when a function is too big to inline.  */
#define INSNS_PER_STMT (10)

static tree declare_return_variable (inline_data *, tree, tree *);
static tree copy_body_r (tree *, int *, void *);
static tree copy_body (inline_data *);
static tree expand_call_inline (tree *, int *, void *);
static void expand_calls_inline (tree *, inline_data *);
static bool inlinable_function_p (tree);
static int limits_allow_inlining (tree, inline_data *);
static tree remap_decl (tree, inline_data *);
static tree initialize_inlined_parameters (inline_data *, tree, tree, tree);
static void remap_block (tree *, inline_data *);
static tree add_stmt_to_compound (tree, tree, tree);
static tree find_alloca_call_1 (tree *, int *, void *);
static tree find_alloca_call (tree);
static tree find_builtin_longjmp_call_1 (tree *, int *, void *);
static tree find_builtin_longjmp_call (tree);

static tree remap_decls (tree, inline_data *);
static tree add_stmt_to_compound (tree, tree, tree);
static void copy_bind_expr (tree *, int *, inline_data *);
static tree mark_local_for_remap_r (tree *, int *, void *);
static tree unsave_r (tree *, int *, void *);


/* Remap DECL during the copying of the BLOCK tree for the function.  */

static tree
remap_decl (tree decl, inline_data *id)
{
  splay_tree_node n;
  tree fn;

  /* We only remap local variables in the current function.  */
  fn = VARRAY_TOP_TREE (id->fns);
#if 0
  /* We need to remap statics, too, so that they get expanded even if the
     inline function is never emitted out of line.  We might as well also
     remap extern decls so that they show up in the debug info.  */
  if (! (*lang_hooks.tree_inlining.auto_var_in_fn_p) (decl, fn))
    return NULL_TREE;
#endif

  /* See if we have remapped this declaration.  */
  n = splay_tree_lookup (id->decl_map, (splay_tree_key) decl);
  /* If we didn't already have an equivalent for this declaration,
     create one now.  */
  if (!n)
    {
      tree t;

      /* Make a copy of the variable or label.  */
      t = copy_decl_for_inlining (decl, fn,
				  VARRAY_TREE (id->fns, 0));

      /* The decl T could be a dynamic array or other variable size type,
	 in which case some fields need to be remapped because they may
	 contain SAVE_EXPRs.  */
      if (TREE_TYPE (t) && TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE
	  && TYPE_DOMAIN (TREE_TYPE (t)))
	{
	  TREE_TYPE (t) = copy_node (TREE_TYPE (t));
	  TYPE_DOMAIN (TREE_TYPE (t))
	    = copy_node (TYPE_DOMAIN (TREE_TYPE (t)));
	  walk_tree (&TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (t))),
		     copy_body_r, id, NULL);
	}

#if 0
      /* FIXME handle anon aggrs.  */
      if (! DECL_NAME (t) && TREE_TYPE (t)
	  && (*lang_hooks.tree_inlining.anon_aggr_type_p) (TREE_TYPE (t)))
	{
	  /* For a VAR_DECL of anonymous type, we must also copy the
	     member VAR_DECLS here and rechain the
	     DECL_ANON_UNION_ELEMS.  */
	  tree members = NULL;
	  tree src;

	  for (src = DECL_ANON_UNION_ELEMS (t); src;
	       src = TREE_CHAIN (src))
	    {
	      tree member = remap_decl (TREE_VALUE (src), id);

	      if (TREE_PURPOSE (src))
		abort ();
	      members = tree_cons (NULL, member, members);
	    }
	  DECL_ANON_UNION_ELEMS (t) = nreverse (members);
	}
#endif

      /* Remember it, so that if we encounter this local entity
	 again we can reuse this copy.  */
      n = splay_tree_insert (id->decl_map,
			     (splay_tree_key) decl,
			     (splay_tree_value) t);
    }

  return (tree) n->value;
}

static tree
remap_decls (tree decls, inline_data *id)
{
  tree old_var;
  tree new_decls = NULL_TREE;

  /* Remap its variables.  */
  for (old_var = decls; old_var; old_var = TREE_CHAIN (old_var))
    {
      tree new_var;

      /* Remap the variable.  */
      new_var = remap_decl (old_var, id);

      /* If we didn't remap this variable, so we can't mess with its
	 TREE_CHAIN.  If we remapped this variable to the return slot, it's
	 already declared somewhere else, so don't declare it here.  */
      if (!new_var || new_var == id->retvar)
	;
#ifdef ENABLE_CHECKING
      else if (!DECL_P (new_var))
	abort ();
#endif
      else
	{
	  TREE_CHAIN (new_var) = new_decls;
	  new_decls = new_var;
	}
    }

  return nreverse (new_decls);
}

/* Copy the BLOCK to contain remapped versions of the variables
   therein.  And hook the new block into the block-tree.  */

static void
remap_block (tree *block, inline_data *id)
{
  tree old_block;
  tree new_block;
  tree fn;

  /* Make the new block.  */
  old_block = *block;
  new_block = make_node (BLOCK);
  TREE_USED (new_block) = TREE_USED (old_block);
  BLOCK_ABSTRACT_ORIGIN (new_block) = old_block;
  *block = new_block;

  /* Remap its variables.  */
  BLOCK_VARS (new_block) = remap_decls (BLOCK_VARS (old_block), id);

  fn = VARRAY_TREE (id->fns, 0);
#if 1
  /* FIXME!  It shouldn't be so hard to manage blocks.  Rebuilding them in
     rest_of_compilation is a good start.  */
  if (id->cloning_p)
    /* We're building a clone; DECL_INITIAL is still
       error_mark_node, and current_binding_level is the parm
       binding level.  */
    (*lang_hooks.decls.insert_block) (new_block);
  else
    {
      /* Attach this new block after the DECL_INITIAL block for the
	 function into which this block is being inlined.  In
	 rest_of_compilation we will straighten out the BLOCK tree.  */
      tree *first_block;
      if (DECL_INITIAL (fn))
	first_block = &BLOCK_CHAIN (DECL_INITIAL (fn));
      else
	first_block = &DECL_INITIAL (fn);
      BLOCK_CHAIN (new_block) = *first_block;
      *first_block = new_block;
    }
#endif
  /* Remember the remapped block.  */
  splay_tree_insert (id->decl_map,
		     (splay_tree_key) old_block,
		     (splay_tree_value) new_block);
}

static void
copy_bind_expr (tree *tp, int *walk_subtrees, inline_data *id)
{
  tree block = BIND_EXPR_BLOCK (*tp);
  /* Copy (and replace) the statement.  */
  copy_tree_r (tp, walk_subtrees, NULL);
  if (block)
    {
      remap_block (&block, id);
      BIND_EXPR_BLOCK (*tp) = block;
    }

  if (BIND_EXPR_VARS (*tp))
    /* This will remap a lot of the same decls again, but this should be
       harmless.  */
    BIND_EXPR_VARS (*tp) = remap_decls (BIND_EXPR_VARS (*tp), id);
}

/* Called from copy_body via walk_tree.  DATA is really an
   `inline_data *'.  */
static tree
copy_body_r (tree *tp, int *walk_subtrees, void *data)
{
  inline_data* id;
  tree fn;

  /* Set up.  */
  id = (inline_data *) data;
  fn = VARRAY_TOP_TREE (id->fns);

#if 0
  /* All automatic variables should have a DECL_CONTEXT indicating
     what function they come from.  */
  if ((TREE_CODE (*tp) == VAR_DECL || TREE_CODE (*tp) == LABEL_DECL)
      && DECL_NAMESPACE_SCOPE_P (*tp))
    if (! DECL_EXTERNAL (*tp) && ! TREE_STATIC (*tp))
      abort ();
#endif

  /* If this is a RETURN_STMT, change it into an EXPR_STMT and a
     GOTO_STMT with the RET_LABEL as its target.  */
  if (TREE_CODE (*tp) == RETURN_EXPR && id->ret_label)
    {
      tree return_stmt = *tp;
      tree goto_stmt;

      /* Build the GOTO_EXPR.  */
      tree assignment = TREE_OPERAND (return_stmt, 0);
      goto_stmt = build1 (GOTO_EXPR, void_type_node, id->ret_label);
      TREE_USED (id->ret_label) = 1;

      /* If we're returning something, just turn that into an
	 assignment into the equivalent of the original
	 RESULT_DECL.  */
      if (assignment)
        {
	  /* Do not create a statement containing a naked RESULT_DECL.  */
	  if (keep_function_tree_in_gimple_form (id->decl))
	    if (TREE_CODE (assignment) == RESULT_DECL)
	      gimplify_stmt (&assignment);

	  *tp = build (BIND_EXPR, void_type_node, NULL_TREE,
		       build (COMPOUND_EXPR, void_type_node,
			      assignment, goto_stmt),
		       make_node (BLOCK));
        }
      /* If we're not returning anything just do the jump.  */
      else
	*tp = goto_stmt;
    }
  /* Local variables and labels need to be replaced by equivalent
     variables.  We don't want to copy static variables; there's only
     one of those, no matter how many times we inline the containing
     function.  */
  else if ((*lang_hooks.tree_inlining.auto_var_in_fn_p) (*tp, fn))
    {
      tree new_decl;

      /* Remap the declaration.  */
      new_decl = remap_decl (*tp, id);
      if (! new_decl)
	abort ();
      /* Replace this variable with the copy.  */
      STRIP_TYPE_NOPS (new_decl);
      *tp = new_decl;
    }
#if 0
  else if (nonstatic_local_decl_p (*tp)
	   && DECL_CONTEXT (*tp) != VARRAY_TREE (id->fns, 0))
    abort ();
#endif
  else if (TREE_CODE (*tp) == SAVE_EXPR)
    remap_save_expr (tp, id->decl_map, VARRAY_TREE (id->fns, 0),
		     walk_subtrees);
  else if (TREE_CODE (*tp) == UNSAVE_EXPR)
    /* UNSAVE_EXPRs should not be generated until expansion time.  */
    abort ();
  else if (TREE_CODE (*tp) == BIND_EXPR)
    copy_bind_expr (tp, walk_subtrees, id);
  else if (TREE_CODE (*tp) == LABELED_BLOCK_EXPR)
    {
      /* We need a new copy of this labeled block; the EXIT_BLOCK_EXPR
         will refer to it, so save a copy ready for remapping.  We
         save it in the decl_map, although it isn't a decl.  */
      tree new_block = copy_node (*tp);
      splay_tree_insert (id->decl_map,
			 (splay_tree_key) *tp,
			 (splay_tree_value) new_block);
      *tp = new_block;
    }
  else if (TREE_CODE (*tp) == EXIT_BLOCK_EXPR)
    {
      splay_tree_node n
	= splay_tree_lookup (id->decl_map,
			     (splay_tree_key) TREE_OPERAND (*tp, 0));
      /* We _must_ have seen the enclosing LABELED_BLOCK_EXPR.  */
      if (! n)
	abort ();
      *tp = copy_node (*tp);
      TREE_OPERAND (*tp, 0) = (tree) n->value;
    }
  /* Otherwise, just copy the node.  Note that copy_tree_r already
     knows not to copy VAR_DECLs, etc., so this is safe.  */
  else
    {
      if (TREE_CODE (*tp) == MODIFY_EXPR
	  && TREE_OPERAND (*tp, 0) == TREE_OPERAND (*tp, 1)
	  && ((*lang_hooks.tree_inlining.auto_var_in_fn_p)
	      (TREE_OPERAND (*tp, 0), fn)))
	{
	  /* Some assignments VAR = VAR; don't generate any rtl code
	     and thus don't count as variable modification.  Avoid
	     keeping bogosities like 0 = 0.  */
	  tree decl = TREE_OPERAND (*tp, 0), value;
	  splay_tree_node n;

	  n = splay_tree_lookup (id->decl_map, (splay_tree_key) decl);
	  if (n)
	    {
	      value = (tree) n->value;
	      STRIP_TYPE_NOPS (value);
	      if (TREE_CONSTANT (value) || TREE_READONLY_DECL_P (value))
		{
		  *tp = value;
		  return copy_body_r (tp, walk_subtrees, data);
		}
	    }
	}
      else if (TREE_CODE (*tp) == ADDR_EXPR
	       && ((*lang_hooks.tree_inlining.auto_var_in_fn_p)
		   (TREE_OPERAND (*tp, 0), fn)))
	{
	  /* Get rid of &* from inline substitutions.  It can occur when
	     someone takes the address of a parm or return slot passed by
	     invisible reference.  */
	  tree decl = TREE_OPERAND (*tp, 0), value;
	  splay_tree_node n;

	  n = splay_tree_lookup (id->decl_map, (splay_tree_key) decl);
	  if (n)
	    {
	      value = (tree) n->value;
	      if (TREE_CODE (value) == INDIRECT_REF)
		{
		  /* Assume that the argument types properly match the
		     parameter types.  We can't compare them well enough
		     without a comptypes langhook, and we don't want to
		     call convert and introduce a NOP_EXPR to convert
		     between two equivalent types (i.e. that only differ
		     in use of typedef names).  */
		  *tp = TREE_OPERAND (value, 0);
		  return copy_body_r (tp, walk_subtrees, data);
		}
	    }
	}
      else if (TREE_CODE (*tp) == INDIRECT_REF)
	{
	  /* Get rid of *& from inline substitutions that can happen when a
	     pointer argument is an ADDR_EXPR.  */
	  tree decl = TREE_OPERAND (*tp, 0), value;
	  splay_tree_node n;

	  n = splay_tree_lookup (id->decl_map, (splay_tree_key) decl);
	  if (n)
	    {
	      value = (tree) n->value;
	      STRIP_NOPS (value);
	      if (TREE_CODE (value) == ADDR_EXPR)
		{
		  *tp = TREE_OPERAND (value, 0);
		  return copy_body_r (tp, walk_subtrees, data);
		}
	    }
	}

      copy_tree_r (tp, walk_subtrees, NULL);

      /* The copied TARGET_EXPR has never been expanded, even if the
	 original node was expanded already.  */
      if (TREE_CODE (*tp) == TARGET_EXPR && TREE_OPERAND (*tp, 3))
	{
	  TREE_OPERAND (*tp, 1) = TREE_OPERAND (*tp, 3);
	  TREE_OPERAND (*tp, 3) = NULL_TREE;
	}
    }

  /* Keep iterating.  */
  return NULL_TREE;
}

/* Make a copy of the body of FN so that it can be inserted inline in
   another function.  */

static tree
copy_body (inline_data *id)
{
  tree body;

  body = DECL_SAVED_TREE (VARRAY_TOP_TREE (id->fns));
  walk_tree (&body, copy_body_r, id, NULL);

  return body;
}

/* Generate code to initialize the parameters of the function at the
   top of the stack in ID from the ARGS (presented as a TREE_LIST).  */

static tree
initialize_inlined_parameters (inline_data *id, tree args, tree fn, tree bind_expr)
{
  tree init_stmts;
  tree parms;
  tree a;
  tree p;
  tree vars = NULL_TREE;
  bool gimplify_init_stmts_p = false;

  /* Figure out what the parameters are.  */
  parms = DECL_ARGUMENTS (fn);

  /* Start with no initializations whatsoever.  */
  init_stmts = NULL_TREE;

  /* Loop through the parameter declarations, replacing each with an
     equivalent VAR_DECL, appropriately initialized.  */
  for (p = parms, a = args; p;
       a = a ? TREE_CHAIN (a) : a, p = TREE_CHAIN (p))
    {
      tree init_stmt;
      tree var;
      tree value;
      tree var_sub;

      /* Find the initializer.  */
      value = (*lang_hooks.tree_inlining.convert_parm_for_inlining)
	      (p, a ? TREE_VALUE (a) : NULL_TREE, fn);

      /* If the parameter is never assigned to, we may not need to
	 create a new variable here at all.  Instead, we may be able
	 to just use the argument value.  */
      if (TREE_READONLY (p)
	  && !TREE_ADDRESSABLE (p)
	  && value && !TREE_SIDE_EFFECTS (value))
	{
#if 0
	  /* Simplify the value, if possible.  */
	  value = fold (DECL_P (value) ? decl_constant_value (value) : value);
#endif

	  /* We can't risk substituting complex expressions.  They
	     might contain variables that will be assigned to later.
	     Theoretically, we could check the expression to see if
	     all of the variables that determine its value are
	     read-only, but we don't bother.  */
	  if (TREE_CONSTANT (value) || TREE_READONLY_DECL_P (value))
	    {
	      /* If this is a declaration, wrap it a NOP_EXPR so that
		 we don't try to put the VALUE on the list of
		 BLOCK_VARS.  */
	      if (DECL_P (value))
		value = build1 (NOP_EXPR, TREE_TYPE (value), value);

	      /* If this is a constant, make sure it has the right type.  */
	      else if (TREE_TYPE (value) != TREE_TYPE (p))
		value = fold (build1 (NOP_EXPR, TREE_TYPE (p), value));

	      splay_tree_insert (id->decl_map,
				 (splay_tree_key) p,
				 (splay_tree_value) value);
	      continue;
	    }
	}

      /* Make an equivalent VAR_DECL.  */
      var = copy_decl_for_inlining (p, fn, VARRAY_TREE (id->fns, 0));

      /* See if the frontend wants to pass this by invisible reference.  If
	 so, our new VAR_DECL will have REFERENCE_TYPE, and we need to
	 replace uses of the PARM_DECL with dereferences.  */
      if (TREE_TYPE (var) != TREE_TYPE (p)
	  && POINTER_TYPE_P (TREE_TYPE (var))
	  && TREE_TYPE (TREE_TYPE (var)) == TREE_TYPE (p))
	var_sub = build1 (INDIRECT_REF, TREE_TYPE (p), var);
      else
	var_sub = var;

      /* Register the VAR_DECL as the equivalent for the PARM_DECL;
	 that way, when the PARM_DECL is encountered, it will be
	 automatically replaced by the VAR_DECL.  */
      splay_tree_insert (id->decl_map,
			 (splay_tree_key) p,
			 (splay_tree_value) var_sub);

      /* Declare this new variable.  */
      TREE_CHAIN (var) = vars;
      vars = var;

      /* Even if P was TREE_READONLY, the new VAR should not be.
	 In the original code, we would have constructed a
	 temporary, and then the function body would have never
	 changed the value of P.  However, now, we will be
	 constructing VAR directly.  The constructor body may
	 change its value multiple times as it is being
	 constructed.  Therefore, it must not be TREE_READONLY;
	 the back-end assumes that TREE_READONLY variable is
	 assigned to only once.  */
      if (TYPE_NEEDS_CONSTRUCTING (TREE_TYPE (p)))
	TREE_READONLY (var) = 0;

      /* Initialize this VAR_DECL from the equivalent argument.  Convert
	 the argument to the proper type in case it was promoted.  */
      if (value)
	{
	  tree rhs = convert (TREE_TYPE (var), value);

	  if (rhs == error_mark_node)
	    continue;

	  /* We want to use MODIFY_EXPR, not INIT_EXPR here so that we
	     keep our trees in gimple form.  */
	  init_stmt = build (MODIFY_EXPR, TREE_TYPE (var), var, rhs);
	  init_stmts = add_stmt_to_compound (init_stmts, void_type_node,
					     init_stmt);

	  /* If the conversion needed to assign VALUE to VAR is not a
	     GIMPLE expression, flag that we will need to gimplify
	     INIT_STMTS at the end.  */
	  if (!is_gimple_rhs (rhs))
	    gimplify_init_stmts_p = true;
	}

#if 0
      tree cleanup;
      /* See if we need to clean up the declaration.  */
      cleanup = (*lang_hooks.maybe_build_cleanup) (var);
      if (cleanup)
	{
	  tree cleanup_stmt;
	  /* Build the cleanup statement.  */
	  cleanup_stmt = build_stmt (CLEANUP_STMT, var, cleanup);
	  /* Add it to the *front* of the list; the list will be
	     reversed below.  */
	  TREE_CHAIN (cleanup_stmt) = init_stmts;
	  init_stmts = cleanup_stmt;
	}
#endif
    }

  /* Evaluate trailing arguments.  */
  for (; a; a = TREE_CHAIN (a))
    {
      tree value = TREE_VALUE (a);

      if (! value || ! TREE_SIDE_EFFECTS (value))
	continue;

      init_stmts = add_stmt_to_compound (init_stmts, void_type_node,
					 value);
    }

  if (gimplify_init_stmts_p
      && keep_function_tree_in_gimple_form (fn))
    gimplify_body (&init_stmts, fn);

  add_var_to_bind_expr (bind_expr, vars);
  return init_stmts;
}

/* Declare a return variable to replace the RESULT_DECL for the
   function we are calling.  An appropriate DECL_STMT is returned.
   The USE_STMT is filled in to contain a use of the declaration to
   indicate the return value of the function.  */

static tree
declare_return_variable (inline_data *id, tree return_slot_addr, tree *use_p)
{
  tree fn = VARRAY_TOP_TREE (id->fns);
  tree result = DECL_RESULT (fn);
  int need_return_decl = 1;
  tree var;

  /* We don't need to do anything for functions that don't return
     anything.  */
  if (!result || VOID_TYPE_P (TREE_TYPE (result)))
    {
      *use_p = NULL_TREE;
      return NULL_TREE;
    }

  var = ((*lang_hooks.tree_inlining.copy_res_decl_for_inlining)
	 (result, fn, VARRAY_TREE (id->fns, 0), id->decl_map,
	  &need_return_decl, return_slot_addr));

  /* Register the VAR_DECL as the equivalent for the RESULT_DECL; that
     way, when the RESULT_DECL is encountered, it will be
     automatically replaced by the VAR_DECL.  */
  splay_tree_insert (id->decl_map,
		     (splay_tree_key) result,
		     (splay_tree_value) var);

  /* Remember this so we can ignore it in remap_decls.  */
  id->retvar = var;

  /* Build the use expr.  If the return type of the function was
     promoted, convert it back to the expected type.  */
  if (return_slot_addr)
    /* The function returns through an explicit return slot, not a normal
       return value.  */
    *use_p = NULL_TREE;
  else if (TREE_TYPE (var) == TREE_TYPE (TREE_TYPE (fn)))
    *use_p = var;
  else if (TREE_CODE (var) == INDIRECT_REF)
    *use_p = build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (fn)),
		     TREE_OPERAND (var, 0));
  else if (TREE_ADDRESSABLE (TREE_TYPE (var)))
    abort ();
  else
    *use_p = build1 (NOP_EXPR, TREE_TYPE (TREE_TYPE (fn)), var);

  /* Build the declaration statement if FN does not return an
     aggregate.  */
  if (need_return_decl)
    return var;
  /* If FN does return an aggregate, there's no need to declare the
     return variable; we're using a variable in our caller's frame.  */
  else
    return NULL_TREE;
}

/* Returns nonzero if a function can be inlined as a tree.  */

bool
tree_inlinable_function_p (tree fn)
{
  return inlinable_function_p (fn);
}

/* If *TP is possibly call to alloca, return nonzero.  */
static tree
find_alloca_call_1 (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
		    void *data ATTRIBUTE_UNUSED)
{
  if (alloca_call_p (*tp))
    return *tp;
  return NULL;
}

/* Return subexpression representing possible alloca call, if any.  */
static tree
find_alloca_call (tree exp)
{
  location_t saved_loc = input_location;
  tree ret = walk_tree_without_duplicates
		(&exp, find_alloca_call_1, NULL);
  input_location = saved_loc;
  return ret;
}

static tree
find_builtin_longjmp_call_1 (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
			     void *data ATTRIBUTE_UNUSED)
{
  tree exp = *tp, decl;

  if (TREE_CODE (exp) == CALL_EXPR
      && TREE_CODE (TREE_OPERAND (exp, 0)) == ADDR_EXPR
      && (decl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0),
	  TREE_CODE (decl) == FUNCTION_DECL)
      && DECL_BUILT_IN_CLASS (decl) == BUILT_IN_NORMAL
      && DECL_FUNCTION_CODE (decl) == BUILT_IN_LONGJMP)
    return decl;

  return NULL;
}

static tree
find_builtin_longjmp_call (tree exp)
{
  location_t saved_loc = input_location;
  tree ret = walk_tree_without_duplicates
		(&exp, find_builtin_longjmp_call_1, NULL);
  input_location = saved_loc;
  return ret;
}

/* Returns nonzero if FN is a function that does not have any
   fundamental inline blocking properties.  */

static bool
inlinable_function_p (tree fn)
{
  bool inlinable = true;
  bool calls_builtin_longjmp = false;
  bool calls_alloca = false;

  /* If we've already decided this function shouldn't be inlined,
     there's no need to check again.  */
  if (DECL_UNINLINABLE (fn))
    return false;

  /* See if there is any language-specific reason it cannot be
     inlined.  (It is important that this hook be called early because
     in C++ it may result in template instantiation.)
     If the function is not inlinable for language-specific reasons,
     it is left up to the langhook to explain why.  */
  inlinable = !(*lang_hooks.tree_inlining.cannot_inline_tree_fn) (&fn);

  /* If we don't have the function body available, we can't inline it.
     However, this should not be recorded since we also get here for
     forward declared inline functions.  Therefore, return at once.  */
  if (!DECL_SAVED_TREE (fn))
    return false;

  /* If we're not inlining at all, then we cannot inline this function.  */
  else if (!flag_inline_trees)
    inlinable = false;

  /* Only try to inline functions if DECL_INLINE is set.  This should be
     true for all functions declared `inline', and for all other functions
     as well with -finline-functions.

     Don't think of disregarding DECL_INLINE when flag_inline_trees == 2;
     it's the front-end that must set DECL_INLINE in this case, because
     dwarf2out loses if a function that does not have DECL_INLINE set is
     inlined anyway.  That is why we have both DECL_INLINE and
     DECL_DECLARED_INLINE_P.  */
  /* FIXME: When flag_inline_trees dies, the check for flag_unit_at_a_time
	    here should be redundant.  */
  else if (!DECL_INLINE (fn) && !flag_unit_at_a_time)
    inlinable = false;

#ifdef INLINER_FOR_JAVA
  /* Synchronized methods can't be inlined.  This is a bug.  */
  else if (METHOD_SYNCHRONIZED (fn))
    inlinable = false;
#endif /* INLINER_FOR_JAVA */

  /* We can't inline functions that call __builtin_longjmp at all.
     The non-local goto machinery really requires the destination
     be in a different function.  If we allow the function calling
     __builtin_longjmp to be inlined into the function calling
     __builtin_setjmp, Things will Go Awry.  */
  /* ??? Need front end help to identify "regular" non-local goto.  */
  else if (find_builtin_longjmp_call (DECL_SAVED_TREE (fn)))
    calls_builtin_longjmp = true;

  /* Refuse to inline alloca call unless user explicitly forced so as this
     may change program's memory overhead drastically when the function
     using alloca is called in loop.  In GCC present in SPEC2000 inlining
     into schedule_block cause it to require 2GB of ram instead of 256MB.  */
  else if (lookup_attribute ("always_inline", DECL_ATTRIBUTES (fn)) == NULL
	   && find_alloca_call (DECL_SAVED_TREE (fn)))
    calls_alloca = true;

  if (calls_builtin_longjmp || calls_alloca)
    {
      /* See if we should warn about uninlinable functions.  Previously,
	 some of these warnings would be issued while trying to expand
	 the function inline, but that would cause multiple warnings
	 about functions that would for example call alloca.  But since
	 this a property of the function, just one warning is enough.
	 As a bonus we can now give more details about the reason why a
	 function is not inlinable.
	 We only warn for functions declared `inline' by the user.  */
      bool do_warning = (warn_inline
			 && DECL_INLINE (fn)
			 && DECL_DECLARED_INLINE_P (fn)
			 && !DECL_IN_SYSTEM_HEADER (fn));

      if (do_warning && calls_builtin_longjmp)
	warning ("%Hfunction '%F' can never be inlined because it uses "
		 "setjmp-longjmp exception handling",
		 TREE_LOCUS (fn), fn);
      if (do_warning && calls_alloca)
	warning ("%Hfunction '%F' can never be inlined because it uses "
		 "setjmp-longjmp exception handling",
		 TREE_LOCUS (fn), fn);

      inlinable = false;
    }

  /* Squirrel away the result so that we don't have to check again.  */
  DECL_UNINLINABLE (fn) = !inlinable;

  return inlinable;
}

/* We can't inline functions that are too big.  Only allow a single
   function to be of MAX_INLINE_INSNS_SINGLE size.  Make special
   allowance for extern inline functions, though.

   Return nonzero if the function FN can be inlined into the inlining
   context ID.  */

static int
limits_allow_inlining (tree fn, inline_data *id)
{
  int estimated_insns = 0;
  size_t i;

  /* Don't even bother if the function is not inlinable.  */
  if (!inlinable_function_p (fn))
    return 0;

  /* Investigate the size of the function.  Return at once
     if the function body size is too large.  */
  if (!(*lang_hooks.tree_inlining.disregard_inline_limits) (fn))
    {
      int currfn_max_inline_insns;

      /* If we haven't already done so, get an estimate of the number of
	 instructions that will be produces when expanding this function.  */
      if (!DECL_ESTIMATED_INSNS (fn))
	DECL_ESTIMATED_INSNS (fn)
	  = (*lang_hooks.tree_inlining.estimate_num_insns) (fn);
      estimated_insns = DECL_ESTIMATED_INSNS (fn);

      /* We may be here either because fn is declared inline or because
	 we use -finline-functions.  For the second case, we are more
	 restrictive.

	 FIXME: -finline-functions should imply -funit-at-a-time, it's
		about equally expensive but unit-at-a-time produces
		better code.  */
      currfn_max_inline_insns = DECL_DECLARED_INLINE_P (fn) ?
		MAX_INLINE_INSNS_SINGLE : MAX_INLINE_INSNS_AUTO;

      /* If the function is too big to be inlined, adieu.  */
      if (estimated_insns > currfn_max_inline_insns)
	return 0;

      /* We now know that we don't disregard the inlining limits and that 
	 we basically should be able to inline this function.
	 We always allow inlining functions if we estimate that they are
	 smaller than MIN_INLINE_INSNS.  Otherwise, investigate further.  */
      if (estimated_insns > MIN_INLINE_INSNS)
	{
	  int sum_insns = (id ? id->inlined_insns : 0) + estimated_insns;

	  /* In the extreme case that we have exceeded the recursive inlining
	     limit by a huge factor (128), we just say no.

	     FIXME:  Should not happen in real life, but people have reported
		     that it actually does!?  */
	  if (sum_insns > MAX_INLINE_INSNS * 128)
	    return 0;

	  /* If we did not hit the extreme limit, we use a linear function
	     with slope -1/MAX_INLINE_SLOPE to exceedingly decrease the
	     allowable size.  */
	  else if (sum_insns > MAX_INLINE_INSNS)
	    {
	      if (estimated_insns > currfn_max_inline_insns
			- (sum_insns - MAX_INLINE_INSNS) / MAX_INLINE_SLOPE)
	        return 0;
	    }
	}
    }

  /* Don't allow recursive inlining.  */
  for (i = 0; i < VARRAY_ACTIVE_SIZE (id->fns); ++i)
    if (VARRAY_TREE (id->fns, i) == fn)
      return 0;

  if (DECL_INLINED_FNS (fn))
    {
      int j;
      tree inlined_fns = DECL_INLINED_FNS (fn);

      for (j = 0; j < TREE_VEC_LENGTH (inlined_fns); ++j)
	if (TREE_VEC_ELT (inlined_fns, j) == VARRAY_TREE (id->fns, 0))
	  return 0;
    }

  /* Go ahead, this function can be inlined.  */
  return 1;
}

/* If *TP is a CALL_EXPR, replace it with its inline expansion.  */

static tree
expand_call_inline (tree *tp, int *walk_subtrees, void *data)
{
  inline_data *id;
  tree t;
  tree expr;
  tree stmt;
  tree use_retvar;
  tree decl;
  tree fn;
  tree arg_inits;
  tree *inlined_body;
  tree inline_result;
  splay_tree st;
  tree args;
  tree return_slot_addr;

  /* See what we've got.  */
  id = (inline_data *) data;
  t = *tp;

  /* Recurse, but letting recursive invocations know that we are
     inside the body of a TARGET_EXPR.  */
  if (TREE_CODE (*tp) == TARGET_EXPR)
    {
#if 0
      int i, len = first_rtl_op (TARGET_EXPR);

      /* We're walking our own subtrees.  */
      *walk_subtrees = 0;

      /* Actually walk over them.  This loop is the body of
	 walk_trees, omitting the case where the TARGET_EXPR
	 itself is handled.  */
      for (i = 0; i < len; ++i)
	{
	  if (i == 2)
	    ++id->in_target_cleanup_p;
	  walk_tree (&TREE_OPERAND (*tp, i), expand_call_inline, data,
		     id->tree_pruner);
	  if (i == 2)
	    --id->in_target_cleanup_p;
	}

      return NULL_TREE;
#endif
    }

  if (TYPE_P (t))
    /* Because types were not copied in copy_body, CALL_EXPRs beneath
       them should not be expanded.  This can happen if the type is a
       dynamic array type, for example.  */
    *walk_subtrees = 0;

  /* From here on, we're only interested in CALL_EXPRs.  */
  if (TREE_CODE (t) != CALL_EXPR)
    return NULL_TREE;

  /* First, see if we can figure out what function is being called.
     If we cannot, then there is no hope of inlining the function.  */
  fn = get_callee_fndecl (t);
  if (!fn)
    return NULL_TREE;

  /* Turn forward declarations into real ones.  */
  if (flag_unit_at_a_time)
    fn = cgraph_node (fn)->decl;

  /* If fn is a declaration of a function in a nested scope that was
     globally declared inline, we don't set its DECL_INITIAL.
     However, we can't blindly follow DECL_ABSTRACT_ORIGIN because the
     C++ front-end uses it for cdtors to refer to their internal
     declarations, that are not real functions.  Fortunately those
     don't have trees to be saved, so we can tell by checking their
     DECL_SAVED_TREE.  */
  if (! DECL_INITIAL (fn)
      && DECL_ABSTRACT_ORIGIN (fn)
      && DECL_SAVED_TREE (DECL_ABSTRACT_ORIGIN (fn)))
    fn = DECL_ABSTRACT_ORIGIN (fn);

  /* Don't try to inline functions that are not well-suited to
     inlining.  */
  if ((flag_unit_at_a_time
       && (!DECL_SAVED_TREE (fn) || !cgraph_inline_p (id->current_decl, fn)))
      || (!flag_unit_at_a_time && !limits_allow_inlining (fn, id)))
    {
      if (warn_inline && DECL_INLINE (fn) && DECL_DECLARED_INLINE_P (fn)
	  && !DECL_IN_SYSTEM_HEADER (fn))
	{
	  warning ("%Hinlining failed in call to '%F'",
                   TREE_LOCUS (fn), fn);
	  warning ("called from here");
	}
      return NULL_TREE;
    }

  if (! (*lang_hooks.tree_inlining.start_inlining) (fn))
    return NULL_TREE;

  /* Build a block containing code to initialize the arguments, the
     actual inline expansion of the body, and a label for the return
     statements within the function to jump to.  The type of the
     statement expression is the return type of the function call.  */
  stmt = NULL;
  expr = build (BIND_EXPR, TREE_TYPE (TREE_TYPE (fn)), NULL_TREE,
		stmt, make_node (BLOCK));
  BLOCK_ABSTRACT_ORIGIN (BIND_EXPR_BLOCK (expr)) = fn;

  /* Local declarations will be replaced by their equivalents in this
     map.  */
  st = id->decl_map;
  id->decl_map = splay_tree_new (splay_tree_compare_pointers,
				 NULL, NULL);

  /* Initialize the parameters.  */
  args = TREE_OPERAND (t, 1);
  return_slot_addr = NULL_TREE;
  if (CALL_EXPR_HAS_RETURN_SLOT_ADDR (t))
    {
      return_slot_addr = TREE_VALUE (args);
      args = TREE_CHAIN (args);
      TREE_TYPE (expr) = void_type_node;
    }

  arg_inits = initialize_inlined_parameters (id, args, fn, expr);
  if (arg_inits)
    {
      /* Expand any inlined calls in the initializers.  Do this before we
	 push FN on the stack of functions we are inlining; we want to
	 inline calls to FN that appear in the initializers for the
	 parameters.

	 Note we need to save and restore the saved tree statement iterator
	 to avoid having it clobbered by expand_calls_inline.  */
      tree_stmt_iterator save_tsi;
     
      save_tsi = id->tsi;
      expand_calls_inline (&arg_inits, id);
      id->tsi = save_tsi;

      /* And add them to the tree.  */
      BIND_EXPR_BODY (expr) = add_stmt_to_compound (BIND_EXPR_BODY (expr),
						    void_type_node, 
						    arg_inits);
    }

  /* Record the function we are about to inline so that we can avoid
     recursing into it.  */
  VARRAY_PUSH_TREE (id->fns, fn);

  /* Record the function we are about to inline if optimize_function
     has not been called on it yet and we don't have it in the list.  */
  if (! DECL_INLINED_FNS (fn))
    {
      int i;

      for (i = VARRAY_ACTIVE_SIZE (id->inlined_fns) - 1; i >= 0; i--)
	if (VARRAY_TREE (id->inlined_fns, i) == fn)
	  break;
      if (i < 0)
	VARRAY_PUSH_TREE (id->inlined_fns, fn);
    }

  /* Return statements in the function body will be replaced by jumps
     to the RET_LABEL.  */
  id->ret_label = build_decl (LABEL_DECL, NULL_TREE, NULL_TREE);
  DECL_CONTEXT (id->ret_label) = VARRAY_TREE (id->fns, 0);

  if (! DECL_INITIAL (fn)
      || TREE_CODE (DECL_INITIAL (fn)) != BLOCK)
    abort ();

  /* Declare the return variable for the function.  */
  decl = declare_return_variable (id, return_slot_addr, &use_retvar);
  if (decl)
    add_var_to_bind_expr (expr, decl);

  /* After we've initialized the parameters, we insert the body of the
     function itself.  */
  BIND_EXPR_BODY (expr)
    = add_stmt_to_compound (BIND_EXPR_BODY (expr), 
			    void_type_node, copy_body (id));
  inlined_body = &BIND_EXPR_BODY (expr);

  /* After the body of the function comes the RET_LABEL.  This must come
     before we evaluate the returned value below, because that evaluation
     may cause RTL to be generated.  */
  if (TREE_USED (id->ret_label))
    {
      tree label = build1 (LABEL_EXPR, void_type_node, id->ret_label);
      BIND_EXPR_BODY (expr)
	= add_stmt_to_compound (BIND_EXPR_BODY (expr), void_type_node, label);
    }

  /* Finally, mention the returned value so that the value of the
     statement-expression is the returned value of the function.  */
  if (use_retvar)
    BIND_EXPR_BODY (expr) 
      = add_stmt_to_compound (BIND_EXPR_BODY (expr), 
			      TREE_TYPE (use_retvar), use_retvar);

  /* Clean up.  */
  splay_tree_delete (id->decl_map);
  id->decl_map = st;

  /* The new expression has side-effects if the old one did.  */
  TREE_SIDE_EFFECTS (expr) = TREE_SIDE_EFFECTS (t);

  /* If we are working with gimple form, then we need to keep the tree
     in gimple form.  If we are not in gimple form, we can just replace
     *tp with the new BIND_EXPR.  */ 
  if (keep_function_tree_in_gimple_form (id->decl))
    {
      tree save_decl;

      /* Keep the new trees in gimple form.  */
      BIND_EXPR_BODY (expr) = rationalize_compound_expr (BIND_EXPR_BODY (expr));

      /* We want to create a new variable to hold the result of the
	 inlined body.  This new variable needs to be added to the
	 function which we are inlining into, thus the saving and
	 restoring of current_function_decl.  */
      save_decl = current_function_decl;
      current_function_decl = id->decl;
      inline_result = voidify_wrapper_expr (expr);
      current_function_decl = save_decl;

      /* If the inlined function returns a result that we care about,
	 then we're going to need to splice in a MODIFY_EXPR.  Otherwise
	 the call was a standalone statement and we can just replace it
	 with the BIND_EXPR inline representation of the called function.  */
      if (TREE_CODE (*tsi_stmt_ptr (id->tsi)) != CALL_EXPR)
	{
	  tree *container_p = tsi_container (id->tsi);
	  tree container = *container_p;

	  if (TREE_CODE (container) != COMPOUND_EXPR)
	    {
	      /* If the container is not a COMPOUND_EXPR, then simply
		 calling add_stmt_to_compound property insert the BIND_EXPR
		 into the proper location.  */
	      *container_p
		= add_stmt_to_compound (expr, TREE_TYPE (expr), container);
	    }
	  else
	    {
	      /* Insertion of our new COMPOUND_EXPR is slightly more
	         complex in this case.  We build a the new COMPOUND_EXPR
		 and set its operands to the contents of the original
		 COMPOUND_EXPR.  */
	      tree new_ce = build (COMPOUND_EXPR, TREE_TYPE (expr), 
				   TREE_OPERAND (container, 0),
				   TREE_OPERAND (container, 1));

	      /* Then we reset the operands of the original
	         COMPOUND_EXPR to the new BIND_EXPR and the new
		 COMPOUND_EXPR.  */
	      TREE_OPERAND (container, 0) = expr;
	      TREE_OPERAND (container, 1) = new_ce;
	    }

	  /* Replace the RHS of the MODIFY_EXPR.  */
	  *tp = inline_result;
	}
      else
	*tp = expr;

      /* When we gimplify a function call, we may clear TREE_SIDE_EFFECTS
	 on the call if it is to a "const" function.  Thus the copy of
	 TREE_SIDE_EFFECTS from the CALL_EXPR to the BIND_EXPR above
	 with result in TREE_SIDE_EFFECTS not being set for the inlined
	 copy of a "const" function.

	 Unfortunately, that is wrong as inlining the function
	 can create/expose interesting side effects (such as setting
	 of a return value).

	 The easiest solution is to simply recalculate TREE_SIDE_EFFECTS
	 for the toplevel expression.  */
      recalculate_side_effects (expr);
    }
  else
    *tp = expr;

  /* If the value of the new expression is ignored, that's OK.  We
     don't warn about this for CALL_EXPRs, so we shouldn't warn about
     the equivalent inlined version either.  */
  TREE_USED (*tp) = 1;

  /* Our function now has more statements than it did before.  */
  DECL_ESTIMATED_INSNS (VARRAY_TREE (id->fns, 0)) += DECL_ESTIMATED_INSNS (fn);
  /* For accounting, subtract one for the saved call/ret.  */
  id->inlined_insns += DECL_ESTIMATED_INSNS (fn) - 1;

  /* Update callgraph if needed.  */
  if (id->decl && flag_unit_at_a_time)
    {
      cgraph_remove_call (id->decl, fn);
      cgraph_create_edges (id->decl, *inlined_body);
    }

  /* Recurse into the body of the just inlined function.  */
  {
    tree old_decl = id->current_decl;
    id->current_decl = fn;
    expand_calls_inline (inlined_body, id);
    id->current_decl = old_decl;
  }
  VARRAY_POP (id->fns);

  /* If we've returned to the top level, clear out the record of how
     much inlining has been done.  */
  if (VARRAY_ACTIVE_SIZE (id->fns) == id->first_inlined_fn)
    id->inlined_insns = 0;

  /* Don't walk into subtrees.  We've already handled them above.  */
  *walk_subtrees = 0;

  (*lang_hooks.tree_inlining.end_inlining) (fn);

  /* Keep iterating.  */
  return NULL_TREE;
}

/* Walk over the entire tree *TP, replacing CALL_EXPRs with inline
   expansions as appropriate.  */

static void
expand_calls_inline (tree *tp, inline_data *id)
{
  tree_stmt_iterator i;

  /* If we are not in gimple form, then we want to walk the tree
     recursively as we do not know anything about the structure
     of the tree.  */

  if (! keep_function_tree_in_gimple_form (id->decl))
    {
      walk_tree (tp, expand_call_inline, id, id->tree_pruner);
      return;
    }

  /* We are in gimple form.  We want to stay in gimple form.  Walk
     the statements, inlining calls in each statement.  By walking
     the statements, we have enough information to keep the tree
     in gimple form as we insert inline bodies.  */
  for (i = tsi_start (tp); !tsi_end_p (i); tsi_next (&i))
    {
      tree *stmt_p = tsi_stmt_ptr (i);
      enum tree_code code = TREE_CODE (*stmt_p); 

      if (code == LOOP_EXPR)
	{
	  /* Dive into the LOOP_EXPR.  */
	  expand_calls_inline (&LOOP_EXPR_BODY (*stmt_p), id);
	}
      else if (code == COND_EXPR)
        {
	  /* Dive into the COND_EXPR.  */
	  expand_calls_inline (&COND_EXPR_COND (*stmt_p), id);
	  expand_calls_inline (&COND_EXPR_THEN (*stmt_p), id);
	  expand_calls_inline (&COND_EXPR_ELSE (*stmt_p), id);
        }
      else if (code == CATCH_EXPR)
        {
	  /* Dive into the CATCH_EXPR.  */
	  expand_calls_inline (&CATCH_BODY (*stmt_p), id);
        }
      else if (code == EH_FILTER_EXPR)
        {
	  /* Dive into the EH_FILTER_EXPR.  */
	  expand_calls_inline (&EH_FILTER_FAILURE (*stmt_p), id);
        }
      else if (code == TRY_CATCH_EXPR || code == TRY_FINALLY_EXPR)
        {
	  /* Dive into TRY_*_EXPRs.  */
	  expand_calls_inline (&TREE_OPERAND (*stmt_p, 0), id);
	  expand_calls_inline (&TREE_OPERAND (*stmt_p, 1), id);
        }
      else if (code == SWITCH_EXPR)
        {
	  /* Dive into the SWITCH_EXPR.  */
	  expand_calls_inline (&SWITCH_COND (*stmt_p), id);
	  expand_calls_inline (&SWITCH_BODY (*stmt_p), id);
        }
      else if (code == BIND_EXPR)
        {
	  /* Dive into the BIND_EXPR.  */
	  expand_calls_inline (&BIND_EXPR_BODY (*stmt_p), id);
        }
      else if (code == COMPOUND_EXPR)
        {
	  /* Dive into the COMPOUND_EXPR; this should only happen at
	     the end of a function tree, so the recursion isn't nearly
	     as bad as you might think.  */
	  expand_calls_inline (&TREE_OPERAND (*stmt_p, 0), id);
	  expand_calls_inline (&TREE_OPERAND (*stmt_p, 1), id);
        }
      else
	{
          /* Search through *TP, replacing all calls to inline functions by
	     appropriate equivalents.  Use walk_tree in no-duplicates mode
 	     to avoid exponential time complexity.  (We can't just use
	     walk_tree_without_duplicates, because of the special TARGET_EXPR
	     handling in expand_calls.  The hash table is set up in
	     optimize_function.  */
	  id->tsi = i;
	  walk_tree (stmt_p, expand_call_inline, id, id->tree_pruner);
	}
    }
}

/* Expand calls to inline functions in the body of FN.  */

void
optimize_inline_calls (tree fn)
{
  inline_data id;
  tree prev_fn;

  /* Clear out ID.  */
  memset (&id, 0, sizeof (id));

  id.decl = fn;
  id.current_decl = fn;
  /* Don't allow recursion into FN.  */
  VARRAY_TREE_INIT (id.fns, 32, "fns");
  VARRAY_PUSH_TREE (id.fns, fn);
  if (!DECL_ESTIMATED_INSNS (fn))
    DECL_ESTIMATED_INSNS (fn) 
      = (*lang_hooks.tree_inlining.estimate_num_insns) (fn);
  /* Or any functions that aren't finished yet.  */
  prev_fn = NULL_TREE;
  if (current_function_decl)
    {
      VARRAY_PUSH_TREE (id.fns, current_function_decl);
      prev_fn = current_function_decl;
    }

  prev_fn = ((*lang_hooks.tree_inlining.add_pending_fn_decls)
	     (&id.fns, prev_fn));

  /* Create the list of functions this call will inline.  */
  VARRAY_TREE_INIT (id.inlined_fns, 32, "inlined_fns");

  /* Keep track of the low-water mark, i.e., the point where the first
     real inlining is represented in ID.FNS.  */
  id.first_inlined_fn = VARRAY_ACTIVE_SIZE (id.fns);

  /* Replace all calls to inline functions with the bodies of those
     functions.  */
  id.tree_pruner = htab_create (37, htab_hash_pointer,
				htab_eq_pointer, NULL);
  expand_calls_inline (&DECL_SAVED_TREE (fn), &id);

  /* Clean up.  */
  htab_delete (id.tree_pruner);
  if (DECL_LANG_SPECIFIC (fn))
    {
      tree ifn = make_tree_vec (VARRAY_ACTIVE_SIZE (id.inlined_fns));

      if (VARRAY_ACTIVE_SIZE (id.inlined_fns))
	memcpy (&TREE_VEC_ELT (ifn, 0), &VARRAY_TREE (id.inlined_fns, 0),
		VARRAY_ACTIVE_SIZE (id.inlined_fns) * sizeof (tree));
      DECL_INLINED_FNS (fn) = ifn;
    }
}

/* FN is a function that has a complete body, and CLONE is a function
   whose body is to be set to a copy of FN, mapping argument
   declarations according to the ARG_MAP splay_tree.  */

void
clone_body (tree clone, tree fn, void *arg_map)
{
  inline_data id;

  /* Clone the body, as if we were making an inline call.  But, remap
     the parameters in the callee to the parameters of caller.  If
     there's an in-charge parameter, map it to an appropriate
     constant.  */
  memset (&id, 0, sizeof (id));
  VARRAY_TREE_INIT (id.fns, 2, "fns");
  VARRAY_PUSH_TREE (id.fns, clone);
  VARRAY_PUSH_TREE (id.fns, fn);
  id.decl_map = (splay_tree)arg_map;

  /* Cloning is treated slightly differently from inlining.  Set
     CLONING_P so that it's clear which operation we're performing.  */
  id.cloning_p = true;

  /* Actually copy the body.  */
  TREE_CHAIN (DECL_SAVED_TREE (clone)) = copy_body (&id);
}

/* Apply FUNC to all the sub-trees of TP in a pre-order traversal.
   FUNC is called with the DATA and the address of each sub-tree.  If
   FUNC returns a non-NULL value, the traversal is aborted, and the
   value returned by FUNC is returned.  If HTAB is non-NULL it is used
   to record the nodes visited, and to avoid visiting a node more than
   once.  */

tree
walk_tree (tree *tp, walk_tree_fn func, void *data, void *htab_)
{
  htab_t htab = (htab_t) htab_;
  enum tree_code code;
  int walk_subtrees;
  tree result;

#define WALK_SUBTREE(NODE)				\
  do							\
    {							\
      result = walk_tree (&(NODE), func, data, htab);	\
      if (result)					\
	return result;					\
    }							\
  while (0)

#define WALK_SUBTREE_TAIL(NODE)				\
  do							\
    {							\
       tp = & (NODE);					\
       goto tail_recurse;				\
    }							\
  while (0)

 tail_recurse:
  /* Skip empty subtrees.  */
  if (!*tp)
    return NULL_TREE;

  if (htab)
    {
      void **slot;

      /* Don't walk the same tree twice, if the user has requested
         that we avoid doing so.  */
      slot = htab_find_slot (htab, *tp, INSERT);
      if (*slot)
	return NULL_TREE;
      *slot = *tp;
    }

  /* Call the function.  */
  walk_subtrees = 1;
  result = (*func) (tp, &walk_subtrees, data);

  /* If we found something, return it.  */
  if (result)
    return result;

  code = TREE_CODE (*tp);

  /* Even if we didn't, FUNC may have decided that there was nothing
     interesting below this point in the tree.  */
  if (!walk_subtrees)
    {
      if (code == TREE_LIST
	  || (*lang_hooks.tree_inlining.tree_chain_matters_p) (*tp))
	/* But we still need to check our siblings.  */
	WALK_SUBTREE_TAIL (TREE_CHAIN (*tp));
      else
	return NULL_TREE;
    }

  result = (*lang_hooks.tree_inlining.walk_subtrees) (tp, &walk_subtrees, func,
						      data, htab);
  if (result || ! walk_subtrees)
    return result;

  if (code != EXIT_BLOCK_EXPR
      && code != SAVE_EXPR
      && IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (code)))
    {
      int i, len;

      /* Set input_line here so we get the right instantiation context
         if we call instantiate_decl from inlinable_function_p.  */
      if (TREE_LOCUS (*tp))
        input_line = TREE_LINENO (*tp);

      /* Walk over all the sub-trees of this operand.  */
      len = first_rtl_op (code);
      /* TARGET_EXPRs are peculiar: operands 1 and 3 can be the same.
	 But, we only want to walk once.  */
      if (code == TARGET_EXPR
	  && TREE_OPERAND (*tp, 3) == TREE_OPERAND (*tp, 1))
	--len;
      /* Go through the subtrees.  We need to do this in forward order so
         that the scope of a FOR_EXPR is handled properly.  */
      for (i = 0; i < len; ++i)
	WALK_SUBTREE (TREE_OPERAND (*tp, i));

      if (code == BIND_EXPR)
	{
	  tree decl;
	  for (decl = BIND_EXPR_VARS (*tp); decl; decl = TREE_CHAIN (decl))
	    {
	      /* Walk the DECL_INITIAL and DECL_SIZE.  We don't want to walk
		 into declarations that are just mentioned, rather than
		 declared; they don't really belong to this part of the tree.
		 And, we can see cycles: the initializer for a declaration can
		 refer to the declaration itself.  */
	      WALK_SUBTREE (DECL_INITIAL (decl));
	      WALK_SUBTREE (DECL_SIZE (decl));
	      WALK_SUBTREE (DECL_SIZE_UNIT (decl));
	    }
	}

      if ((*lang_hooks.tree_inlining.tree_chain_matters_p) (*tp))
	/* Check our siblings.  */
	WALK_SUBTREE_TAIL (TREE_CHAIN (*tp));
    }
  else if (TREE_CODE_CLASS (code) == 'd')
    {
      WALK_SUBTREE_TAIL (TREE_TYPE (*tp));
    }
  else
    {
      if (TREE_CODE_CLASS (code) == 't')
	{
	  WALK_SUBTREE (TYPE_SIZE (*tp));
	  WALK_SUBTREE (TYPE_SIZE_UNIT (*tp));
	  /* Also examine various special fields, below.  */
	}

      /* Not one of the easy cases.  We must explicitly go through the
	 children.  */
      switch (code)
	{
	case ERROR_MARK:
	case IDENTIFIER_NODE:
	case INTEGER_CST:
	case REAL_CST:
	case VECTOR_CST:
	case STRING_CST:
	case REAL_TYPE:
	case COMPLEX_TYPE:
	case VECTOR_TYPE:
	case VOID_TYPE:
	case BOOLEAN_TYPE:
	case UNION_TYPE:
	case ENUMERAL_TYPE:
	case BLOCK:
	case RECORD_TYPE:
	case SSA_NAME:
	  /* None of thse have subtrees other than those already walked
	     above.  */
	  break;

	case POINTER_TYPE:
	case REFERENCE_TYPE:
	  WALK_SUBTREE_TAIL (TREE_TYPE (*tp));
	  break;

	case TREE_LIST:
	  WALK_SUBTREE (TREE_VALUE (*tp));
	  WALK_SUBTREE_TAIL (TREE_CHAIN (*tp));
	  break;

	case TREE_VEC:
	  {
	    int len = TREE_VEC_LENGTH (*tp);

	    if (len == 0)
	      break;

	    /* Walk all elements but the first.  */
	    while (--len)
	      WALK_SUBTREE (TREE_VEC_ELT (*tp, len));

	    /* Now walk the first one as a tail call.  */
	    WALK_SUBTREE_TAIL (TREE_VEC_ELT (*tp, 0));
	  }

	case COMPLEX_CST:
	  WALK_SUBTREE (TREE_REALPART (*tp));
	  WALK_SUBTREE_TAIL (TREE_IMAGPART (*tp));

	case CONSTRUCTOR:
	  WALK_SUBTREE_TAIL (CONSTRUCTOR_ELTS (*tp));

	case METHOD_TYPE:
	  WALK_SUBTREE (TYPE_METHOD_BASETYPE (*tp));
	  /* Fall through.  */

	case FUNCTION_TYPE:
	  WALK_SUBTREE (TREE_TYPE (*tp));
	  {
	    tree arg = TYPE_ARG_TYPES (*tp);

	    /* We never want to walk into default arguments.  */
	    for (; arg; arg = TREE_CHAIN (arg))
	      WALK_SUBTREE (TREE_VALUE (arg));
	  }
	  break;

	case ARRAY_TYPE:
	  WALK_SUBTREE (TREE_TYPE (*tp));
	  WALK_SUBTREE_TAIL (TYPE_DOMAIN (*tp));

	case INTEGER_TYPE:
	case CHAR_TYPE:
	  WALK_SUBTREE (TYPE_MIN_VALUE (*tp));
	  WALK_SUBTREE_TAIL (TYPE_MAX_VALUE (*tp));

	case OFFSET_TYPE:
	  WALK_SUBTREE (TREE_TYPE (*tp));
	  WALK_SUBTREE_TAIL (TYPE_OFFSET_BASETYPE (*tp));

	case EXIT_BLOCK_EXPR:
	  WALK_SUBTREE_TAIL (TREE_OPERAND (*tp, 1));

	case SAVE_EXPR:
	  WALK_SUBTREE_TAIL (TREE_OPERAND (*tp, 0));

	default:
	  abort ();
	}
    }

  /* We didn't find what we were looking for.  */
  return NULL_TREE;

#undef WALK_SUBTREE
#undef WALK_SUBTREE_TAIL
}

/* Like walk_tree, but does not walk duplicate nodes more than
   once.  */

tree
walk_tree_without_duplicates (tree *tp, walk_tree_fn func, void *data)
{
  tree result;
  htab_t htab;

  htab = htab_create (37, htab_hash_pointer, htab_eq_pointer, NULL);
  result = walk_tree (tp, func, data, htab);
  htab_delete (htab);
  return result;
}

/* Passed to walk_tree.  Copies the node pointed to, if appropriate.  */

tree
copy_tree_r (tree *tp, int *walk_subtrees, void *data ATTRIBUTE_UNUSED)
{
  enum tree_code code = TREE_CODE (*tp);

  /* We make copies of most nodes.  */
  if (IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (code))
      || TREE_CODE_CLASS (code) == 'c'
      || code == TREE_LIST
      || code == TREE_VEC
      || (*lang_hooks.tree_inlining.tree_chain_matters_p) (*tp))
    {
      /* Because the chain gets clobbered when we make a copy, we save it
	 here.  */
      tree chain = TREE_CHAIN (*tp);

      /* Copy the node.  */
      *tp = copy_node (*tp);

      /* Now, restore the chain, if appropriate.  That will cause
	 walk_tree to walk into the chain as well.  */
      if (code == PARM_DECL || code == TREE_LIST
	  || (*lang_hooks.tree_inlining.tree_chain_matters_p) (*tp))
	TREE_CHAIN (*tp) = chain;

      /* For now, we don't update BLOCKs when we make copies.  So, we
	 have to nullify all BIND_EXPRs.  */
      if (TREE_CODE (*tp) == BIND_EXPR)
	BIND_EXPR_BLOCK (*tp) = NULL_TREE;
    }
  else if (TREE_CODE_CLASS (code) == 't' && !variably_modified_type_p (*tp))
    /* Types only need to be copied if they are variably modified.  */
    *walk_subtrees = 0;
  else if (TREE_CODE_CLASS (code) == 'd')
    *walk_subtrees = 0;

  return NULL_TREE;
}

/* The SAVE_EXPR pointed to by TP is being copied.  If ST contains
   information indicating to what new SAVE_EXPR this one should be
   mapped, use that one.  Otherwise, create a new node and enter it in
   ST.  FN is the function into which the copy will be placed.  */

void
remap_save_expr (tree *tp, void *st_, tree fn, int *walk_subtrees)
{
  splay_tree st = (splay_tree) st_;
  splay_tree_node n;

  /* See if we already encountered this SAVE_EXPR.  */
  n = splay_tree_lookup (st, (splay_tree_key) *tp);

  /* If we didn't already remap this SAVE_EXPR, do so now.  */
  if (!n)
    {
      tree t = copy_node (*tp);

      /* The SAVE_EXPR is now part of the function into which we
	 are inlining this body.  */
      SAVE_EXPR_CONTEXT (t) = fn;
      /* And we haven't evaluated it yet.  */
      SAVE_EXPR_RTL (t) = NULL_RTX;
      /* Remember this SAVE_EXPR.  */
      n = splay_tree_insert (st,
			     (splay_tree_key) *tp,
			     (splay_tree_value) t);
      /* Make sure we don't remap an already-remapped SAVE_EXPR.  */
      splay_tree_insert (st, (splay_tree_key) t,
			 (splay_tree_value) error_mark_node);
    }
  else
    /* We've already walked into this SAVE_EXPR, so we needn't do it
       again.  */
    *walk_subtrees = 0;

  /* Replace this SAVE_EXPR with the copy.  */
  *tp = (tree) n->value;
}

/* Add STMT to EXISTING if possible, otherwise create a new
   COMPOUND_EXPR and add STMT to it.  */

static tree
add_stmt_to_compound (tree existing, tree type, tree stmt)
{
  if (!stmt)
    return existing;
  else if (existing)
    return build (COMPOUND_EXPR, type, existing, stmt);
  else
    return stmt;
}

/* Called via walk_tree.  If *TP points to a DECL_STMT for a local
   declaration, copies the declaration and enters it in the splay_tree
   in DATA (which is really an `inline_data *').  */

static tree
mark_local_for_remap_r (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
			void *data)
{
  tree t = *tp;
  inline_data *id = (inline_data *) data;
  splay_tree st = id->decl_map;
  tree decl;

  /* Don't walk into types.  */
  if (TYPE_P (t))
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }

  if (TREE_CODE (t) == LABEL_EXPR)
    decl = TREE_OPERAND (t, 0);
  else
    /* We don't need to handle anything else ahead of time.  */
    decl = NULL_TREE;

  if (decl)
    {
      tree copy;

      /* Make a copy.  */
      copy = copy_decl_for_inlining (decl, 
				     DECL_CONTEXT (decl), 
				     DECL_CONTEXT (decl));

      /* Remember the copy.  */
      splay_tree_insert (st,
			 (splay_tree_key) decl, 
			 (splay_tree_value) copy);
    }

  return NULL_TREE;
}

/* Called via walk_tree when an expression is unsaved.  Using the
   splay_tree pointed to by ST (which is really a `splay_tree'),
   remaps all local declarations to appropriate replacements.  */

static tree
unsave_r (tree *tp, int *walk_subtrees, void *data)
{
  inline_data *id = (inline_data *) data;
  splay_tree st = id->decl_map;
  splay_tree_node n;

  /* Only a local declaration (variable or label).  */
  if ((TREE_CODE (*tp) == VAR_DECL && !TREE_STATIC (*tp))
      || TREE_CODE (*tp) == LABEL_DECL)
    {
      /* Lookup the declaration.  */
      n = splay_tree_lookup (st, (splay_tree_key) *tp);
      
      /* If it's there, remap it.  */
      if (n)
	*tp = (tree) n->value;
    }
  else if (TREE_CODE (*tp) == BIND_EXPR)
    copy_bind_expr (tp, walk_subtrees, id);
  else if (TREE_CODE (*tp) == SAVE_EXPR)
    remap_save_expr (tp, st, current_function_decl, walk_subtrees);
  else
    {
      copy_tree_r (tp, walk_subtrees, NULL);

      /* Do whatever unsaving is required.  */
      unsave_expr_1 (*tp);
    }

  /* Keep iterating.  */
  return NULL_TREE;
}

/* Default lang hook for "unsave_expr_now".  Copies everything in EXPR and
   replaces variables, labels and SAVE_EXPRs local to EXPR.  */

tree
lhd_unsave_expr_now (tree expr)
{
  inline_data id;

  /* There's nothing to do for NULL_TREE.  */
  if (expr == 0)
    return expr;

  /* Set up ID.  */
  memset (&id, 0, sizeof (id));
  VARRAY_TREE_INIT (id.fns, 1, "fns");
  VARRAY_PUSH_TREE (id.fns, current_function_decl);
  id.decl_map = splay_tree_new (splay_tree_compare_pointers, NULL, NULL);

  /* Walk the tree once to find local labels.  */
  walk_tree_without_duplicates (&expr, mark_local_for_remap_r, &id);

  /* Walk the tree again, copying, remapping, and unsaving.  */
  walk_tree (&expr, unsave_r, &id, NULL);

  /* Clean up.  */
  splay_tree_delete (id.decl_map);

  return expr;
}

/* Allow someone to determine if SEARCH is a child of TOP from gdb.  */
static tree
debug_find_tree_1 (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED, void *data)
{
  if (*tp == data)
    return (tree) data;
  else
    return NULL;
}

extern bool debug_find_tree (tree top, tree search);

bool
debug_find_tree (tree top, tree search)
{
  return walk_tree_without_duplicates (&top, debug_find_tree_1, search) != 0;
}
