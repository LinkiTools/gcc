/* Top-level LTO routines.
   Copyright 2006 Free Software Foundation, Inc.
   Contributed by CodeSourcery, Inc.

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
#include "opts.h"
#include "toplev.h"
#include "tree.h"
#include "diagnostic.h"
#include "tm.h"
#include "libiberty.h"
#include "cgraph.h"
#include "ggc.h"
#include "tree-ssa-operands.h"
#include "tree-pass.h"
#include "langhooks.h"
#include "lto.h"
#include "lto-tree.h"
#include "lto-section.h"
#include "lto-section-in.h"
#include "lto-section-out.h"
#include "lto-tree-in.h"
#include "lto-tags.h"
#include "lto-opts.h"
#include "lto-utils.h"
#include "vec.h"
#include "bitmap.h"
#include "pointer-set.h"
#include "ipa-prop.h"
#include "common.h"
#include "timevar.h"

/* This needs to be included after config.h.  Otherwise, _GNU_SOURCE will not
   be defined in time to set __USE_GNU in the system headers, and strsignal
   will not be declared.  */
#include <sys/mman.h>

DEF_VEC_P(bitmap);
DEF_VEC_ALLOC_P(bitmap,heap);

/* Read the constructors and inits.  */

static void
lto_materialize_constructors_and_inits (struct lto_file_decl_data * file_data)
{
  size_t len;
  const char *data = lto_get_section_data (file_data, 
					   LTO_section_static_initializer,
					   NULL, &len);
  lto_input_constructors_and_inits (file_data, data);
  lto_free_section_data (file_data, LTO_section_static_initializer, NULL,
			 data, len);
}

/* Read the function body for the function associated with NODE if possible.  */

static void
lto_materialize_function (struct cgraph_node *node)
{
  tree decl = node->decl;
  struct lto_file_decl_data *file_data = node->local.lto_file_data;
  const char *data;
  size_t len;
  tree step;
  const char *name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)); 

  /* We may have renamed the declaration, e.g., a static function.  */
  name = lto_get_decl_name_mapping (file_data, name);

  data = lto_get_section_data (file_data, LTO_section_function_body,
			       name, &len);
  if (data)
    {
      struct function *fn;

      /* This function has a definition.  */
      TREE_STATIC (decl) = 1;

      allocate_struct_function (decl, false);

      /* Load the function body only if not operating in WPA mode.  In
	 WPA mode, the body of the function is not needed.  */
      if (!flag_wpa)
	{
	  lto_input_function_body (file_data, decl, data);
	  lto_stats.num_function_bodies++;
	}

      fn = DECL_STRUCT_FUNCTION (decl);
      lto_free_section_data (file_data, LTO_section_function_body, name,
			     data, len);

      /* Look for initializers of constant variables and private
	 statics.  */
      for (step = fn->local_decls; step; step = TREE_CHAIN (step))
	{
	  tree decl = TREE_VALUE (step);
	  if (TREE_CODE (decl) == VAR_DECL
	      && (TREE_STATIC (decl) && !DECL_EXTERNAL (decl))
	      && flag_unit_at_a_time)
	    varpool_finalize_decl (decl);
	}
    }
  else
    DECL_EXTERNAL (decl) = 1;

  /* Let the middle end know about the function.  */
  rest_of_decl_compilation (decl, 1, 0);
  if (cgraph_node (decl)->needed)
    cgraph_mark_reachable_node (cgraph_node (decl));
}


/* Initialize the globals vector with pointers to well-known trees.  */

static void
preload_common_nodes (struct data_in *data_in)
{
  unsigned i;
  htab_t index_table;
  VEC(tree, heap) *common_nodes;
  tree node;

  /* The global tree for the main identifier is filled in by language-specific
     front-end initialization that is not run in the LTO back-end.  It appears
     that all languages that perform such initialization currently do so in the
     same way, so we do it here.  */
  if (!main_identifier_node)
    main_identifier_node = get_identifier ("main");

  ptrdiff_type_node = integer_type_node;

  common_nodes = lto_get_common_nodes ();
  /* FIXME lto.  In the C++ front-end, fileptr_type_node is defined as a
     variant copy of of ptr_type_node, rather than ptr_node itself.  The
     distinction should only be relevant to the front-end, so we always
     use the C definition here in lto1.  */
  gcc_assert (fileptr_type_node == ptr_type_node);

  index_table = htab_create (37, lto_hash_global_slot_node,
			     lto_eq_global_slot_node, free);

#ifdef GLOBAL_STREAMER_TRACE
  fprintf (stderr, "\n\nPreloading all common_nodes.\n");
#endif

  for (i = 0; VEC_iterate (tree, common_nodes, i, node); i++)
    preload_common_node (node, index_table, &data_in->globals_index, NULL);

#ifdef GLOBAL_STREAMER_TRACE
  fprintf (stderr, "\n\nPreloaded %u common nodes.\n", i - 1);
#endif

  VEC_free(tree, heap, common_nodes);
  htab_delete (index_table);
}

/* Decode the content of memory pointed to by DATA in the the
   in decl state object STATE. DATA_IN points to a data_in structure for
   decoding. Return the address after the decoded object in the input.  */

static const uint32_t*
lto_read_in_decl_state (struct data_in *data_in, const uint32_t *data,
			struct lto_in_decl_state *state)
{
  uint32_t fn_decl_index;
  tree decl;
  uint32_t i, j;
  
  fn_decl_index = *data++;
  decl = VEC_index (tree, data_in->globals_index, fn_decl_index);
  if (TREE_CODE (decl) != FUNCTION_DECL)
    {
      gcc_assert (decl == void_type_node);
      decl = NULL_TREE;
    }
  state->fn_decl = decl;

  for (i = 0; i < LTO_N_DECL_STREAMS; i++)
    {
      uint32_t size = *data++;
      tree *decls = (tree *) xcalloc (size, sizeof (tree));

      for (j = 0; j < size; j++)
        decls[j] = VEC_index (tree, data_in->globals_index, data[j]);
      state->streams[i].size = size;
      state->streams[i].trees = decls;
      data += size;
    }

  return data;
}

