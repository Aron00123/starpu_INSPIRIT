/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009, 2010  Université de Bordeaux 1
 * Copyright (C) 2010  Mehdi Juhoor <mjuhoor@gmail.com>
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

#include "xlu.h"
#include "xlu_kernels.h"

#define TAG11(k)	((starpu_tag_t)( (1ULL<<60) | (unsigned long long)(k)))
#define TAG12(k,i)	((starpu_tag_t)(((2ULL<<60) | (((unsigned long long)(k))<<32)	\
					| (unsigned long long)(i))))
#define TAG21(k,j)	((starpu_tag_t)(((3ULL<<60) | (((unsigned long long)(k))<<32)	\
					| (unsigned long long)(j))))
#define TAG22(k,i,j)	((starpu_tag_t)(((4ULL<<60) | ((unsigned long long)(k)<<32) 	\
					| ((unsigned long long)(i)<<16)	\
					| (unsigned long long)(j))))

static unsigned no_prio = 0;
struct timeval xlu_start;
struct timeval xlu_end;
starpu_data_handle xlu_dataA;



/*
 *	Construct the DAG
 */

static struct starpu_task *create_task(starpu_tag_t id)
{
	struct starpu_task *task = starpu_task_create();
		task->cl_arg = NULL;

	task->use_tag = 1;
	task->tag_id = id;

	return task;
}

static struct starpu_task *create_task_11(starpu_data_handle dataA, unsigned k)
{
//	printf("task 11 k = %d TAG = %llx\n", k, (TAG11(k)));

	struct starpu_task *task = create_task(TAG11(k));

	task->cl = &cl11;

	/* which sub-data is manipulated ? */
	task->buffers[0].handle = starpu_data_get_sub_data(dataA, 2, k, k);
	task->buffers[0].mode = STARPU_RW;

	/* this is an important task */
	if (!no_prio)
		task->priority = STARPU_MAX_PRIO;

	/* enforce dependencies ... */
	if (k > 0) {
		starpu_tag_declare_deps(TAG11(k), 1, TAG22(k-1, k, k));
	}

	return task;
}

static void create_task_12(starpu_data_handle dataA, unsigned k, unsigned j)
{
//	printf("task 12 k,i = %d,%d TAG = %llx\n", k,i, TAG12(k,i));

	struct starpu_task *task = create_task(TAG12(k, j));
	
	task->cl = &cl12;

	/* which sub-data is manipulated ? */
	task->buffers[0].handle = starpu_data_get_sub_data(dataA, 2, k, k); 
	task->buffers[0].mode = STARPU_R;
	task->buffers[1].handle = starpu_data_get_sub_data(dataA, 2, j, k); 
	task->buffers[1].mode = STARPU_RW;

	if (!no_prio && (j == k+1)) {
		task->priority = STARPU_MAX_PRIO;
	}

	/* enforce dependencies ... */
	if (k > 0) {
		starpu_tag_declare_deps(TAG12(k, j), 2, TAG11(k), TAG22(k-1, k, j));
	}
	else {
		starpu_tag_declare_deps(TAG12(k, j), 1, TAG11(k));
	}

	starpu_task_submit(task);
}

static void create_task_21(starpu_data_handle dataA, unsigned k, unsigned i)
{
	struct starpu_task *task = create_task(TAG21(k, i));

	task->cl = &cl21;
	
	/* which sub-data is manipulated ? */
	task->buffers[0].handle = starpu_data_get_sub_data(dataA, 2, k, k); 
	task->buffers[0].mode = STARPU_R;
	task->buffers[1].handle = starpu_data_get_sub_data(dataA, 2, k, i); 
	task->buffers[1].mode = STARPU_RW;

	if (!no_prio && (i == k+1)) {
		task->priority = STARPU_MAX_PRIO;
	}

	/* enforce dependencies ... */
	if (k > 0) {
		starpu_tag_declare_deps(TAG21(k, i), 2, TAG11(k), TAG22(k-1, i, k));
	}
	else {
		starpu_tag_declare_deps(TAG21(k, i), 1, TAG11(k));
	}

	starpu_task_submit(task);
}

