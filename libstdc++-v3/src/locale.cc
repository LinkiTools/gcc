// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004
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
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <clocale>
#include <cstring>
#include <cstdlib>     // For getenv
#include <cctype>
#include <cwctype>     // For towupper, etc.
#include <locale>
#include <bits/atomicity.h>

namespace std 
{
  // Definitions for static const data members of locale.
  const locale::category 	locale::none;
  const locale::category 	locale::ctype;
  const locale::category 	locale::numeric;
  const locale::category 	locale::collate;
  const locale::category 	locale::time;
  const locale::category 	locale::monetary;
  const locale::category 	locale::messages;
  const locale::category 	locale::all;

  // These are no longer exported.
  locale::_Impl*                locale::_S_classic;
  locale::_Impl* 		locale::_S_global; 
  const size_t 			locale::_S_categories_size;

#ifdef __GTHREADS
  __gthread_once_t 		locale::_S_once = __GTHREAD_ONCE_INIT;
#endif

  locale::locale(const locale& __other) throw()
  { (_M_impl = __other._M_impl)->_M_add_reference(); }

  // This is used to initialize global and classic locales, and
  // assumes that the _Impl objects are constructed correctly.
  // The lack of a reference increment is intentional.
  locale::locale(_Impl* __ip) throw() : _M_impl(__ip)
  { }

  locale::~locale() throw()
  { _M_impl->_M_remove_reference(); }

  bool
  locale::operator==(const locale& __rhs) const throw()
  {
    // Deal first with the common cases, fast to process: refcopies,
    // unnamed (i.e., !_M_names[0]), "simple" (!_M_names[1] => all the
    // categories same name, i.e., _M_names[0]). Otherwise fall back
    // to the general locale::name().
    bool __ret;
    if (_M_impl == __rhs._M_impl)
      __ret = true;
    else if (!_M_impl->_M_names[0] || !__rhs._M_impl->_M_names[0]
	     || std::strcmp(_M_impl->_M_names[0],
			    __rhs._M_impl->_M_names[0]) != 0)
      __ret = false;
    else if (!_M_impl->_M_names[1] && !__rhs._M_impl->_M_names[1])
      __ret = true;
    else
      __ret = this->name() == __rhs.name();
    return __ret;
  }

  const locale&
  locale::operator=(const locale& __other) throw()
  {
    __other._M_impl->_M_add_reference();
    _M_impl->_M_remove_reference();
    _M_impl = __other._M_impl;
    return *this;
  }

  string
  locale::name() const
  {
    string __ret;
    if (!_M_impl->_M_names[0])
      __ret = '*';
    else if (_M_impl->_M_check_same_name())
      __ret = _M_impl->_M_names[0];
    else
      {
	__ret.reserve(128);
	__ret += _S_categories[0];
	__ret += '=';
	__ret += _M_impl->_M_names[0]; 
	for (size_t __i = 1; __i < _S_categories_size; ++__i)
	  {
	    __ret += ';';
	    __ret += _S_categories[__i];
	    __ret += '=';
	    __ret += _M_impl->_M_names[__i];
	  }
      }
    return __ret;
  }

  locale::category
  locale::_S_normalize_category(category __cat) 
  {
    int __ret = 0;
    if (__cat == none || (__cat & all) && !(__cat & ~all))
      __ret = __cat;
    else
      {
	// NB: May be a C-style "LC_ALL" category; convert.
	switch (__cat)
	  {
	  case LC_COLLATE:  
	    __ret = collate; 
	    break;
	  case LC_CTYPE:    
	    __ret = ctype;
	    break;
	  case LC_MONETARY: 
	    __ret = monetary;
	    break;
	  case LC_NUMERIC:  
	    __ret = numeric;
	    break;
	  case LC_TIME:     
	    __ret = time; 
	    break;
#ifdef _GLIBCXX_HAVE_LC_MESSAGES
	  case LC_MESSAGES: 
	    __ret = messages;
	    break;
#endif	
	  case LC_ALL:      
	    __ret = all;
	    break;
	  default:
	    __throw_runtime_error(__N("locale::_S_normalize_category "
				  "category not found"));
	  }
      }
    return __ret;
  }

  // locale::facet
  __c_locale locale::facet::_S_c_locale;

  const char locale::facet::_S_c_name[2] = "C";

#ifdef __GTHREADS
  __gthread_once_t locale::facet::_S_once = __GTHREAD_ONCE_INIT;
#endif

  void
  locale::facet::_S_initialize_once()
  {
    // Initialize the underlying locale model.
    _S_create_c_locale(_S_c_locale, _S_c_name);
  }

  __c_locale
  locale::facet::_S_get_c_locale()
  {
#ifdef __GHTREADS
    if (__gthread_active_p())
      __gthread_once(&_S_once, _S_initialize_once);
    else
#endif
      {
	if (!_S_c_locale)
	  _S_initialize_once();
      }
    return _S_c_locale;
  }

  const char*
  locale::facet::_S_get_c_name()
  { return _S_c_name; }

  locale::facet::
  ~facet() { }

