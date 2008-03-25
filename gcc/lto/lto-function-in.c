/* Read the gimple representation of a function and it's local
   variables from the memory mapped representation of a a .o file.

   Copyright 2006, 2007, 2008 Free Software Foundation, Inc.
   Contributed by Kenneth Zadeck <zadeck@naturalbridge.com>

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
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "toplev.h"
#include "tree.h"
#include "expr.h"
#include "flags.h"
#include "params.h"
#include "input.h"
#include "varray.h"
#include "hashtab.h"
#include "langhooks.h"
#include "basic-block.h"
#include "tree-iterator.h"
#include "tree-pass.h"
#include "tree-flow.h"
#include "cgraph.h"
#include "function.h"
#include "ggc.h"
#include "diagnostic.h"
#include "except.h"
#include "debug.h"
#include "vec.h"
#include "timevar.h"
#include "dwarf2asm.h"
#include "dwarf2out.h"
#include "output.h"
#include "lto-tags.h"
#include "lto.h"
#include <ctype.h>
#include "cpplib.h"

static enum tree_code tag_to_expr[LTO_tree_last_tag];

/* The number of flags that are defined for each tree code.  */
static int flags_length_for_code[NUM_TREE_CODES];

struct data_in
{
  /* That global decls and types.  */
  struct lto_file_decl_data* file_data;

  /* The offsets to decode the local_decls.  */
  int *local_decls_index;

  /* A table to reconstruct the unexpanded_vars_list.  */
  int *unexpanded_indexes;  

#ifdef LTO_STREAM_DEBUGGING
  /* The offsets to decode the local_decls debug info.  */
  int *local_decls_index_d; 
#endif

  /* The local var_decls and the parm_decls.  */
  tree *local_decls;

  /* All of the labels.  */
  tree *labels;

  /* The string table.  */
  const char * strings;
  
  /* The length of the string table.  */
  unsigned int strings_len;
  /* Number of named labels.  Used to find the index of unnamed labels
     since they share space with the named labels.  */
  unsigned int num_named_labels;  
  const char *current_file;
  int current_line;
  int current_col;
};


/* This hash table is used to hash the file names in the
   source_location field.  Unlike other structures here, this is a
   persistent structure whose data lives for the entire
   compilation.  */

struct string_slot {
  const char *s;
  unsigned int slot_num;
};


/* Returns a hash code for P.  */

static hashval_t
hash_string_slot_node (const void *p)
{
  const struct string_slot *ds = (const struct string_slot *) p;
  return (hashval_t) htab_hash_string (ds->s);
}


/* Returns nonzero if P1 and P2 are equal.  */

static int
eq_string_slot_node (const void *p1, const void *p2)
{
  const struct string_slot *ds1 =
    (const struct string_slot *) p1;
  const struct string_slot *ds2 =
    (const struct string_slot *) p2;

  return strcmp (ds1->s, ds2->s) == 0;
}

/* The table to hold the file_names.  */
static htab_t file_name_hash_table;

static tree
input_expr_operand (struct lto_input_block *, struct data_in *, struct function *, 
                    enum LTO_tags);
static tree
input_local_var (struct lto_input_block *, struct data_in *, struct function *, unsigned int i);


/* Return the next character of input from IB.  Abort if you
   overrun.  */

/* Read the string at LOC from the string table in DATA_IN.  */

static const char * 
input_string_internal (struct data_in *data_in, unsigned int loc, 
		       unsigned int *rlen)
{
  struct lto_input_block str_tab 
    = {data_in->strings, loc, data_in->strings_len};
  unsigned int len = lto_input_uleb128 (&str_tab);
  const char * result;

  *rlen = len;
  gcc_assert (str_tab.p + len <= data_in->strings_len);

  result = (const char *)(data_in->strings + str_tab.p);
  LTO_DEBUG_STRING (result, len);
  return result;
}


/* Read a STRING_CST at LOC from the string table in DATA_IN.  */

static tree
input_string (struct data_in *data_in, unsigned int loc)
{
  unsigned int len;
  const char * ptr = input_string_internal (data_in, loc, &len);
  return build_string (len, ptr);
}


/* Input a real constant of TYPE at LOC.  */

static tree
input_real (struct lto_input_block *ib, struct data_in *data_in, tree type)
{
  unsigned int loc;
  unsigned int len;
  const char * str;
  REAL_VALUE_TYPE value;
  static char buffer[1000];

  LTO_DEBUG_TOKEN ("real");
  loc = lto_input_uleb128 (ib);
  str = input_string_internal (data_in, loc, &len);
  /* Copy over to make sure real_from_string doesn't see peculiar
     trailing characters in the exponent.  */
  memcpy (buffer, str, len);
  buffer[len] = '\0';
  real_from_string (&value, buffer);
  return build_real (type, value);
}


/* Return the next tag in the input block IB.  */

static enum LTO_tags
input_record_start (struct lto_input_block *ib)
{
  enum LTO_tags tag = lto_input_1_unsigned (ib);

#ifdef LTO_STREAM_DEBUGGING
  if (tag)
    LTO_DEBUG_INDENT (tag);
  else
    LTO_DEBUG_WIDE ("U", 0);
#endif    
  return tag;
} 


/* Get the label referenced by the next token in IB.  */

static tree 
get_label_decl (struct data_in *data_in, struct lto_input_block *ib)
{
  int index = lto_input_sleb128 (ib);
  if (index >= 0)
    return data_in->labels[index];
  else
    return data_in->labels[data_in->num_named_labels - index];
}


/* Get the type referenced by the next token in IB.  */

static tree
input_type_ref (struct data_in *data_in, struct lto_input_block *ib)
{
  int index;

  LTO_DEBUG_TOKEN ("type");
  index = lto_input_uleb128 (ib);
  return data_in->file_data->types[index];
}

/* Set all of the FLAGS for NODE.  */
#define CLEAROUT (BITS_PER_LTO_FLAGS_TYPE - 1)


/* Read the tree flags for CODE from IB.  */

static lto_flags_type
input_tree_flags (struct lto_input_block *ib, enum tree_code code, bool force)
{
  lto_flags_type flags;

  if (force || TEST_BIT (lto_flags_needed_for, code))
    {
      LTO_DEBUG_TOKEN ("flags");
      flags = lto_input_widest_uint_uleb128 (ib);
      LTO_DEBUG_TREE_FLAGS (code, flags);
    }
  else
    flags = 0;
  return flags;
}


/* Set all of the flag bits inside EXPR by unpacking FLAGS.  */

