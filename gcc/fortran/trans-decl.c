/* Backend function setup
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
   Contributed by Paul Brook

This file is part of GNU G95.

GNU G95 is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU G95 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU G95; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* trans-decl.c -- Handling of backend function and variable decls, etc */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "tree-dump.h"
#include "tree-simple.h"
#include <stdio.h>
#include "ggc.h"
#include "toplev.h"
#include "tm.h"
#include "target.h"
#include "function.h"
#include "errors.h"
#include "flags.h"
#include <assert.h>
#include "gfortran.h"
#include "trans.h"
#include "trans-types.h"
#include "trans-array.h"
#include "trans-const.h"
/* Only for gfc_trans_code.  Shouldn't need to include this.  */
#include "trans-stmt.h"

#define MAX_LABEL_VALUE 99999


/* Holds the result of the function if no result variable specified.  */

static GTY(()) tree current_fake_result_decl;

static GTY(()) tree current_function_return_label;


/* Holds the variable DECLs for the current function.  */

static GTY(()) tree saved_function_decls = NULL_TREE;
static GTY(()) tree saved_parent_function_decls = NULL_TREE;


/* The namespace of the module we're currently generating.  Only used while
   outputting decls for module variables.  Do not rely on this being set.  */

static gfc_namespace *module_namespace;


/* List of static constructor functions.  */

tree gfc_static_ctors;


/* Function declarations for builtin library functions.  */

tree gfor_fndecl_internal_malloc;
tree gfor_fndecl_internal_malloc64;
tree gfor_fndecl_internal_free;
tree gfor_fndecl_allocate;
tree gfor_fndecl_allocate64;
tree gfor_fndecl_deallocate;
tree gfor_fndecl_pause_numeric;
tree gfor_fndecl_pause_string;
tree gfor_fndecl_stop_numeric;
tree gfor_fndecl_stop_string;
tree gfor_fndecl_select_string;
tree gfor_fndecl_runtime_error;
tree gfor_fndecl_in_pack;
tree gfor_fndecl_in_unpack;
tree gfor_fndecl_associated;


/* Math functions.  Many other math functions are handled in
   trans-intrinsic.c.  */

tree gfor_fndecl_math_powf;
tree gfor_fndecl_math_pow;
tree gfor_fndecl_math_cpowf;
tree gfor_fndecl_math_cpow;
tree gfor_fndecl_math_cabsf;
tree gfor_fndecl_math_cabs;
tree gfor_fndecl_math_sign4;
tree gfor_fndecl_math_sign8;
tree gfor_fndecl_math_ishftc4;
tree gfor_fndecl_math_ishftc8;
tree gfor_fndecl_math_exponent4;
tree gfor_fndecl_math_exponent8;


/* String functions.  */

tree gfor_fndecl_copy_string;
tree gfor_fndecl_compare_string;
tree gfor_fndecl_concat_string;
tree gfor_fndecl_string_len_trim;
tree gfor_fndecl_string_index;
tree gfor_fndecl_string_scan;
tree gfor_fndecl_string_verify;
tree gfor_fndecl_string_trim;
tree gfor_fndecl_string_repeat;
tree gfor_fndecl_adjustl;
tree gfor_fndecl_adjustr;


/* Other misc. runtime library functions.  */

tree gfor_fndecl_size0;
tree gfor_fndecl_size1;

/* Intrinsic functions implemented in FORTRAN.  */
tree gfor_fndecl_si_kind;
tree gfor_fndecl_sr_kind;


static void
gfc_add_decl_to_parent_function (tree decl)
{
  assert (decl);
  DECL_CONTEXT (decl) = DECL_CONTEXT (current_function_decl);
  DECL_NONLOCAL (decl) = 1;
  TREE_CHAIN (decl) = saved_parent_function_decls;
  saved_parent_function_decls = decl;
}

void
gfc_add_decl_to_function (tree decl)
{
  assert (decl);
  TREE_USED (decl) = 1;
  DECL_CONTEXT (decl) = current_function_decl;
  TREE_CHAIN (decl) = saved_function_decls;
  saved_function_decls = decl;
}


/* Build a  backend label declaration.
   Set TREE_USED for named lables.  For artificial labels it's up to the
   caller to mark the label as used.  */

tree
gfc_build_label_decl (tree label_id)
{
  /* 2^32 temporaries should be enough.  */
  static unsigned int tmp_num = 1;
  tree label_decl;
  char *label_name;

  if (label_id == NULL_TREE)
    {
      /* Build an internal label name.  */
      ASM_FORMAT_PRIVATE_NAME (label_name, "L", tmp_num++);
      label_id = get_identifier (label_name);
    }
  else
    label_name = NULL;

  /* Build the LABEL_DECL node. Labels have no type.  */
  label_decl = build_decl (LABEL_DECL, label_id, void_type_node);
  DECL_CONTEXT (label_decl) = current_function_decl;
  DECL_MODE (label_decl) = VOIDmode;

  if (label_name)
    {
      DECL_ARTIFICIAL (label_decl) = 1;
    }
  else
    {
      /* We always define the label as used, even if the original source
         file never references the label.  We don't want all kinds of
         spurious warnings for old-style Fortran code with too many
         labels.  */
      TREE_USED (label_decl) = 1;
    }

  return label_decl;
}


/* Returns the return label for the current function.  */

tree
gfc_get_return_label (void)
{
  char name[GFC_MAX_SYMBOL_LEN + 10];

  if (current_function_return_label)
    return current_function_return_label;

  sprintf (name, "__return_%s",
	   IDENTIFIER_POINTER (DECL_NAME (current_function_decl)));

  current_function_return_label =
    gfc_build_label_decl (get_identifier (name));

  DECL_ARTIFICIAL (current_function_return_label) = 1;

  return current_function_return_label;
}


/* Return the backend label declaration for a given label structure,
   or create it if it doesn't exist yet.  */

tree
gfc_get_label_decl (gfc_st_label * lp)
{

  if (lp->backend_decl)
    return lp->backend_decl;
  else
    {
      char label_name[GFC_MAX_SYMBOL_LEN + 1];
      tree label_decl;

      /* Validate the label declaration from the front end.  */
      assert (lp != NULL && lp->value <= MAX_LABEL_VALUE);

      /* Build a mangled name for the label.  */
      sprintf (label_name, "__label_%.6d", lp->value);

      /* Build the LABEL_DECL node.  */
      label_decl = gfc_build_label_decl (get_identifier (label_name));

      /* Tell the debugger where the label came from.  */
      if (lp->value <= MAX_LABEL_VALUE)	/* An internal label */
	{
	  DECL_SOURCE_LINE (label_decl) = lp->where.line;
	  DECL_SOURCE_FILE (label_decl) = lp->where.file->filename;
	}
      else
	DECL_ARTIFICIAL (label_decl) = 1;

      /* Store the label in the label list and return the LABEL_DECL.  */
      lp->backend_decl = label_decl;
      return label_decl;
    }
}


/* Convert a gfc_symbol to an identifier of the same name.  */

static tree
gfc_sym_identifier (gfc_symbol * sym)
{
  return (get_identifier (sym->name));
}


/* Construct mangled name from symbol name.  */

static tree
gfc_sym_mangled_identifier (gfc_symbol * sym)
{
  char name[GFC_MAX_MANGLED_SYMBOL_LEN + 1];

  if (sym->module[0] == 0)
    return gfc_sym_identifier (sym);
  else
    {
      snprintf (name, sizeof name, "__%s__%s", sym->module, sym->name);
      return get_identifier (name);
    }
}


/* Construct mangled function name from symbol name.  */

