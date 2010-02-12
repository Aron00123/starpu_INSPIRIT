/*
 * StarPU
 * Copyright (C) INRIA 2008-2010 (see AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#include <core/policies/deque-modeling-policy-data-aware.h>
#include <core/perfmodel/perfmodel.h>

static unsigned nworkers;
static struct jobq_s *queue_array[STARPU_NMAXWORKERS];
static int use_prefetch = 0;

static double alpha = 1.0;
static double beta = 1.0;

static job_t dmda_pop_task(struct jobq_s *q)
{
	struct job_s *j;

	j = fifo_pop_task(q);
	if (j) {
		struct fifo_jobq_s *fifo = q->queue;
		double model = j->predicted;
	
		fifo->exp_len -= model;
		fifo->exp_start = timing_now() + model;
		fifo->exp_end = fifo->exp_start + fifo->exp_len;
	}	

	return j;
}

static void update_data_requests(struct jobq_s *q, struct starpu_task *task)
{
	uint32_t memory_node = q->memory_node;
	unsigned nbuffers = task->cl->nbuffers;
	unsigned buffer;

	for (buffer = 0; buffer < nbuffers; buffer++)
	{
		starpu_data_handle handle = task->buffers[buffer].handle;

		set_data_requested_flag_if_needed(handle, memory_node);
	}
}

static int _dmda_push_task(struct jobq_s *q __attribute__ ((unused)) , job_t j, unsigned prio)
{
	/* find the queue */
	struct fifo_jobq_s *fifo;
	unsigned worker;
	int best = -1;
	
	/* this flag is set if the corresponding worker is selected because
	   there is no performance prediction available yet */
	int forced_best = -1;

	double local_task_length[nworkers];
	double local_data_penalty[nworkers];
	double exp_end[nworkers];

	double fitness[nworkers];

	double best_exp_end = 10e240;
	double model_best = 0.0;
	double penality_best = 0.0;

	struct starpu_task *task = j->task;

	for (worker = 0; worker < nworkers; worker++)
	{
		fifo = queue_array[worker]->queue;

		fifo->exp_start = STARPU_MAX(fifo->exp_start, timing_now());
		fifo->exp_end = STARPU_MAX(fifo->exp_end, timing_now());

		if ((queue_array[worker]->who & task->cl->where) == 0)
		{
			/* no one on that queue may execute this task */
			continue;
		}

		local_task_length[worker] = job_expected_length(queue_array[worker]->who,
							j, queue_array[worker]->arch);

		//local_data_penalty[worker] = 0;
		local_data_penalty[worker] = data_expected_penalty(queue_array[worker], task);

		if (local_task_length[worker] == -1.0)
		{
			forced_best = worker;
			break;
		}

		exp_end[worker] = fifo->exp_start + fifo->exp_len + local_task_length[worker];

		if (exp_end[worker] < best_exp_end)
		{
			/* a better solution was found */
			best_exp_end = exp_end[worker];
		}
	}

	double best_fitness = -1;
	
	if (forced_best == -1)
	{
		for (worker = 0; worker < nworkers; worker++)
		{
			fifo = queue_array[worker]->queue;
	
			if ((queue_array[worker]->who & task->cl->where) == 0)
			{
				/* no one on that queue may execute this task */
				continue;
			}
	
			fitness[worker] = alpha*(exp_end[worker] - best_exp_end) 
					+ beta*(local_data_penalty[worker]);

			if (best == -1 || fitness[worker] < best_fitness)
			{
				/* we found a better solution */
				best_fitness = fitness[worker];
				best = worker;

	//			fprintf(stderr, "best fitness (worker %d) %le = alpha*(%le) + beta(%le) \n", worker, best_fitness, exp_end[worker] - best_exp_end, local_data_penalty[worker]);
			}
		}
	}

	STARPU_ASSERT(forced_best != -1 || best != -1);
	
	if (forced_best != -1)
	{
		/* there is no prediction available for that task
		 * with that arch we want to speed-up calibration time
		 * so we force this measurement */
		best = worker;
		model_best = 0.0;
		penality_best = 0.0;
	}
	else 
	{
		model_best = local_task_length[best];
		penality_best = local_data_penalty[best];
	}

	/* we should now have the best worker in variable "best" */
	fifo = queue_array[best]->queue;

	fifo->exp_end += model_best;
	fifo->exp_len += model_best;

	j->predicted = model_best;
	j->penality = penality_best;

	update_data_requests(queue_array[best], task);
	
	if (use_prefetch)
		prefetch_task_input_on_node(task, queue_array[best]->memory_node);

	if (prio) {
		return fifo_push_prio_task(queue_array[best], j);
	} else {
		return fifo_push_task(queue_array[best], j);
	}
}

static int dmda_push_prio_task(struct jobq_s *q, job_t j)
{
	return _dmda_push_task(q, j, 1);
}

static int dmda_push_task(struct jobq_s *q, job_t j)
{
	if (j->task->priority == STARPU_MAX_PRIO)
		return _dmda_push_task(q, j, 1);

	return _dmda_push_task(q, j, 0);
}

static struct jobq_s *init_dmda_fifo(void)
{
	struct jobq_s *q;

	q = create_fifo();

	q->push_task = dmda_push_task; 
	q->push_prio_task = dmda_push_prio_task; 
	q->pop_task = dmda_pop_task;
	q->who = 0;

	queue_array[nworkers++] = q;

	return q;
}

static void initialize_dmda_policy(struct machine_config_s *config, 
	 __attribute__ ((unused)) struct sched_policy_s *_policy) 
{
	nworkers = 0;

	use_prefetch = starpu_get_env_number("PREFETCH");
	if (use_prefetch == -1)
		use_prefetch = 0;

	const char *strval_alpha = getenv("STARPU_SCHED_ALPHA");
	if (strval_alpha)
		beta = atof(strval_alpha);

	const char *strval_beta = getenv("STARPU_SCHED_BETA");
	if (strval_beta)
		beta = atof(strval_beta);

	setup_queues(init_fifo_queues_mechanisms, init_dmda_fifo, config);
}

static struct jobq_s *get_local_queue_dmda(struct sched_policy_s *policy __attribute__ ((unused)))
{
	struct jobq_s *queue;
	queue = pthread_getspecific(policy->local_queue_key);

	if (!queue)
	{
		/* take one randomly as this *must* be for a push anyway XXX */
		queue = queue_array[0];
	}

	return queue;
}

struct sched_policy_s sched_dmda_policy = {
	.init_sched = initialize_dmda_policy,
	.deinit_sched = NULL,
	.get_local_queue = get_local_queue_dmda,
	.policy_name = "dmda",
	.policy_description = "data-aware performance model"
};
