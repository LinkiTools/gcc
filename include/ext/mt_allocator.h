// MT-optimized allocator -*- C++ -*-

// Copyright (C) 2003, 2004 Free Software Foundation, Inc.
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

/** @file ext/mt_allocator.h
 *  This file is a GNU extension to the Standard C++ Library.
 *  You should only include this header if you are using GCC 3 or later.
 */

#ifndef _MT_ALLOCATOR_H
#define _MT_ALLOCATOR_H 1

#include <new>
#include <cstdlib>
#include <bits/functexcept.h>
#include <bits/gthr.h>
#include <bits/atomicity.h>

namespace __gnu_cxx
{
  /**
   *  This is a fixed size (power of 2) allocator which - when
   *  compiled with thread support - will maintain one freelist per
   *  size per thread plus a "global" one. Steps are taken to limit
   *  the per thread freelist sizes (by returning excess back to
   *  "global").
   *
   *  Usage examples:
   *  @code
   *    vector<int, __gnu_cxx::__mt_alloc<int> > v1;
   *
   *    typedef __gnu_cxx::__mt_alloc<char> > string_allocator;
   *    std::basic_string<char, std::char_traits<char>, string_allocator> s1;
   *  @endcode
   */
  template<typename _Tp>
    class __mt_alloc
    {
    public:
      typedef size_t     size_type;
      typedef ptrdiff_t  difference_type;
      typedef _Tp*       pointer;
      typedef const _Tp* const_pointer;
      typedef _Tp&       reference;
      typedef const _Tp& const_reference;
      typedef _Tp        value_type;

      template<typename _Tp1>
        struct rebind
        { typedef __mt_alloc<_Tp1> other; };

      __mt_alloc() throw() 
      {
	// XXX
      }

      __mt_alloc(const __mt_alloc&) throw()
      {
	// XXX
      }

      template<typename _Tp1>
        __mt_alloc(const __mt_alloc<_Tp1>&) throw()
        {
	  // XXX
	}

      ~__mt_alloc() throw() { }

      pointer
      address(reference __x) const { return &__x; }

      const_pointer
      address(const_reference __x) const { return &__x; }

      size_type
      max_size() const throw() 
      { return size_t(-1) / sizeof(_Tp); }

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 402. wrong new expression in [some_] allocator::construct
      void 
      construct(pointer __p, const _Tp& __val) 
      { ::new(__p) _Tp(__val); }

      void 
      destroy(pointer __p) { __p->~_Tp(); }

    private:
      /*
       * We need to create the initial lists and set up some variables
       * before we can answer to the first request for memory.
       * The initialization of these variables is done at file scope
       * below class declaration.
       */
#ifdef __GTHREADS
      static __gthread_once_t _S_once_mt;
#endif
      static bool volatile _S_initialized;

      /*
       * If the env var GLIBCXX_FORCE_NEW is set during _S_init()
       * we set this var to true which causes all allocations to use new()
       */
      static bool _S_force_new;

      /*
       * Using short int as type for the binmap implies we are never caching
       * blocks larger than 65535 with this allocator
       */
      typedef unsigned short int binmap_type;
      static binmap_type* _S_binmap;

      static void _S_init();

      /*
       * Variables used to "tune" the behavior of the allocator, assigned
       * and explained in detail below.
       */
      static size_t _S_max_bytes;
      static size_t _S_chunk_size;
      static size_t _S_max_threads;
      static size_t _S_no_of_bins;
      static size_t _S_freelist_headroom;

      /*
       * Each requesting thread is assigned an id ranging from 1 to
       * _S_max_threads. Thread id 0 is used as a global memory pool.
       * In order to get constant performance on the thread assignment
       * routine, we keep a list of free ids. When a thread first requests
       * memory we remove the first record in this list and stores the address
       * in a __gthread_key. When initializing the __gthread_key
       * we specify a destructor. When this destructor (i.e. the thread dies)
       * is called, we return the thread id to the front of this list.
       */
#ifdef __GTHREADS
      struct thread_record
      {
        /*
         * Points to next free thread id record. NULL if last record in list.
         */
        thread_record* volatile next;

