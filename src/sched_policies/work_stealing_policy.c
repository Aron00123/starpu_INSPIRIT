/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010-2016  Université de Bordeaux
 * Copyright (C) 2010, 2011, 2012, 2013  CNRS
 * Copyright (C) 2011, 2012  INRIA
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

/* Work stealing policy */

#include <float.h>

#include <core/workers.h>
#include <sched_policies/fifo_queues.h>
#include <core/debug.h>
#include <starpu_scheduler.h>

#ifdef HAVE_AYUDAME_H
#include <Ayudame.h>
#endif

/* Experimental (dead) code which needs to be tested, fixed... */
/* #define USE_OVERLOAD */

struct _starpu_work_stealing_data
{
	unsigned (*select_victim)(unsigned, int);

	struct _starpu_fifo_taskq **queue_array;
	int **proxlist;
	/* keep track of the work performed from the beginning of the algorithm to make
	 * better decisions about which queue to select when stealing or deferring work
	 */
	unsigned last_pop_worker;
	unsigned last_push_worker;
};

#ifdef USE_OVERLOAD

/**
 * Minimum number of task we wait for being processed before we start assuming
 * on which worker the computation would be faster.
 */
static int calibration_value = 0;

#endif /* USE_OVERLOAD */


/**
 * Return a worker from which a task can be stolen.
 * Selecting a worker is done in a round-robin fashion, unless
 * the worker previously selected doesn't own any task,
 * then we return the first non-empty worker.
 */
static unsigned select_victim_round_robin(unsigned sched_ctx_id)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	unsigned worker = ws->last_pop_worker;
	unsigned nworkers = starpu_sched_ctx_get_nworkers(sched_ctx_id);
	int *workerids = NULL;
	starpu_sched_ctx_get_workers_list(sched_ctx_id, &workerids);

	/* If the worker's queue is empty, let's try
	 * the next ones */
	while (1)
	{
		unsigned ntasks;

		/* Here helgrind would shout that this is unprotected, but we
		 * are fine with getting outdated values, this is just an
		 * estimation */
		ntasks = ws->queue_array[workerids[worker]]->ntasks;

		if (ntasks)
			break;

		worker = (worker + 1) % nworkers;
		if (worker == ws->last_pop_worker)
		{
			/* We got back to the first worker,
			 * don't go in infinite loop */
			break;
		}
	}

	ws->last_pop_worker = (worker + 1) % nworkers;

	worker = workerids[worker];
	free(workerids);

	return worker;
}

/**
 * Return a worker to whom add a task.
 * Selecting a worker is done in a round-robin fashion.
 */
static unsigned select_worker_round_robin(unsigned sched_ctx_id)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	unsigned worker = ws->last_push_worker;
	unsigned nworkers = starpu_sched_ctx_get_nworkers(sched_ctx_id);
	int *workerids = NULL;
	starpu_sched_ctx_get_workers_list(sched_ctx_id, &workerids);

	ws->last_push_worker = (ws->last_push_worker + 1) % nworkers;

	worker = workerids[worker];
	free(workerids);

	return worker;
}

#ifdef USE_OVERLOAD

/**
 * Return a ratio helpful to determine whether a worker is suitable to steal
 * tasks from or to put some tasks in its queue.
 *
 * \return	a ratio with a positive or negative value, describing the current state of the worker :
 * 		a smaller value implies a faster worker with an relatively emptier queue : more suitable to put tasks in
 * 		a bigger value implies a slower worker with an reletively more replete queue : more suitable to steal tasks from
 */
static float overload_metric(unsigned sched_ctx_id, unsigned id)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	float execution_ratio = 0.0f;
	float current_ratio = 0.0f;

	int nprocessed = _starpu_get_deque_nprocessed(ws->queue_array[id]);
	unsigned njobs = _starpu_get_deque_njobs(ws->queue_array[id]);

	/* Did we get enough information ? */
	if (performed_total > 0 && nprocessed > 0)
	{
		/* How fast or slow is the worker compared to the other workers */
		execution_ratio = (float) nprocessed / performed_total;
		/* How replete is its queue */
		current_ratio = (float) njobs / nprocessed;
	}
	else
	{
		return 0.0f;
	}

	return (current_ratio - execution_ratio);
}

/**
 * Return the most suitable worker from which a task can be stolen.
 * The number of previously processed tasks, total and local,
 * and the number of tasks currently awaiting to be processed
 * by the tasks are taken into account to select the most suitable
 * worker to steal task from.
 */