static tree
gfc_sym_mangled_function_id (gfc_symbol * sym)
{
  int has_underscore;
  char name[GFC_MAX_MANGLED_SYMBOL_LEN + 1];

  if (sym->module[0] == 0 || sym->attr.proc == PROC_EXTERNAL
      || (sym->module[0] != 0 && sym->attr.if_source == IFSRC_IFBODY))
    {
      if (strcmp (sym->name, "MAIN__") == 0
	  || sym->attr.proc == PROC_INTRINSIC)
	return get_identifier (sym->name);

      if (gfc_option.flag_underscoring)
	{
	  has_underscore = strchr (sym->name, '_') != 0;
	  if (gfc_option.flag_second_underscore && has_underscore)
	    snprintf (name, sizeof name, "%s__", sym->name);
	  else
	    snprintf (name, sizeof name, "%s_", sym->name);
	  return get_identifier (name);
	}
      else
	return get_identifier (sym->name);
    }
  else
    {
      snprintf (name, sizeof name, "__%s__%s", sym->module, sym->name);
      return get_identifier (name);
    }
}


/* Finish processing of a declaration and install its initial value.  */

static void
gfc_finish_decl (tree decl, tree init)
{
  if (TREE_CODE (decl) == PARM_DECL)
    assert (init == NULL_TREE);
  /* Remember that PARM_DECL doesn't have a DECL_INITIAL field per se
     -- it overlaps DECL_ARG_TYPE.  */
  else if (init == NULL_TREE)
    assert (DECL_INITIAL (decl) == NULL_TREE);
  else
    assert (DECL_INITIAL (decl) == error_mark_node);

  if (init != NULL_TREE)
    {
      if (TREE_CODE (decl) != TYPE_DECL)
	DECL_INITIAL (decl) = init;
      else
	{
	  /* typedef foo = bar; store the type of bar as the type of foo.  */
	  TREE_TYPE (decl) = TREE_TYPE (init);
	  DECL_INITIAL (decl) = init = 0;
	}
    }

  if (TREE_CODE (decl) == VAR_DECL)
    {
      if (DECL_SIZE (decl) == NULL_TREE
	  && TYPE_SIZE (TREE_TYPE (decl)) != NULL_TREE)
	layout_decl (decl, 0);

      /* A static variable with an incomplete type is an error if it is
         initialized. Also if it is not file scope. Otherwise, let it
         through, but if it is not `extern' then it may cause an error
         message later.  */
      /* An automatic variable with an incomplete type is an error.  */
      if (DECL_SIZE (decl) == NULL_TREE
          && (TREE_STATIC (decl) ? (DECL_INITIAL (decl) != 0
				    || DECL_CONTEXT (decl) != 0)
                                 : !DECL_EXTERNAL (decl)))
	{
	  gfc_fatal_error ("storage size not known");
	}

      if ((DECL_EXTERNAL (decl) || TREE_STATIC (decl))
	  && (DECL_SIZE (decl) != 0)
	  && (TREE_CODE (DECL_SIZE (decl)) != INTEGER_CST))
	{
	  gfc_fatal_error ("storage size not constant");
	}
    }

}


/* Apply symbol attributes to a variable, and add it to the function scope.  */

static void
gfc_finish_var_decl (tree decl, gfc_symbol * sym)
{
  /* TREE_ADDRESSABLE means the address of this variable is acualy needed.
     This is the equivalent of the TARGET variables.
     We also need to set this if the variable is passed by reference in a
     CALL statement.  */
  if (sym->attr.target)
    TREE_ADDRESSABLE (decl) = 1;
  /* If it wasn't used we wouldn't be getting it.  */
  TREE_USED (decl) = 1;

  /* Chain this decl to the pending declarations.  Don't do pushdecl()
     because this would add them to the current scope rather than the
     function scope.  */
  if (current_function_decl != NULL_TREE)
    {
      if (sym->ns->proc_name->backend_decl == current_function_decl)
	gfc_add_decl_to_function (decl);
      else
	gfc_add_decl_to_parent_function (decl);
    }

  /* If a variable is USE associated, it's always external.  */
  if (sym->attr.use_assoc)
    {
      DECL_EXTERNAL (decl) = 1;
      TREE_PUBLIC (decl) = 1;
    }
  else if (sym->module[0] && !sym->attr.result)
    {
      /* TODO: Don't set sym->module for result variables.  */
      assert (current_function_decl == NULL_TREE);
      /* This is the declaration of a module variable.  */
      TREE_PUBLIC (decl) = 1;
      TREE_STATIC (decl) = 1;
    }

  if ((sym->attr.save || sym->attr.data || sym->value)
      && !sym->attr.use_assoc)
    TREE_STATIC (decl) = 1;
  
  /* Keep variables larger than max-stack-var-size off stack.  */
  if (!sym->ns->proc_name->attr.recursive
      && !gfc_can_put_var_on_stack (DECL_SIZE_UNIT (decl)))
    TREE_STATIC (decl) = 1;
}


/* Allocate the lang-specific part of a decl.  */

void
gfc_allocate_lang_decl (tree decl)
{
  DECL_LANG_SPECIFIC (decl) = (struct lang_decl *)
    ggc_alloc_cleared (sizeof (struct lang_decl));
}

/* Remember a symbol to generate initialization/cleanup code at function
   entry/exit.  */

static void
gfc_defer_symbol_init (gfc_symbol * sym)
{
  gfc_symbol *p;
  gfc_symbol *last;
  gfc_symbol *head;

  /* Don't add a symbol twice.  */
  if (sym->tlink)
    return;

  last = head = sym->ns->proc_name;
  p = last->tlink;

  /* Make sure that setup code for dummy variables which are used in the
     setup of other variables is generated first.  */
  if (sym->attr.dummy)
    {
      /* Find the first dummy arg seen after us, or the first non-dummy arg.
         This is a circular list, so don't go past the head.  */
      while (p != head
             && (!p->attr.dummy || p->dummy_order > sym->dummy_order))
        {
          last = p;
          p = p->tlink;
        }
    }
  /* Insert in between last and p.  */
  last->tlink = sym;
  sym->tlink = p;
}


/* Create an array index type variable with function scope.  */

static tree
create_index_var (const char * pfx, int nest)
{
  tree decl;

  decl = gfc_create_var_np (gfc_array_index_type, pfx);
  if (nest)
    gfc_add_decl_to_parent_function (decl);
  else
    gfc_add_decl_to_function (decl);
  return decl;
}


/* Create variables to hold all the non-constant bits of info for a
   descriptorless array.  Remember these in the lang-specific part of the
   type.  */

static void
gfc_build_qualified_array (tree decl, gfc_symbol * sym)
{
  tree type;
  int dim;
  int nest;

  type = TREE_TYPE (decl);

  /* We just use the descriptor, if there is one.  */
  if (GFC_DESCRIPTOR_TYPE_P (type))
    return;

  assert (GFC_ARRAY_TYPE_P (type));
  nest = (sym->ns->proc_name->backend_decl != current_function_decl)
	 && !sym->attr.contained;

  for (dim = 0; dim < GFC_TYPE_ARRAY_RANK (type); dim++)
    {
      if (GFC_TYPE_ARRAY_LBOUND (type, dim) == NULL_TREE)
        GFC_TYPE_ARRAY_LBOUND (type, dim) = create_index_var ("lbound", nest);
      /* Don't try to use the unkown bound for assumed shape arrays.  */
      if (GFC_TYPE_ARRAY_UBOUND (type, dim) == NULL_TREE
          && (sym->as->type != AS_ASSUMED_SIZE
              || dim < GFC_TYPE_ARRAY_RANK (type) - 1))
        GFC_TYPE_ARRAY_UBOUND (type, dim) = create_index_var ("ubound", nest);

      if (GFC_TYPE_ARRAY_STRIDE (type, dim) == NULL_TREE)
        GFC_TYPE_ARRAY_STRIDE (type, dim) = create_index_var ("stride", nest);
    }
  if (GFC_TYPE_ARRAY_OFFSET (type) == NULL_TREE)
    {
      GFC_TYPE_ARRAY_OFFSET (type) = gfc_create_var_np (type, "base");
      if (nest)
	gfc_add_decl_to_parent_function (GFC_TYPE_ARRAY_OFFSET (type));
      else
	gfc_add_decl_to_function (GFC_TYPE_ARRAY_OFFSET (type));
    }
}


