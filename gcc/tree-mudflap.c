/* Mudflap: narrow-pointer bounds-checking by tree rewriting.
   Copyright (C) 2001, 2002 Free Software Foundation, Inc.
   Contributed by Frank Ch. Eigler <fche@redhat.com>
   and Graydon Hoare <graydon@redhat.com>

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


#include "config.h"
#include "errors.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tree-inline.h"
#include "c-tree.h"
#include "c-common.h"
#include "tree-simple.h"
#include "diagnostic.h"
#include "hashtab.h"
#include "output.h"
#include "varray.h"
#include "langhooks.h"
#include "tree-mudflap.h"
#include "ggc.h"
#include "target.h"
#include "flags.h"
#include "rtl.h"
#include "toplev.h"
#include "function.h"


/* Internal function decls */

static void mf_xform_derefs PARAMS ((tree));
static void mf_xform_decls PARAMS ((tree, tree));
static void mf_init_extern_trees PARAMS ((void));
static tree mf_varname_tree PARAMS ((tree));
static tree mf_file_function_line_tree PARAMS ((const char *, int));
static void mf_enqueue_register_call PARAMS ((const char*, tree, tree, tree));
static void mf_flush_enqueued_calls PARAMS ((void));
static tree mf_mostly_copy_tree_r PARAMS ((tree *, int *, void *));
static tree mx_flag PARAMS ((tree));
static tree mx_xfn_indirect_ref PARAMS ((tree *, int *, void *));
static tree mx_xfn_xform_decls PARAMS ((tree *, int *, void *));

static tree mf_offset_expr_of_array_ref PARAMS ((tree, tree *, tree *, tree *));
static tree mf_build_check_statement_for PARAMS ((tree, tree, tree, tree, 
						  const char *, int));
static void mx_register_decls PARAMS ((tree, tree *));


/* These macros are used to mark tree nodes, so that they are not
   repeatedly transformed.  The `bounded' flag is not otherwise used.  */

#define MARK_TREE_MUDFLAPPED(tree)  do { TREE_BOUNDED (tree) = 1; } while (0)
#define TREE_MUDFLAPPED_P(tree)  TREE_BOUNDED (tree)

/* extern mudflap functions */

/* Perform the mudflap tree transforms on the given function.  */
 
void 
mudflap_c_function (t)
     tree t;
{
  tree fnbody = DECL_SAVED_TREE (t);
  tree fnparams = DECL_ARGUMENTS (t);

  if (getenv ("UNPARSE"))  /* XXX */
    {
      print_generic_expr (stderr, DECL_RESULT (t), 0);
      fprintf (stderr, " ");
      print_generic_expr (stderr, DECL_NAME (t), 0);
      fprintf (stderr, " (");
      print_generic_expr (stderr, DECL_ARGUMENTS (t), 0);
      fprintf (stderr, " )\n");
      print_generic_stmt (stderr, DECL_SAVED_TREE (t), 0);
    }

  mf_init_extern_trees ();

  mf_xform_decls (fnbody, fnparams);
  mf_xform_derefs (fnbody);

  if (getenv ("UNPARSE"))  /* XXX */
    {
      fprintf (stderr, "/* after -fmudflap: */\n");
      print_generic_stmt (stderr, DECL_SAVED_TREE (t), 0);
    }
}


/* ------------------------------------------------------------------------ */


/* Remember given node as a static of some kind: global data,
   function-scope static, or an anonymous constant.  Its assembler
   label is given.
*/


/* A list of globals whose incomplete declarations we encountered.
   Instead of emitting the __mf_register call for them here, it's
   delayed until program finish time.  If they're still incomplete by
   then, warnings are emitted.  */

static GTY (()) varray_type deferred_static_decls;
static GTY (()) varray_type deferred_static_decl_labels;
static int deferred_static_decls_init;

/* What I really want is a std::map<union tree_node,std::string> .. :-(  */


void 
mudflap_enqueue_decl (obj, label)
     tree obj;
     const char *label;
{
  if (TREE_MUDFLAPPED_P (obj))
    return;

  /*
  fprintf (stderr, "enqueue_decl obj=`");
  print_generic_expr (stderr, obj, 0);
  fprintf (stderr, "' label=`%s'\n", label);
  */

  if (COMPLETE_OR_VOID_TYPE_P (TREE_TYPE (obj))) 
    {
      /* NB: the above condition doesn't require TREE_USED or
         TREE_ADDRESSABLE.  That's because this object may be a global
         only used from other compilation units.  XXX: Maybe static
         objects could require those attributes being set.  */
      mf_enqueue_register_call (label,
				size_in_bytes (TREE_TYPE (obj)),
				build_int_2 (3, 0), /* __MF_TYPE_STATIC */
				mf_varname_tree (obj));
    }
  else
    {
      unsigned i;
      int found_p;
      
      if (! deferred_static_decls_init)
	{
	  deferred_static_decls_init = 1;
	  VARRAY_TREE_INIT (deferred_static_decls, 10, "deferred static list");
	  VARRAY_CHAR_PTR_INIT (deferred_static_decl_labels, 10, "label list");
	}
      
      /* Ugh, linear search... */
      found_p = 0;
      for (i=0; i < VARRAY_ACTIVE_SIZE (deferred_static_decls); i++)
	if (VARRAY_TREE (deferred_static_decls, i) == obj)
	  found_p = 1;
      
      if (found_p)
	warning_with_decl (obj, "mudflap cannot track lifetime of `%s'", 
			   IDENTIFIER_POINTER (DECL_NAME (obj)));
      else
	{
	  VARRAY_PUSH_TREE (deferred_static_decls, obj);
	  VARRAY_PUSH_CHAR_PTR (deferred_static_decl_labels, (char *) label);
	}
    }
}


