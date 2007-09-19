/* Copyright (C) 2002,2003,2005 Free Software Foundation, Inc.
   Contributed by Andy Vaught

This file is part of the GNU Fortran 95 runtime library (libgfortran).

Libgfortran is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

Libgfortran is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with libgfortran; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "libgfortran.h"

/* Environment scanner.  Examine the environment for controlling minor
 * aspects of the program's execution.  Our philosophy here that the
 * environment should not prevent the program from running, so an
 * environment variable with a messed-up value will be interpreted in
 * the default way.
 *
 * Most of the environment is checked early in the startup sequence,
 * but other variables are checked during execution of the user's
 * program. */

options_t options;


typedef struct variable
{
  const char *name;
  int value, *var;
  void (*init) (struct variable *);
  void (*show) (struct variable *);
  const char *desc;
  int bad;
}
variable;

static void init_unformatted (variable *);

/* print_spaces()-- Print a particular number of spaces.  */

static void
print_spaces (int n)
{
  char buffer[80];
  int i;

  if (n <= 0)
    return;

  for (i = 0; i < n; i++)
    buffer[i] = ' ';

  buffer[i] = '\0';

  st_printf (buffer);
}


/* var_source()-- Return a string that describes where the value of a
 * variable comes from */

static const char *
var_source (variable * v)
{
  if (getenv (v->name) == NULL)
    return "Default";

  if (v->bad)
    return "Bad    ";

  return "Set    ";
}


/* init_integer()-- Initialize an integer environment variable.  */

static void
init_integer (variable * v)
{
  char *p, *q;

  p = getenv (v->name);
  if (p == NULL)
    goto set_default;

  for (q = p; *q; q++)
    if (!isdigit (*q) && (p != q || *q != '-'))
      {
	v->bad = 1;
	goto set_default;
      }

  *v->var = atoi (p);
  return;

 set_default:
  *v->var = v->value;
  return;
}


/* init_unsigned_integer()-- Initialize an integer environment variable
   which has to be positive.  */

static void
init_unsigned_integer (variable * v)
{
  char *p, *q;

  p = getenv (v->name);
  if (p == NULL)
    goto set_default;

  for (q = p; *q; q++)
    if (!isdigit (*q))
      {
	v->bad = 1;
	goto set_default;
      }

  *v->var = atoi (p);
  return;

 set_default:
  *v->var = v->value;
  return;
}


/* show_integer()-- Show an integer environment variable */

static void
show_integer (variable * v)
{
  st_printf ("%s  %d\n", var_source (v), *v->var);
}


/* init_boolean()-- Initialize a boolean environment variable.  We
 * only look at the first letter of the variable. */

static void
init_boolean (variable * v)
{
  char *p;

  p = getenv (v->name);
  if (p == NULL)
    goto set_default;

  if (*p == '1' || *p == 'Y' || *p == 'y')
    {
      *v->var = 1;
      return;
    }

  if (*p == '0' || *p == 'N' || *p == 'n')
    {
      *v->var = 0;
      return;
    }

  v->bad = 1;

set_default:
  *v->var = v->value;
  return;
}


/* show_boolean()-- Show a boolean environment variable */

static void
show_boolean (variable * v)
{
  st_printf ("%s  %s\n", var_source (v), *v->var ? "Yes" : "No");
}


/* init_mem()-- Initialize environment variables that have to do with
 * how memory from an ALLOCATE statement is filled.  A single flag
 * enables filling and a second variable gives the value that is used
 * to initialize the memory. */

static void
init_mem (variable * v)
{
  int offset, n;
  char *p;

  p = getenv (v->name);

  options.allocate_init_flag = 0;	/* The default */

  if (p == NULL)
    return;

  if (strcasecmp (p, "NONE") == 0)
    return;

  /* IEEE-754 Quiet Not-a-Number that will work for single and double
   * precision.  Look for the 'f95' mantissa in debug dumps. */

  if (strcasecmp (p, "NaN") == 0)
    {
      options.allocate_init_flag = 1;
      options.allocate_init_value = 0xfff80f95;
      return;
    }

  /* Interpret the string as a hexadecimal constant */

  n = 0;
  while (*p)
    {
      if (!isxdigit (*p))
	{
	  v->bad = 1;
	  return;
	}

      offset = '0';
      if (islower (*p))
	offset = 'a';
      if (isupper (*p))
	offset = 'A';

      n = (n << 4) | (*p++ - offset);
    }

  options.allocate_init_flag = 1;
  options.allocate_init_value = n;
}