/* Get a temporary decl for a dummy array parameter.  */

static tree
gfc_build_dummy_array_decl (gfc_symbol * sym, tree dummy)
{
  tree decl;
  tree type;
  gfc_array_spec *as;
  char *name;
  int packed;
  int n;

  if (sym->attr.pointer || sym->attr.allocatable)
    return dummy;

  /* Add to list of variables if not a fake result variable.  */
  if (sym->attr.result || sym->attr.dummy)
    gfc_defer_symbol_init (sym);

  type = TREE_TYPE (dummy);
  assert (TREE_CODE (dummy) == PARM_DECL
          && POINTER_TYPE_P (type));

  if (!GFC_DESCRIPTOR_TYPE_P (TREE_TYPE (type)))
    {
      assert (GFC_ARRAY_TYPE_P (type));
      gfc_build_qualified_array (dummy, sym);
      return dummy;
    }

  type = TREE_TYPE (type);

  as = sym->as;
  packed = 0;
  if (!gfc_option.flag_repack_arrays)
    {
      if (as->type == AS_ASSUMED_SIZE)
        packed = 2;
    }
  else
    {
      if (as->type == AS_EXPLICIT)
        {
          packed = 2;
          for (n = 0; n < as->rank; n++)
            {
              if (!(as->upper[n]
                    && as->lower[n]
                    && as->upper[n]->expr_type == EXPR_CONSTANT
                    && as->lower[n]->expr_type == EXPR_CONSTANT))
                packed = 1;
            }
        }
      else
        packed = 1;
    }

  type = gfc_get_nodesc_array_type (gfc_get_element_type (type), sym->as,
                                    packed);

  ASM_FORMAT_PRIVATE_NAME (name, IDENTIFIER_POINTER (DECL_NAME (dummy)), 0);
  decl = build_decl (VAR_DECL, get_identifier (name), type);

  DECL_ARTIFICIAL (decl) = 1;
  TREE_PUBLIC (decl) = 0;
  TREE_STATIC (decl) = 0;
  DECL_EXTERNAL (decl) = 0;

  if (sym->as->type == AS_DEFERRED)
    internal_error ("possible gfortran frontend bug: deferred shape dummy array");

  switch (packed)
    {
    case 1:
      GFC_DECL_PARTIAL_PACKED_ARRAY (decl) = 1;
      break;

    case 2:
      GFC_DECL_PACKED_ARRAY (decl) = 1;
      break;
    }

  gfc_build_qualified_array (decl, sym);

  if (DECL_LANG_SPECIFIC (dummy))
    DECL_LANG_SPECIFIC (decl) = DECL_LANG_SPECIFIC (dummy);
  else
    gfc_allocate_lang_decl (decl);

  GFC_DECL_SAVED_DESCRIPTOR (decl) = dummy;
  GFC_DECL_STRING (decl) = GFC_DECL_STRING (dummy);

  if (sym->ns->proc_name->backend_decl == current_function_decl
      || sym->attr.contained)
    gfc_add_decl_to_function (decl);
  else
    gfc_add_decl_to_parent_function (decl);

  return decl;
}


/* Return the decl for a gfc_symbol, create it if it doesn't already
   exist.  */

tree
gfc_get_symbol_decl (gfc_symbol * sym)
{
  tree decl;
  tree length;
  gfc_se se;
  int byref;

  assert (sym->attr.referenced);

  if (sym->ns && sym->ns->proc_name->attr.function)
    byref = gfc_return_by_reference (sym->ns->proc_name);
  else
    byref = 0;

  if ((sym->attr.dummy && ! sym->attr.function) || (sym->attr.result && byref))
    {
      /* Return via extra parameter.  */
      if (sym->attr.result && byref
	  && !sym->backend_decl)
	{
	  sym->backend_decl =
	    DECL_ARGUMENTS (sym->ns->proc_name->backend_decl);
	}

      /* Dummy variables should already have been created.  */
      assert (sym->backend_decl);

      /* Use a copy of the descriptor for dummy arrays.  */
      if (sym->attr.dimension && !TREE_USED (sym->backend_decl))
        {
          sym->backend_decl =
            gfc_build_dummy_array_decl (sym, sym->backend_decl);
	}
      TREE_USED (sym->backend_decl) = 1;
      return sym->backend_decl;
    }

  if (sym->backend_decl)
    return sym->backend_decl;

  if (sym->attr.entry)
    gfc_todo_error ("alternate entry");

  /* Catch function declarations.  Only used for actual parameters.  */
  if (sym->attr.flavor == FL_PROCEDURE)
    {
      decl = gfc_get_extern_function_decl (sym);
      return decl;
    }

  if (sym->attr.intrinsic)
    internal_error ("intrinsic variable which isn't a procedure");

  decl = build_decl (VAR_DECL, gfc_sym_identifier (sym), gfc_sym_type (sym));

  /* Symbols from modules have its assembler name should be mangled.
     This is done here rather than in gfc_finish_var_decl because it
     is different for string length variables.  */
  if (sym->module[0])
    SET_DECL_ASSEMBLER_NAME (decl, gfc_sym_mangled_identifier (sym));

  if (sym->attr.dimension)
    {
      /* Create variables to hold the non-constant bits of array info.  */
      gfc_build_qualified_array (decl, sym);

      /* Remember this variable for allocation/cleanup.  */
      gfc_defer_symbol_init (sym);

      if ((sym->attr.allocatable || !sym->attr.dummy) && !sym->attr.pointer)
	GFC_DECL_PACKED_ARRAY (decl) = 1;
    }

  gfc_finish_var_decl (decl, sym);

  if (sym->attr.assign)
    {
      gfc_allocate_lang_decl (decl);
      GFC_DECL_ASSIGN (decl) = 1;
      GFC_DECL_STRING_LENGTH (decl) =
        gfc_create_var (gfc_strlen_type_node, sym->name);
      GFC_DECL_ASSIGN_ADDR (decl) = gfc_create_var (pvoid_type_node, sym->name);
      TREE_STATIC (GFC_DECL_STRING_LENGTH (decl)) = 1;
      /*  STRING_LENGTH is also used as flag. Less than -1 means that
          ASSIGN_ADDR can not be used. Equal -1 means that ASSIGN_ADDR is the
          target label's address. Other value is the length of format string
          and ASSIGN_ADDR is the address of format string.  */
      DECL_INITIAL (GFC_DECL_STRING_LENGTH (decl)) =
        build_int_2 (-2, -1);
    }

  /* TODO: Initialization of pointer variables.  */
  switch (sym->ts.type)
    {
    case BT_CHARACTER:
      /* Character variables need special handling.  */
      /* Character lengths are common for a whole array.  */

      gfc_allocate_lang_decl (decl);
      GFC_DECL_STRING (decl) = 1;

      if (sym->ts.cl->length->expr_type == EXPR_CONSTANT)
	{
	  length =
	    gfc_conv_mpz_to_tree (sym->ts.cl->length->value.integer, 4);

	  /* Static initializer.  */
	  if (sym->value)
	    {
	      assert (TREE_STATIC (decl));
	      if (sym->attr.pointer)
		gfc_todo_error ("initialization of pointers");
	      DECL_INITIAL (decl) = gfc_conv_string_init (length, sym->value);
	    }
	}
      else
	{
	  char name[GFC_MAX_MANGLED_SYMBOL_LEN + 2];

	  /* Create annother variable to hold the length.  Prefix the name
	     to avoid conflicts.  */
	  strcpy (&name[1], sym->name);
	  name[0] = '.';
	  length = build_decl (VAR_DECL, get_identifier (name),
			       gfc_strlen_type_node);

	  DECL_ARTIFICIAL (decl) = 1;
	  /* Also prefix the mangled name for symbols from modules.  */
	  if (sym->module[0])
	    {
	      strcpy (&name[1],
		      IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (length)));
	      SET_DECL_ASSEMBLER_NAME (decl, get_identifier (name));
	    }
	  gfc_finish_var_decl (length, sym);
	  /* Remember this variable for allocation/cleanup.  */
	  gfc_defer_symbol_init (sym);
	  assert (!sym->value);
	}

      GFC_DECL_STRING_LENGTH (decl) = length;
      break;

    case BT_DERIVED:
      if (sym->value && ! (sym->attr.use_assoc || sym->attr.dimension))
        {
          gfc_init_se (&se, NULL);
          gfc_conv_structure (&se, sym->value, 1);
          DECL_INITIAL (decl) = se.expr;
        }
      break;

    default:
      /* Static initializers for SAVEd variables.  Arrays have already been
         remembered.  Module variables are initialized when the module is
         loaded.  */
      if (sym->value && ! (sym->attr.use_assoc || sym->attr.dimension))
	{
	  assert (TREE_STATIC (decl));
	  gfc_init_se (&se, NULL);
	  gfc_conv_constant (&se, sym->value);
	  DECL_INITIAL (decl) = se.expr;
	}
      break;
    }
  sym->backend_decl = decl;

  return decl;
}