void 
mudflap_enqueue_constant (obj, label)
     tree obj;
     const char *label;
{
  if (TREE_MUDFLAPPED_P (obj))
    return;

  if (TREE_CODE (obj) == STRING_CST)
    {
      mf_enqueue_register_call (label,
				build_int_2 (TREE_STRING_LENGTH (obj), 0),
				build_int_2 (3, 0), /* __MF_TYPE_STATIC */
				mx_flag (fix_string_type
					 (build_string (15, "string literal"))));
    }
  else
    {
      mf_enqueue_register_call (label,
				size_in_bytes (TREE_TYPE (obj)),
				build_int_2 (3, 0), /* __MF_TYPE_STATIC */
				mx_flag (fix_string_type
					 (build_string (9, "constant"))));
    }
}



/* Emit any file-wide instrumentation.  */
void 
mudflap_finish_file ()
{
  /* Try to give the deferred objects one final try.  */
  if (deferred_static_decls_init)
    {
      unsigned i;

      for (i = 0; i < VARRAY_ACTIVE_SIZE (deferred_static_decls); i++)
	{
	  tree obj = VARRAY_TREE (deferred_static_decls, i);
	  const char *label = VARRAY_CHAR_PTR (deferred_static_decl_labels, i);

	  /* Call enqueue_decl again on the same object it has previously
	     put into the table.  (It won't modify the table this time, so
	     infinite iteration is not a problem.)  */
	  mudflap_enqueue_decl (obj, label);
	}

      VARRAY_CLEAR (deferred_static_decls);
      VARRAY_CLEAR (deferred_static_decl_labels);
    }
	     
  mf_flush_enqueued_calls ();
}

/* global tree nodes */

/* Global tree objects for global variables and functions exported by
   mudflap runtime library.  mf_init_extern_trees must be called
   before using these.  */

static GTY (()) tree mf_uintptr_type;      /* uintptr_t (usually "unsigned long") */
static GTY (()) tree mf_cache_struct_type; /* struct __mf_cache { uintptr_t low; uintptr_t high; }; */
static GTY (()) tree mf_cache_structptr_type; /* struct __mf_cache * const */
static GTY (()) tree mf_cache_array_decl;  /* extern struct __mf_cache __mf_lookup_cache []; */
static GTY (()) tree mf_cache_shift_decl;  /* extern const unsigned char __mf_lc_shift; */
static GTY (()) tree mf_cache_mask_decl;   /* extern const uintptr_t __mf_lc_mask; */
static GTY (()) tree mf_check_fndecl;      /* extern void __mf_check (void *ptr, size_t sz, const char *); */
static GTY (()) tree mf_register_fndecl;   /* extern void __mf_register (void *ptr, size_t sz, int type, const char *); */
static GTY (()) tree mf_unregister_fndecl; /* extern void __mf_unregister (void *ptr, size_t sz); */



/* Initialize the global tree nodes that correspond to mf-runtime.h
   declarations.  */
static void
mf_init_extern_trees ()
{
  tree tmp;

  static int done = 0;
  if (done) return;

#define mf_lookup_name(id) \
  (tmp = lookup_name (id), \
   (tmp ? tmp : (internal_error ("mudflap: cannot find declarations from mf-runtime.h"), tmp)))

  mf_uintptr_type = TREE_TYPE (mf_lookup_name (get_identifier ("uintptr_t")));

  mf_cache_struct_type = xref_tag (RECORD_TYPE, get_identifier ("__mf_cache"));
  mf_cache_structptr_type = build_pointer_type (mf_cache_struct_type);

  mf_cache_array_decl = mx_flag (mf_lookup_name (get_identifier ("__mf_lookup_cache")));
  mf_cache_shift_decl = mx_flag (mf_lookup_name (get_identifier ("__mf_lc_shift")));
  mf_cache_mask_decl = mx_flag (mf_lookup_name (get_identifier ("__mf_lc_mask")));
  mf_check_fndecl = mf_lookup_name (get_identifier ("__mf_check"));
  mf_register_fndecl = mf_lookup_name (get_identifier ("__mf_register"));
  mf_unregister_fndecl = mf_lookup_name (get_identifier ("__mf_unregister"));

  done = 1;
}



/* Mark and return the given tree node to prevent further mudflap
   transforms.  */
static tree
mx_flag (t)
     tree t;
{
  if (!t)
    abort ();
  MARK_TREE_MUDFLAPPED(t);
  return t;
}



/* A copy of c-simplify.c's mostly_copy_tree_r.  */
static tree
mf_mostly_copy_tree_r (tp, walk_subtrees, data)
     tree *tp;
     int *walk_subtrees;
     void *data;
{
  if (TREE_CODE (*tp) == SAVE_EXPR)
    *walk_subtrees = 0;
  else
    copy_tree_r (tp, walk_subtrees, data);

  return NULL_TREE;
}



/* Create a properly typed STRING_CST node that describes the given
   declaration.  It will be used as an argument for __mf_register().
   Try to construct a helpful string, including file/function/variable
   name.
*/