static void
show_mem (variable * v)
{
  char *p;

  p = getenv (v->name);

  st_printf ("%s  ", var_source (v));

  if (options.allocate_init_flag)
    st_printf ("0x%x", options.allocate_init_value);

  st_printf ("\n");
}


static void
init_sep (variable * v)
{
  int seen_comma;
  char *p;

  p = getenv (v->name);
  if (p == NULL)
    goto set_default;

  v->bad = 1;
  options.separator = p;
  options.separator_len = strlen (p);

  /* Make sure the separator is valid */

  if (options.separator_len == 0)
    goto set_default;
  seen_comma = 0;

  while (*p)
    {
      if (*p == ',')
	{
	  if (seen_comma)
	    goto set_default;
	  seen_comma = 1;
	  p++;
	  continue;
	}

      if (*p++ != ' ')
	goto set_default;
    }

  v->bad = 0;
  return;

set_default:
  options.separator = " ";
  options.separator_len = 1;
}


static void
show_sep (variable * v)
{
  st_printf ("%s  \"%s\"\n", var_source (v), options.separator);
}


static void
init_string (variable * v __attribute__ ((unused)))
{
}

static void
show_string (variable * v)
{
  const char *p;

  p = getenv (v->name);
  if (p == NULL)
    p = "";

  st_printf ("%s  \"%s\"\n", var_source (v), p);
}


/* Structure for associating names and values.  */

typedef struct
{
  const char *name;
  int value;
}
choice;


enum
{ FP_ROUND_NEAREST, FP_ROUND_UP, FP_ROUND_DOWN, FP_ROUND_ZERO };

static const choice rounding[] = {
  {"NEAREST", FP_ROUND_NEAREST},
  {"UP", FP_ROUND_UP},
  {"DOWN", FP_ROUND_DOWN},
  {"ZERO", FP_ROUND_ZERO},
  {NULL, 0}
};

static const choice precision[] =
{
  { "24", 1},
  { "53", 2},
  { "64", 0},
  { NULL, 0}
};

static const choice signal_choices[] =
{
  { "IGNORE", 1},
  { "ABORT", 0},
  { NULL, 0}
};


static void
init_choice (variable * v, const choice * c)
{
  char *p;

  p = getenv (v->name);
  if (p == NULL)
    goto set_default;

  for (; c->name; c++)
    if (strcasecmp (c->name, p) == 0)
      break;

  if (c->name == NULL)
    {
      v->bad = 1;
      goto set_default;
    }

  *v->var = c->value;
  return;

 set_default:
  *v->var = v->value;
}


static void
show_choice (variable * v, const choice * c)
{
  st_printf ("%s  ", var_source (v));

  for (; c->name; c++)
    if (c->value == *v->var)
      break;

  if (c->name)
    st_printf ("%s\n", c->name);
  else
    st_printf ("(Unknown)\n");
}


static void
init_round (variable * v)
{
  init_choice (v, rounding);
}

static void
show_round (variable * v)
{
  show_choice (v, rounding);
}

static void
init_precision (variable * v)
{
  init_choice (v, precision);
}

static void
show_precision (variable * v)
{
  show_choice (v, precision);
}

static void
init_signal (variable * v)
{
  init_choice (v, signal_choices);
}

static void
show_signal (variable * v)
{
  show_choice (v, signal_choices);
}