static void
lto_read_decls (struct lto_file_decl_data *decl_data, const void *data,
		VEC(ld_plugin_symbol_resolution_t,heap) *resolutions)
{
  const struct lto_decl_header *header = (const struct lto_decl_header *) data;
  const int32_t decl_offset = sizeof (struct lto_decl_header);
  const int32_t main_offset = decl_offset + header->decl_state_size;
  const int32_t string_offset = main_offset + header->main_size;
#ifdef LTO_STREAM_DEBUGGING
  int32_t debug_main_offset;
  struct lto_input_block debug_main;
#endif
  struct lto_input_block ib_main;
  struct data_in data_in;
  unsigned int i;
  const uint32_t *data_ptr, *data_end;
  uint32_t num_decl_states;

#ifdef LTO_STREAM_DEBUGGING
  debug_main_offset = string_offset + header->string_size;
#endif
  
  LTO_INIT_INPUT_BLOCK (ib_main, (const char *) data + main_offset, 0,
			header->main_size);
#ifdef LTO_STREAM_DEBUGGING
  LTO_INIT_INPUT_BLOCK (debug_main, (const char *) data + debug_main_offset, 0,
			header->debug_main_size);
#endif

  memset (&data_in, 0, sizeof (struct data_in));
  data_in.file_data          = decl_data;
  data_in.strings            = (const char *) data + string_offset;
  data_in.strings_len        = header->string_size;
  data_in.globals_index	     = NULL;
  data_in.globals_resolution = resolutions;

  /* FIXME: This doesn't belong here.
     Need initialization not done in lto_static_init ().  */
  lto_static_init_local ();

#ifdef LTO_STREAM_DEBUGGING
  lto_debug_context.out = lto_debug_in_fun;
  lto_debug_context.indent = 0;
  lto_debug_context.tag_names = LTO_tree_tag_names;
  lto_debug_context.current_data = &debug_main;
#endif

  /* Preload references to well-known trees.  */
  preload_common_nodes (&data_in);

  /* Read the global declarations and types.  */
  /* FIXME: We should be a bit more graceful regarding truncated files. */
  while (ib_main.p < ib_main.len)
    {
      input_tree (&ib_main, &data_in);
      gcc_assert (ib_main.p <= ib_main.len);
    }

  /* Read in lto_in_decl_state objects. */

  data_ptr = (const uint32_t *) ((const char*) data + decl_offset); 
  data_end =
     (const uint32_t *) ((const char*) data_ptr + header->decl_state_size);
  num_decl_states = *data_ptr++;
  
  gcc_assert (num_decl_states > 0);
  decl_data->global_decl_state = lto_new_in_decl_state ();
  data_ptr = lto_read_in_decl_state (&data_in, data_ptr,
				     decl_data->global_decl_state);

  /* Read in per-function decl states and enter them in hash table.  */
  decl_data->function_decl_states =
    htab_create (37, lto_hash_in_decl_state, lto_eq_in_decl_state, free);

  for (i = 1; i < num_decl_states; i++)
    {
      struct lto_in_decl_state *state = lto_new_in_decl_state ();
      void **slot;

      data_ptr = lto_read_in_decl_state (&data_in, data_ptr, state);
      slot = htab_find_slot (decl_data->function_decl_states, state, INSERT);
      gcc_assert (*slot == NULL);
      *slot = state;
    }
  gcc_assert (data_ptr == data_end);
  
  /* Set the current decl state to be the global state. */
  decl_data->current_decl_state = decl_data->global_decl_state;

  /* The globals index vector is needed only while reading.  */

  VEC_free (tree, heap, data_in.globals_index);
  VEC_free (ld_plugin_symbol_resolution_t, heap, data_in.globals_resolution);
}

/* Read resolution for file named FILE_NAME. The resolution is read from
   RESOLUTION. An array with the symbol resolution is returned. The array
   size is written to SIZE. */

static VEC(ld_plugin_symbol_resolution_t,heap) *
lto_resolution_read (FILE *resolution, const char *file_name)
{
  /* We require that objects in the resolution file are in the same
     order as the lto1 command line. */
  unsigned int name_len;
  char *obj_name;
  unsigned int num_symbols;
  unsigned int i;
  VEC(ld_plugin_symbol_resolution_t,heap) *ret = NULL;
  unsigned max_index = 0;

  if (!resolution)
    return NULL;

  name_len = strlen (file_name);
  obj_name = XNEWVEC (char, name_len + 1);
  fscanf (resolution, " ");   /* Read white space. */

  fread (obj_name, sizeof (char), name_len, resolution);
  obj_name[name_len] = '\0';
  gcc_assert (strcmp(obj_name, file_name) == 0);
  free (obj_name);

  fscanf (resolution, "%u", &num_symbols);

  for (i = 0; i < num_symbols; i++)
    {
      unsigned index;
      char r_str[27];
      enum ld_plugin_symbol_resolution r;
      unsigned int j;
      unsigned int lto_resolution_str_len =
	sizeof (lto_resolution_str) / sizeof (char *);

      fscanf (resolution, "%u %26s", &index, r_str);
      if (index > max_index)
	max_index = index;

      for (j = 0; j < lto_resolution_str_len; j++)
	{
	  if (strcmp (lto_resolution_str[j], r_str) == 0)
	    {
	      r = j;
	      break;
	    }
	}
      gcc_assert (j < lto_resolution_str_len);

      VEC_safe_grow_cleared (ld_plugin_symbol_resolution_t, heap, ret,
			     index + 1);
      VEC_replace (ld_plugin_symbol_resolution_t, ret, index, r);
    }

  return ret;
}

/* Generate a TREE representation for all types and external decls
   entities in FILE.  

   Read all of the globals out of the file.  Then read the cgraph
   and process the .o index into the cgraph nodes so that it can open
   the .o file to load the functions and ipa information.   */

static struct lto_file_decl_data *
lto_file_read (lto_file *file, FILE *resolution_file)
{
  struct lto_file_decl_data *file_data;
  const char *data;
  size_t len;
  VEC(ld_plugin_symbol_resolution_t,heap) *resolutions;
  
  resolutions = lto_resolution_read (resolution_file, file->filename);

  file_data = XCNEW (struct lto_file_decl_data);
  file_data->file_name = file->filename;
  file_data->fd = -1;
  file_data->section_hash_table = lto_elf_build_section_table (file);
  file_data->renaming_hash_table = lto_create_renaming_table ();

  data = lto_get_section_data (file_data, LTO_section_decls, NULL, &len);
  lto_read_decls (file_data, data, resolutions);
  lto_free_section_data (file_data, LTO_section_decls, NULL, data, len);

  return file_data;
}

/****************************************************************************
  Input routines for reading sections from .o files.

  FIXME: These routines may need to be generalized.  They assume that
  the .o file can be read into memory and the secions just mapped.
  This may not be true if the .o file is in some form of archive.
****************************************************************************/

/* Page size of machine is used for mmap and munmap calls.  */
static size_t page_mask;

/* Get the section data of length LEN from FILENAME starting at
   OFFSET.  The data segment must be freed by the caller when the
   caller is finished.  Returns NULL if all was not well.  */

static char *
lto_read_section_data (struct lto_file_decl_data *file_data,
		       intptr_t offset, size_t len)
{
  char *result;
  intptr_t computed_len;
  intptr_t computed_offset;
  intptr_t diff;

  if (!page_mask)
    {
      size_t page_size = sysconf (_SC_PAGE_SIZE);
      page_mask = ~(page_size - 1);
    }

  if (file_data->fd == -1)
    file_data->fd = open (file_data->file_name, O_RDONLY);

  if (file_data->fd == -1)
    return NULL;

  computed_offset = offset & page_mask;
  diff = offset - computed_offset;
  computed_len = len + diff;

  result = (char *) mmap (NULL, computed_len, PROT_READ, MAP_PRIVATE,
			  file_data->fd, computed_offset);
  if (result == MAP_FAILED)
    {
      close (file_data->fd);
      return NULL;
    }

  return result + diff;
}    