static tree
mf_varname_tree (decl)
     tree decl;
{
  static output_buffer buf_rec;
  static int initialized = 0;
  output_buffer *buf = & buf_rec;
  const char *buf_contents;
  tree result;

  if (decl == NULL_TREE)
    abort ();

  if (!initialized)
    {
      init_output_buffer (buf, /* prefix */ NULL, /* line-width */ 0);
      initialized = 1;
    }
  output_clear_message_text (buf);

  /* Add FILENAME[:LINENUMBER]. */
  {
    const char *sourcefile;
    unsigned sourceline;

    sourcefile = DECL_SOURCE_FILE (decl);
    if (sourcefile == NULL && current_function_decl != NULL_TREE)
      sourcefile = DECL_SOURCE_FILE (current_function_decl);
    if (sourcefile == NULL)
      sourcefile = "<unknown file>";

    output_add_string (buf, sourcefile);

    sourceline = DECL_SOURCE_LINE (decl);
    if (sourceline != 0)
      {
	output_add_string (buf, ":");
	output_decimal (buf, sourceline);
      }
  }

  if (current_function_decl != NULL_TREE)
    {
      /* Add (FUNCTION): */
      output_add_string (buf, " (");
      {
	const char *funcname = NULL;
	if (DECL_NAME (current_function_decl))
	  funcname = (*lang_hooks.decl_printable_name) (current_function_decl, 2);
	if (funcname == NULL)
	  funcname = "anonymous fn";
	
	output_add_string (buf, funcname);
      }
      output_add_string (buf, ") ");
    }
  else
    output_add_string (buf, " ");

  /* Add <variable-declaration> */
  dump_generic_node (buf, decl, 0, 0);

  /* Return the lot as a new STRING_CST.  */
  buf_contents = output_finalize_message (buf);
  result = fix_string_type (build_string (strlen (buf_contents) + 1, buf_contents));
  output_clear_message_text (buf);

  return mx_flag (result);
}


/* And another friend, for producing a simpler message.  */
static tree
mf_file_function_line_tree (file, line)
     const char * file;
     int line;
{
  static output_buffer buf_rec;
  static int initialized = 0;
  output_buffer *buf = & buf_rec;
  const char *buf_contents;
  tree result;

  if (!initialized)
    {
      init_output_buffer (buf, /* prefix */ NULL, /* line-width */ 0);
      initialized = 1;
    }
  output_clear_message_text (buf);

  /* Add FILENAME[:LINENUMBER]. */
  if (file == NULL && current_function_decl != NULL_TREE)
    file = DECL_SOURCE_FILE (current_function_decl);
  if (file == NULL)
    file = "<unknown file>";
  output_add_string (buf, file);

  if (line > 0)
    {
      output_add_string (buf, ":");
      output_decimal (buf, line);
    }

  /* Add (FUNCTION) */
  if (current_function_decl != NULL_TREE)
    {
      output_add_string (buf, " (");
      {
	const char *funcname = NULL;
	if (DECL_NAME (current_function_decl))
	  funcname = (*lang_hooks.decl_printable_name) (current_function_decl, 2);
	if (funcname == NULL)
	  funcname = "anonymous fn";
	
	output_add_string (buf, funcname);
      }
      output_add_string (buf, ")");
    }

  /* Return the lot as a new STRING_CST.  */
  buf_contents = output_finalize_message (buf);
  result = fix_string_type (build_string (strlen (buf_contents) + 1, buf_contents));

  return mx_flag (result);
}



/* 
   Assuming the declaration "foo a[xdim][ydim][zdim];", we will get
   an expression "a[x][y][z]" as a tree structure something like
   
   {ARRAY_REF, z, type = foo,
    {ARRAY_REF, y, type = foo[zdim],
     {ARRAY_REF, x, type = foo[ydim][zdim],
      {ARRAY, a, type = foo[xdim][ydim][zdim] }}}
   
   from which we will produce an offset value of the form:
   
   {PLUS_EXPR z, {MULT_EXPR zdim,
    {PLUS_EXPR y, {MULT_EXPR ydim, 
     x }}}}

   The offset expression may be folded to a constant, or may
   require reference to temporary variables whose decls are
   pushed on to the decls chain.
*/

static tree 
mf_offset_expr_of_array_ref (t, offset, base, decls)
     tree t;
     tree *offset;
     tree *base;
     tree *decls;
{
  /* Replace the array index operand [1] with a temporary variable.
     This is meant to emulate SAVE_EXPRs that are sometimes screwed up
     by other parts of gcc.  */
  if ((TREE_CODE (t) == ARRAY_REF ||
       TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE) &&
      ! really_constant_p (*offset))
    {
      static unsigned declindex;
      char declname[20];
      tree newdecl, idxexpr;

      idxexpr = *offset;
      sprintf (declname, "__mf_index_%u", declindex++);

      newdecl = build_decl (VAR_DECL, get_identifier (declname),
			    TREE_TYPE (idxexpr));
      DECL_ARTIFICIAL (newdecl) = 1;
      DECL_INITIAL (newdecl) = idxexpr;

      /* Accumulate this new decl. */
      *decls = tree_cons (NULL_TREE, newdecl, *decls);

      /* Replace the index expression with the plain VAR_DECL reference.  */
      *offset = newdecl;
    }

  if (TREE_CODE (t) == ARRAY_REF)
    {
      /* It's a sub-array-ref; recurse. */

      tree factor = fold (build (PLUS_EXPR, 
				 integer_type_node, 
				 integer_one_node, 
				 TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (t)))));

      /* Mark this node to inhibit further transformation.  */
      mx_flag (t);
      
      return
	fold (build (PLUS_EXPR, integer_type_node, *offset, 
		     fold (build (MULT_EXPR, integer_type_node, factor, 
				  mf_offset_expr_of_array_ref (TREE_OPERAND (t, 0), 
							       & TREE_OPERAND (t, 1),
							       base,
							       decls)))));
    } 
  else if (TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE)
    {
      /* It's *not* an ARRAY_REF, but it *is* an ARRAY_TYPE; we are at
	 the bottom of the ARRAY_REF expression.  */ 
      *base = t;

      return *offset;
    }
  else 
    {
      /* It's an array ref of a non-array -> failure. */
      abort ();
    }
}



