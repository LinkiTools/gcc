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

// 25.5.8 [lib.alg.unique]

#include <algorithm>
#include <testsuite_hooks.h>
#include <testsuite_iterators.h>

using __gnu_test::test_container;
using __gnu_test::forward_iterator_wrapper;
using std::unique;

typedef test_container<int, forward_iterator_wrapper> Container;
int array1[] = {0, 0, 0, 1, 1, 1};
int array2[2];

void 
test1()
{
  Container con1(array1, array1);
  Container con2(array2, array2);
  VERIFY(unique_copy(con1.begin(), con1.end(), con2.begin()).ptr == array2);
}

void
test2()
{  
  Container con1(array1, array1 + 6);
  Container con2(array2, array2 + 2);
  VERIFY(unique_copy(con1.begin(), con1.end(), con2.begin()).ptr 
         == array2 + 2);
}

int 
main()
{
  test1();
}
