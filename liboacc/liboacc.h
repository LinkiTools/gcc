/*
This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */
/*
functions generated by OpenACC implementation
in the place of directives
*/

#ifndef LIBOACC_H
#define LIBOACC_H

#if __CYGWIN__ && !_WIN32
#define _WIN32 1
#endif

#ifdef __cplusplus
extern "C" {
#endif


int get_global_id(int);

/* handles to various internel objects */
typedef void* oacc_kernel;
typedef void* oacc_buffer;
typedef void* oacc_event;

/* make sure environment and device initialized */
void OACC_check_cur_dev(void);
/* create or get kernel from cache */
oacc_kernel OACC_get_kernel(const char* prog_name, const char* kern_name);
/* start a kernel with specified worksize */
void OACC_start_kernel(oacc_kernel kernel, unsigned worksize,
                       unsigned offset, int groupsize, oacc_event ev,
                       unsigned ev_idx);
/* associate memory object with kernel argument */
void OACC_set_kernel_arg(oacc_kernel kern, unsigned idx, oacc_buffer buf);

/* copy memory object to device */
oacc_buffer OACC_copyin(void *mem, unsigned size, int check_present,
                        oacc_event ev, unsigned ev_idx);
/* check object presence on device */
oacc_buffer OACC_check_present(void *mem);
/* create memory object on device */
oacc_buffer OACC_create_on_device(void* mem, unsigned size, int check_present,
                                  oacc_event ev, unsigned ev_idx);
/* copy memory object to host */
void OACC_copyout(void *mem, unsigned size, int check_present, oacc_event ev,
                  unsigned ev_idx);

/* create synchro queue */
oacc_event OACC_create_events(const char* src, int lineno);
/* set one step of processing */
void OACC_enqueue_events(oacc_event ev, unsigned n, int k);
/* advance on next step */
void OACC_advance_events(oacc_event ev);
/* synchronize */
void OACC_wait_events(oacc_event ev);

/* create or add global named async */
void OACC_add_named_async(int cookie, oacc_event ev);
/* create global nameless async */
void OACC_add_nameless_async(oacc_event ev);
/* wait specified async */
void OACC_wait_named_async(int cookie);
/* wait all async */
void OACC_wait_all_async(void);

void OACC_start_profiling(void);

#ifdef __cplusplus
}
#endif

#endif
