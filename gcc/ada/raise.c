/****************************************************************************
 *                                                                          *
 *                         GNAT COMPILER COMPONENTS                         *
 *                                                                          *
 *                                R A I S E                                 *
 *                                                                          *
 *                          C Implementation File                           *
 *                                                                          *
 *             Copyright (C) 1992-2003, Free Software Foundation, Inc.      *
 *                                                                          *
 * GNAT is free software;  you can  redistribute it  and/or modify it under *
 * terms of the  GNU General Public License as published  by the Free Soft- *
 * ware  Foundation;  either version 2,  or (at your option) any later ver- *
 * sion.  GNAT is distributed in the hope that it will be useful, but WITH- *
 * OUT ANY WARRANTY;  without even the  implied warranty of MERCHANTABILITY *
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License *
 * for  more details.  You should have  received  a copy of the GNU General *
 * Public License  distributed with GNAT;  see file COPYING.  If not, write *
 * to  the Free Software Foundation,  59 Temple Place - Suite 330,  Boston, *
 * MA 02111-1307, USA.                                                      *
 *                                                                          *
 * As a  special  exception,  if you  link  this file  with other  files to *
 * produce an executable,  this file does not by itself cause the resulting *
 * executable to be covered by the GNU General Public License. This except- *
 * ion does not  however invalidate  any other reasons  why the  executable *
 * file might be covered by the  GNU Public License.                        *
 *                                                                          *
 * GNAT was originally developed  by the GNAT team at  New York University. *
 * Extensive contributions were provided by Ada Core Technologies Inc.      *
 *                                                                          *
 ****************************************************************************/

/* Routines to support runtime exception handling */

#ifdef IN_RTS
#include "tconfig.h"
/* In the top-of-tree GCC, tconfig does not include tm.h, but in GCC 3.2
   it does.  To avoid branching raise.c just for that purpose, we kludge by
   looking for a symbol always defined by tm.h and if it's not defined,
   we include it.  */
#ifndef FIRST_PSEUDO_REGISTER
#include "coretypes.h"
#include "tm.h"
#endif
#include "tsystem.h"
#include <sys/stat.h>
typedef char bool;
# define true 1
# define false 0
#else
#include "config.h"
#include "system.h"
#endif

#include "adaint.h"
#include "raise.h"

/*  We have not yet figured out how to import this directly */

void
_gnat_builtin_longjmp (ptr, flag)
     void *ptr;
     int flag ATTRIBUTE_UNUSED;
{
   __builtin_longjmp (ptr, 1);
}

/* When an exception is raised for which no handler exists, the procedure
   Ada.Exceptions.Unhandled_Exception is called, which performs the call to
   adafinal to complete finalization, and then prints out the error messages
   for the unhandled exception. The final step is to call this routine, which
   performs any system dependent cleanup required.  */

void
__gnat_unhandled_terminate ()
{
  /* Special termination handling for VMS */

#ifdef VMS
    {
      long prvhnd;

      /* Remove the exception vector so it won't intercept any errors
	 in the call to exit, and go into and endless loop */

      SYS$SETEXV (1, 0, 3, &prvhnd);
      __gnat_os_exit (1);
    }

/* Termination handling for all other systems. */

#elif !defined (__RT__)
    __gnat_os_exit (1);
#endif
}

/* Below is the code related to the integration of the GCC mechanism for
   exception handling.  */

#include "unwind.h"

/* The names of a couple of "standard" routines for unwinding/propagation
   actually vary depending on the underlying GCC scheme for exception handling
   (SJLJ or DWARF). We need a consistently named interface to import from
   a-except, so stubs are defined here.  */

typedef struct _Unwind_Context _Unwind_Context;
typedef struct _Unwind_Exception _Unwind_Exception;

_Unwind_Reason_Code
__gnat_Unwind_RaiseException PARAMS ((_Unwind_Exception *));

_Unwind_Reason_Code
__gnat_Unwind_ForcedUnwind PARAMS ((_Unwind_Exception *, void *, void *));


#ifdef IN_RTS   /* For eh personality routine */

#include "dwarf2.h"
#include "unwind-dw2-fde.h"
#include "unwind-pe.h"


/* --------------------------------------------------------------
   -- The DB stuff below is there for debugging purposes only. --
   -------------------------------------------------------------- */