/* Get the section data from FILE_DATA of SECTION_TYPE with NAME.
   NAME will be NULL unless the section type is for a function
   body.  */

static const char *
get_section_data (struct lto_file_decl_data *file_data,
		      enum lto_section_type section_type,
		      const char *name,
		      size_t *len)
{
  htab_t section_hash_table = file_data->section_hash_table;
  struct lto_section_slot *f_slot;
  struct lto_section_slot s_slot;
  const char *section_name = lto_get_section_name (section_type, name);
  char *data = NULL;

  *len = 0;
  s_slot.name = section_name;
  f_slot = (struct lto_section_slot *) htab_find (section_hash_table, &s_slot);
  if (f_slot)
    {
      data = lto_read_section_data (file_data, f_slot->start, f_slot->len);
      *len = f_slot->len;
    }

  free (CONST_CAST (char *, section_name));
  return data;
}


/* Free the section data from FILE_DATA of SECTION_TYPE with NAME that
   starts at OFFSET and has LEN bytes.  */

static void
free_section_data (struct lto_file_decl_data *file_data,
		   enum lto_section_type section_type ATTRIBUTE_UNUSED,
		   const char *name ATTRIBUTE_UNUSED,
		   const char *offset, size_t len)
{
  intptr_t computed_len;
  intptr_t computed_offset;
  intptr_t diff;

  if (file_data->fd == -1)
    return;

  computed_offset = ((intptr_t)offset) & page_mask;
  diff = (intptr_t)offset - computed_offset;
  computed_len = len + diff;

  munmap ((void *)computed_offset, computed_len);
}

/* Vector of all cgraph node sets. */
static GTY (()) VEC(cgraph_node_set ,gc) *lto_cgraph_node_sets;

/* Group cgrah nodes by input files.  This is used mainly for testing
   right now.  */

static void
lto_1_to_1_map (void)
{
  struct cgraph_node *node;
  struct lto_file_decl_data *file_data;
  struct pointer_map_t *pmap;
  cgraph_node_set set;
  void **slot;

  timevar_push (TV_WHOPR_WPA);

  lto_cgraph_node_sets = VEC_alloc (cgraph_node_set, gc, 1);

  /* If the cgraph is empty, create one cgraph node set so that there is still
     an output file for any variables that need to be exported in a DSO.  */
  if (!cgraph_nodes)
    {
      set = cgraph_node_set_new ();
      VEC_safe_push (cgraph_node_set, gc, lto_cgraph_node_sets, set);
      goto finish;
    }

  pmap = pointer_map_create ();

  for (node = cgraph_nodes; node; node = node->next)
    {
      /* We assume file_data are unique.  */
      file_data = node->local.lto_file_data;
      gcc_assert (file_data);

      slot = pointer_map_contains (pmap, file_data);
      if (slot)
	  set = (cgraph_node_set) *slot;
      else
	{
	  set = cgraph_node_set_new ();
	  slot = pointer_map_insert (pmap, file_data);
	  *slot = set;
	  VEC_safe_push (cgraph_node_set, gc, lto_cgraph_node_sets, set);
	}
      cgraph_node_set_add (set, node);
    }

  pointer_map_destroy (pmap);

finish:
  timevar_pop (TV_WHOPR_WPA);

  lto_stats.num_cgraph_partitions += VEC_length (cgraph_node_set, 
						 lto_cgraph_node_sets);
}


/* Add inlined clone NODE and its master clone to SET, if NODE itself has
   inlined callee, recursively add the callees.  */

static void
lto_add_inline_clones (cgraph_node_set set, struct cgraph_node *node,
		       bitmap original_decls, bitmap inlined_decls)
{
   struct cgraph_node *callee;
   struct cgraph_edge *edge;

   cgraph_node_set_add (set, node);

   if (!bitmap_bit_p (original_decls, DECL_UID (node->decl)))
     bitmap_set_bit (inlined_decls, DECL_UID (node->decl));

   /* Check to see if NODE has any inlined callee.  */
   for (edge = node->callees; edge != NULL; edge = edge->next_callee)
     {
	callee = edge->callee;
	if (callee->global.inlined_to != NULL)
	  lto_add_inline_clones (set, callee, original_decls,
				 inlined_decls);
     }
}

/* Compute the transitive closure of inlining of SET based on the
   information in the call-graph.  Returns a bitmap of decls indexed
   by UID.  */

static bitmap
lto_add_all_inlinees (cgraph_node_set set)
{
  cgraph_node_set_iterator csi;
  struct cgraph_node *node;
  bitmap original_nodes = lto_bitmap_alloc ();
  bitmap original_decls = lto_bitmap_alloc ();
  bitmap inlined_decls = lto_bitmap_alloc();
  bool changed;

  /* We are going to iterate SET will adding to it, mark all original
     nodes so that we only add node inlined to original nodes.  */
  for (csi = csi_start (set); !csi_end_p (csi); csi_next (&csi))
    {
      bitmap_set_bit (original_nodes, csi_node (csi)->uid);
      bitmap_set_bit (original_decls, DECL_UID (csi_node (csi)->decl));
    }

  /* Some of the original nodes might not be needed anymore.  Remove them. */
  do
    {
      changed = false;
      for (csi = csi_start (set); !csi_end_p (csi); csi_next (&csi))
	{
	  struct cgraph_node *inlined_to;
	  node = csi_node (csi);

	  /* NODE was not inlined. We still need it. */
	  if (!node->global.inlined_to)
	    continue;

	  inlined_to = node->global.inlined_to;

	  /* NODE should have only one caller */
	  gcc_assert (!node->callers->next_caller);

	  if (!bitmap_bit_p (original_nodes, inlined_to->uid))
	    {
	      bitmap_clear_bit (original_nodes, node->uid);
	      cgraph_node_set_remove (set, node);
	      changed = true;
	    }
	}
    } while (changed);

  for (csi = csi_start (set); !csi_end_p (csi); csi_next (&csi))
    {
      node = csi_node (csi);
      if (bitmap_bit_p (original_nodes, node->uid))
	lto_add_inline_clones (set, node, original_decls, inlined_decls);
    }

  lto_bitmap_free (original_nodes);
  lto_bitmap_free (original_decls);
  return inlined_decls;
}

/* Owing to inlining, we may need to promote a file-scope variable
   to a global variable.  Consider this case:

   a.c:
   static int var;

   void
   foo (void)
   {
     var++;
   }

   b.c:

   extern void foo (void);

   void
   bar (void)
   {
     foo ();
   }

   If WPA inlines FOO inside BAR, then the static variable VAR needs to
   be promoted to global because BAR and VAR may be in different LTRANS
   files. */

/* This struct keeps track of states used in globalization.  */

typedef struct
{
  /* Current cgraph node set.  */  
  cgraph_node_set set;

  /* Function DECLs of cgraph nodes seen.  */
  bitmap seen_node_decls;

  /* Use in walk_tree to avoid multiple visits of a node.  */
  struct pointer_set_t *visited;

  /* static vars in this set.  */
  bitmap static_vars_in_set;

  /* static vars in all previous set.  */
  bitmap all_static_vars;

  /* all vars in all previous set.  */
  bitmap all_vars;
} globalize_context_t;

