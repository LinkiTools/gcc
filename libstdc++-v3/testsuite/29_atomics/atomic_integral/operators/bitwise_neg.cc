// { dg-options "-std=gnu++0x" }
// { dg-do compile }
// -*- C++ -*-
 
// Copyright (C) 2008 Free Software Foundation, Inc.
 
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2, or (at
// your option) any later version.
 
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
 
// You should have received a copy of the GNU General Public License
// along with this library; see the file COPYING.  If not, write to
// the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
// MA 02110-1301, USA.
 
// As a special exception, you may use this file as part of a free
// software library without restriction.  Specifically, if other files
// instantiate templates or use macros or inline functions from this
// file, or you compile this file and link it with other files to
// produce an executable, this file does not by itself cause the
// resulting executable to be covered by the GNU General Public
// License.  This exception does not however invalidate any other
// reasons why the executable file might be covered by the GNU General
// Public License.
 
#include <cstdatomic>
#include <testsuite_common_types.h>

int main()
{
  __gnu_test::bitwise_assignment_operators<std::atomic_bool>();
  return 0;
}

// { dg-error "operator" "" { target *-*-* } 413 }
// { dg-error "operator" "" { target *-*-* } 414 }
// { dg-error "operator" "" { target *-*-* } 415 }
// { dg-excess-errors "In file included from" }