/* Get a basic decl for an external function.  */

tree
gfc_get_extern_function_decl (gfc_symbol * sym)
{
  tree type;
  tree fndecl;
  gfc_expr e;
  gfc_intrinsic_sym *isym;
  gfc_expr argexpr;
  char s[GFC_MAX_SYMBOL_LEN];
  tree name;
  tree mangled_name;

  if (sym->backend_decl)
    return sym->backend_decl;

  if (sym->attr.intrinsic)
    {
      /* Call the resolution function to get the actual name.  This is
         a nasty hack which relies on the resolution functions only looking
	 at the first argument.  We pass NULL for the second argument
	 otherwise things like AINT get confused.  */
      isym = gfc_find_function (sym->name);
      assert (isym->resolve.f0 != NULL);

      memset (&e, 0, sizeof (e));
      e.expr_type = EXPR_FUNCTION;

      memset (&argexpr, 0, sizeof (argexpr));
      assert (isym->formal);
      argexpr.ts = isym->formal->ts;

      if (isym->formal->next == NULL)
	isym->resolve.f1 (&e, &argexpr);
      else
	{
	  /* All specific intrinsics take one or two arguments.  */
	  assert (isym->formal->next->next == NULL);
	  isym->resolve.f2 (&e, &argexpr, NULL);
	}
      sprintf (s, "specific%s", e.value.function.name);
      name = get_identifier (s);
      mangled_name = name;
    }
  else
    {
      name = gfc_sym_identifier (sym);
      mangled_name = gfc_sym_mangled_function_id (sym);
    }

  type = gfc_get_function_type (sym);
  fndecl = build_decl (FUNCTION_DECL, name, type);

  SET_DECL_ASSEMBLER_NAME (fndecl, mangled_name);
  /* If the return type is a pointer, avoid alias issues by setting
     DECL_IS_MALLOC to nonzero. This means that the function should be
     treated as if it were a malloc, meaning it returns a pointer that
     is not an alias.  */
  if (POINTER_TYPE_P (type))
    DECL_IS_MALLOC (fndecl) = 1;

  /* Set the context of this decl.  */
  if (sym->ns && sym->ns->proc_name)
    {
      /* TODO: Add external decls to the appropriate scope.  */
      DECL_CONTEXT (fndecl) = sym->ns->proc_name->backend_decl;
    }
  else
    {
      /* Global declaration, eg. intrinsic subroutine.  */
      DECL_CONTEXT (fndecl) = NULL_TREE;
    }

  DECL_EXTERNAL (fndecl) = 1;

  /* This specifies if a function is globaly addressable, ie. it is
     the opposite of declaring static in C.  */
  TREE_PUBLIC (fndecl) = 1;

  /* Set attributes for PURE functions. A call to PURE function in the
     Fortran 95 sense is both pure and without side effects in the C
     sense.  */
  if (sym->attr.pure || sym->attr.elemental)
    {
      DECL_IS_PURE (fndecl) = 1;
/* TODO: check if pure/elemental procedures can have INTENT(OUT) parameters.
      TREE_SIDE_EFFECTS (fndecl) = 0;*/
    }

  sym->backend_decl = fndecl;

  if (DECL_CONTEXT (fndecl) == NULL_TREE)
    pushdecl_top_level (fndecl);

  return fndecl;
}


/* Create a declaration for a procedure.  For external functions (in the C
   sense) use gfc_get_extern_function_decl.  */