static variable variable_table[] = {
  {"GFORTRAN_STDIN_UNIT", 5, &options.stdin_unit, init_integer, show_integer,
   "Unit number that will be preconnected to standard input\n"
   "(No preconnection if negative)", 0},

  {"GFORTRAN_STDOUT_UNIT", 6, &options.stdout_unit, init_integer,
   show_integer,
   "Unit number that will be preconnected to standard output\n"
   "(No preconnection if negative)", 0},

  {"GFORTRAN_STDERR_UNIT", 0, &options.stderr_unit, init_integer,
   show_integer,
   "Unit number that will be preconnected to standard error\n"
   "(No preconnection if negative)", 0},

  {"GFORTRAN_USE_STDERR", 1, &options.use_stderr, init_boolean,
   show_boolean,
   "Sends library output to standard error instead of standard output.", 0},

  {"GFORTRAN_TMPDIR", 0, NULL, init_string, show_string,
   "Directory for scratch files.  Overrides the TMP environment variable\n"
   "If TMP is not set " DEFAULT_TEMPDIR " is used.", 0},

  {"GFORTRAN_UNBUFFERED_ALL", 0, &options.all_unbuffered, init_boolean,
   show_boolean,
   "If TRUE, all output is unbuffered.  This will slow down large writes "
   "but can be\nuseful for forcing data to be displayed immediately.", 0},

  {"GFORTRAN_SHOW_LOCUS", 1, &options.locus, init_boolean, show_boolean,
   "If TRUE, print filename and line number where runtime errors happen.", 0},

  {"GFORTRAN_OPTIONAL_PLUS", 0, &options.optional_plus, init_boolean, show_boolean,
   "Print optional plus signs in numbers where permitted.  Default FALSE.", 0},

  {"GFORTRAN_DEFAULT_RECL", DEFAULT_RECL, &options.default_recl,
   init_unsigned_integer, show_integer,
   "Default maximum record length for sequential files.  Most useful for\n"
   "adjusting line length of preconnected units.  Default "
   stringize (DEFAULT_RECL), 0},

  {"GFORTRAN_LIST_SEPARATOR", 0, NULL, init_sep, show_sep,
   "Separator to use when writing list output.  May contain any number of "
   "spaces\nand at most one comma.  Default is a single space.", 0},

  /* Memory related controls */

  {"GFORTRAN_MEM_INIT", 0, NULL, init_mem, show_mem,
   "How to initialize allocated memory.  Default value is NONE for no "
   "initialization\n(faster), NAN for a Not-a-Number with the mantissa "
   "0x40f95 or a custom\nhexadecimal value", 0},

  {"GFORTRAN_MEM_CHECK", 0, &options.mem_check, init_boolean, show_boolean,
   "Whether memory still allocated will be reported when the program ends.",
   0},

  /* Signal handling (Unix).  */

  {"GFORTRAN_SIGHUP", 0, &options.sighup, init_signal, show_signal,
   "Whether the program will IGNORE or ABORT on SIGHUP.", 0},

  {"GFORTRAN_SIGINT", 0, &options.sigint, init_signal, show_signal,
   "Whether the program will IGNORE or ABORT on SIGINT.", 0},

  /* Floating point control */

  {"GFORTRAN_FPU_ROUND", 0, &options.fpu_round, init_round, show_round,
   "Set floating point rounding.  Values are NEAREST, UP, DOWN, ZERO.", 0},

  {"GFORTRAN_FPU_PRECISION", 0, &options.fpu_precision, init_precision,
   show_precision,
   "Precision of intermediate results.  Values are 24, 53 and 64.", 0},

  /* GFORTRAN_CONVERT_UNIT - Set the default data conversion for
   unformatted I/O.  */
  {"GFORTRAN_CONVERT_UNIT", 0, 0, init_unformatted, show_string,
   "Set format for unformatted files", 0},

  /* Behaviour when encoutering a runtime error.  */
  {"GFORTRAN_ERROR_DUMPCORE", -1, &options.dump_core,
    init_boolean, show_boolean,
    "Dump a core file (if possible) on runtime error", -1},

  {"GFORTRAN_ERROR_BACKTRACE", -1, &options.backtrace,
    init_boolean, show_boolean,
    "Print out a backtrace (if possible) on runtime error", -1},

  {NULL, 0, NULL, NULL, NULL, NULL, 0}
};


/* init_variables()-- Initialize most runtime variables from
 * environment variables. */

void
init_variables (void)
{
  variable *v;

  for (v = variable_table; v->name; v++)
    v->init (v);
}


/* check_buffered()-- Given an unit number n, determine if an override
 * for the stream exists.  Returns zero for unbuffered, one for
 * buffered or two for not set. */

int
check_buffered (int n)
{
  char name[22 + sizeof (n) * 3];
  variable v;
  int rv;

  if (options.all_unbuffered)
    return 0;

  sprintf (name, "GFORTRAN_UNBUFFERED_%d", n);

  v.name = name;
  v.value = 2;
  v.var = &rv;

  init_boolean (&v);

  return rv;
}


void
show_variables (void)
{
  variable *v;
  int n;

  /* TODO: print version number.  */
  st_printf ("GNU Fortran 95 runtime library version "
	     "UNKNOWN" "\n\n");

  st_printf ("Environment variables:\n");
  st_printf ("----------------------\n");

  for (v = variable_table; v->name; v++)
    {
      n = st_printf ("%s", v->name);
      print_spaces (25 - n);

      if (v->show == show_integer)
	st_printf ("Integer ");
      else if (v->show == show_boolean)
	st_printf ("Boolean ");
      else
	st_printf ("String  ");

      v->show (v);
      st_printf ("%s\n\n", v->desc);
    }

  /* System error codes */

  st_printf ("\nRuntime error codes:");
  st_printf ("\n--------------------\n");

  for (n = ERROR_FIRST + 1; n < ERROR_LAST; n++)
    if (n < 0 || n > 9)
      st_printf ("%d  %s\n", n, translate_error (n));
    else
      st_printf (" %d  %s\n", n, translate_error (n));

  st_printf ("\nCommand line arguments:\n");
  st_printf ("  --help               Print this list\n");

  /* st_printf("  --resume <dropfile>  Resume program execution from dropfile\n"); */

  sys_exit (0);
}