static void
process_tree_flags (tree expr, lto_flags_type flags)
{
  enum tree_code code = TREE_CODE (expr);
  /* Shift the flags up so that the first flag is at the top of the
     flag word.  */
  flags <<= BITS_PER_LTO_FLAGS_TYPE - flags_length_for_code[code];

#define START_CLASS_SWITCH()              \
  {                                       \
                                          \
    switch (TREE_CODE_CLASS (code))       \
    {

#define START_CLASS_CASE(class)    case class:
#define ADD_CLASS_DECL_FLAG(flag_name)    \
  { expr->decl_common. flag_name = flags >> CLEAROUT; flags <<= 1; }
#define ADD_CLASS_EXPR_FLAG(flag_name)    \
  { expr->base. flag_name = flags >> CLEAROUT; flags <<= 1; }
#define END_CLASS_CASE(class)      break;
#define END_CLASS_SWITCH()                \
    default:                              \
      gcc_unreachable ();                 \
    }


#define START_EXPR_SWITCH()               \
    switch (code)			  \
    {
#define START_EXPR_CASE(code)    case code:
#define ADD_EXPR_FLAG(flag_name) \
  { expr->base. flag_name = (flags >> CLEAROUT); flags <<= 1; }
#define ADD_DECL_FLAG(flag_name) \
  { expr->decl_common. flag_name = flags >> CLEAROUT; flags <<= 1; }
#define ADD_VIS_FLAG(flag_name)  \
  { expr->decl_with_vis. flag_name = (flags >> CLEAROUT); flags <<= 1; }
#define ADD_VIS_FLAG_SIZE(flag_name,size)					\
  { expr->decl_with_vis. flag_name = (flags >> (BITS_PER_LTO_FLAGS_TYPE - size)); flags <<= size; }
#define END_EXPR_CASE(class)      break;
#define END_EXPR_SWITCH()                 \
    default:                              \
      gcc_unreachable ();                 \
    }                                     \
  }

#include "lto-tree-flags.def"

#undef START_CLASS_SWITCH
#undef START_CLASS_CASE
#undef ADD_CLASS_DECL_FLAG
#undef ADD_CLASS_EXPR_FLAG
#undef END_CLASS_CASE
#undef END_CLASS_SWITCH
#undef START_EXPR_SWITCH
#undef START_EXPR_CASE
#undef ADD_EXPR_FLAG
#undef ADD_DECL_FLAG
#undef ADD_VIS_FLAG
#undef ADD_VIS_FLAG_SIZE
#undef END_EXPR_CASE
#undef END_EXPR_SWITCH
}


/* Return the one true copy of STRING.  */

static const char *
canon_file_name (const char *string)
{
  void **slot;
  struct string_slot s_slot;
  s_slot.s = string;

  slot = htab_find_slot (file_name_hash_table, &s_slot, INSERT);
  if (*slot == NULL)
    {
      size_t len = strlen (string);
      char * saved_string = xmalloc (len + 1);
      strcpy (saved_string, string);

      struct string_slot *new_slot
	= xmalloc (sizeof (struct string_slot));

      new_slot->s = saved_string;
      *slot = new_slot;
      return saved_string;
    }
  else
    {
      struct string_slot *old_slot = (struct string_slot *)*slot;
      return old_slot->s;
    }
}


/* Based on the FLAGS, read in a file, a line and a col into the
   fields in DATA_IN.  */

static bool
input_line_info (struct lto_input_block *ib, struct data_in *data_in, 
		 lto_flags_type flags)
{
  if (flags & LTO_SOURCE_FILE)
    {
      unsigned int len;
      if (data_in->current_file)
	linemap_add (line_table, LC_LEAVE, false, NULL, 0);

      LTO_DEBUG_TOKEN ("file");
      data_in->current_file 
	= canon_file_name (input_string_internal (data_in, lto_input_uleb128 (ib), &len));
    }
  if (flags & LTO_SOURCE_LINE)
    {
      LTO_DEBUG_TOKEN ("line");
      data_in->current_line = lto_input_uleb128 (ib);

      if (!(flags & LTO_SOURCE_FILE))
	linemap_line_start (line_table, data_in->current_line, 80);
    }
  if (flags & LTO_SOURCE_FILE)
    linemap_add (line_table, LC_ENTER, false, data_in->current_file, data_in->current_line);

  if (flags & LTO_SOURCE_COL)
    {
      LTO_DEBUG_TOKEN ("col");
      data_in->current_col = lto_input_uleb128 (ib);
    }
  return (flags & LTO_SOURCE_HAS_LOC) != 0;
}


/* Set the line info stored in DATA_IN for NODE.  */

static void
set_line_info (struct data_in *data_in, tree node)
{
  if (EXPR_P (node))
    LINEMAP_POSITION_FOR_COLUMN (EXPR_CHECK (node)->exp.locus, line_table, data_in->current_col);
  else if (GIMPLE_STMT_P (node))
    LINEMAP_POSITION_FOR_COLUMN (GIMPLE_STMT_LOCUS (node), line_table, data_in->current_col);
  else if (DECL_P (node))
    LINEMAP_POSITION_FOR_COLUMN (DECL_SOURCE_LOCATION (node), line_table, data_in->current_col);
}


/* Clear the line info stored in DATA_IN.  */

static void
clear_line_info (struct data_in *data_in)
{
  if (data_in->current_file)
    linemap_add (line_table, LC_LEAVE, false, NULL, 0);
  data_in->current_file = NULL;
  data_in->current_line = 0;
  data_in->current_col = 0;
}


/* Read a node in the gimple tree from IB.  The TAG has already been
   read.  */

static tree
input_expr_operand (struct lto_input_block *ib, struct data_in *data_in, 
		    struct function *fn, enum LTO_tags tag)
{
  enum tree_code code = tag_to_expr[tag];
  tree type = NULL_TREE;
  lto_flags_type flags;
  gcc_assert (code);
  tree result = NULL_TREE;
  bool needs_line_set = false;
  
  if (TEST_BIT (lto_types_needed_for, code))
    type = input_type_ref (data_in, ib);

  flags = input_tree_flags (ib, code, false);