void
gfc_build_function_decl (gfc_symbol * sym)
{
  tree fndecl, type, result_decl, typelist, arglist;
  tree length;
  symbol_attribute attr;
  gfc_formal_arglist *f;

  assert (!sym->backend_decl);
  assert (!sym->attr.external);

  /* Allow only one nesting level.  Allow external declarations.  */
  assert (current_function_decl == NULL_TREE
	  || DECL_CONTEXT (current_function_decl) == NULL_TREE);

  type = gfc_get_function_type (sym);
  fndecl = build_decl (FUNCTION_DECL, gfc_sym_identifier (sym), type);

  /* Perform name mangling if this is a top level or module procedure.  */
  if (current_function_decl == NULL_TREE)
    SET_DECL_ASSEMBLER_NAME (fndecl, gfc_sym_mangled_function_id (sym));

  /* Figure out the return type of the declared function, and build a
     RESULT_DECL for it.  If this is subroutine with alternate
     returns, build a RESULT_DECL for it.  */
  attr = sym->attr;

  result_decl = NULL_TREE;
  if (attr.function)
    {
      if (gfc_return_by_reference (sym))
	type = void_type_node;
      else
	{
	  if (sym->result != sym)
	    result_decl = gfc_sym_identifier (sym->result);

	  type = TREE_TYPE (TREE_TYPE (fndecl));
	}
    }
  else
    {
      /* Look for an alternate return placeholders.  */
      int has_alternate_returns = 0;
      for (f = sym->formal; f; f = f->next)
	{
	  if (f->sym == NULL)
	    {
	      has_alternate_returns = 1;
	      break;
	    }
	}

      if (has_alternate_returns)
	type = integer_type_node;
      else
	type = void_type_node;
    }

  result_decl = build_decl (RESULT_DECL, result_decl, type);
  DECL_CONTEXT (result_decl) = fndecl;
  DECL_RESULT (fndecl) = result_decl;

  /* Don't call layout_decl for a RESULT_DECL.
     layout_decl (result_decl, 0); */

  /* If the return type is a pointer, avoid alias issues by setting
     DECL_IS_MALLOC to nonzero. This means that the function should be
     treated as if it were a malloc, meaning it returns a pointer that
     is not an alias.  */
  if (POINTER_TYPE_P (type))
    DECL_IS_MALLOC (fndecl) = 1;

  /* Set up all attributes for the function.  */
  DECL_CONTEXT (fndecl) = current_function_decl;
  DECL_EXTERNAL (fndecl) = 0;

  /* This specifies if a function is globaly addressable, ie. it is
     the opposite of decalring static  in C.  */
  if (DECL_CONTEXT (fndecl) == NULL_TREE || attr.external)
    TREE_PUBLIC (fndecl) = 1;

  /* TREE_STATIC means the function body is defined here.  */
  if (!attr.external)
    TREE_STATIC (fndecl) = 1;

  /* Set attributes for PURE functions. A call to PURE function in the
     Fortran 95 sense is both pure and without side effects in the C
     sense.  */
  if (attr.pure || attr.elemental)
    {
      DECL_IS_PURE (fndecl) = 1;
      TREE_SIDE_EFFECTS (fndecl) = 0;
    }

  /* Layout the function declaration and put it in the binding level
     of the current function.  */
  if (!attr.external)
    {
      tree parm;

      pushdecl (fndecl);
      /* Build formal argument list. Make sure that their TREE_CONTEXT is
         the new FUNCTION_DECL node.  */
      current_function_decl = fndecl;
      arglist = NULL_TREE;
      typelist = TYPE_ARG_TYPES (TREE_TYPE (fndecl));
      if (gfc_return_by_reference (sym))
	{
	  type = TREE_VALUE (typelist);
	  parm = build_decl (PARM_DECL, get_identifier ("__result"), type);

	  DECL_CONTEXT (parm) = fndecl;
	  DECL_ARG_TYPE (parm) = type;
	  TREE_READONLY (parm) = 1;
	  if (sym->ts.type == BT_CHARACTER)
	    {
	      gfc_allocate_lang_decl (parm);
	      GFC_DECL_STRING (parm) = 1;

	      assert (sym->ts.cl && sym->ts.cl->length
		      && sym->ts.cl->length->expr_type == EXPR_CONSTANT);
	      GFC_DECL_STRING_LENGTH (parm) =
		gfc_conv_mpz_to_tree (sym->ts.cl->length->value.integer, 4);

	    }
	  gfc_finish_decl (parm, NULL_TREE);

	  arglist = chainon (arglist, parm);
	  typelist = TREE_CHAIN (typelist);
	}

      for (f = sym->formal; f; f = f->next)
	{
	  if (f->sym != NULL)	/* ignore alternate returns. */
	    {
	      length = NULL_TREE;

	      type = TREE_VALUE (typelist);

	      /* Build a the argument declaration.  */
	      parm = build_decl (PARM_DECL,
				 gfc_sym_identifier (f->sym), type);

	      /* Fill in arg stuff.  */
	      DECL_CONTEXT (parm) = fndecl;
	      DECL_ARG_TYPE (parm) = type;
	      DECL_ARG_TYPE_AS_WRITTEN (parm) = type;
	      /* All implementation args are read-only.  */
	      TREE_READONLY (parm) = 1;

	      gfc_finish_decl (parm, NULL_TREE);

	      f->sym->backend_decl = parm;

	      arglist = chainon (arglist, parm);
	      typelist = TREE_CHAIN (typelist);
	    }
        }

      /* Add the hidden string length parameters.  */
      parm = arglist;
      for (f = sym->formal; f; f = f->next)
	{
          char name[GFC_MAX_SYMBOL_LEN + 2];
	  /* Ignore alternate returns.  */
	  if (f->sym == NULL)
            continue;

          if (f->sym->ts.type != BT_CHARACTER)
            continue;

          parm = f->sym->backend_decl;
          type = TREE_VALUE (typelist);
          assert (type == gfc_strlen_type_node);

          strcpy (&name[1], f->sym->name);
          name[0] = '.';
          length = build_decl (PARM_DECL, get_identifier (name), type);

          arglist = chainon (arglist, length);
          DECL_CONTEXT (length) = fndecl;
          DECL_ARG_TYPE (length) = type;
          TREE_READONLY (length) = 1;
          gfc_finish_decl (length, NULL_TREE);

          gfc_allocate_lang_decl (parm);
          GFC_DECL_STRING (parm) = 1;
          if (f->sym->ts.cl
              && f->sym->ts.cl->length
              && f->sym->ts.cl->length->expr_type == EXPR_CONSTANT)
            {
              length = gfc_conv_mpz_to_tree (
                  f->sym->ts.cl->length->value.integer, 4);
            }
          else
            TREE_USED (length) = 1;
          GFC_DECL_STRING_LENGTH (parm) = length;

          parm = TREE_CHAIN (parm);
          typelist = TREE_CHAIN (typelist);
	}

      assert (TREE_VALUE (typelist) == void_type_node);
      DECL_ARGUMENTS (fndecl) = arglist;

      /* Restore the old context.  */
      current_function_decl = DECL_CONTEXT (fndecl);
    }
  sym->backend_decl = fndecl;
}


/* Return the decl used to hold the function return value.  */

tree
gfc_get_fake_result_decl (gfc_symbol * sym)
{
  tree decl;
  char name[GFC_MAX_SYMBOL_LEN + 10];

  if (current_fake_result_decl != NULL_TREE)
    return current_fake_result_decl;

  /* Only when gfc_get_fake_result_decl is called by gfc_trans_return,
     sym is NULL.  */
  if (!sym)
    return NULL_TREE;

  if (gfc_return_by_reference (sym))
    {
      decl = DECL_ARGUMENTS (sym->backend_decl);

      TREE_USED (decl) = 1;
      if (sym->as)
	decl = gfc_build_dummy_array_decl (sym, decl);
    }
  else
    {
      sprintf (name, "__result_%.20s",
	       IDENTIFIER_POINTER (DECL_NAME (current_function_decl)));

      decl = build_decl (VAR_DECL, get_identifier (name),
			 TREE_TYPE (TREE_TYPE (current_function_decl)));

      DECL_ARTIFICIAL (decl) = 1;
      DECL_EXTERNAL (decl) = 0;
      TREE_PUBLIC (decl) = 0;
      TREE_USED (decl) = 1;

      layout_decl (decl, 0);

      gfc_add_decl_to_function (decl);
    }

  current_fake_result_decl = decl;

  return decl;
}


/* Builds a function decl.  The remaining parameters are the types of the
   function arguments.  Negative nargs indicates a varargs function.  */

tree
gfc_build_library_function_decl (tree name, tree rettype, int nargs, ...)
{
  tree arglist;
  tree argtype;
  tree fntype;
  tree fndecl;
  va_list p;
  int n;

  /* Library functions must be declared with global scope.  */
  assert (current_function_decl == NULL_TREE);

  va_start (p, nargs);


  /* Create a list of the argument types.  */
  for (arglist = NULL_TREE, n = abs (nargs); n > 0; n--)
    {
      argtype = va_arg (p, tree);
      arglist = gfc_chainon_list (arglist, argtype);
    }

  if (nargs >= 0)
    {
      /* Terminate the list.  */
      arglist = gfc_chainon_list (arglist, void_type_node);
    }

  /* Build the function type and decl.  */
  fntype = build_function_type (rettype, arglist);
  fndecl = build_decl (FUNCTION_DECL, name, fntype);

  /* Mark this decl as external.  */
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  va_end (p);

  pushdecl (fndecl);

  rest_of_decl_compilation (fndecl, NULL, 1, 0);

  return fndecl;
}

