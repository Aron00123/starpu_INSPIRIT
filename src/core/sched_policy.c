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

#include <pthread.h>

#include <starpu.h>
#include <common/config.h>
#include <common/utils.h>
#include <core/sched_policy.h>
#include <profiling/profiling.h>

//static struct starpu_sched_policy_s policy;

static int use_prefetch = 0;
static pthread_cond_t blocking_ths_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t blocking_ths_mutex = PTHREAD_MUTEX_INITIALIZER;
static int nblocked_ths = 0;

int starpu_get_prefetch_flag(void)
{
	return use_prefetch;
}

/*
 *	Predefined policies
 */

extern struct starpu_sched_policy_s _starpu_sched_ws_policy;
extern struct starpu_sched_policy_s _starpu_sched_prio_policy;
extern struct starpu_sched_policy_s _starpu_sched_no_prio_policy;
extern struct starpu_sched_policy_s _starpu_sched_random_policy;
extern struct starpu_sched_policy_s _starpu_sched_dm_policy;
extern struct starpu_sched_policy_s _starpu_sched_dmda_policy;
extern struct starpu_sched_policy_s _starpu_sched_dmda_ready_policy;
extern struct starpu_sched_policy_s _starpu_sched_dmda_sorted_policy;
extern struct starpu_sched_policy_s _starpu_sched_eager_policy;
extern struct starpu_sched_policy_s _starpu_sched_parallel_heft_policy;
extern struct starpu_sched_policy_s _starpu_sched_pgreedy_policy;
extern struct starpu_sched_policy_s heft_policy;

#define NPREDEFINED_POLICIES	12

static struct starpu_sched_policy_s *predefined_policies[NPREDEFINED_POLICIES] = {
	&_starpu_sched_ws_policy,
	&_starpu_sched_prio_policy,
	&_starpu_sched_no_prio_policy,
	&_starpu_sched_dm_policy,
	&_starpu_sched_dmda_policy,
	&heft_policy,
	&_starpu_sched_dmda_ready_policy,
	&_starpu_sched_dmda_sorted_policy,
	&_starpu_sched_random_policy,
	&_starpu_sched_eager_policy,
	&_starpu_sched_parallel_heft_policy,
	&_starpu_sched_pgreedy_policy
};

struct starpu_sched_policy_s *_starpu_get_sched_policy(struct starpu_sched_ctx *sched_ctx)
{
	return sched_ctx->sched_policy;
}

/*
 *	Methods to initialize the scheduling policy
 */

static void load_sched_policy(struct starpu_sched_policy_s *sched_policy, struct starpu_sched_ctx *sched_ctx)
{
	STARPU_ASSERT(sched_policy);

#ifdef STARPU_VERBOSE
	if (sched_policy->policy_name)
	{
		if (sched_policy->policy_description)
                        _STARPU_DEBUG("Use %s scheduler (%s)\n", sched_policy->policy_name, sched_policy->policy_description);
                else
                        _STARPU_DEBUG("Use %s scheduler \n", sched_policy->policy_name);

	}
#endif
        struct starpu_sched_policy_s *policy = sched_ctx->sched_policy;

	policy->init_sched = sched_policy->init_sched;
	policy->deinit_sched = sched_policy->deinit_sched;
	policy->push_task = sched_policy->push_task;
	policy->push_prio_task = sched_policy->push_prio_task;
	policy->pop_task = sched_policy->pop_task;
        policy->post_exec_hook = sched_policy->post_exec_hook;
	policy->pop_every_task = sched_policy->pop_every_task;
	policy->policy_name = sched_policy->policy_name;
}

static struct starpu_sched_policy_s *find_sched_policy_from_name(const char *policy_name)
{

	if (!policy_name)
		return NULL;

	unsigned i;
	for (i = 0; i < NPREDEFINED_POLICIES; i++)
	{
		struct starpu_sched_policy_s *p;
		p = predefined_policies[i];
		if (p->policy_name)
		{
			if (strcmp(policy_name, p->policy_name) == 0) {
				/* we found a policy with the requested name */
				return p;
			}
		}
	}

	/* nothing was found */
	return NULL;
}