static void create_task_22(starpu_data_handle dataA, unsigned k, unsigned i, unsigned j)
{
//	printf("task 22 k,i,j = %d,%d,%d TAG = %llx\n", k,i,j, TAG22(k,i,j));

	struct starpu_task *task = create_task(TAG22(k, i, j));

	task->cl = &cl22;

	/* which sub-data is manipulated ? */
	task->buffers[0].handle = starpu_data_get_sub_data(dataA, 2, k, i); /* produced by TAG21(k, i) */ 
	task->buffers[0].mode = STARPU_R;
	task->buffers[1].handle = starpu_data_get_sub_data(dataA, 2, j, k); /* produced by TAG12(k, j) */
	task->buffers[1].mode = STARPU_R;
	task->buffers[2].handle = starpu_data_get_sub_data(dataA, 2, j, i); /* produced by TAG22(k-1, i, j) */
	task->buffers[2].mode = STARPU_RW;

	if (!no_prio &&  (i == k + 1) && (j == k +1) ) {
		task->priority = STARPU_MAX_PRIO;
	}

	/* enforce dependencies ... */
	if (k > 0) {
		starpu_tag_declare_deps(TAG22(k, i, j), 3, TAG22(k-1, i, j), TAG12(k, j), TAG21(k, i));
	}
	else {
		starpu_tag_declare_deps(TAG22(k, i, j), 2, TAG12(k, j), TAG21(k, i));
	}

	starpu_task_submit(task);
}

/*
 *	code to bootstrap the factorization 
 */

static void dw_codelet_facto_v3(starpu_data_handle dataA, unsigned nblocks)
{
	struct starpu_task *entry_task = NULL;

	/* create all the DAG nodes */
	unsigned i,j,k;

	for (k = 0; k < nblocks; k++)
	{
		struct starpu_task *task = create_task_11(dataA, k);

		/* we defer the launch of the first task */
		if (k == 0) {
			entry_task = task;
		}
		else {
		  starpu_task_submit(task);
		}
		
		for (i = k+1; i<nblocks; i++)
		{
			create_task_12(dataA, k, i);
			create_task_21(dataA, k, i);
		}

		for (i = k+1; i<nblocks; i++)
		{
			for (j = k+1; j<nblocks; j++)
			{
			  create_task_22(dataA, k, i, j);
			}
		}
	}

	/* schedule the codelet */
	gettimeofday(&xlu_start, NULL);
	int ret = starpu_task_submit(entry_task);
	if (STARPU_UNLIKELY(ret == -ENODEV))
	{
		fprintf(stderr, "No worker may execute this task\n");
		exit(-1);
	}

}

void STARPU_LU(lu_decomposition)(TYPE *matA, unsigned size, unsigned ld, unsigned nblocks)
{
	/* monitor and partition the A matrix into blocks :
	 * one block is now determined by 2 unsigned (i,j) */
	starpu_matrix_data_register(&xlu_dataA, 0, (uintptr_t)matA, ld, size, size, sizeof(TYPE));

	/* We already enforce deps by hand */
	starpu_data_set_sequential_consistency_flag(xlu_dataA, 0);

	struct starpu_data_filter f;
		f.filter_func = starpu_vertical_block_filter_func;
		f.nchildren = nblocks;
		f.get_nchildren = NULL;
		f.get_child_ops = NULL;

	struct starpu_data_filter f2;
		f2.filter_func = starpu_block_filter_func;
		f2.nchildren = nblocks;
		f2.get_nchildren = NULL;
		f2.get_child_ops = NULL;

	starpu_data_map_filters(xlu_dataA, 2, &f, &f2);

	dw_codelet_facto_v3(xlu_dataA, nblocks);
}

void finish_lu_decomposition(unsigned nblocks)
{
	/* stall the application until the end of computations */
	starpu_tag_wait(TAG11(nblocks-1));

	gettimeofday(&xlu_end, NULL);

	double timing = (double)((xlu_end.tv_sec - xlu_start.tv_sec)*1000000 + (xlu_end.tv_usec - xlu_start.tv_usec));
	//	fprintf(stderr, "Computation took (in ms)\n");
	printf("%2.2f ", timing/1000);

	unsigned n = starpu_matrix_get_nx(xlu_dataA);
	double flop = (2.0f*n*n*n)/3.0f;
	//	fprintf(stderr, "Synthetic GFlops : %2.2f\n", (flop/timing/1000.0f));

	/* gather all the data */
	starpu_data_unpartition(xlu_dataA, 0);
}
