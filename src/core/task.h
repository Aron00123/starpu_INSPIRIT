/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009, 2010-2011  Université de Bordeaux 1
 * Copyright (C) 2010  Centre National de la Recherche Scientifique
 *
 * StarPU is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * StarPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#ifndef __CORE_TASK_H__
#define __CORE_TASK_H__

#include <starpu.h>
#include <common/config.h>
#include <core/jobs.h>

/* In order to implement starpu_task_wait_for_all, we keep track of the number of
 * task currently submitted */
void _starpu_decrement_nsubmitted_tasks(void);
/* In order to implement starpu_task_wait_for_no_ready, we keep track of the number of
 * task currently ready */
void _starpu_increment_nready_tasks(void);
void _starpu_decrement_nready_tasks(void);

/* A pthread key is used to store the task currently executed on the thread.
 * _starpu_initialize_current_task_key initializes this pthread key and
 * _starpu_set_current_task updates its current value. */
void _starpu_initialize_current_task_key(void);
void _starpu_set_current_task(struct starpu_task *task);

/* NB the second argument makes it possible to count regenerable tasks only
 * once. */
int _starpu_submit_job(starpu_job_t j);

/* Returns the job structure (which is the internal data structure associated
 * to a task). */
starpu_job_t _starpu_get_job_associated_to_task(struct starpu_task *task);

struct starpu_task *_starpu_create_task_alias(struct starpu_task *task);

#endif // __CORE_TASK_H__
