/* File format for coverage information
   Copyright (C) 1996, 1997, 1998, 2000, 2002,
   2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Bob Manson <manson@cygnus.com>.
   Completely remangled by Nathan Sidwell <nathan@codesourcery.com>.

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
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

/* Coverage information is held in two files.  A notes file, which is
   generated by the compiler, and a data file, which is generated by
   the program under test.  Both files use a similar structure.  We do
   not attempt to make these files backwards compatible with previous
   versions, as you only need coverage information when developing a
   program.  We do hold version information, so that mismatches can be
   detected, and we use a format that allows tools to skip information
   they do not understand or are not interested in.

   Numbers are recorded in the 32 bit unsigned binary form of the
   endianness of the machine generating the file. 64 bit numbers are
   stored as two 32 bit numbers, the low part first.  Strings are
   padded with 1 to 4 NUL bytes, to bring the length up to a multiple
   of 4. The number of 4 bytes is stored, followed by the padded
   string. Zero length and NULL strings are simply stored as a length
   of zero (they have no trailing NUL or padding).

   	int32:  byte3 byte2 byte1 byte0 | byte0 byte1 byte2 byte3
	int64:  int32:low int32:high
	string: int32:0 | int32:length char* char:0 padding
	padding: | char:0 | char:0 char:0 | char:0 char:0 char:0
	item: int32 | int64 | string

   The basic format of the files is

   	file : int32:magic int32:version int32:stamp record*

   The magic ident is different for the notes and the data files.  The
   magic ident is used to determine the endianness of the file, when
   reading.  The version is the same for both files and is derived
   from gcc's version number. The stamp value is used to synchronize
   note and data files and to synchronize merging within a data
   file. It need not be an absolute time stamp, merely a ticker that
   increments fast enough and cycles slow enough to distinguish
   different compile/run/compile cycles.
   
   Although the ident and version are formally 32 bit numbers, they
   are derived from 4 character ASCII strings.  The version number
   consists of the single character major version number, a two
   character minor version number (leading zero for versions less than
   10), and a single character indicating the status of the release.
   That will be 'e' experimental, 'p' prerelease and 'r' for release.
   Because, by good fortune, these are in alphabetical order, string
   collating can be used to compare version strings.  Be aware that
   the 'e' designation will (naturally) be unstable and might be
   incompatible with itself.  For gcc 3.4 experimental, it would be
   '304e' (0x33303465).  When the major version reaches 10, the
   letters A-Z will be used.  Assuming minor increments releases every
   6 months, we have to make a major increment every 50 years.
   Assuming major increments releases every 5 years, we're ok for the
   next 155 years -- good enough for me.

   A record has a tag, length and variable amount of data.

   	record: header data
	header: int32:tag int32:length
	data: item*

   Records are not nested, but there is a record hierarchy.  Tag
   numbers reflect this hierarchy.  Tags are unique across note and
   data files.  Some record types have a varying amount of data.  The
   LENGTH is the number of 4bytes that follow and is usually used to
   determine how much data.  The tag value is split into 4 8-bit
   fields, one for each of four possible levels.  The most significant
   is allocated first.  Unused levels are zero.  Active levels are
   odd-valued, so that the LSB of the level is one.  A sub-level
   incorporates the values of its superlevels.  This formatting allows
   you to determine the tag hierarchy, without understanding the tags
   themselves, and is similar to the standard section numbering used
   in technical documents.  Level values [1..3f] are used for common
   tags, values [41..9f] for the notes file and [a1..ff] for the data
   file.

   The basic block graph file contains the following records
   	note: unit function-graph*
	unit: header int32:checksum string:source
	function-graph: announce_function basic_blocks {arcs | lines}*
	announce_function: header int32:ident int32:checksum
		string:name string:source int32:lineno
	basic_block: header int32:flags*
	arcs: header int32:block_no arc*
	arc:  int32:dest_block int32:flags
        lines: header int32:block_no line*
               int32:0 string:NULL
	line:  int32:line_no | int32:0 string:filename

   The BASIC_BLOCK record holds per-bb flags.  The number of blocks
   can be inferred from its data length.  There is one ARCS record per
   basic block.  The number of arcs from a bb is implicit from the
   data length.  It enumerates the destination bb and per-arc flags.
   There is one LINES record per basic block, it enumerates the source
   lines which belong to that basic block.  Source file names are
   introduced by a line number of 0, following lines are from the new
   source file.  The initial source file for the function is NULL, but
   the current source file should be remembered from one LINES record
   to the next.  The end of a block is indicated by an empty filename
   - this does not reset the current source file.  Note there is no
   ordering of the ARCS and LINES records: they may be in any order,
   interleaved in any manner.  The current filename follows the order
   the LINES records are stored in the file, *not* the ordering of the
   blocks they are for.

   The data file contains the following records.
        data: {unit function-data* summary:object summary:program*}*
	unit: header int32:checksum
        function-data:	announce_function arc_counts
	announce_function: header int32:ident int32:checksum
	arc_counts: header int64:count*
	summary: int32:checksum {count-summary}GCOV_COUNTERS
	count-summary:	int32:num int32:runs int64:sum
			int64:max int64:sum_max

   The ANNOUNCE_FUNCTION record is the same as that in the note file,
   but without the source location.  The ARC_COUNTS gives the counter
   values for those arcs that are instrumented.  The SUMMARY records
   give information about the whole object file and about the whole
   program.  The checksum is used for whole program summaries, and
   disambiguates different programs which include the same
   instrumented object file.  There may be several program summaries,
   each with a unique checksum.  The object summary's checksum is zero.
   Note that the data file might contain information from several runs
   concatenated, or the data might be merged.

   This file is included by both the compiler, gcov tools and the
   runtime support library libgcov. IN_LIBGCOV and IN_GCOV are used to
   distinguish which case is which.  If IN_LIBGCOV is nonzero,
   libgcov is being built. If IN_GCOV is nonzero, the gcov tools are
   being built. Otherwise the compiler is being built. IN_GCOV may be
   positive or negative. If positive, we are compiling a tool that
   requires additional functions (see the code for knowledge of what
   those functions are).  */