#define DB_PHASES     0x1
#define DB_CSITE      0x2
#define DB_ACTIONS    0x4
#define DB_REGIONS    0x8

#define DB_ERR        0x1000

/* The "action" stuff below is also there for debugging purposes only.  */

typedef struct
{
  _Unwind_Action phase;
  char * description;
} phase_descriptor;

static phase_descriptor phase_descriptors[]
  = {{ _UA_SEARCH_PHASE,  "SEARCH_PHASE" },
     { _UA_CLEANUP_PHASE, "CLEANUP_PHASE" },
     { _UA_HANDLER_FRAME, "HANDLER_FRAME" },
     { _UA_FORCE_UNWIND,  "FORCE_UNWIND" },
     { -1, 0}};

static int
db_accepted_codes (void)
{
  static int accepted_codes = -1;

  if (accepted_codes == -1)
    {
      char * db_env = getenv ("EH_DEBUG");

      accepted_codes = db_env ? (atoi (db_env) | DB_ERR) : 0;
      /* Arranged for ERR stuff to always be visible when the variable
	 is defined. One may just set the variable to 0 to see the ERR
	 stuff only.  */
    }

  return accepted_codes;
}

#define DB_INDENT_INCREASE 0x01
#define DB_INDENT_DECREASE 0x02
#define DB_INDENT_OUTPUT   0x04
#define DB_INDENT_NEWLINE  0x08
#define DB_INDENT_RESET    0x10

#define DB_INDENT_UNIT     8

static void
db_indent (int requests)
{
  static int current_indentation_level = 0;

  if (requests & DB_INDENT_RESET)
    {
      current_indentation_level = 0;
    }

  if (requests & DB_INDENT_INCREASE)
    {
      current_indentation_level ++;
    }

  if (requests & DB_INDENT_DECREASE)
    {
      current_indentation_level --;
    }

  if (requests & DB_INDENT_NEWLINE)
    {
      fprintf (stderr, "\n");
    }

  if (requests & DB_INDENT_OUTPUT)
    {
      fprintf (stderr, "%*s",
	       current_indentation_level * DB_INDENT_UNIT, " ");
    }

}

static void
db (int db_code, char * msg_format, ...)
{
  if (db_accepted_codes () & db_code)
    {
      va_list msg_args;

      db_indent (DB_INDENT_OUTPUT);

      va_start (msg_args, msg_format);
      vfprintf (stderr, msg_format, msg_args);
      va_end (msg_args);
    }
}

static void
db_phases (int phases)
{
  phase_descriptor *a = phase_descriptors;

  if (! (db_accepted_codes() & DB_PHASES))
    return;

  db (DB_PHASES, "\n");

  for (; a->description != 0; a++)
    if (phases & a->phase)
      db (DB_PHASES, "%s ", a->description);

  db (DB_PHASES, " :\n");
}


/* ---------------------------------------------------------------
   --  Now come a set of useful structures and helper routines. --
   --------------------------------------------------------------- */

