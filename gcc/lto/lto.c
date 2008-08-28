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

#include <sys/mman.h>
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "opts.h"
#include "toplev.h"
#include "tree.h"
#include "tm.h"
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
#include "vec.h"
#include "bitmap.h"
#include "pointer-set.h"
#include "ipa-prop.h"

static bitmap_obstack lto_bitmap_obstack;

/* Read the constructors and inits.  */

static void
lto_materialize_constructors_and_inits (struct lto_file_decl_data * file_data)
{
  size_t len;
  const char *data = lto_get_section_data (file_data, 
					   LTO_section_static_initializer, NULL, &len);
  lto_input_constructors_and_inits (file_data, data);
  lto_free_section_data (file_data, LTO_section_static_initializer, NULL, data, len);
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
  name = lto_original_decl_name (file_data, name);

  data = lto_get_section_data (file_data, LTO_section_function_body,
			       name, &len);
  if (data)
    {
      struct function *fn;

      /* This function has a definition.  */
      TREE_STATIC (decl) = 1;
      DECL_EXTERNAL (decl) = 0;

      allocate_struct_function (decl, false);
      if (!flag_wpa)
         lto_input_function_body (file_data, decl, data);
      fn = DECL_STRUCT_FUNCTION (decl);
      lto_free_section_data (file_data, LTO_section_function_body, name,
			     data, len);

      /* Look for initializers of constant variables and private
	 statics.  */
      for (step = fn->local_decls;
	   step;
	   step = TREE_CHAIN (step))
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
  rest_of_decl_compilation (decl,
                            /*top_level=*/1,
                            /*at_end=*/0);
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
lto_read_decls (struct lto_file_decl_data *decl_data, const void *data)
{
  const struct lto_decl_header * header 
      = (const struct lto_decl_header *) data;
  const int32_t decl_offset = sizeof (struct lto_decl_header);
  const int32_t main_offset = decl_offset + header->decl_state_size;
  const int32_t string_offset = main_offset + header->main_size;
#ifdef LTO_STREAM_DEBUGGING
  int32_t debug_main_offset;
#endif

  struct lto_input_block ib_main;
  struct lto_input_block debug_main;
  struct data_in data_in;
  unsigned int i;
  const uint32_t *data_ptr, *data_end;
  uint32_t num_decl_states;

#ifdef LTO_STREAM_DEBUGGING
  debug_main_offset = string_offset + header->string_size;
#endif
  
  LTO_INIT_INPUT_BLOCK (ib_main, (const char*) data + main_offset, 0, header->main_size);
#ifdef LTO_STREAM_DEBUGGING
  LTO_INIT_INPUT_BLOCK (debug_main, (const char*) data + debug_main_offset, 0, header->debug_main_size);
#endif

  memset (&data_in, 0, sizeof (struct data_in));
  data_in.file_data          = decl_data;
  data_in.strings            = (const char *) data + string_offset;
  data_in.strings_len        = header->string_size;
  data_in.globals_index	     = NULL;

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
      tree dummy;

      input_tree (&dummy, &ib_main, &data_in);
      gcc_assert (ib_main.p <= ib_main.len);
    }

  /* Read in lto_in_decl_state objects. */

  data_ptr = (const uint32_t*) ((const char*) data + decl_offset); 
  data_end =
     (const uint32_t*) ((const char*) data_ptr + header->decl_state_size);
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
}

/* Generate a TREE representation for all types and external decls
   entities in FILE.  If an entity in FILE has already been read (from
   another object file), merge the two entities.  Returns the
   file_data from the last context.

   FIXME, this is a bug that will go away with Maddox's streaming
   merge since there will no longer be contexts.

   Read all of the globals out of the file.  Then read the cgraph
   and process the .o index into the cgraph nodes so that it can open
   the .o file to load the functions and ipa information.   */

static struct lto_file_decl_data*
lto_file_read (lto_file *file)
{
  struct lto_file_decl_data* file_data;
  const char *data;
  size_t len;
  htab_t section_hash_table;
  htab_t renaming_hash_table;

  file_data = XNEW (struct lto_file_decl_data);

  file_data->file_name = file->filename;
  file_data->fd = -1;

  section_hash_table = lto_elf_build_section_table (file);
  file_data->section_hash_table = section_hash_table;

  renaming_hash_table = lto_create_renaming_table ();
  file_data->renaming_hash_table = renaming_hash_table;
  
  data = lto_get_section_data (file_data, LTO_section_decls, NULL, &len);
  lto_read_decls (file_data, data);
  lto_free_section_data (file_data, LTO_section_decls, NULL, data, len);

  /* FIXME: We never free file_data.  */

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
   NAME will be null unless the section type is for a function
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
  char * data = NULL;

  s_slot.name = section_name;
  f_slot = (struct lto_section_slot *)htab_find (section_hash_table, &s_slot);
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
   right now. */

static void
lto_1_to_1_map (void)
{
  struct cgraph_node *node;
  struct lto_file_decl_data *file_data;
  struct pointer_map_t *pmap;
  cgraph_node_set set;
  void **slot;

  lto_cgraph_node_sets = VEC_alloc (cgraph_node_set, gc, 1);
  pmap = pointer_map_create ();

  for (node = cgraph_nodes; node; node = node->next)
    {
      /* We assume file_data are unique. */
      file_data = node->local.lto_file_data;
      gcc_assert(file_data);

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
}

/* Compute the transitive closure of inlining of SET based on the
   information in the call-graph.  Insert the inlinees into SET.  */

static void
lto_add_all_inlinees (cgraph_node_set set)
{
  cgraph_node_set_iterator csi;
  struct cgraph_node *node, *callee;
  size_t i, orig_size = cgraph_node_set_size (set);
  VEC(cgraph_node_ptr,heap) *queue =
    VEC_alloc(cgraph_node_ptr,heap, orig_size);
  struct cgraph_edge *edge;
  bitmap queued_p = BITMAP_ALLOC (&lto_bitmap_obstack);

  /* Perform a breadth-first-search to find the transitive closure
     of inlinees. */

  /* Nodes in SET are root of BFS.  */
  for (csi = csi_start (set); !csi_end_p (csi); csi_next (&csi))
    {
      node = csi_node (csi);
      bitmap_set_bit (queued_p, node->uid);
      VEC_quick_push (cgraph_node_ptr, queue, node);
    }

  i = 0;
  while (i < VEC_length (cgraph_node_ptr, queue))
    {
      node = VEC_index (cgraph_node_ptr, queue, i);
      for (edge = node->callees; edge != NULL; edge = edge->next_callee)
	if (edge->inline_failed == NULL)
	  {
	    callee = edge->callee;
	    if (!bitmap_bit_p (queued_p, callee->uid))
	      {
		bitmap_set_bit (queued_p, callee->uid);
		VEC_safe_push (cgraph_node_ptr, heap, queue, callee);
	      }
	  }
      i++;
    }

  /* Add found inlinees to set.  We can skip entries [0..orig_size)
     in queue as these are already in set. */
  for (i = orig_size; i < VEC_length (cgraph_node_ptr, queue); i++)
    {
      node = VEC_index (cgraph_node_ptr, queue, i);
      cgraph_node_set_add (set, node);
    }

  BITMAP_FREE (queued_p);
}

static lto_file *current_lto_file;

/* Write all output files in WPA modes. */

static void
lto_wpa_write_files (void)
{
  unsigned i;
  char temp_filename[100];
  lto_file *file;
  cgraph_node_set set;

  for (i = 0; VEC_iterate (cgraph_node_set, lto_cgraph_node_sets, i, set); i++)
    {
      sprintf (temp_filename, "bogus%d.lto.o", i);
      fprintf (stderr, "output to %s\n", temp_filename);

      file = lto_elf_file_open (temp_filename, /*writable=*/true);
      if (!file)
        fatal_error ("lto_elf_file_open() failed");

      lto_set_current_out_file (file);

      /* Include all inlined function. */
      lto_add_all_inlinees (set);

      ipa_write_summaries_of_cgraph_node_set (set);
      
      lto_set_current_out_file (NULL);
      lto_elf_file_close (file);
    } 
}

void
lto_main (int debug_p ATTRIBUTE_UNUSED)
{
  unsigned int i;
  unsigned int j = 0;
  tree decl;
  struct cgraph_node *node; 
  struct lto_file_decl_data** all_file_decl_data 
    = XNEWVEC (struct lto_file_decl_data*, num_in_fnames + 1);
  struct lto_file_decl_data* file_data = NULL;

  /* Set the hooks so that all of the ipa passes can read in their data.  */
  lto_set_in_hooks (all_file_decl_data, get_section_data,
		    free_section_data);

  bitmap_obstack_initialize (&lto_bitmap_obstack);

  /* Read all of the object files specified on the command line.  */
  for (i = 0; i < num_in_fnames; ++i)
    {
      current_lto_file = lto_elf_file_open (in_fnames[i], /*writable=*/false);
      if (!current_lto_file)
	break;
      file_data = lto_file_read (current_lto_file);
      if (!file_data)
	break;

      all_file_decl_data [j++] = file_data;

      lto_elf_file_close (current_lto_file);
      current_lto_file = NULL;
    }

  all_file_decl_data [j] = NULL;

  /* Set the hooks so that all of the ipa passes can read in their data.  */
  lto_set_in_hooks (all_file_decl_data, get_section_data,
		    free_section_data);

  /* FIXME!!! This loop needs to be changed to use the pass manager to
     call the ipa passes directly.  */
  for (i = 0; i < j; i++)
    {
      struct lto_file_decl_data* file_data = all_file_decl_data [i];

      lto_materialize_constructors_and_inits (file_data);
    }

  ipa_read_summaries ();

  if (flag_wpa)
    lto_1_to_1_map ();

  /* Now that we have input the cgraph, we need to clear all of the aux
     nodes and read the functions if we are not running in WPA mode.  

     FIXME!!!!! This loop obviously leaves a lot to be desired:
     1) it loads all of the functions at once.  
     2) it closes and reopens the files over and over again. 

     It would obviously be better for the cgraph code to look to load
     a batch of functions and sort those functions by the file they
     come from and then load all of the functions from a give .o file
     at one time.  This of course will require that the open and close
     code be pulled out of lto_materialize_function, but that is a
     small part of what will be a complex set of management
     issues.  */
  for (node = cgraph_nodes; node; node = node->next)
    {
      /* FIXME!!!  There really needs to be some check to see if the
	 function is really not external here.  Currently the only
	 check is to see if the section was defined in the file_data
	 index.  There is of course the value in the node->aux field
	 that is nulled out in the previous line, but we should really
	 be able to look at the cgraph info at the is point and make
	 the proper determination.   Honza will fix this.  */
      lto_materialize_function (node);
    }
  current_function_decl = NULL;
  set_cfun (NULL);

  /* Inform the middle end about the global variables we have seen.  */
  for (i = 0; VEC_iterate (tree, lto_global_var_decls, i, decl); i++)
    rest_of_decl_compilation (decl,
                              /*top_level=*/1,
                              /*at_end=*/0);

  /* This is some bogus wrapper code for development testing.  It will be
     replaced once some basic WPA partitioning logic is implemented.  To use
     this pass "-flto -fsyntax-only" to the lto1 invocation.  */
  if (flag_generate_lto)
    {
      lto_file *file;

      file = lto_elf_file_open ("bogus.lto.o", /*writable=*/true);
      if (!file)
	fatal_error ("lto_elf_file_open() failed");
      lto_set_current_out_file (file);
    }

  /* Let the middle end know that we have read and merged all of the
     input files.  */ 
  /*cgraph_finalize_compilation_unit ();*/
  if (!flag_wpa)
    cgraph_optimize ();
  else
    {
      /* FIXME-lto: Hack. We should use the IPA passes.  There are a number
         of issues with this now. 1. There is no convenient way to do this.
         2. Some passes may depend on properties that requires the function
	 bodies to compute.  */
      cgraph_function_flags_ready = true;
      bitmap_obstack_initialize (NULL);
      ipa_register_cgraph_hooks ();
      for (node = cgraph_nodes; node; node = node->next)
	{
	  struct cgraph_edge *e;
	
	  for (e = node->callees; e != NULL; e = e->next_callee)
	    e->inline_failed = NULL;
	}

      /* FIXME: We should not call this function directly. */
      pass_ipa_inline.pass.execute ();

      bitmap_obstack_release (NULL);
    }

  /* This is the continuation of the previous bogus wrapper code.  It will be
     replaced once some basic WPA partitioning logic is implemented.  */
  if (flag_generate_lto)
    {
      lto_file *file;

      file = lto_set_current_out_file (NULL);
      lto_elf_file_close (file);
    }

  if (flag_wpa)
    {
      lto_wpa_write_files ();
    }

  bitmap_obstack_release (&lto_bitmap_obstack);
}

#include "gt-lto-lto.h"