#ifndef GCC_GCOV_IO_H
#define GCC_GCOV_IO_H

#if IN_LIBGCOV
/* About the target */

#if BITS_PER_UNIT == 8
typedef unsigned gcov_unsigned_t __attribute__ ((mode (SI)));
typedef unsigned gcov_position_t __attribute__ ((mode (SI)));
#if LONG_LONG_TYPE_SIZE > 32
typedef signed gcov_type __attribute__ ((mode (DI)));
#else
typedef signed gcov_type __attribute__ ((mode (SI)));
#endif
#else
#if BITS_PER_UNIT == 16
typedef unsigned gcov_unsigned_t __attribute__ ((mode (HI)));
typedef unsigned gcov_position_t __attribute__ ((mode (HI)));
#if LONG_LONG_TYPE_SIZE > 32
typedef signed gcov_type __attribute__ ((mode (SI)));
#else
typedef signed gcov_type __attribute__ ((mode (HI)));
#endif
#else
typedef unsigned gcov_unsigned_t __attribute__ ((mode (QI)));
typedef unsigned gcov_position_t __attribute__ ((mode (QI)));
#if LONG_LONG_TYPE_SIZE > 32
typedef signed gcov_type __attribute__ ((mode (HI)));
#else
typedef signed gcov_type __attribute__ ((mode (QI)));
#endif
#endif
#endif


#if defined (TARGET_POSIX_IO)
#define GCOV_LOCKED 1
#else
#define GCOV_LOCKED 0
#endif

#else /* !IN_LIBGCOV */
/* About the host */

typedef unsigned gcov_unsigned_t;
typedef unsigned gcov_position_t;
/* gcov_type is typedef'd elsewhere for the compiler */
#if IN_GCOV
#define GCOV_LINKAGE static
typedef HOST_WIDEST_INT gcov_type;
#if IN_GCOV > 0
#include <sys/types.h>
#endif
#else /*!IN_GCOV */
#define GCOV_TYPE_SIZE (LONG_LONG_TYPE_SIZE > 32 ? 64 : 32)
#endif

#if defined (HOST_HAS_F_SETLKW)
#define GCOV_LOCKED 1
#else
#define GCOV_LOCKED 0
#endif

#endif /* !IN_LIBGCOV */

/* In gcov we want function linkage to be static.  In the compiler we want
   it extern, so that they can be accessed from elsewhere.  In libgcov we
   need these functions to be extern, so prefix them with __gcov.  In
   libgcov they must also be hidden so that the instance in the executable
   is not also used in a DSO.  */
#if IN_LIBGCOV

#include "tconfig.h"