static void display_sched_help_message(void)
{
	const char *sched_env = getenv("STARPU_SCHED");
	if (sched_env && (strcmp(sched_env, "help") == 0)) {
		fprintf(stderr, "STARPU_SCHED can be either of\n");

		/* display the description of all predefined policies */
		unsigned i;
		for (i = 0; i < NPREDEFINED_POLICIES; i++)
		{
			struct starpu_sched_policy_s *p;
			p = predefined_policies[i];
			fprintf(stderr, "%s\t-> %s\n", p->policy_name, p->policy_description);
		}
	 }
}

static struct starpu_sched_policy_s *select_sched_policy(struct starpu_machine_config_s *config, const char *policy_name)
{
	struct starpu_sched_policy_s *selected_policy = NULL;
	struct starpu_conf *user_conf = config->user_conf;

	/* First, we check whether the application explicitely gave a scheduling policy or not */
	if (user_conf && (user_conf->sched_policy))
		return user_conf->sched_policy;

	/* Otherwise, we look if the application specified the name of a policy to load */
	const char *sched_pol_name;
	if (user_conf && (user_conf->sched_policy_name))
	{
		sched_pol_name = user_conf->sched_policy_name;
	}
	else {
		sched_pol_name = getenv("STARPU_SCHED");
	}

	if (sched_pol_name)
		selected_policy = find_sched_policy_from_name(sched_pol_name);
	else
		if(policy_name)
			selected_policy = find_sched_policy_from_name(policy_name);

	/* Perhaps there was no policy that matched the name */
	if (selected_policy)
		return selected_policy;

	/* If no policy was specified, we use the greedy policy as a default */
	return &_starpu_sched_eager_policy;
}

void _starpu_init_sched_policy(struct starpu_machine_config_s *config, struct starpu_sched_ctx *sched_ctx, const char *policy_name)
{
	/* Perhaps we have to display some help */
	display_sched_help_message();

	/* Prefetch is activated by default */
	use_prefetch = starpu_get_env_number("STARPU_PREFETCH");
	if (use_prefetch == -1)
		use_prefetch = 1;
  

	/* By default, we don't calibrate */
	unsigned do_calibrate = 0;
	if (config->user_conf && (config->user_conf->calibrate != -1))
	{
		do_calibrate = config->user_conf->calibrate;
	}
	else {
		int res = starpu_get_env_number("STARPU_CALIBRATE");
		do_calibrate =  (res < 0)?0:(unsigned)res;
	}

	_starpu_set_calibrate_flag(do_calibrate);

	struct starpu_sched_policy_s *selected_policy;
	selected_policy = select_sched_policy(config, policy_name);

	load_sched_policy(selected_policy, sched_ctx);

	sched_ctx->sched_policy->init_sched(sched_ctx);
}

void _starpu_deinit_sched_policy(struct starpu_machine_config_s *config, struct starpu_sched_ctx *sched_ctx)
{
        struct starpu_sched_policy_s *policy = sched_ctx->sched_policy;
	if (policy->deinit_sched)
		policy->deinit_sched(sched_ctx);
}

/* Enqueue a task into the list of tasks explicitely attached to a worker. In
 * case workerid identifies a combined worker, a task will be enqueued into
 * each worker of the combination. */
