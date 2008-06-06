/* Input functions for reading lto sections.

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
#include "lto-section.h"
#include "lto-section-in.h"
#include <ctype.h>
#include "cpplib.h"


/* Return 0 or 1 based on the last bit of FLAGS and right shift FLAGS
   by 1.  */

unsigned int
lto_get_flag (unsigned HOST_WIDEST_INT *flags)
{
  unsigned int result = *flags & 1;
  *flags = *flags >> 1;
  return result;
}


/* Return an integer based on the last WIDTH bits of FLAGS and right
   shift FLAGS by WIDTH.  */

unsigned int
lto_get_flags (unsigned HOST_WIDEST_INT *flags, unsigned int width)
{
  unsigned HOST_WIDEST_INT mask = 1 << (((unsigned HOST_WIDEST_INT)width) - 1);
  unsigned int result = *flags & mask;
  *flags = *flags >> width;
  return result;
}


unsigned char 
lto_input_1_unsigned (struct lto_input_block *ib)
{
  gcc_assert (ib->p < ib->len);
  return (ib->data[ib->p++]);
}


/* Read an ULEB128 Number of IB.  */

unsigned HOST_WIDE_INT 
lto_input_uleb128 (struct lto_input_block *ib)
{
  unsigned HOST_WIDE_INT result = 0;
  int shift = 0;
  unsigned HOST_WIDE_INT byte;

  while (true)
    {
      byte = lto_input_1_unsigned (ib);
      result |= (byte & 0x7f) << shift;
      shift += 7;
      if ((byte & 0x80) == 0)
	{
	  LTO_DEBUG_WIDE ("U", result);
	  return result;
	}
    }
}

/* HOST_WIDEST_INT version of lto_input_uleb128.  IB is as in lto_input_uleb128.  */

unsigned HOST_WIDEST_INT 
lto_input_widest_uint_uleb128 (struct lto_input_block *ib)
{
  unsigned HOST_WIDEST_INT result = 0;
  int shift = 0;
  unsigned HOST_WIDEST_INT byte;

  while (true)
    {
      byte = lto_input_1_unsigned (ib);
      result |= (byte & 0x7f) << shift;
      shift += 7;
      if ((byte & 0x80) == 0)
	{
	  LTO_DEBUG_WIDE ("U", result);
	  return result;
	}
    }
}

/* Read an SLEB128 Number of IB.  */

HOST_WIDE_INT 
lto_input_sleb128 (struct lto_input_block *ib)
{
  HOST_WIDE_INT result = 0;
  int shift = 0;
  unsigned HOST_WIDE_INT byte;

  while (true)
    {
      byte = lto_input_1_unsigned (ib);
      result |= (byte & 0x7f) << shift;
      shift += 7;
      if ((byte & 0x80) == 0)
	{
	  if ((shift < HOST_BITS_PER_WIDE_INT) && (byte & 0x40))
	    result |= - ((HOST_WIDE_INT)1 << shift);

	  LTO_DEBUG_WIDE ("S", result);
	  return result;
	}
    }
}


/* Input the next integer constant of TYPE in IB.  */

tree
lto_input_integer (struct lto_input_block *ib, tree type)
{
  HOST_WIDE_INT low = 0;
  HOST_WIDE_INT high = 0;
  int shift = 0;
  unsigned HOST_WIDE_INT byte;

  while (true)
    {
      byte = lto_input_1_unsigned (ib);
      if (shift < HOST_BITS_PER_WIDE_INT - 7)
	/* Working on the low part.  */
	low |= (byte & 0x7f) << shift;
      else if (shift >= HOST_BITS_PER_WIDE_INT)
	/* Working on the high part.  */
	high |= (byte & 0x7f) << (shift - HOST_BITS_PER_WIDE_INT);
      else
	{
	  /* Working on the transition between the low and high parts.  */
	  low |= (byte & 0x7f) << shift;
	  high |= (byte & 0x7f) >> (HOST_BITS_PER_WIDE_INT - shift);
	}

      shift += 7;
      if ((byte & 0x80) == 0)
	{
	  if (byte & 0x40)
	    {
	      /* The number is negative.  */
	      if (shift < HOST_BITS_PER_WIDE_INT)
		{
		  low |= - ((HOST_WIDE_INT)1 << shift);
		  high = -1;
		}
	      else if (shift < (2 * HOST_BITS_PER_WIDE_INT))
		high |= - ((HOST_WIDE_INT)1 << (shift - HOST_BITS_PER_WIDE_INT));
	    }

#ifdef LTO_STREAM_DEBUGGING
	  /* Have to match the quick out in the lto writer.  */
	  if (((high == -1) && (low < 0))
	      || ((high == 0) && (low >= 0)))
	    LTO_DEBUG_WIDE ("S", low);
	  else 
	    LTO_DEBUG_INTEGER ("SS", high, low);
#endif	  
	  return build_int_cst_wide (type, low, high);
	}
    }
}


/* Hooks so that the ipa passes can call into the lto front end to get
   sections.  */

static struct lto_file_decl_data ** file_decl_data; 
static lto_get_section_data_f* get_section_f;
static lto_free_section_data_f* free_section_f;


/* This is called from the lto front end to set up the hooks that are
   used by the ipa passes to get the data that they will
   deserialize.  */

void 
lto_set_in_hooks (struct lto_file_decl_data ** data, 
		  lto_get_section_data_f* get_f,
		  lto_free_section_data_f* free_f)
{
  file_decl_data = data;
  get_section_f = get_f;
  free_section_f = free_f;
}


