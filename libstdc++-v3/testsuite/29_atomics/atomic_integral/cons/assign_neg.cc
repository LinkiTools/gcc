// { dg-options "-std=gnu++0x" }
// { dg-do compile }

// Copyright (C) 2008, 2009, 2010 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

#include <atomic>
#include <testsuite_common_types.h>

int main()
{
  __gnu_test::assignable test;
  __gnu_cxx::typelist::apply_generator(test,
				       __gnu_test::atomic_integrals::type());
  return 0;
}

// { dg-error "deleted" "" { target *-*-* } 544 }
// { dg-prune-output "include" }
