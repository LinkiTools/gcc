/* Language hooks common to C and ObjC front ends.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Contributed by Ziemowit Laski  <zlaski@apple.com>

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

#ifndef GCC_C_OBJC_COMMON
#define GCC_C_OBJC_COMMON

/* In c-objc-common.c.  */
extern void c_initialize_diagnostics (diagnostic_context *);

/* Lang hooks that are shared between C and ObjC are defined here.  Hooks
   specific to C or ObjC go in c-lang.c and objc/objc-lang.c, respectively.  */

#undef LANG_HOOKS_IDENTIFIER_SIZE
#define LANG_HOOKS_IDENTIFIER_SIZE C_SIZEOF_STRUCT_LANG_IDENTIFIER
#undef LANG_HOOKS_FINISH
#define LANG_HOOKS_FINISH c_common_finish
#undef LANG_HOOKS_INIT_OPTIONS
#define LANG_HOOKS_INIT_OPTIONS c_common_init_options
#undef LANG_HOOKS_INITIALIZE_DIAGNOSTICS
#define LANG_HOOKS_INITIALIZE_DIAGNOSTICS c_initialize_diagnostics
#undef LANG_HOOKS_HANDLE_OPTION
#define LANG_HOOKS_HANDLE_OPTION c_common_handle_option
#undef LANG_HOOKS_MISSING_ARGUMENT
#define LANG_HOOKS_MISSING_ARGUMENT c_common_missing_argument
#undef LANG_HOOKS_POST_OPTIONS
#define LANG_HOOKS_POST_OPTIONS c_common_post_options
#undef LANG_HOOKS_GET_ALIAS_SET
#define LANG_HOOKS_GET_ALIAS_SET c_common_get_alias_set
#undef LANG_HOOKS_EXPAND_EXPR
#define LANG_HOOKS_EXPAND_EXPR c_expand_expr
#undef LANG_HOOKS_EXPAND_DECL
#define LANG_HOOKS_EXPAND_DECL c_expand_decl
#undef LANG_HOOKS_MARK_ADDRESSABLE
#define LANG_HOOKS_MARK_ADDRESSABLE c_mark_addressable
#undef LANG_HOOKS_PARSE_FILE
#define LANG_HOOKS_PARSE_FILE c_common_parse_file
#undef LANG_HOOKS_CLEAR_BINDING_STACK
#define LANG_HOOKS_CLEAR_BINDING_STACK lhd_do_nothing
#undef LANG_HOOKS_TRUTHVALUE_CONVERSION
#define LANG_HOOKS_TRUTHVALUE_CONVERSION c_objc_common_truthvalue_conversion
#undef LANG_HOOKS_FINISH_INCOMPLETE_DECL
#define LANG_HOOKS_FINISH_INCOMPLETE_DECL c_finish_incomplete_decl
#undef LANG_HOOKS_UNSAFE_FOR_REEVAL
#define LANG_HOOKS_UNSAFE_FOR_REEVAL c_common_unsafe_for_reeval
#undef LANG_HOOKS_STATICP
#define LANG_HOOKS_STATICP c_staticp
#undef LANG_HOOKS_SET_DECL_ASSEMBLER_NAME
#define LANG_HOOKS_SET_DECL_ASSEMBLER_NAME c_static_assembler_name
#undef LANG_HOOKS_NO_BODY_BLOCKS
#define LANG_HOOKS_NO_BODY_BLOCKS true
#undef LANG_HOOKS_WARN_UNUSED_GLOBAL_DECL
#define LANG_HOOKS_WARN_UNUSED_GLOBAL_DECL c_warn_unused_global_decl
#undef LANG_HOOKS_PRINT_IDENTIFIER
#define LANG_HOOKS_PRINT_IDENTIFIER c_print_identifier
#undef LANG_HOOKS_FUNCTION_ENTER_NESTED
#define LANG_HOOKS_FUNCTION_ENTER_NESTED c_push_function_context
#undef LANG_HOOKS_FUNCTION_LEAVE_NESTED
#define LANG_HOOKS_FUNCTION_LEAVE_NESTED c_pop_function_context
#undef LANG_HOOKS_FUNCTION_MISSING_NORETURN_OK_P
#define LANG_HOOKS_FUNCTION_MISSING_NORETURN_OK_P c_missing_noreturn_ok_p
#undef LANG_HOOKS_DUP_LANG_SPECIFIC_DECL
#define LANG_HOOKS_DUP_LANG_SPECIFIC_DECL c_dup_lang_specific_decl

