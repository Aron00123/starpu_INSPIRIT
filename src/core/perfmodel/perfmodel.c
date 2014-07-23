/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009-2014  Université de Bordeaux 1
 * Copyright (C) 2010, 2011, 2012, 2013, 2014  Centre National de la Recherche Scientifique
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

#include <starpu.h>
#include <starpu_profiling.h>
#include <common/config.h>
#include <common/utils.h>
#include <unistd.h>
#include <sys/stat.h>
#include <core/perfmodel/perfmodel.h>
#include <core/jobs.h>
#include <core/workers.h>
#include <datawizard/datawizard.h>

#ifdef STARPU_HAVE_WINDOWS
#include <windows.h>
#endif

/* This flag indicates whether performance models should be calibrated or not.
 *	0: models need not be calibrated
 *	1: models must be calibrated
 *	2: models must be calibrated, existing models are overwritten.
 */
static unsigned calibrate_flag = 0;
void _starpu_set_calibrate_flag(unsigned val)
{
	calibrate_flag = val;
}

unsigned _starpu_get_calibrate_flag(void)
{
	return calibrate_flag;
}

struct starpu_perfmodel_arch* starpu_worker_get_perf_archtype(int workerid, unsigned sched_ctx_id)
{
	if(sched_ctx_id != STARPU_NMAX_SCHED_CTXS)
	{
		unsigned child_sched_ctx = starpu_sched_ctx_worker_is_master_for_child_ctx(workerid, sched_ctx_id);
		if(child_sched_ctx != STARPU_NMAX_SCHED_CTXS)
			return _starpu_sched_ctx_get_perf_archtype(child_sched_ctx);
	}

	struct _starpu_machine_config *config = _starpu_get_machine_config();

	/* This workerid may either be a basic worker or a combined worker */
	unsigned nworkers = config->topology.nworkers;

	if (workerid < (int)config->topology.nworkers)
		return &config->workers[workerid].perf_arch;


	/* We have a combined worker */
	unsigned ncombinedworkers = config->topology.ncombinedworkers;
	STARPU_ASSERT(workerid < (int)(ncombinedworkers + nworkers));
	return &config->combined_workers[workerid - nworkers].perf_arch;
}

/*
 * PER ARCH model
 */

static double per_arch_task_expected_perf(struct starpu_perfmodel *model, struct starpu_perfmodel_arch * arch, struct starpu_task *task, unsigned nimpl)
{
	int comb;
	double (*per_arch_cost_function)(struct starpu_task *task, struct starpu_perfmodel_arch* arch, unsigned nimpl);

	comb = starpu_perfmodel_arch_comb_get(arch->ndevices, arch->devices);
	if (comb == -1)
		return NAN;
	if (model->state->per_arch[comb] == NULL)
		// The model has not been executed on this combination
		return NAN;

	per_arch_cost_function = model->state->per_arch[comb][nimpl].cost_function;
	STARPU_ASSERT_MSG(per_arch_cost_function, "STARPU_PER_ARCH needs per-arch cost_function to be defined");

	return per_arch_cost_function(task, arch, nimpl);
}

/*
 * Common model
 */

double starpu_worker_get_relative_speedup(struct starpu_perfmodel_arch* perf_arch)
{
	double speedup = 0;
	int dev;
	for(dev = 0; dev < perf_arch->ndevices; dev++)
	{
		double coef = 0.0;
		if (perf_arch->devices[dev].type == STARPU_CPU_WORKER)
			coef = _STARPU_CPU_ALPHA;
		else if (perf_arch->devices[dev].type == STARPU_CUDA_WORKER)
			coef = _STARPU_CUDA_ALPHA;
		else if (perf_arch->devices[dev].type == STARPU_OPENCL_WORKER)
			coef = _STARPU_OPENCL_ALPHA;
		else if (perf_arch->devices[dev].type == STARPU_MIC_WORKER)
			coef =  _STARPU_MIC_ALPHA;

		speedup += coef * (perf_arch->devices[dev].ncores + 1);
	}
	return speedup == 0 ? NAN : speedup;
}

