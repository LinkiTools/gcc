// { dg-do run { target c++11 } }
// { dg-options "-D__STDCPP_WANT_MATH_SPEC_FUNCS__" }
//
// Copyright (C) 2016 Free Software Foundation, Inc.
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

//  comp_ellint_1

//  Compare against values generated by the GNU Scientific Library.
//  The GSL can be found on the web: http://www.gnu.org/software/gsl/
#include <limits>
#include <cmath>
#if defined(__TEST_DEBUG)
#  include <iostream>
#  define VERIFY(A) \
  if (!(A)) \
    { \
      std::cout << "line " << __LINE__ \
	<< "  max_abs_frac = " << max_abs_frac \
	<< std::endl; \
    }
#else
#  include <testsuite_hooks.h>
#endif
#include <specfun_testcase.h>

// Test data.
// max(|f - f_GSL|): 6.6613381477509392e-16
// max(|f - f_GSL| / |f_GSL|): 4.0617918857203532e-16
const testcase_comp_ellint_1<double>
data001[19] =
{
  { 2.2805491384227703, -0.90000000000000002 },
  { 1.9953027776647296, -0.80000000000000004 },
  { 1.8456939983747236, -0.69999999999999996 },
  { 1.7507538029157526, -0.59999999999999998 },
  { 1.6857503548125963, -0.50000000000000000 },
  { 1.6399998658645112, -0.40000000000000002 },
  { 1.6080486199305128, -0.30000000000000004 },
  { 1.5868678474541660, -0.19999999999999996 },
  { 1.5747455615173562, -0.099999999999999978 },
  { 1.5707963267948966, 0.0000000000000000 },
  { 1.5747455615173562, 0.10000000000000009 },
  { 1.5868678474541660, 0.19999999999999996 },
  { 1.6080486199305128, 0.30000000000000004 },
  { 1.6399998658645112, 0.39999999999999991 },
  { 1.6857503548125963, 0.50000000000000000 },
  { 1.7507538029157526, 0.60000000000000009 },
  { 1.8456939983747236, 0.69999999999999996 },
  { 1.9953027776647296, 0.80000000000000004 },
  { 2.2805491384227703, 0.89999999999999991 },
};
const double toler001 = 2.5000000000000020e-13;

template<typename Tp, unsigned int Num>
  void
  test(const testcase_comp_ellint_1<Tp> (&data)[Num], Tp toler)
  {
    const Tp eps = std::numeric_limits<Tp>::epsilon();
    Tp max_abs_diff = -Tp(1);
    Tp max_abs_frac = -Tp(1);
    unsigned int num_datum = Num;
    for (unsigned int i = 0; i < num_datum; ++i)
      {
	const Tp f = std::comp_ellint_1(data[i].k);
	const Tp f0 = data[i].f0;
	const Tp diff = f - f0;
	if (std::abs(diff) > max_abs_diff)
	  max_abs_diff = std::abs(diff);
	if (std::abs(f0) > Tp(10) * eps
	 && std::abs(f) > Tp(10) * eps)
	  {
	    const Tp frac = diff / f0;
	    if (std::abs(frac) > max_abs_frac)
	      max_abs_frac = std::abs(frac);
	  }
      }
    VERIFY(max_abs_frac < toler);
  }

int
main()
{
  test(data001, toler001);
  return 0;
}
