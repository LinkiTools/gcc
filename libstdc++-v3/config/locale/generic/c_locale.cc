// Wrapper for underlying C-language localization -*- C++ -*-

// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008
// Free Software Foundation, Inc.
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
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

//
// ISO C++ 14882: 22.8  Standard locale categories.
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <cerrno>  // For errno
#include <cmath>  // For isinf, finite, finitef, fabs
#include <cstdlib>  // For strof, strtold
#include <cstring>
#include <cstdio>
#include <locale>
#include <limits>
#include <cstddef>

#ifdef _GLIBCXX_HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

_GLIBCXX_BEGIN_NAMESPACE(std)

  // Specializations for all types used in num_get.
  template<>
    void
    __convert_to_v(const char* __s, float& __v, ios_base::iostate& __err, 
		   const __c_locale&) 	      
    {
      // Assumes __s formatted for "C" locale.
      char* __old = setlocale(LC_ALL, NULL);
      const size_t __len = strlen(__old) + 1;
      char* __sav = new char[__len];
      memcpy(__sav, __old, __len);
      setlocale(LC_ALL, "C");
      char* __sanity;

#if !__FLT_HAS_INFINITY__
      errno = 0;
#endif

#if defined(_GLIBCXX_HAVE_STRTOF)
      float __f = strtof(__s, &__sanity);
#else
      double __d = strtod(__s, &__sanity);
      float __f = static_cast<float>(__d);
#ifdef _GLIBCXX_HAVE_FINITEF
      if (!finitef (__f))
	__s = __sanity;
#elif defined (_GLIBCXX_HAVE_FINITE)
      if (!finite (static_cast<double> (__f)))
	__s = __sanity;
#elif defined (_GLIBCXX_HAVE_ISINF)
      if (isinf (static_cast<double> (__f)))
	__s = __sanity;
#else
      if (fabs(__d) > numeric_limits<float>::max())
	__s = __sanity;
#endif
#endif

      if (__sanity != __s
#if !__FLT_HAS_INFINITY__
	  && errno != ERANGE)
#else
	  && __f != __builtin_huge_valf() && __f != -__builtin_huge_valf())
#endif
	__v = __f;
      else
	__err |= ios_base::failbit;

      setlocale(LC_ALL, __sav);
      delete [] __sav;
    }

  template<>
    void
    __convert_to_v(const char* __s, double& __v, ios_base::iostate& __err, 
		   const __c_locale&) 
    {
      // Assumes __s formatted for "C" locale.
      char* __old = setlocale(LC_ALL, NULL);
      const size_t __len = strlen(__old) + 1;
      char* __sav = new char[__len];
      memcpy(__sav, __old, __len);
      setlocale(LC_ALL, "C");
      char* __sanity;

#if !__DBL_HAS_INFINITY__
      errno = 0;
#endif

      double __d = strtod(__s, &__sanity);

      if (__sanity != __s
#if !__DBL_HAS_INFINITY__
          && errno != ERANGE) 
#else
	  && __d != __builtin_huge_val() && __d != -__builtin_huge_val())
#endif
	__v = __d;
      else
	__err |= ios_base::failbit;

      setlocale(LC_ALL, __sav);
      delete [] __sav;
    }

  template<>
    void
    __convert_to_v(const char* __s, long double& __v, 
		   ios_base::iostate& __err, const __c_locale&) 
    {
      // Assumes __s formatted for "C" locale.
      char* __old = setlocale(LC_ALL, NULL);
      const size_t __len = strlen(__old) + 1;
      char* __sav = new char[__len];
      memcpy(__sav, __old, __len);
      setlocale(LC_ALL, "C");

#if !__LDBL_HAS_INFINITY__
      errno = 0;
#endif

#if defined(_GLIBCXX_HAVE_STRTOLD) && !defined(_GLIBCXX_HAVE_BROKEN_STRTOLD)
      char* __sanity;
      long double __ld = strtold(__s, &__sanity);

      if (__sanity != __s
#if !__LDBL_HAS_INFINITY__
          && errno != ERANGE)
#else
	  && __ld != __builtin_huge_vall() && __ld != -__builtin_huge_vall())
#endif
	__v = __ld;

#else
      typedef char_traits<char>::int_type int_type;
      long double __ld;
      int __p = sscanf(__s, "%Lf", &__ld);

      if (__p && static_cast<int_type>(__p) != char_traits<char>::eof()
#if !__LDBL_HAS_INFINITY__
          && errno != ERANGE)
#else
          && __ld != __builtin_huge_vall() && __ld != -__builtin_huge_vall())
#endif
	__v = __ld;

#endif
      else
	__err |= ios_base::failbit;

      setlocale(LC_ALL, __sav);
      delete [] __sav;
    }

  void
  locale::facet::_S_create_c_locale(__c_locale& __cloc, const char* __s, 
				    __c_locale)
  {
    // Currently, the generic model only supports the "C" locale.
    // See http://gcc.gnu.org/ml/libstdc++/2003-02/msg00345.html
    __cloc = NULL;
    if (strcmp(__s, "C"))
      __throw_runtime_error(__N("locale::facet::_S_create_c_locale "
			    "name not valid"));
  }

  void
  locale::facet::_S_destroy_c_locale(__c_locale& __cloc)
  { __cloc = NULL; }

  __c_locale
  locale::facet::_S_clone_c_locale(__c_locale&)
  { return __c_locale(); }

_GLIBCXX_END_NAMESPACE

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  const char* const category_names[6 + _GLIBCXX_NUM_CATEGORIES] =
    {
      "LC_CTYPE", 
      "LC_NUMERIC",
      "LC_TIME",   
      "LC_COLLATE", 
      "LC_MONETARY",
      "LC_MESSAGES"
    };

_GLIBCXX_END_NAMESPACE

_GLIBCXX_BEGIN_NAMESPACE(std)

  const char* const* const locale::_S_categories = __gnu_cxx::category_names;

_GLIBCXX_END_NAMESPACE

// XXX GLIBCXX_ABI Deprecated
#ifdef _GLIBCXX_LONG_DOUBLE_COMPAT
#define _GLIBCXX_LDBL_COMPAT(dbl, ldbl) \
  extern "C" void ldbl (void) __attribute__ ((alias (#dbl)))
_GLIBCXX_LDBL_COMPAT(_ZSt14__convert_to_vIdEvPKcRT_RSt12_Ios_IostateRKPi, _ZSt14__convert_to_vIeEvPKcRT_RSt12_Ios_IostateRKPi);
#endif // _GLIBCXX_LONG_DOUBLE_COMPAT
