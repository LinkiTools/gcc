------------------------------------------------------------------------------
--                                                                          --
--                         GNAT COMPILER COMPONENTS                         --
--                                                                          --
--              S Y S T E M . S T A N D A R D _ L I B R A R Y               --
--                                                                          --
--                                 B o d y                                  --
--                                                                          --
--          Copyright (C) 1995-2003 Free Software Foundation, Inc.          --
--                                                                          --
-- GNAT is free software;  you can  redistribute it  and/or modify it under --
-- terms of the  GNU General Public License as published  by the Free Soft- --
-- ware  Foundation;  either version 2,  or (at your option) any later ver- --
-- sion.  GNAT is distributed in the hope that it will be useful, but WITH- --
-- OUT ANY WARRANTY;  without even the  implied warranty of MERCHANTABILITY --
-- or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License --
-- for  more details.  You should have  received  a copy of the GNU General --
-- Public License  distributed with GNAT;  see file COPYING.  If not, write --
-- to  the Free Software Foundation,  59 Temple Place - Suite 330,  Boston, --
-- MA 02111-1307, USA.                                                      --
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

--  The purpose of this body is simply to ensure that the two with'ed units
--  are properly included in the link. They are not with'ed from the spec
--  of System.Standard_Library, since this would cause order of elaboration
--  problems (Elaborate_Body would have the same problem).

pragma Warnings (Off);
--  Kill warnings from unused withs

pragma Polling (Off);
--  We must turn polling off for this unit, because otherwise we get
--  elaboration circularities with Ada.Exceptions if polling is on.

with System.Soft_Links;
--  Referenced directly from generated code using external symbols so it
--  must always be present in a build, even if no unit has a direct with
--  of this unit. Also referenced from exception handling routines.
--  This is needed for programs that don't use exceptions explicitely but
--  direct calls to Ada.Exceptions are generated by gigi (for example,
--  by calling __gnat_raise_constraint_error directly).

with System.Memory;
--  Referenced directly from generated code using external symbols, so it
--  must always be present in a build, even if no unit has a direct with
--  of this unit.

package body System.Standard_Library is

   Runtime_Finalized : Boolean := False;
   --  Set to True when adafinal is called. Used to ensure that subsequent
   --  calls to adafinal after the first have no effect.

   Inside_Elab_Final_Code : Integer := 0;
   pragma Export (C, Inside_Elab_Final_Code, "__gnat_inside_elab_final_code");
   --  ???This variable is obsolete starting from 29/08 but cannot be removed
   --  ???right away due to the bootstrap problems

   --------------------------
   -- Abort_Undefer_Direct --
   --------------------------

   procedure Abort_Undefer_Direct is
   begin
      System.Soft_Links.Abort_Undefer.all;
   end Abort_Undefer_Direct;

   --------------
   -- Adafinal --
   --------------

   procedure Adafinal is
   begin
      if not Runtime_Finalized then
         Runtime_Finalized := True;
         System.Soft_Links.Adafinal.all;
      end if;
   end Adafinal;

   -----------------
   -- Break_Start --
   -----------------

   procedure Break_Start;
   pragma Export (C, Break_Start, "__gnat_break_start");
   --  This is a dummy procedure that is called at the start of execution.
   --  Its sole purpose is to provide a well defined point for the placement
   --  of a main program breakpoint.

   procedure Break_Start is
   begin
      null;
   end Break_Start;

end System.Standard_Library;