/* Callback for walk_tree.  Examine the tree pointer to by TP and see if
   if its a file-scope static variable of function that need to be turned
   into a global.  */

static tree
globalize_cross_file_statics (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
			      void *data)
{
  globalize_context_t *context = (globalize_context_t *) data;
  tree t = *tp;

  if (t == NULL_TREE)
    return NULL;

  /* The logic for globalization of VAR_DECLs and FUNCTION_DECLs are
     different.  For functions, we can simply look at the cgraph node sets
     to tell if there are references to static functions outside the set.
     The cgraph node sets do not keep track of vars, we need to traverse
     the trees to determine what vars need to be globalized.  */
  if (TREE_CODE (t) == VAR_DECL)
    {
      if (!TREE_PUBLIC (t))
	{
	  /* This file-scope static variable is reachable from more
	     that one set.  Make it global but with hidden visibility
	     so that we do not export it in dynamic linking.  */
	  if (bitmap_bit_p (context->all_static_vars, DECL_UID (t)))
	    {
	      TREE_PUBLIC (t) = 1;
	      DECL_VISIBILITY (t) = VISIBILITY_HIDDEN;
	    }
	  bitmap_set_bit (context->static_vars_in_set, DECL_UID (t));
	}
      bitmap_set_bit (context->all_vars, DECL_UID (t));
      walk_tree (&DECL_INITIAL (t), globalize_cross_file_statics, context,
		 context->visited);
    }
  else if (TREE_CODE (t) == FUNCTION_DECL && !TREE_PUBLIC (t))
    {
      if (!cgraph_node_in_set_p (cgraph_node (t), context->set))
	{
	  /* This file-scope static function is reachable from a set
	     which does not contain the function DECL.  Make it global
	     but with hidden visibility.  */
	  TREE_PUBLIC (t) = 1;
	  DECL_VISIBILITY (t) = VISIBILITY_HIDDEN;
	}
    }

  return NULL; 
}

/* Helper of lto_scan_statics_in_cgraph_node below.  Scan TABLE for
   static decls that may be used in more than one LTRANS file.
   CONTEXT is a globalize_context_t for storing scanning states.  */

static void
lto_scan_statics_in_ref_table (struct lto_tree_ref_table *table,
			       globalize_context_t *context)
{
  unsigned i;

  for (i = 0; i < table->size; i++)
    walk_tree (&table->trees[i], globalize_cross_file_statics, context,
	       context->visited);
}

/* Promote file-scope decl reachable from NODE if necessary to global.
   CONTEXT is a globalize_context_t storing scanning states.  */

static void
lto_scan_statics_in_cgraph_node (struct cgraph_node *node,
				 globalize_context_t *context)
{
  struct lto_in_decl_state *state;
  
  /* Return if NODE has no function body or is not the master clone. */
  if (!node->analyzed)
    return;
  
  /* Return if the DECL of nodes has been visited before.  */
  if (bitmap_bit_p (context->seen_node_decls, DECL_UID (node->decl)))
    return;
  bitmap_set_bit (context->seen_node_decls, DECL_UID (node->decl));

  state = lto_get_function_in_decl_state (node->local.lto_file_data,
					  node->decl);
  gcc_assert (state);

  lto_scan_statics_in_ref_table (&state->streams[LTO_DECL_STREAM_VAR_DECL],
				 context);
  lto_scan_statics_in_ref_table (&state->streams[LTO_DECL_STREAM_FN_DECL],
				 context);
}

/* Scan all global variables that we have not yet seen so far.  CONTEXT
   is a globalize_context_t storing scanning states.  */

static void
lto_scan_statics_in_remaining_global_vars (globalize_context_t *context)
{
  tree var, var_context;
  struct varpool_node *vnode;

  FOR_EACH_STATIC_VARIABLE (vnode)
    {
      var = vnode->decl;
      var_context = DECL_CONTEXT (var);
      if (TREE_STATIC (var)
	  && TREE_PUBLIC (var)
          && (!var_context || TREE_CODE (var_context) != FUNCTION_DECL)
          && !bitmap_bit_p (context->all_vars, DECL_UID (var)))
	walk_tree (&var, globalize_cross_file_statics, context,
		   context->visited);
    }
}

/* Find out all static decls that need to be promoted to global because
   of cross file sharing.  This function must be run in the WPA mode after
   all inlinees are added.  */

static void
lto_promote_cross_file_statics (void)
{
  unsigned i, n_sets;
  cgraph_node_set set;
  cgraph_node_set_iterator csi;
  globalize_context_t context;
  
  memset (&context, 0, sizeof (context));
  context.all_vars = lto_bitmap_alloc ();
  context.all_static_vars = lto_bitmap_alloc ();

  n_sets = VEC_length (cgraph_node_set, lto_cgraph_node_sets);
  for (i = 0; i < n_sets; i++)
    {
      set = VEC_index (cgraph_node_set, lto_cgraph_node_sets, i);
      context.set = set;
      context.visited = pointer_set_create ();
      context.static_vars_in_set = lto_bitmap_alloc ();
      context.seen_node_decls = lto_bitmap_alloc ();

      for (csi = csi_start (set); !csi_end_p (csi); csi_next (&csi))
	lto_scan_statics_in_cgraph_node (csi_node (csi), &context);

      if (i == n_sets - 1)
        lto_scan_statics_in_remaining_global_vars (&context);

      bitmap_ior_into (context.all_static_vars, context.static_vars_in_set);

      pointer_set_destroy (context.visited);
      lto_bitmap_free (context.static_vars_in_set);
      lto_bitmap_free (context.seen_node_decls);
    }

  lto_bitmap_free (context.all_vars);
  lto_bitmap_free (context.all_static_vars);
}

static lto_file *current_lto_file;

/* Write all output files in WPA mode.  Returns a NULL-terminated array of
   output file names.  */