static tree 
mf_build_check_statement_for (ptrvalue, chkbase, chksize, 
			      chkdecls, filename, lineno)
     tree ptrvalue;
     tree chkbase;
     tree chksize;
     tree chkdecls;
     const char *filename;
     int lineno;
{
  tree ptrtype = TREE_TYPE (ptrvalue);
  tree myptrtype = build_qualified_type (ptrtype, TYPE_QUAL_CONST);
  tree location_string = mf_file_function_line_tree (filename, lineno);

  tree bind_decls = NULL_TREE;  /* A chain of VAR_DECL nodes. */
  tree bind_exprs = NULL_TREE;  /* Eventually a COMPOUND_EXPR. */

  tree t1_2_1;
  tree t1_2a_1;
  tree t1_2b_1;
  tree t1_3_1;
  tree t1_4_1, t1_4_2;
  tree t0;

  /* Insert any supplied helper declarations.  */
  while (chkdecls != NULL_TREE)
    {
      tree decl = TREE_VALUE (chkdecls);
      tree init = DECL_INITIAL (decl);
      tree type = TREE_TYPE (init);
      tree declstmt = build (INIT_EXPR, type, decl, init);

      DECL_INITIAL (decl) = NULL_TREE; /* use INIT_EXPR instead */
      TREE_CHAIN (decl) = bind_decls; bind_decls = decl;
      add_tree (declstmt, & bind_exprs);

      chkdecls = TREE_CHAIN (chkdecls);
    }

  /* <TYPE> const __mf_value = <EXPR>; */
  t1_2_1 = build_decl (VAR_DECL, get_identifier ("__mf_value"), myptrtype);
  DECL_ARTIFICIAL (t1_2_1) = 1;
  TREE_CHAIN (t1_2_1) = bind_decls; bind_decls = t1_2_1;
  add_tree (build (INIT_EXPR, myptrtype, 
		   t1_2_1,
		   ptrvalue), 
	    & bind_exprs);

  /* uintptr_t __mf_base = <EXPR2>; */
  t1_2a_1 = build_decl (VAR_DECL, get_identifier ("__mf_base"), mf_uintptr_type);
  DECL_ARTIFICIAL (t1_2a_1) = 1;
  TREE_CHAIN (t1_2a_1) = bind_decls; bind_decls = t1_2a_1;
  add_tree (build (INIT_EXPR, mf_uintptr_type,
		   t1_2a_1, 
		   convert (mf_uintptr_type, ((chkbase == ptrvalue) ? t1_2_1 : chkbase))),
	    & bind_exprs);

  /* uintptr_t __mf_size = <EXPR>; */
  t1_2b_1 = build_decl (VAR_DECL, get_identifier ("__mf_size"), mf_uintptr_type);
  DECL_ARTIFICIAL (t1_2b_1) = 1;
  TREE_CHAIN (t1_2b_1) = bind_decls; bind_decls = t1_2b_1;
  add_tree (build (INIT_EXPR, mf_uintptr_type,
		   t1_2b_1, 
		   convert (mf_uintptr_type,
			    ((chksize == NULL_TREE) ? integer_one_node : chksize))),
	    & bind_exprs);

  /* struct __mf_cache * const __mf_elem = [...] */
  t1_3_1 = build_decl (VAR_DECL, get_identifier ("__mf_elem"), mf_cache_structptr_type);
  DECL_ARTIFICIAL (t1_3_1) = 1;
  TREE_CHAIN (t1_3_1) = bind_decls; bind_decls = t1_3_1;

    /* & __mf_lookup_cache [(((uintptr_t)__mf_base) >> __mf_shift) & __mf_mask] */
  add_tree (build (INIT_EXPR, mf_cache_structptr_type,
		   t1_3_1, 
		   mx_flag (build1 (ADDR_EXPR, mf_cache_structptr_type,
				    mx_flag (build (ARRAY_REF, TYPE_MAIN_VARIANT (TREE_TYPE
										  (TREE_TYPE
										   (mf_cache_array_decl))),
						    mf_cache_array_decl,
						    build (BIT_AND_EXPR, mf_uintptr_type,
							   build (RSHIFT_EXPR, mf_uintptr_type,
								  convert (mf_uintptr_type, t1_2a_1),
								  mf_cache_shift_decl),
							   mf_cache_mask_decl)))))),
	    & bind_exprs);
  
  /* Quick validity check.  */
  t1_4_1 = build (BIT_IOR_EXPR, integer_type_node,
		  build (GT_EXPR, integer_type_node,
			 mx_flag (build (COMPONENT_REF, mf_uintptr_type, /* __mf_elem->low */
					 mx_flag (build1 (INDIRECT_REF, 
							  mf_cache_struct_type, t1_3_1)),
					 TYPE_FIELDS (mf_cache_struct_type))),
			 t1_2a_1), /* __mf_base */
		  build (LT_EXPR, integer_type_node,
			 mx_flag (build (COMPONENT_REF, mf_uintptr_type, /* __mf_elem->high */
					 mx_flag (build1 (INDIRECT_REF, 
							  mf_cache_struct_type, t1_3_1)),
					 TREE_CHAIN (TYPE_FIELDS (mf_cache_struct_type)))),
			 build (PLUS_EXPR, mf_uintptr_type, /* __mf_elem + sizeof(T) - 1 */
				t1_2a_1,
				fold (build (MINUS_EXPR, mf_uintptr_type,
					     t1_2b_1,
					     integer_one_node)))));
  
  /* Mark condition as UNLIKELY using __builtin_expect.  */
  t1_4_1 = build_function_call (built_in_decls[BUILT_IN_EXPECT],
				tree_cons (NULL_TREE,
					   convert (long_integer_type_node, t1_4_1),
					   tree_cons (NULL_TREE,
						      integer_zero_node,
						      NULL_TREE)));
  
  t1_4_2 = build_function_call (mf_check_fndecl,
				tree_cons (NULL_TREE,
					   t1_2a_1,
					   tree_cons (NULL_TREE, 
						      t1_2b_1,
						      tree_cons (NULL_TREE,
								 location_string,
								 NULL_TREE))));

  add_tree (build (COND_EXPR, void_type_node,
		   t1_4_1,
		   t1_4_2,
		   empty_stmt_node),
	    & bind_exprs);

  /* "return" __mf_value, or provided finale.  */
  TREE_SIDE_EFFECTS (t1_2_1) = 1; /* add_tree antifreak */
  add_tree (t1_2_1, & bind_exprs);

  /* Turn the tree into right-recursive form. */
  bind_exprs = rationalize_compound_expr (bind_exprs);

  t0 = build (BIND_EXPR, myptrtype, 
	      nreverse (bind_decls),
	      bind_exprs, 
	      NULL_TREE); /* XXX: BLOCK == NULL */
  TREE_SIDE_EFFECTS (t0) = 1;

  return t0;
}



