// 2001-05-21 Benjamin Kosnik  <bkoz@redhat.com>

// Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
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

// 27.8.1.4 Overridden virtual functions

#include <fstream>
#include <testsuite_hooks.h>
#include <testsuite_io.h>

// @require@ %-*.tst %-*.txt
// @diff@ %-*.tst %*.txt

const char name_01[] = "filebuf_virtuals-1.txt"; // file with data in it
const char name_03[] = "tmp_sputc_2io.tst";

void test05() 
{
  using namespace std;
  using namespace __gnu_cxx_test;

  typedef filebuf::int_type 	int_type;
  typedef filebuf::traits_type 	traits_type;

  bool 					test = true;
  streamsize 			strmsz_1, strmsz_2;
  int 					i = 0, j = 0, k = 0;
  int_type 			c1, c2, c3;

  // int_type sputc(char_type c)
  // if out_cur not avail, return overflow(traits_type::to_int_type(c)) 
  // else, stores c at out_cur,
  // increments out_cur, and returns c as int_type

  // in | out 1
  {
    constraint_filebuf fb_03; 
    fb_03.pubsetbuf(0, 0);
    fb_03.open(name_03, ios_base::out | ios_base::in | ios_base::trunc);
    VERIFY( fb_03.unbuffered() );
    c1 = fb_03.sputc('b'); 
    VERIFY( c1 == 'b' );
    c2 = fb_03.sputc('d'); 
    VERIFY( c2 == 'd' );
    for (int i = 50; i <= 90; ++i) 
      c2 = fb_03.sputc(char(i));
    VERIFY( fb_03.unbuffered() );
  }
}

main() 
{
  test05();
  return 0;
}