static char **
lto_wpa_write_files (void)
{
  char **output_files;
  unsigned i, n_sets;
  lto_file *file;
  cgraph_node_set set;
  bitmap decls;
  VEC(bitmap,heap) *inlined_decls = NULL;

  timevar_push (TV_WHOPR_WPA);

  /* Include all inlined functions.  */
  for (i = 0; VEC_iterate (cgraph_node_set, lto_cgraph_node_sets, i, set); i++)
    {
      decls = lto_add_all_inlinees (set);
      VEC_safe_push (bitmap, heap, inlined_decls, decls);
      lto_stats.num_output_cgraph_nodes += VEC_length (cgraph_node_ptr,
						       set->nodes);
    }

  /* After adding all inlinees, find out statics that need to be promoted
     to globals because of cross-file inlining.  */
  lto_promote_cross_file_statics ();

  timevar_pop (TV_WHOPR_WPA);

  timevar_push (TV_WHOPR_WPA_IO);

  output_files = XNEWVEC (char *, VEC_length (cgraph_node_set,
					      lto_cgraph_node_sets) + 1);

  n_sets = VEC_length (cgraph_node_set, lto_cgraph_node_sets);
  for (i = 0; i < n_sets; i++)
    {
      char *temp_filename = make_cwd_temp_file (".lto.o");

      output_files[i] = temp_filename;

      file = lto_elf_file_open (temp_filename, true);
      if (!file)
        fatal_error ("lto_elf_file_open() failed");

      lto_set_current_out_file (file);
      lto_new_extern_inline_states ();

      decls = VEC_index (bitmap, inlined_decls, i);
      lto_force_functions_extern_inline (decls);

      /* Set AUX to 1 in the last LTRANS file.  */
      set = VEC_index (cgraph_node_set, lto_cgraph_node_sets, i);
      set->aux = (void*) ((intptr_t) (i == (n_sets - 1)));
      ipa_write_summaries_of_cgraph_node_set (set);
      lto_delete_extern_inline_states ();
      
      lto_set_current_out_file (NULL);
      lto_elf_file_close (file);
    }

  lto_stats.num_output_files += n_sets;

  output_files[i] = NULL;

  for (i = 0; VEC_iterate (bitmap, inlined_decls, i, decls); i++)
    lto_bitmap_free (decls);
  VEC_free (bitmap, heap, inlined_decls);

  timevar_pop (TV_WHOPR_WPA_IO);

  return output_files;
}


/* Perform local transformations (LTRANS) on the files in the NULL-terminated
   FILES array.  These should have been written previously by
   lto_wpa_write_files ().  Transformations are performed via the
   ltrans_driver executable, which is passed a list of filenames via the
   command line.  The CC and CFLAGS environment variables are set to
   appropriate values before it is executed.  */

static void
lto_execute_ltrans (char *const *files)
{
  struct pex_obj *pex;
  const char *env_val;
  const char *extra_cflags = " -fno-wpa -fltrans -xlto";
  struct obstack env_obstack;
  char **argv;
  const char **argv_ptr;
  const char *errmsg;
  size_t i;
  int err;
  int status;
  FILE *ltrans_output_list_stream = NULL;

  timevar_push (TV_WHOPR_WPA_LTRANS_EXEC);

  /* Set the CC environment variable.  */
  env_val = getenv ("COLLECT_GCC");
  if (!env_val)
    fatal_error ("environment variable COLLECT_GCC must be set");

  obstack_init (&env_obstack);
  obstack_grow (&env_obstack, "CC=", sizeof ("CC=") - 1);
  obstack_grow (&env_obstack, env_val, strlen (env_val) + 1);
  putenv (XOBFINISH (&env_obstack, char *));

  /* Set the CFLAGS environment variable.  */
  env_val = getenv ("COLLECT_GCC_OPTIONS");
  if (!env_val)
    fatal_error ("environment variable COLLECT_GCC_OPTIONS must be set");

  obstack_init (&env_obstack);
  obstack_grow (&env_obstack, "CFLAGS=", sizeof ("CFLAGS=") - 1);
  obstack_grow (&env_obstack, env_val, strlen (env_val));
  obstack_grow (&env_obstack, extra_cflags, strlen (extra_cflags) + 1);
  putenv (XOBFINISH (&env_obstack, char *));

  pex = pex_init (0, "lto1", NULL);
  if (pex == NULL)
    fatal_error ("pex_init failed: %s", xstrerror (errno));

  /* Initalize the arguments for the LTRANS driver.  */
  for (i = 0; files[i]; ++i);
  argv = XNEWVEC (char *, i + 2);

  /* Open the LTRANS output list.  */
  if (ltrans_output_list)
    {
      ltrans_output_list_stream = fopen (ltrans_output_list, "w");
      if (ltrans_output_list_stream == NULL)
	error ("opening LTRANS output list %s: %m", ltrans_output_list);
    }

  argv_ptr = (const char **)argv;
  *argv_ptr++ = ltrans_driver;
  for (i = 0; files[i]; ++i)
    {
      *argv_ptr++ = files[i];

      /* Replace the .o suffix with a .ltrans.o suffix and write the resulting
	 name to the LTRANS output list.  */
      if (ltrans_output_list_stream)
	{
	  size_t len = strlen (files[i]) - 2;

	  if (fwrite (files[i], 1, len, ltrans_output_list_stream) < len
	      || fwrite (".ltrans.o\n", 1, 10, ltrans_output_list_stream) < 10)
	    error ("writing to LTRANS output list %s: %m", ltrans_output_list);
	}
    }
  *argv_ptr++ = NULL;

  /* Close the LTRANS output list.  */
  if (ltrans_output_list_stream && fclose (ltrans_output_list_stream))
    error ("closing LTRANS output list %s: %m", ltrans_output_list);

  /* Execute the LTRANS driver.  */
  errmsg = pex_run (pex, PEX_LAST | PEX_SEARCH, argv[0], argv, NULL, NULL,
		    &err);
  if (errmsg)
    fatal_error ("%s: %s", errmsg, xstrerror (err));

  if (!pex_get_status (pex, 1, &status))
    fatal_error ("can't get program status: %s", xstrerror (errno));

  pex_free (pex);

  if (status)
    {
      if (WIFSIGNALED (status))
	{
	  int sig = WTERMSIG (status);
	  fatal_error ("%s terminated with signal %d [%s]%s",
		       argv[0], sig, strsignal (sig),
		       WCOREDUMP (status) ? ", core dumped" : "");
	}
      else
	fatal_error ("%s terminated with status %d", argv[0], status);
    }

  timevar_pop (TV_WHOPR_WPA_LTRANS_EXEC);
}


typedef struct {
  struct pointer_set_t *free_list;
  struct pointer_set_t *seen;
} lto_fixup_data_t;

#define LTO_FIXUP_SUBTREE(t) \
  do \
    walk_tree (&(t), lto_fixup_tree, data, NULL); \
  while (0)

static tree lto_fixup_tree (tree *, int *, void *);

/* Return true if T does not need to be fixed up recursively.  */

static inline bool
no_fixup_p (tree t)
{
  return (t == NULL
	  || CONSTANT_CLASS_P (t)
	  || TREE_CODE (t) == IDENTIFIER_NODE);
}

/* Fix up fields of a tree_common T.  DATA points to fix-up states.  */

static void
lto_fixup_common (tree t, void *data)
{
  LTO_FIXUP_SUBTREE (TREE_TYPE (t));
  /* This is not very efficient because we cannot do tail-recursion with
     a long chain of trees. */
  LTO_FIXUP_SUBTREE (TREE_CHAIN (t));
}

/* Fix up fields of a decl_minimal T.  DATA points to fix-up states.  */

static void
lto_fixup_decl_minimal (tree t, void *data)
{
  lto_fixup_common (t, data);
  LTO_FIXUP_SUBTREE (DECL_NAME (t));
  LTO_FIXUP_SUBTREE (DECL_CONTEXT (t));
}

/* Fix up fields of a decl_common T.  DATA points to fix-up states.  */

