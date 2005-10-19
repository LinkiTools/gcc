// Short-string-optimized string -*- C++ -*-

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

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/** @file ext/sso_string.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _SSO_STRING_H
#define _SSO_STRING_H 1

#include <ext/string_util.h>

namespace __gnu_cxx
{
  template<typename _CharT, typename _Traits, typename _Alloc>
    class __sso_string
    : protected __string_utility<_CharT, _Traits, _Alloc>
    {
    public:
      typedef _Traits					    traits_type;
      typedef typename _Traits::char_type		    value_type;
      typedef _Alloc					    allocator_type;

      typedef typename __string_utility<_CharT, _Traits, _Alloc>::
        _CharT_alloc_type                                   _CharT_alloc_type;
      typedef typename _CharT_alloc_type::size_type	    size_type;
      
    private:
      // The maximum number of individual char_type elements of an
      // individual string is determined by _S_max_size. This is the
      // value that will be returned by max_size().  (Whereas npos
      // is the maximum number of bytes the allocator can allocate.)
      // If one was to divvy up the theoretical largest size string,
      // with a terminating character and m _CharT elements, it'd
      // look like this:
      // npos = m * sizeof(_CharT) + sizeof(_CharT)
      // Solving for m:
      // m = npos / sizeof(_CharT) - 1
      // In addition, this implementation quarters this amount.
      enum { _S_max_size = ((static_cast<size_type>(-1)
			     / sizeof(_CharT)) - 1) / 4 };

      // Use empty-base optimization: http://www.cantrip.org/emptyopt.html
      struct _Alloc_hider : _Alloc
      {
	_Alloc_hider(const _Alloc& __a, _CharT* __ptr)
	: _Alloc(__a), _M_p(__ptr) { }

	_CharT* _M_p; // The actual data.
      };

      // Data Members (private):
      _Alloc_hider	        _M_dataplus;
      size_type                 _M_string_length;

      enum { _S_local_capacity = 15 };
      
      union
      {
	_CharT                  _M_local_data[_S_local_capacity + 1];
	size_type               _M_allocated_capacity;
      };

      _CharT*
      _M_data(_CharT* __p)
      { return (_M_dataplus._M_p = __p); }

      void
      _M_length(size_type __length)
      { _M_string_length = __length; }

      void
      _M_capacity(size_type __capacity)
      { _M_allocated_capacity = __capacity; }

      bool
      _M_is_local() const
      { return _M_data() == _M_local_data; }

      // Create & Destroy
      _CharT*
      _M_create(size_type&, size_type);
      
      void
      _M_dispose() throw()
      {
	if (!_M_is_local())
	  _M_destroy(_M_allocated_capacity + 1);
      }

      void
      _M_destroy(size_type) throw();

      // _M_construct_aux is used to implement the 21.3.1 para 15 which
      // requires special behaviour if _InIter is an integral type
      template<class _InIterator>
        void
        _M_construct_aux(_InIterator __beg, _InIterator __end, __false_type)
	{
          typedef typename iterator_traits<_InIterator>::iterator_category _Tag;
          _M_construct(__beg, __end, _Tag());
	}

      template<class _InIterator>
        void
        _M_construct_aux(_InIterator __beg, _InIterator __end, __true_type)
	{ _M_construct(static_cast<size_type>(__beg),
		       static_cast<value_type>(__end)); }

      template<class _InIterator>
        void
        _M_construct(_InIterator __beg, _InIterator __end)
	{
	  typedef typename std::__is_integer<_InIterator>::__type _Integral;
	  _M_construct_aux(__beg, __end, _Integral());
        }

      // For Input Iterators, used in istreambuf_iterators, etc.
      template<class _InIterator>
        void
        _M_construct(_InIterator __beg, _InIterator __end,
		     std::input_iterator_tag);
      
      // For forward_iterators up to random_access_iterators, used for
      // string::iterator, _CharT*, etc.
      template<class _FwdIterator>
        void
        _M_construct(_FwdIterator __beg, _FwdIterator __end,
		     std::forward_iterator_tag);

      void
      _M_construct(size_type __req, _CharT __c);

    public:
      size_type
      _M_max_size() const
      { return size_type(_S_max_size); }

      _CharT*
      _M_data() const
      { return _M_dataplus._M_p; }

      size_type
      _M_length() const
      { return _M_string_length; }

      size_type
      _M_capacity() const
      {
	return _M_is_local() ? size_type(_S_local_capacity)
	                     : _M_allocated_capacity; 
      }

      bool
      _M_is_shared() const
      { return false; }

      bool
      _M_is_leaked() const
      { return false; }

      void
      _M_set_sharable() { }

      void
      _M_set_leaked() { }

      void
      _M_set_length(size_type __n)
      {
	_M_length(__n);
	// grrr. (per 21.3.4)
	// You cannot leave those LWG people alone for a second.
	traits_type::assign(_M_data()[__n], _CharT());
      }

      void
      _M_leak() { }

      __sso_string()
      : _M_dataplus(_Alloc(), _M_local_data)
      { _M_set_length(0); }

      __sso_string(const _Alloc& __a);

      __sso_string(const __sso_string& __rcs);

      __sso_string(size_type __n, _CharT __c, const _Alloc& __a);

      template<typename _InputIterator>
        __sso_string(_InputIterator __beg, _InputIterator __end,
		     const _Alloc& __a);

      ~__sso_string()
      { _M_dispose(); }

      allocator_type
      _M_get_allocator() const
      { return _M_dataplus; }

      void
      _M_swap(__sso_string& __rcs);

      void
      _M_assign(const __sso_string& __rcs);

      void
      _M_reserve(size_type __res);

      void
      _M_mutate(size_type __pos, size_type __len1, size_type __len2);
    };

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string<_CharT, _Traits, _Alloc>::
    _M_destroy(size_type __size) throw()
    { _CharT_alloc_type(_M_get_allocator()).deallocate(_M_data(), __size); }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string<_CharT, _Traits, _Alloc>::
    _M_swap(__sso_string& __rcs)
    {
      const bool __local = _M_is_local();
      const bool __rcs_local = __rcs._M_is_local();
      
      if (__local && __rcs_local)
	{
	  _CharT __tmp_data[_S_local_capacity + 1];
	  const size_type __tmp_length = __rcs._M_length();
	  _S_copy(__tmp_data, __rcs._M_data(), __rcs._M_length() + 1);
	  __rcs._M_length(_M_length());
	  _S_copy(__rcs._M_data(), _M_data(), _M_length() + 1);
	  _M_length(__tmp_length);
	  _S_copy(_M_data(), __tmp_data, __tmp_length + 1);
	}
      else if (__local && !__rcs_local)
	{
	  const size_type __tmp_capacity = __rcs._M_allocated_capacity;
	  const size_type __tmp_length = __rcs._M_length();
	  _CharT* __tmp_ptr = __rcs._M_data();
	  __rcs._M_data(__rcs._M_local_data);
	  _S_copy(__rcs._M_data(), _M_data(), _M_length() + 1);
	  __rcs._M_length(_M_length());
	  _M_data(__tmp_ptr);
	  _M_length(__tmp_length);
	  _M_capacity(__tmp_capacity);
	}
      else if (!__local && __rcs_local)
	{
	  const size_type __tmp_capacity = _M_allocated_capacity;
	  const size_type __tmp_length = _M_length();
	  _CharT* __tmp_ptr = _M_data();
	  _M_data(_M_local_data);
	  _S_copy(_M_data(), __rcs._M_data(), __rcs._M_length() + 1);
	  _M_length(__rcs._M_length());
	  __rcs._M_data(__tmp_ptr);
	  __rcs._M_length(__tmp_length);
	  __rcs._M_capacity(__tmp_capacity);
	}
      else
	{
	  const size_type __tmp_capacity = _M_allocated_capacity;
	  const size_type __tmp_length = _M_length();
	  _CharT* __tmp_ptr = _M_data();
	  _M_data(__rcs._M_data());
	  _M_length(__rcs._M_length());
	  _M_capacity(__rcs._M_allocated_capacity);
	  __rcs._M_data(__tmp_ptr);
	  __rcs._M_length(__tmp_length);
	  __rcs._M_capacity(__tmp_capacity);
	}
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    _CharT*
    __sso_string<_CharT, _Traits, _Alloc>::
    _M_create(size_type& __capacity, size_type __old_capacity)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 83.  String::npos vs. string::max_size()
      if (__capacity > size_type(_S_max_size))
	std::__throw_length_error(__N("__sso_string::_M_create"));

      // The below implements an exponential growth policy, necessary to
      // meet amortized linear time requirements of the library: see
      // http://gcc.gnu.org/ml/libstdc++/2001-07/msg00085.html.
      // It's active for allocations requiring an amount of memory above
      // system pagesize. This is consistent with the requirements of the
      // standard: http://gcc.gnu.org/ml/libstdc++/2001-07/msg00130.html
      if (__capacity > __old_capacity && __capacity < 2 * __old_capacity)
	__capacity = 2 * __old_capacity;

      // NB: Need an array of char_type[__capacity], plus a terminating
      // null char_type() element.
      return _CharT_alloc_type(_M_get_allocator()).allocate(__capacity + 1);
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    __sso_string<_CharT, _Traits, _Alloc>::
    __sso_string(const _Alloc& __a)
    : _M_dataplus(__a, _M_local_data)
    { _M_set_length(0); }

  template<typename _CharT, typename _Traits, typename _Alloc>
    __sso_string<_CharT, _Traits, _Alloc>::
    __sso_string(const __sso_string& __rcs)
    : _M_dataplus(__rcs._M_get_allocator(), _M_local_data)
    { _M_construct(__rcs._M_data(), __rcs._M_data() + __rcs._M_length()); }

  template<typename _CharT, typename _Traits, typename _Alloc>
    __sso_string<_CharT, _Traits, _Alloc>::
    __sso_string(size_type __n, _CharT __c, const _Alloc& __a)
    : _M_dataplus(__a, _M_local_data)
    { _M_construct(__n, __c); }

  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InputIterator>
    __sso_string<_CharT, _Traits, _Alloc>::
    __sso_string(_InputIterator __beg, _InputIterator __end, const _Alloc& __a)
    : _M_dataplus(__a, _M_local_data)
    { _M_construct(__beg, __end); }

  // NB: This is the special case for Input Iterators, used in
  // istreambuf_iterators, etc.
  // Input Iterators have a cost structure very different from
  // pointers, calling for a different coding style.
  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InIterator>
      void
      __sso_string<_CharT, _Traits, _Alloc>::
      _M_construct(_InIterator __beg, _InIterator __end,
		   std::input_iterator_tag)
      {
	// Avoid reallocation for common case.
	size_type __len = 0;
	size_type __capacity = size_type(_S_local_capacity);

	while (__beg != __end && __len < __capacity)
	  {
	    _M_data()[__len++] = *__beg;
	    ++__beg;
	  }
	
	try
	  {
	    while (__beg != __end)
	      {
		if (__len == __capacity)
		  {
		    // Allocate more space.
		    __capacity = __len + 1;
		    _CharT* __another = _M_create(__capacity, __len);
		    _S_copy(__another, _M_data(), __len);
		    _M_dispose();
		    _M_data(__another);
		    _M_capacity(__capacity);
		  }
		_M_data()[__len++] = *__beg;
		++__beg;
	      }
	  }
	catch(...)
	  {
	    _M_dispose();
	    __throw_exception_again;
	  }

	_M_set_length(__len);
      }

  template<typename _CharT, typename _Traits, typename _Alloc>
    template <typename _InIterator>
      void
      __sso_string<_CharT, _Traits, _Alloc>::
      _M_construct(_InIterator __beg, _InIterator __end,
		   std::forward_iterator_tag)
      {
	// NB: Not required, but considered best practice.
	if (__builtin_expect(__is_null_pointer(__beg) && __beg != __end, 0))
	  std::__throw_logic_error(__N("__sso_string::"
				       "_M_construct NULL not valid"));

	size_type __dnew = static_cast<size_type>(std::distance(__beg, __end));

	if (__dnew > size_type(_S_local_capacity))
	  {
	    _M_data(_M_create(__dnew, size_type(0)));
	    _M_capacity(__dnew);
	  }

	// Check for out_of_range and length_error exceptions.
	try
	  { _S_copy_chars(_M_data(), __beg, __end); }
	catch(...)
	  {
	    _M_dispose();
	    __throw_exception_again;
	  }

	_M_set_length(__dnew);
      }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string<_CharT, _Traits, _Alloc>::
    _M_construct(size_type __n, _CharT __c)
    {
      if (__n > size_type(_S_local_capacity))
	{
	  _M_data(_M_create(__n, size_type(0)));
	  _M_capacity(__n);
	}

      if (__n)
	_S_assign(_M_data(), __n, __c);

      _M_set_length(__n);
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string<_CharT, _Traits, _Alloc>::
    _M_assign(const __sso_string& __rcs)
    {
      if (this != &__rcs)
	{
	  size_type __size = __rcs._M_length();

	  _CharT* __tmp = _M_local_data;
	  if (__size > size_type(_S_local_capacity))
	    __tmp = _M_create(__size, size_type(0));

	  _M_dispose();
	  _M_data(__tmp);

	  if (__size)
	    _S_copy(_M_data(), __rcs._M_data(), __size);

	  if (!_M_is_local())
	    _M_capacity(__size);

	  _M_set_length(__size);
	}
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string<_CharT, _Traits, _Alloc>::
    _M_reserve(size_type __res)
    {
      const size_type __capacity = _M_capacity();
      if (__res != __capacity)
	{
	  // Make sure we don't shrink below the current size.
	  if (__res < _M_length())
	    __res = _M_length();

	  if (__res > __capacity
	      || __res > size_type(_S_local_capacity))
	    {
	      _CharT* __tmp = _M_create(__res, __capacity);
	      if (_M_length())
		_S_copy(__tmp, _M_data(), _M_length());
	      _M_dispose();
	      _M_data(__tmp);
	      _M_capacity(__res);
	    }
	  else if (!_M_is_local())
	    {
	      const size_type __tmp_capacity = _M_allocated_capacity;
	      if (_M_length())
		_S_copy(_M_local_data, _M_data(), _M_length());
	      _M_destroy(__tmp_capacity + 1);
	      _M_data(_M_local_data);
	    }	  
	  
	  _M_set_length(_M_length());
	}
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string<_CharT, _Traits, _Alloc>::
    _M_mutate(size_type __pos, size_type __len1, size_type __len2)
    {
      const size_type __old_size = _M_length();
      const size_type __new_size = __old_size + __len2 - __len1;
      const size_type __how_much = __old_size - __pos - __len1;
      
      if (__new_size > _M_capacity())
	{
	  // Must reallocate.
	  size_type __new_capacity = __new_size;
	  _CharT* __r = _M_create(__new_capacity, _M_capacity());

	  if (__pos)
	    _S_copy(__r, _M_data(), __pos);
	  if (__how_much)
	    _S_copy(__r + __pos + __len2,
		    _M_data() + __pos + __len1, __how_much);

	  _M_dispose();
	  _M_data(__r);
	  _M_capacity(__new_capacity);
	}
      else if (__how_much && __len1 != __len2)
	{
	  // Work in-place.
	  _S_move(_M_data() + __pos + __len2,
		  _M_data() + __pos + __len1, __how_much);
	}

      _M_set_length(__new_size);
    }
} // namespace __gnu_cxx

#endif /* _SSO_STRING_H */
