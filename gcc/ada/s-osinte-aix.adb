------------------------------------------------------------------------------
--                                                                          --
--                 GNAT RUN-TIME LIBRARY (GNARL) COMPONENTS                 --
--                                                                          --
--                   S Y S T E M . O S _ I N T E R F A C E                  --
--                                                                          --
--                                  B o d y                                 --
--                                                                          --
--          Copyright (C) 1997-2006, Free Software Fundation, Inc.          --
--                                                                          --
-- GNARL is free software; you can  redistribute it  and/or modify it under --
-- terms of the  GNU General Public License as published  by the Free Soft- --
-- ware  Foundation;  either version 2,  or (at your option) any later ver- --
-- sion. GNARL is distributed in the hope that it will be useful, but WITH- --
-- OUT ANY WARRANTY;  without even the  implied warranty of MERCHANTABILITY --
-- or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License --
-- for  more details.  You should have  received  a copy of the GNU General --
-- Public License  distributed with GNARL; see file COPYING.  If not, write --
-- to  the  Free Software Foundation,  51  Franklin  Street,  Fifth  Floor, --
-- Boston, MA 02110-1301, USA.                                              --
--                                                                          --
-- As a special exception,  if other files  instantiate  generics from this --
-- unit, or you link  this unit with other files  to produce an executable, --
-- this  unit  does not  by itself cause  the resulting  executable  to  be --
-- covered  by the  GNU  General  Public  License.  This exception does not --
-- however invalidate  any other reasons why  the executable file  might be --
-- covered by the  GNU Public License.                                      --
--                                                                          --
-- GNARL was developed by the GNARL team at Florida State University.       --
-- Extensive contributions were provided by Ada Core Technologies, Inc.     --
--                                                                          --
------------------------------------------------------------------------------

--  This is a AIX (Native) version of this package

pragma Polling (Off);
--  Turn off polling, we do not want ATC polling to take place during
--  tasking operations. It causes infinite loops and other problems.

package body System.OS_Interface is

   use Interfaces.C;

   -----------------
   -- To_Duration --
   -----------------

   function To_Duration (TS : timespec) return Duration is
   begin
      return Duration (TS.tv_sec) + Duration (TS.tv_nsec) / 10#1#E9;
   end To_Duration;

   function To_Duration (TV : struct_timeval) return Duration is
   begin
      return Duration (TV.tv_sec) + Duration (TV.tv_usec) / 10#1#E6;
   end To_Duration;

   ------------------------
   -- To_Target_Priority --
   ------------------------

   function To_Target_Priority
     (Prio : System.Any_Priority) return Interfaces.C.int
   is
   begin
      --  Priorities on AIX are defined in the range 1 .. 127, so we
      --  map 0 .. 126 to 1 .. 127.

      return Interfaces.C.int (Prio) + 1;
   end To_Target_Priority;

   -----------------
   -- To_Timespec --
   -----------------

   function To_Timespec (D : Duration) return timespec is
      S : time_t;
      F : Duration;

   begin
      S := time_t (Long_Long_Integer (D));
      F := D - Duration (S);

      --  If F has negative value due to a round-up, adjust for positive F
      --  value.

      if F < 0.0 then
         S := S - 1;
         F := F + 1.0;
      end if;

      return timespec'(tv_sec => S,
                       tv_nsec => long (Long_Long_Integer (F * 10#1#E9)));
   end To_Timespec;

   ----------------
   -- To_Timeval --
   ----------------

   function To_Timeval (D : Duration) return struct_timeval is
      S : long;
      F : Duration;

   begin
      S := long (Long_Long_Integer (D));
      F := D - Duration (S);

      --  If F has negative value due to a round-up, adjust for positive F
      --  value.

      if F < 0.0 then
         S := S - 1;
         F := F + 1.0;
      end if;

      return
        struct_timeval'
          (tv_sec => S,
           tv_usec => long (Long_Long_Integer (F * 10#1#E6)));
   end To_Timeval;

   -------------------
   -- clock_gettime --
   -------------------

   function clock_gettime
     (clock_id : clockid_t;
      tp       : access timespec)
      return     int
   is
      pragma Warnings (Off, clock_id);

      Result : int;
      tv     : aliased struct_timeval;

      function gettimeofday
        (tv   : access struct_timeval;
         tz   : System.Address := System.Null_Address)
         return int;
      pragma Import (C, gettimeofday, "gettimeofday");

   begin
      Result := gettimeofday (tv'Unchecked_Access);
      tp.all := To_Timespec (To_Duration (tv));
      return Result;
   end clock_gettime;

   -----------------
   -- sched_yield --
   -----------------

   --  AIX Thread does not have sched_yield;

   function sched_yield return int is
      procedure pthread_yield;
      pragma Import (C, pthread_yield, "sched_yield");
   begin
      pthread_yield;
      return 0;
   end sched_yield;

   --------------------
   -- Get_Stack_Base --
   --------------------

   function Get_Stack_Base (thread : pthread_t) return Address is
      pragma Warnings (Off, thread);
   begin
      return Null_Address;
   end Get_Stack_Base;

   --------------------------
   -- PTHREAD_PRIO_INHERIT --
   --------------------------

   AIX_Version : Integer := 0;
   --  AIX version in the form xy for AIX version x.y (0 means not set)

   SYS_NMLN : constant := 32;
   --  AIX system constant used to define utsname, see sys/utsname.h

   subtype String_NMLN is String (1 .. SYS_NMLN);

   type utsname is record
      sysname    : String_NMLN;
      nodename   : String_NMLN;
      release    : String_NMLN;
      version    : String_NMLN;
      machine    : String_NMLN;
      procserial : String_NMLN;
   end record;
   pragma Convention (C, utsname);

   procedure uname (name : out utsname);
   pragma Import (C, uname);

   function PTHREAD_PRIO_INHERIT return int is
      name : utsname;

      function Val (C : Character) return Integer;
      --  Transform a numeric character ('0' .. '9') to an integer

      ---------
      -- Val --
      ---------

      function Val (C : Character) return Integer is
      begin
         return Character'Pos (C) - Character'Pos ('0');
      end Val;

   --  Start of processing for PTHREAD_PRIO_INHERIT

   begin
      if AIX_Version = 0 then

         --  Set AIX_Version

         uname (name);
         AIX_Version := Val (name.version (1)) * 10 + Val (name.release (1));
      end if;

      if AIX_Version < 53 then

         --  Under AIX < 5.3, PTHREAD_PRIO_INHERIT is defined as 0 in pthread.h

         return 0;

      else
         --  Under AIX >= 5.3, PTHREAD_PRIO_INHERIT is defined as 3

         return 3;
      end if;
   end PTHREAD_PRIO_INHERIT;

end System.OS_Interface;
