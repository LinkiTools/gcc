/* Copyright (C) 2007, 2008 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
   more details.

   You should have received a copy of the GNU Lesser General Public License 
   along with libgomp; see the file COPYING.LIB.  If not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files, some
   of which are compiled with GCC, to produce an executable, this library
   does not by itself cause the resulting executable to be covered by the
   GNU General Public License.  This exception does not however invalidate
   any other reasons why the executable file might be covered by the GNU
   General Public License.  */

/* This file handles the maintainence of tasks in response to task
   creation and termination.  */

#include "libgomp.h"
#include <stdlib.h>
#include <string.h>


/* Create a new task data structure.  */

void
gomp_init_task (struct gomp_task *task, struct gomp_task *prev_task,
		struct gomp_task_icv *prev_icv)
{
  task->prev = prev_task;
  task->icv = *prev_icv;
}

/* Clean up and free a task, after completing it.  */

void
gomp_end_task (void)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_task *task = thr->task;

  thr->task = task->prev;
}

/* Called when encountering an explicit task directive.  If IF_CLAUSE is
   true, then we must not delay in executing the task.  If UNTIED is true,
   then the task may be executed by any member of the team.  */

void
GOMP_task (void (*fn) (void *), void *data, void (*cpyfn) (void *, void *),
	   long arg_size, long arg_align,
	   bool if_clause __attribute__((unused)),
	   unsigned flags __attribute__((unused)))
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_task task;
  gomp_init_task (&task, thr->task, gomp_icv (false));
  thr->task = &task;

  {
    /* We only implement synchronous tasks at the moment, which means that
       we cannot defer or untie the task.  Which means we execute it now.  */
    char buf[arg_size + arg_align - 1];
    char *arg = (char *) (((uintptr_t) buf + arg_align - 1)
			  & ~(uintptr_t) (arg_align - 1));
    if (cpyfn)
      cpyfn (arg, data);
    else
      memcpy (arg, data, arg_size);
    fn (arg);
  }

  gomp_end_task ();
}

/* Called when encountering a taskwait directive.  */

void
GOMP_taskwait (void)
{
  /* Since we never deferred any tasks, there are none to wait for.  */
}