  if (IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (code))
      || IS_GIMPLE_STMT_CODE_CLASS(TREE_CODE_CLASS (code)))
    needs_line_set = input_line_info (ib, data_in, flags);

  switch (code)
    {
    case COMPLEX_CST:
      {
	tree elt_type = input_type_ref (data_in, ib);

	result = build0 (code, type);
	if (tag == LTO_complex_cst1)
	  {
	    TREE_REALPART (result) = input_real (ib, data_in, elt_type);
	    TREE_IMAGPART (result) = input_real (ib, data_in, elt_type);
	  }
	else
	  {
	    TREE_REALPART (result) = lto_input_integer (ib, elt_type);
	    TREE_IMAGPART (result) = lto_input_integer (ib, elt_type);
	  }
      }
      break;

    case INTEGER_CST:
      result = lto_input_integer (ib, type);
      break;

    case REAL_CST:
      result = input_real (ib, data_in, type);
      break;

    case STRING_CST:
      result = input_string (data_in, lto_input_uleb128 (ib));
      TREE_TYPE (result) = type;
      break;

    case IDENTIFIER_NODE:
      {
	unsigned int len;
	const char * ptr = input_string_internal (data_in, lto_input_uleb128 (ib), &len);
	result = get_identifier_with_length (ptr, len);
      }
      break;

    case VECTOR_CST:
      {
	tree chain = NULL_TREE;
	int len = lto_input_uleb128 (ib);
	tree elt_type = input_type_ref (data_in, ib);

	if (len && tag == LTO_vector_cst1)
	  {
	    int i;
	    tree last 
	      = build_tree_list (NULL_TREE, input_real (ib, data_in, elt_type));
	    chain = last; 
	    for (i = 1; i < len; i++)
	      {
		tree t 
		  = build_tree_list (NULL_TREE, input_real (ib, data_in, elt_type));
		TREE_CHAIN (last) = t;
		last = t;
	      }
	  }
	else
	  {
	    int i;
	    tree last = build_tree_list (NULL_TREE, lto_input_integer (ib, elt_type));
	    chain = last; 
	    for (i = 1; i < len; i++)
	      {
		tree t 
		  = build_tree_list (NULL_TREE, lto_input_integer (ib, elt_type));
		TREE_CHAIN (last) = t;
		last = t;
	      }
	  }
	result = build_vector (type, chain);
      }
      break;

    case CASE_LABEL_EXPR:
      {
	int variant = tag - LTO_case_label_expr0;
	tree op0 = NULL_TREE;
	tree op1 = NULL_TREE;
	
	if (variant & 0x1)
	  op0 = input_expr_operand (ib, data_in, fn, 
				    input_record_start (ib));

	if (variant & 0x2)
	  op1 = input_expr_operand (ib, data_in, fn, 
				    input_record_start (ib));

	result = build3 (code, void_type_node, 
			 op0, op1, get_label_decl (data_in, ib));
      }
      break;

    case CONSTRUCTOR:
      {
	VEC(constructor_elt,gc) *vec = NULL;
	unsigned int len = lto_input_uleb128 (ib);
	
	if (len)
	  {
	    unsigned int i = 0;
	    vec = VEC_alloc (constructor_elt, gc, len);
	    for (i = 0; i < len; i++)
	      {
		tree purpose = NULL_TREE;
		tree value;
		constructor_elt *elt; 
		enum LTO_tags ctag = input_record_start (ib);
		
		if (ctag)
		  purpose = input_expr_operand (ib, data_in, fn, ctag);
		
		value = input_expr_operand (ib, data_in, fn, input_record_start (ib));
		elt = VEC_quick_push (constructor_elt, vec, NULL);
		elt->index = purpose;
		elt->value = value;
	      }
	  }
	result = build_constructor (type, vec);
      }
      break;

    case SSA_NAME:
      result = VEC_index (tree, SSANAMES (fn), lto_input_uleb128 (ib));
      add_referenced_var (SSA_NAME_VAR (result));
      break;

    case CONST_DECL:
      /* Just ignore these, Mark will make them disappear.  */
      break;

    case FIELD_DECL:
      result = data_in->file_data->field_decls [lto_input_uleb128 (ib)];
      break;

    case FUNCTION_DECL:
      result = data_in->file_data->fn_decls [lto_input_uleb128 (ib)];
      gcc_assert (result);
      break;

    case TYPE_DECL:
      result = data_in->file_data->type_decls [lto_input_uleb128 (ib)];
      gcc_assert (result);
      break;

    case VAR_DECL:
    case PARM_DECL:
      if (tag == LTO_var_decl1)
        {
          /* Static or externs are here.  */
          result = data_in->file_data->var_decls [lto_input_uleb128 (ib)];
	  varpool_mark_needed_node (varpool_node (result));
        }
      else 
	{
	  /* Locals are here.  */
	  int lv_index = lto_input_uleb128 (ib);
	  result = data_in->local_decls [lv_index];
	  if (result == NULL)
	    {
	      /* Create a context to read the local variable so that
		 it does not disturb the position of the code that is
		 calling for the local variable.  This allows locals
		 to refer to other locals.  */
	      struct lto_input_block lib;

#ifdef LTO_STREAM_DEBUGGING
	      struct lto_input_block *current = lto_debug_context.current_data;
	      struct lto_input_block debug;
	      int current_indent = lto_debug_context.indent;

	      debug.data = current->data;
	      debug.len = current->len;
	      debug.p = data_in->local_decls_index_d[lv_index];

	      lto_debug_context.indent = 0;
	      lto_debug_context.current_data = &debug;
	      lto_debug_context.tag_names = LTO_tree_tag_names;
#endif
	      lib.data = ib->data;
	      lib.len = ib->len;
	      lib.p = data_in->local_decls_index[lv_index];

	      result = input_local_var (&lib, data_in, fn, lv_index); 
	      data_in->local_decls [lv_index] = result;

#ifdef LTO_STREAM_DEBUGGING
	      lto_debug_context.indent = current_indent;
	      lto_debug_context.current_data = current;
	      lto_debug_context.tag_names = LTO_tree_tag_names;
#endif

	    }
	}
      break;

    case LABEL_DECL:
      result = get_label_decl (data_in, ib);
      break;

    case LABEL_EXPR:
      result = build1 (code, void_type_node, get_label_decl (data_in, ib));
      if (!DECL_CONTEXT (LABEL_EXPR_LABEL (result)))
	DECL_CONTEXT (LABEL_EXPR_LABEL (result)) = fn->decl;
      break;

    case COND_EXPR:
      if (tag == LTO_cond_expr0)
	{
	  tree op0;
	  tree op1;
	  tree op2;
	  op0 = input_expr_operand (ib, data_in, fn, 
				    input_record_start (ib));
	  op1 = input_expr_operand (ib, data_in, fn, 
				    input_record_start (ib));
	  op2 = input_expr_operand (ib, data_in, fn, 
				    input_record_start (ib));
	  result = build3 (code, type, op0, op1, op2);
	}
      else
	{
	  tree op0;
	  op0 = input_expr_operand (ib, data_in, fn, 
				    input_record_start (ib));
	  result = build3 (code, type, op0, NULL, NULL);
	}
      break;
      

    case RESULT_DECL:
      result = DECL_RESULT (current_function_decl);
      add_referenced_var (result);
      break;

    case COMPONENT_REF:
      {
	tree op0;
	tree op1;
	op0 = input_expr_operand (ib, data_in, fn, 
				  input_record_start (ib));
	op1 = input_expr_operand (ib, data_in, fn,
				  input_record_start (ib));
  
	/* Ignore 3 because it can be recomputed.  */
	result = build3 (code, type, op0, op1, NULL_TREE);
      }
      break;

    case CALL_EXPR:
      {
	unsigned int i;
	unsigned int count = lto_input_uleb128 (ib);
	tree op1;
	tree op2 = NULL_TREE;

	/* The call chain.  */
	if (tag == LTO_call_expr1)
	  op2 = input_expr_operand (ib, data_in, fn, 
				    input_record_start (ib));

	/* The callee.  */
	op1 = input_expr_operand (ib, data_in, fn, 
				  input_record_start (ib));

	result = build_vl_exp (code, count);
	CALL_EXPR_FN (result) = op1;
	CALL_EXPR_STATIC_CHAIN (result) = op2;
	for (i = 3; i < count; i++)
	  TREE_OPERAND (result, i) 
	    = input_expr_operand (ib, data_in, fn, 
				  input_record_start (ib));
        TREE_TYPE (result) = type;
      }
      break;

    case BIT_FIELD_REF:
      {
	tree op0;
	tree op1;
	tree op2;
	if (tag == LTO_bit_field_ref1)
	  {
	    op1 = build_int_cst_wide (sizetype, lto_input_uleb128 (ib), 0);
	    op2 = build_int_cst_wide (bitsizetype, lto_input_uleb128 (ib), 0);
	    op0 = input_expr_operand (ib, data_in, fn,
				      input_record_start (ib));
	  }
	else
	  {
	    op0 = input_expr_operand (ib, data_in, fn,
				      input_record_start (ib));
	    op1 = input_expr_operand (ib, data_in, fn,
				      input_record_start (ib));
	    op2 = input_expr_operand (ib, data_in, fn,
				      input_record_start (ib));
	  }
	result = build3 (code, type, op0, op1, op2);
      }
      break;

    case ARRAY_REF:
    case ARRAY_RANGE_REF:
      /* Ignore operands 2 and 3 for ARRAY_REF and ARRAY_RANGE REF
	 because they can be recomputed.  */
      {
	tree op0 = input_expr_operand (ib, data_in, fn, 
				       input_record_start (ib));
	tree op1 = input_expr_operand (ib, data_in, fn,
				       input_record_start (ib));
	result = build4 (code, type, op0, op1, NULL_TREE, NULL_TREE);
      }
      break;

    case ASM_EXPR:
      {
	tree str = input_string (data_in, lto_input_uleb128 (ib));
	tree ins = NULL_TREE;
	tree outs = NULL_TREE;
	tree clobbers = NULL_TREE;
	tree tl;

	tag = input_record_start (ib);
	if (tag)
	  ins = input_expr_operand (ib, data_in, fn, tag); 
	tag = input_record_start (ib);
	if (tag)
	  outs = input_expr_operand (ib, data_in, fn, tag); 
	tag = input_record_start (ib);
	if (tag)
	  clobbers = input_expr_operand (ib, data_in, fn, tag);

	result = build4 (code, void_type_node, str, outs, ins, clobbers);

	for (tl = ASM_OUTPUTS (result); tl; tl = TREE_CHAIN (tl))
	  if (TREE_CODE (TREE_VALUE (tl)) == SSA_NAME)
	    SSA_NAME_DEF_STMT (TREE_VALUE (tl)) = result;
      }
      break;

    case RESX_EXPR:
      result = build1 (code, void_type_node, lto_input_integer (ib, NULL_TREE));
      break;

    case RETURN_EXPR:
      switch (tag) 
	{
	case LTO_return_expr0:
	  result = build1 (code, type, NULL_TREE);
	  break;
	  
	case LTO_return_expr1:
          {
            enum LTO_tags tag = input_record_start (ib);
            tree op0;

            if (tag)
              op0 = input_expr_operand (ib, data_in, fn, tag);
            else
	      {
		op0 = DECL_RESULT (current_function_decl);
		add_referenced_var (op0);
	      }

            result = build1 (code, type, op0);

	    if ((TREE_CODE (op0) == GIMPLE_MODIFY_STMT)
		&& (TREE_CODE (GIMPLE_STMT_OPERAND (op0, 0)) == SSA_NAME))
		SSA_NAME_DEF_STMT (GIMPLE_STMT_OPERAND (op0, 0)) = result;
          }
	  break;
	  
	case LTO_return_expr2:
	  {
	    tree op0 = input_expr_operand (ib, data_in, fn,
					   input_record_start (ib));
	    tree op1 = input_expr_operand (ib, data_in, fn,
					   input_record_start (ib));
	    result = build1 (code, type, 
			     build2 (MODIFY_EXPR, NULL_TREE, op0, op1));
	  }
	  break;

        default:
          gcc_unreachable ();
	}
      break;

    case RANGE_EXPR:
      {
	tree op0 = lto_input_integer (ib, input_type_ref (data_in, ib));
	tree op1 = lto_input_integer (ib, input_type_ref (data_in, ib));
	result = build2 (RANGE_EXPR, sizetype, op0, op1);
      }
      break;

    case GIMPLE_MODIFY_STMT:
      {
	tree op0 = input_expr_operand (ib, data_in, fn, 
				       input_record_start (ib));
	tree op1 = input_expr_operand (ib, data_in, fn,
				       input_record_start (ib));

	result = build_gimple_modify_stmt (op0, op1);
	if (TREE_CODE (op0) == SSA_NAME)
	  SSA_NAME_DEF_STMT (op0) = result;
      }
      break;

    case SWITCH_EXPR:
      {
	unsigned int len = lto_input_uleb128 (ib);
	unsigned int i;
	tree op0 = input_expr_operand (ib, data_in, fn, 
				       input_record_start (ib));
	tree op2 = make_tree_vec (len);
	
	for (i = 0; i < len; ++i)
	  TREE_VEC_ELT (op2, i) 
	    = input_expr_operand (ib, data_in, fn,
				  input_record_start (ib));
	result = build3 (code, type, op0, NULL_TREE, op2);
      }
      break;

    case TREE_LIST:
      {
	unsigned int count = lto_input_uleb128 (ib);
	tree next = NULL;

	result = NULL_TREE;
	while (count--)
	  {
	    tree value;
	    tree purpose;
	    tree elt;
	    enum LTO_tags tag = input_record_start (ib);

	    if (tag)
	      value = input_expr_operand (ib, data_in, fn, tag);
	    else 
	      value = NULL_TREE;
	    tag = input_record_start (ib);
	    if (tag)
	      purpose = input_expr_operand (ib, data_in, fn, tag);
	    else 
	      purpose = NULL_TREE;

	    elt = build_tree_list (purpose, value);
	    if (result)
	      TREE_CHAIN (next) = elt;
	    else
	      /* Save the first one.  */
	      result = elt;
	    next = elt;
	  }
      }
      break;

      /* This is the default case. All of the cases that can be done
	 completely mechanically are done here.  */
#define SET_NAME(a,b)
#define TREE_SINGLE_MECHANICAL_TRUE
#define MAP_EXPR_TAG(expr,tag) case expr:
#include "lto-tree-tags.def"
#undef MAP_EXPR_TAG
#undef TREE_SINGLE_MECHANICAL_TRUE
#undef SET_NAME
      {
	tree ops[7];
	int len = TREE_CODE_LENGTH (code);
	int i;
	for (i = 0; i<len; i++)
	  ops[i] = input_expr_operand (ib, data_in, fn, 
				       input_record_start (ib));
	switch (len)
	  {
	  case 0:
	    result = build0 (code, type);
	    break;
	  case 1:
	    result = build1 (code, type, ops[0]);
	    break;
	  case 2:
	    result = build2 (code, type, ops[0], ops[1]);
	    break;
	  case 3:
	    result = build3 (code, type, ops[0], ops[1], ops[2]);
	    break;
	  case 4:
	    result = build4 (code, type, ops[0], ops[1], ops[2], ops[3]);
	    break;
	  case 5:
	    result = build5 (code, type, ops[0], ops[1], ops[2], ops[3], 
			     ops[4]);
	    break;
            /* No 'case 6'.  */
	  case 7:
	    result = build7 (code, type, ops[0], ops[1], ops[2], ops[3], 
			     ops[4], ops[5], ops[6]);
	    break;
	  default:
	    gcc_unreachable ();
	  }
      }
      break;
      /* This is the error case, these are type codes that will either
	 never happen or that we have not gotten around to dealing
	 with are here.  */
    case BIND_EXPR:
    case BLOCK:
    case CATCH_EXPR:
    case EH_FILTER_EXPR:
    case NAME_MEMORY_TAG:
    case OMP_CONTINUE:
    case OMP_CRITICAL:
    case OMP_FOR:
    case OMP_MASTER:
    case OMP_ORDERED:
    case OMP_PARALLEL:
    case OMP_RETURN:
    case OMP_SECTIONS:
    case OMP_SINGLE:
    case STRUCT_FIELD_TAG:
    case SYMBOL_MEMORY_TAG:
    case TARGET_MEM_REF:
    case TRY_CATCH_EXPR:
    case TRY_FINALLY_EXPR:
    default:
      /* We cannot have forms that are not explicity handled.  So when
	 this is triggered, there is some form that is not being
	 output.  */
      gcc_unreachable ();
    }

  LTO_DEBUG_UNDENT();
  if (flags)
    process_tree_flags (result, flags);

  if (needs_line_set)
    set_line_info (data_in, result);

  /* It is not enought to just put the flags back as we serialized
     them.  There are side effects to the buildN functions which play
     with the flags to the point that we just have to call this here
     to get it right.  */
  if (code == ADDR_EXPR)
    {
      tree x;

      /* Following tree-cfg.c:verify_expr: skip any references and
	 ensure that any variable used as a prefix is marked
	 addressable.  */
      for (x = TREE_OPERAND (result, 0);
	   handled_component_p (x);
	   x = TREE_OPERAND (x, 0))
	;

      if (TREE_CODE (x) == VAR_DECL || TREE_CODE (x) == PARM_DECL)
	TREE_ADDRESSABLE (x) = 1;
      else if (TREE_CODE (x) == FUNCTION_DECL)
	cgraph_mark_needed_node (cgraph_node (x));

      recompute_tree_invariant_for_addr_expr (result);
    }
  return result;
}


