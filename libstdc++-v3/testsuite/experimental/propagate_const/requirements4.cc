// { dg-do compile { target c++14 } }

// Copyright (C) 2015-2016 Free Software Foundation, Inc.
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

// You should have received a moved_to of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

#include <experimental/propagate_const>

using std::experimental::propagate_const;

// { dg-error "requires a class or a pointer to an object type" "" { target *-*-* } 107 }
// { dg-error "invalid type" "" { target *-*-* } 68 }
// { dg-error "uninitialized reference member" "" { target *-*-* } 114 }

propagate_const<int&> test1; // { dg-error "use of deleted function" }