#define gcov_var __gcov_var
#define gcov_open __gcov_open
#define gcov_close __gcov_close
#define gcov_write_tag_length __gcov_write_tag_length
#define gcov_position __gcov_position
#define gcov_seek __gcov_seek
#define gcov_rewrite __gcov_rewrite
#define gcov_is_error __gcov_is_error
#define gcov_write_unsigned __gcov_write_unsigned
#define gcov_write_counter __gcov_write_counter
#define gcov_write_summary __gcov_write_summary
#define gcov_read_unsigned __gcov_read_unsigned
#define gcov_read_counter __gcov_read_counter
#define gcov_read_summary __gcov_read_summary

/* Poison these, so they don't accidentally slip in.  */
#pragma GCC poison gcov_write_string gcov_write_tag gcov_write_length
#pragma GCC poison gcov_read_string gcov_sync gcov_time gcov_magic

#ifdef HAVE_GAS_HIDDEN
#define ATTRIBUTE_HIDDEN  __attribute__ ((__visibility__ ("hidden")))
#else
#define ATTRIBUTE_HIDDEN
#endif

#else

#define ATTRIBUTE_HIDDEN

#endif

#ifndef GCOV_LINKAGE
#define GCOV_LINKAGE extern
#endif

/* File suffixes.  */
#define GCOV_DATA_SUFFIX ".gcda"
#define GCOV_NOTE_SUFFIX ".gcno"

/* File magic. Must not be palindromes.  */
#define GCOV_DATA_MAGIC ((gcov_unsigned_t)0x67636461) /* "gcda" */
#define GCOV_NOTE_MAGIC ((gcov_unsigned_t)0x67636e6f) /* "gcno" */

/* gcov-iov.h is automatically generated by the makefile from
   version.c, it looks like
   	#define GCOV_VERSION ((gcov_unsigned_t)0x89abcdef)
*/
#include "gcov-iov.h"

/* Convert a magic or version number to a 4 character string.  */
#define GCOV_UNSIGNED2STRING(ARRAY,VALUE)	\
  ((ARRAY)[0] = (char)((VALUE) >> 24),		\
   (ARRAY)[1] = (char)((VALUE) >> 16),		\
   (ARRAY)[2] = (char)((VALUE) >> 8),		\
   (ARRAY)[3] = (char)((VALUE) >> 0))

/* The record tags.  Values [1..3f] are for tags which may be in either
   file.  Values [41..9f] for those in the note file and [a1..ff] for
   the data file.  The tag value zero is used as an explicit end of
   file marker -- it is not required to be present.  */

#define GCOV_TAG_FUNCTION	 ((gcov_unsigned_t)0x01000000)
#define GCOV_TAG_FUNCTION_LENGTH (2)
#define GCOV_TAG_BLOCKS		 ((gcov_unsigned_t)0x01410000)
#define GCOV_TAG_BLOCKS_LENGTH(NUM) (NUM)
#define GCOV_TAG_BLOCKS_NUM(LENGTH) (LENGTH)
#define GCOV_TAG_ARCS		 ((gcov_unsigned_t)0x01430000)
#define GCOV_TAG_ARCS_LENGTH(NUM)  (1 + (NUM) * 2)
#define GCOV_TAG_ARCS_NUM(LENGTH)  (((LENGTH) - 1) / 2)
#define GCOV_TAG_LINES		 ((gcov_unsigned_t)0x01450000)
#define GCOV_TAG_COUNTER_BASE 	 ((gcov_unsigned_t)0x01a10000)
#define GCOV_TAG_COUNTER_LENGTH(NUM) ((NUM) * 2)
#define GCOV_TAG_COUNTER_NUM(LENGTH) ((LENGTH) / 2)
#define GCOV_TAG_OBJECT_SUMMARY  ((gcov_unsigned_t)0xa1000000)
#define GCOV_TAG_PROGRAM_SUMMARY ((gcov_unsigned_t)0xa3000000)
#define GCOV_TAG_SUMMARY_LENGTH  \
	(1 + GCOV_COUNTERS_SUMMABLE * (2 + 3 * 2))

/* Counters that are collected.  */
#define GCOV_COUNTER_ARCS 	0  /* Arc transitions.  */
#define GCOV_COUNTERS_SUMMABLE	1  /* Counters which can be
				      summaried.  */