/* There are three major runtime tables involved, generated by the
   GCC back-end. Contents slightly vary depending on the underlying
   implementation scheme (dwarf zero cost / sjlj).

   =======================================
   * Tables for the dwarf zero cost case *
   =======================================

   call_site []
   -------------------------------------------------------------------
   * region-start | region-length | landing-pad | first-action-index *
   -------------------------------------------------------------------

   Identify possible actions to be taken and where to resume control
   for that when an exception propagates through a pc inside the region
   delimited by start and length.

   A null landing-pad indicates that nothing is to be done.

   Otherwise, first-action-index provides an entry into the action[]
   table which heads a list of possible actions to be taken (see below).

   If it is determined that indeed an action should be taken, that
   is, if one action filter matches the exception beeing propagated,
   then control should be transfered to landing-pad.

   A null first-action-index indicates that there are only cleanups
   to run there.

   action []
   -------------------------------
   * action-filter | next-action *
   -------------------------------

   This table contains lists (called action chains) of possible actions
   associated with call-site entries described in the call-site [] table.
   There is at most one action list per call-site entry.

   A null action-filter indicates a cleanup.

   Non null action-filters provide an index into the ttypes [] table
   (see below), from which information may be retrieved to check if it
   matches the exception beeing propagated.

   action-filter > 0  means there is a regular handler to be run,

   action-filter < 0  means there is a some "exception_specification"
                      data to retrieve, which is only relevant for C++
		      and should never show up for Ada.

   next-action indexes the next entry in the list. 0 indicates there is
   no other entry.

   ttypes []
   ---------------
   * ttype-value *
   ---------------

   A null value indicates a catch-all handler in C++, and an "others"
   handler in Ada.

   Non null values are used to match the exception beeing propagated:
   In C++ this is a pointer to some rtti data, while in Ada this is an
   exception id.

   The special id value 1 indicates an "all_others" handler.

   For C++, this table is actually also used to store "exception
   specification" data. The differentiation between the two kinds
   of entries is made by the sign of the associated action filter,
   which translates into positive or negative offsets from the
   so called base of the table:

   Exception Specification data is stored at positive offsets from
   the ttypes table base, which Exception Type data is stored at
   negative offsets:

   ---------------------------------------------------------------------------

   Here is a quick summary of the tables organization:

	  +-- Unwind_Context (pc, ...)
	  |
	  |(pc)
	  |
	  |   CALL-SITE[]
	  |
	  |   +=============================================================+
	  |   | region-start + length |  landing-pad   | first-action-index |
	  |   +=============================================================+
	  +-> |       pc range          0 => no-action   0 => cleanups only |
	      |                         !0 => jump @              N --+     |
	      +====================================================== | ====+
                                                                      |
                                                                      |
       ACTION []                                                      |
                                                                      |
       +==========================================================+   |
       |              action-filter           |   next-action     |   |
       +==========================================================+   |
       |  0 => cleanup                                            |   |
       | >0 => ttype index for handler ------+  0 => end of chain | <-+
       | <0 => ttype index for spec data     |                    |
       +==================================== | ===================+
                                             |
                                             |
       TTYPES []                             |
					     |  Offset negated from
		 +=====================+     |  the actual base.
		 |     ttype-value     |     |
    +============+=====================+     |
    |            |  0 => "others"      |     |
    |    ...     |  1 => "all others"  | <---+
    |            |  X => exception id  |
    |  handlers	 +---------------------+
    |            |        ...          |
    |    ...     |        ...          |
    |            |        ...          |
    +============+=====================+ <<------ Table base
    |    ...     |        ...          |
    |   specs    |        ...          | (should not see negative filter
    |    ...     |        ...          |  values for Ada).
    +============+=====================+


   ============================
   * Tables for the sjlj case *
   ============================

   So called "function contexts" are pushed on a context stack by calls to
   _Unwind_SjLj_Register on function entry, and popped off at exit points by
   calls to _Unwind_SjLj_Unregister. The current call_site for a function is
   updated in the function context as the function's code runs along.

   The generic unwinding engine in _Unwind_RaiseException walks the function
   context stack and not the actual call chain.

   The ACTION and TTYPES tables remain unchanged, which allows to search them
   during the propagation phase to determine wether or not the propagated
   exception is handled somewhere. When it is, we only "jump" up once directly
   to the context where the handler will be found. Besides, this allows "break
   exception unhandled" to work also

   The CALL-SITE table is setup differently, though: the pc attached to the
   unwind context is a direct index into the table, so the entries in this
   table do not hold region bounds any more.

   A special index (-1) is used to indicate that no action is possibly
   connected with the context at hand, so null landing pads cannot appear
   in the table.

   Additionally, landing pad values in the table do not represent code address
   to jump at, but so called "dispatch" indices used by a common landing pad
   for the function to switch to the appropriate post-landing-pad.

   +-- Unwind_Context (pc, ...)
   |
   | pc = call-site index
   |  0 => terminate (should not see this for Ada)
   | -1 => no-action
   |
   |   CALL-SITE[]
   |
   |   +=====================================+
   |   |  landing-pad   | first-action-index |
   |   +=====================================+
   +-> |                  0 => cleanups only |
       | dispatch index             N        |
       +=====================================+


   ===================================
   * Basic organization of this unit *
   ===================================

   The major point of this unit is to provide an exception propagation
   personality routine for Ada. This is __gnat_eh_personality.

   It is provided with a pointer to the propagated exception, an unwind
   context describing a location the propagation is going through, and a
   couple of other arguments including a description of the current
   propagation phase.

   It shall return to the generic propagation engine what is to be performed
   next, after possible context adjustments, depending on what it finds in the
   traversed context (a handler for the exception, a cleanup, nothing, ...),
   and on the propagation phase.

   A number of structures and subroutines are used for this purpose, as
   sketched below:

   o region_descriptor: General data associated with the context (base pc,
     call-site table, action table, ttypes table, ...)

   o action_descriptor: Data describing the action to be taken for the
     propagated exception in the provided context (kind of action: nothing,
     handler, cleanup; pointer to the action table entry, ...).

   raise
     |
    ... (a-except.adb)
     |
   Propagate_Exception (a-exexpr.adb)
     |
     |
   _Unwind_RaiseException (libgcc)
     |
     |   (Ada frame)
     |
     +--> __gnat_eh_personality (context, exception)
	   |
	   +--> get_region_descriptor_for (context)
	   |
	   +--> get_action_descriptor_for (context, exception, region)
	   |       |
	   |       +--> get_call_site_action_for (context, region)
	   |            (one version for each underlying scheme)
           |
	   +--> setup_to_install (context)

   This unit is inspired from the C++ version found in eh_personality.cc,
   part of libstdc++-v3.

*/