/* Load NAMED_COUNT named labels and constuct UNNAMED_COUNT unnamed
   labels from DATA segment SIZE bytes long using DATA_IN.  */

static void 
input_labels (struct lto_input_block *ib, struct data_in *data_in, 
	      unsigned int named_count, unsigned int unnamed_count)
{
  unsigned int i;

  clear_line_info (data_in);
  /* The named and unnamed labels share the same array.  In the lto
     code, the unnamed labels have a negative index.  Their position
     in the array can be found by subtracting that index from the
     number of named labels.  */
  data_in->labels = xcalloc (named_count + unnamed_count, sizeof (tree));
  for (i = 0; i < named_count; i++)
    {
      unsigned int name_index = lto_input_uleb128 (ib);
      unsigned int len;
      const char *s = input_string_internal (data_in, name_index, &len);
      tree name = get_identifier_with_length (s, len);
      data_in->labels[i] = build_decl (LABEL_DECL, name, void_type_node);
    }

  for (i = 0; i < unnamed_count; i++)
    data_in->labels[i + named_count] 
      = build_decl (LABEL_DECL, NULL_TREE, void_type_node);
 }


/* Input the local var index table.  */


static void
input_local_vars_index (struct lto_input_block *ib, struct data_in *data_in, 
			unsigned int count)
{
  unsigned int i;
  data_in->local_decls_index = xcalloc (count, sizeof (unsigned int));
#ifdef LTO_STREAM_DEBUGGING
  data_in->local_decls_index_d = xcalloc (count, sizeof (unsigned int));
#endif

  for (i = 0; i < count; i++)
    {
      data_in->local_decls_index[i] = lto_input_uleb128 (ib); 
#ifdef LTO_STREAM_DEBUGGING
      data_in->local_decls_index_d[i] = lto_input_uleb128 (ib); 
#endif
    }
}