#define GCOV_FIRST_VALUE_COUNTER 1 /* The first of counters used for value
				      profiling.  They must form a consecutive
				      interval and their order must match
				      the order of HIST_TYPEs in
				      value-prof.h.  */
#define GCOV_COUNTER_V_INTERVAL	1  /* Histogram of value inside an interval.  */
#define GCOV_COUNTER_V_POW2	2  /* Histogram of exact power2 logarithm
				      of a value.  */
#define GCOV_COUNTER_V_SINGLE	3  /* The most common value of expression.  */
#define GCOV_COUNTER_V_DELTA	4  /* The most common difference between
				      consecutive values of expression.  */

#define GCOV_COUNTER_V_INDIR	5  /* The most common indirect address */
#define GCOV_COUNTER_AVERAGE	6  /* The most common difference between
				      consecutive values of expression.  */
#define GCOV_COUNTER_IOR	7  /* The most common difference between
				      consecutive values of expression.  */
#define GCOV_LAST_VALUE_COUNTER 7  /* The last of counters used for value
				      profiling.  */
#define GCOV_COUNTERS		8

/* Number of counters used for value profiling.  */
#define GCOV_N_VALUE_COUNTERS \
  (GCOV_LAST_VALUE_COUNTER - GCOV_FIRST_VALUE_COUNTER + 1)
  
  /* A list of human readable names of the counters */
#define GCOV_COUNTER_NAMES	{"arcs", "interval", "pow2", "single", \
				 "delta","indirect_call", "average", "ior"}
  
  /* Names of merge functions for counters.  */
#define GCOV_MERGE_FUNCTIONS	{"__gcov_merge_add",	\
				 "__gcov_merge_add",	\
				 "__gcov_merge_add",	\
				 "__gcov_merge_single",	\
				 "__gcov_merge_delta",  \
				 "__gcov_merge_single", \
				 "__gcov_merge_add",	\
				 "__gcov_merge_ior"}
  
/* Convert a counter index to a tag.  */
#define GCOV_TAG_FOR_COUNTER(COUNT)				\
	(GCOV_TAG_COUNTER_BASE + ((gcov_unsigned_t)(COUNT) << 17))
/* Convert a tag to a counter.  */
#define GCOV_COUNTER_FOR_TAG(TAG)					\
	((unsigned)(((TAG) - GCOV_TAG_COUNTER_BASE) >> 17))
/* Check whether a tag is a counter tag.  */
#define GCOV_TAG_IS_COUNTER(TAG)				\
	(!((TAG) & 0xFFFF) && GCOV_COUNTER_FOR_TAG (TAG) < GCOV_COUNTERS)

/* The tag level mask has 1's in the position of the inner levels, &
   the lsb of the current level, and zero on the current and outer
   levels.  */
#define GCOV_TAG_MASK(TAG) (((TAG) - 1) ^ (TAG))

/* Return nonzero if SUB is an immediate subtag of TAG.  */
#define GCOV_TAG_IS_SUBTAG(TAG,SUB)				\
	(GCOV_TAG_MASK (TAG) >> 8 == GCOV_TAG_MASK (SUB) 	\
	 && !(((SUB) ^ (TAG)) & ~GCOV_TAG_MASK(TAG)))

/* Return nonzero if SUB is at a sublevel to TAG.  */
#define GCOV_TAG_IS_SUBLEVEL(TAG,SUB)				\
     	(GCOV_TAG_MASK (TAG) > GCOV_TAG_MASK (SUB))

/* Basic block flags.  */
#define GCOV_BLOCK_UNEXPECTED	(1 << 1)

/* Arc flags.  */
#define GCOV_ARC_ON_TREE 	(1 << 0)
#define GCOV_ARC_FAKE		(1 << 1)
#define GCOV_ARC_FALLTHROUGH	(1 << 2)

/* Structured records.  */

/* Cumulative counter data.  */
struct gcov_ctr_summary
{
  gcov_unsigned_t num;		/* number of counters.  */
  gcov_unsigned_t runs;		/* number of program runs */
  gcov_type sum_all;		/* sum of all counters accumulated.  */
  gcov_type run_max;		/* maximum value on a single run.  */
  gcov_type sum_max;    	/* sum of individual run max values.  */
};

/* Object & program summary record.  */
struct gcov_summary
{
  gcov_unsigned_t checksum;	/* checksum of program */
  struct gcov_ctr_summary ctrs[GCOV_COUNTERS_SUMMABLE];
};