static void
gfc_build_intrinsic_function_decls (void)
{
  /* String functions.  */
  gfor_fndecl_copy_string =
    gfc_build_library_function_decl (get_identifier (PREFIX("copy_string")),
				     void_type_node,
				     4,
				     gfc_strlen_type_node, pchar_type_node,
				     gfc_strlen_type_node, pchar_type_node);

  gfor_fndecl_compare_string =
    gfc_build_library_function_decl (get_identifier (PREFIX("compare_string")),
				     gfc_int4_type_node,
				     4,
				     gfc_strlen_type_node, pchar_type_node,
				     gfc_strlen_type_node, pchar_type_node);

  gfor_fndecl_concat_string =
    gfc_build_library_function_decl (get_identifier (PREFIX("concat_string")),
				     void_type_node,
				     6,
				     gfc_strlen_type_node, pchar_type_node,
				     gfc_strlen_type_node, pchar_type_node,
				     gfc_strlen_type_node, pchar_type_node);

  gfor_fndecl_string_len_trim =
    gfc_build_library_function_decl (get_identifier (PREFIX("string_len_trim")),
				     gfc_int4_type_node,
				     2, gfc_strlen_type_node,
				     pchar_type_node);

  gfor_fndecl_string_index =
    gfc_build_library_function_decl (get_identifier (PREFIX("string_index")),
				     gfc_int4_type_node,
				     5, gfc_strlen_type_node, pchar_type_node,
				     gfc_strlen_type_node, pchar_type_node,
                                     gfc_logical4_type_node);

  gfor_fndecl_string_scan =
    gfc_build_library_function_decl (get_identifier (PREFIX("string_scan")),
                                     gfc_int4_type_node,
                                     5, gfc_strlen_type_node, pchar_type_node,
                                     gfc_strlen_type_node, pchar_type_node,
                                     gfc_logical4_type_node);

  gfor_fndecl_string_verify =
    gfc_build_library_function_decl (get_identifier (PREFIX("string_verify")),
                                     gfc_int4_type_node,
                                     5, gfc_strlen_type_node, pchar_type_node,
                                     gfc_strlen_type_node, pchar_type_node,
                                     gfc_logical4_type_node);

  gfor_fndecl_string_trim = 
    gfc_build_library_function_decl (get_identifier (PREFIX("string_trim")),
                                     void_type_node,
                                     4,
                                     build_pointer_type (gfc_strlen_type_node),
                                     ppvoid_type_node,
                                     gfc_strlen_type_node,
                                     pchar_type_node);

  gfor_fndecl_string_repeat =
    gfc_build_library_function_decl (get_identifier (PREFIX("string_repeat")),
                                     void_type_node,
                                     4,
                                     pchar_type_node,
                                     gfc_strlen_type_node,
                                     pchar_type_node,
                                     gfc_int4_type_node);

  gfor_fndecl_adjustl =
    gfc_build_library_function_decl (get_identifier (PREFIX("adjustl")),
				     void_type_node,
				     3,
				     pchar_type_node,
				     gfc_strlen_type_node, pchar_type_node);

  gfor_fndecl_adjustr =
    gfc_build_library_function_decl (get_identifier (PREFIX("adjustr")),
				     void_type_node,
				     3,
				     pchar_type_node,
				     gfc_strlen_type_node, pchar_type_node);

  gfor_fndecl_si_kind =
    gfc_build_library_function_decl (get_identifier ("selected_int_kind"),
                                     gfc_int4_type_node,
                                     1,
                                     pvoid_type_node);

  gfor_fndecl_sr_kind =
    gfc_build_library_function_decl (get_identifier ("selected_real_kind"),
                                     gfc_int4_type_node,
                                     2, pvoid_type_node,
                                     pvoid_type_node);


  /* Power functions.  */
  gfor_fndecl_math_powf =
    gfc_build_library_function_decl (get_identifier ("powf"),
				     gfc_real4_type_node,
				     1, gfc_real4_type_node);
  gfor_fndecl_math_pow =
    gfc_build_library_function_decl (get_identifier ("pow"),
				     gfc_real8_type_node,
				     1, gfc_real8_type_node);
  gfor_fndecl_math_cpowf =
    gfc_build_library_function_decl (get_identifier ("cpowf"),
				     gfc_complex4_type_node,
				     1, gfc_complex4_type_node);
  gfor_fndecl_math_cpow =
    gfc_build_library_function_decl (get_identifier ("cpow"),
				     gfc_complex8_type_node,
				     1, gfc_complex8_type_node);
  gfor_fndecl_math_cabsf =
    gfc_build_library_function_decl (get_identifier ("cabsf"),
				     gfc_real4_type_node,
				     1, gfc_complex4_type_node);
  gfor_fndecl_math_cabs =
    gfc_build_library_function_decl (get_identifier ("cabs"),
				     gfc_real8_type_node,
				     1, gfc_complex8_type_node);
  gfor_fndecl_math_sign4 =
    gfc_build_library_function_decl (get_identifier ("copysignf"),
				     gfc_real4_type_node,
				     1, gfc_real4_type_node);
  gfor_fndecl_math_sign8 =
    gfc_build_library_function_decl (get_identifier ("copysign"),
				     gfc_real8_type_node,
				     1, gfc_real8_type_node);
  gfor_fndecl_math_ishftc4 =
    gfc_build_library_function_decl (get_identifier (PREFIX("ishftc4")),
				     gfc_int4_type_node,
				     3, gfc_int4_type_node,
				     gfc_int4_type_node, gfc_int4_type_node);
  gfor_fndecl_math_ishftc8 =
    gfc_build_library_function_decl (get_identifier (PREFIX("ishftc8")),
				     gfc_int8_type_node,
				     3, gfc_int8_type_node,
				     gfc_int8_type_node, gfc_int8_type_node);
  gfor_fndecl_math_exponent4 =
    gfc_build_library_function_decl (get_identifier (PREFIX("exponent_r4")),
				     gfc_int4_type_node,
				     1, gfc_real4_type_node);
  gfor_fndecl_math_exponent8 =
    gfc_build_library_function_decl (get_identifier (PREFIX("exponent_r8")),
				     gfc_int4_type_node,
				     1, gfc_real8_type_node);

  /* Other functions.  */
  gfor_fndecl_size0 =
    gfc_build_library_function_decl (get_identifier (PREFIX("size0")),
				     gfc_array_index_type,
				     1, pvoid_type_node);
  gfor_fndecl_size1 =
    gfc_build_library_function_decl (get_identifier (PREFIX("size1")),
				     gfc_array_index_type,
				     2, pvoid_type_node,
				     gfc_array_index_type);
}


/* Make prototypes for runtime library functions.  */

void
gfc_build_builtin_function_decls (void)
{
  gfor_fndecl_internal_malloc =
    gfc_build_library_function_decl (get_identifier (PREFIX("internal_malloc")),
				     pvoid_type_node, 1, gfc_int4_type_node);

  gfor_fndecl_internal_malloc64 =
    gfc_build_library_function_decl (get_identifier
				     (PREFIX("internal_malloc64")),
				     pvoid_type_node, 1, gfc_int8_type_node);

  gfor_fndecl_internal_free =
    gfc_build_library_function_decl (get_identifier (PREFIX("internal_free")),
				     void_type_node, 1, pvoid_type_node);

  gfor_fndecl_allocate =
    gfc_build_library_function_decl (get_identifier (PREFIX("allocate")),
				     void_type_node, 2, ppvoid_type_node,
				     gfc_int4_type_node);

  gfor_fndecl_allocate64 =
    gfc_build_library_function_decl (get_identifier (PREFIX("allocate64")),
				     void_type_node, 2, ppvoid_type_node,
				     gfc_int8_type_node);

  gfor_fndecl_deallocate =
    gfc_build_library_function_decl (get_identifier (PREFIX("deallocate")),
				     void_type_node, 1, ppvoid_type_node);

  gfor_fndecl_stop_numeric =
    gfc_build_library_function_decl (get_identifier (PREFIX("stop_numeric")),
				     void_type_node, 1, gfc_int4_type_node);

  gfor_fndecl_stop_string =
    gfc_build_library_function_decl (get_identifier (PREFIX("stop_string")),
				     void_type_node, 2, pchar_type_node,
                                     gfc_int4_type_node);

  gfor_fndecl_pause_numeric =
    gfc_build_library_function_decl (get_identifier (PREFIX("pause_numeric")),
				     void_type_node, 1, gfc_int4_type_node);

  gfor_fndecl_pause_string =
    gfc_build_library_function_decl (get_identifier (PREFIX("pause_string")),
				     void_type_node, 2, pchar_type_node,
                                     gfc_int4_type_node);

  gfor_fndecl_select_string =
    gfc_build_library_function_decl (get_identifier (PREFIX("select_string")),
                                     pvoid_type_node, 0);

  gfor_fndecl_runtime_error =
    gfc_build_library_function_decl (get_identifier (PREFIX("runtime_error")),
				     void_type_node,
				     3,
				     pchar_type_node, pchar_type_node,
				     gfc_int4_type_node);

  gfor_fndecl_in_pack = gfc_build_library_function_decl (
        get_identifier (PREFIX("internal_pack")),
        pvoid_type_node, 1, pvoid_type_node);

  gfor_fndecl_in_unpack = gfc_build_library_function_decl (
        get_identifier (PREFIX("internal_unpack")),
        pvoid_type_node, 1, pvoid_type_node);

  gfor_fndecl_associated =
    gfc_build_library_function_decl (
                                     get_identifier (PREFIX("associated")),
                                     gfc_logical4_type_node,
                                     2,
                                     ppvoid_type_node,
                                     ppvoid_type_node);

  gfc_build_intrinsic_function_decls ();
  gfc_build_intrinsic_lib_fndecls ();
  gfc_build_io_library_fndecls ();
}


