------------------------------------------------------------------------------
--                                                                          --
--                         GNAT COMPILER COMPONENTS                         --
--                                                                          --
--                             A L I . U T I L                              --
--                                                                          --
--                                 B o d y                                  --
--                                                                          --
--          Copyright (C) 1992-2006, Free Software Foundation, Inc.         --
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
-- GNAT was originally developed  by the GNAT team at  New York University. --
-- Extensive contributions were provided by Ada Core Technologies Inc.      --
--                                                                          --
------------------------------------------------------------------------------

with Debug;   use Debug;
with Binderr; use Binderr;
with Lib;     use Lib;
with Namet;   use Namet;
with Opt;     use Opt;
with Output;  use Output;
with Osint;   use Osint;
with Scans;   use Scans;
with Scng;
with Sinput.C;
with Snames;  use Snames;
with Styleg;

package body ALI.Util is

   --  Empty procedures needed to instantiate Scng. Error procedures are
   --  empty, because we don't want to report any errors when computing
   --  a source checksum.

   procedure Post_Scan;

   procedure Error_Msg (Msg : String; Flag_Location : Source_Ptr);

   procedure Error_Msg_S (Msg : String);

   procedure Error_Msg_SC (Msg : String);

   procedure Error_Msg_SP (Msg : String);

   procedure Obsolescent_Check (S : Source_Ptr);

   --  Instantiation of Styleg, needed to instantiate Scng

   package Style is new Styleg
     (Error_Msg, Error_Msg_S, Error_Msg_SC, Error_Msg_SP);

   --  A Scanner is needed to get checksum of a source (procedure
   --  Get_File_Checksum).

   package Scanner is new Scng
     (Post_Scan, Error_Msg, Error_Msg_S, Error_Msg_SC, Error_Msg_SP,
      Obsolescent_Check, Style);

   type Header_Num is range 0 .. 1_000;

   function Hash (F : File_Name_Type) return Header_Num;
   --  Function used to compute hash of ALI file name

   package Interfaces is new Simple_HTable (
     Header_Num => Header_Num,
     Element    => Boolean,
     No_Element => False,
     Key        => File_Name_Type,
     Hash       => Hash,
     Equal      => "=");

   ---------------------
   -- Checksums_Match --
   ---------------------

   function Checksums_Match (Checksum1, Checksum2 : Word) return Boolean is
   begin
      return Checksum1 = Checksum2 and then Checksum1 /= Checksum_Error;
   end Checksums_Match;

   ---------------
   -- Error_Msg --
   ---------------

   procedure Error_Msg (Msg : String; Flag_Location : Source_Ptr) is
      pragma Warnings (Off, Msg);
      pragma Warnings (Off, Flag_Location);
   begin
      null;
   end Error_Msg;

   -----------------
   -- Error_Msg_S --
   -----------------

   procedure Error_Msg_S (Msg : String) is
      pragma Warnings (Off, Msg);
   begin
      null;
   end Error_Msg_S;

   ------------------
   -- Error_Msg_SC --
   ------------------

   procedure Error_Msg_SC (Msg : String) is
      pragma Warnings (Off, Msg);
   begin
      null;
   end Error_Msg_SC;

   ------------------
   -- Error_Msg_SP --
   ------------------

   procedure Error_Msg_SP (Msg : String) is
      pragma Warnings (Off, Msg);
   begin
      null;
   end Error_Msg_SP;

   -----------------------
   -- Get_File_Checksum --
   -----------------------

   function Get_File_Checksum (Fname : Name_Id) return Word is
      Full_Name    : Name_Id;
      Source_Index : Source_File_Index;

   begin
      Full_Name := Find_File (Fname, Osint.Source);

      --  If we cannot find the file, then return an impossible checksum,
      --  impossible becaues checksums have the high order bit zero, so
      --  that checksums do not match.

      if Full_Name = No_File then
         return Checksum_Error;
      end if;

      Source_Index := Sinput.C.Load_File (Get_Name_String (Full_Name));

      if Source_Index = No_Source_File then
         return Checksum_Error;
      end if;

      Scanner.Initialize_Scanner (Source_Index);

      --  Make sure that the project language reserved words are not
      --  recognized as reserved words, but as identifiers. The byte info for
      --  those names have been set if we are in gnatmake.

      Set_Name_Table_Byte (Name_Project,  0);
      Set_Name_Table_Byte (Name_Extends,  0);
      Set_Name_Table_Byte (Name_External, 0);

      --  Scan the complete file to compute its checksum

      loop
         Scanner.Scan;
         exit when Token = Tok_EOF;
      end loop;

      return Scans.Checksum;
   end Get_File_Checksum;

   ----------
   -- Hash --
   ----------

   function Hash (F : File_Name_Type) return Header_Num is
   begin
      return Header_Num (Int (F) rem Header_Num'Range_Length);
   end Hash;

   ---------------------------
   -- Initialize_ALI_Source --
   ---------------------------

   procedure Initialize_ALI_Source is
   begin
      --  When (re)initializing ALI data structures the ALI user expects to
      --  get a fresh set of data structures. Thus we first need to erase the
      --  marks put in the name table by the previous set of ALI routine calls.
      --  This loop is empty and harmless the first time in.

      for J in Source.First .. Source.Last loop
         Set_Name_Table_Info (Source.Table (J).Sfile, 0);
         Source.Table (J).Source_Found := False;
      end loop;

      Source.Init;
      Interfaces.Reset;
   end Initialize_ALI_Source;

   -----------------------
   -- Obsolescent_Check --
   -----------------------

   procedure Obsolescent_Check (S : Source_Ptr) is
      pragma Warnings (Off, S);
   begin
      null;
   end Obsolescent_Check;

   ---------------
   -- Post_Scan --
   ---------------

   procedure Post_Scan is
   begin
      null;
   end Post_Scan;

   --------------
   -- Read_ALI --
   --------------

   procedure Read_ALI (Id : ALI_Id) is
      Afile  : File_Name_Type;
      Text   : Text_Buffer_Ptr;
      Idread : ALI_Id;

   begin
      --  Process all dependent units

      for U in ALIs.Table (Id).First_Unit .. ALIs.Table (Id).Last_Unit loop
         for
           W in Units.Table (U).First_With .. Units.Table (U).Last_With
         loop
            Afile := Withs.Table (W).Afile;

            --  Only process if not a generic (Afile /= No_File) and if
            --  file has not been processed already.

            if Afile /= No_File
              and then Get_Name_Table_Info (Afile) = 0
            then
               Text := Read_Library_Info (Afile);

               --  Return with an error if source cannot be found and if this
               --  is not a library generic (now we can, but does not have to
               --  compile library generics)

               if Text = null then
                  if Generic_Separately_Compiled (Withs.Table (W).Sfile) then
                     Error_Msg_Name_1 := Afile;
                     Error_Msg_Name_2 := Withs.Table (W).Sfile;
                     Error_Msg ("% not found, % must be compiled");
                     Set_Name_Table_Info (Afile, Int (No_Unit_Id));
                     return;

                  else
                     goto Skip_Library_Generics;
                  end if;
               end if;

               --  Enter in ALIs table

               Idread :=
                 Scan_ALI
                   (F         => Afile,
                    T         => Text,
                    Ignore_ED => False,
                    Err       => False);

               Free (Text);

               if ALIs.Table (Idread).Compile_Errors then
                  Error_Msg_Name_1 := Withs.Table (W).Sfile;
                  Error_Msg ("% had errors, must be fixed, and recompiled");
                  Set_Name_Table_Info (Afile, Int (No_Unit_Id));

               elsif ALIs.Table (Idread).No_Object then
                  Error_Msg_Name_1 := Withs.Table (W).Sfile;
                  Error_Msg ("% must be recompiled");
                  Set_Name_Table_Info (Afile, Int (No_Unit_Id));
               end if;

               --  If the Unit is an Interface to a Stand-Alone Library,
               --  set the Interface flag in the Withs table, so that its
               --  dependant are not considered for elaboration order.

               if ALIs.Table (Idread).SAL_Interface then
                  Withs.Table (W).SAL_Interface  := True;
                  Interface_Library_Unit := True;

                  --  Set the entry in the Interfaces hash table, so that other
                  --  units that import this unit will set the flag in their
                  --  entry in the Withs table.

                  Interfaces.Set (Afile, True);

               else
                  --  Otherwise, recurse to get new dependents

                  Read_ALI (Idread);
               end if;

               <<Skip_Library_Generics>> null;

            --  If the ALI file has already been processed and is an interface,
            --  set the flag in the entry of the Withs table.

            elsif Interface_Library_Unit and then Interfaces.Get (Afile) then
               Withs.Table (W).SAL_Interface := True;
            end if;
         end loop;
      end loop;
   end Read_ALI;

   ----------------------
   -- Set_Source_Table --
   ----------------------

   procedure Set_Source_Table (A : ALI_Id) is
      F     : File_Name_Type;
      S     : Source_Id;
      Stamp : Time_Stamp_Type;

   begin
      Sdep_Loop : for D in
        ALIs.Table (A).First_Sdep .. ALIs.Table (A).Last_Sdep
      loop
         F := Sdep.Table (D).Sfile;

         if F /= No_Name then

            --  If this is the first time we are seeing this source file,
            --  then make a new entry in the source table.

            if Get_Name_Table_Info (F) = 0 then
               Source.Increment_Last;
               S := Source.Last;
               Set_Name_Table_Info (F, Int (S));
               Source.Table (S).Sfile := F;
               Source.Table (S).All_Timestamps_Match := True;

               --  Initialize checksum fields

               Source.Table (S).Checksum := Sdep.Table (D).Checksum;
               Source.Table (S).All_Checksums_Match := True;

               --  In check source files mode, try to get time stamp from file

               if Opt.Check_Source_Files then
                  Stamp := Source_File_Stamp (F);

                  --  If we got the stamp, then set the stamp in the source
                  --  table entry and mark it as set from the source so that
                  --  it does not get subsequently changed.

                  if Stamp (Stamp'First) /= ' ' then
                     Source.Table (S).Stamp := Stamp;
                     Source.Table (S).Source_Found := True;

                  --  If we could not find the file, then the stamp is set
                  --  from the dependency table entry (to be possibly reset
                  --  if we find a later stamp in subsequent processing)

                  else
                     Source.Table (S).Stamp := Sdep.Table (D).Stamp;
                     Source.Table (S).Source_Found := False;

                     --  In All_Sources mode, flag error of file not found

                     if Opt.All_Sources then
                        Error_Msg_Name_1 := F;
                        Error_Msg ("cannot locate %");
                     end if;
                  end if;

               --  First time for this source file, but Check_Source_Files
               --  is off, so simply initialize the stamp from the Sdep entry

               else
                  Source.Table (S).Source_Found := False;
                  Source.Table (S).Stamp := Sdep.Table (D).Stamp;
               end if;

            --  Here if this is not the first time for this source file,
            --  so that the source table entry is already constructed.

            else
               S := Source_Id (Get_Name_Table_Info (F));

               --  Update checksum flag

               if not Checksums_Match
                        (Sdep.Table (D).Checksum, Source.Table (S).Checksum)
               then
                  Source.Table (S).All_Checksums_Match := False;
               end if;

               --  Check for time stamp mismatch

               if Sdep.Table (D).Stamp /= Source.Table (S).Stamp then
                  Source.Table (S).All_Timestamps_Match := False;

                  --  When we have a time stamp mismatch, we go look for the
                  --  source file even if Check_Source_Files is false, since
                  --  if we find it, then we can use it to resolve which of the
                  --  two timestamps in the ALI files is likely to be correct.

                  if not Check_Source_Files then
                     Stamp := Source_File_Stamp (F);

                     if Stamp (Stamp'First) /= ' ' then
                        Source.Table (S).Stamp := Stamp;
                        Source.Table (S).Source_Found := True;
                     end if;
                  end if;

                  --  If the stamp in the source table entry was set from the
                  --  source file, then we do not change it (the stamp in the
                  --  source file is always taken as the "right" one).

                  if Source.Table (S).Source_Found then
                     null;

                  --  Otherwise, we have no source file available, so we guess
                  --  that the later of the two timestamps is the right one.
                  --  Note that this guess only affects which error messages
                  --  are issued later on, not correct functionality.

                  else
                     if Sdep.Table (D).Stamp > Source.Table (S).Stamp then
                        Source.Table (S).Stamp := Sdep.Table (D).Stamp;
                     end if;
                  end if;
               end if;
            end if;

            --  Set the checksum value in the source table

            S := Source_Id (Get_Name_Table_Info (F));
            Source.Table (S).Checksum := Sdep.Table (D).Checksum;
         end if;

      end loop Sdep_Loop;
   end Set_Source_Table;

   ----------------------
   -- Set_Source_Table --
   ----------------------

   procedure Set_Source_Table is
   begin
      for A in ALIs.First .. ALIs.Last loop
         Set_Source_Table (A);
      end loop;
   end Set_Source_Table;

   -------------------------
   -- Time_Stamp_Mismatch --
   -------------------------

   function Time_Stamp_Mismatch
     (A         : ALI_Id;
      Read_Only : Boolean := False)
      return      File_Name_Type
   is
      Src : Source_Id;
      --  Source file Id for the current Sdep entry

   begin
      for D in ALIs.Table (A).First_Sdep .. ALIs.Table (A).Last_Sdep loop
         Src := Source_Id (Get_Name_Table_Info (Sdep.Table (D).Sfile));

         if Opt.Minimal_Recompilation
           and then Sdep.Table (D).Stamp /= Source.Table (Src).Stamp
         then
            --  If minimal recompilation is in action, replace the stamp
            --  of the source file in the table if checksums match.

            --  ??? It is probably worth updating the ALI file with a new
            --  field to avoid recomputing it each time.

            if Checksums_Match
                 (Get_File_Checksum (Sdep.Table (D).Sfile),
                  Source.Table (Src).Checksum)
            then
               Sdep.Table (D).Stamp := Source.Table (Src).Stamp;
            end if;

         end if;

         if (not Read_Only) or else Source.Table (Src).Source_Found then
            if not Source.Table (Src).Source_Found
              or else Sdep.Table (D).Stamp /= Source.Table (Src).Stamp
            then
               --  If -t debug flag set, output time stamp found/expected

               if Source.Table (Src).Source_Found and Debug_Flag_T then
                  Write_Str ("Source: """);
                  Get_Name_String (Sdep.Table (D).Sfile);
                  Write_Str (Name_Buffer (1 .. Name_Len));
                  Write_Line ("""");

                  Write_Str ("   time stamp expected: ");
                  Write_Line (String (Sdep.Table (D).Stamp));

                  Write_Str ("      time stamp found: ");
                  Write_Line (String (Source.Table (Src).Stamp));
               end if;

               --  Return the source file

               return Source.Table (Src).Sfile;
            end if;
         end if;
      end loop;

      return No_File;
   end Time_Stamp_Mismatch;

end ALI.Util;