/* ------------------------------------------------------------------------ */
/* INDIRECT_REF transform */

/* Perform the mudflap bounds-checking transform on the given function
   tree.  The tree is mutated in place, with possibly copied subtree
   nodes.

   (1)  (INDIRECT_REF (tree))
        ==>
        (INDIRECT_REF ({TYPE * const value = (tree); // <== RECURSE HERE
	                struct __mf_cache * elem = 
			     & __mf_lookup_cache [((unsigned)value >> __mf_shift) &
                                                  __mf_mask];
			if (UNLIKELY ((elem->low > value) ||
			              (elem->high < value+sizeof(TYPE)-1)))
			   __mf_check (value, sizeof(TYPE));
                        value;})

   (2) (ARRAY_REF ({ARRAY_REF ... (tree, indexM)}, indexN))  //  tree[N][M][O]...
       ==> (as if)
       (INDIRECT_REF (tree + (.... + sizeM*sizeO*indexO + sizeM*indexM + indexN))
       ... except the base value for the check is &tree[0], not &tree[N][M][O]

   (3) (COMPONENT_REF (INDIRECT_REF (tree), field))
       ==> (as if)
       (COMPONENT_REF (INDIRECT_REF (tree), field))
       ... except the size value for the check is offsetof(field)+sizeof(field)-1

   XXX: COMPONENT_REF (ARRAY_REF ....) is also possible

   (4) (BIT_FIELD_REF (INDIRECT_REF (tree), bitsize, bitpos))
       ==> (as if)
       (BIT_FIELD_REF (INDIRECT_REF (tree), bitsize, bitpos))
       ... except the size value for the check is to include byte @ bitsize+bitpos
*/