/* This is the structure of exception objects as built by the GNAT runtime
   library (a-exexpr.adb). The layouts should exactly match, and the "common"
   header is mandated by the exception handling ABI.  */

typedef struct
{
  _Unwind_Exception common;
  /* ABI header, maximally aligned. */

  _Unwind_Ptr id;
  /* Id of the exception beeing propagated, filled by Propagate_Exception.

     This is compared against the ttype entries associated with actions in the
     examined context to see if one of these actions matches.  */

  bool handled_by_others;
  /* Indicates wether a "when others" may catch this exception, also filled by
     Propagate_Exception.

     This is used to decide if a GNAT_OTHERS ttype entry matches.  */

  int  n_cleanups_to_trigger;
  /* Number of cleanups on the propagation way for the occurrence. This is
     initialized to 0 by Propagate_Exception and computed by the personality
     routine during the first phase of the propagation (incremented for each
     context in which only cleanup actions match).

     This is used by Propagate_Exception when the occurrence is not handled,
     to control a forced unwinding phase aimed at triggering all the cleanups
     before calling Unhandled_Exception_Terminate.

     This is also used by __gnat_eh_personality to identify the point at which
     the notification routine shall be called for a handled occurrence.  */
} _GNAT_Exception;

/* The two constants below are specific ttype identifiers for special
   exception ids. Their value is currently hardcoded at the gigi level
   (see N_Exception_Handler).  */

#define GNAT_OTHERS      ((_Unwind_Ptr) 0x0)
#define GNAT_ALL_OTHERS  ((_Unwind_Ptr) 0x1)

/* Describe the useful region data associated with an unwind context.  */

typedef struct
{
  /* The base pc of the region.  */
  _Unwind_Ptr base;

  /* Pointer to the Language Specific Data for the region.  */
  _Unwind_Ptr lsda;

  /* Call-Site data associated with this region.  */
  unsigned char call_site_encoding;
  const unsigned char *call_site_table;

  /* The base to which are relative landing pad offsets inside the call-site
     entries .  */
  _Unwind_Ptr lp_base;

  /* Action-Table associated with this region.  */
  const unsigned char *action_table;

  /* Ttype data associated with this region.  */
  unsigned char ttype_encoding;
  const unsigned char *ttype_table;
  _Unwind_Ptr ttype_base;

} region_descriptor;

static void
db_region_for (region, uw_context)
     region_descriptor *region;
     _Unwind_Context *uw_context;
{
  _Unwind_Ptr ip = _Unwind_GetIP (uw_context) - 1;

  if (! (db_accepted_codes () & DB_REGIONS))
    return;

  db (DB_REGIONS, "For ip @ 0x%08x => ", ip);

  if (region->lsda)
    db (DB_REGIONS, "lsda @ 0x%x", region->lsda);
  else
    db (DB_REGIONS, "no lsda");

  db (DB_REGIONS, "\n");
}

/* Retrieve the ttype entry associated with FILTER in the REGION's
   ttype table.  */

static const _Unwind_Ptr
get_ttype_entry_for (region, filter)
     region_descriptor *region;
     long filter;
{
  _Unwind_Ptr ttype_entry;

  filter *= size_of_encoded_value (region->ttype_encoding);
  read_encoded_value_with_base
    (region->ttype_encoding, region->ttype_base,
     region->ttype_table - filter, &ttype_entry);

  return ttype_entry;
}

