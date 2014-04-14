/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2014  Inria
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

#ifndef __OPENMP_RUNTIME_SUPPORT_H__
#define __OPENMP_RUNTIME_SUPPORT_H__

#include <starpu.h>

#ifdef STARPU_OPENMP
#include <common/list.h>
#include <common/starpu_spinlock.h>
#include <common/uthash.h>

/* ucontexts have been deprecated as of POSIX 1-2004
 * _XOPEN_SOURCE required at least on OS/X
 * 
 * TODO: add detection in configure.ac
 */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#include <ucontext.h>

/*
 * Arbitrary limit on the number of nested parallel sections
 */
#define STARPU_OMP_MAX_ACTIVE_LEVELS 4

/*
 * Proc bind modes defined by the OpenMP spec
 */
enum starpu_omp_bind_mode
{
	starpu_omp_bind_false  = 0,
	starpu_omp_bind_true   = 1,
	starpu_omp_bind_master = 2,
	starpu_omp_bind_close  = 3,
	starpu_omp_bind_spread = 4
};

enum starpu_omp_schedule_mode
{
	starpu_omp_schedule_static  = 0,
	starpu_omp_schedule_dynamic = 1,
	starpu_omp_schedule_guided  = 2,
	starpu_omp_schedule_auto    = 3
};

/*
 * Possible abstract names for OpenMP places
 */
enum starpu_omp_place_name
{
	starpu_omp_place_undefined = 0,
	starpu_omp_place_threads   = 1,
	starpu_omp_place_cores     = 2,
	starpu_omp_place_sockets   = 3,
	starpu_omp_place_numerical = 4 /* place specified numerically */
};

struct starpu_omp_numeric_place
{
	int excluded_place;
	int *included_numeric_items;
	int nb_included_numeric_items;
	int *excluded_numeric_items;
	int nb_excluded_numeric_items;
};

/*
 * OpenMP place for thread afinity, defined by the OpenMP spec
 */
struct starpu_omp_place
{
	int abstract_name;
	int abstract_excluded;
	int abstract_length;
	struct starpu_omp_numeric_place *numeric_places;
	int nb_numeric_places;
};

/* 
 * Internal Control Variables (ICVs) declared following
 * OpenMP 4.0.0 spec section 2.3.1
 */
struct starpu_omp_data_environment_icvs
{
	/* parallel region icvs */
	int dyn_var;
	int nest_var;
	int *nthreads_var; /* nthreads_var ICV is a list */
	int thread_limit_var;

	int active_levels_var;
	int levels_var;
	int *bind_var; /* bind_var ICV is a list */

	/* loop region icvs */
	int run_sched_var;

	/* program execution icvs */
	int default_device_var;
};

struct starpu_omp_device_icvs
{
	/* parallel region icvs */
	int max_active_levels_var;

	/* loop region icvs */
	int def_sched_var;

	/* program execution icvs */
	int stacksize_var;
	int wait_policy_var;
};

struct starpu_omp_implicit_task_icvs
{
	/* parallel region icvs */
	int place_partition_var;
};

struct starpu_omp_global_icvs
{
	/* program execution icvs */
	int cancel_var;
};

struct starpu_omp_initial_icv_values
{
	int dyn_var;
	int nest_var;
	int *nthreads_var;
	int run_sched_var;
	int run_sched_chunk_var;
	int def_sched_var;
	int *bind_var;
	int stacksize_var;
	int wait_policy_var;
	int thread_limit_var;
	int max_active_levels_var;
	int active_levels_var;
	int levels_var;
	int place_partition_var;
	int cancel_var;
	int default_device_var;

	/* not a real ICV, but needed to store the contents of OMP_PLACES */
	struct starpu_omp_place places;
};

struct starpu_omp_task_group
{
	int descendent_task_count;
	struct starpu_omp_task_group *next;
};

struct starpu_omp_task_link
{
	struct starpu_omp_task *task;
	struct starpu_omp_task_link *next;
};

struct starpu_omp_critical
{
	UT_hash_handle hh;
	struct _starpu_spinlock lock;
	unsigned state;
	struct starpu_omp_task_link *contention_list_head;
	const char *name;
};

enum starpu_omp_task_state
{
	starpu_omp_task_state_clear      = 0,
	starpu_omp_task_state_preempted  = 1,
	starpu_omp_task_state_terminated = 2,
};

LIST_TYPE(starpu_omp_task,

	struct starpu_omp_task *parent_task;
	struct starpu_omp_thread *owner_thread;
	struct starpu_omp_region *owner_region;
	struct starpu_omp_region *nested_region;
	int is_implicit;
	int is_undeferred;
	int is_final;
	int is_untied;
	int child_task_count;
	struct starpu_omp_task_group *task_group;
	struct _starpu_spinlock lock;
	int barrier_count;
	int single_id;
	struct starpu_omp_data_environment_icvs data_env_icvs;
	struct starpu_omp_implicit_task_icvs implicit_task_icvs;

	struct starpu_task *starpu_task;
	void **starpu_buffers;
	void *starpu_cl_arg;

	/* actual task function to be run */
	void (*f)(void **starpu_buffers, void *starpu_cl_arg);

	enum starpu_omp_task_state state;

	/* 
	 * context to store the processing state of the task
	 * in case of blocking/recursive task operation
	 */
	ucontext_t ctx;

	/*
	 * stack to execute the task over, to be able to switch
	 * in case blocking/recursive task operation
	 */
	void *stack;
)

LIST_TYPE(starpu_omp_thread,

	struct starpu_omp_task *current_task;
	struct starpu_omp_region *owner_region;

	struct starpu_omp_task *primary_task;

	/*
	 * stack to execute the initial thread over
	 * when preempting the initial task
	 * note: should not be used for other threads
	 */
	void *initial_thread_stack;

	/*
	 * context to store the 'scheduler' state of the thread,
	 * to which the execution of thread comes back upon a
	 * blocking/recursive task operation
	 */
	ucontext_t ctx;

	struct starpu_driver starpu_driver;
	unsigned starpu_worker_id;
)

struct starpu_omp_region
{
	struct starpu_omp_region *parent_region;
	struct starpu_omp_device *owner_device;
	struct starpu_omp_thread *master_thread;
	/* note: the list of threads does not include the master_thread */
	struct starpu_omp_thread_list *thread_list;
	/* list of implicit omp tasks created to run the region */
	struct starpu_omp_task_list *implicit_task_list;
	/* include both the master thread and the region own threads */
	int nb_threads;
	int barrier_count;
	int bound_explicit_task_count;
	int single_id;
	int level;
	struct starpu_task *continuation_starpu_task;
};

struct starpu_omp_device
{
	struct starpu_omp_device_icvs icvs;
};

struct starpu_omp_global
{
	struct starpu_omp_global_icvs icvs;
	struct starpu_omp_task *initial_task;
	struct starpu_omp_thread *initial_thread;
	struct starpu_omp_region *initial_region;
	struct starpu_omp_device *initial_device;
	struct starpu_omp_critical *default_critical;
	struct starpu_omp_critical *named_criticals;
	struct _starpu_spinlock named_criticals_lock;
};

/* 
 * internal global variables
 */
extern struct starpu_omp_initial_icv_values *_starpu_omp_initial_icv_values;
extern struct starpu_omp_global *_starpu_omp_global_state;
extern double _starpu_omp_clock_ref;

/* 
 * internal API
 */
void _starpu_omp_environment_init(void);
void _starpu_omp_environment_exit(void);
#endif // STARPU_OPENMP

#endif // __OPENMP_RUNTIME_SUPPORT_H__
