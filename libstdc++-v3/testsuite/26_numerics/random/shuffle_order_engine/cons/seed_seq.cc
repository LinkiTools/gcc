// { dg-options "-std=c++0x" }
//
// 2008-12-07  Edward M. Smith-Rowland <3dw4rd@verizon.net>
//
// Copyright (C) 2008, 2009 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

// 26.4.4.3 class template shuffle_order_engine [rand.adapt.shuf]
// 26.4.2.3 concept RandomNumberEngineAdaptor [rand.concept.adapt]

#include <random>
#include <testsuite_hooks.h>

void
test01()
{
  bool test __attribute__((unused)) = true;

  std::seed_seq seq;

  std::shuffle_order_engine
    <
      std::linear_congruential_engine<uint_fast32_t,16807UL, 0UL, 2147483647UL>,
      256
    > e(seq);
}

int main()
{
  test01();
  return 0;
}