static unsigned select_victim_overload(unsigned sched_ctx_id)
{
	unsigned worker;
	float  worker_ratio;
	unsigned best_worker = 0;
	float best_ratio = FLT_MIN;

	/* Don't try to play smart until we get
	 * enough informations. */
	if (performed_total < calibration_value)
		return select_victim_round_robin(sched_ctx_id);

	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);

	struct starpu_sched_ctx_iterator it;

	workers->init_iterator(workers, &it);
	while(workers->has_next(workers, &it))
	{
                worker = workers->get_next(workers, &it);
		worker_ratio = overload_metric(sched_ctx_id, worker);

		if (worker_ratio > best_ratio)
		{
			best_worker = worker;
			best_ratio = worker_ratio;
		}
	}

	return best_worker;
}

/**
 * Return the most suitable worker to whom add a task.
 * The number of previously processed tasks, total and local,
 * and the number of tasks currently awaiting to be processed
 * by the tasks are taken into account to select the most suitable
 * worker to add a task to.
 */
static unsigned select_worker_overload(unsigned sched_ctx_id)
{
	unsigned worker;
	float  worker_ratio;
	unsigned best_worker = 0;
	float best_ratio = FLT_MAX;

	/* Don't try to play smart until we get
	 * enough informations. */
	if (performed_total < calibration_value)
		return select_worker_round_robin(sched_ctx_id);

	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);

	struct starpu_sched_ctx_iterator it;

	workers->init_iterator(workers, &it);
	while(workers->has_next(workers, &it))
	{
		worker = workers->get_next(workers, &it);

		worker_ratio = overload_metric(sched_ctx_id, worker);

		if (worker_ratio < best_ratio)
		{
			best_worker = worker;
			best_ratio = worker_ratio;
		}
	}

	return best_worker;
}

#endif /* USE_OVERLOAD */


/**
 * Return a worker from which a task can be stolen.
 * This is a phony function used to call the right
 * function depending on the value of USE_OVERLOAD.
 */
static inline unsigned select_victim(unsigned sched_ctx_id,
				     int workerid STARPU_ATTRIBUTE_UNUSED)
{
#ifdef USE_OVERLOAD
	return select_victim_overload(sched_ctx_id);
#else
	return select_victim_round_robin(sched_ctx_id);
#endif /* USE_OVERLOAD */
}

/**
 * Return a worker from which a task can be stolen.
 * This is a phony function used to call the right
 * function depending on the value of USE_OVERLOAD.
 */
static inline unsigned select_worker(unsigned sched_ctx_id)
{
#ifdef USE_OVERLOAD
	return select_worker_overload(sched_ctx_id);
#else
	return select_worker_round_robin(sched_ctx_id);
#endif /* USE_OVERLOAD */
}


/* Note: this is not scalable work stealing,  use lws instead */
static struct starpu_task *ws_pop_task(unsigned sched_ctx_id)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	struct starpu_task *task;
	int workerid = starpu_worker_get_id();

	STARPU_ASSERT(workerid != -1);

	task = _starpu_fifo_pop_task(ws->queue_array[workerid], workerid);
	if (task)
	{
		/* there was a local task */
		return task;
	}
	starpu_pthread_mutex_t *worker_sched_mutex;
	starpu_pthread_cond_t *worker_sched_cond;
	starpu_worker_get_sched_condition(workerid, &worker_sched_mutex, &worker_sched_cond);

	/* we need to steal someone's job */
	unsigned victim = ws->select_victim(sched_ctx_id, workerid);

	/* Note: Releasing this mutex before taking the victim mutex, to avoid interlock*/
	STARPU_PTHREAD_MUTEX_UNLOCK_SCHED(worker_sched_mutex);

	starpu_pthread_mutex_t *victim_sched_mutex;
	starpu_pthread_cond_t *victim_sched_cond;

	starpu_worker_get_sched_condition(victim, &victim_sched_mutex, &victim_sched_cond);
	STARPU_PTHREAD_MUTEX_LOCK_SCHED(victim_sched_mutex);

	if (ws->queue_array[victim] != NULL && ws->queue_array[victim]->ntasks > 0)
		task = _starpu_fifo_pop_task(ws->queue_array[victim], workerid);
	if (task)
	{
		_STARPU_TRACE_WORK_STEALING(workerid, victim);
	}

	STARPU_PTHREAD_MUTEX_UNLOCK_SCHED(victim_sched_mutex);

	STARPU_PTHREAD_MUTEX_LOCK_SCHED(worker_sched_mutex);
	if(!task)
	{
		if (ws->queue_array[workerid] != NULL && ws->queue_array[workerid]->ntasks > 0)
			task = _starpu_fifo_pop_task(ws->queue_array[workerid], workerid);
		if (task)
		{
			/* there was a local task */
			return task;
		}
	}

	return task;
}

