// Copyright (C) 2005 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// 25.1.7 [lib.mismatch]

// { dg-do compile }

#include <algorithm>
#include <utility>
#include <testsuite_iterators.h>

using __gnu_test::input_iterator_wrapper;

struct Lhs1 { };

struct Rhs1 { };

bool operator==(const Lhs1&, const Rhs1&) {return true;}

struct Lhs2 { };

struct Rhs2 { };

bool predicate(const Lhs2&, const Rhs2&) {return true;}

std::pair<input_iterator_wrapper<Lhs1>, input_iterator_wrapper<Rhs1> >
test1(input_iterator_wrapper<Lhs1>& lhs1, input_iterator_wrapper<Rhs1>& rhs1)
{
  return std::mismatch(lhs1, lhs1, rhs1);
}

std::pair<input_iterator_wrapper<Lhs2>, input_iterator_wrapper<Rhs2> >
test2(input_iterator_wrapper<Lhs2>& lhs2, input_iterator_wrapper<Rhs2>& rhs2)
{
  return std::mismatch(lhs2, lhs2, rhs2, predicate);
}