/* Input local var I for FN from IB.  */

static tree
input_local_var (struct lto_input_block *ib, struct data_in *data_in, 
		 struct function *fn, unsigned int i)
{
  enum LTO_tags tag;
  unsigned int variant;
  bool is_var;
  unsigned int name_index;
  tree name;
  tree type;
  lto_flags_type flags;
  tree result;
  tree context;

  /* The line number info needs to be reset for each local var since
     they are read in random order.  */
  clear_line_info (data_in);

  tag = input_record_start (ib);
  variant = tag & 0xF;
  is_var = ((tag & 0xFFF0) == LTO_local_var_decl_body0);
  
  name_index = lto_input_uleb128 (ib);
  if (name_index)
    {
      unsigned int len;
      const char *s = input_string_internal (data_in, name_index, &len);
      name = get_identifier_with_length (s, len);
    }
  else 
    name = NULL_TREE;
  
  type = input_type_ref (data_in, ib);
  gcc_assert (type);
  
  if (is_var)
    result = build_decl (VAR_DECL, name, type);
  else
    result = build_decl (PARM_DECL, name, type);

  data_in->local_decls[i] = result;
  
  if (is_var)
    {
      int index;

      LTO_DEBUG_INDENT_TOKEN ("init");
      tag = input_record_start (ib);
      if (tag)
	DECL_INITIAL (result) = input_expr_operand (ib, data_in, fn, tag);

      LTO_DEBUG_INDENT_TOKEN ("unexpanded index");
      index = lto_input_sleb128 (ib);
      if (index != -1)
	data_in->unexpanded_indexes[index] = i;
    }
  else
    {
      DECL_ARG_TYPE (result) = input_type_ref (data_in, ib);
      LTO_DEBUG_TOKEN ("chain");
      tag = input_record_start (ib);
      if (tag)
	TREE_CHAIN (result) = input_expr_operand (ib, data_in, fn, tag);
      else 
	TREE_CHAIN (result) = NULL_TREE;
    }

  flags = input_tree_flags (ib, 0, true);
  if (input_line_info (ib, data_in, flags))
    set_line_info (data_in, result);

  LTO_DEBUG_TOKEN ("context");
  context = input_expr_operand (ib, data_in, fn, input_record_start (ib));
  if (TYPE_P (context))
    DECL_CONTEXT (result) = TYPE_NAME (context);
  else
    DECL_CONTEXT (result) = context;
  
  LTO_DEBUG_TOKEN ("align");
  DECL_ALIGN (result) = lto_input_uleb128 (ib);
  LTO_DEBUG_TOKEN ("size");
  DECL_SIZE (result) = input_expr_operand (ib, data_in, fn, input_record_start (ib));

  if (variant & 0x1)
    {
      LTO_DEBUG_TOKEN ("attributes");
      DECL_ATTRIBUTES (result) 
	= input_expr_operand (ib, data_in, fn, input_record_start (ib));
    }
  if (variant & 0x2)
    DECL_SIZE_UNIT (result) 
      = input_expr_operand (ib, data_in, fn, input_record_start (ib));
  if (variant & 0x4)
    SET_DECL_DEBUG_EXPR (result, 
			 input_expr_operand (ib, data_in, fn, 
					     input_record_start (ib)));
  if (variant & 0x8)
    DECL_ABSTRACT_ORIGIN (result) 
      = input_expr_operand (ib, data_in, fn, input_record_start (ib));
  
  process_tree_flags (result, flags);
  LTO_DEBUG_UNDENT();

  return result;
}


/* Load COUNT local var_decls and parm_decls from a DATA segment SIZE
   bytes long using DATA_IN.  */

static void 
input_local_vars (struct lto_input_block *ib, struct data_in *data_in, 
		  struct function *fn, unsigned int count)
{
  int i;
  unsigned int tag;

  data_in->unexpanded_indexes = xcalloc (count, sizeof (int));
  data_in->local_decls = xcalloc (count, sizeof (tree*));

  memset (data_in->unexpanded_indexes, -1, count * sizeof (int));

  /* Recreate the unexpanded_var_list.  Put the statics at the end.*/
  fn->unexpanded_var_list = NULL;
  LTO_DEBUG_TOKEN ("local statics");
  tag = input_record_start (ib);
  
  while (tag)
    {
      tree var = input_expr_operand (ib, data_in, fn, tag);
      fn->unexpanded_var_list 
	= tree_cons (NULL_TREE, var, fn->unexpanded_var_list);

      if (lto_input_uleb128 (ib))
	DECL_CONTEXT (var) = fn->decl;
	
      /* DECL_INITIAL.  */
      tag = input_record_start (ib);
      if (tag)
	DECL_INITIAL (var) = input_expr_operand (ib, data_in, fn, tag);

      /* Statics never have external visibility.  */
      DECL_EXTERNAL (var) = 0;

      /* Next static.  */
      tag = input_record_start (ib);
    }

