// Special functions -*- C++ -*-

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
// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

/** @file bits/sf_dawson.tcc
 *  This is an internal header file, included by other library headers.
 *  Do not attempt to use it directly. @headername{cmath}
 */

#ifndef _GLIBCXX_BITS_SF_DAWSON_TCC
#define _GLIBCXX_BITS_SF_DAWSON_TCC 1

#pragma GCC system_header

#include <ext/math_const.h>

namespace std _GLIBCXX_VISIBILITY(default)
{
// Implementation-space details.
namespace __detail
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

  /**
   * @brief Compute the Dawson integral using the series expansion.
   */
  template<typename _Tp>
    _Tp
    __dawson_series(_Tp __x)
    {
      auto __x2 = __x * __x;
      _Tp __sum(1);
      auto __k = 1;
      _Tp __term(1);
      while (true)
	{
	  __term *= -(_Tp{2} / _Tp(2 * __k + 1)) * __x2;
	  __sum += __term;
	  ++__k;
	  if (std::abs(__term) < __gnu_cxx::__epsilon<_Tp>())
	    break;
	}
      return __x * __sum;
    }


  /**
   * @brief Compute the Dawson integral using a sampling theorem
   * representation.
   */
  template<typename _Tp>
    _Tp
    __dawson_cont_frac(_Tp __x)
    {
      constexpr auto _S_1_sqrtpi{0.5641895835477562869480794515607726L};
      constexpr auto _S_eps = __gnu_cxx::__epsilon<_Tp>();
      constexpr auto _S_H{0.2L};
      /// @todo this needs some compile-time construction!
      constexpr auto _S_n_max = 100;
      // The array below is produced by the following snippet:
	//static _Tp _S_c[_S_n_max + 1];
	//static auto __init = false;
	//if (! __init)
	//  {
	//    __init = true;
	//    for (unsigned int __i = 0; __i < _S_n_max; ++__i)
	//  	{
	//  	  auto __y = _Tp(2 * __i + 1) * _S_H;
	//  	  _S_c[__i] = std::exp(-__y * __y);
	//  	}
	//  }
      constexpr _Tp
      _S_c[_S_n_max]
      {
	9.60789439152323209438169001326016e-001L,
	6.97676326071031057202321464142399e-001L,
	3.67879441171442321585552377928190e-001L,
	1.40858420921044996140488229803164e-001L,
	3.91638950989870737363317023736605e-002L,
	7.90705405159344049259833141481939e-003L,
	1.15922917390459114979971194637303e-003L,
	1.23409804086679549467531425748256e-004L,
	9.54016287307923483860084888844751e-006L,
	5.35534780279310615538302709570342e-007L,
	2.18295779512547920804008261508151e-008L,
	6.46143177310610898572394840226245e-010L,
	1.38879438649640205852509269274927e-011L,
	2.16756888261896194059418783466426e-013L,
	2.45659536879214445146530280703707e-015L,
	2.02171584869534202501301885439244e-017L,
	1.20818201989997357022759799713705e-019L,
	5.24288566336346393020847897060042e-022L,
	1.65209178231426859061623229756950e-024L,
	3.78027784477608462898406047061928e-027L,
	6.28114814760598920398901871648105e-030L,
	7.57844526761838263084531617905181e-033L,
	6.63967719958073438612478157054279e-036L,
	4.22415240620620042745713680530106e-039L,
	1.95145238029537774304135768095865e-042L,
	6.54639343720499329608790652000114e-046L,
	1.59467436689686986494851454027059e-049L,
	2.82077008846013539186713516083226e-053L,
	3.62317350508722347934110715686963e-057L,
	3.37937463327921536912579153668807e-061L,
	2.28880774041243919016662861963690e-065L,
	1.12566212332063150824140805397885e-069L,
	4.02006021574335522941543113897424e-074L,
	1.04251624107215374431522714856879e-078L,
	1.96317432844445950021408646503182e-083L,
	2.68448306782610758445887030316137e-088L,
	2.66555861809636445675211579279178e-093L,
	1.92194772782384905675933472433427e-098L,
	1.00628424189764403921411803642026e-103L,
	3.82582884899196609344187870411088e-109L,
	1.05622433516056737024713529752639e-114L,
	2.11744708802352680125417726491892e-120L,
	3.08244069694909838852521233803938e-126L,
	3.25838695945952019907932932091244e-132L,
	2.50113050879336730108217682794430e-138L,
	1.39410605788744688310664420089858e-144L,
	5.64262307776046700110508712234927e-151L,
	1.65841047768114512490788414841255e-157L,
	3.53939302656965650656012371162244e-164L,
	5.48518544141128941971605912419011e-171L,
	6.17276302016755883677739891028503e-178L,
	5.04421581617080666599125850865623e-185L,
	2.99318445226019269570089318605868e-192L,
	1.28973078889439493438423594343846e-199L,
	4.03543559387410258104874489257167e-207L,
	9.16869527015865195249604938556767e-215L,
	1.51269169695184528724035028896828e-222L,
	1.81225402579399230372761095817594e-230L,
	1.57657083780365412281042949520717e-238L,
	9.95941136080552759796029421444741e-247L,
	4.56856300016410640187636545957934e-255L,
	1.52177810552438350226292055756678e-263L,
	3.68085585480180054048750621026265e-272L,
	6.46505249021408739998493794189369e-281L,
	8.24557727130540112516494412700777e-290L,
	7.63652613360855025076250919098713e-299L,
	5.13566142435820732098775884762237e-308L,
	2.50797205186097588307523168005178e-317L,
	8.89354212166825988685748389268173e-327L,
	2.29009029289207343509554502505914e-336L,
	4.28209414411196708972357973309330e-346L,
	5.81414130368226737065152548476486e-356L,
	5.73245586032578521491483688909045e-366L,
	4.10413485100712456782307385028474e-376L,
	2.13367510791650599201905024202141e-386L,
	8.05491060616403355837454851476424e-397L,
	2.20810095242489485189514149199835e-407L,
	4.39544541351233950364876366113300e-418L,
	6.35349397868382398176675869724792e-429L,
	6.66880655999041479890323913548341e-440L,
	5.08287446085302533267012977225643e-451L,
	2.81317281842290615009371958753732e-462L,
	1.13060058895745543739097618631818e-473L,
	3.29949722931472550915694191808876e-485L,
	6.99217186328695864989695230548821e-497L,
	1.07597501474761095153971136302831e-508L,
	1.20231438909832022259340851931311e-520L,
	9.75572766967242798560310989969135e-533L,
	5.74813630703326414824802394458614e-545L,
	2.45934928751589391095933749522896e-557L,
	7.64080532462818847220429025437078e-570L,
	1.72378787535648674299867938844160e-582L,
	2.82393225820965430848685848068497e-595L,
	3.35931317332728269858168144943446e-608L,
	2.90183341500822904994175717131364e-621L,
	1.82020468349683678875494180471718e-634L,
	8.29074854613655162715131076161845e-648L,
	2.74216147544224226066985666394109e-661L,
	6.58594459239068165700389626561349e-675L,
	1.14860019578505635531744602406704e-688L
      };

      auto __xx = std::abs(__x);
      auto __n0 = 2 * static_cast<int>(_Tp{0.5L} + _Tp{0.5L} * __xx / _S_H);
      auto __xp = __xx - __n0 * _S_H;
      auto __e1 = std::exp(_Tp{2} * __xp * _S_H);
      auto __e2 = __e1 * __e1;
      auto __d1 = _Tp(__n0) + _Tp{1};
      auto __d2 = __d1 - _Tp{2};
      auto __sum = _Tp{0};
      for (unsigned int __i = 0; __i < _S_n_max; ++__i)
	{
	  auto __term = _S_c[__i] * (__e1 / __d1 + _Tp{1} / (__d2 * __e1));
	  __sum += __term;
	  if (std::abs(__term / __sum) < _S_eps)
	    break;
	  __d1 += _Tp{2};
	  __d2 -= _Tp{2};
	  __e1 *= __e2;
	}
      return std::copysign(std::exp(-__xp * __xp), __x)
	   * __sum * _S_1_sqrtpi;
    }

  /**
   * @brief Return the Dawson integral, @f$ F(x) @f$, for real argument @c x.
   *
   * The Dawson integral is defined by:
   * @f[
   *    F(x) = e^{-x^2} \int_0^x e^{y^2} dy
   * @f]
   * and it's derivative is:
   * @f[
   *    F'(x) = 1 - 2xF(x)
   * @f]
   *
   * @param __x The argument @f$ -inf < x < inf @f$.
   */
  template<typename _Tp>
    _Tp
    __dawson(_Tp __x)
    {
      constexpr auto _S_NaN = __gnu_cxx::__quiet_NaN<_Tp>();
      constexpr _Tp _S_x_min{0.2L};

      if (__isnan(__x))
	return _S_NaN;
      else if (std::abs(__x) < _S_x_min)
	return __dawson_series(__x);
      else
	return __dawson_cont_frac(__x);
    }

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace __detail
}

#endif // _GLIBCXX_BITS_SF_DAWSON_TCC