static tree
mx_xfn_indirect_ref (t, continue_p, data)
     tree *t;
     int *continue_p;
     void *data;
{
  static const char *last_filename = NULL;
  static int last_lineno = -1;
  htab_t verboten = (htab_t) data;

#if 0
  fprintf (stderr, "expr=%s: ", tree_code_name [TREE_CODE (*t)]);
  print_generic_expr (stderr, *t, 0);
  fprintf (stderr, "\n");
#endif

  *continue_p = 1;

  /* Track file-name/line-numbers.  */
  if (statement_code_p (TREE_CODE (*t)))
    last_lineno = (STMT_LINENO (*t) > 0 ? STMT_LINENO (*t) : last_lineno);
  if (TREE_CODE (*t) == FILE_STMT)
    last_filename = FILE_STMT_FILENAME (*t);
  if (TREE_CODE (*t) == EXPR_WITH_FILE_LOCATION)
    {
      last_filename = EXPR_WFL_FILENAME (*t);
      last_lineno = (EXPR_WFL_LINENO (*t) > 0 ? EXPR_WFL_LINENO (*t) : last_lineno);
    }

  /* Avoid traversal into subtrees specifically listed as
     do-not-traverse.  This occurs for certain nested operator/array
     expressions.  */
  if (htab_find (verboten, *t) != NULL)
    {
      *continue_p = 0;
      return NULL_TREE;
    }

  /* Avoid infinite recursion of transforming instrumented or
     instrumentation code.  NB: This check is done second, in case the
     same node is marked verboten as well as mudflapped.  The former
     takes priority, and is meant to prevent further traversal.  */
  if (TREE_MUDFLAPPED_P (*t))
    return NULL_TREE;

  /* Process some node types.  */
  switch (TREE_CODE (*t))
    {
    default:
      ; /* Continue traversal.  */
      break;

    case ARRAY_REF:
      {
	tree base_array, base_obj_type, base_ptr_type;
	tree offset_expr;
	tree value_ptr, check_ptr, check_size;
	tree check_decls = NULL_TREE;

	/* Unshare the whole darned tree.  */
	walk_tree (t, mf_mostly_copy_tree_r, NULL, NULL);

	offset_expr = mf_offset_expr_of_array_ref (TREE_OPERAND (*t,0), 
						   & TREE_OPERAND (*t,1), 
						   & base_array,
						   & check_decls);
	check_decls = nreverse (check_decls); /* XXX: evaluation order?  */

	/* We now have a tree representing the array in base_array, 
	   and a tree representing the complete desired offset in
	   offset_expr. */

	base_obj_type = TREE_TYPE (TREE_TYPE (TREE_OPERAND(*t,0)));
	base_ptr_type = build_pointer_type (base_obj_type);

        check_ptr = mx_flag (build1 (ADDR_EXPR, 
				     base_ptr_type, 
				     mx_flag (build (ARRAY_REF, 
						     base_obj_type, 
						     base_array,
						     integer_zero_node))));
	TREE_ADDRESSABLE (base_array) = 1;

	value_ptr = mx_flag (build1 (ADDR_EXPR,
				     base_ptr_type,
				     mx_flag (*t)));
	walk_tree (& value_ptr, mf_mostly_copy_tree_r, NULL, NULL);
	TREE_ADDRESSABLE (*t) = 1;

	check_size = fold (build (MULT_EXPR, 
				  integer_type_node,
				  TYPE_SIZE_UNIT (base_obj_type),
				  fold (build (PLUS_EXPR, size_type_node,
					       integer_one_node,
					       offset_expr))));

	/* As an optimization, omit checking if the base object is
	   known to be large enough.  Only certain kinds of
	   declarations and indexes/sizes are trustworthy.  */
	if (TREE_CODE (check_size) == INTEGER_CST && /* constant offset */
	    TREE_CODE (base_array) == VAR_DECL && /* not a PARM_DECL */
	    ! DECL_EXTERNAL (base_array) && /* has known size */
	    TREE_CODE (TREE_TYPE (base_array)) == ARRAY_TYPE && /* an array */
	    ((size_t) int_size_in_bytes (TREE_TYPE (base_array)) >= (size_t) TREE_INT_CST_LOW (check_size) &&
	     TREE_INT_CST_HIGH (check_size) == 0)) /* offset within bounds */
	  {
#if 0
	    warning ("mudflap is omitting array bounds checks");
	    fprintf (stderr, "  for expression: ");
	    print_generic_expr (stderr, *t, 0);
	    fprintf (stderr, " array-size=%u", int_size_in_bytes (TREE_TYPE (base_array)));
	    fprintf (stderr, " check-size=%u", TREE_INT_CST_LOW (check_size));
	    fprintf (stderr, "\n");
#endif
	    if (check_decls != NULL_TREE) abort();
	    break;
	  }
	else
	  {
	    tree tmp;

	    /* In case we're instrumenting an expression like a[b[c]], the
	       following call is meant to eliminate the
	       redundant/recursive check of the outer size=b[c] check. */
	    * (htab_find_slot (verboten, check_size, INSERT)) = check_size;
	    * (htab_find_slot (verboten, check_ptr, INSERT)) = check_ptr;
	    
	    tmp = mf_build_check_statement_for (value_ptr, check_ptr, check_size,
						check_decls,
						last_filename, last_lineno);
	    *t = mx_flag (build1 (INDIRECT_REF, base_obj_type, tmp));
	  }
      }
      break;
      
    case ARRAY_RANGE_REF:
      /* not yet implemented */
      warning ("mudflap checking not yet implemented for ARRAY_RANGE_REF");
      break;

    case INDIRECT_REF:
      /* Substitute check statement for ptrvalue in INDIRECT_REF.  */
      TREE_OPERAND (*t, 0) = 
	mf_build_check_statement_for (TREE_OPERAND (*t, 0),
				      TREE_OPERAND (*t, 0),
				      TYPE_SIZE_UNIT (TREE_TYPE
						      (TREE_TYPE
						       (TREE_OPERAND (*t, 0)))),
				      NULL_TREE, last_filename, last_lineno);
	/* Prevent this transform's reapplication to this tree node.
	   Note that we do not prevent recusion in walk_tree toward
	   subtrees of this node, in case of nested pointer expressions.  */
      mx_flag (*t);
      break;

    case COMPONENT_REF:
      if (TREE_CODE (TREE_OPERAND (*t, 0)) == INDIRECT_REF)
	{
	  tree *pointer = & TREE_OPERAND (TREE_OPERAND (*t, 0), 0);

	  tree field = TREE_OPERAND (*t, 1);
	  tree field_offset = byte_position (field);
	  tree field_size =
	    DECL_BIT_FIELD_TYPE (field) ?
	    size_binop (TRUNC_DIV_EXPR, 
			size_binop (PLUS_EXPR, 
				    DECL_SIZE (field), /* bitfield width */
				    convert (bitsizetype, 
					     build_int_2 (BITS_PER_UNIT - 1, 0))),
			convert (bitsizetype, build_int_2 (BITS_PER_UNIT, 0)))
	    : size_in_bytes (TREE_TYPE (TREE_OPERAND (*t, 1)));
	  tree check_size = fold (build (PLUS_EXPR, size_type_node,
					 field_offset, field_size));

	  *pointer = 
	    mf_build_check_statement_for (*pointer,
					  *pointer,
					  check_size,
					  NULL_TREE,
					  last_filename, last_lineno);
	  
	  /* Don't instrument the nested INDIRECT_REF. */ 
	  mx_flag (TREE_OPERAND (*t, 0));
	  mx_flag (*t);
	}
      break;


    case BIT_FIELD_REF:
      if (TREE_CODE (TREE_OPERAND (*t, 0)) == INDIRECT_REF)
	{
	  tree *pointer = & TREE_OPERAND (TREE_OPERAND (*t, 0), 0);

	  tree bitsize = TREE_OPERAND (*t, 1);
	  tree bitpos = TREE_OPERAND (*t, 2);
	  tree check_size =
	    fold (build (TRUNC_DIV_EXPR, size_type_node,
			 fold (build (PLUS_EXPR, size_type_node,
				      bitsize, 
				      fold (build (PLUS_EXPR, size_type_node, 
						   bitpos,
						   build_int_2 (BITS_PER_UNIT - 1, 0))))),
			 build_int_2 (BITS_PER_UNIT, 0)));

	  *pointer = 
	    mf_build_check_statement_for (*pointer,
					  *pointer,
					  check_size,
					  NULL_TREE,
					  last_filename, last_lineno);
	  
	  /* Don't instrument the nested INDIRECT_REF. */ 
	  mx_flag (TREE_OPERAND (*t, 0));
	  mx_flag (*t);
	}
      break;
    }

  return NULL_TREE;
}



