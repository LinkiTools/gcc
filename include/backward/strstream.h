// Backward-compat support -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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

/*
 * Copyright (c) 1998
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

// WARNING: The classes defined in this header are DEPRECATED.  This
// header is defined in section D.7.1 of the C++ standard, and it
// MAY BE REMOVED in a future standard revision.  You should use the
// header <sstream> instead.

#ifndef _CPP_BACKWARD_STRSTREAM_H
#define _CPP_BACKWARD_STRSTREAM_H 1

#include "backward_warning.h"
#include <iosfwd>
#include <ios>
#include <istream>
#include <ostream>
#include <string>

// Class strstreambuf, a streambuf class that manages an array of char.
// Note that this class is not a template.
class strstreambuf : public std::basic_streambuf<char>
{
public:                         
  // Types.
  typedef std::char_traits<char>              _Traits;
  typedef std::basic_streambuf<char, _Traits> _Base;

public:  
  // Constructor, destructor
  explicit strstreambuf(std::streamsize __initial_capacity = 0);
  strstreambuf(void* (*__alloc)(size_t), void (*__free)(void*));

  strstreambuf(char* __get, std::streamsize __n, char* __put = 0);
  strstreambuf(signed char* __get, std::streamsize __n, 
	       signed char* __put = 0);
  strstreambuf(unsigned char* __get, std::streamsize __n, 
	       unsigned char* __put=0);
  
  strstreambuf(const char* __get, std::streamsize __n);
  strstreambuf(const signed char* __get, std::streamsize __n);
  strstreambuf(const unsigned char* __get, std::streamsize __n);
  
  virtual ~strstreambuf();

public:
  void freeze(bool = true);
  char* str();
  int pcount() const;

protected:
  virtual int_type overflow(int_type __c  = _Traits::eof());
  virtual int_type pbackfail(int_type __c = _Traits::eof());
  virtual int_type underflow();
  virtual _Base* setbuf(char* __buf, std::streamsize __n);
  virtual pos_type seekoff(off_type __off, std::ios_base::seekdir __dir,
			   std::ios_base::openmode __mode
			   = std::ios_base::in | std::ios_base::out);
  virtual pos_type seekpos(pos_type __pos, std::ios_base::openmode __mode
			   = std::ios_base::in | std::ios_base::out);
  
private:  
  // Dynamic allocation, possibly using _M_alloc_fun and _M_free_fun.
  char* _M_alloc(size_t);
  void  _M_free(char*);
  
  // Helper function used in constructors.
  void _M_setup(char* __get, char* __put, std::streamsize __n);
  
private:  
  // Data members.
  void* (*_M_alloc_fun)(size_t);
  void  (*_M_free_fun)(void*);
  
  bool _M_dynamic  : 1;
  bool _M_frozen   : 1;
  bool _M_constant : 1;
};

// Class istrstream, an istream that manages a strstreambuf.
class istrstream : public std::basic_istream<char>
{
public:
  explicit istrstream(char*);
  explicit istrstream(const char*);
  istrstream(char* , std::streamsize);
  istrstream(const char*, std::streamsize);
  virtual ~istrstream();
  
  strstreambuf* rdbuf() const;
  char* str();
  
private:
  strstreambuf _M_buf;
};

// Class ostrstream
class ostrstream : public std::basic_ostream<char>
{
public:
  ostrstream();
  ostrstream(char*, int, std::ios_base::openmode = std::ios_base::out);
  virtual ~ostrstream();
  
  strstreambuf* rdbuf() const;
  void freeze(bool = true);
  char* str();
  int pcount() const;
  
private:
  strstreambuf _M_buf;
};

// Class strstream
class strstream : public std::basic_iostream<char>
{
public:
  typedef char                        char_type;
  typedef std::char_traits<char>::int_type int_type;
  typedef std::char_traits<char>::pos_type pos_type;
  typedef std::char_traits<char>::off_type off_type;
  
  strstream();
  strstream(char*, int, 
	    std::ios_base::openmode = std::ios_base::in | std::ios_base::out);
  virtual ~strstream();
  
  strstreambuf* rdbuf() const;
  void freeze(bool = true);
  int pcount() const;
  char* str();
  
private:
  strstreambuf _M_buf;
};
#endif 