/* Return an array of file decl datas for all of the files passed to
   this compilation.  */

struct lto_file_decl_data **
lto_get_file_decl_data (void)
{
  gcc_assert (file_decl_data);
  return file_decl_data;
}

/* Return a char pointer to the start of a data stream for an lto pass
   or function.  FILE_DATA indicates where to obtain the data.
   SECTION_TYPE is the type of information to be obtained.  NAME is
   the name of the function and is only used when finding a function
   body; otherwise it is NULL.  LEN is the size of the data
   returned.  */

const char* 
lto_get_section_data (struct lto_file_decl_data *file_data, 
		      enum lto_section_type section_type,
		      const char *name, 
		      size_t *len)
{
  gcc_assert (get_section_f);
  return (get_section_f) (file_data, section_type, name, len);
}


/* Return the data found from the above call.  The first three
   parameters are the same as above.  DATA is the data to be freed and
   LEN is the length of that data. */

void 
lto_free_section_data (struct lto_file_decl_data *file_data, 
		       enum lto_section_type section_type,
		       const char *name,
		       const char *data,
		       size_t len)
{
  gcc_assert (free_section_f);
  (free_section_f) (file_data, section_type, name, data, len);
}


/* Load a section of type SECTION_TYPE from FILE_DATA, parse the
   header and then return an input block pointing to the section.  The
   raw pointer to the section is returned in DATAR and LEN.  These are
   used to free the section.  */

struct lto_input_block *
lto_create_simple_input_block (struct lto_file_decl_data *file_data, 
			       enum lto_section_type section_type,
			       const char **datar, size_t *len)
{
  const char *data = lto_get_section_data (file_data, section_type, NULL, len);
  const struct lto_simple_header * header 
    = (const struct lto_simple_header *) data;
  struct lto_input_block* ib_main = XNEW (struct lto_input_block);
  int32_t main_offset = sizeof (struct lto_simple_header); 
#ifdef LTO_STREAM_DEBUGGING
  int32_t debug_main_offset = main_offset + header->main_size;
  struct lto_input_block *debug_main = XNEW (struct lto_input_block);
#endif

  *datar = data;
  LTO_INIT_INPUT_BLOCK_PTR (ib_main, data + main_offset,
			    0, header->main_size);
#ifdef LTO_STREAM_DEBUGGING
  lto_debug_context.out = lto_debug_in_fun;
  LTO_INIT_INPUT_BLOCK_PTR (debug_main, data + debug_main_offset,
			    0, header->debug_main_size);
  lto_debug_context.current_data = debug_main;
  lto_debug_context.indent = 0;
#endif
  
  return ib_main;
}


/* Close the section returned from a call to
   LTO_CREATE_SIMPLE_INPUT_BLOCK.  IB is the input block returned from
   that call.  The FILE_DATA and SECTION_TYPE are the same as what was
   passed to that call and the DATA and LEN are what was returned from
   that call.  */

void
lto_destroy_simple_input_block (struct lto_file_decl_data *file_data, 
				enum lto_section_type section_type,
				struct lto_input_block *ib,
				const char *data, size_t len)
{
  free (ib);
#ifdef LTO_STREAM_DEBUGGING
  free (lto_debug_context.current_data);
#endif
  lto_free_section_data (file_data, section_type, NULL, data, len);
}

/*****************************************************************************/
/* Stream debugging support code.                                            */
/*****************************************************************************/

#ifdef LTO_STREAM_DEBUGGING

/* Dump the debug STREAM, and two characters B and C.  */

static void 
dump_debug_stream (struct lto_input_block *stream, 
		   const char *stream_name, char b, char c)
{
  unsigned int i = 0;
  bool new_line = true;
  int chars = 0;
  int hit_pos = -1;
  fprintf (stderr, 
	   "stream failure: looking for a '%c'[0x%x] in the %s debug stream.\nHowever the data translated into a '%c'[0x%x]at position%d\n\n",
	   c, c, stream_name, b, b, stream->p);
  
  while (i < stream->len)
    {
      char x;
      
      if (new_line)
	{
	  if (hit_pos >= 0)
	    {
	      int j;
	      fprintf (stderr, "             ");
	      for (j=0; j<hit_pos; j++)
		fputc (' ', stderr);
	      fprintf (stderr, "^\n             ");
	      for (j=0; j<hit_pos; j++)
		fputc (' ', stderr);
	      fprintf (stderr, "|\n");
	      hit_pos = -1;
	    }
	  
	  fprintf (stderr, "%6d   -->>", i);
	  new_line = false;
	  chars = 0;
	}
      
      x = stream->data[i++];
      if (x == '\n')
	{
	  fprintf (stderr, "<<--\n");
	  new_line = true;
	}
      else 
	fputc (x, stderr);
      
      if (i == stream->p)
	hit_pos = chars;
      chars++;
    }
}


/* The low level output routine to for a single character.  Unlike the
   version on the writing side, this does interesting processing.

   This call checks that the debugging information generated by
   lto-function-out matches the debugging information generated by the
   reader. Each character is checked and a call to abort is generated
   when the first mismatch is found.  
  */

void
lto_debug_in_fun (struct lto_debug_context *context, char c)
{
  struct lto_input_block *stream = (struct lto_input_block *)context->current_data;
  char b = lto_input_1_unsigned (stream);

  if (b != c)
    {
      dump_debug_stream (stream, context->stream_name, b, c);
      gcc_unreachable ();
    }
}
 
#endif