static double common_task_expected_perf(struct starpu_perfmodel *model, struct starpu_perfmodel_arch* arch, struct starpu_task *task, unsigned nimpl)
{
	double exp;
	double alpha;

	STARPU_ASSERT_MSG(model->cost_function, "STARPU_COMMON requires common cost_function to be defined");

	exp = model->cost_function(task, nimpl);
	alpha = starpu_worker_get_relative_speedup(arch);

	STARPU_ASSERT(!_STARPU_IS_ZERO(alpha));

	return (exp/alpha);
}

void _starpu_load_perfmodel(struct starpu_perfmodel *model)
{
	if (!model || model->is_loaded)
		return;

	int load_model = _starpu_register_model(model);
	if (!load_model)
		return;

	switch (model->type)
	{
		case STARPU_PER_ARCH:
			_starpu_load_per_arch_based_model(model);
			break;
		case STARPU_COMMON:
			_starpu_load_common_based_model(model);
			break;
		case STARPU_HISTORY_BASED:
		case STARPU_NL_REGRESSION_BASED:
			_starpu_load_history_based_model(model, 1);
			break;
		case STARPU_REGRESSION_BASED:
			_starpu_load_history_based_model(model, 0);
			break;

		default:
			STARPU_ABORT();
	}

	model->is_loaded = 1;
}

static double starpu_model_expected_perf(struct starpu_task *task, struct starpu_perfmodel *model, struct starpu_perfmodel_arch* arch,  unsigned nimpl)
{
	if (model)
	{
		if (model->symbol)
			_starpu_load_perfmodel(model);

		struct _starpu_job *j = _starpu_get_job_associated_to_task(task);

		switch (model->type)
		{
			case STARPU_PER_ARCH:
				return per_arch_task_expected_perf(model, arch, task, nimpl);
			case STARPU_COMMON:
				return common_task_expected_perf(model, arch, task, nimpl);
			case STARPU_HISTORY_BASED:
				return _starpu_history_based_job_expected_perf(model, arch, j, nimpl);
			case STARPU_REGRESSION_BASED:
				return _starpu_regression_based_job_expected_perf(model, arch, j, nimpl);
			case STARPU_NL_REGRESSION_BASED:
				return _starpu_non_linear_regression_based_job_expected_perf(model, arch, j,nimpl);
			default:
				STARPU_ABORT();
		}
	}

	/* no model was found */
	return 0.0;
}

double starpu_task_expected_length(struct starpu_task *task, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	return starpu_model_expected_perf(task, task->cl->model, arch, nimpl);
}

double starpu_task_expected_power(struct starpu_task *task, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	return starpu_model_expected_perf(task, task->cl->power_model, arch, nimpl);
}

double starpu_task_expected_conversion_time(struct starpu_task *task,
					    struct starpu_perfmodel_arch* arch,
					    unsigned nimpl)
{
	if(arch->ndevices > 1)
		return -1.0;
	unsigned i;
	double sum = 0.0;
	enum starpu_node_kind node_kind;
	unsigned nbuffers = STARPU_TASK_GET_NBUFFERS(task);

	for (i = 0; i < nbuffers; i++)
	{
		starpu_data_handle_t handle;
		struct starpu_task *conversion_task;

		handle = STARPU_TASK_GET_HANDLE(task, i);
		if (!_starpu_data_is_multiformat_handle(handle))
			continue;

		switch(arch->devices[0].type)
		{
			case STARPU_CPU_WORKER:
				node_kind = STARPU_CPU_RAM;
				break;
			case STARPU_CUDA_WORKER:
				node_kind = STARPU_CUDA_RAM;
				break;
			case STARPU_OPENCL_WORKER:
				node_kind = STARPU_OPENCL_RAM;
				break;
			case STARPU_MIC_WORKER:
				node_kind = STARPU_MIC_RAM;
				break;
			case STARPU_SCC_WORKER:
				node_kind = STARPU_SCC_RAM;
				break;
			default:
				STARPU_ABORT();
				break;
		}
		if (!_starpu_handle_needs_conversion_task_for_arch(handle, node_kind))
			continue;

		conversion_task = _starpu_create_conversion_task_for_arch(handle, node_kind);
		sum += starpu_task_expected_length(conversion_task, arch, nimpl);
		_starpu_spin_lock(&handle->header_lock);
		handle->refcnt--;
		handle->busy_count--;
		if (!_starpu_data_check_not_busy(handle))
			_starpu_spin_unlock(&handle->header_lock);
		starpu_task_clean(conversion_task);
		free(conversion_task);
	}

	return sum;
}

