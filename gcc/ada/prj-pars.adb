------------------------------------------------------------------------------
--                                                                          --
--                         GNAT COMPILER COMPONENTS                         --
--                                                                          --
--                             P R J . P A R S                              --
--                                                                          --
--                                 B o d y                                  --
--                                                                          --
--          Copyright (C) 2001-2009, Free Software Foundation, Inc.         --
--                                                                          --
-- GNAT is free software;  you can  redistribute it  and/or modify it under --
-- terms of the  GNU General Public License as published  by the Free Soft- --
-- ware  Foundation;  either version 3,  or (at your option) any later ver- --
-- sion.  GNAT is distributed in the hope that it will be useful, but WITH- --
-- OUT ANY WARRANTY;  without even the  implied warranty of MERCHANTABILITY --
-- or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License --
-- for  more details.  You should have  received  a copy of the GNU General --
-- Public License  distributed with GNAT; see file COPYING3.  If not, go to --
-- http://www.gnu.org/licenses for a complete copy of the license.          --
--                                                                          --
-- GNAT was originally developed  by the GNAT team at  New York University. --
-- Extensive contributions were provided by Ada Core Technologies Inc.      --
--                                                                          --
------------------------------------------------------------------------------

with Ada.Exceptions; use Ada.Exceptions;
with GNAT.Directory_Operations; use GNAT.Directory_Operations;

with Output;   use Output;
with Prj.Err;  use Prj.Err;
with Prj.Part;
with Prj.Proc;
with Prj.Tree; use Prj.Tree;
with Sinput.P;

package body Prj.Pars is

   -----------
   -- Parse --
   -----------

   procedure Parse
     (In_Tree           : Project_Tree_Ref;
      Project           : out Project_Id;
      Project_File_Name : String;
      Packages_To_Check : String_List_Access := All_Packages;
      When_No_Sources   : Error_Warning := Error;
      Report_Error      : Put_Line_Access := null;
      Reset_Tree        : Boolean := True;
      Is_Config_File    : Boolean := False)
   is
      Project_Node      : Project_Node_Id := Empty_Node;
      The_Project       : Project_Id      := No_Project;
      Success           : Boolean         := True;
      Current_Dir       : constant String := Get_Current_Dir;
      Project_Node_Tree : Prj.Tree.Project_Node_Tree_Ref;

   begin
      Project_Node_Tree := new Project_Node_Tree_Data;
      Prj.Tree.Initialize (Project_Node_Tree);

      --  Parse the main project file into a tree

      Sinput.P.Reset_First;
      Prj.Part.Parse
        (In_Tree                => Project_Node_Tree,
         Project                => Project_Node,
         Project_File_Name      => Project_File_Name,
         Always_Errout_Finalize => False,
         Packages_To_Check      => Packages_To_Check,
         Current_Directory      => Current_Dir,
         Is_Config_File         => Is_Config_File);

      --  If there were no error, process the tree

      if Project_Node /= Empty_Node then
         Prj.Proc.Process
           (In_Tree                => In_Tree,
            Project                => The_Project,
            Success                => Success,
            From_Project_Node      => Project_Node,
            From_Project_Node_Tree => Project_Node_Tree,
            Report_Error           => Report_Error,
            Reset_Tree             => Reset_Tree,
            When_No_Sources        => When_No_Sources,
            Current_Dir            => Current_Dir,
            Is_Config_File         => Is_Config_File);

         Prj.Err.Finalize;

         if not Success then
            The_Project := No_Project;
         end if;
      end if;

      Project := The_Project;

      --  ??? Should free the project_node_tree, no longer useful

   exception
      when X : others =>

         --  Internal error

         Write_Line (Exception_Information (X));
         Write_Str  ("Exception ");
         Write_Str  (Exception_Name (X));
         Write_Line (" raised, while processing project file");
         Project := No_Project;
   end Parse;

   -------------------
   -- Set_Verbosity --
   -------------------

   procedure Set_Verbosity (To : Verbosity) is
   begin
      Current_Verbosity := To;
   end Set_Verbosity;

end Prj.Pars;