/* Fill out the REGION descriptor for the provided UW_CONTEXT.  */

static void
get_region_description_for (uw_context, region)
     _Unwind_Context *uw_context;
     region_descriptor *region;
{
  const unsigned char * p;
  _Unwind_Word tmp;
  unsigned char lpbase_encoding;

  /* Get the base address of the lsda information. If the provided context
     is null or if there is no associated language specific data, there's
     nothing we can/should do.  */
  region->lsda
    = (_Unwind_Ptr) (uw_context
		     ? _Unwind_GetLanguageSpecificData (uw_context) : 0);

  if (! region->lsda)
    return;

  /* Parse the lsda and fill the region descriptor.  */
  p = (char *)region->lsda;

  region->base = _Unwind_GetRegionStart (uw_context);

  /* Find @LPStart, the base to which landing pad offsets are relative.  */
  lpbase_encoding = *p++;
  if (lpbase_encoding != DW_EH_PE_omit)
    p = read_encoded_value
      (uw_context, lpbase_encoding, p, &region->lp_base);
  else
    region->lp_base = region->base;

  /* Find @TType, the base of the handler and exception spec type data.  */
  region->ttype_encoding = *p++;
  if (region->ttype_encoding != DW_EH_PE_omit)
    {
      p = read_uleb128 (p, &tmp);
      region->ttype_table = p + tmp;
    }
   else
     region->ttype_table = 0;

  region->ttype_base
    = base_of_encoded_value (region->ttype_encoding, uw_context);

  /* Get the encoding and length of the call-site table; the action table
     immediately follows.  */
  region->call_site_encoding = *p++;
  region->call_site_table = read_uleb128 (p, &tmp);

  region->action_table = region->call_site_table + tmp;
}


/* Describe an action to be taken when propagating an exception up to
   some context.  */

typedef enum
{
  /* Found some call site base data, but need to analyze further
     before beeing able to decide.  */
  unknown,

  /* There is nothing relevant in the context at hand. */
  nothing,

  /* There are only cleanups to run in this context.  */
  cleanup,

  /* There is a handler for the exception in this context.  */
  handler
} action_kind;


typedef struct
{
  /* The kind of action to be taken.  */
  action_kind kind;

  /* A pointer to the action record entry.  */
  const unsigned char *table_entry;

  /* Where we should jump to actually take an action (trigger a cleanup or an
     exception handler).  */
  _Unwind_Ptr landing_pad;

  /* If we have a handler matching our exception, these are the filter to
     trigger it and the corresponding id.  */
  _Unwind_Sword ttype_filter;
  _Unwind_Ptr   ttype_entry;

} action_descriptor;


static void
db_action_for (action, uw_context)
     action_descriptor *action;
     _Unwind_Context *uw_context;
{
  _Unwind_Ptr ip = _Unwind_GetIP (uw_context) - 1;

  db (DB_ACTIONS, "For ip @ 0x%08x => ", ip);

  switch (action->kind)
     {
     case unknown:
       db (DB_ACTIONS, "lpad @ 0x%x, record @ 0x%x\n",
	   ip, action->landing_pad, action->table_entry);
       break;

     case nothing:
       db (DB_ACTIONS, "Nothing\n");
       break;

     case cleanup:
       db (DB_ACTIONS, "Cleanup\n");
       break;

     case handler:
       db (DB_ACTIONS, "Handler, filter = %d\n", action->ttype_filter);
       break;

     default:
       db (DB_ACTIONS, "Err? Unexpected action kind !\n");
       break;
    }

  return;
}


/* Search the call_site_table of REGION for an entry appropriate for the
   UW_CONTEXT's ip. If one is found, store the associated landing_pad and
   action_table entry, and set the ACTION kind to unknown for further
   analysis. Otherwise, set the ACTION kind to nothing.

   There are two variants of this routine, depending on the underlying
   mechanism (dwarf/sjlj), which account for differences in the tables
   organization.
*/

#ifdef __USING_SJLJ_EXCEPTIONS__

#define __builtin_eh_return_data_regno(x) x