static int _starpu_push_task_on_specific_worker(struct starpu_task *task, int workerid)
{
	int nbasic_workers = (int)starpu_worker_get_count();

	/* Is this a basic worker or a combined worker ? */
	int is_basic_worker = (workerid < nbasic_workers);

	unsigned memory_node; 
	struct starpu_worker_s *worker;
	struct starpu_combined_worker_s *combined_worker;

	if (is_basic_worker)
	{
		worker = _starpu_get_worker_struct(workerid);
		memory_node = worker->memory_node;
	}
	else
	{
		combined_worker = _starpu_get_combined_worker_struct(workerid);
		memory_node = combined_worker->memory_node;
	}

	if (use_prefetch)
		starpu_prefetch_task_input_on_node(task, memory_node);

	unsigned i;
	for(i = 0; i < worker->nctxs; i++){
		if (worker->sched_ctx[i]->sched_policy->push_task_notify)
			worker->sched_ctx[i]->sched_policy->push_task_notify(task, workerid);
	}

	if (is_basic_worker)
	{
		return _starpu_push_local_task(worker, task, 0);
	}
	else {
		/* This is a combined worker so we create task aliases */
		int worker_size = combined_worker->worker_size;
		int *combined_workerid = combined_worker->combined_workerid;

		int ret = 0;
		int i;

		starpu_job_t j = _starpu_get_job_associated_to_task(task);
		j->task_size = worker_size;
		j->combined_workerid = workerid;
		j->active_task_alias_count = 0;

		PTHREAD_BARRIER_INIT(&j->before_work_barrier, NULL, worker_size);
		PTHREAD_BARRIER_INIT(&j->after_work_barrier, NULL, worker_size);

		for (i = 0; i < worker_size; i++)
		{
			struct starpu_task *alias = _starpu_create_task_alias(task);

			worker = _starpu_get_worker_struct(combined_workerid[i]);
			ret |= _starpu_push_local_task(worker, alias, 0);
		}

		return ret;
	}
}

/* the generic interface that call the proper underlying implementation */
int _starpu_push_task(starpu_job_t j, unsigned job_is_already_locked)
{
	struct starpu_task *task = j->task;
        _STARPU_LOG_IN();

	task->status = STARPU_TASK_READY;
	_starpu_profiling_set_task_push_start_time(task);

	/* in case there is no codelet associated to the task (that's a control
	 * task), we directly execute its callback and enforce the
	 * corresponding dependencies */
	if (task->cl == NULL)
	{
		_starpu_handle_job_termination(j, job_is_already_locked);
                _STARPU_LOG_OUT_TAG("handle_job_termination");
		return 0;
	}

        int ret;
	if (STARPU_UNLIKELY(task->execute_on_a_specific_worker))
	{
		ret = _starpu_push_task_on_specific_worker(task, task->workerid);
	}
	else {
	        struct starpu_sched_ctx *sched_ctx = task->sched_ctx;
		STARPU_ASSERT(sched_ctx->sched_policy->push_task);
		ret = sched_ctx->sched_policy->push_task(task, sched_ctx);
	}

	_starpu_profiling_set_task_push_end_time(task);

        _STARPU_LOG_OUT();
        return ret;
}

struct starpu_task *_starpu_pop_task(struct starpu_worker_s *worker)
{
	struct starpu_task *task;

	/* We can't tell in advance which task will be picked up, so we measure
	 * a timestamp, and will attribute it afterwards to the task. */
	int profiling = starpu_profiling_status_get();
	struct timespec pop_start_time;
	if (profiling)
		starpu_clock_gettime(&pop_start_time);

	/* perhaps there is some local task to be executed first */
	task = _starpu_pop_local_task(worker);

	if(!task){
		struct starpu_sched_ctx *sched_ctx;
		unsigned i;
		for(i = 0; i < worker->nctxs; i++){
			sched_ctx = worker->sched_ctx[i];
			if (sched_ctx->sched_policy->pop_task){
				task = sched_ctx->sched_policy->pop_task();
				break;
			}
		}
	}

	if(task){
	  printf("task %s poped by th %d with strateg %s\n", task->name, worker->workerid, task->sched_ctx->sched_policy->policy_name);
	}

	/* Note that we may get a NULL task in case the scheduler was unlocked
	 * for some reason. */
	if (profiling && task)
	{
		struct starpu_task_profiling_info *profiling_info;
		profiling_info = task->profiling_info;

		/* The task may have been created before profiling was enabled,
		 * so we check if the profiling_info structure is available
		 * even though we already tested if profiling is enabled. */
		if (profiling_info)
		{
			memcpy(&profiling_info->pop_start_time,
				&pop_start_time, sizeof(struct timespec));
			starpu_clock_gettime(&profiling_info->pop_end_time);
		}
	}

	return task;
}

struct starpu_task *_starpu_pop_every_task(struct starpu_sched_ctx *sched_ctx)
{
	STARPU_ASSERT(sched_ctx->sched_policy->pop_every_task);

	/* TODO set profiling info */
	return sched_ctx->sched_policy->pop_every_task();
}

