/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009, 2010-2011  Université de Bordeaux
 * Copyright (C) 2010, 2011, 2012, 2013  Centre National de la Recherche Scientifique
 * Copyright (C) 2011  Télécom-SudParis
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

/*
 * As a convention, in that file, buffers[0] is represented by A,
 * 				  buffers[1] is B ...
 */

/*
 *	Number of flops of Gemm
 */

#include <starpu.h>
#include <starpu_perfmodel.h>
#include "cholesky.h"

/* #define USE_PERTURBATION	1 */

#ifdef USE_PERTURBATION
#define PERTURBATE(a)	((starpu_drand48()*2.0f*(AMPL) + 1.0f - (AMPL))*(a))
#else
#define PERTURBATE(a)	(a)
#endif

double cpu_chol_task_11_cost(struct starpu_task *task, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	uint32_t n;

	n = starpu_matrix_get_nx(task->handles[0]);

	double cost = (((double)(n)*n*n)/1000.0f*0.894/0.79176);

#ifdef STARPU_MODEL_DEBUG
	FPRINTF(stdout, "cpu_chol_task_11_cost n %d cost %e\n", n, cost);
#endif

	return PERTURBATE(cost);
}

double cuda_chol_task_11_cost(struct starpu_task *task, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	uint32_t n;

	n = starpu_matrix_get_nx(task->handles[0]);

	double cost = (((double)(n)*n*n)/50.0f/10.75/5.088633/0.9883);

#ifdef STARPU_MODEL_DEBUG
	FPRINTF(stdout, "cuda_chol_task_11_cost n %d cost %e\n", n, cost);
#endif

	return PERTURBATE(cost);
}

double cpu_chol_task_21_cost(struct starpu_task *task, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	uint32_t n;

	n = starpu_matrix_get_nx(task->handles[0]);

	double cost = (((double)(n)*n*n)/7706.674/0.95/0.9965);

#ifdef STARPU_MODEL_DEBUG
	FPRINTF(stdout, "cpu_chol_task_21_cost n %d cost %e\n", n, cost);
#endif

	return PERTURBATE(cost);
}

double cuda_chol_task_21_cost(struct starpu_task *task, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	uint32_t n;

	n = starpu_matrix_get_nx(task->handles[0]);

	double cost = (((double)(n)*n*n)/50.0f/10.75/87.29520);

#ifdef STARPU_MODEL_DEBUG
	FPRINTF(stdout, "cuda_chol_task_21_cost n %d cost %e\n", n, cost);
#endif

	return PERTURBATE(cost);
}

double cpu_chol_task_22_cost(struct starpu_task *task, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	uint32_t n;

	n = starpu_matrix_get_nx(task->handles[0]);

	double cost = (((double)(n)*n*n)/50.0f/10.75/8.0760);

#ifdef STARPU_MODEL_DEBUG
	FPRINTF(stdout, "cpu_chol_task_22_cost n %d cost %e\n", n, cost);
#endif

	return PERTURBATE(cost);
}

double cuda_chol_task_22_cost(struct starpu_task *task, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	uint32_t n;

	n = starpu_matrix_get_nx(task->handles[0]);

	double cost = (((double)(n)*n*n)/50.0f/10.75/76.30666);

#ifdef STARPU_MODEL_DEBUG
	FPRINTF(stdout, "cuda_chol_task_22_cost n %d cost %e\n", n, cost);
#endif

	return PERTURBATE(cost);
}

void initialize_chol_model(struct starpu_perfmodel* model, char * symbol,
		double (*cpu_cost_function)(struct starpu_task *, struct starpu_perfmodel_arch*, unsigned),
		double (*cuda_cost_function)(struct starpu_task *, struct starpu_perfmodel_arch*, unsigned))
{
	model->symbol = symbol;
	model->type = STARPU_HISTORY_BASED;
	starpu_perfmodel_init(model);
	model->per_arch[STARPU_CPU_WORKER][0][0][0].cost_function = cpu_cost_function;
	if(starpu_worker_get_count_by_type(STARPU_CUDA_WORKER) != 0)
		model->per_arch[STARPU_CUDA_WORKER][0][0][0].cost_function = cuda_cost_function;
}