        /*
         * Thread id ranging from 1 to _S_max_threads.
         */
        size_t id;
      };

      static thread_record* volatile _S_thread_freelist_first;
      static __gthread_mutex_t _S_thread_freelist_mutex;
      static void _S_thread_key_destr(void* freelist_pos);
      static __gthread_key_t _S_thread_key;
      static size_t _S_get_thread_id();
#endif

      struct block_record
      {
        /*
         * Points to the next block_record for its thread_id.
         */
        block_record* volatile next;

        /*
         * The thread id of the thread which has requested this block.
         */
#ifdef __GTHREADS
        size_t thread_id;
#endif
      };

      struct bin_record
      {
        /*
         * An "array" of pointers to the first free block for each
         * thread id. Memory to this "array" is allocated in _S_init()
         * for _S_max_threads + global pool 0.
         */
        block_record** volatile first;

        /*
         * An "array" of counters used to keep track of the amount of blocks
         * that are on the freelist/used for each thread id.
         * Memory to these "arrays" is allocated in _S_init()
         * for _S_max_threads + global pool 0.
         */
        size_t* volatile free;
        size_t* volatile used;

        /*
         * Each bin has its own mutex which is used to ensure data integrity
         * while changing "ownership" on a block.
         * The mutex is initialized in _S_init().
         */
#ifdef __GTHREADS
        __gthread_mutex_t* mutex;
#endif
      };

      /*
       * An "array" of bin_records each of which represents a specific
       * power of 2 size. Memory to this "array" is allocated in _S_init().
       */
      static bin_record* volatile _S_bin;

