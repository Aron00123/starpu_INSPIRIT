/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2015  INRIA
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

#ifndef __STARPU_SCHEDULER_HETEROPRIO_H__
#define __STARPU_SCHEDULER_HETEROPRIO_H__

#include <starpu.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HETEROPRIO_MAX_PRIO 100
/* #define FSTARPU_NB_TYPES 3 */
/* #define FSTARPU_CPU_IDX 0 */
/* #define FSTARPU_CUDA_IDX 1 */
/* #define FSTARPU_OPENCL_IDX 2 */

#define HETEROPRIO_MAX_PREFETCH 2
#if HETEROPRIO_MAX_PREFETCH <= 0
#error HETEROPRIO_MAX_PREFETCH == 1 means no prefetch so HETEROPRIO_MAX_PREFETCH must >= 1
#endif

enum FStarPUTypes{
// First will be zero
#ifdef STARPU_USE_CPU
	FSTARPU_CPU_IDX, // = 0
#endif
#ifdef STARPU_USE_CUDA
	FSTARPU_CUDA_IDX,
#endif
#ifdef STARPU_USE_OPENCL
	FSTARPU_OPENCL_IDX,
#endif
// This will be the number of archs
	FSTARPU_NB_TYPES
};

const unsigned FStarPUTypesToArch[FSTARPU_NB_TYPES+1] = {
#ifdef STARPU_USE_CPU
	STARPU_CPU,
#endif
#ifdef STARPU_USE_CUDA
	STARPU_CUDA,
#endif
#ifdef STARPU_USE_OPENCL
	STARPU_OPENCL,
#endif
	0
};


/** Tell how many prio there are for a given arch */
void starpu_heteroprio_set_nb_prios(unsigned sched_ctx_id, enum FStarPUTypes arch, unsigned max_prio);

/** Set the mapping for a given arch prio=>bucket */
void starpu_heteroprio_set_mapping(unsigned sched_ctx_id, enum FStarPUTypes arch, unsigned source_prio, unsigned dest_bucket_id);

/** Tell which arch is the faster for the tasks of a bucket (optional) */
void starpu_heteroprio_set_faster_arch(unsigned sched_ctx_id, enum FStarPUTypes arch, unsigned bucket_id);

/** Tell how slow is a arch for the tasks of a bucket (optional) */ 
void starpu_heteroprio_set_arch_slow_factor(unsigned sched_ctx_id, enum FStarPUTypes arch, unsigned bucket_id, float slow_factor);

#ifdef __cplusplus
}
#endif

#endif /* __STARPU_SCHEDULER_HETEROPRIO_H__ */