/* Allocate and cleanup an automatic character variable.  */

static tree
gfc_trans_auto_character_variable (gfc_symbol * sym, tree fnbody)
{
  tree tmp;
  tree args;
  tree len;
  stmtblock_t block;
  stmtblock_t body;

  assert (sym->ts.cl && sym->ts.cl->length);
  assert (sym->backend_decl != NULL_TREE);

  gfc_start_block (&body);
  gfc_start_block (&block);

  len = gfc_conv_init_string_length (sym, &block);
  args = gfc_chainon_list (NULL_TREE, len);
  tmp = gfc_build_function_call (gfor_fndecl_internal_malloc, args);
  tmp = convert (TREE_TYPE (sym->backend_decl), tmp);
  gfc_add_modify_expr (&block, sym->backend_decl, tmp);

  tmp = gfc_finish_block (&block);
  gfc_add_expr_to_block (&body, tmp);

  gfc_add_expr_to_block (&body, fnbody);

  gfc_start_block (&block);

  tmp = convert (pvoid_type_node, sym->backend_decl);
  args = gfc_chainon_list (NULL_TREE, tmp);
  tmp = gfc_build_function_call (gfor_fndecl_internal_free, args);
  gfc_add_expr_to_block (&block, tmp);

  tmp = gfc_finish_block (&block);
  gfc_add_expr_to_block (&body, tmp);

  return gfc_finish_block (&body);
}


/* Generate function entry and exit code, and add it to the function body.
   This includes:
    Allocation and initialisation of array variables.
    Allocation of character string variables.
    Initialization and possibly repacking of dummy arrays.  */

static tree
gfc_trans_deferred_vars (gfc_symbol * proc_sym, tree fnbody)
{
  locus loc;
  gfc_symbol *sym;

  /* Deal with implicit return variables.  Explicit return variables will
     already have been added.  */
  if (gfc_return_by_reference (proc_sym) && proc_sym->result == proc_sym)
    {
      if (!current_fake_result_decl)
	{
	  warning ("Function does not return a value");
	  return fnbody;
	}

      if (proc_sym->as)
	{
	  fnbody = gfc_trans_dummy_array_bias (proc_sym,
					       current_fake_result_decl,
					       fnbody);
	}
      else if (proc_sym->ts.type != BT_CHARACTER)
	gfc_todo_error ("Deferred non-array return by reference");
    }

  for (sym = proc_sym->tlink; sym != proc_sym; sym = sym->tlink)
    {
      /* For now this is only array variables, but may get extended to
         derived types.  */
      if (sym->attr.dimension)
	{
	  switch (sym->as->type)
	    {
	    case AS_EXPLICIT:
	      if (sym->attr.dummy || sym->attr.result)
		fnbody =
		  gfc_trans_dummy_array_bias (sym, sym->backend_decl, fnbody);
	      else if (sym->attr.pointer || sym->attr.allocatable)
		{
		  if (TREE_STATIC (sym->backend_decl))
		    gfc_trans_static_array_pointer (sym);
		  else
		    fnbody = gfc_trans_deferred_array (sym, fnbody);
		}
	      else
		{
		  gfc_get_backend_locus (&loc);
		  gfc_set_backend_locus (&sym->declared_at);
		  fnbody = gfc_trans_auto_array_allocation (sym->backend_decl,
		      sym, fnbody);
		  gfc_set_backend_locus (&loc);
		}
	      break;

	    case AS_ASSUMED_SIZE:
	      /* Must be a dummy parameter.  */
	      assert (sym->attr.dummy);

	      /* We should always pass assumed size arrays the g77 way.  */
	      assert (TREE_CODE (sym->backend_decl) == PARM_DECL);
	      fnbody = gfc_trans_g77_array (sym, fnbody);
              break;

	    case AS_ASSUMED_SHAPE:
	      /* Must be a dummy parameter.  */
	      assert (sym->attr.dummy);

	      fnbody = gfc_trans_dummy_array_bias (sym, sym->backend_decl,
						   fnbody);
	      break;

	    case AS_DEFERRED:
	      fnbody = gfc_trans_deferred_array (sym, fnbody);
	      break;

	    default:
	      abort ();
	    }
	}
      else if (sym->ts.type == BT_CHARACTER)
	{
	  gfc_get_backend_locus (&loc);
	  gfc_set_backend_locus (&sym->declared_at);
	  fnbody = gfc_trans_auto_character_variable (sym, fnbody);
	  gfc_set_backend_locus (&loc);
	}
      else
	abort ();
    }

  return fnbody;
}


/* Output an initialized decl for a module variable.  */

static void
gfc_create_module_variable (gfc_symbol * sym)
{
  tree decl;
  gfc_se se;

  /* Only output symbols from this module.  */
  if (sym->ns != module_namespace)
    {
      /* I don't think this should ever happen.  */
      internal_error ("module symbol %s in wrong namespace", sym->name);
    }

  /* Don't ouptut symbols from common blocks.  */
  if (sym->attr.common)
    return;

  /* Only output variables and array valued parametes.  */
  if (sym->attr.flavor != FL_VARIABLE
      && (sym->attr.flavor != FL_PARAMETER || sym->attr.dimension == 0))
    return;

  /* Don't generate variables from other modules.  */
  if (sym->attr.use_assoc)
    return;

  if (sym->backend_decl)
    internal_error ("backend decl for module variable %s already exists",
		    sym->name);

  /* We always want module variables to be created.  */
  sym->attr.referenced = 1;
  /* Create the decl.  */
  decl = gfc_get_symbol_decl (sym);

  /* We want to allocate storage for this variable.  */
  TREE_STATIC (decl) = 1;

  if (sym->attr.dimension)
    {
      assert (sym->attr.pointer || sym->attr.allocatable
	      || GFC_ARRAY_TYPE_P (TREE_TYPE (sym->backend_decl)));
      if (sym->attr.pointer || sym->attr.allocatable)
	gfc_trans_static_array_pointer (sym);
      else
	gfc_trans_auto_array_allocation (sym->backend_decl, sym, NULL_TREE);
    }
  else if (sym->ts.type == BT_DERIVED)
    {
      if (sym->value)
	gfc_todo_error ("Initialization of derived type module variables");
    }
  else
    {
      if (sym->value)
	{
	  gfc_init_se (&se, NULL);
	  gfc_conv_constant (&se, sym->value);
	  DECL_INITIAL (decl) = se.expr;
	}
    }

  /* Create the variable.  */
  pushdecl (decl);
  rest_of_decl_compilation (decl, NULL, 1, 0);

  /* Also add length of strings.  */
  if (GFC_DECL_STRING (decl))
    {
      tree length;

      length = GFC_DECL_STRING_LENGTH (decl);
      if (!INTEGER_CST_P (length))
        {
          pushdecl (length);
          rest_of_decl_compilation (length, NULL, 1, 0);
        }
    }
}


/* Generate all the required code for module variables.  */

void
gfc_generate_module_vars (gfc_namespace * ns)
{
  module_namespace = ns;

  /* Check the frontend left the namespace in a reasonable state.  */
  assert (ns->proc_name && !ns->proc_name->tlink);

  /* Create decls for all the module varuiables.  */
  gfc_traverse_ns (ns, gfc_create_module_variable);
}

static void
gfc_generate_contained_functions (gfc_namespace * parent)
{
  gfc_namespace *ns;

  /* We create all the prototypes before generating any code.  */
  for (ns = parent->contained; ns; ns = ns->sibling)
    {
      /* Skip namespaces from used modules.  */
      if (ns->parent != parent)
	continue;

      gfc_build_function_decl (ns->proc_name);
    }

  for (ns = parent->contained; ns; ns = ns->sibling)
    {
      /* Skip namespaces from used modules.  */
      if (ns->parent != parent)
	continue;

      gfc_generate_function_code (ns);
    }
}


/* Generate decls for all local variables.  We do this to ensure correct
   handling of expressions which only appear in the specification of
   other functions.  */