    public:
      pointer
      allocate(size_t __n, const void* = 0)
      {
        /*
         * Although the test in __gthread_once() would suffice, we
         * wrap test of the once condition in our own unlocked
         * check. This saves one function call to pthread_once()
         * (which itself only tests for the once value unlocked anyway
         * and immediately returns if set)
         */
        if (!_S_initialized)
          {
#ifdef __GTHREADS
            if (__gthread_active_p())
              __gthread_once(&_S_once_mt, _S_init);
            else
#endif
              {
                _S_max_threads = 0;
                _S_init();
              }
          }

        /*
         * Requests larger than _S_max_bytes are handled by
         * new/delete directly
         */
        if (__n * sizeof(_Tp) > _S_max_bytes || _S_force_new)
          {
            void* __ret = ::operator new(__n * sizeof(_Tp));
            if (!__ret)
              std::__throw_bad_alloc();
            return static_cast<_Tp*>(__ret);
          }

        /*
         * Round up to power of 2 and figure out which bin to use
         */
        size_t bin = _S_binmap[__n * sizeof(_Tp)];

#ifdef __GTHREADS
        size_t thread_id = _S_get_thread_id();
#else
        size_t thread_id = 0;
#endif

        block_record* block = NULL;

        /*
         * Find out if we have blocks on our freelist.
         * If so, go ahead and use them directly without
         * having to lock anything.
         */
        if (_S_bin[bin].first[thread_id] == NULL)
          {
            /*
             * Are we using threads?
             * - Yes, check if there are free blocks on the global
             *   list. If so, grab up to block_count blocks in one
             *   lock and change ownership. If the global list is 
             *   empty, we allocate a new chunk and add those blocks 
             *   directly to our own freelist (with us as owner).
             * - No, all operations are made directly to global pool 0
             *   no need to lock or change ownership but check for free
             *   blocks on global list (and if not add new ones) and
             *   get the first one.
             */
#ifdef __GTHREADS
            if (__gthread_active_p())
              {
                size_t bin_t = 1 << bin;
                size_t block_count =
                  _S_chunk_size /(bin_t + sizeof(block_record));

                __gthread_mutex_lock(_S_bin[bin].mutex);

                if (_S_bin[bin].first[0] == NULL)
                  {
                    /*
                     * No need to hold the lock when we are adding a
                     * whole chunk to our own list
                     */
                    __gthread_mutex_unlock(_S_bin[bin].mutex);

                    _S_bin[bin].first[thread_id] =
                     static_cast<block_record*>(::operator new(_S_chunk_size));

                    if (!_S_bin[bin].first[thread_id])
                      std::__throw_bad_alloc();

                    _S_bin[bin].free[thread_id] = block_count;

                    block_count--;
                    block = _S_bin[bin].first[thread_id];

                    while (block_count > 0)
                      {
                        block->next = (block_record*)((char*)block +
                                      (bin_t + sizeof(block_record)));
                        block->thread_id = thread_id;
                        block = block->next;
                        block_count--;
                      }

                    block->next = NULL;
                    block->thread_id = thread_id;
                  }
                else
                  {
                    size_t global_count = 0;

                    block_record* tmp;

                    while( _S_bin[bin].first[0] != NULL &&
                           global_count < block_count )
                      {
                        tmp = _S_bin[bin].first[0]->next;

                        block = _S_bin[bin].first[0];

                        if (_S_bin[bin].first[thread_id] == NULL)
                          {
                            _S_bin[bin].first[thread_id] = block;
                            block->next = NULL;
                          }
                        else
                          {
                            block->next = _S_bin[bin].first[thread_id];
                            _S_bin[bin].first[thread_id] = block;
                          }

                        block->thread_id = thread_id;

                        _S_bin[bin].free[thread_id]++;

                        _S_bin[bin].first[0] = tmp;

                        global_count++;
                      }

                    __gthread_mutex_unlock(_S_bin[bin].mutex);
                  }

                /*
                 * Return the first newly added block in our list and
                 * update the counters
                 */
                block = _S_bin[bin].first[thread_id];
                _S_bin[bin].first[thread_id] = 
                  _S_bin[bin].first[thread_id]->next;

                _S_bin[bin].free[thread_id]--;
                _S_bin[bin].used[thread_id]++;
              }
            else
#endif
              {
                _S_bin[bin].first[0] = 
                  static_cast<block_record*>(::operator new(_S_chunk_size));

                if (!_S_bin[bin].first[0])
                  std::__throw_bad_alloc();

                size_t bin_t = 1 << bin;
                size_t block_count = 
                  _S_chunk_size / (bin_t + sizeof(block_record));

                block_count--;
                block = _S_bin[bin].first[0];

                while (block_count > 0)
                  {
                    block->next = (block_record*)((char*)block +
                                  (bin_t + sizeof(block_record)));
                    block = block->next;
                    block_count--;
                  }

                block->next = NULL;

                block = _S_bin[bin].first[0];

                /*
                 * Remove from list
                 */
                _S_bin[bin].first[0] = _S_bin[bin].first[0]->next;
              }
          }
        else
          {
            /*
             * "Default" operation - we have blocks on our own freelist
             * grab the first record and update the counters.
             */
            block = _S_bin[bin].first[thread_id];

            _S_bin[bin].first[thread_id] = _S_bin[bin].first[thread_id]->next;

#ifdef __GTHREADS
            if (__gthread_active_p())
              {
                _S_bin[bin].free[thread_id]--;
                _S_bin[bin].used[thread_id]++;
              }
#endif
          }

        return static_cast<_Tp*>(static_cast<void*>((char*)block + 
                                                    sizeof(block_record)));
      }

