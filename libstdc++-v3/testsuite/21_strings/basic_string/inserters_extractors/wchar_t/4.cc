// 1999-07-01 bkoz

// Copyright (C) 1999, 2000, 2001, 2002, 2003 Free Software Foundation, Inc.
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

// 21.3.7.9 inserters and extractors

// NB: This file is predicated on sstreams, istreams, and ostreams
// working, not to mention other major details like char_traits, and
// all of the string class.

#include <string>
#include <sstream>
#include <testsuite_hooks.h>

// testing basic_stringbuf::xsputn via stress testing with large strings
// based on a bug report libstdc++ 9
void test04(int size)
{
  bool test = true;
  std::wstring str(size, L's');
  int expected_size = 2 * (size + 1);
  std::wostringstream oss(str);
  
  // sanity checks
  VERIFY( str.size() == size );
  VERIFY( oss.good() );

  // stress test
  oss << str << std::endl;
  if (!oss.good()) 
    test = false;

  oss << str << std::endl;
  if (!oss.good()) 
    test = false;

  VERIFY( str.size() == size );
  VERIFY( oss.good() );
  std::wstring str_tmp = oss.str();
  VERIFY( str_tmp.size() == expected_size );
}

int main()
{ 
  test04(1); // expected_size == 4
  test04(1000); // expected_size == 2002
  test04(10000); // expected_size == 20002
  return 0;
}