static void
mf_xform_derefs (fnbody)
     tree fnbody;
{
  htab_t verboten = htab_create (31, htab_hash_pointer, htab_eq_pointer, NULL);
  walk_tree_without_duplicates (& fnbody, mx_xfn_indirect_ref, (void*) verboten);
  htab_delete (verboten);
}



/* ------------------------------------------------------------------------ */
/* ADDR_EXPR transform */


/* This struct is passed between mf_xform_decls to store state needed
   during the traversal searching for objects that have their
   addresses taken. */
struct mf_xform_decls_data
{
  tree param_decls;
};


/* Synthesize a CALL_EXPR and a TRY_FINALLY_EXPR, for this chain of
   _DECLs if appropriate.  Arrange to call the __mf_register function
   now, and the __mf_unregister function later for each.  */
static void
mx_register_decls (decl, compound_expr)
     tree decl;
     tree *compound_expr;
{
  tree finally_stmts = NULL_TREE;
  tree initially_stmts = NULL_TREE;

  while (decl != NULL_TREE)
    {
      /* Eligible decl?  */
      if ((TREE_CODE (decl) == VAR_DECL || TREE_CODE (decl) == PARM_DECL) &&
	  (! TREE_STATIC (decl)) && /* auto variable */
	  (! DECL_EXTERNAL (decl)) && /* not extern variable */
	  (COMPLETE_OR_VOID_TYPE_P (TREE_TYPE (decl))) && /* complete type */
	  (! TREE_MUDFLAPPED_P (decl)) && /* not already processed */
	  (TREE_ADDRESSABLE (decl))) /* has address taken */
	{
	  /* (& VARIABLE, sizeof (VARIABLE)) */
	  tree unregister_fncall_params =
	    tree_cons (NULL_TREE,
		       convert (mf_uintptr_type, 
				mx_flag (build1 (ADDR_EXPR, 
						 build_pointer_type (TREE_TYPE (decl)),
						 decl))),
		       tree_cons (NULL_TREE, 
				  convert (mf_uintptr_type, 
					   TYPE_SIZE_UNIT (TREE_TYPE (decl))),
				  NULL_TREE));
	  /* __mf_unregister (...) */
	  tree unregister_fncall =
	    build_function_call (mf_unregister_fndecl,
				 unregister_fncall_params);

	  /* (& VARIABLE, sizeof (VARIABLE), __MF_LIFETIME_STACK=2) */
	  tree variable_name = mf_varname_tree (decl);
	  tree register_fncall_params =
	    tree_cons (NULL_TREE,
		   convert (mf_uintptr_type, 
			    mx_flag (build1 (ADDR_EXPR, 
					     build_pointer_type (TREE_TYPE (decl)),
					     decl))),
		       tree_cons (NULL_TREE, 
				  convert (mf_uintptr_type, 
					   TYPE_SIZE_UNIT (TREE_TYPE (decl))),
				  tree_cons (NULL_TREE,
					     build_int_2 (2, 0),
					     tree_cons (NULL_TREE,
							variable_name,
							NULL_TREE))));
	  /* __mf_register (...) */
	  tree register_fncall =
	    build_function_call (mf_register_fndecl,
				 register_fncall_params);

	  /* Accumulate the two calls.  */
	  add_tree (register_fncall, & initially_stmts);
	  add_tree (unregister_fncall, & finally_stmts);
	  
	  mx_flag (decl);
	  /* Hint to inhibit any fancy register optimizations on this variable. */
	  TREE_ADDRESSABLE (decl) = 1;
	}

      decl = TREE_CHAIN (decl);
    }

  /* Now process the pending register/unregister calls, if any.  */
  if (initially_stmts != NULL_TREE)
    {
      /* Prepend them to the existing expression parameter of the
	 enclosing BIND_EXPR.  */
      *compound_expr = build (COMPOUND_EXPR, TREE_TYPE (*compound_expr), 
			      initially_stmts, *compound_expr);
      *compound_expr = rationalize_compound_expr (*compound_expr);
    }

  /* Actually, (initially_stmts!=NULL) <=> (finally_stmts!=NULL) */
  if (finally_stmts != NULL_TREE)
    {
      finally_stmts = rationalize_compound_expr (finally_stmts);
      *compound_expr = build (TRY_FINALLY_EXPR, TREE_TYPE (*compound_expr),
			      *compound_expr, finally_stmts);
    }
}


/* Process every variable mentioned in BIND_EXPRs.  */
static tree
mx_xfn_xform_decls (t, continue_p, data)
     tree *t;
     int *continue_p;
     void *data;
{
  struct mf_xform_decls_data* d = (struct mf_xform_decls_data*) data;

  if (*t == NULL_TREE || *t == error_mark_node)
    {
      *continue_p = 0;
      return NULL_TREE;
    }

  *continue_p = 1;

  switch (TREE_CODE (*t))
    {
    case BIND_EXPR:
      {
	/* Process function parameters now (but only once).  */
	mx_register_decls (d->param_decls, & BIND_EXPR_BODY (*t));
	d->param_decls = NULL_TREE;

	mx_register_decls (BIND_EXPR_VARS (*t), & BIND_EXPR_BODY (*t));
      }
      break;

    default:
      break;
    }

  return NULL;
}



/* Perform the object lifetime tracking mudflap transform on the given function
   tree.  The tree is mutated in place, with possibly copied subtree nodes.

   For every auto variable declared, if its address is ever taken
   within the function, then supply its lifetime to the mudflap
   runtime with the __mf_register and __mf_unregister calls.
*/