      void
      deallocate(pointer __p, size_type __n)
      {
        /*
         * Requests larger than _S_max_bytes are handled by
         * operators new/delete directly
         */
        if (__n * sizeof(_Tp) > _S_max_bytes || _S_force_new)
          {
            ::operator delete(__p);
            return;
          }

        /*
         * Round up to power of 2 and figure out which bin to use
         */
        size_t bin = _S_binmap[__n * sizeof(_Tp)];

#ifdef __GTHREADS
        size_t thread_id = _S_get_thread_id();
#else
        size_t thread_id = 0;
#endif

        block_record* block = (block_record*)((char*)__p
                                             - sizeof(block_record));

#ifdef __GTHREADS
        if (__gthread_active_p())
          {
            /*
             * Calculate the number of records to remove from our freelist
             */
            int remove = _S_bin[bin].free[thread_id] -
                         (_S_bin[bin].used[thread_id] / _S_freelist_headroom);

            /*
             * The calculation above will almost always tell us to
             * remove one or two records at a time, but this creates
             * too much contention when locking and therefore we
             * wait until the number of records is "high enough".
             */
            if (remove > (int)(100 * (_S_no_of_bins - bin)) &&
                remove > (int)(_S_bin[bin].free[thread_id] /
                               _S_freelist_headroom))
              {
                __gthread_mutex_lock(_S_bin[bin].mutex);

                block_record* tmp;

                while (remove > 0)
                  {
                    tmp = _S_bin[bin].first[thread_id]->next;

                    if (_S_bin[bin].first[0] == NULL)
                      {
                        _S_bin[bin].first[0] = _S_bin[bin].first[thread_id];
                        _S_bin[bin].first[0]->next = NULL;
                      }
                    else
                      {
                        _S_bin[bin].first[thread_id]->next = _S_bin[bin].first[0];
                        _S_bin[bin].first[0] = _S_bin[bin].first[thread_id];
                      }

                    _S_bin[bin].first[thread_id] = tmp;

                    _S_bin[bin].free[thread_id]--;

                    remove--;
                  }

                __gthread_mutex_unlock(_S_bin[bin].mutex);
              }

            /*
             * Return this block to our list and update
             * counters and owner id as needed
             */
            if (_S_bin[bin].first[thread_id] == NULL)
              {
                _S_bin[bin].first[thread_id] = block;
                block->next = NULL;
              }
            else
              {
                block->next = _S_bin[bin].first[thread_id];
                _S_bin[bin].first[thread_id] = block;
              }

            _S_bin[bin].free[thread_id]++;

            if (thread_id == block->thread_id)
              _S_bin[bin].used[thread_id]--;
            else
              {
                _S_bin[bin].used[block->thread_id]--;
                block->thread_id = thread_id;
              }
          }
        else
#endif
          {
            /*
             * Single threaded application - return to global pool
             */
            if (_S_bin[bin].first[0] == NULL)
              {
                _S_bin[bin].first[0] = block;
                block->next = NULL;
              }
            else
              {
                block->next = _S_bin[bin].first[0];
                _S_bin[bin].first[0] = block;
              }
          }
      }
    };