/* Predict the transfer time (in µs) to move a handle to a memory node */
double starpu_data_expected_transfer_time(starpu_data_handle_t handle, unsigned memory_node, enum starpu_data_access_mode mode)
{
	/* If we don't need to read the content of the handle */
	if (!(mode & STARPU_R))
		return 0.0;

	if (_starpu_is_data_present_or_requested(handle, memory_node))
		return 0.0;

	size_t size = _starpu_data_get_size(handle);

	/* XXX in case we have an abstract piece of data (eg.  with the
	 * void interface, this does not introduce any overhead, and we
	 * don't even want to consider the latency that is not
	 * relevant). */
	if (size == 0)
		return 0.0;

	int src_node = _starpu_select_src_node(handle, memory_node);
	if (src_node < 0)
		/* Will just create it in place. Ideally we should take the
		 * time to create it into account */
		return 0.0;
	return starpu_transfer_predict(src_node, memory_node, size);
}

/* Data transfer performance modeling */
double starpu_task_expected_data_transfer_time(unsigned memory_node, struct starpu_task *task)
{
	unsigned nbuffers = STARPU_TASK_GET_NBUFFERS(task);
	unsigned buffer;

	double penalty = 0.0;

	for (buffer = 0; buffer < nbuffers; buffer++)
	{
		starpu_data_handle_t handle = STARPU_TASK_GET_HANDLE(task, buffer);
		enum starpu_data_access_mode mode = STARPU_TASK_GET_MODE(task, buffer);

		penalty += starpu_data_expected_transfer_time(handle, memory_node, mode);
	}

	return penalty;
}

/* Return the expected duration of the entire task bundle in µs */
double starpu_task_bundle_expected_length(starpu_task_bundle_t bundle, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	double expected_length = 0.0;

	/* We expect the length of the bundle the be the sum of the different tasks length. */
	STARPU_PTHREAD_MUTEX_LOCK(&bundle->mutex);

	struct _starpu_task_bundle_entry *entry;
	entry = bundle->list;

	while (entry)
	{
		if(!entry->task->scheduled)
		{
			double task_length = starpu_task_expected_length(entry->task, arch, nimpl);

			/* In case the task is not calibrated, we consider the task
			 * ends immediately. */
			if (task_length > 0.0)
				expected_length += task_length;
		}

		entry = entry->next;
	}

	STARPU_PTHREAD_MUTEX_UNLOCK(&bundle->mutex);

	return expected_length;
}

/* Return the expected power consumption of the entire task bundle in J */
double starpu_task_bundle_expected_power(starpu_task_bundle_t bundle, struct starpu_perfmodel_arch* arch, unsigned nimpl)
{
	double expected_power = 0.0;

	/* We expect total consumption of the bundle the be the sum of the different tasks consumption. */
	STARPU_PTHREAD_MUTEX_LOCK(&bundle->mutex);

	struct _starpu_task_bundle_entry *entry;
	entry = bundle->list;

	while (entry)
	{
		double task_power = starpu_task_expected_power(entry->task, arch, nimpl);

		/* In case the task is not calibrated, we consider the task
		 * ends immediately. */
		if (task_power > 0.0)
			expected_power += task_power;

		entry = entry->next;
	}

	STARPU_PTHREAD_MUTEX_UNLOCK(&bundle->mutex);

	return expected_power;
}