static void
lto_fixup_decl_common (tree t, void *data)
{
  lto_fixup_decl_minimal (t, data);
  gcc_assert (no_fixup_p (DECL_SIZE (t)));
  gcc_assert (no_fixup_p (DECL_SIZE_UNIT (t)));
  LTO_FIXUP_SUBTREE (DECL_INITIAL (t));
  LTO_FIXUP_SUBTREE (DECL_ATTRIBUTES (t));
  LTO_FIXUP_SUBTREE (DECL_ABSTRACT_ORIGIN (t));
}

/* Fix up fields of a decl_with_vis T.  DATA points to fix-up states.  */

static void
lto_fixup_decl_with_vis (tree t, void *data)
{
  lto_fixup_decl_common (t, data);

  /* Accessor macro has side-effects, use field-name here. */
  LTO_FIXUP_SUBTREE (t->decl_with_vis.assembler_name);

  gcc_assert (no_fixup_p (DECL_SECTION_NAME (t)));
}

/* Fix up fields of a decl_non_common T.  DATA points to fix-up states.  */

static void
lto_fixup_decl_non_common (tree t, void *data)
{
  lto_fixup_decl_with_vis (t, data);
  LTO_FIXUP_SUBTREE (DECL_ARGUMENT_FLD (t));
  LTO_FIXUP_SUBTREE (DECL_RESULT_FLD (t));
  LTO_FIXUP_SUBTREE (DECL_VINDEX (t));

  /* SAVED_TREE should not cleared by now.  Also no accessor for base type. */
  gcc_assert (no_fixup_p (t->decl_non_common.saved_tree));
}

/* Fix up fields of a decl_non_common T.  DATA points to fix-up states.  */

static void
lto_fixup_function (tree t, void *data)
{
  lto_fixup_decl_non_common (t, data);
  LTO_FIXUP_SUBTREE (DECL_FUNCTION_PERSONALITY (t));
}

/* Fix up fields of a field_decl T.  DATA points to fix-up states.  */

static void
lto_fixup_field_decl (tree t, void *data)
{
  lto_fixup_decl_common (t, data);
  gcc_assert (no_fixup_p (DECL_FIELD_OFFSET (t)));
  LTO_FIXUP_SUBTREE (DECL_BIT_FIELD_TYPE (t));
  LTO_FIXUP_SUBTREE (DECL_QUALIFIER (t));
  gcc_assert (no_fixup_p (DECL_FIELD_BIT_OFFSET (t)));
  LTO_FIXUP_SUBTREE (DECL_FCONTEXT (t));
}

/* Fix up fields of a type T.  DATA points to fix-up states.  */

static void
lto_fixup_type (tree t, void *data)
{
  lto_fixup_common (t, data);
  LTO_FIXUP_SUBTREE (TYPE_CACHED_VALUES (t));
  gcc_assert (no_fixup_p (TYPE_SIZE (t)));
  gcc_assert (no_fixup_p (TYPE_SIZE_UNIT (t)));
  LTO_FIXUP_SUBTREE (TYPE_ATTRIBUTES (t));
  LTO_FIXUP_SUBTREE (TYPE_POINTER_TO (t));
  LTO_FIXUP_SUBTREE (TYPE_REFERENCE_TO (t));
  LTO_FIXUP_SUBTREE (TYPE_NAME (t));

  /* Accessors are for derived node types only. */
  LTO_FIXUP_SUBTREE (t->type.minval);
  LTO_FIXUP_SUBTREE (t->type.maxval);

  LTO_FIXUP_SUBTREE (TYPE_NEXT_VARIANT (t));
  LTO_FIXUP_SUBTREE (TYPE_MAIN_VARIANT (t));

  /* Accessor is for derived node types only. */
  LTO_FIXUP_SUBTREE (t->type.binfo);

  LTO_FIXUP_SUBTREE (TYPE_CONTEXT (t));
  LTO_FIXUP_SUBTREE (TYPE_CANONICAL (t));
}

/* Fix up fields of a BINFO T.  DATA points to fix-up states.  */

static void
lto_fixup_binfo (tree t, void *data)
{
  unsigned HOST_WIDE_INT i, n;
  tree base, saved_base;

  lto_fixup_common (t, data);
  gcc_assert (no_fixup_p (BINFO_OFFSET (t)));
  LTO_FIXUP_SUBTREE (BINFO_VTABLE (t));
  LTO_FIXUP_SUBTREE (BINFO_VIRTUALS (t));
  LTO_FIXUP_SUBTREE (BINFO_VPTR_FIELD (t));
  n = VEC_length (tree, BINFO_BASE_ACCESSES (t));
  for (i = 0; i < n; i++)
    {
      saved_base = base = BINFO_BASE_ACCESS (t, i);
      LTO_FIXUP_SUBTREE (base);
      if (base != saved_base)
	VEC_replace (tree, BINFO_BASE_ACCESSES (t), i, base);
    }
  LTO_FIXUP_SUBTREE (BINFO_INHERITANCE_CHAIN (t));
  LTO_FIXUP_SUBTREE (BINFO_SUBVTT_INDEX (t));
  LTO_FIXUP_SUBTREE (BINFO_VPTR_INDEX (t));
  n = BINFO_N_BASE_BINFOS (t);
  for (i = 0; i < n; i++)
    {
      saved_base = base = BINFO_BASE_BINFO (t, i);
      LTO_FIXUP_SUBTREE (base);
      if (base != saved_base)
	VEC_replace (tree, BINFO_BASE_BINFOS (t), i, base);
    }
}

/* A walk_tree callback used by lto_fixup_state. TP is the pointer to the
   current tree. WALK_SUBTREES indicates if the subtrees will be walked.
   DATA is a pointer set to record visited nodes. */