static void
generate_local_decl (gfc_symbol * sym)
{
  if (sym->attr.flavor == FL_VARIABLE)
    {
      /* TODO: The frontend sometimes creates symbols for things which don't
         actually exist.  E.g. common block names and the names of formal
	 arguments.  The latter are created while attempting to parse
	 the argument list as a substring reference.

	 The proper fix is to avoid adding these symbols in the first place.
	 For now we hack round it by ignoring anything with an unknown type.
       */
      if (sym->ts.type == BT_UNKNOWN)
	return;

      if (sym->attr.referenced)
        gfc_get_symbol_decl (sym);
      else if (sym->attr.dummy)
        {
          if (warn_unused_parameter)
            warning ("unused parameter `%s'", sym->name);
        }
      else if (warn_unused_variable)
        warning ("unused variable `%s'", sym->name);
    }
}

static void
generate_local_vars (gfc_namespace * ns)
{
  gfc_traverse_ns (ns, generate_local_decl);
}


/* Generate code for a function.  */

void
gfc_generate_function_code (gfc_namespace * ns)
{
  tree fndecl;
  tree old_context;
  tree decl;
  tree tmp;
  stmtblock_t block;
  stmtblock_t body;
  tree result;
  gfc_symbol *sym;

  /* Create the declaration for functions with global scope.  */
  if (!ns->proc_name->backend_decl)
    gfc_build_function_decl (ns->proc_name);

  old_context = current_function_decl;

  if (old_context)
    {
      push_function_context ();
      saved_parent_function_decls = saved_function_decls;
      saved_function_decls = NULL_TREE;
    }

  sym = ns->proc_name;

  fndecl = sym->backend_decl;

  /* let GCC know the current scope is this function */
  current_function_decl = fndecl;

  /* print function name on the console at compile time
     (unless this feature was switched of by command line option "-quiet" */
  announce_function (fndecl);

  if (DECL_CONTEXT (fndecl) == NULL_TREE)
    {
      /* create RTL for function declaration */
      rest_of_decl_compilation (fndecl, NULL, 1, 0);
    }

  /* create RTL for function definition */
  make_decl_rtl (fndecl, NULL);

  /* Set the line and filename.  sym->decalred_at seems to point to the last
     statement for subroutines, but it'll do for now.  */
  gfc_set_backend_locus (&sym->declared_at);

  /* line and file should not be 0 */
  init_function_start (fndecl);

  /* We're in function-at-a-time mode. */
  cfun->x_whole_function_mode_p = 1;

  /* Even though we're inside a function body, we still don't want to
     call expand_expr to calculate the size of a variable-sized array.
     We haven't necessarily assigned RTL to all variables yet, so it's
     not safe to try to expand expressions involving them.  */
  immediate_size_expand = 0;
  cfun->x_dont_save_pending_sizes_p = 1;

  /* Will be created as needed.  */
  current_fake_result_decl = NULL_TREE;

  /* function.c requires a push at the start of the function */
  pushlevel (0);

  /* Check that the frontend isn't still using this.  */
  assert (sym->tlink == NULL);
  sym->tlink = sym;

  gfc_start_block (&block);

  gfc_generate_contained_functions (ns);

  /* Translate COMMON blocks.  */
  gfc_trans_common (ns);

  generate_local_vars (ns);

  current_function_return_label = NULL;

  /* Now generate the code for the body of this function.  */
  gfc_init_block (&body);

  if (TREE_TYPE (DECL_RESULT (fndecl)) != void_type_node
      && sym->attr.subroutine)
    {
      tree alternate_return;
      alternate_return = gfc_get_fake_result_decl (sym);
      gfc_add_modify_expr (&body, alternate_return, integer_zero_node);
    }

  tmp = gfc_trans_code (ns->code);
  gfc_add_expr_to_block (&body, tmp);

  /* Add a return label if needed.  */
  if (current_function_return_label)
    {
      tmp = build1_v (LABEL_EXPR, current_function_return_label);
      gfc_add_expr_to_block (&body, tmp);
    }

  tmp = gfc_finish_block (&body);
  /* Add code to create and cleanup arrays.  */
  tmp = gfc_trans_deferred_vars (sym, tmp);
  gfc_add_expr_to_block (&block, tmp);

  if (TREE_TYPE (DECL_RESULT (fndecl)) != void_type_node)
    {
      if (sym->attr.subroutine ||sym == sym->result)
	{
	  result = current_fake_result_decl;
	  current_fake_result_decl = NULL_TREE;
	}
      else
	result = sym->result->backend_decl;

      if (result == NULL_TREE)
	warning ("Function return value not set");
      else
	{
	  /* Set the return value to the the dummy result variable.  */
	  tmp = build (MODIFY_EXPR, TREE_TYPE (result),
		       DECL_RESULT (fndecl), result);
	  tmp = build_v (RETURN_EXPR, tmp);
	  gfc_add_expr_to_block (&block, tmp);
	}
    }

  /* Add all the decls we created during processing.  */
  decl = saved_function_decls;
  while (decl)
    {
      tree next;

      next = TREE_CHAIN (decl);
      TREE_CHAIN (decl) = NULL_TREE;
      pushdecl (decl);
      decl = next;
    }
  saved_function_decls = NULL_TREE;

  DECL_SAVED_TREE (fndecl) = gfc_finish_block (&block);

  /* Finish off this function and send it for code generation.  */
  poplevel (1, 0, 1);
  BLOCK_SUPERCONTEXT (DECL_INITIAL (fndecl)) = fndecl;

  /* Output the SIMPLE tree.  */
  dump_function (TDI_original, fndecl);

  /* Store the end of the function, so that we get good line number
     info for the epilogue.  */
  cfun->function_end_locus = input_location;

  /* We're leaving the context of this function, so zap cfun.  It's still in
     DECL_SAVED_INSNS, and we'll restore it in tree_rest_of_compilation.  */
  cfun = NULL;

  if (old_context)
    {
      pop_function_context ();
      saved_function_decls = saved_parent_function_decls;
    }
  else
    {
      /* Pass the function to the backend.  */
      expand_function_body (fndecl, 0);
    }

  current_function_decl = old_context;
}


void
gfc_generate_constructors (void)
{
  if (gfc_static_ctors != NULL_TREE)
    abort ();
#if 0
  tree fnname;
  tree type;
  tree fndecl;
  tree decl;
  tree tmp;

  if (gfc_static_ctors == NULL_TREE)
    return;

  fnname = get_file_function_name ('I');
  type = build_function_type (void_type_node,
			      gfc_chainon_list (NULL_TREE, void_type_node));

  fndecl = build_decl (FUNCTION_DECL, fnname, type);
  TREE_PUBLIC (fndecl) = 1;

  decl = build_decl (RESULT_DECL, NULL_TREE, void_type_node);
  DECL_CONTEXT (decl) = fndecl;
  DECL_RESULT (fndecl) = decl;

  pushdecl (fndecl);

  current_function_decl = fndecl;

  rest_of_decl_compilation (fndecl, NULL, 1, 0);

  make_decl_rtl (fndecl, NULL);

  init_function_start (fndecl, input_filename, input_line);

  cfun->x_whole_function_mode_p = 1;

  immediate_size_expand = 0;

  pushlevel (0);

  for (; gfc_static_ctors; gfc_static_ctors = TREE_CHAIN (gfc_static_ctors))
    {
      tmp =
	gfc_build_function_call (TREE_VALUE (gfc_static_ctors), NULL_TREE);
      DECL_SAVED_TREE (fndecl) = build_stmt (EXPR_STMT, tmp);
    }

  poplevel (1, 0, 1);

  BLOCK_SUPERCONTEXT (DECL_INITIAL (fndecl)) = fndecl;

  free_after_parsing (cfun);
  free_after_compilation (cfun);

  expand_function_body (fndecl, 0);

  current_function_decl = NULL_TREE;
#endif
}

#include "gt-fortran-trans-decl.h"