  LTO_DEBUG_TOKEN ("local vars");
  for (i = 0; i < (int)count; i++)
    /* Some local decls may have already been read in if they are used
       as part of a previous local_decl.  */
    if (!data_in->local_decls[i])
      {
#ifdef LTO_STREAM_DEBUGGING
	((struct lto_input_block *)lto_debug_context.current_data)->p 
	  = data_in->local_decls_index_d[i]; 
#endif
	ib->p = data_in->local_decls_index[i];
	input_local_var (ib, data_in, fn, i);
      }

  /* Add the regular locals in the proper order.  */
  for (i = count - 1; i >= 0; i--)
    if (data_in->unexpanded_indexes[i] != -1)
      fn->unexpanded_var_list 
	= tree_cons (NULL_TREE, 
		     data_in->local_decls[data_in->unexpanded_indexes[i]],
		     fn->unexpanded_var_list);

  free (data_in->unexpanded_indexes);
  data_in->unexpanded_indexes = NULL;
}


/* Read the exception table.  */

static void
input_eh_regions (struct lto_input_block *ib, 
		  struct function *fn ATTRIBUTE_UNUSED, 
		  struct data_in *data_in ATTRIBUTE_UNUSED)
{
  /* Not ready to read exception records yet.  */
  lto_input_uleb128 (ib);
}


/* Make a new basic block at INDEX in FN.  */

static basic_block
make_new_block (struct function *fn, unsigned int index)
{
  basic_block bb = alloc_block ();
  bb->index = index;
  SET_BASIC_BLOCK_FOR_FUNCTION (fn, index, bb);
  bb->il.tree = GGC_CNEW (struct tree_bb_info);
  n_basic_blocks_for_function (fn)++;
  bb->flags = 0;
  set_bb_stmt_list (bb, alloc_stmt_list ());
  return bb;
}


/* Set up the cfg for THIS_FUN.  */

static void 
input_cfg (struct lto_input_block *ib, struct function *fn)
{
  unsigned int bb_count;
  basic_block p_bb;
  unsigned int i;
  int index;

  init_empty_tree_cfg_for_function (fn);
  init_ssa_operands ();

  LTO_DEBUG_TOKEN ("lastbb");
  bb_count = lto_input_uleb128 (ib);

  last_basic_block_for_function (fn) = bb_count;
  if (bb_count > VEC_length (basic_block,
			     basic_block_info_for_function (fn)))
    VEC_safe_grow_cleared (basic_block, gc,
			   basic_block_info_for_function (fn), bb_count);
  if (bb_count > VEC_length (basic_block,
			     label_to_block_map_for_function (fn)))
    VEC_safe_grow_cleared (basic_block, gc, 
			   label_to_block_map_for_function (fn), bb_count);

  LTO_DEBUG_TOKEN ("bbindex");
  index = lto_input_sleb128 (ib);
  while (index != -1)
    {
      basic_block bb = BASIC_BLOCK_FOR_FUNCTION (fn, index);
      unsigned int edge_count;

      if (bb == NULL)
	bb = make_new_block (fn, index);

      LTO_DEBUG_TOKEN ("edgecount");
      edge_count = lto_input_uleb128 (ib);

      /* Connect up the cfg.  */
      for (i = 0; i < edge_count; i++)
	{
	  unsigned int dest_index;
	  unsigned int edge_flags;
	  basic_block dest;

	  LTO_DEBUG_TOKEN ("dest");
	  dest_index = lto_input_uleb128 (ib);
	  LTO_DEBUG_TOKEN ("eflags");
	  edge_flags = lto_input_uleb128 (ib);
	  dest = BASIC_BLOCK_FOR_FUNCTION (fn, dest_index);

	  if (dest == NULL) 
	    dest = make_new_block (fn, dest_index);
	  make_edge (bb, dest, edge_flags);
	}

      LTO_DEBUG_TOKEN ("bbindex");
      index = lto_input_sleb128 (ib);
    }

  p_bb = ENTRY_BLOCK_PTR_FOR_FUNCTION(fn);
  LTO_DEBUG_TOKEN ("bbchain");
  index = lto_input_sleb128 (ib);
  while (index != -1)
    {
      basic_block bb = BASIC_BLOCK_FOR_FUNCTION (fn, index);
      bb->prev_bb = p_bb;
      p_bb->next_bb = bb;
      p_bb = bb;
      LTO_DEBUG_TOKEN ("bbchain");
      index = lto_input_sleb128 (ib);
    }
}


/* Input the next phi function for BB.  */

static tree
input_phi (struct lto_input_block *ib, basic_block bb, 
	   struct data_in *data_in, struct function *fn)
{
  lto_flags_type flags = input_tree_flags (ib, PHI_NODE, false);

  tree phi_result = VEC_index (tree, SSANAMES (fn), lto_input_uleb128 (ib));
  int len = EDGE_COUNT (bb->preds);
  int i;
  tree result = create_phi_node (phi_result, bb);

  SSA_NAME_DEF_STMT (phi_result) = result;

  /* We have to go thru a lookup process here because the preds in the
     reconstructed graph are generally in a different order than they
     were in the original program.  */
  for (i = 0; i < len; i++)
    {
      tree def = input_expr_operand (ib, data_in, fn, input_record_start (ib));
      int src_index = lto_input_uleb128 (ib);
      basic_block sbb = BASIC_BLOCK_FOR_FUNCTION (fn, src_index);
      
      edge e = NULL;
      int j;
      
      for (j = 0; j < len; j++)
	if (EDGE_PRED (bb, j)->src == sbb)
	  {
	    e = EDGE_PRED (bb, j);
	    break;
	  }

      add_phi_arg (result, def, e); 
    }

  if (flags)
    process_tree_flags (result, flags);

  LTO_DEBUG_UNDENT();

  return result;
}


/* Read in the ssa_names array from IB.  */

static void
input_ssa_names (struct lto_input_block *ib, struct data_in *data_in, struct function *fn)
{
  unsigned int i;
  int size = lto_input_uleb128 (ib);

  init_ssanames (fn, size);
  i = lto_input_uleb128 (ib);

  while (i)
    {
      tree ssa_name;
      tree name;
      lto_flags_type flags;

      /* Skip over the elements that had been freed.  */
      while (VEC_length (tree, SSANAMES (fn)) < i)
	VEC_quick_push (tree, SSANAMES (fn), NULL_TREE);

      name = input_expr_operand (ib, data_in, fn, input_record_start (ib));
      ssa_name = make_ssa_name (fn, name, build_empty_stmt ());

      flags = input_tree_flags (ib, 0, true);
      process_tree_flags (ssa_name, flags);
      if (SSA_NAME_IS_DEFAULT_DEF (ssa_name))
	set_default_def (SSA_NAME_VAR (ssa_name), ssa_name);
      i = lto_input_uleb128 (ib);
    } 
}

 
/* Read in the next basic block.  */

static void
input_bb (struct lto_input_block *ib, enum LTO_tags tag, 
	  struct data_in *data_in, struct function *fn)
{
  unsigned int index;
  basic_block bb;
  block_stmt_iterator bsi;

  LTO_DEBUG_TOKEN ("bbindex");
  index = lto_input_uleb128 (ib);
  bb = BASIC_BLOCK_FOR_FUNCTION (fn, index);

  /* LTO_bb1 has stmts, LTO_bb0 does not.  */
  if (tag == LTO_bb0)
    {
      LTO_DEBUG_UNDENT();
      return;
    }