static tree
lto_fixup_tree (tree *tp, int *walk_subtrees, void *data)
{
  tree t;
  lto_fixup_data_t *fixup_data = (lto_fixup_data_t *) data;
  tree prevailing;

  t = *tp;
  *walk_subtrees = 0;
  if (pointer_set_contains (fixup_data->seen, t))
    return NULL;

  if (TREE_CODE (t) == VAR_DECL || TREE_CODE (t) == FUNCTION_DECL)
    {
      prevailing = lto_symtab_prevailing_decl (t);

      if (t != prevailing)
	{
	  if (TREE_CODE (t) == FUNCTION_DECL
	      && TREE_NOTHROW (prevailing) != TREE_NOTHROW (t))
	    {
	      /* If the prevailing definition does not throw but the
		 declaration (T) was considered throwing, then we
		 simply add PREVAILING to the list of throwing
		 functions.  However, if the opposite is true, then
		 the call to PREVAILING was generated assuming that
		 the function didn't throw, which means that CFG
		 cleanup may have removed surrounding try/catch
		 regions.

		 Note that we currently accept these cases even when
		 they occur within a single file.  It's certainly a
		 user error, but we silently allow the compiler to
		 remove surrounding try/catch regions.  Perhaps we
		 could emit a warning here, instead of silently
		 accepting the conflicting declaration.  */
	      if (TREE_NOTHROW (prevailing))
		lto_mark_nothrow_fndecl (prevailing);
	    }

	  pointer_set_insert (fixup_data->free_list, t);

	   /* Also replace t with prevailing defintion.  We don't want to
	      insert the other defintion in the seen set as we want to
	      replace all instances of it.  */
	  *tp = prevailing;
	  t = prevailing;
	}
    }

  pointer_set_insert (fixup_data->seen, t);

  /* walk_tree does not visit all reachable nodes that need to be fixed up.
     Hence we do special processing here for those kind of nodes. */
  switch (TREE_CODE (t))
    {
    case FIELD_DECL:
      lto_fixup_field_decl (t, data);
      break;

    case LABEL_DECL:
    case CONST_DECL:
    case PARM_DECL:
    case RESULT_DECL:
      lto_fixup_decl_common (t, data);
      break;

    case VAR_DECL:
      lto_fixup_decl_with_vis (t, data);
      break;	

    case TYPE_DECL:
      lto_fixup_decl_non_common (t, data);
      break;

    case FUNCTION_DECL:
      lto_fixup_function (t, data);
      break;

    case TREE_BINFO:
      lto_fixup_binfo (t, data);
      break;

    default:
      if (TYPE_P (t))
	lto_fixup_type (t, data);
      else if (EXPR_P (t))
	{
	  /* walk_tree only handles TREE_OPERANDs. Do the rest here.  */
	  lto_fixup_common (t, data);
	  LTO_FIXUP_SUBTREE (t->exp.block);
	  *walk_subtrees = 1;
	}
      else
	{
	  /* Let walk_tree handle sub-trees.  */
	  *walk_subtrees = 1;
	}
    }

  return NULL;
}

/* Helper function of lto_fixup_decls. Walks the var and fn streams in STATE,
   replaces var and function decls with the corresponding prevailing def and
   records the old decl in the free-list in DATA. We also record visted nodes
   in the seen-set in DATA to avoid multiple visit for nodes that need not
   to be replaced.  */

static void
lto_fixup_state (struct lto_in_decl_state *state, lto_fixup_data_t *data)
{
  unsigned i, si;
  struct lto_tree_ref_table *table;

  /* Although we only want to replace FUNCTION_DECLs and VAR_DECLs,
     we still need to walk from all DECLs to find the reachable
     FUNCTION_DECLs and VAR_DECLs.  */
  for (si = 0; si < LTO_N_DECL_STREAMS; si++)
    {
      table = &state->streams[si];
      for (i = 0; i < table->size; i++)
	walk_tree (table->trees + i, lto_fixup_tree, data, NULL);
    }
}

/* A callback of htab_traverse. Just extract a state from SLOT and the
   lto_fixup_data_t object from AUX and calls lto_fixup_state. */

static int
lto_fixup_state_aux (void **slot, void *aux)
{
  struct lto_in_decl_state *state = (struct lto_in_decl_state *) *slot;
  lto_fixup_state (state, (lto_fixup_data_t *) aux);
  return 1;
}

/* A callback to pointer_set_traverse. Frees the tree pointed by p. Removes
   from it from the UID -> DECL mapping. */

static bool
free_decl (const void *p, void *data ATTRIBUTE_UNUSED)
{
  const_tree ct = (const_tree) p;
  tree t = CONST_CAST_TREE (ct);

  lto_symtab_clear_resolution (t);
  ggc_free (t);
  return true;
}

/* Fix the decls from all FILES. Replaces each decl with the corresponding
   prevailing one.  */

static void
lto_fixup_decls (struct lto_file_decl_data **files)
{
  unsigned int i;
  tree decl;
  struct pointer_set_t *free_list = pointer_set_create ();
  struct pointer_set_t *seen = pointer_set_create ();
  lto_fixup_data_t data;

  data.free_list = free_list;
  data.seen = seen;
  for (i = 0; files[i]; i++)
    {
      struct lto_file_decl_data *file = files[i];
      struct lto_in_decl_state *state = file->global_decl_state;
      lto_fixup_state (state, &data);

      htab_traverse (file->function_decl_states, lto_fixup_state_aux, &data);
    }

  for (i = 0; VEC_iterate (tree, lto_global_var_decls, i, decl); i++)
    {
      tree saved_decl = decl;
      walk_tree (&decl, lto_fixup_tree, &data, NULL);
      if (decl != saved_decl)
	VEC_replace (tree, lto_global_var_decls, i, decl);
    }

  pointer_set_traverse (free_list, free_decl, NULL);
  pointer_set_destroy (free_list);
  pointer_set_destroy (seen);
}

/* Unlink a temporary LTRANS file unless requested otherwise.  */

static void
lto_maybe_unlink (const char *file)
{
  if (!getenv ("WPA_SAVE_LTRANS"))
    {
      if (unlink_if_ordinary (file))
        error ("deleting LTRANS file %s: %m", file);
    }
  else
    fprintf (stderr, "[Leaving LTRANS %s]\n", file);
}

/* Read the options saved from each file in the command line.  Called
   from lang_hooks.post_options which is called by process_options
   right before all the options are used to initialize the compiler.
   This assumes that decode_options has already run, so the
   num_in_fnames and in_fnames are properly set.

   FIXME lto, this assumes that all the files had been compiled with
   the same options, which is not a good assumption.  In general,
   options ought to be read from all the files in the set and merged.
   However, it is still unclear what the merge rules should be.  */

void
lto_read_all_file_options (void)
{
  size_t i;

  /* Clear any file options currently saved.  */
  lto_clear_file_options ();

  /* Set the hooks to read ELF sections.  */
  lto_set_in_hooks (NULL, get_section_data, free_section_data);

  for (i = 0; i < num_in_fnames; i++)
    {
      struct lto_file_decl_data *file_data;
      lto_file *file = lto_elf_file_open (in_fnames[i], false);
      if (!file)
	break;

      file_data = XCNEW (struct lto_file_decl_data);
      file_data->file_name = file->filename;
      file_data->fd = -1;
      file_data->section_hash_table = lto_elf_build_section_table (file);

      lto_read_file_options (file_data);

      lto_elf_file_close (file);
      htab_delete (file_data->section_hash_table);
      if (file_data->fd != -1)
	close (file_data->fd);
      free (file_data);
    }

  /* Apply globally the options read from all the files.  */
  lto_reissue_options ();
}


/* Read all the symbols from the input files FNAMES.  NFILES is the
   number of files requested in the command line.  Instantiate a
   global call graph by aggregating all the sub-graphs found in each
   file.  */

