/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009-2012  Université de Bordeaux 1
 * Copyright (C) 2010, 2011, 2012  Centre National de la Recherche Scientifique
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

#ifndef __STARPU_H__
#define __STARPU_H__

#include <stdlib.h>

#ifndef _MSC_VER
#include <stdint.h>
#else
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
typedef unsigned long long uint64_t;
#endif

#include <starpu_config.h>

#ifdef STARPU_HAVE_WINDOWS
#include <windows.h>
#endif

#if defined(STARPU_USE_OPENCL) && !defined(__CUDACC__)
#include <starpu_opencl.h>
#endif

#include <starpu_util.h>
#include <starpu_data.h>
#include <starpu_data_interfaces.h>
#include <starpu_data_filters.h>
#include <starpu_perfmodel.h>
#include <starpu_worker.h>
#include <starpu_task.h>
#include <starpu_task_list.h>
#ifdef BUILDING_STARPU
#include <util/starpu_task_list_inline.h>
#endif
#include <starpu_task_util.h>
#include <starpu_scheduler.h>
#include <starpu_sched_ctx.h>
#include <starpu_expert.h>
#include <starpu_rand.h>
#include <starpu_cuda.h>
#include <starpu_cublas.h>
#include <starpu_bound.h>
#include <starpu_hash.h>
#include <starpu_profiling.h>
#include <starpu_top.h>
#include <starpu_fxt.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef STARPU_SIMGRID
#define main starpu_main
#endif

struct starpu_driver
{
	enum starpu_archtype type;
	union
	{
		unsigned cpu_id;
		unsigned cuda_id;
#if defined(STARPU_USE_OPENCL) && !defined(__CUDACC__)
		cl_device_id opencl_id;
#endif
		/*
		 * HOWTO: add a new kind of device to the starpu_driver structure.
		 * 1) Add a member to this union.
		 * 2) Edit _starpu_launch_drivers() to make sure the driver is
		 *    not always launched.
		 * 3) Edit starpu_driver_run() so that it can handle another
		 *    kind of architecture.
		 * 4) Write _starpu_run_foobar() in the corresponding driver.
		 * 5) Test the whole thing :)
		 */
	} id;
};

struct starpu_conf
{
	/* Will be initialized by starpu_conf_init */
	int magic;

	/* which scheduling policy should be used ? (NULL for default) */
	const char *sched_policy_name;
	struct starpu_sched_policy *sched_policy;

	/* number of CPU workers (-1 for default) */
	int ncpus;
	/* number of CUDA GPU workers (-1 for default) */
	int ncuda;
	/* number of GPU OpenCL device workers (-1 for default) */
	int nopencl;
	/* number of Cell's SPUs (-1 for default) */
	int nspus;

	unsigned use_explicit_workers_bindid;
	unsigned workers_bindid[STARPU_NMAXWORKERS];

	unsigned use_explicit_workers_cuda_gpuid;
	unsigned workers_cuda_gpuid[STARPU_NMAXWORKERS];

	unsigned use_explicit_workers_opencl_gpuid;
	unsigned workers_opencl_gpuid[STARPU_NMAXWORKERS];

	/* calibrate bus (-1 for default) */
	int bus_calibrate;

	/* calibrate performance models, if any (-1 for default) */
	int calibrate;

	/* Create only one combined worker, containing all CPU workers */
	int single_combined_worker;

	/* indicate if all asynchronous copies should be disabled */
	int disable_asynchronous_copy;

	/* indicate if asynchronous copies to CUDA devices should be disabled */
	int disable_asynchronous_cuda_copy;

	/* indicate if asynchronous copies to OpenCL devices should be disabled */
	int disable_asynchronous_opencl_copy;

	/* Enable CUDA/OpenGL interoperation on these CUDA devices */
	unsigned *cuda_opengl_interoperability;
	unsigned n_cuda_opengl_interoperability;

	/* A driver that the application will run in one of its own threads. */
	struct starpu_driver *not_launched_drivers;
	unsigned n_not_launched_drivers;

	/* Indicates whether we start the profiling automatically
	   or we leave the appl start it if it wants to */
	unsigned no_auto_profile;
};

/* Initialize a starpu_conf structure with default values. */
int starpu_conf_init(struct starpu_conf *conf);

/* Initialization method: it must be called prior to any other StarPU call
 * Default configuration is used if NULL is passed as argument.
 */
int starpu_init(struct starpu_conf *conf) STARPU_WARN_UNUSED_RESULT;

/* Shutdown method: note that statistics are only generated once StarPU is
 * shutdown */
void starpu_shutdown(void);

/* Print topology configuration */
void starpu_topology_print(FILE *f);

int starpu_asynchronous_copy_disabled(void);
int starpu_asynchronous_cuda_copy_disabled(void);
int starpu_asynchronous_opencl_copy_disabled(void);

void starpu_profiling_init();
void starpu_display_stats();
int starpu_driver_run(struct starpu_driver *d);
void starpu_drivers_request_termination(void);

int starpu_driver_init(struct starpu_driver *d);
int starpu_driver_run_once(struct starpu_driver *d);
int starpu_driver_deinit(struct starpu_driver *d);
#ifdef __cplusplus
}
#endif

#if defined(STARPU_USE_DEPRECATED_API)
#include "starpu_deprecated_api.h"
#endif /* STARPU_USE_DEPRECATED_API */

#endif /* __STARPU_H__ */