static void
mf_xform_decls (fnbody, fnparams)
     tree fnbody;
     tree fnparams;
{
  struct mf_xform_decls_data d;
  d.param_decls = fnparams;
  walk_tree_without_duplicates (& fnbody, mx_xfn_xform_decls, & d);
}


/* ------------------------------------------------------------------------ */
/* global variable transform */

/* A chain of EXPR_STMTs for calling __mf_register() at initialization
   time.  */
static GTY (()) tree enqueued_call_stmt_chain = NULL_TREE;


/* Build and enqueue EXPR_STMT for calling __mf_register on the object
   given by the parameters.  One odd thing: the object's address is
   given by the given assembler label string (since that's all we may
   know about a string literal, or the static data thingie may be out
   of the future scope).  To turn that into a validish C tree, we
   create a weird synthetic VAR_DECL node.
*/ 

static void
mf_enqueue_register_call (label, regsize, regtype, regname)
     const char* label;
     tree regsize;
     tree regtype;
     tree regname;
{
  tree decltype, decl;
  tree call_params;
  tree call_stmt;

  mf_init_extern_trees ();

  /* See gcc-checker's c-bounds.c (declare_private_statics)  */
  decltype = build_array_type (char_type_node, build_index_type (integer_zero_node));
  decl = mx_flag (build_decl (VAR_DECL, get_identifier (label), decltype));

  TREE_STATIC (decl) = 1;
  TREE_READONLY (decl) = 1;
  TREE_ASM_WRITTEN (decl) = 1;
  DECL_IGNORED_P (decl) = 1;
  DECL_INITIAL (decl) = NULL_TREE;
  layout_decl (decl, 0);
  TREE_USED (decl) = 1;
  SET_DECL_ASSEMBLER_NAME (decl, get_identifier (label));
  DECL_DEFER_OUTPUT (decl) = 1;
  /* XXX: what else? */
  /* rest_of_decl_compilation (decl, build_string (strlen (label) + 1, label), 1, 0); */
  /* make_decl_rtl (decl,  build_string (strlen (label) + 1, label)); */

  call_params = tree_cons (NULL_TREE,
			   convert (mf_uintptr_type, 
				    mx_flag (build1 (ADDR_EXPR, 
						     build_pointer_type (TREE_TYPE (decl)),
						     decl))),
			   tree_cons (NULL_TREE, 
				      convert (mf_uintptr_type, regsize),
				      tree_cons (NULL_TREE,
						 regtype,
						 tree_cons (NULL_TREE,
							    regname,
							    NULL_TREE))));

  call_stmt = build1 (EXPR_STMT, void_type_node,
		      build_function_call (mf_register_fndecl,
					   call_params));

  /* Link this call into the chain. */
  TREE_CHAIN (call_stmt) = enqueued_call_stmt_chain;
  enqueued_call_stmt_chain = call_stmt;
}


/* Emit a synthetic CTOR function for the current file.  Populate it
   from the enqueued __mf_register calls.  Call the RTL expanders
   inline.  */

static void
mf_flush_enqueued_calls ()
{
  /* See profile.c (output_func_start_profiler) */
  tree fnname;
  char *nmplus;
  tree fndecl;
  tree body;

  /* Short-circuit!  */
  if (enqueued_call_stmt_chain == NULL_TREE)
    return;

  /* Create the COMPOUND_STMT that becomes the new function's body.  */
  body = make_node (COMPOUND_STMT);
  COMPOUND_BODY (body) = enqueued_call_stmt_chain;
  enqueued_call_stmt_chain = NULL_TREE;

  /* Create a ctor function declaration.  */
  nmplus = concat (IDENTIFIER_POINTER (get_file_function_name ('I')), "_mudflap", NULL);
  fnname = get_identifier (nmplus);
  free (nmplus);
  fndecl = build_decl (FUNCTION_DECL, fnname,
		       build_function_type (void_type_node, NULL_TREE));
  DECL_EXTERNAL (fndecl) = 0;
  TREE_PUBLIC (fndecl) = ! targetm.have_ctors_dtors;
  TREE_USED (fndecl) = 1;
  DECL_RESULT (fndecl) = build_decl (RESULT_DECL, NULL_TREE, void_type_node);

  /* Now compile the sucker as we go.  This is a weird semi-inlined
  form of the guts of the c-parse.y `fndef' production, and a hybrid
  with c_expand_body. */

  /* start_function */
  fndecl = pushdecl (fndecl);
  pushlevel (0);
  rest_of_decl_compilation (fndecl, 0, 1, 0);
  announce_function (fndecl);
  current_function_decl = fndecl;
  DECL_INITIAL (fndecl) = error_mark_node;
  DECL_SAVED_TREE (fndecl) = body;
  make_decl_rtl (fndecl, NULL);

  /* store_parm_decls */
  init_function_start (fndecl, input_filename, lineno);
  cfun->x_whole_function_mode_p = 1;

  /* finish_function */
  poplevel (1, 0, 1);
  BLOCK_SUPERCONTEXT (DECL_INITIAL (fndecl)) = fndecl;

  /* c_expand_body */
  expand_function_start (fndecl, 0);
  expand_stmt (DECL_SAVED_TREE (fndecl));
  if (lang_expand_function_end)
    (*lang_expand_function_end) ();
  expand_function_end (input_filename, lineno, 0);
  rest_of_compilation (fndecl);
  if (! quiet_flag) 
    fflush (asm_out_file);
  current_function_decl = NULL_TREE;
  if (targetm.have_ctors_dtors)
    (* targetm.asm_out.constructor) (XEXP (DECL_RTL (fndecl), 0),
                                     DEFAULT_INIT_PRIORITY);
  else
    static_ctors = tree_cons (NULL_TREE, fndecl, static_ctors);
}



#include "gt-tree-mudflap.h"
