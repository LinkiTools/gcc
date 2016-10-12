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

//  expint

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
// max(|f - f_GSL|): 4.4408920985006262e-16
// max(|f - f_GSL| / |f_GSL|): 2.0242558374827411e-15
// mean(f - f_GSL): 1.0184926479400409e-17
// variance(f - f_GSL): 3.9207190155645133e-33
// stddev(f - f_GSL): 6.2615645134139710e-17
const testcase_expint<double>
data001[50] =
{
  { -3.7832640295504591e-24, -50.000000000000000, 0.0 },
  { -1.0489811642368024e-23, -49.000000000000000, 0.0 },
  { -2.9096641904058423e-23, -48.000000000000000, 0.0 },
  { -8.0741978427258127e-23, -47.000000000000000, 0.0 },
  { -2.2415317597442998e-22, -46.000000000000000, 0.0 },
  { -6.2256908094623848e-22, -45.000000000000000, 0.0 },
  { -1.7299598742816476e-21, -44.000000000000000, 0.0 },
  { -4.8094965569500181e-21, -43.000000000000000, 0.0 },
  { -1.3377908810011775e-20, -42.000000000000000, 0.0 },
  { -3.7231667764599780e-20, -41.000000000000000, 0.0 },
  { -1.0367732614516570e-19, -40.000000000000000, 0.0 },
  { -2.8887793015227007e-19, -39.000000000000000, 0.0 },
  { -8.0541069142907499e-19, -38.000000000000000, 0.0 },
  { -2.2470206975885714e-18, -37.000000000000000, 0.0 },
  { -6.2733390097622421e-18, -36.000000000000000, 0.0 },
  { -1.7527059389947371e-17, -35.000000000000000, 0.0 },
  { -4.9006761183927874e-17, -34.000000000000000, 0.0 },
  { -1.3713843484487468e-16, -33.000000000000000, 0.0 },
  { -3.8409618012250671e-16, -32.000000000000000, 0.0 },
  { -1.0767670386162383e-15, -31.000000000000000, 0.0 },
  { -3.0215520106888124e-15, -30.000000000000000, 0.0 },
  { -8.4877597783535634e-15, -29.000000000000000, 0.0 },
  { -2.3869415119337330e-14, -28.000000000000000, 0.0 },
  { -6.7206374352620390e-14, -27.000000000000000, 0.0 },
  { -1.8946858856749785e-13, -26.000000000000000, 0.0 },
  { -5.3488997553402167e-13, -25.000000000000000, 0.0 },
  { -1.5123058939997059e-12, -24.000000000000000, 0.0 },
  { -4.2826847956656722e-12, -23.000000000000000, 0.0 },
  { -1.2149378956204371e-11, -22.000000000000000, 0.0 },
  { -3.4532012671467559e-11, -21.000000000000000, 0.0 },
  { -9.8355252906498815e-11, -20.000000000000000, 0.0 },
  { -2.8078290970607954e-10, -19.000000000000000, 0.0 },
  { -8.0360903448286769e-10, -18.000000000000000, 0.0 },
  { -2.3064319898216547e-09, -17.000000000000000, 0.0 },
  { -6.6404872494410427e-09, -16.000000000000000, 0.0 },
  { -1.9186278921478670e-08, -15.000000000000000, 0.0 },
  { -5.5656311111451816e-08, -14.000000000000000, 0.0 },
  { -1.6218662188014328e-07, -13.000000000000000, 0.0 },
  { -4.7510818246724931e-07, -12.000000000000000, 0.0 },
  { -1.4003003042474418e-06, -11.000000000000000, 0.0 },
  { -4.1569689296853246e-06, -10.000000000000000, 0.0 },
  { -1.2447354178006272e-05, -9.0000000000000000, 0.0 },
  { -3.7665622843924906e-05, -8.0000000000000000, 0.0 },
  { -0.00011548173161033820, -7.0000000000000000, 0.0 },
  { -0.00036008245216265862, -6.0000000000000000, 0.0 },
  { -0.0011482955912753257, -5.0000000000000000, 0.0 },
  { -0.0037793524098489058, -4.0000000000000000, 0.0 },
  { -0.013048381094197037, -3.0000000000000000, 0.0 },
  { -0.048900510708061125, -2.0000000000000000, 0.0 },
  { -0.21938393439552029, -1.0000000000000000, 0.0 },
};
const double toler001 = 2.5000000000000020e-13;
//  expint