void _starpu_sched_post_exec_hook(struct starpu_task *task)
{
	if (task->sched_ctx->sched_policy->post_exec_hook)
		task->sched_ctx->sched_policy->post_exec_hook(task);
}

void _starpu_wait_on_sched_event(void)
{
	struct starpu_worker_s *worker = _starpu_get_local_worker_key();

	PTHREAD_MUTEX_LOCK(worker->sched_mutex);

	_starpu_handle_all_pending_node_data_requests(worker->memory_node);

	if (_starpu_machine_is_running())
	{
#ifndef STARPU_NON_BLOCKING_DRIVERS
		pthread_cond_wait(worker->sched_cond, worker->sched_mutex);
#endif
	}

	PTHREAD_MUTEX_UNLOCK(worker->sched_mutex);
}

/* The scheduling policy may put tasks directly into a worker's local queue so
 * that it is not always necessary to create its own queue when the local queue
 * is sufficient. If "back" not null, the task is put at the back of the queue
 * where the worker will pop tasks first. Setting "back" to 0 therefore ensures
 * a FIFO ordering. */
int starpu_push_local_task(int workerid, struct starpu_task *task, int back)
{
	struct starpu_worker_s *worker = _starpu_get_worker_struct(workerid);

	return _starpu_push_local_task(worker, task, back);
}

static int starpu_get_ctx_id(struct starpu_sched_ctx *sched_ctx, struct starpu_worker_s *worker){
	unsigned i;
	for(i = 0; i < worker->nctxs; i++)
		if(worker->sched_ctx[i] == sched_ctx)
			return i;
	return -1;
}

void _starpu_create_sched_ctx(struct starpu_sched_ctx *sched_ctx, const char *policy_name, int *workerids_in_ctx, int nworkerids_in_ctx, unsigned is_init_sched)
{
	sched_ctx->nworkers_in_ctx = nworkerids_in_ctx;
	sched_ctx->sched_policy = malloc(sizeof(struct starpu_sched_policy_s));
	sched_ctx->is_init_sched = is_init_sched;


	struct starpu_machine_config_s *config = _starpu_get_machine_config();
	int nworkers = config->topology.nworkers;
       
	int j;
	/*all the workers are in this contex*/
	if(workerids_in_ctx == NULL){
		for(j = 0; j < nworkers; j++){
			sched_ctx->workerid[j] = j;
			struct starpu_worker_s *workerarg = &config->workers[j];
			workerarg->sched_ctx[workerarg->nctxs++] = sched_ctx;
		}
		sched_ctx->nworkers_in_ctx = nworkers;
	} else {
		int i;
		for(i = 0; i < nworkerids_in_ctx; i++){
			sched_ctx->workerid[i] = workerids_in_ctx[i];
			for(j = 0; j < nworkers; j++){
				if(sched_ctx->workerid[i] == j){
					struct starpu_worker_s *workerarg = &config->workers[j];
					workerarg->sched_ctx[workerarg->nctxs++] = sched_ctx;
				}
			}
		}
	}

	_starpu_init_sched_policy(config, sched_ctx, policy_name);

	return;
}

static int _starpu_wait_for_all_threads_to_block(int nworkers)
{
	PTHREAD_MUTEX_LOCK(&blocking_ths_mutex);

	while (nblocked_ths < nworkers)
		PTHREAD_COND_WAIT(&blocking_ths_cond, &blocking_ths_mutex);
       
	PTHREAD_MUTEX_UNLOCK(&blocking_ths_mutex);

	return 0;
}

static void _starpu_decrement_nblocked_ths(void)
{
	PTHREAD_MUTEX_LOCK(&blocking_ths_mutex);

	nblocked_ths--;
	
	PTHREAD_MUTEX_UNLOCK(&blocking_ths_mutex);
}

void _starpu_increment_nblocked_ths(int nworkers)
{
	PTHREAD_MUTEX_LOCK(&blocking_ths_mutex);

	if (++nblocked_ths == nworkers)
		PTHREAD_COND_BROADCAST(&blocking_ths_cond);

	PTHREAD_MUTEX_UNLOCK(&blocking_ths_mutex);
}