/* Structures embedded in coveraged program.  The structures generated
   by write_profile must match these.  */

#if IN_LIBGCOV
/* Information about a single function.  This uses the trailing array
   idiom. The number of counters is determined from the counter_mask
   in gcov_info.  We hold an array of function info, so have to
   explicitly calculate the correct array stride.  */
struct gcov_fn_info
{
  gcov_unsigned_t ident;	/* unique ident of function */
  gcov_unsigned_t checksum;	/* function checksum */
  unsigned n_ctrs[0];		/* instrumented counters */
};

/* Type of function used to merge counters.  */
typedef void (*gcov_merge_fn) (gcov_type *, gcov_unsigned_t);

/* Information about counters.  */
struct gcov_ctr_info
{
  gcov_unsigned_t num;		/* number of counters.  */
  gcov_type *values;		/* their values.  */
  gcov_merge_fn merge;  	/* The function used to merge them.  */
};

/* Information about a single object file.  */
struct gcov_info
{
  gcov_unsigned_t version;	/* expected version number */
  struct gcov_info *next;	/* link to next, used by libgcov */

  gcov_unsigned_t stamp;	/* uniquifying time stamp */
  const char *filename;		/* output file name */
  
  unsigned n_functions;		/* number of functions */
  const struct gcov_fn_info *functions; /* table of functions */

  unsigned ctr_mask;		/* mask of counters instrumented.  */
  struct gcov_ctr_info counts[0]; /* count data. The number of bits
				     set in the ctr_mask field
				     determines how big this array
				     is.  */
};

/* Register a new object file module.  */
extern void __gcov_init (struct gcov_info *) ATTRIBUTE_HIDDEN;

/* Called before fork, to avoid double counting.  */
extern void __gcov_flush (void) ATTRIBUTE_HIDDEN;

/* The merge function that just sums the counters.  */
extern void __gcov_merge_add (gcov_type *, unsigned) ATTRIBUTE_HIDDEN;

/* The merge function to choose the most common value.  */
extern void __gcov_merge_single (gcov_type *, unsigned) ATTRIBUTE_HIDDEN;

/* The merge function to choose the most common difference between
   consecutive values.  */
extern void __gcov_merge_delta (gcov_type *, unsigned) ATTRIBUTE_HIDDEN;

/* The profiler functions.  */
extern void __gcov_interval_profiler (gcov_type *, gcov_type, int, unsigned); 
extern void __gcov_pow2_profiler (gcov_type *, gcov_type);
extern void __gcov_one_value_profiler (gcov_type *, gcov_type);
extern void __gcov_indirect_call_profiler (gcov_type *, gcov_type, void *, void *);
extern void __gcov_average_profiler (gcov_type *, gcov_type);
extern void __gcov_ior_profiler (gcov_type *, gcov_type);
extern void __gcov_merge_ior (gcov_type *, unsigned);

#ifndef inhibit_libc
/* The wrappers around some library functions..  */
extern pid_t __gcov_fork (void);
extern int __gcov_execl (const char *, const char *, ...) ATTRIBUTE_HIDDEN;
extern int __gcov_execlp (const char *, const char *, ...) ATTRIBUTE_HIDDEN;
extern int __gcov_execle (const char *,  const char *, ...) ATTRIBUTE_HIDDEN;
extern int __gcov_execv (const char *, char *const []) ATTRIBUTE_HIDDEN;
extern int __gcov_execvp (const char *, char *const []) ATTRIBUTE_HIDDEN;
extern int __gcov_execve (const char *, char  *const [], char *const [])
  ATTRIBUTE_HIDDEN;
#endif

#endif /* IN_LIBGCOV */

#if IN_LIBGCOV >= 0

/* Optimum number of gcov_unsigned_t's read from or written to disk.  */
#define GCOV_BLOCK_SIZE (1 << 10)