static
int ws_push_task(struct starpu_task *task)
{
	unsigned sched_ctx_id = task->sched_ctx;
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	int workerid;

pick_worker:
	workerid = starpu_worker_get_id();

	/* If the current thread is not a worker but
	 * the main thread (-1), we find the better one to
	 * put task on its queue */
	if (workerid == -1)
		workerid = select_worker(sched_ctx_id);

	starpu_pthread_mutex_t *sched_mutex;
	starpu_pthread_cond_t *sched_cond;
	starpu_worker_get_sched_condition(workerid, &sched_mutex, &sched_cond);
	STARPU_PTHREAD_MUTEX_LOCK_SCHED(sched_mutex);

	/* Maybe the worker we selected was removed before we picked the mutex */
	if (ws->queue_array[workerid] == NULL)
		goto pick_worker;

#ifdef HAVE_AYUDAME_H
	struct _starpu_job *j = _starpu_get_job_associated_to_task(task);
	if (AYU_event)
	{
		intptr_t id = workerid;
		AYU_event(AYU_ADDTASKTOQUEUE, j->job_id, &id);
	}
#endif

	_starpu_fifo_push_task(ws->queue_array[workerid], task);

	starpu_push_task_end(task);

	STARPU_PTHREAD_MUTEX_UNLOCK_SCHED(sched_mutex);

#if !defined(STARPU_NON_BLOCKING_DRIVERS) || defined(STARPU_SIMGRID)
	/* TODO: implement fine-grain signaling, similar to what eager does */
	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);
	struct starpu_sched_ctx_iterator it;

	workers->init_iterator(workers, &it);
	while(workers->has_next(workers, &it))
		starpu_wake_worker(workers->get_next(workers, &it));
#endif
	return 0;
}

static void ws_add_workers(unsigned sched_ctx_id, int *workerids,unsigned nworkers)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	unsigned i;
	int workerid;

	for (i = 0; i < nworkers; i++)
	{
		workerid = workerids[i];
		starpu_sched_ctx_worker_shares_tasks_lists(workerid, sched_ctx_id);
		ws->queue_array[workerid] = _starpu_create_fifo();

		/* Tell helgrid that we are fine with getting outdated values,
		 * this is just an estimation */
		STARPU_HG_DISABLE_CHECKING(ws->queue_array[workerid]->ntasks);
	}
}

static void ws_remove_workers(unsigned sched_ctx_id, int *workerids, unsigned nworkers)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	unsigned i;
	int workerid;

	for (i = 0; i < nworkers; i++)
	{
		starpu_pthread_mutex_t *sched_mutex;
		starpu_pthread_cond_t *sched_cond;

		workerid = workerids[i];
		starpu_worker_get_sched_condition(workerid, &sched_mutex, &sched_cond);
		STARPU_PTHREAD_MUTEX_LOCK_SCHED(sched_mutex);
		if (ws->queue_array[workerid] != NULL)
		{
			_starpu_destroy_fifo(ws->queue_array[workerid]);
			ws->queue_array[workerid] = NULL;
		}
		if (ws->proxlist != NULL)
			free(ws->proxlist[workerid]);
		STARPU_PTHREAD_MUTEX_UNLOCK_SCHED(sched_mutex);
	}
}

static void initialize_ws_policy(unsigned sched_ctx_id)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)malloc(sizeof(struct _starpu_work_stealing_data));
	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)ws);

	ws->last_pop_worker = 0;
	ws->last_push_worker = 0;
	ws->proxlist = NULL;
	ws->select_victim = select_victim;

	unsigned nw = starpu_worker_get_count();
	ws->queue_array = (struct _starpu_fifo_taskq**)malloc(nw*sizeof(struct _starpu_fifo_taskq*));
}