  template<typename _Tp>
    void
    __mt_alloc<_Tp>::
    _S_init()
    {
      if (getenv("GLIBCXX_FORCE_NEW"))
        {
          _S_force_new = true;
          _S_initialized = true;

          /*
           * Since none of the code in allocate/deallocate ever will be 
           * executed due to that the GLIBCXX_FORCE_NEW flag is set
           * there is no need to create the internal structures either.
           */
          return;
        }

      /*
       * Calculate the number of bins required based on _S_max_bytes,
       * _S_no_of_bins is initialized to 1 below.
       */
      {
        size_t bin_t = 1;
        while (_S_max_bytes > bin_t)
          {
            bin_t = bin_t << 1;
            _S_no_of_bins++;
          }
      }

      /*
       * Setup the bin map for quick lookup of the relevant bin
       */
      _S_binmap = (binmap_type*)
        ::operator new ((_S_max_bytes + 1) * sizeof(binmap_type));

      if (!_S_binmap)
        std::__throw_bad_alloc();

      binmap_type* bp_t = _S_binmap;
      binmap_type bin_max_t = 1;
      binmap_type bin_t = 0;
      for (binmap_type ct = 0; ct <= _S_max_bytes; ct++)
        {
          if (ct > bin_max_t)
            {
              bin_max_t <<= 1;
              bin_t++;
            }
          *bp_t++ = bin_t;
        }

      /*
       * If __gthread_active_p() create and initialize the list of
       * free thread ids. Single threaded applications use thread id 0
       * directly and have no need for this.
       */
#ifdef __GTHREADS
      if (__gthread_active_p())
        {
          _S_thread_freelist_first =
            static_cast<thread_record*>(::operator 
              new(sizeof(thread_record) * _S_max_threads));

          if (!_S_thread_freelist_first)
            std::__throw_bad_alloc();

          /*
           * NOTE! The first assignable thread id is 1 since the global
           * pool uses id 0
           */
          size_t i;
          for (i = 1; i < _S_max_threads; i++)
            {
              _S_thread_freelist_first[i - 1].next = 
                &_S_thread_freelist_first[i];

              _S_thread_freelist_first[i - 1].id = i;
            }

          /*
           * Set last record
           */
          _S_thread_freelist_first[i - 1].next = NULL;
          _S_thread_freelist_first[i - 1].id = i;

          /*
           * Initialize per thread key to hold pointer to
           * _S_thread_freelist
           */
          __gthread_key_create(&_S_thread_key, _S_thread_key_destr);
        }
#endif

      /*
       * Initialize _S_bin and its members
       */
      _S_bin = static_cast<bin_record*>(::operator 
        new(sizeof(bin_record) * _S_no_of_bins));

      if (!_S_bin)
        std::__throw_bad_alloc();

      std::size_t __n = 1;

#ifdef __GTHREADS
      if (__gthread_active_p())
        __n = _S_max_threads + 1;
#endif

      for (size_t bin = 0; bin < _S_no_of_bins; bin++)
        {
          _S_bin[bin].first = static_cast<block_record**>(::operator 
            new(sizeof(block_record*) * __n));

          if (!_S_bin[bin].first)
            std::__throw_bad_alloc();

#ifdef __GTHREADS
          if (__gthread_active_p())
            {
              _S_bin[bin].free = static_cast<size_t*>(::operator 
                new(sizeof(size_t) * __n));

              if (!_S_bin[bin].free)
                std::__throw_bad_alloc();

              _S_bin[bin].used = static_cast<size_t*>(::operator 
                new(sizeof(size_t) * __n));

              if (!_S_bin[bin].used)
                std::__throw_bad_alloc();

              _S_bin[bin].mutex = static_cast<__gthread_mutex_t*>(::operator 
                new(sizeof(__gthread_mutex_t)));

#ifdef __GTHREAD_MUTEX_INIT
              {
                // Do not copy a POSIX/gthr mutex once in use.
                __gthread_mutex_t __tmp = __GTHREAD_MUTEX_INIT;
                *_S_bin[bin].mutex = __tmp;
              }
#else
              { __GTHREAD_MUTEX_INIT_FUNCTION (_S_bin[bin].mutex); }
#endif
            }
#endif

          for (size_t thread = 0; thread < __n; thread++)
            {
              _S_bin[bin].first[thread] = NULL;
#ifdef __GTHREADS
              if (__gthread_active_p())
                {
                  _S_bin[bin].free[thread] = 0;
                  _S_bin[bin].used[thread] = 0;
                }
#endif
            }
        }

      _S_initialized = true;
    }

#ifdef __GTHREADS
  template<typename _Tp>
    void
    __mt_alloc<_Tp>::
    _S_thread_key_destr(void* freelist_pos)
    {
      /*
       * Return this thread id record to front of thread_freelist
       */
      __gthread_mutex_lock(&_S_thread_freelist_mutex);
      ((thread_record*)freelist_pos)->next = _S_thread_freelist_first;
      _S_thread_freelist_first = (thread_record*)freelist_pos;
      __gthread_mutex_unlock(&_S_thread_freelist_mutex);
    }