GCOV_LINKAGE struct gcov_var
{
  FILE *file;
  gcov_position_t start;	/* Position of first byte of block */
  unsigned offset;		/* Read/write position within the block.  */
  unsigned length;		/* Read limit in the block.  */
  unsigned overread;		/* Number of words overread.  */
  int error;			/* < 0 overflow, > 0 disk error.  */
  int mode;	                /* < 0 writing, > 0 reading */
#if IN_LIBGCOV
  /* Holds one block plus 4 bytes, thus all coverage reads & writes
     fit within this buffer and we always can transfer GCOV_BLOCK_SIZE
     to and from the disk. libgcov never backtracks and only writes 4
     or 8 byte objects.  */
  gcov_unsigned_t buffer[GCOV_BLOCK_SIZE + 1];
#else
  int endian;			/* Swap endianness.  */
  /* Holds a variable length block, as the compiler can write
     strings and needs to backtrack.  */
  size_t alloc;
  gcov_unsigned_t *buffer;
#endif
} gcov_var ATTRIBUTE_HIDDEN;

/* Functions for reading and writing gcov files. In libgcov you can
   open the file for reading then writing. Elsewhere you can open the
   file either for reading or for writing. When reading a file you may
   use the gcov_read_* functions, gcov_sync, gcov_position, &
   gcov_error. When writing a file you may use the gcov_write
   functions, gcov_seek & gcov_error. When a file is to be rewritten
   you use the functions for reading, then gcov_rewrite then the
   functions for writing.  Your file may become corrupted if you break
   these invariants.  */
#if IN_LIBGCOV
GCOV_LINKAGE int gcov_open (const char */*name*/) ATTRIBUTE_HIDDEN;
#else
GCOV_LINKAGE int gcov_open (const char */*name*/, int /*direction*/);
GCOV_LINKAGE int gcov_magic (gcov_unsigned_t, gcov_unsigned_t);
#endif
GCOV_LINKAGE int gcov_close (void) ATTRIBUTE_HIDDEN;

/* Available everywhere.  */
static gcov_position_t gcov_position (void);
static int gcov_is_error (void);

GCOV_LINKAGE gcov_unsigned_t gcov_read_unsigned (void) ATTRIBUTE_HIDDEN;
GCOV_LINKAGE gcov_type gcov_read_counter (void) ATTRIBUTE_HIDDEN;
GCOV_LINKAGE void gcov_read_summary (struct gcov_summary *) ATTRIBUTE_HIDDEN;

#if IN_LIBGCOV
/* Available only in libgcov */
GCOV_LINKAGE void gcov_write_counter (gcov_type) ATTRIBUTE_HIDDEN;
GCOV_LINKAGE void gcov_write_tag_length (gcov_unsigned_t, gcov_unsigned_t)
    ATTRIBUTE_HIDDEN;
GCOV_LINKAGE void gcov_write_summary (gcov_unsigned_t /*tag*/,
				      const struct gcov_summary *)
    ATTRIBUTE_HIDDEN;
static void gcov_rewrite (void);
GCOV_LINKAGE void gcov_seek (gcov_position_t /*position*/) ATTRIBUTE_HIDDEN;
#else
/* Available outside libgcov */
GCOV_LINKAGE const char *gcov_read_string (void);
GCOV_LINKAGE void gcov_sync (gcov_position_t /*base*/,
			     gcov_unsigned_t /*length */);
#endif

#if !IN_GCOV
/* Available outside gcov */
GCOV_LINKAGE void gcov_write_unsigned (gcov_unsigned_t) ATTRIBUTE_HIDDEN;
#endif

#if !IN_GCOV && !IN_LIBGCOV
/* Available only in compiler */
GCOV_LINKAGE void gcov_write_string (const char *);
GCOV_LINKAGE gcov_position_t gcov_write_tag (gcov_unsigned_t);
GCOV_LINKAGE void gcov_write_length (gcov_position_t /*position*/);
#endif

#if IN_GCOV > 0
/* Available in gcov */
GCOV_LINKAGE time_t gcov_time (void);
#endif

/* Save the current position in the gcov file.  */

static inline gcov_position_t
gcov_position (void)
{
  gcc_assert (gcov_var.mode > 0);
  return gcov_var.start + gcov_var.offset;
}

/* Return nonzero if the error flag is set.  */

static inline int
gcov_is_error (void)
{
  return gcov_var.file ? gcov_var.error : 1;
}

#if IN_LIBGCOV
/* Move to beginning of file and initialize for writing.  */

static inline void
gcov_rewrite (void)
{
  gcc_assert (gcov_var.mode > 0);
  gcov_var.mode = -1;
  gcov_var.start = 0;
  gcov_var.offset = 0;
  fseek (gcov_var.file, 0L, SEEK_SET);
}
#endif

#endif /* IN_LIBGCOV >= 0 */

#endif /* GCC_GCOV_IO_H */