static void deinit_ws_policy(unsigned sched_ctx_id)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	free(ws->queue_array);
	free(ws->proxlist);
	free(ws);
}

struct starpu_sched_policy _starpu_sched_ws_policy =
{
	.init_sched = initialize_ws_policy,
	.deinit_sched = deinit_ws_policy,
	.add_workers = ws_add_workers,
	.remove_workers = ws_remove_workers,
	.push_task = ws_push_task,
	.pop_task = ws_pop_task,
	.pre_exec_hook = NULL,
	.post_exec_hook = NULL,
	.pop_every_task = NULL,
	.policy_name = "ws",
	.policy_description = "work stealing",
	.worker_type = STARPU_WORKER_LIST,
};

/* local work stealing policy */
/* Return a worker to steal a task from. The worker is selected according to
 * the proximity list built using the info on te architecture provided by hwloc
 */
static unsigned lws_select_victim(unsigned sched_ctx_id, int workerid)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data *)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	int nworkers = starpu_sched_ctx_get_nworkers(sched_ctx_id);
	int neighbor;
	int i;

	for (i = 0; i < nworkers; i++)
	{
		neighbor = ws->proxlist[workerid][i];
		/* if a worker was removed, then nothing tells us that the proxlist is correct */
		if (!starpu_sched_ctx_contains_worker(neighbor, sched_ctx_id))
		{
			i--;
			continue;
		}
		int ntasks = ws->queue_array[neighbor]->ntasks;
		if (ntasks)
			return neighbor;
	}
	return workerid;
}

static void lws_add_workers(unsigned sched_ctx_id, int *workerids,
			    unsigned nworkers)
{
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	unsigned i;
	int workerid;

	ws_add_workers(sched_ctx_id, workerids, nworkers);

#ifdef STARPU_HAVE_HWLOC
	/* Build a proximity list for every worker. It is cheaper to
	 * build this once and then use it for popping tasks rather
	 * than traversing the hwloc tree every time a task must be
	 * stolen */
	ws->proxlist = (int**)malloc(starpu_worker_get_count()*sizeof(int*));
	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);
	struct starpu_tree *tree = (struct starpu_tree*)workers->workerids;
	for (i = 0; i < nworkers; i++)
	{
		workerid = workerids[i];
		ws->proxlist[workerid] = (int*)malloc(nworkers*sizeof(int));
		int bindid;

		struct starpu_tree *neighbour = NULL;
		struct starpu_sched_ctx_iterator it;

		workers->init_iterator(workers, &it);

		bindid   = starpu_worker_get_bindid(workerid);
		it.value = starpu_tree_get(tree, bindid);
		int cnt = 0;
		for(;;)
		{
			neighbour = (struct starpu_tree*)it.value;
			int *neigh_workerids;
			int neigh_nworkers = starpu_bindid_get_workerids(neighbour->id, &neigh_workerids);
			int w;
			for(w = 0; w < neigh_nworkers; w++)
			{
				if(!it.visited[neigh_workerids[w]] && workers->present[neigh_workerids[w]])
				{
					ws->proxlist[workerid][cnt++] = neigh_workerids[w];
					it.visited[neigh_workerids[w]] = 1;
				}
			}
			if(!workers->has_next(workers, &it))
				break;
			it.value = it.possible_value;
			it.possible_value = NULL;
		}
	}
#endif
}

static void initialize_lws_policy(unsigned sched_ctx_id)
{
	/* lws is loosely based on ws, except that it might use hwloc. */
	initialize_ws_policy(sched_ctx_id);

#ifdef STARPU_HAVE_HWLOC
	struct _starpu_work_stealing_data *ws = (struct _starpu_work_stealing_data *)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	ws->select_victim = lws_select_victim;
#endif
}

struct starpu_sched_policy _starpu_sched_lws_policy =
{
	.init_sched = initialize_lws_policy,
	.deinit_sched = deinit_ws_policy,
	.add_workers = lws_add_workers,
	.remove_workers = ws_remove_workers,
	.push_task = ws_push_task,
	.pop_task = ws_pop_task,
	.pre_exec_hook = NULL,
	.post_exec_hook = NULL,
	.pop_every_task = NULL,
	.policy_name = "lws",
	.policy_description = "locality work stealing",
#ifdef STARPU_HAVE_HWLOC
	.worker_type = STARPU_WORKER_TREE,
#else
	.worker_type = STARPU_WORKER_LIST,
#endif
};