  bsi = bsi_start (bb);
  LTO_DEBUG_INDENT_TOKEN ("stmt");
  tag = input_record_start (ib);
  while (tag)
    {
      tree stmt = input_expr_operand (ib, data_in, fn, tag);
      bsi_insert_after (&bsi, stmt, BSI_NEW_STMT);
      LTO_DEBUG_INDENT_TOKEN ("stmt");
      tag = input_record_start (ib);
    /* FIXME, add code to handle the exception.  */
    }

  LTO_DEBUG_INDENT_TOKEN ("phi");  
  tag = input_record_start (ib);
  while (tag)
    {
      input_phi (ib, bb, data_in, fn);
      LTO_DEBUG_INDENT_TOKEN ("phi");
      tag = input_record_start (ib);
    }

  LTO_DEBUG_UNDENT();
}


/* Fill in the body of FN_DECL.  */

static void
input_function (tree fn_decl, struct data_in *data_in, 
		struct lto_input_block *ib)
{
  struct function *fn = DECL_STRUCT_FUNCTION (fn_decl);
  enum LTO_tags tag = input_record_start (ib);
  tree *stmts;
  struct cgraph_edge *cedge; 
  basic_block bb;

  DECL_INITIAL (fn_decl) = DECL_SAVED_TREE (fn_decl) = make_node (BLOCK);
  BLOCK_ABSTRACT_ORIGIN (DECL_SAVED_TREE (fn_decl)) = fn_decl;
  clear_line_info (data_in);

  tree_register_cfg_hooks ();
  gcc_assert (tag == LTO_function);

  input_eh_regions (ib, fn, data_in);

  LTO_DEBUG_INDENT_TOKEN ("decl_arguments");
  tag = input_record_start (ib);
  if (tag)
    DECL_ARGUMENTS (fn_decl) = input_expr_operand (ib, data_in, fn, tag); 

  LTO_DEBUG_INDENT_TOKEN ("decl_context");
  tag = input_record_start (ib);
  if (tag)
    DECL_CONTEXT (fn_decl) = input_expr_operand (ib, data_in, fn, tag); 

  tag = input_record_start (ib);
  while (tag)
    {
      input_bb (ib, tag, data_in, fn);
      tag = input_record_start (ib);
    }

  /* Fix up the call stmts that are mentioned in the cgraph_edges.  */
  renumber_gimple_stmt_uids ();
  stmts = xcalloc (gimple_stmt_max_uid(fn), sizeof (tree));
  FOR_ALL_BB (bb)
    {
      block_stmt_iterator bsi;
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree stmt = bsi_stmt (bsi);
	  stmts [gimple_stmt_uid (stmt)] = stmt;
#ifdef LOCAL_TRACE
	  fprintf (stderr, "%d = ", gimple_stmt_uid (stmt));
	  print_generic_stmt (stderr, stmt, 0);
#endif
	}
    }

#ifdef LOCAL_TRACE
  fprintf (stderr, "%s\n", IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (fn_decl)));
#endif

  cedge = cgraph_node (fn_decl)->callees;
  while (cedge)
    {
      cedge->call_stmt = stmts [cedge->lto_stmt_uid];
#ifdef LOCAL_TRACE
      fprintf (stderr, "fixing up call %d\n", cedge->lto_stmt_uid);
#endif
      cedge = cedge->next_callee;
    }
#ifdef LOCAL_TRACE
  fprintf (stderr, "\n");
#endif
  
  free (stmts);

  LTO_DEBUG_UNDENT();
}


/* Fill in the initializers of the public statics.  */

static void
input_constructors_or_inits (struct data_in *data_in, 
			     struct lto_input_block *ib)
{
  enum LTO_tags tag;

  clear_line_info (data_in);
  tag = input_record_start (ib);
  while (tag)
    {
      tree var;
      var = input_expr_operand (ib, data_in, NULL, tag);
      LTO_DEBUG_TOKEN ("init");
      tag = input_record_start (ib);
      DECL_INITIAL (var) = input_expr_operand (ib, data_in, NULL, tag);
      tag = input_record_start (ib);
    }
}


static bool initialized_local = false;


/* Static initialization for the lto reader.  */

static void
lto_static_init_local (void)
{
  if (initialized_local)
    return;

  initialized_local = true;

  /* Initialize the expression to tag mapping.  */
#define MAP_EXPR_TAG(expr,tag)   tag_to_expr [tag] = expr;
#define MAP_EXPR_TAGS(expr,tag,count) \
  {                                   \
    int i;                            \
    for (i=0; i<count; i++)           \
      tag_to_expr [tag + i] = expr;   \
  }
#define TREE_MULTIPLE
#define TREE_SINGLE_MECHANICAL_TRUE
#define TREE_SINGLE_MECHANICAL_FALSE
#define SET_NAME(a,b)
#include "lto-tree-tags.def"

#undef MAP_EXPR_TAG
#undef MAP_EXPR_TAGS
#undef TREE_MULTIPLE
#undef TREE_SINGLE_MECHANICAL_TRUE
#undef TREE_SINGLE_MECHANICAL_FALSE
#undef SET_NAME
  /* Initialize flags_length_for_code.  */


#define START_CLASS_SWITCH()                  \
  {                                           \
    int code;				      \
    for (code=0; code<NUM_TREE_CODES; code++) \
      {                                       \
	/* The LTO_SOURCE_LOC_BITS leaves room for file and line number for exprs.  */ \
        flags_length_for_code[code] = LTO_SOURCE_LOC_BITS; \
                                              \
        switch (TREE_CODE_CLASS (code))       \
          {

#define START_CLASS_CASE(class)    case class:
#define ADD_CLASS_DECL_FLAG(flag_name)    flags_length_for_code[code]++;
#define ADD_CLASS_EXPR_FLAG(flag_name)    flags_length_for_code[code]++;
#define END_CLASS_CASE(class)      break;
#define END_CLASS_SWITCH()                    \
          default:                            \
	    fprintf (stderr, "no declaration for TREE CODE CLASS for = %s(%d)\n", \
                     tree_code_name[code], code);                                 \
            gcc_unreachable ();               \
          }


#define START_EXPR_SWITCH()                   \
        switch (code)			      \
          {
#define START_EXPR_CASE(code)    case code:
#define ADD_EXPR_FLAG(flag_name)           flags_length_for_code[code]++;
#define ADD_DECL_FLAG(flag_name)           flags_length_for_code[code]++;
#define ADD_VIS_FLAG(flag_name)            flags_length_for_code[code]++;
#define ADD_VIS_FLAG_SIZE(flag_name,size)  flags_length_for_code[code] += size;
#define END_EXPR_CASE(class)      break;
#define END_EXPR_SWITCH()                     \
          default:                            \
	    fprintf (stderr, "no declaration for TREE CODE = %s(%d)\n", \
                     tree_code_name[code], code);		        \
            gcc_unreachable ();               \
          }                                   \
      }					      \
  }

#include "lto-tree-flags.def"

#undef START_CLASS_SWITCH
#undef START_CLASS_CASE
#undef ADD_CLASS_DECL_FLAG
#undef ADD_CLASS_EXPR_FLAG
#undef END_CLASS_CASE
#undef END_CLASS_SWITCH
#undef START_EXPR_SWITCH
#undef START_EXPR_CASE
#undef ADD_EXPR_FLAG
#undef ADD_DECL_FLAG
#undef ADD_VIS_FLAG
#undef ADD_VIS_FLAG_SIZE
#undef END_EXPR_CASE
#undef END_EXPR_SWITCH

  /* Verify that lto_flags_type is wide enough.  */
  {
    int code;
    for (code = 0; code < NUM_TREE_CODES; code++)
      gcc_assert (flags_length_for_code[code] <= BITS_PER_LTO_FLAGS_TYPE);
  }
  
  lto_static_init ();
  tree_register_cfg_hooks ();

  file_name_hash_table
    = htab_create (37, hash_string_slot_node, eq_string_slot_node, free);
}