static void
get_call_site_action_for (uw_context, region, action)
     _Unwind_Context *uw_context;
     region_descriptor *region;
     action_descriptor *action;
{
  _Unwind_Ptr call_site
    = _Unwind_GetIP (uw_context) - 1;
  /* Subtract 1 because GetIP returns the actual call_site value + 1.  */

  /* call_site is a direct index into the call-site table, with two special
     values : -1 for no-action and 0 for "terminate". The latter should never
     show up for Ada. To test for the former, beware that _Unwind_Ptr might be
     unsigned.  */

  if ((int)call_site < 0)
    {
      action->kind = nothing;
      return;
    }
  else if (call_site == 0)
    {
      db (DB_ERR, "========> Err, null call_site for Ada/sjlj\n");
      action->kind = nothing;
      return;
    }
  else
    {
      _Unwind_Word cs_lp, cs_action;

      /* Let the caller know there may be an action to take, but let it
	 determine the kind.  */
      action->kind = unknown;

      /* We have a direct index into the call-site table, but this table is
	 made of leb128 values, the encoding length of which is variable. We
	 can't merely compute an offset from the index, then, but have to read
	 all the entries before the one of interest.  */

      const unsigned char * p = region->call_site_table;

      do {
	p = read_uleb128 (p, &cs_lp);
	p = read_uleb128 (p, &cs_action);
      } while (--call_site);


      action->landing_pad = cs_lp + 1;

      if (cs_action)
	action->table_entry = region->action_table + cs_action - 1;
      else
	action->table_entry = 0;

      return;
    }
}

#else
/* ! __USING_SJLJ_EXCEPTIONS__ */

static void
get_call_site_action_for (uw_context, region, action)
     _Unwind_Context *uw_context;
     region_descriptor *region;
     action_descriptor *action;
{
  _Unwind_Ptr ip
    = _Unwind_GetIP (uw_context) - 1;
  /* Substract 1 because GetIP yields a call return address while we are
     interested in information for the call point. This does not always yield
     the exact call instruction address but always brings the ip back within
     the corresponding region.

     ??? When unwinding up from a signal handler triggered by a trap on some
     instruction, we usually have the faulting instruction address here and
     subtracting 1 might get us into the wrong region.  */

  const unsigned char * p
    = region->call_site_table;

  /* Unless we are able to determine otherwise ... */
  action->kind = nothing;

  db (DB_CSITE, "\n");

  while (p < region->action_table)
    {
      _Unwind_Ptr cs_start, cs_len, cs_lp;
      _Unwind_Word cs_action;

      /* Note that all call-site encodings are "absolute" displacements.  */
      p = read_encoded_value (0, region->call_site_encoding, p, &cs_start);
      p = read_encoded_value (0, region->call_site_encoding, p, &cs_len);
      p = read_encoded_value (0, region->call_site_encoding, p, &cs_lp);
      p = read_uleb128 (p, &cs_action);

      db (DB_CSITE,
	  "c_site @ 0x%08x (+0x%03x), len = %3d, lpad @ 0x%08x (+0x%03x)\n",
	  region->base+cs_start, cs_start, cs_len,
	  region->lp_base+cs_lp, cs_lp);

      /* The table is sorted, so if we've passed the ip, stop.  */
      if (ip < region->base + cs_start)
 	break;

      /* If we have a match, fill the ACTION fields accordingly.  */
      else if (ip < region->base + cs_start + cs_len)
	{
	  /* Let the caller know there may be an action to take, but let it
	     determine the kind.  */
	  action->kind = unknown;

	  if (cs_lp)
	    action->landing_pad = region->lp_base + cs_lp;
	  else
	    action->landing_pad = 0;

	  if (cs_action)
	    action->table_entry = region->action_table + cs_action - 1;
	  else
	    action->table_entry = 0;

	  db (DB_CSITE, "+++\n");
	  return;
	}
    }

  db (DB_CSITE, "---\n");
}

#endif

/* Fill out the ACTION to be taken from propagating UW_EXCEPTION up to
   UW_CONTEXT in REGION.  */