int set_changing_ctx_flag(starpu_worker_status changing_ctx, int nworkerids_in_ctx, int *workerids_in_ctx){
	struct starpu_machine_config_s *config = _starpu_get_machine_config();

	int i;
	int nworkers = nworkerids_in_ctx == -1 ? config->topology.nworkers : nworkerids_in_ctx;
	
	struct starpu_worker_s *worker = NULL;
	pthread_mutex_t *changing_ctx_mutex = NULL;
	pthread_cond_t *changing_ctx_cond = NULL;

	int workerid = -1;
	
	for(i = 0; i < nworkers; i++){
		workerid = workerids_in_ctx == NULL ? i : workerids_in_ctx[i];
		worker = &config->workers[workerid];
		
		changing_ctx_mutex = &worker->changing_ctx_mutex;
		changing_ctx_cond = &worker->changing_ctx_cond;

		/*if the status is CHANGING_CTX let the thread know that it must block*/
		PTHREAD_MUTEX_LOCK(changing_ctx_mutex);
		worker->status = changing_ctx;
		worker->nworkers_of_next_ctx = nworkers;
		PTHREAD_MUTEX_UNLOCK(changing_ctx_mutex);
		
		/*if we have finished changing the ctx wake up the blocked threads*/
		if(changing_ctx == STATUS_UNKNOWN){
			PTHREAD_MUTEX_LOCK(changing_ctx_mutex);
			PTHREAD_COND_SIGNAL(changing_ctx_cond);
			PTHREAD_MUTEX_UNLOCK(changing_ctx_mutex);
			_starpu_decrement_nblocked_ths();
		}
	}

	/*after letting know all the concered threads about the change
	  wait for them to take into account the info*/
	if(changing_ctx == STATUS_CHANGING_CTX)
		_starpu_wait_for_all_threads_to_block(nworkers);

	return 0;
}

void starpu_create_sched_ctx(struct starpu_sched_ctx *sched_ctx, const char *policy_name, int *workerids_in_ctx, int nworkerids_in_ctx){
    	  if(!starpu_task_wait_for_all()){
		/*block the workers until the contex is switched*/
		set_changing_ctx_flag(STATUS_CHANGING_CTX, nworkerids_in_ctx, workerids_in_ctx);
		_starpu_create_sched_ctx(sched_ctx, policy_name, workerids_in_ctx, nworkerids_in_ctx, 0);
		set_changing_ctx_flag(STATUS_UNKNOWN, nworkerids_in_ctx, workerids_in_ctx);
	  }
	  return;
}

int worker_belongs_to_ctx(struct starpu_worker_s *workerarg, struct starpu_sched_ctx *sched_ctx){
	unsigned i;
	for(i = 0; i < workerarg->nctxs; i++)
		if(sched_ctx != NULL && workerarg->sched_ctx[i] == sched_ctx 
		   && workerarg->status != STATUS_JOINED)
		  return 1;
	return 0;

}
void starpu_delete_sched_ctx(struct starpu_sched_ctx *sched_ctx)
{
	struct starpu_machine_config_s *config = _starpu_get_machine_config();
	int nworkers = config->topology.nworkers;
       
	int i;
	for(i = 0; i < nworkers; i++){
		struct starpu_worker_s *workerarg = &config->workers[i];
		if(worker_belongs_to_ctx(workerarg, sched_ctx))
			workerarg->nctxs--;
	}

	free(sched_ctx->sched_policy);
	sched_ctx->sched_policy = NULL;
}

void _starpu_delete_all_sched_ctxs(){
  	struct starpu_machine_config_s *config = _starpu_get_machine_config();
	int nworkers = config->topology.nworkers;

	unsigned i, j;
	struct starpu_sched_ctx *sched_ctx = NULL;
	struct starpu_worker_s *workerarg = NULL;
	for(i = 0; i < nworkers; i++){
		workerarg = &config->workers[i];
		for(j = 0; j < workerarg->nctxs; j++){
			sched_ctx = workerarg->sched_ctx[j];
			if(sched_ctx != NULL && !sched_ctx->is_init_sched){
				free(sched_ctx->sched_policy);
				sched_ctx->sched_policy = NULL;
				workerarg->nctxs--;
			}
		}
	}
}