/* This is the handling of the GFORTRAN_CONVERT_UNITS environment variable.
   It is called from environ.c to parse this variable, and from
   open.c to determine if the user specified a default for an
   unformatted file.
   The syntax of the environment variable is, in bison grammar:

   GFORTRAN_CONVERT_UNITS: mode | mode ';' exception ;
   mode: 'native' | 'swap' | 'big_endian' | 'little_endian' ;
   exception: mode ':' unit_list | unit_list ;
   unit_list: unit_spec | unit_list unit_spec ;
   unit_spec: INTEGER | INTEGER '-' INTEGER ;
*/

/* Defines for the tokens.  Other valid tokens are ',', ':', '-'.  */


#define NATIVE   257
#define SWAP     258
#define BIG      259
#define LITTLE   260
/* Some space for additional tokens later.  */
#define INTEGER  273
#define END      (-1)
#define ILLEGAL  (-2)

typedef struct
{
  int unit;
  unit_convert conv;
} exception_t;


static char *p;            /* Main character pointer for parsing.  */
static char *lastpos;      /* Auxiliary pointer, for backing up.  */
static int unit_num;       /* The last unit number read.  */
static int unit_count;     /* The number of units found. */
static int do_count;       /* Parsing is done twice - first to count the number
			      of units, then to fill in the table.  This
			      variable controls what to do.  */
static exception_t *elist; /* The list of exceptions to the default. This is
			      sorted according to unit number.  */
static int n_elist;        /* Number of exceptions to the default.  */

static unit_convert endian; /* Current endianness.  */

static unit_convert def; /* Default as specified (if any).  */

/* Search for a unit number, using a binary search.  The
   first argument is the unit number to search for.  The second argument
   is a pointer to an index.
   If the unit number is found, the function returns 1, and the index
   is that of the element.
   If the unit number is not found, the function returns 0, and the
   index is the one where the element would be inserted.  */

static int
search_unit (int unit, int *ip)
{
  int low, high, mid;

  low = -1;
  high = n_elist;
  while (high - low > 1)
    {
      mid = (low + high) / 2;
      if (unit <= elist[mid].unit)
	high = mid;
      else
	low = mid;
    }
  *ip = high;
  if (elist[high].unit == unit)
    return 1;
  else
    return 0;
}

/* This matches a keyword.  If it is found, return the token supplied,
   otherwise return ILLEGAL.  */

static int
match_word (const char *word, int tok)
{
  int res;

  if (strncasecmp (p, word, strlen (word)) == 0)
    {
      p += strlen (word);
      res = tok;
    }
  else
    res = ILLEGAL;
  return res;

}

/* Match an integer and store its value in unit_num.  This only works
   if p actually points to the start of an integer.  The caller has
   to ensure this.  */

static int
match_integer (void)
{
  unit_num = 0;
  while (isdigit (*p))
    unit_num = unit_num * 10 + (*p++ - '0');
  return INTEGER;

}

/* This reads the next token from the GFORTRAN_CONVERT_UNITS variable.
   Returned values are the different tokens.  */

static int
next_token (void)
{
  int result;

  lastpos = p;
  switch (*p)
    {
    case '\0':
      result = END;
      break;
      
    case ':':
    case ',': 
    case '-':
    case ';':
      result = *p;
      p++;
      break;

    case 'b':
    case 'B':
      result = match_word ("big_endian", BIG);
      break;

    case 'l':
    case 'L':
      result = match_word ("little_endian", LITTLE);
      break;

    case 'n':
    case 'N':
      result = match_word ("native", NATIVE);
      break;

    case 's':
    case 'S':
      result = match_word ("swap", SWAP);
      break;

    case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9':
      result = match_integer ();
      break;

    default:
      result = ILLEGAL;
      break;
    }
  return result;
}

/* Back up the last token by setting back the character pointer.  */

static void
push_token (void)
{
  p = lastpos;
}

/* This is called when a unit is identified.  If do_count is nonzero,
   increment the number of units by one.  If do_count is zero,
   put the unit into the table.  */

static void
mark_single (int unit)
{
  int i,j;

  if (do_count)
    {
      unit_count++;
      return;
    }
  if (search_unit (unit, &i))
    {
      elist[unit].conv = endian;
    }
  else
    {
      for (j=n_elist; j>=i; j--)
	elist[j+1] = elist[j];
    
      n_elist += 1;
      elist[i].unit = unit;
      elist[i].conv = endian;
    }
}