static void
get_action_description_for (uw_context, uw_exception, region, action)
     _Unwind_Context *uw_context;
     _Unwind_Exception *uw_exception;
     region_descriptor *region;
     action_descriptor *action;
{
  _GNAT_Exception * gnat_exception = (_GNAT_Exception *) uw_exception;

  /* Search the call site table first, which may get us a landing pad as well
     as the head of an action record list.  */
  get_call_site_action_for (uw_context, region, action);
  db_action_for (action, uw_context);

  /* If there is not even a call_site entry, we are done.  */
  if (action->kind == nothing)
    return;

  /* Otherwise, check what we have at the place of the call site  */

  /* No landing pad => no cleanups or handlers.  */
  if (action->landing_pad == 0)
    {
      action->kind = nothing;
      return;
    }

  /* Landing pad + null table entry => only cleanups.  */
  else if (action->table_entry == 0)
    {
      action->kind = cleanup;
      return;
    }

  /* Landing pad + Table entry => handlers + possible cleanups.  */
  else
    {
      const unsigned char * p = action->table_entry;

      _Unwind_Sword ar_filter, ar_disp;

      action->kind = nothing;

      while (1)
	{
	  p = read_sleb128 (p, &ar_filter);
	  read_sleb128 (p, &ar_disp);
	  /* Don't assign p here, as it will be incremented by ar_disp
	     below.  */

	  /* Null filters are for cleanups. */
	  if (ar_filter == 0)
	    action->kind = cleanup;

	  /* Positive filters are for regular handlers.  */
	  else if (ar_filter > 0)
	    {
	      /* See if the filter we have is for an exception which matches
		 the one we are propagating.  */
	      _Unwind_Ptr eid = get_ttype_entry_for (region, ar_filter);

	      if (eid == gnat_exception->id
		  || eid == GNAT_ALL_OTHERS
		  || (eid == GNAT_OTHERS && gnat_exception->handled_by_others))
		{
		  action->ttype_filter = ar_filter;
		  action->ttype_entry = eid;
		  action->kind = handler;
		  return;
		}
	    }

	  /* Negative filter values are for C++ exception specifications.
	     Should not be there for Ada :/  */
	  else
	    db (DB_ERR, "========> Err, filter < 0 for Ada/dwarf\n");

	  if (ar_disp == 0)
	    return;

	  p += ar_disp;
	}
    }
}

/* Setup in UW_CONTEXT the eh return target IP and data registers, which will
   be restored with the others and retrieved by the landing pad once the jump
   occured.  */

static void
setup_to_install (uw_context, uw_exception, uw_landing_pad, uw_filter)
     _Unwind_Context *uw_context;
     _Unwind_Exception *uw_exception;
     int uw_filter;
     _Unwind_Ptr uw_landing_pad;
{
#ifndef EH_RETURN_DATA_REGNO
  /* We should not be called if the appropriate underlying support is not
     there.  */
  abort ();
#else
  /* 1/ exception object pointer, which might be provided back to
     _Unwind_Resume (and thus to this personality routine) if we are jumping
     to a cleanup.  */
  _Unwind_SetGR (uw_context, __builtin_eh_return_data_regno (0),
		 (_Unwind_Word)uw_exception);

  /* 2/ handler switch value register, which will also be used by the target
     landing pad to decide what action it shall take.  */
  _Unwind_SetGR (uw_context, __builtin_eh_return_data_regno (1),
		 (_Unwind_Word)uw_filter);

  /* Setup the address we should jump at to reach the code where there is the
     "something" we found.  */
  _Unwind_SetIP (uw_context, uw_landing_pad);
#endif
}

/* The following is defined from a-except.adb. Its purpose is to enable
   automatic backtraces upon exception raise, as provided through the
   GNAT.Traceback facilities.  */
extern void __gnat_notify_handled_exception PARAMS ((void));
extern void __gnat_notify_unhandled_exception PARAMS ((void));

/* Below is the eh personality routine per se. We currently assume that only
   GNU-Ada exceptions are met.  */