static void
read_cgraph_and_symbols (unsigned nfiles, const char **fnames)
{
  unsigned int i, last_file_ix;
  struct lto_file_decl_data **all_file_decl_data;
  FILE *resolution;

  lto_stats.num_input_files = nfiles;

  timevar_push (TV_IPA_LTO_DECL_IO);

  /* Set the hooks so that all of the ipa passes can read in their data.  */
  all_file_decl_data = XNEWVEC (struct lto_file_decl_data *, nfiles + 1);
  lto_set_in_hooks (all_file_decl_data, get_section_data, free_section_data);

  /* Read the resolution file.  */
  resolution = NULL;
  if (resolution_file_name)
    {
      int t;
      unsigned num_objects;

      resolution = fopen (resolution_file_name, "r");
      gcc_assert (resolution != NULL);
      t = fscanf (resolution, "%u", &num_objects);
      gcc_assert (t == 1);

      /* True, since the plugin splits the archives.  */
      gcc_assert (num_objects == nfiles);
    }

  /* Read all of the object files specified on the command line.  */
  for (i = 0, last_file_ix = 0; i < nfiles; ++i)
    {
      struct lto_file_decl_data *file_data = NULL;

      current_lto_file = lto_elf_file_open (fnames[i], false);
      if (!current_lto_file)
	break;

      file_data = lto_file_read (current_lto_file, resolution);
      if (!file_data)
	break;

      all_file_decl_data[last_file_ix++] = file_data;

      lto_elf_file_close (current_lto_file);
      current_lto_file = NULL;
    }

  if (resolution_file_name)
    fclose (resolution);

  all_file_decl_data[last_file_ix] = NULL;

  /* Set the hooks so that all of the ipa passes can read in their data.  */
  lto_set_in_hooks (all_file_decl_data, get_section_data, free_section_data);

  /* Each pass will set the appropriate timer.  */
  timevar_pop (TV_IPA_LTO_DECL_IO);

  ipa_read_summaries ();

  timevar_push (TV_IPA_LTO_DECL_IO);

  lto_fixup_decls (all_file_decl_data);

  /* FIXME lto. This loop needs to be changed to use the pass manager to
     call the ipa passes directly.  */
  if (!errorcount)
    for (i = 0; i < last_file_ix; i++)
      {
	struct lto_file_decl_data *file_data = all_file_decl_data [i];
	lto_materialize_constructors_and_inits (file_data);
      }

  timevar_pop (TV_IPA_LTO_DECL_IO);
}


/* Materialize all the bodies for all the nodes in the callgraph.  */

static void
materialize_cgraph (void)
{
  tree decl;
  struct cgraph_node *node; 
  unsigned i;
  timevar_id_t lto_timer;

  /* Now that we have input the cgraph, we need to clear all of the aux
     nodes and read the functions if we are not running in WPA mode.  

     FIXME lto.  When not operating in WPA mode, this loop will:

     1) Load all of the functions at once.  
     2) Close and reopen the files over and over again. 

     It would obviously be better for the cgraph code to look to load
     a batch of functions and sort those functions by the file they
     come from and then load all of the functions from a give .o file
     at one time.  This of course will require that the open and close
     code be pulled out of lto_materialize_function, but that is a
     small part of what will be a complex set of management
     issues.  */
  timevar_push (TV_IPA_LTO_GIMPLE_IO);

  for (node = cgraph_nodes; node; node = node->next)
    {
      lto_materialize_function (node);
      lto_stats.num_input_cgraph_nodes++;
    }

  timevar_pop (TV_IPA_LTO_GIMPLE_IO);

  /* Start the appropriate timer depending on the mode that we are
     operating in.  */
  lto_timer = (flag_wpa) ? TV_WHOPR_WPA
	      : (flag_ltrans) ? TV_WHOPR_LTRANS
	      : TV_LTO;
  timevar_push (lto_timer);

  current_function_decl = NULL;
  set_cfun (NULL);

  /* Inform the middle end about the global variables we have seen.  */
  for (i = 0; VEC_iterate (tree, lto_global_var_decls, i, decl); i++)
    rest_of_decl_compilation (decl, 1, 0);

  /* Fix up any calls to DECLs that have become not exception throwing.  */
  lto_fixup_nothrow_decls ();

  timevar_pop (lto_timer);
}


/* Perform whole program analysis (WPA) on the callgraph and write out the
   optimization plan.  */

static void
do_whole_program_analysis (void)
{
  char **output_files;
  size_t i;
  struct cgraph_node *node; 

  lto_1_to_1_map ();

  /* Note that since we are in WPA mode, materialize_cgraph will not
     actually read in all the function bodies.  It only materializes
     the decls and cgraph nodes so that analysis can be performed.  */
  materialize_cgraph ();

  /* Reading in the cgraph uses different timers, start timing WPA now.  */
  timevar_push (TV_WHOPR_WPA);

  /* FIXME lto. Hack. We should use the IPA passes.  There are a
     number of issues with this now. 1. There is no convenient way to
     do this. 2. Some passes may depend on properties that requires
     the function bodies to compute.  */
  cgraph_function_flags_ready = true;
  bitmap_obstack_initialize (NULL);
  ipa_register_cgraph_hooks ();

  /* Reset inlining information before running IPA inliner.  */
  for (node = cgraph_nodes; node; node = node->next)
    reset_inline_failed (node);

  /* FIXME lto.  We should not call this function directly. */
  pass_ipa_inline.pass.execute ();

  verify_cgraph ();
  bitmap_obstack_release (NULL);

  /* We are about to launch the final LTRANS phase, stop the WPA timer.  */
  timevar_pop (TV_WHOPR_WPA);

  output_files = lto_wpa_write_files ();
  lto_execute_ltrans (output_files);

  for (i = 0; output_files[i]; ++i)
    {
      lto_maybe_unlink (output_files[i]);
      free (output_files[i]);
    }
  XDELETEVEC (output_files);
}


/* Main entry point for the GIMPLE front end.  This front end has
   three main personalities:

   - LTO (-flto).  All the object files on the command line are
     loaded in memory and processed as a single translation unit.
     This is the traditional link-time optimization behavior.

   - WPA (-fwpa).  Only the callgraph and summary information for
     files in the command file are loaded.  A single callgraph
     (without function bodies) is instantiated for the whole set of
     files.  IPA passes are only allowed to analyze the call graph
     and make transformation decisions.  The callgraph is
     partitioned, each partition is written to a new object file
     together with the transformation decisions.

   - LTRANS (-fltrans).  Similar to -flto but it prevents the IPA
     summary files from running again.  Since WPA computed summary
     information and decided what transformations to apply, LTRANS
     simply applies them.  FIXME lto, it may be possible to remove
     this flag and just use -flto for LTRANS.  */

void
lto_main (int debug_p ATTRIBUTE_UNUSED)
{
  /* Initialize stats counters.  */
  memset (&lto_stats, 0, sizeof (lto_stats));

  /* Read all the symbols and call graph from all the files in the
     command line.  */
  read_cgraph_and_symbols (num_in_fnames, in_fnames);

  if (!errorcount)
    {
      /* If WPA is enabled analyze the whole call graph and create an
	 optimization plan.  Otherwise, read in all the function
	 bodies and continue with optimization.  */
      if (flag_wpa)
	do_whole_program_analysis ();
      else
	{
	  materialize_cgraph ();

	  /* Let the middle end know that we have read and merged all of
	     the input files.  */ 
	  cgraph_optimize ();
	}
    }

  if (flag_lto_report)
    print_lto_report ();
}

#include "gt-lto-lto.h"