/* Return the time (in µs) expected to transfer all data used within the bundle */
double starpu_task_bundle_expected_data_transfer_time(starpu_task_bundle_t bundle, unsigned memory_node)
{
	STARPU_PTHREAD_MUTEX_LOCK(&bundle->mutex);

	struct _starpu_handle_list *handles = NULL;

	/* We list all the handle that are accessed within the bundle. */

	/* For each task in the bundle */
	struct _starpu_task_bundle_entry *entry = bundle->list;
	while (entry)
	{
		struct starpu_task *task = entry->task;

		if (task->cl)
		{
			unsigned b;
			unsigned nbuffers = STARPU_TASK_GET_NBUFFERS(task);
			for (b = 0; b < nbuffers; b++)
			{
				starpu_data_handle_t handle = STARPU_TASK_GET_HANDLE(task, b);
				enum starpu_data_access_mode mode = STARPU_TASK_GET_MODE(task, b);

				if (!(mode & STARPU_R))
					continue;

				/* Insert the handle in the sorted list in case
				 * it's not already in that list. */
				_insertion_handle_sorted(&handles, handle, mode);
			}
		}

		entry = entry->next;
	}

	STARPU_PTHREAD_MUTEX_UNLOCK(&bundle->mutex);

	/* Compute the sum of data transfer time, and destroy the list */

	double total_exp = 0.0;

	while (handles)
	{
		struct _starpu_handle_list *current = handles;
		handles = handles->next;

		double exp;
		exp = starpu_data_expected_transfer_time(current->handle, memory_node, current->mode);

		total_exp += exp;

		free(current);
	}

	return total_exp;
}

static int directory_existence_was_tested = 0;

void _starpu_get_perf_model_dir(char *path, size_t maxlen)
{
#ifdef STARPU_PERF_MODEL_DIR
	/* use the directory specified at configure time */
	snprintf(path, maxlen, "%s", STARPU_PERF_MODEL_DIR);
#else
	snprintf(path, maxlen, "%s/.starpu/sampling/", _starpu_get_home_path());
#endif
}

void _starpu_get_perf_model_dir_codelets(char *path, size_t maxlen)
{
	char perf_model_path[256];
	_starpu_get_perf_model_dir(perf_model_path, maxlen);
	snprintf(path, maxlen, "%s/codelets/%d/", perf_model_path, _STARPU_PERFMODEL_VERSION);
}

void _starpu_get_perf_model_dir_bus(char *path, size_t maxlen)
{
	_starpu_get_perf_model_dir(path, maxlen);
	strncat(path, "/bus/", maxlen);
}

void _starpu_get_perf_model_dir_debug(char *path, size_t maxlen)
{
	_starpu_get_perf_model_dir(path, maxlen);
	strncat(path, "/debug/", maxlen);
}

void _starpu_create_sampling_directory_if_needed(void)
{
	if (!directory_existence_was_tested)
	{
		char perf_model_dir[256];
		_starpu_get_perf_model_dir(perf_model_dir, 256);

		/* The performance of the codelets are stored in
		 * $STARPU_PERF_MODEL_DIR/codelets/ while those of the bus are stored in
		 * $STARPU_PERF_MODEL_DIR/bus/ so that we don't have name collisions */

		/* Testing if a directory exists and creating it otherwise
		   may not be safe: it is possible that the permission are
		   changed in between. Instead, we create it and check if
		   it already existed before */
		_starpu_mkpath_and_check(perf_model_dir, S_IRWXU);


		/* Per-task performance models */
		char perf_model_dir_codelets[256];
		_starpu_get_perf_model_dir_codelets(perf_model_dir_codelets, 256);
		_starpu_mkpath_and_check(perf_model_dir_codelets, S_IRWXU);

		/* Performance of the memory subsystem */
		char perf_model_dir_bus[256];
		_starpu_get_perf_model_dir_bus(perf_model_dir_bus, 256);
		_starpu_mkpath_and_check(perf_model_dir_bus, S_IRWXU);

		/* Performance debug measurements */
		char perf_model_dir_debug[256];
		_starpu_get_perf_model_dir_debug(perf_model_dir_debug, 256);
		_starpu_mkpath_and_check(perf_model_dir_debug, S_IRWXU);

		directory_existence_was_tested = 1;
	}
}