_Unwind_Reason_Code
__gnat_eh_personality (uw_version, uw_phases,
		       uw_exception_class, uw_exception, uw_context)
     int uw_version;
     _Unwind_Action uw_phases;
     _Unwind_Exception_Class uw_exception_class;
     _Unwind_Exception *uw_exception;
     _Unwind_Context *uw_context;
{
  _GNAT_Exception * gnat_exception = (_GNAT_Exception *) uw_exception;

  region_descriptor region;
  action_descriptor action;

  if (uw_version != 1)
    return _URC_FATAL_PHASE1_ERROR;

  db_indent (DB_INDENT_RESET);
  db_phases (uw_phases);
  db_indent (DB_INDENT_INCREASE);

  /* Get the region description for the context we were provided with. This
     will tell us if there is some lsda, call_site, action and/or ttype data
     for the associated ip.  */
  get_region_description_for (uw_context, &region);
  db_region_for (&region, uw_context);

  /* No LSDA => no handlers or cleanups => we shall unwind further up.  */
  if (! region.lsda)
    return _URC_CONTINUE_UNWIND;

  /* Search the call-site and action-record tables for the action associated
     with this IP.  */
  get_action_description_for (uw_context, uw_exception, &region, &action);
  db_action_for (&action, uw_context);

  /* Whatever the phase, if there is nothing relevant in this frame,
     unwinding should just go on.  */
  if (action.kind == nothing)
    return _URC_CONTINUE_UNWIND;

  /* If we found something in search phase, we should return a code indicating
     what to do next depending on what we found. If we only have cleanups
     around, we shall try to unwind further up to find a handler, otherwise,
     tell we have a handler, which will trigger the second phase.  */
  if (uw_phases & _UA_SEARCH_PHASE)
    {
      if (action.kind == cleanup)
	{
	  gnat_exception->n_cleanups_to_trigger ++;
	  return _URC_CONTINUE_UNWIND;
	}
      else
	{
	  /* Trigger the appropriate notification routines before the second
	     phase starts, which ensures the stack is still intact. */
	  __gnat_notify_handled_exception ();

	  return _URC_HANDLER_FOUND;
	}
    }

  /* We found something in cleanup/handler phase, which might be the handler
     or a cleanup for a handled occurrence, or a cleanup for an unhandled
     occurrence (we are in a FORCED_UNWIND phase in this case). Install the
     context to get there.  */

  /* If we are going to install a cleanup context, decrement the cleanup
     count.  This is required in a FORCED_UNWINDing phase (for an unhandled
     exception), as this is used from the forced unwinding handler in
     Ada.Exceptions.Exception_Propagation to decide wether unwinding should
     proceed further or Unhandled_Exception_Terminate should be called.  */
  if (action.kind == cleanup)
    gnat_exception->n_cleanups_to_trigger --;

  setup_to_install
    (uw_context, uw_exception, action.landing_pad, action.ttype_filter);

  return _URC_INSTALL_CONTEXT;
}

/* Define the consistently named stubs imported by Propagate_Exception.  */

#ifdef __USING_SJLJ_EXCEPTIONS__

#undef _Unwind_RaiseException

_Unwind_Reason_Code
__gnat_Unwind_RaiseException (e)
     _Unwind_Exception *e;
{
  return _Unwind_SjLj_RaiseException (e);
}


#undef _Unwind_ForcedUnwind

_Unwind_Reason_Code
__gnat_Unwind_ForcedUnwind (e, handler, argument)
     _Unwind_Exception *e;
     void * handler;
     void * argument;
{
  return _Unwind_SjLj_ForcedUnwind (e, handler, argument);
}


#else /* __USING_SJLJ_EXCEPTIONS__ */

_Unwind_Reason_Code
__gnat_Unwind_RaiseException (e)
     _Unwind_Exception *e;
{
  return _Unwind_RaiseException (e);
}

_Unwind_Reason_Code
__gnat_Unwind_ForcedUnwind (e, handler, argument)
     _Unwind_Exception *e;
     void * handler;
     void * argument;
{
  return _Unwind_ForcedUnwind (e, handler, argument);
}

#endif /* __USING_SJLJ_EXCEPTIONS__ */

#else
/* ! IN_RTS  */

/* The calls to the GCC runtime interface for exception raising are currently
   issued from a-exexpr.adb, which is used by both the runtime library and the
   compiler.

   As the compiler binary is not linked against the GCC runtime library, we
   need also need stubs for this interface in the compiler case. We should not
   be using the GCC eh mechanism for the compiler, however, so expect these
   functions never to be called.  */

_Unwind_Reason_Code
__gnat_Unwind_RaiseException (e)
     _Unwind_Exception *e ATTRIBUTE_UNUSED;
{
  abort ();
}


_Unwind_Reason_Code
__gnat_Unwind_ForcedUnwind (e, handler, argument)
     _Unwind_Exception *e ATTRIBUTE_UNUSED;
     void * handler ATTRIBUTE_UNUSED;
     void * argument ATTRIBUTE_UNUSED;
{
  abort ();
}

#endif /* IN_RTS */