// Test data.
// max(|f - f_GSL|): 2048.0000000000000
// max(|f - f_GSL| / |f_GSL|): 1.4993769017626171e-15
// mean(f - f_GSL): -28.457166483323103
// variance(f - f_GSL): inf
// stddev(f - f_GSL): 1.1944581611574716e+294
const testcase_expint<double>
data002[50] =
{
  { 1.8951178163559366, 1.0000000000000000, 0.0 },
  { 4.9542343560018907, 2.0000000000000000, 0.0 },
  { 9.9338325706254160, 3.0000000000000000, 0.0 },
  { 19.630874470056217, 4.0000000000000000, 0.0 },
  { 40.185275355803178, 5.0000000000000000, 0.0 },
  { 85.989762142439204, 6.0000000000000000, 0.0 },
  { 191.50474333550136, 7.0000000000000000, 0.0 },
  { 440.37989953483833, 8.0000000000000000, 0.0 },
  { 1037.8782907170896, 9.0000000000000000, 0.0 },
  { 2492.2289762418782, 10.000000000000000, 0.0 },
  { 6071.4063740986112, 11.000000000000000, 0.0 },
  { 14959.532666397528, 12.000000000000000, 0.0 },
  { 37197.688490689041, 13.000000000000000, 0.0 },
  { 93192.513633965369, 14.000000000000000, 0.0 },
  { 234955.85249076830, 15.000000000000000, 0.0 },
  { 595560.99867083691, 16.000000000000000, 0.0 },
  { 1516637.8940425171, 17.000000000000000, 0.0 },
  { 3877904.3305974435, 18.000000000000000, 0.0 },
  { 9950907.2510468438, 19.000000000000000, 0.0 },
  { 25615652.664056588, 20.000000000000000, 0.0 },
  { 66127186.355484925, 21.000000000000000, 0.0 },
  { 171144671.30036369, 22.000000000000000, 0.0 },
  { 443966369.83027124, 23.000000000000000, 0.0 },
  { 1154115391.8491828, 24.000000000000000, 0.0 },
  { 3005950906.5255494, 25.000000000000000, 0.0 },
  { 7842940991.8981876, 26.000000000000000, 0.0 },
  { 20496497119.880810, 27.000000000000000, 0.0 },
  { 53645118592.314682, 28.000000000000000, 0.0 },
  { 140599195758.40689, 29.000000000000000, 0.0 },
  { 368973209407.27417, 30.000000000000000, 0.0 },
  { 969455575968.39392, 31.000000000000000, 0.0 },
  { 2550043566357.7871, 32.000000000000000, 0.0 },
  { 6714640184076.4971, 33.000000000000000, 0.0 },
  { 17698037244116.266, 34.000000000000000, 0.0 },
  { 46690550144661.602, 35.000000000000000, 0.0 },
  { 123285207991209.75, 36.000000000000000, 0.0 },
  { 325798899867226.50, 37.000000000000000, 0.0 },
  { 861638819996578.75, 38.000000000000000, 0.0 },
  { 2280446200301902.5, 39.000000000000000, 0.0 },
  { 6039718263611242.0, 40.000000000000000, 0.0 },
  { 16006649143245042., 41.000000000000000, 0.0 },
  { 42447960921368504., 42.000000000000000, 0.0 },
  { 1.1263482901669666e+17, 43.000000000000000, 0.0 },
  { 2.9904447186323366e+17, 44.000000000000000, 0.0 },
  { 7.9439160357044531e+17, 45.000000000000000, 0.0 },
  { 2.1113423886478239e+18, 46.000000000000000, 0.0 },
  { 5.6143296808103424e+18, 47.000000000000000, 0.0 },
  { 1.4936302131129930e+19, 48.000000000000000, 0.0 },
  { 3.9754427479037444e+19, 49.000000000000000, 0.0 },
  { 1.0585636897131690e+20, 50.000000000000000, 0.0 },
};
const double toler002 = 2.5000000000000020e-13;

template<typename Ret, unsigned int Num>
  void
  test(const testcase_expint<Ret> (&data)[Num], Ret toler)
  {
    bool test __attribute__((unused)) = true;
    const Ret eps = std::numeric_limits<Ret>::epsilon();
    Ret max_abs_diff = -Ret(1);
    Ret max_abs_frac = -Ret(1);
    unsigned int num_datum = Num;
    for (unsigned int i = 0; i < num_datum; ++i)
      {
	const Ret f = std::expint(data[i].x);
	const Ret f0 = data[i].f0;
	const Ret diff = f - f0;
	if (std::abs(diff) > max_abs_diff)
	  max_abs_diff = std::abs(diff);
	if (std::abs(f0) > Ret(10) * eps
	 && std::abs(f) > Ret(10) * eps)
	  {
	    const Ret frac = diff / f0;
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
  test(data002, toler002);
  return 0;
}
