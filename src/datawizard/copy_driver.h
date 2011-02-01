/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010  Université de Bordeaux 1
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

#ifndef __COPY_DRIVER_H__
#define __COPY_DRIVER_H__

#include <common/config.h>
#include <datawizard/memory_nodes.h>
#include "coherency.h"
#include "memalloc.h"

#ifdef STARPU_USE_CUDA
#include <cuda.h>
#include <cuda_runtime.h>
#include <cublas.h>
#endif

#ifdef STARPU_USE_OPENCL
#include <CL/cl.h>
#endif

struct starpu_data_request_s;
struct starpu_data_replicate_s;

/* this is a structure that can be queried to see whether an asynchronous
 * transfer has terminated or not */
typedef union {
	int dummy;
#ifdef STARPU_USE_CUDA
	cudaEvent_t cuda_event;
#endif
#ifdef STARPU_USE_OPENCL
        cl_event opencl_event;
#endif
} starpu_async_channel;

void _starpu_wake_all_blocked_workers_on_node(unsigned nodeid);

int _starpu_driver_copy_data_1_to_1(starpu_data_handle handle,
					struct starpu_data_replicate_s *src_replicate,
					struct starpu_data_replicate_s *dst_replicate,
					unsigned donotread,
					struct starpu_data_request_s *req,
					unsigned may_alloc);

unsigned _starpu_driver_test_request_completion(starpu_async_channel *async_channel, unsigned handling_node);
void _starpu_driver_wait_request_completion(starpu_async_channel *async_channel, unsigned handling_node);
#endif // __COPY_DRIVER_H__