/* Read the body form DATA for tree T and fill it in.  File_data are
   the global decls and types.  SECTION_TYPE is either
   LTO_section_function_body or LTO_section_static_initializer.  IF
   section type is LTO_section_function_body, FN must be the decl for
   that function.  */

static void 
lto_read_body (struct lto_file_decl_data* file_data,
	       tree fn_decl,
	       const void *data,
	       enum lto_section_type section_type)
{
  struct lto_function_header * header 
    = (struct lto_function_header *) data;
  struct data_in data_in;

  int32_t named_label_offset = sizeof (struct lto_function_header); 
  int32_t ssa_names_offset 
    = named_label_offset + header->named_label_size;
  int32_t cfg_offset 
    = ssa_names_offset + header->ssa_names_size;
  int32_t local_decls_index_offset = cfg_offset + header->cfg_size;
  int32_t local_decls_offset = local_decls_index_offset + header->local_decls_index_size;
  int32_t main_offset = local_decls_offset + header->local_decls_size;
  int32_t string_offset = main_offset + header->main_size;

#ifdef LTO_STREAM_DEBUGGING
  int32_t debug_decl_index_offset = string_offset + header->string_size;
  int32_t debug_decl_offset = debug_decl_index_offset + header->debug_decl_index_size;
  int32_t debug_label_offset = debug_decl_offset + header->debug_decl_size;
  int32_t debug_ssa_names_offset = debug_label_offset + header->debug_label_size;
  int32_t debug_cfg_offset = debug_ssa_names_offset + header->debug_ssa_names_size;
  int32_t debug_main_offset = debug_cfg_offset + header->debug_cfg_size;

  struct lto_input_block debug_decl_index 
    = {data + debug_decl_index_offset, 0, header->debug_decl_index_size};
  struct lto_input_block debug_decl 
    = {data + debug_decl_offset, 0, header->debug_decl_size};
  struct lto_input_block debug_label 
    = {data + debug_label_offset, 0, header->debug_label_size};
  struct lto_input_block debug_ssa_names 
    = {data + debug_ssa_names_offset, 0, header->debug_ssa_names_size};
  struct lto_input_block debug_cfg 
    = {data + debug_cfg_offset, 0, header->debug_cfg_size};
  struct lto_input_block debug_main 
    = {data + debug_main_offset, 0, header->debug_main_size};
#endif

  struct lto_input_block ib_named_labels 
    = {data + named_label_offset, 0, header->named_label_size};
  struct lto_input_block ib_ssa_names 
    = {data + ssa_names_offset, 0, header->ssa_names_size};
  struct lto_input_block ib_cfg 
    = {data + cfg_offset, 0, header->cfg_size};
  struct lto_input_block ib_local_decls_index 
    = {data + local_decls_index_offset, 0, header->local_decls_index_size};
  struct lto_input_block ib_local_decls 
    = {data + local_decls_offset, 0, header->local_decls_size};
  struct lto_input_block ib_main 
    = {data + main_offset, 0, header->main_size};

#ifdef LTO_STREAM_DEBUGGING
  lto_debug_context.out = lto_debug_in_fun;
  lto_debug_context.indent = 0;
  lto_debug_context.tag_names = LTO_tree_tag_names;
#endif
  memset (&data_in, 0, sizeof (struct data_in));
  data_in.file_data          = file_data;
  data_in.strings            = data + string_offset;
  data_in.strings_len        = header->string_size;

  lto_static_init_local ();

  /* No upward compatibility here.  */
  gcc_assert (header->lto_header.major_version == LTO_major_version);
  gcc_assert (header->lto_header.minor_version == LTO_minor_version);

  if (section_type == LTO_section_function_body)
    {
      struct function *fn = DECL_STRUCT_FUNCTION (fn_decl);
      push_cfun (fn);
      init_tree_ssa (fn);
      data_in.num_named_labels = header->num_named_labels;

#ifdef LTO_STREAM_DEBUGGING
      lto_debug_context.current_data = &debug_label;
#endif
      input_labels (&ib_named_labels, &data_in, 
		    header->num_named_labels, header->num_unnamed_labels);
      
#ifdef LTO_STREAM_DEBUGGING
      lto_debug_context.current_data = &debug_decl_index;
#endif
      input_local_vars_index (&ib_local_decls_index, &data_in, header->num_local_decls);
      
#ifdef LTO_STREAM_DEBUGGING
      lto_debug_context.current_data = &debug_decl;
#endif
      input_local_vars (&ib_local_decls, &data_in, fn, header->num_local_decls);
      
#ifdef LTO_STREAM_DEBUGGING
      lto_debug_context.current_data = &debug_ssa_names;
#endif
      input_ssa_names (&ib_ssa_names, &data_in, fn);
      
#ifdef LTO_STREAM_DEBUGGING
      lto_debug_context.current_data = &debug_cfg;
#endif
      input_cfg (&ib_cfg, fn);

      /* Ensure that all our variables have annotations attached to them
	 so building SSA doesn't choke.  */
      {
	unsigned int i;
	int j;

	for (i = 0; i < file_data->num_var_decls; i++)
	  add_referenced_var (file_data->var_decls[i]);
	for (j = 0; j < header->num_local_decls; j++)
	  add_referenced_var (data_in.local_decls[j]);
      }

#ifdef LTO_STREAM_DEBUGGING
      lto_debug_context.current_data = &debug_main;
#endif
      /* Set up the struct function.  */
      input_function (fn_decl, &data_in, &ib_main);

      /* We should now be in SSA.  */
      cfun->gimple_df->in_ssa_p = true;
      /* Fill in properties we know hold for the rebuilt CFG.  */
      cfun->curr_properties = PROP_ssa | PROP_cfg | PROP_gimple_any | PROP_gimple_lcf | PROP_gimple_leh | PROP_referenced_vars;

      pop_cfun ();
    }
  else 
    {
#ifdef LTO_STREAM_DEBUGGING
      lto_debug_context.current_data = &debug_main;
#endif
      input_constructors_or_inits (&data_in, &ib_main);
    }

  clear_line_info (&data_in);
  if (section_type == LTO_section_function_body)
    {
      free (data_in.labels);
      free (data_in.local_decls_index);
      free (data_in.local_decls_index_d);
    }
}


/* Read in FN_DECL using DATA.  File_data are the global decls and types.  */

void 
lto_input_function_body (struct lto_file_decl_data* file_data,
			 tree fn_decl,
			 const void *data)
{
  current_function_decl = fn_decl;
  lto_read_body (file_data, fn_decl, data, LTO_section_function_body);
}


/* Read in VAR_DECL using DATA.  File_data are the global decls and types.  */

void 
lto_input_constructors_and_inits (struct lto_file_decl_data* file_data,
				  const void *data)
{
  lto_read_body (file_data, NULL, data, LTO_section_static_initializer);
}