  // locale::_Impl
  locale::_Impl::
  ~_Impl() throw()
  {
    if (_M_facets)
      for (size_t __i = 0; __i < _M_facets_size; ++__i)
	if (_M_facets[__i])
	  _M_facets[__i]->_M_remove_reference();
    delete [] _M_facets;

    if (_M_caches)
      for (size_t __i = 0; __i < _M_facets_size; ++__i)
	if (_M_caches[__i])
	  _M_caches[__i]->_M_remove_reference(); 
    delete [] _M_caches;

    if (_M_names)
      for (size_t __i = 0; __i < _S_categories_size; ++__i)
	delete [] _M_names[__i];  
    delete [] _M_names;
  }

  // Clone existing _Impl object.
  locale::_Impl::
  _Impl(const _Impl& __imp, size_t __refs)
  : _M_refcount(__refs), _M_facets_size(__imp._M_facets_size)
  {
    _M_facets = _M_caches = 0;
    _M_names = 0;
    try
      {
	_M_facets = new const facet*[_M_facets_size];
	for (size_t __i = 0; __i < _M_facets_size; ++__i)
	  {
	    _M_facets[__i] = __imp._M_facets[__i];
	    if (_M_facets[__i])
	      _M_facets[__i]->_M_add_reference();
	  }
	_M_caches = new const facet*[_M_facets_size];
	for (size_t __i = 0; __i < _M_facets_size; ++__i)
	  {
	    _M_caches[__i] = __imp._M_caches[__i];
	    if (_M_caches[__i])
	      _M_caches[__i]->_M_add_reference(); 	
	  }
	_M_names = new char*[_S_categories_size];
	for (size_t __i = 0; __i < _S_categories_size; ++__i)
	  _M_names[__i] = 0;

	// Name the categories.
	for (size_t __i = 0; (__i < _S_categories_size
			      && __imp._M_names[__i]); ++__i)
	  {
	    const size_t __len = std::strlen(__imp._M_names[__i]) + 1;
	    _M_names[__i] = new char[__len];
	    std::memcpy(_M_names[__i], __imp._M_names[__i], __len);
	  }
      }
    catch(...)
      {
	this->~_Impl();
	__throw_exception_again;
      }
  }

  void
  locale::_Impl::
  _M_replace_category(const _Impl* __imp, const locale::id* const* __idpp)
  {
    for (; *__idpp; ++__idpp)
      _M_replace_facet(__imp, *__idpp);
  }
  
  void
  locale::_Impl::
  _M_replace_facet(const _Impl* __imp, const locale::id* __idp)
  {
    size_t __index = __idp->_M_id();
    if ((__index > (__imp->_M_facets_size - 1)) || !__imp->_M_facets[__index])
      __throw_runtime_error(__N("locale::_Impl::_M_replace_facet"));
    _M_install_facet(__idp, __imp->_M_facets[__index]); 
  }

  void
  locale::_Impl::
  _M_install_facet(const locale::id* __idp, const facet* __fp)
  {
    if (__fp)
      {
	size_t __index = __idp->_M_id();

	// Check size of facet vector to ensure adequate room.
	if (__index > _M_facets_size - 1)
	  {
	    const size_t __new_size = __index + 4;

	    // New facet array.
	    const facet** __oldf = _M_facets;
	    const facet** __newf;
	    __newf = new const facet*[__new_size]; 
	    for (size_t __i = 0; __i < _M_facets_size; ++__i)
	      __newf[__i] = _M_facets[__i];
	    for (size_t __i2 = _M_facets_size; __i2 < __new_size; ++__i2)
	      __newf[__i2] = 0;

	    // New cache array.
	    const facet** __oldc = _M_caches;
	    const facet** __newc;
	    try
	      {
		__newc = new const facet*[__new_size];
	      }
	    catch(...)
	      {
		delete [] __newf;
		__throw_exception_again;
	      }
	    for (size_t __i = 0; __i < _M_facets_size; ++__i)
	      __newc[__i] = _M_caches[__i];
	    for (size_t __i2 = _M_facets_size; __i2 < __new_size; ++__i2)
	      __newc[__i2] = 0;

	    _M_facets_size = __new_size;
	    _M_facets = __newf;
	    _M_caches = __newc;
	    delete [] __oldf;
	    delete [] __oldc;
	  }

	__fp->_M_add_reference();
	const facet*& __fpr = _M_facets[__index];
	if (__fpr)
	  {
	    // Replacing an existing facet. Order matters.
	    __fpr->_M_remove_reference();
	    __fpr = __fp;
	  }
	else
	  {
	    // Installing a newly created facet into an empty
	    // _M_facets container, say a newly-constructed,
	    // swanky-fresh _Impl.
	    _M_facets[__index] = __fp;
	  }

	// Ideally, it would be nice to only remove the caches that
	// are now incorrect. However, some of the caches depend on
	// multiple facets, and we only know about one facet
	// here. It's no great loss: the first use of the new facet
	// will create a new, correctly cached facet anyway.
	for (size_t __i = 0; __i < _M_facets_size; ++__i)
	  {
	    const facet* __cpr = _M_caches[__i];
	    if (__cpr)
	      {
		__cpr->_M_remove_reference();
		_M_caches[__i] = 0;
	      }
	  }
      }
  }

  // locale::id
  // Definitions for static const data members of locale::id
  _Atomic_word locale::id::_S_refcount;  // init'd to 0 by linker

  size_t
  locale::id::_M_id() const
  {
    if (!_M_index)
      _M_index = 1 + __gnu_cxx::__exchange_and_add(&_S_refcount, 1);
    return _M_index - 1;
  }
} // namespace std


