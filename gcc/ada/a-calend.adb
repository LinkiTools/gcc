------------------------------------------------------------------------------
--                                                                          --
--                         GNAT RUN-TIME COMPONENTS                         --
--                                                                          --
--                         A D A . C A L E N D A R                          --
--                                                                          --
--                                 B o d y                                  --
--                                                                          --
--          Copyright (C) 1992-2007, Free Software Foundation, Inc.         --
--                                                                          --
-- GNAT is free software;  you can  redistribute it  and/or modify it under --
-- terms of the  GNU General Public License as published  by the Free Soft- --
-- ware  Foundation;  either version 2,  or (at your option) any later ver- --
-- sion.  GNAT is distributed in the hope that it will be useful, but WITH- --
-- OUT ANY WARRANTY;  without even the  implied warranty of MERCHANTABILITY --
-- or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License --
-- for  more details.  You should have  received  a copy of the GNU General --
-- Public License  distributed with GNAT;  see file COPYING.  If not, write --
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
-- GNAT was originally developed  by the GNAT team at  New York University. --
-- Extensive contributions were provided by Ada Core Technologies Inc.      --
--                                                                          --
------------------------------------------------------------------------------

with Ada.Unchecked_Conversion;

with System.OS_Primitives;
--  used for Clock

package body Ada.Calendar is

   --------------------------
   -- Implementation Notes --
   --------------------------

   --  In complex algorithms, some variables of type Ada.Calendar.Time carry
   --  suffix _S or _N to denote units of seconds or nanoseconds.
   --
   --  Because time is measured in different units and from different origins
   --  on various targets, a system independent model is incorporated into
   --  Ada.Calendar. The idea behind the design is to encapsulate all target
   --  dependent machinery in a single package, thus providing a uniform
   --  interface to all existing and any potential children.

   --     package Ada.Calendar
   --        procedure Split (5 parameters) -------+
   --                                              | Call from local routine
   --     private                                  |
   --        package Formatting_Operations         |
   --           procedure Split (11 parameters) <--+
   --        end Formatting_Operations             |
   --     end Ada.Calendar                         |
   --                                              |
   --     package Ada.Calendar.Formatting          | Call from child routine
   --        procedure Split (9 or 10 parameters) -+
   --     end Ada.Calendar.Formatting

   --  The behaviour of the interfacing routines is controlled via various
   --  flags. All new Ada 2005 types from children of Ada.Calendar are
   --  emulated by a similar type. For instance, type Day_Number is replaced
   --  by Integer in various routines. One ramification of this model is that
   --  the caller site must perform validity checks on returned results.
   --  The end result of this model is the lack of target specific files per
   --  child of Ada.Calendar (a-calfor, a-calfor-vms, a-calfor-vxwors, etc).

   -----------------------
   -- Local Subprograms --
   -----------------------

   procedure Check_Within_Time_Bounds (T : Time_Rep);
   --  Ensure that a time representation value falls withing the bounds of Ada
   --  time. Leap seconds support is taken into account.

   procedure Cumulative_Leap_Seconds
     (Start_Date    : Time_Rep;
      End_Date      : Time_Rep;
      Elapsed_Leaps : out Natural;
      Next_Leap     : out Time_Rep);
   --  Elapsed_Leaps is the sum of the leap seconds that have occurred on or
   --  after Start_Date and before (strictly before) End_Date. Next_Leap_Sec
   --  represents the next leap second occurrence on or after End_Date. If
   --  there are no leaps seconds after End_Date, End_Of_Time is returned.
   --  End_Of_Time can be used as End_Date to count all the leap seconds that
   --  have occurred on or after Start_Date.
   --
   --  Note: Any sub seconds of Start_Date and End_Date are discarded before
   --  the calculations are done. For instance: if 113 seconds is a leap
   --  second (it isn't) and 113.5 is input as an End_Date, the leap second
   --  at 113 will not be counted in Leaps_Between, but it will be returned
   --  as Next_Leap_Sec. Thus, if the caller wants to know if the End_Date is
   --  a leap second, the comparison should be:
   --
   --     End_Date >= Next_Leap_Sec;
   --
   --  After_Last_Leap is designed so that this comparison works without
   --  having to first check if Next_Leap_Sec is a valid leap second.

   function Duration_To_Time_Rep is
     new Ada.Unchecked_Conversion (Duration, Time_Rep);
   --  Convert a duration value into a time representation value

   function Time_Rep_To_Duration is
     new Ada.Unchecked_Conversion (Time_Rep, Duration);
   --  Convert a time representation value into a duration value

   -----------------
   -- Local Types --
   -----------------

   --  An integer time duration. The type is used whenever a positive elapsed
   --  duration is needed, for instance when splitting a time value. Here is
   --  how Time_Rep and Time_Dur are related:

   --            'First  Ada_Low                  Ada_High  'Last
   --  Time_Rep: +-------+------------------------+---------+
   --  Time_Dur:         +------------------------+---------+
   --                    0                                  'Last

   type Time_Dur is range 0 .. 2 ** 63 - 1;

   --------------------------
   -- Leap seconds control --
   --------------------------

   Flag : Integer;
   pragma Import (C, Flag, "__gl_leap_seconds_support");
   --  This imported value is used to determine whether the compilation had
   --  binder flag "-y" present which enables leap seconds. A value of zero
   --  signifies no leap seconds support while a value of one enables the
   --  support.

   Leap_Support : constant Boolean := Flag = 1;
   --  The above flag controls the usage of leap seconds in all Ada.Calendar
   --  routines.

   Leap_Seconds_Count : constant Natural := 23;

   ---------------------
   -- Local Constants --
   ---------------------

   Ada_Min_Year          : constant Year_Number := Year_Number'First;
   Secs_In_Four_Years    : constant := (3 * 365 + 366) * Secs_In_Day;
   Secs_In_Non_Leap_Year : constant := 365 * Secs_In_Day;

   --  Lower and upper bound of Ada time. The zero (0) value of type Time is
   --  positioned at year 2150. Note that the lower and upper bound account
   --  for the non-leap centennial years.

   Ada_Low  : constant Time_Rep := -(61 * 366 + 188 * 365) * Nanos_In_Day;
   Ada_High : constant Time_Rep :=  (60 * 366 + 190 * 365) * Nanos_In_Day;

   --  Even though the upper bound of time is 2399-12-31 23:59:59.999999999
   --  UTC, it must be increased to include all leap seconds.

   Ada_High_And_Leaps : constant Time_Rep :=
                          Ada_High + Time_Rep (Leap_Seconds_Count) * Nano;

   --  Two constants used in the calculations of elapsed leap seconds.
   --  End_Of_Time is later than Ada_High in time zone -28. Start_Of_Time
   --  is earlier than Ada_Low in time zone +28.

   End_Of_Time   : constant Time_Rep :=
                     Ada_High + Time_Rep (3) * Nanos_In_Day;
   Start_Of_Time : constant Time_Rep :=
                     Ada_Low - Time_Rep (3) * Nanos_In_Day;

   --  The Unix lower time bound expressed as nanoseconds since the
   --  start of Ada time in UTC.

   Unix_Min : constant Time_Rep :=
                Ada_Low + Time_Rep (17 * 366 + 52 * 365) * Nanos_In_Day;

   Cumulative_Days_Before_Month :
     constant array (Month_Number) of Natural :=
       (0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334);

   --  The following table contains the hard time values of all existing leap
   --  seconds. The values are produced by the utility program xleaps.adb.

   Leap_Second_Times : constant array (1 .. Leap_Seconds_Count) of Time_Rep :=
     (-5601484800000000000,
      -5585587199000000000,
      -5554051198000000000,
      -5522515197000000000,
      -5490979196000000000,
      -5459356795000000000,
      -5427820794000000000,
      -5396284793000000000,
      -5364748792000000000,
      -5317487991000000000,
      -5285951990000000000,
      -5254415989000000000,
      -5191257588000000000,
      -5112287987000000000,
      -5049129586000000000,
      -5017593585000000000,
      -4970332784000000000,
      -4938796783000000000,
      -4907260782000000000,
      -4859827181000000000,
      -4812566380000000000,
      -4765132779000000000,
      -4544207978000000000);

   ---------
   -- "+" --
   ---------

   function "+" (Left : Time; Right : Duration) return Time is
      pragma Unsuppress (Overflow_Check);
      Left_N : constant Time_Rep := Time_Rep (Left);
   begin
      return Time (Left_N + Duration_To_Time_Rep (Right));
   exception
      when Constraint_Error =>
         raise Time_Error;
   end "+";

   function "+" (Left : Duration; Right : Time) return Time is
   begin
      return Right + Left;
   end "+";

   ---------
   -- "-" --
   ---------

   function "-" (Left : Time; Right : Duration) return Time is
      pragma Unsuppress (Overflow_Check);
      Left_N : constant Time_Rep := Time_Rep (Left);
   begin
      return Time (Left_N - Duration_To_Time_Rep (Right));
   exception
      when Constraint_Error =>
         raise Time_Error;
   end "-";

   function "-" (Left : Time; Right : Time) return Duration is
      pragma Unsuppress (Overflow_Check);

      --  The bounds of type Duration expressed as time representations

      Dur_Low  : constant Time_Rep := Duration_To_Time_Rep (Duration'First);
      Dur_High : constant Time_Rep := Duration_To_Time_Rep (Duration'Last);

      Res_N : Time_Rep;

   begin
      Res_N := Time_Rep (Left) - Time_Rep (Right);

      --  Due to the extended range of Ada time, "-" is capable of producing
      --  results which may exceed the range of Duration. In order to prevent
      --  the generation of bogus values by the Unchecked_Conversion, we apply
      --  the following check.

      if Res_N < Dur_Low
        or else Res_N > Dur_High
      then
         raise Time_Error;
      end if;

      return Time_Rep_To_Duration (Res_N);
   exception
      when Constraint_Error =>
         raise Time_Error;
   end "-";

   ---------
   -- "<" --
   ---------

   function "<" (Left, Right : Time) return Boolean is
   begin
      return Time_Rep (Left) < Time_Rep (Right);
   end "<";

   ----------
   -- "<=" --
   ----------

   function "<=" (Left, Right : Time) return Boolean is
   begin
      return Time_Rep (Left) <= Time_Rep (Right);
   end "<=";

   ---------
   -- ">" --
   ---------

   function ">" (Left, Right : Time) return Boolean is
   begin
      return Time_Rep (Left) > Time_Rep (Right);
   end ">";

   ----------
   -- ">=" --
   ----------

   function ">=" (Left, Right : Time) return Boolean is
   begin
      return Time_Rep (Left) >= Time_Rep (Right);
   end ">=";

   ------------------------------
   -- Check_Within_Time_Bounds --
   ------------------------------

   procedure Check_Within_Time_Bounds (T : Time_Rep) is
   begin
      if Leap_Support then
         if T < Ada_Low or else T > Ada_High_And_Leaps then
            raise Time_Error;
         end if;
      else
         if T < Ada_Low or else T > Ada_High then
            raise Time_Error;
         end if;
      end if;
   end Check_Within_Time_Bounds;

   -----------
   -- Clock --
   -----------

   function Clock return Time is
      Elapsed_Leaps : Natural;
      Next_Leap_N   : Time_Rep;

      --  The system clock returns the time in UTC since the Unix Epoch of
      --  1970-01-01 00:00:00.0. We perform an origin shift to the Ada Epoch
      --  by adding the number of nanoseconds between the two origins.

      Res_N : Time_Rep :=
                Duration_To_Time_Rep (System.OS_Primitives.Clock) +
                  Unix_Min;

   begin
      --  If the target supports leap seconds, determine the number of leap
      --  seconds elapsed until this moment.

      if Leap_Support then
         Cumulative_Leap_Seconds
           (Start_Of_Time, Res_N, Elapsed_Leaps, Next_Leap_N);

         --  The system clock may fall exactly on a leap second

         if Res_N >= Next_Leap_N then
            Elapsed_Leaps := Elapsed_Leaps + 1;
         end if;

      --  The target does not support leap seconds

      else
         Elapsed_Leaps := 0;
      end if;

      Res_N := Res_N + Time_Rep (Elapsed_Leaps) * Nano;

      return Time (Res_N);
   end Clock;

   -----------------------------
   -- Cumulative_Leap_Seconds --
   -----------------------------

   procedure Cumulative_Leap_Seconds
     (Start_Date    : Time_Rep;
      End_Date      : Time_Rep;
      Elapsed_Leaps : out Natural;
      Next_Leap     : out Time_Rep)
   is
      End_Index   : Positive;
      End_T       : Time_Rep := End_Date;
      Start_Index : Positive;
      Start_T     : Time_Rep := Start_Date;

   begin
      --  Both input dates must be normalized to UTC

      pragma Assert (Leap_Support and then End_Date >= Start_Date);

      Next_Leap := End_Of_Time;

      --  Make sure that the end date does not exceed the upper bound
      --  of Ada time.

      if End_Date > Ada_High then
         End_T := Ada_High;
      end if;

      --  Remove the sub seconds from both dates

      Start_T := Start_T - (Start_T mod Nano);
      End_T   := End_T   - (End_T   mod Nano);

      --  Some trivial cases:
      --                     Leap 1 . . . Leap N
      --  ---+========+------+############+-------+========+-----
      --     Start_T  End_T                       Start_T  End_T

      if End_T < Leap_Second_Times (1) then
         Elapsed_Leaps := 0;
         Next_Leap     := Leap_Second_Times (1);
         return;

      elsif Start_T > Leap_Second_Times (Leap_Seconds_Count) then
         Elapsed_Leaps := 0;
         Next_Leap     := End_Of_Time;
         return;
      end if;

      --  Perform the calculations only if the start date is within the leap
      --  second occurrences table.

      if Start_T <= Leap_Second_Times (Leap_Seconds_Count) then

         --    1    2                  N - 1   N
         --  +----+----+--  . . .  --+-------+---+
         --  | T1 | T2 |             | N - 1 | N |
         --  +----+----+--  . . .  --+-------+---+
         --         ^                   ^
         --         | Start_Index       | End_Index
         --         +-------------------+
         --             Leaps_Between

         --  The idea behind the algorithm is to iterate and find two
         --  closest dates which are after Start_T and End_T. Their
         --  corresponding index difference denotes the number of leap
         --  seconds elapsed.

         Start_Index := 1;
         loop
            exit when Leap_Second_Times (Start_Index) >= Start_T;
            Start_Index := Start_Index + 1;
         end loop;

         End_Index := Start_Index;
         loop
            exit when End_Index > Leap_Seconds_Count
              or else Leap_Second_Times (End_Index) >= End_T;
            End_Index := End_Index + 1;
         end loop;

         if End_Index <= Leap_Seconds_Count then
            Next_Leap := Leap_Second_Times (End_Index);
         end if;

         Elapsed_Leaps := End_Index - Start_Index;

      else
         Elapsed_Leaps := 0;
      end if;
   end Cumulative_Leap_Seconds;

   ---------
   -- Day --
   ---------

   function Day (Date : Time) return Day_Number is
      D : Day_Number;
      Y : Year_Number;
      M : Month_Number;
      S : Day_Duration;
      pragma Unreferenced (Y, M, S);
   begin
      Split (Date, Y, M, D, S);
      return D;
   end Day;

   -------------
   -- Is_Leap --
   -------------

   function Is_Leap (Year : Year_Number) return Boolean is
   begin
      --  Leap centennial years

      if Year mod 400 = 0 then
         return True;

      --  Non-leap centennial years

      elsif Year mod 100 = 0 then
         return False;

      --  Regular years

      else
         return Year mod 4 = 0;
      end if;
   end Is_Leap;

   -----------
   -- Month --
   -----------

   function Month (Date : Time) return Month_Number is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      S : Day_Duration;
      pragma Unreferenced (Y, D, S);
   begin
      Split (Date, Y, M, D, S);
      return M;
   end Month;

   -------------
   -- Seconds --
   -------------

   function Seconds (Date : Time) return Day_Duration is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      S : Day_Duration;
      pragma Unreferenced (Y, M, D);
   begin
      Split (Date, Y, M, D, S);
      return S;
   end Seconds;

   -----------
   -- Split --
   -----------

   procedure Split
     (Date    : Time;
      Year    : out Year_Number;
      Month   : out Month_Number;
      Day     : out Day_Number;
      Seconds : out Day_Duration)
   is
      H  : Integer;
      M  : Integer;
      Se : Integer;
      Ss : Duration;
      Le : Boolean;

      pragma Unreferenced (H, M, Se, Ss, Le);

   begin
      --  Even though the input time zone is UTC (0), the flag Is_Ada_05 will
      --  ensure that Split picks up the local time zone.

      Formatting_Operations.Split
        (Date      => Date,
         Year      => Year,
         Month     => Month,
         Day       => Day,
         Day_Secs  => Seconds,
         Hour      => H,
         Minute    => M,
         Second    => Se,
         Sub_Sec   => Ss,
         Leap_Sec  => Le,
         Is_Ada_05 => False,
         Time_Zone => 0);

      --  Validity checks

      if not Year'Valid
        or else not Month'Valid
        or else not Day'Valid
        or else not Seconds'Valid
      then
         raise Time_Error;
      end if;
   end Split;

   -------------
   -- Time_Of --
   -------------

   function Time_Of
     (Year    : Year_Number;
      Month   : Month_Number;
      Day     : Day_Number;
      Seconds : Day_Duration := 0.0) return Time
   is
      --  The values in the following constants are irrelevant, they are just
      --  placeholders; the choice of constructing a Day_Duration value is
      --  controlled by the Use_Day_Secs flag.

      H  : constant Integer := 1;
      M  : constant Integer := 1;
      Se : constant Integer := 1;
      Ss : constant Duration := 0.1;

   begin
      --  Validity checks

      if not Year'Valid
        or else not Month'Valid
        or else not Day'Valid
        or else not Seconds'Valid
      then
         raise Time_Error;
      end if;

      --  Even though the input time zone is UTC (0), the flag Is_Ada_05 will
      --  ensure that Split picks up the local time zone.

      return
        Formatting_Operations.Time_Of
          (Year         => Year,
           Month        => Month,
           Day          => Day,
           Day_Secs     => Seconds,
           Hour         => H,
           Minute       => M,
           Second       => Se,
           Sub_Sec      => Ss,
           Leap_Sec     => False,
           Use_Day_Secs => True,
           Is_Ada_05    => False,
           Time_Zone    => 0);
   end Time_Of;

   ----------
   -- Year --
   ----------

   function Year (Date : Time) return Year_Number is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      S : Day_Duration;
      pragma Unreferenced (M, D, S);
   begin
      Split (Date, Y, M, D, S);
      return Y;
   end Year;

   --  The following packages assume that Time is a signed 64 bit integer
   --  type, the units are nanoseconds and the origin is the start of Ada
   --  time (1901-01-01 00:00:00.0 UTC).

   ---------------------------
   -- Arithmetic_Operations --
   ---------------------------

   package body Arithmetic_Operations is

      ---------
      -- Add --
      ---------

      function Add (Date : Time; Days : Long_Integer) return Time is
         pragma Unsuppress (Overflow_Check);
         Date_N : constant Time_Rep := Time_Rep (Date);
      begin
         return Time (Date_N + Time_Rep (Days) * Nanos_In_Day);
      exception
         when Constraint_Error =>
            raise Time_Error;
      end Add;

      ----------------
      -- Difference --
      ----------------

      procedure Difference
        (Left         : Time;
         Right        : Time;
         Days         : out Long_Integer;
         Seconds      : out Duration;
         Leap_Seconds : out Integer)
      is
         Res_Dur       : Time_Dur;
         Earlier       : Time_Rep;
         Elapsed_Leaps : Natural;
         Later         : Time_Rep;
         Negate        : Boolean := False;
         Next_Leap_N   : Time_Rep;
         Sub_Secs      : Duration;
         Sub_Secs_Diff : Time_Rep;

      begin
         --  Both input time values are assumed to be in UTC

         if Left >= Right then
            Later   := Time_Rep (Left);
            Earlier := Time_Rep (Right);
         else
            Later   := Time_Rep (Right);
            Earlier := Time_Rep (Left);
            Negate  := True;
         end if;

         --  If the target supports leap seconds, process them

         if Leap_Support then
            Cumulative_Leap_Seconds
              (Earlier, Later, Elapsed_Leaps, Next_Leap_N);

            if Later >= Next_Leap_N then
               Elapsed_Leaps := Elapsed_Leaps + 1;
            end if;

         --  The target does not support leap seconds

         else
            Elapsed_Leaps := 0;
         end if;

         --  Sub seconds processing. We add the resulting difference to one
         --  of the input dates in order to account for any potential rounding
         --  of the difference in the next step.

         Sub_Secs_Diff := Later mod Nano - Earlier mod Nano;
         Earlier       := Earlier + Sub_Secs_Diff;
         Sub_Secs      := Duration (Sub_Secs_Diff) / Nano_F;

         --  Difference processing. This operation should be able to calculate
         --  the difference between opposite values which are close to the end
         --  and start of Ada time. To accommodate the large range, we convert
         --  to seconds. This action may potentially round the two values and
         --  either add or drop a second. We compensate for this issue in the
         --  previous step.

         Res_Dur :=
           Time_Dur (Later / Nano - Earlier / Nano) - Time_Dur (Elapsed_Leaps);

         Days         := Long_Integer (Res_Dur / Secs_In_Day);
         Seconds      := Duration (Res_Dur mod Secs_In_Day) + Sub_Secs;
         Leap_Seconds := Integer (Elapsed_Leaps);

         if Negate then
            Days    := -Days;
            Seconds := -Seconds;

            if Leap_Seconds /= 0 then
               Leap_Seconds := -Leap_Seconds;
            end if;
         end if;
      end Difference;

      --------------
      -- Subtract --
      --------------

      function Subtract (Date : Time; Days : Long_Integer) return Time is
         pragma Unsuppress (Overflow_Check);
         Date_N : constant Time_Rep := Time_Rep (Date);
      begin
         return Time (Date_N - Time_Rep (Days) * Nanos_In_Day);
      exception
         when Constraint_Error =>
            raise Time_Error;
      end Subtract;
   end Arithmetic_Operations;

   ----------------------
   -- Delay_Operations --
   ----------------------

   package body Delays_Operations is

      -----------------
      -- To_Duration --
      -----------------

      function To_Duration (Date : Time) return Duration is
         Elapsed_Leaps : Natural;
         Next_Leap_N   : Time_Rep;
         Res_N         : Time_Rep;

      begin
         Res_N := Time_Rep (Date);

         --  If the target supports leap seconds, remove any leap seconds
         --  elapsed up to the input date.

         if Leap_Support then
            Cumulative_Leap_Seconds
              (Start_Of_Time, Res_N, Elapsed_Leaps, Next_Leap_N);

            --  The input time value may fall on a leap second occurrence

            if Res_N >= Next_Leap_N then
               Elapsed_Leaps := Elapsed_Leaps + 1;
            end if;

         --  The target does not support leap seconds

         else
            Elapsed_Leaps := 0;
         end if;

         Res_N := Res_N - Time_Rep (Elapsed_Leaps) * Nano;

         --  Perform a shift in origins, note that enforcing type Time on
         --  both operands will invoke Ada.Calendar."-".

         return Time (Res_N) - Time (Unix_Min);
      end To_Duration;
   end Delays_Operations;

   ---------------------------
   -- Formatting_Operations --
   ---------------------------

   package body Formatting_Operations is

      -----------------
      -- Day_Of_Week --
      -----------------

      function Day_Of_Week (Date : Time) return Integer is
         Y  : Year_Number;
         Mo : Month_Number;
         D  : Day_Number;
         Ds : Day_Duration;
         H  : Integer;
         Mi : Integer;
         Se : Integer;
         Su : Duration;
         Le : Boolean;

         pragma Unreferenced (Ds, H, Mi, Se, Su, Le);

         Day_Count : Long_Integer;
         Res_Dur   : Time_Dur;
         Res_N     : Time_Rep;

      begin
         Formatting_Operations.Split
           (Date      => Date,
            Year      => Y,
            Month     => Mo,
            Day       => D,
            Day_Secs  => Ds,
            Hour      => H,
            Minute    => Mi,
            Second    => Se,
            Sub_Sec   => Su,
            Leap_Sec  => Le,
            Is_Ada_05 => True,
            Time_Zone => 0);

         --  Build a time value in the middle of the same day

         Res_N :=
           Time_Rep
             (Formatting_Operations.Time_Of
               (Year         => Y,
                Month        => Mo,
                Day          => D,
                Day_Secs     => 0.0,
                Hour         => 12,
                Minute       => 0,
                Second       => 0,
                Sub_Sec      => 0.0,
                Leap_Sec     => False,
                Use_Day_Secs => False,
                Is_Ada_05    => True,
                Time_Zone    => 0));

         --  Determine the elapsed seconds since the start of Ada time

         Res_Dur := Time_Dur (Res_N / Nano - Ada_Low / Nano);

         --  Count the number of days since the start of Ada time. 1901-1-1
         --  GMT was a Tuesday.

         Day_Count := Long_Integer (Res_Dur / Secs_In_Day) + 1;

         return Integer (Day_Count mod 7);
      end Day_Of_Week;

      -----------
      -- Split --
      -----------

      procedure Split
        (Date      : Time;
         Year      : out Year_Number;
         Month     : out Month_Number;
         Day       : out Day_Number;
         Day_Secs  : out Day_Duration;
         Hour      : out Integer;
         Minute    : out Integer;
         Second    : out Integer;
         Sub_Sec   : out Duration;
         Leap_Sec  : out Boolean;
         Is_Ada_05 : Boolean;
         Time_Zone : Long_Integer)
      is
         --  The following constants represent the number of nanoseconds
         --  elapsed since the start of Ada time to and including the non
         --  leap centennial years.

         Year_2101 : constant Time_Rep := Ada_Low +
                       Time_Rep (49 * 366 + 151 * 365) * Nanos_In_Day;
         Year_2201 : constant Time_Rep := Ada_Low +
                       Time_Rep (73 * 366 + 227 * 365) * Nanos_In_Day;
         Year_2301 : constant Time_Rep := Ada_Low +
                       Time_Rep (97 * 366 + 303 * 365) * Nanos_In_Day;

         Date_Dur       : Time_Dur;
         Date_N         : Time_Rep;
         Day_Seconds    : Natural;
         Elapsed_Leaps  : Natural;
         Four_Year_Segs : Natural;
         Hour_Seconds   : Natural;
         Is_Leap_Year   : Boolean;
         Next_Leap_N    : Time_Rep;
         Rem_Years      : Natural;
         Sub_Sec_N      : Time_Rep;
         Year_Day       : Natural;

      begin
         Date_N := Time_Rep (Date);

         --  Step 1: Leap seconds processing in UTC

         if Leap_Support then
            Cumulative_Leap_Seconds
              (Start_Of_Time, Date_N, Elapsed_Leaps, Next_Leap_N);

            Leap_Sec := Date_N >= Next_Leap_N;

            if Leap_Sec then
               Elapsed_Leaps := Elapsed_Leaps + 1;
            end if;

         --  The target does not support leap seconds

         else
            Elapsed_Leaps := 0;
            Leap_Sec      := False;
         end if;

         Date_N := Date_N - Time_Rep (Elapsed_Leaps) * Nano;

         --  Step 2: Time zone processing. This action converts the input date
         --  from GMT to the requested time zone.

         if Is_Ada_05 then
            if Time_Zone /= 0 then
               Date_N := Date_N + Time_Rep (Time_Zone) * 60 * Nano;
            end if;

         --  Ada 83 and 95

         else
            declare
               Off : constant Long_Integer :=
                       Time_Zones_Operations.UTC_Time_Offset (Time (Date_N));
            begin
               Date_N := Date_N + Time_Rep (Off) * Nano;
            end;
         end if;

         --  Step 3: Non-leap centennial year adjustment in local time zone

         --  In order for all divisions to work properly and to avoid more
         --  complicated arithmetic, we add fake February 29s to dates which
         --  occur after a non-leap centennial year.

         if Date_N >= Year_2301 then
            Date_N := Date_N + Time_Rep (3) * Nanos_In_Day;

         elsif Date_N >= Year_2201 then
            Date_N := Date_N + Time_Rep (2) * Nanos_In_Day;

         elsif Date_N >= Year_2101 then
            Date_N := Date_N + Time_Rep (1) * Nanos_In_Day;
         end if;

         --  Step 4: Sub second processing in local time zone

         Sub_Sec_N := Date_N mod Nano;
         Sub_Sec   := Duration (Sub_Sec_N) / Nano_F;
         Date_N    := Date_N - Sub_Sec_N;

         --  Convert Date_N into a time duration value, changing the units
         --  to seconds.

         Date_Dur := Time_Dur (Date_N / Nano - Ada_Low / Nano);

         --  Step 5: Year processing in local time zone. Determine the number
         --  of four year segments since the start of Ada time and the input
         --  date.

         Four_Year_Segs := Natural (Date_Dur / Secs_In_Four_Years);

         if Four_Year_Segs > 0 then
            Date_Dur := Date_Dur - Time_Dur (Four_Year_Segs) *
                                   Secs_In_Four_Years;
         end if;

         --  Calculate the remaining non-leap years

         Rem_Years := Natural (Date_Dur / Secs_In_Non_Leap_Year);

         if Rem_Years > 3 then
            Rem_Years := 3;
         end if;

         Date_Dur := Date_Dur - Time_Dur (Rem_Years) * Secs_In_Non_Leap_Year;

         Year := Ada_Min_Year + Natural (4 * Four_Year_Segs + Rem_Years);
         Is_Leap_Year := Is_Leap (Year);

         --  Step 6: Month and day processing in local time zone

         Year_Day := Natural (Date_Dur / Secs_In_Day) + 1;

         Month := 1;

         --  Processing for months after January

         if Year_Day > 31 then
            Month    := 2;
            Year_Day := Year_Day - 31;

            --  Processing for a new month or a leap February

            if Year_Day > 28
              and then (not Is_Leap_Year or else Year_Day > 29)
            then
               Month    := 3;
               Year_Day := Year_Day - 28;

               if Is_Leap_Year then
                  Year_Day := Year_Day - 1;
               end if;

               --  Remaining months

               while Year_Day > Days_In_Month (Month) loop
                  Year_Day := Year_Day - Days_In_Month (Month);
                  Month    := Month + 1;
               end loop;
            end if;
         end if;

         --  Step 7: Hour, minute, second and sub second processing in local
         --  time zone.

         Day          := Day_Number (Year_Day);
         Day_Seconds  := Integer (Date_Dur mod Secs_In_Day);
         Day_Secs     := Duration (Day_Seconds) + Sub_Sec;
         Hour         := Day_Seconds / 3_600;
         Hour_Seconds := Day_Seconds mod 3_600;
         Minute       := Hour_Seconds / 60;
         Second       := Hour_Seconds mod 60;
      end Split;

      -------------
      -- Time_Of --
      -------------

      function Time_Of
        (Year         : Year_Number;
         Month        : Month_Number;
         Day          : Day_Number;
         Day_Secs     : Day_Duration;
         Hour         : Integer;
         Minute       : Integer;
         Second       : Integer;
         Sub_Sec      : Duration;
         Leap_Sec     : Boolean;
         Use_Day_Secs : Boolean;
         Is_Ada_05    : Boolean;
         Time_Zone    : Long_Integer) return Time
      is
         Count         : Integer;
         Elapsed_Leaps : Natural;
         Next_Leap_N   : Time_Rep;
         Res_N         : Time_Rep;
         Rounded_Res_N : Time_Rep;

      begin
         --  Step 1: Check whether the day, month and year form a valid date

         if Day > Days_In_Month (Month)
           and then (Day /= 29 or else Month /= 2 or else not Is_Leap (Year))
         then
            raise Time_Error;
         end if;

         --  Start accumulating nanoseconds from the low bound of Ada time

         Res_N := Ada_Low;

         --  Step 2: Year processing and centennial year adjustment. Determine
         --  the number of four year segments since the start of Ada time and
         --  the input date.

         Count := (Year - Year_Number'First) / 4;
         Res_N := Res_N + Time_Rep (Count) * Secs_In_Four_Years * Nano;

         --  Note that non-leap centennial years are automatically considered
         --  leap in the operation above. An adjustment of several days is
         --  required to compensate for this.

         if Year > 2300 then
            Res_N := Res_N - Time_Rep (3) * Nanos_In_Day;

         elsif Year > 2200 then
            Res_N := Res_N - Time_Rep (2) * Nanos_In_Day;

         elsif Year > 2100 then
            Res_N := Res_N - Time_Rep (1) * Nanos_In_Day;
         end if;

         --  Add the remaining non-leap years

         Count := (Year - Year_Number'First) mod 4;
         Res_N := Res_N + Time_Rep (Count) * Secs_In_Non_Leap_Year * Nano;

         --  Step 3: Day of month processing. Determine the number of days
         --  since the start of the current year. Do not add the current
         --  day since it has not elapsed yet.

         Count := Cumulative_Days_Before_Month (Month) + Day - 1;

         --  The input year is leap and we have passed February

         if Is_Leap (Year)
           and then Month > 2
         then
            Count := Count + 1;
         end if;

         Res_N := Res_N + Time_Rep (Count) * Nanos_In_Day;

         --  Step 4: Hour, minute, second and sub second processing

         if Use_Day_Secs then
            Res_N := Res_N + Duration_To_Time_Rep (Day_Secs);

         else
            Res_N := Res_N +
              Time_Rep (Hour * 3_600 + Minute * 60 + Second) * Nano;

            if Sub_Sec = 1.0 then
               Res_N := Res_N + Time_Rep (1) * Nano;
            else
               Res_N := Res_N + Duration_To_Time_Rep (Sub_Sec);
            end if;
         end if;

         --  At this point, the generated time value should be withing the
         --  bounds of Ada time.

         Check_Within_Time_Bounds (Res_N);

         --  Step 4: Time zone processing. At this point we have built an
         --  arbitrary time value which is not related to any time zone.
         --  For simplicity, the time value is normalized to GMT, producing
         --  a uniform representation which can be treated by arithmetic
         --  operations for instance without any additional corrections.

         if Is_Ada_05 then
            if Time_Zone /= 0 then
               Res_N := Res_N - Time_Rep (Time_Zone) * 60 * Nano;
            end if;

         --  Ada 83 and 95

         else
            declare
               Current_Off   : constant Long_Integer :=
                                 Time_Zones_Operations.UTC_Time_Offset
                                   (Time (Res_N));
               Current_Res_N : constant Time_Rep :=
                                 Res_N - Time_Rep (Current_Off) * Nano;
               Off           : constant Long_Integer :=
                                 Time_Zones_Operations.UTC_Time_Offset
                                   (Time (Current_Res_N));
            begin
               Res_N := Res_N - Time_Rep (Off) * Nano;
            end;
         end if;

         --  Step 5: Leap seconds processing in GMT

         if Leap_Support then
            Cumulative_Leap_Seconds
              (Start_Of_Time, Res_N, Elapsed_Leaps, Next_Leap_N);

            Res_N := Res_N + Time_Rep (Elapsed_Leaps) * Nano;

            --  An Ada 2005 caller requesting an explicit leap second or an
            --  Ada 95 caller accounting for an invisible leap second.

            if Leap_Sec
              or else Res_N >= Next_Leap_N
            then
               Res_N := Res_N + Time_Rep (1) * Nano;
            end if;

            --  Leap second validity check

            Rounded_Res_N := Res_N - (Res_N mod Nano);

            if Is_Ada_05
              and then Leap_Sec
              and then Rounded_Res_N /= Next_Leap_N
            then
               raise Time_Error;
            end if;
         end if;

         return Time (Res_N);
      end Time_Of;
   end Formatting_Operations;

   ---------------------------
   -- Time_Zones_Operations --
   ---------------------------

   package body Time_Zones_Operations is

      --  The Unix time bounds in nanoseconds: 1970/1/1 .. 2037/1/1

      Unix_Min : constant Time_Rep := Ada_Low +
                   Time_Rep (17 * 366 +  52 * 365) * Nanos_In_Day;

      Unix_Max : constant Time_Rep := Ada_Low +
                   Time_Rep (34 * 366 + 102 * 365) * Nanos_In_Day +
                   Time_Rep (Leap_Seconds_Count) * Nano;

      --  The following constants denote February 28 during non-leap
      --  centennial years, the units are nanoseconds.

      T_2100_2_28 : constant Time_Rep := Ada_Low +
                      (Time_Rep (49 * 366 + 150 * 365 + 59) * Secs_In_Day +
                       Time_Rep (Leap_Seconds_Count)) * Nano;

      T_2200_2_28 : constant Time_Rep := Ada_Low +
                      (Time_Rep (73 * 366 + 226 * 365 + 59) * Secs_In_Day +
                       Time_Rep (Leap_Seconds_Count)) * Nano;

      T_2300_2_28 : constant Time_Rep := Ada_Low +
                      (Time_Rep (97 * 366 + 302 * 365 + 59) * Secs_In_Day +
                       Time_Rep (Leap_Seconds_Count)) * Nano;

      --  56 years (14 leap years + 42 non leap years) in nanoseconds:

      Nanos_In_56_Years : constant := (14 * 366 + 42 * 365) * Nanos_In_Day;

      --  Base C types. There is no point dragging in Interfaces.C just for
      --  these four types.

      type char_Pointer is access Character;
      subtype int is Integer;
      subtype long is Long_Integer;
      type long_Pointer is access all long;

      --  The Ada equivalent of struct tm and type time_t

      type tm is record
         tm_sec    : int;           --  seconds after the minute (0 .. 60)
         tm_min    : int;           --  minutes after the hour (0 .. 59)
         tm_hour   : int;           --  hours since midnight (0 .. 24)
         tm_mday   : int;           --  day of the month (1 .. 31)
         tm_mon    : int;           --  months since January (0 .. 11)
         tm_year   : int;           --  years since 1900
         tm_wday   : int;           --  days since Sunday (0 .. 6)
         tm_yday   : int;           --  days since January 1 (0 .. 365)
         tm_isdst  : int;           --  Daylight Savings Time flag (-1 .. 1)
         tm_gmtoff : long;          --  offset from UTC in seconds
         tm_zone   : char_Pointer;  --  timezone abbreviation
      end record;

      type tm_Pointer is access all tm;

      subtype time_t is long;
      type time_t_Pointer is access all time_t;

      procedure localtime_tzoff
       (C   : time_t_Pointer;
        res : tm_Pointer;
        off : long_Pointer);
      pragma Import (C, localtime_tzoff, "__gnat_localtime_tzoff");
      --  This is a lightweight wrapper around the system library function
      --  localtime_r. Parameter 'off' captures the UTC offset which is either
      --  retrieved from the tm struct or calculated from the 'timezone' extern
      --  and the tm_isdst flag in the tm struct.

      ---------------------
      -- UTC_Time_Offset --
      ---------------------

      function UTC_Time_Offset (Date : Time) return Long_Integer is
         Adj_Cent : Integer := 0;
         Date_N   : Time_Rep;
         Offset   : aliased long;
         Secs_T   : aliased time_t;
         Secs_TM  : aliased tm;

      begin
         Date_N := Time_Rep (Date);

         --  Dates which are 56 years apart fall on the same day, day light
         --  saving and so on. Non-leap centennial years violate this rule by
         --  one day and as a consequence, special adjustment is needed.

         if Date_N > T_2100_2_28 then
            if Date_N > T_2200_2_28 then
               if Date_N > T_2300_2_28 then
                  Adj_Cent := 3;
               else
                  Adj_Cent := 2;
               end if;

            else
               Adj_Cent := 1;
            end if;
         end if;

         if Adj_Cent > 0 then
            Date_N := Date_N - Time_Rep (Adj_Cent) * Nanos_In_Day;
         end if;

         --  Shift the date within bounds of Unix time

         while Date_N < Unix_Min loop
            Date_N := Date_N + Nanos_In_56_Years;
         end loop;

         while Date_N >= Unix_Max loop
            Date_N := Date_N - Nanos_In_56_Years;
         end loop;

         --  Perform a shift in origins from Ada to Unix

         Date_N := Date_N - Unix_Min;

         --  Convert the date into seconds

         Secs_T := time_t (Date_N / Nano);

         localtime_tzoff
           (Secs_T'Unchecked_Access,
            Secs_TM'Unchecked_Access,
            Offset'Unchecked_Access);

         return Offset;
      end UTC_Time_Offset;
   end Time_Zones_Operations;

--  Start of elaboration code for Ada.Calendar

begin
   System.OS_Primitives.Initialize;
end Ada.Calendar;