  template<typename _Tp>
    size_t
    __mt_alloc<_Tp>::
    _S_get_thread_id()
    {
      /*
       * If we have thread support and it's active we check the thread
       * key value and return it's id or if it's not set we take the
       * first record from _S_thread_freelist and sets the key and
       * returns it's id.
       */
      if (__gthread_active_p())
        {
          thread_record* freelist_pos;

          if ((freelist_pos =
              (thread_record*)__gthread_getspecific(_S_thread_key)) == NULL)
            {
              /*
               * Since _S_max_threads must be larger than the
               * theoretical max number of threads of the OS the list
               * can never be empty.
               */
              __gthread_mutex_lock(&_S_thread_freelist_mutex);
              freelist_pos = _S_thread_freelist_first;
              _S_thread_freelist_first = _S_thread_freelist_first->next;
              __gthread_mutex_unlock(&_S_thread_freelist_mutex);

              __gthread_setspecific(_S_thread_key, (void*)freelist_pos);
            }

          return freelist_pos->id;
        }

      /*
       * Otherwise (no thread support or inactive) all requests are
       * served from the global pool 0.
       */
      return 0;
    }

  template<typename _Tp> __gthread_once_t
  __mt_alloc<_Tp>::_S_once_mt = __GTHREAD_ONCE_INIT;
#endif

  template<typename _Tp> 
  bool volatile __mt_alloc<_Tp>::_S_initialized = false;

  template<typename _Tp> bool
  __mt_alloc<_Tp>::_S_force_new = false;

  template<typename _Tp> typename __mt_alloc<_Tp>::binmap_type*
  __mt_alloc<_Tp>::_S_binmap = NULL;

  /*
   * Allocation requests (after round-up to power of 2) below this
   * value will be handled by the allocator. A raw new/ call
   * will be used for requests larger than this value.
   */
  template<typename _Tp> size_t
  __mt_alloc<_Tp>::_S_max_bytes = 128;

  /*
   * In order to avoid fragmenting and minimize the number of new()
   * calls we always request new memory using this value. Based on
   * previous discussions on the libstdc++ mailing list we have
   * choosen the value below. See
   * http://gcc.gnu.org/ml/libstdc++/2001-07/msg00077.html
   */
  template<typename _Tp> size_t
  __mt_alloc<_Tp>::_S_chunk_size = 4096 - 4 * sizeof(void*);

  /*
   * The maximum number of supported threads. Our Linux 2.4.18 reports
   * 4070 in /proc/sys/kernel/threads-max
   */
  template<typename _Tp> size_t
  __mt_alloc<_Tp>::_S_max_threads = 4096;

  /*
   * Actual value calculated in _S_init()
   */
  template<typename _Tp> size_t
  __mt_alloc<_Tp>::_S_no_of_bins = 1;

  /*
   * Each time a deallocation occurs in a threaded application we make
   * sure that there are no more than _S_freelist_headroom % of used
   * memory on the freelist. If the number of additional records is
   * more than _S_freelist_headroom % of the freelist, we move these
   * records back to the global pool.
   */
  template<typename _Tp> size_t
  __mt_alloc<_Tp>::_S_freelist_headroom = 10;

  /*
   * Actual initialization in _S_init()
   */
#ifdef __GTHREADS
  template<typename _Tp> typename __mt_alloc<_Tp>::thread_record*
  volatile __mt_alloc<_Tp>::_S_thread_freelist_first = NULL;

  template<typename _Tp> __gthread_mutex_t
#ifdef __GTHREAD_MUTEX_INIT
  __mt_alloc<_Tp>::_S_thread_freelist_mutex = __GTHREAD_MUTEX_INIT;
#else
  // XXX
  __mt_alloc<_Tp>::_S_thread_freelist_mutex;
#endif

  /*
   * Actual initialization in _S_init()
   */
  template<typename _Tp> __gthread_key_t
  __mt_alloc<_Tp>::_S_thread_key;
#endif

  template<typename _Tp> typename __mt_alloc<_Tp>::bin_record*
  volatile __mt_alloc<_Tp>::_S_bin = NULL;

  template<typename _Tp>
    inline bool
    operator==(const __mt_alloc<_Tp>&, const __mt_alloc<_Tp>&)
    { return true; }
  
  template<typename _Tp>
    inline bool
    operator!=(const __mt_alloc<_Tp>&, const __mt_alloc<_Tp>&)
    { return false; }
} // namespace __gnu_cxx

#endif