/* Attribute hooks.  */
#undef LANG_HOOKS_COMMON_ATTRIBUTE_TABLE
#define LANG_HOOKS_COMMON_ATTRIBUTE_TABLE c_common_attribute_table
#undef LANG_HOOKS_FORMAT_ATTRIBUTE_TABLE
#define LANG_HOOKS_FORMAT_ATTRIBUTE_TABLE c_common_format_attribute_table

#undef LANG_HOOKS_TREE_INLINING_CANNOT_INLINE_TREE_FN
#define LANG_HOOKS_TREE_INLINING_CANNOT_INLINE_TREE_FN \
  c_cannot_inline_tree_fn
#undef LANG_HOOKS_TREE_INLINING_DISREGARD_INLINE_LIMITS
#define LANG_HOOKS_TREE_INLINING_DISREGARD_INLINE_LIMITS \
  c_disregard_inline_limits
#undef LANG_HOOKS_TREE_INLINING_ANON_AGGR_TYPE_P
#define LANG_HOOKS_TREE_INLINING_ANON_AGGR_TYPE_P \
  anon_aggr_type_p
#undef LANG_HOOKS_TREE_INLINING_CONVERT_PARM_FOR_INLINING
#define LANG_HOOKS_TREE_INLINING_CONVERT_PARM_FOR_INLINING \
  c_convert_parm_for_inlining
#undef LANG_HOOKS_TREE_DUMP_DUMP_TREE_FN
#define LANG_HOOKS_TREE_DUMP_DUMP_TREE_FN c_dump_tree

#undef LANG_HOOKS_CALLGRAPH_EXPAND_FUNCTION
#define LANG_HOOKS_CALLGRAPH_EXPAND_FUNCTION c_expand_body

#undef LANG_HOOKS_TYPE_FOR_MODE
#define LANG_HOOKS_TYPE_FOR_MODE c_common_type_for_mode
#undef LANG_HOOKS_TYPE_FOR_SIZE
#define LANG_HOOKS_TYPE_FOR_SIZE c_common_type_for_size
#undef LANG_HOOKS_SIGNED_TYPE
#define LANG_HOOKS_SIGNED_TYPE c_common_signed_type
#undef LANG_HOOKS_UNSIGNED_TYPE
#define LANG_HOOKS_UNSIGNED_TYPE c_common_unsigned_type
#undef LANG_HOOKS_SIGNED_OR_UNSIGNED_TYPE
#define LANG_HOOKS_SIGNED_OR_UNSIGNED_TYPE c_common_signed_or_unsigned_type
#undef LANG_HOOKS_INCOMPLETE_TYPE_ERROR
#define LANG_HOOKS_INCOMPLETE_TYPE_ERROR c_incomplete_type_error
#undef LANG_HOOKS_TYPE_PROMOTES_TO
#define LANG_HOOKS_TYPE_PROMOTES_TO c_type_promotes_to
#undef LANG_HOOKS_REGISTER_BUILTIN_TYPE
#define LANG_HOOKS_REGISTER_BUILTIN_TYPE c_register_builtin_type

/* The C front end's scoping structure is very different from
   that expected by the language-independent code; it is best
   to disable all of pushlevel, poplevel, set_block, and getdecls.
   This means it must also provide its own write_globals.  */

#undef LANG_HOOKS_PUSHLEVEL
#define LANG_HOOKS_PUSHLEVEL lhd_do_nothing_i
#undef LANG_HOOKS_POPLEVEL
#define LANG_HOOKS_POPLEVEL lhd_do_nothing_iii_return_null_tree
#undef LANG_HOOKS_SET_BLOCK
#define LANG_HOOKS_SET_BLOCK lhd_do_nothing_t
#undef LANG_HOOKS_GETDECLS
#define LANG_HOOKS_GETDECLS lhd_return_null_tree_v
#undef LANG_HOOKS_WRITE_GLOBALS
#define LANG_HOOKS_WRITE_GLOBALS c_write_global_declarations

/* Hooks for tree gimplification.  */
#undef LANG_HOOKS_GIMPLIFY_EXPR
#define LANG_HOOKS_GIMPLIFY_EXPR c_gimplify_expr

#endif /* GCC_C_OBJC_COMMON */