/* This is called when a unit range is identified.  If do_count is
   nonzero, increase the number of units.  If do_count is zero,
   put the unit into the table.  */

static void
mark_range (int unit1, int unit2)
{
  int i;
  if (do_count)
    unit_count += abs (unit2 - unit1) + 1;
  else
    {
      if (unit2 < unit1)
	for (i=unit2; i<=unit1; i++)
	  mark_single (i);
      else
	for (i=unit1; i<=unit2; i++)
	  mark_single (i);
    }
}

/* Parse the GFORTRAN_CONVERT_UNITS variable.  This is called
   twice, once to count the units and once to actually mark them in
   the table.  When counting, we don't check for double occurrences
   of units.  */

static int
do_parse (void)
{
  int tok;
  int unit1;
  int continue_ulist;
  char *start;

  unit_count = 0;

  start = p;

  /* Parse the string.  First, let's look for a default.  */
  tok = next_token ();
  switch (tok)
    {
    case NATIVE:
      endian = CONVERT_NATIVE;
      break;

    case SWAP:
      endian = CONVERT_SWAP;
      break;

    case BIG:
      endian = CONVERT_BIG;
      break;

    case LITTLE:
      endian = CONVERT_LITTLE;
      break;

    case INTEGER:
      /* A leading digit means that we are looking at an exception.
	 Reset the position to the beginning, and continue processing
	 at the exception list.  */
      p = start;
      goto exceptions;
      break;

    case END:
      goto end;
      break;

    default:
      goto error;
      break;
    }

  tok = next_token ();
  switch (tok)
    {
    case ';':
      def = endian;
      break;

    case ':':
      /* This isn't a default after all.  Reset the position to the
	 beginning, and continue processing at the exception list.  */
      p = start;
      goto exceptions;
      break;

    case END:
      def = endian;
      goto end;
      break;

    default:
      goto error;
      break;
    }

 exceptions:

  /* Loop over all exceptions.  */
  while(1)
    {
      tok = next_token ();
      switch (tok)
	{
	case NATIVE:
	  if (next_token () != ':')
	    goto error;
	  endian = CONVERT_NATIVE;
	  break;

	case SWAP:
	  if (next_token () != ':')
	    goto error;
	  endian = CONVERT_SWAP;
	  break;

	case LITTLE:
	  if (next_token () != ':')
	    goto error;
	  endian = CONVERT_LITTLE;
	  break;

	case BIG:
	  if (next_token () != ':')
	    goto error;
	  endian = CONVERT_BIG;
	  break;

	case INTEGER:
	  push_token ();
	  break;

	case END:
	  goto end;
	  break;

	default:
	  goto error;
	  break;
	}
      /* We arrive here when we want to parse a list of
	 numbers.  */
      continue_ulist = 1;
      do
	{
	  tok = next_token ();
	  if (tok != INTEGER)
	    goto error;

	  unit1 = unit_num;
	  tok = next_token ();
	  /* The number can be followed by a - and another number,
	     which means that this is a unit range, a comma
	     or a semicolon.  */
	  if (tok == '-')
	    {
	      if (next_token () != INTEGER)
		goto error;

	      mark_range (unit1, unit_num);
	      tok = next_token ();
	      if (tok == END)
		goto end;
	      else if (tok == ';')
		continue_ulist = 0;
	      else if (tok != ',')
		goto error;
	    }
	  else
	    {
	      mark_single (unit1);
	      switch (tok)
		{
		case ';':
		  continue_ulist = 0;
		  break;

		case ',':
		  break;

		case END:
		  goto end;
		  break;

		default:
		  goto error;
		}
	    }
	} while (continue_ulist);
    }
 end:
  return 0;
 error:
  def = CONVERT_NONE;
  return -1;
}

void init_unformatted (variable * v)
{
  char *val;
  val = getenv (v->name);
  def = CONVERT_NONE;
  n_elist = 0;

  if (val == NULL)
    return;
  do_count = 1;
  p = val;
  do_parse ();
  if (do_count <= 0)
    {
      n_elist = 0;
      elist = NULL;
    }
  else
    {
      elist = get_mem (unit_count * sizeof (exception_t));
      do_count = 0;
      p = val;
      do_parse ();
    }
}

/* Get the default conversion for for an unformatted unit.  */

unit_convert
get_unformatted_convert (int unit)
{
  int i;

  if (elist == NULL)
    return def;
  else if (search_unit (unit, &i))
    return elist[i].conv;
  else
    return def;
}
