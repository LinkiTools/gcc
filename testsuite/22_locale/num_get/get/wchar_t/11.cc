// Copyright (C) 2003 Free Software Foundation
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

// 22.2.2.1.1  num_get members

#include <locale>
#include <sstream>
#include <testsuite_hooks.h>

struct Punct: std::numpunct<wchar_t>
{
  std::string do_grouping() const { return "\1"; }
  wchar_t do_thousands_sep() const { return L'2'; }
  wchar_t do_decimal_point() const { return L'4'; }
};

void test01()
{
  using namespace std;
  typedef istreambuf_iterator<wchar_t> iterator_type;
  
  bool test __attribute__((unused)) = true;

  wistringstream iss;
  iss.imbue(locale(iss.getloc(), static_cast<numpunct<wchar_t>*>(new Punct)));
  const num_get<wchar_t>& ng = use_facet<num_get<wchar_t> >(iss.getloc()); 
  ios_base::iostate err = ios_base::goodbit;
  iterator_type end;
  double d = 0.0;
  double d1 = 13.0;
  long l = 0l;
  long l1 = 13l;
  
  iss.str(L"1234");
  err = ios_base::goodbit;
  end = ng.get(iss.rdbuf(), 0, iss, err, d);
  VERIFY( err == ios_base::eofbit );
  VERIFY( d == d1 );

  iss.str(L"1234");
  iss.clear();
  err = ios_base::goodbit;
  end = ng.get(iss.rdbuf(), 0, iss, err, l);
  VERIFY( err == ios_base::goodbit );
  VERIFY( l == l1 );
}


int main()
{
  test01();
  return 0;
}
