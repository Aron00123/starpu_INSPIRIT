/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2011  INRIA
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

#include <core/sched_policy.h>
#include <core/sched_ctx.h>
#include <common/utils.h>

pthread_key_t sched_ctx_key;

static unsigned _starpu_get_first_available_sched_ctx_id(struct starpu_machine_config_s *config);
static unsigned _starpu_get_first_free_sched_ctx_in_worker_list(struct starpu_worker_s *worker);
static void _starpu_rearange_sched_ctx_workerids(struct starpu_sched_ctx *sched_ctx, int old_nworkerids_in_ctx);

struct sched_ctx_info {
	unsigned sched_ctx_id;
	struct starpu_sched_ctx *sched_ctx;
	struct starpu_worker_s *worker;
};

static void update_workers_func(void *buffers[] __attribute__ ((unused)), void *_args)
{
	struct sched_ctx_info *sched_ctx_info_args = (struct sched_ctx_info*)_args;
	struct starpu_worker_s *worker = sched_ctx_info_args->worker;
	struct starpu_sched_ctx *current_sched_ctx = sched_ctx_info_args->sched_ctx;
	unsigned sched_ctx_id = sched_ctx_info_args->sched_ctx_id;

	if(current_sched_ctx != NULL)
	  {
		/* add context to worker */
		worker->sched_ctx[sched_ctx_info_args->sched_ctx_id] = current_sched_ctx;
		worker->nctxs++;
	  }
	else
	  {
		/* remove context from worker */
		int i;
		for(i = 0; i < STARPU_NMAX_SCHED_CTXS; i++)
			if(worker->sched_ctx[i] != NULL && worker->sched_ctx[i]->sched_ctx_id == sched_ctx_id)
			  {
				worker->sched_ctx[i] = NULL;
				worker->nctxs--;
			  }
	  }
}

static void _starpu_update_workers(int *workerids_in_ctx, int nworkerids_in_ctx, 
				   int sched_ctx_id, struct starpu_sched_ctx *sched_ctx)
{
	struct starpu_task *tasks[nworkerids_in_ctx];

	struct starpu_codelet_t sched_ctx_info_cl = {
		.where = STARPU_CPU|STARPU_CUDA|STARPU_OPENCL,
		.cuda_func = update_workers_func,
		.cpu_func = update_workers_func,
		.opencl_func = update_workers_func,
		.nbuffers = 0
	};

	int i, ret;
	struct starpu_worker_s *worker[nworkerids_in_ctx];
	struct sched_ctx_info sched_info_args[nworkerids_in_ctx];
	for(i = 0; i < nworkerids_in_ctx; i++)
	  {
		worker[i] = _starpu_get_worker_struct(workerids_in_ctx[i]);
		
		sched_info_args[i].sched_ctx_id = sched_ctx_id == -1  ? 
			_starpu_get_first_free_sched_ctx_in_worker_list(worker[i]) : 
			(unsigned)sched_ctx_id;

		sched_info_args[i].sched_ctx = sched_ctx;
		sched_info_args[i].worker = worker[i];
		
		tasks[i] = starpu_task_create();
		tasks[i]->cl = &sched_ctx_info_cl;
		tasks[i]->cl_arg = &sched_info_args[i];
		tasks[i]->execute_on_a_specific_worker = 1;
		tasks[i]->workerid = workerids_in_ctx[i];
		tasks[i]->detach = 0;
		tasks[i]->destroy = 0;

		_starpu_exclude_task_from_dag(tasks[i]);

		ret = _starpu_task_submit_internal(tasks[i]);
		if (ret == -ENODEV)
		  {
		    /* if the worker is not able to execute this tasks, we
		     * don't insist as this means the worker is not
		     * designated by the "where" bitmap */
		    starpu_task_destroy(tasks[i]);
		    tasks[i] = NULL;
		  }
	  }

	for (i = 0; i < nworkerids_in_ctx; i++)
	  {
	    if (tasks[i])
	      {
		ret = starpu_task_wait(tasks[i]);
		STARPU_ASSERT(!ret);
		starpu_task_destroy(tasks[i]);
	      }
	  }

}
struct starpu_sched_ctx*  _starpu_create_sched_ctx(const char *policy_name, int *workerids_in_ctx, 
				  int nworkerids_in_ctx, unsigned is_initial_sched,
				  const char *sched_name)
{
	struct starpu_machine_config_s *config = (struct starpu_machine_config_s *)_starpu_get_machine_config();
	STARPU_ASSERT(config->topology.nsched_ctxs < STARPU_NMAX_SCHED_CTXS - 1);

	unsigned id = _starpu_get_first_available_sched_ctx_id(config);

	struct starpu_sched_ctx *sched_ctx = &config->sched_ctxs[id];
	sched_ctx->sched_ctx_id = id;
	int nworkers = config->topology.nworkers;
	
	STARPU_ASSERT(nworkerids_in_ctx <= nworkers);
  
	sched_ctx->nworkers_in_ctx = nworkerids_in_ctx;
	sched_ctx->temp_nworkers_in_ctx = -1;
	PTHREAD_MUTEX_INIT(&sched_ctx->changing_ctx_mutex, NULL);

	sched_ctx->sched_policy = malloc(sizeof(struct starpu_sched_policy_s));
	sched_ctx->is_initial_sched = is_initial_sched;
	sched_ctx->sched_name = sched_name;

	_starpu_barrier_counter_init(&sched_ctx->tasks_barrier, 0);

	int j;
	/* if null add all the workers are to the contex */
	if(workerids_in_ctx == NULL)
	  {
		for(j = 0; j < nworkers; j++)
		  {
			sched_ctx->workerid[j] = j;
		  }
		sched_ctx->nworkers_in_ctx = nworkers;
	  } 
	else 
	  {
		int i;
		for(i = 0; i < nworkerids_in_ctx; i++)
		  {
			/* the user should not ask for a resource that does not exist */
			STARPU_ASSERT( workerids_in_ctx[i] >= 0 &&  workerids_in_ctx[i] <= nworkers);		    
			sched_ctx->workerid[i] = workerids_in_ctx[i];

		  }
	  }

	/* initialise all sync structures bc the number of workers can modify */
	sched_ctx->sched_mutex = (pthread_mutex_t**)malloc(STARPU_NMAXWORKERS* sizeof(pthread_mutex_t*));
	sched_ctx->sched_cond = (pthread_cond_t**)malloc(STARPU_NMAXWORKERS *sizeof(pthread_cond_t*));


	_starpu_init_sched_policy(config, sched_ctx, policy_name);

	config->topology.nsched_ctxs++;	

	/* if we create the initial big sched ctx we can update workers' status here
	   because they haven't been launched yet */
	if(is_initial_sched)
	  {
	    int i;
	    for(i = 0; i < sched_ctx->nworkers_in_ctx; i++)
	      {
		struct starpu_worker_s *worker = _starpu_get_worker_struct(sched_ctx->workerid[i]);
		worker->sched_ctx[_starpu_get_first_free_sched_ctx_in_worker_list(worker)] = sched_ctx;
	      }
	  }

	return sched_ctx;
}


unsigned starpu_create_sched_ctx(const char *policy_name, int *workerids_in_ctx, 
			    int nworkerids_in_ctx, const char *sched_name)
{
	struct starpu_sched_ctx *sched_ctx = _starpu_create_sched_ctx(policy_name, workerids_in_ctx, nworkerids_in_ctx, 0, sched_name);
	_starpu_update_workers(sched_ctx->workerid, sched_ctx->nworkers_in_ctx, -1, sched_ctx);
	return sched_ctx->sched_ctx_id;
}

/* check if the worker already belongs to the context */
static unsigned _starpu_worker_belongs_to_ctx(int workerid, struct starpu_sched_ctx *sched_ctx)
{
	int i;
	for(i = 0; i < sched_ctx->nworkers_in_ctx; i++)
	  if(sched_ctx->workerid[i] == workerid)
		  return 1;
	return 0;
}

/* free all structures for the context */
static void free_sched_ctx_mem(struct starpu_sched_ctx *sched_ctx)
{
	free(sched_ctx->sched_policy);
	free(sched_ctx->sched_mutex);
	free(sched_ctx->sched_cond);
	/* just for debug in order to seg fault if we use these structures after del */
	sched_ctx->sched_policy = NULL;
	sched_ctx->sched_mutex = NULL;
	sched_ctx->sched_cond = NULL;
	struct starpu_machine_config_s *config = _starpu_get_machine_config();
	config->topology.nsched_ctxs--;
	sched_ctx->sched_ctx_id = STARPU_NMAX_SCHED_CTXS;

}

static void _starpu_manage_delete_sched_ctx(struct starpu_sched_ctx *sched_ctx)
{
	_starpu_update_workers(sched_ctx->workerid, sched_ctx->nworkers_in_ctx, 
			       sched_ctx->sched_ctx_id, NULL);
}

static void _starpu_add_workers_to_sched_ctx(int *new_workers, int nnew_workers,
					     struct starpu_sched_ctx *sched_ctx)
{
        struct starpu_machine_config_s *config = (struct starpu_machine_config_s *)_starpu_get_machine_config();
        int ntotal_workers = config->topology.nworkers;
        int nworkerids_already_in_ctx =  sched_ctx->nworkers_in_ctx;
	
	int n_added_workers = 0;
	int added_workers[ntotal_workers];

        /*if null add the rest of the workers which don't already belong to this ctx*/
        if(new_workers == NULL)
          {        
		int j;
                for(j = 0; j < ntotal_workers; j++)
                        if(!_starpu_worker_belongs_to_ctx(j, sched_ctx))
			  {
                                sched_ctx->workerid[++nworkerids_already_in_ctx] = j;
				added_workers[n_added_workers] = j;
			  }
                          
		n_added_workers = ntotal_workers;
          }
        else
          {
                int i;
                for(i = 0; i < nnew_workers; i++)
                  {
                        /* take care the user does not ask for a resource that does not exist */
                        STARPU_ASSERT( new_workers[i] >= 0 &&  new_workers[i] <= ntotal_workers);

			if(!_starpu_worker_belongs_to_ctx(new_workers[i], sched_ctx))
			  {
			    /* add worker to context */
			    sched_ctx->workerid[ nworkerids_already_in_ctx + n_added_workers] = new_workers[i];
			    added_workers[n_added_workers] = new_workers[i];
			    n_added_workers++;
			  }
                  }
          }

        sched_ctx->sched_policy->init_sched_for_workers(sched_ctx->sched_ctx_id, n_added_workers);

	_starpu_update_workers(added_workers, n_added_workers, -1, sched_ctx);

        return;
}

void starpu_delete_sched_ctx(unsigned sched_ctx_id, unsigned inheritor_sched_ctx_id)
{
	if(!starpu_wait_for_all_tasks_of_sched_ctx(sched_ctx_id))
	  {

		struct starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx(sched_ctx_id);
		struct starpu_sched_ctx *inheritor_sched_ctx = _starpu_get_sched_ctx(inheritor_sched_ctx_id);

		_starpu_manage_delete_sched_ctx(sched_ctx);

		/*if both of them have all the ressources is pointless*/
		/*trying to transfer ressources from one ctx to the other*/
		struct starpu_machine_config_s *config = (struct starpu_machine_config_s *)_starpu_get_machine_config();
		int ntotal_workers = config->topology.nworkers;

		if(!(sched_ctx->nworkers_in_ctx == ntotal_workers && sched_ctx->nworkers_in_ctx == inheritor_sched_ctx->nworkers_in_ctx))
		  _starpu_add_workers_to_sched_ctx(sched_ctx->workerid, sched_ctx->nworkers_in_ctx, inheritor_sched_ctx);
		free_sched_ctx_mem(sched_ctx);

	  }		
	return;	
}

/* called after the workers are terminated so we don't have anything else to do but free the memory*/
void _starpu_delete_all_sched_ctxs()
{
	unsigned i;
	for(i = 0; i < STARPU_NMAX_SCHED_CTXS; i++)
	  {
		struct starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx(i);
		_starpu_barrier_counter_destroy(&sched_ctx->tasks_barrier);
		if(sched_ctx->sched_ctx_id != STARPU_NMAX_SCHED_CTXS)
			free_sched_ctx_mem(sched_ctx);
	  }
	return;
}

void starpu_add_workers_to_sched_ctx(int *workerids_in_ctx, int nworkerids_in_ctx,
				     unsigned sched_ctx_id)
{
	struct starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx(sched_ctx_id);
	_starpu_add_workers_to_sched_ctx(workerids_in_ctx, nworkerids_in_ctx, sched_ctx);

	return;
}

static void _starpu_remove_workers_from_sched_ctx(int *workerids_in_ctx, int nworkerids_in_ctx, 
					  struct starpu_sched_ctx *sched_ctx)
{
  	struct starpu_machine_config_s *config = (struct starpu_machine_config_s *)_starpu_get_machine_config();
	int nworkers = config->topology.nworkers;
	int nworkerids_already_in_ctx =  sched_ctx->nworkers_in_ctx;

	STARPU_ASSERT(nworkerids_in_ctx  <= nworkerids_already_in_ctx);

	int i, workerid;

	int nremoved_workers = 0;
	int removed_workers[nworkers];

	/*if null remove all the workers that belong to this ctx*/
	if(workerids_in_ctx == NULL)
	  {
		for(i = 0; i < nworkerids_already_in_ctx; i++)
		  {
			removed_workers[i] = sched_ctx->workerid[i];
			sched_ctx->workerid[i] = -1;
		  }

		sched_ctx->nworkers_in_ctx = 0;
		nremoved_workers = nworkerids_already_in_ctx;
	  } 
	else 
	  {
		for(i = 0; i < nworkerids_in_ctx; i++)
		  {
		    	workerid = workerids_in_ctx[i]; 
			/* take care the user does not ask for a resource that does not exist */
			STARPU_ASSERT( workerid >= 0 &&  workerid <= nworkers);
			removed_workers[i] = sched_ctx->workerid[i];

			int j;
			/* don't leave the workerid with a correct value even if we don't use it anymore */
			for(j = 0; j < nworkerids_already_in_ctx; j++)
				if(sched_ctx->workerid[j] == workerid)				 
					sched_ctx->workerid[j] = -1;
		  }

		nremoved_workers = nworkerids_in_ctx;
		sched_ctx->nworkers_in_ctx -= nworkerids_in_ctx;
		/* reorder the worker's list of contexts in order to avoid 
		   the holes in the list after removing some elements */
		_starpu_rearange_sched_ctx_workerids(sched_ctx, nworkerids_already_in_ctx);
	  }

	_starpu_update_workers(removed_workers, nremoved_workers, sched_ctx->sched_ctx_id, NULL);

	return;
}

void starpu_remove_workers_from_sched_ctx(int *workerids_in_ctx, int nworkerids_in_ctx, 
					  unsigned sched_ctx_id)
{
	  /* wait for the workers concerned by the change of contex    
	   * to finish their work in the previous context */
	if(!starpu_wait_for_all_tasks_of_workers(workerids_in_ctx, nworkerids_in_ctx))
	  {
		struct starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx(sched_ctx_id);

		PTHREAD_MUTEX_LOCK(&sched_ctx->changing_ctx_mutex);
		_starpu_remove_workers_from_sched_ctx(workerids_in_ctx, nworkerids_in_ctx, sched_ctx);
		PTHREAD_MUTEX_UNLOCK(&sched_ctx->changing_ctx_mutex);
	  }
	return;
}

/* unused sched_ctx have the id STARPU_NMAX_SCHED_CTXS */
void _starpu_init_all_sched_ctx(struct starpu_machine_config_s *config)
{
	pthread_key_create(&sched_ctx_key, NULL);

	unsigned i;
	for(i = 0; i < STARPU_NMAX_SCHED_CTXS; i++)
		config->sched_ctxs[i].sched_ctx_id = STARPU_NMAX_SCHED_CTXS;

	return;
}

/* unused sched_ctx pointers of a worker are NULL */
void _starpu_init_sched_ctx_for_worker(unsigned workerid)
{
	struct starpu_worker_s *worker = _starpu_get_worker_struct(workerid);
	worker->sched_ctx = (struct starpu_sched_ctx**)malloc(STARPU_NMAX_SCHED_CTXS * sizeof(struct starpu_sched_ctx*));
	unsigned i;
	for(i = 0; i < STARPU_NMAX_SCHED_CTXS; i++)
		worker->sched_ctx[i] = NULL;

	return;
}

/* sched_ctx aren't necessarly one next to another */
/* for eg when we remove one its place is free */
/* when we add  new one we reuse its place */
static unsigned _starpu_get_first_available_sched_ctx_id(struct starpu_machine_config_s *config)
{
	unsigned i;
	for(i = 0; i < STARPU_NMAX_SCHED_CTXS; i++)
		if(config->sched_ctxs[i].sched_ctx_id == STARPU_NMAX_SCHED_CTXS)
			return i;

	STARPU_ASSERT(0);
	return STARPU_NMAX_SCHED_CTXS;
}

static unsigned _starpu_get_first_free_sched_ctx_in_worker_list(struct starpu_worker_s *worker)
{
	unsigned i;
	for(i = 0; i < STARPU_NMAX_SCHED_CTXS; i++)
		if(worker->sched_ctx[i] == NULL)
			return i;
	STARPU_ASSERT(0);
	return STARPU_NMAX_SCHED_CTXS;
}

static int _starpu_get_first_free_worker_space(int *workerids, int old_nworkerids_in_ctx)
{
	int i;
	for(i = 0; i < old_nworkerids_in_ctx; i++)
		if(workerids[i] == -1)
			return i;

	return -1;
}

/* rearange array of workerids in order not to have {-1, -1, 5, -1, 7}
   and have instead {5, 7, -1, -1, -1} 
   it is easier afterwards to iterate the array
*/
static void _starpu_rearange_sched_ctx_workerids(struct starpu_sched_ctx *sched_ctx, int old_nworkerids_in_ctx)
{
	int first_free_id = -1;
	int i;
	for(i = 0; i < old_nworkerids_in_ctx; i++)
	  {
		if(sched_ctx->workerid[i] != -1)
		  {
			first_free_id = _starpu_get_first_free_worker_space(sched_ctx->workerid, old_nworkerids_in_ctx);
			if(first_free_id != -1)
			  {
				sched_ctx->workerid[first_free_id] = sched_ctx->workerid[i];
				sched_ctx->workerid[i] = -1;
			  }
		  }
	  }
}

int starpu_wait_for_all_tasks_of_worker(int workerid)
{
	if (STARPU_UNLIKELY(!_starpu_worker_may_perform_blocking_calls()))
		return -EDEADLK;

	struct starpu_worker_s *worker = _starpu_get_worker_struct(workerid);
	
	_starpu_barrier_counter_wait_for_empty_counter(&worker->tasks_barrier);
	
	return 0;
}

int starpu_wait_for_all_tasks_of_workers(int *workerids_in_ctx, int nworkerids_in_ctx){
	int ret_val = 0;
	
	struct starpu_machine_config_s *config = _starpu_get_machine_config();
	int nworkers = nworkerids_in_ctx == -1 ? (int)config->topology.nworkers : nworkerids_in_ctx;
	
	int workerid = -1;
	int i, n;
	
	for(i = 0; i < nworkers; i++)
	  {
		workerid = workerids_in_ctx == NULL ? i : workerids_in_ctx[i];
		n = starpu_wait_for_all_tasks_of_worker(workerid);
		ret_val = (ret_val && n);
	  }
	
	return ret_val;
}

void _starpu_decrement_nsubmitted_tasks_of_worker(int workerid)
{
	struct starpu_worker_s *worker = _starpu_get_worker_struct(workerid);
	
	_starpu_barrier_counter_decrement_until_empty_counter(&worker->tasks_barrier);

	return;
}

void _starpu_increment_nsubmitted_tasks_of_worker(int workerid)
{
	struct starpu_worker_s *worker = _starpu_get_worker_struct(workerid);

	_starpu_barrier_counter_increment(&worker->tasks_barrier);

	return;
}

int starpu_wait_for_all_tasks_of_sched_ctx(unsigned sched_ctx_id)
{
	struct starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx(sched_ctx_id);
	
	if (STARPU_UNLIKELY(!_starpu_worker_may_perform_blocking_calls()))
	  return -EDEADLK;
	
	return _starpu_barrier_counter_wait_for_empty_counter(&sched_ctx->tasks_barrier);
}

void _starpu_decrement_nsubmitted_tasks_of_sched_ctx(unsigned sched_ctx_id)
{
	struct starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx(sched_ctx_id);
	_starpu_barrier_counter_decrement_until_empty_counter(&sched_ctx->tasks_barrier);
//	printf("%d: %d\n", sched_ctx_id, sched_ctx->tasks_barrier.barrier.reached_start);
}

void _starpu_increment_nsubmitted_tasks_of_sched_ctx(unsigned sched_ctx_id)
{
	struct starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx(sched_ctx_id);
	_starpu_barrier_counter_increment(&sched_ctx->tasks_barrier);
}

int _starpu_get_index_in_ctx_of_workerid(unsigned sched_ctx_id, unsigned workerid)
{
	struct starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx(sched_ctx_id);
	
	int nworkers_in_ctx = sched_ctx->nworkers_in_ctx;

	int i;
	for(i = 0; i < nworkers_in_ctx; i++)
		if(sched_ctx->workerid[i] == (int)workerid)
			return i;
	
	return -1;
}

pthread_mutex_t *_starpu_get_sched_mutex(struct starpu_sched_ctx *sched_ctx, int worker)
{
	int workerid_ctx = _starpu_get_index_in_ctx_of_workerid(sched_ctx->sched_ctx_id, worker);
	return (workerid_ctx == -1 ? NULL : sched_ctx->sched_mutex[workerid_ctx]);
}

pthread_cond_t *_starpu_get_sched_cond(struct starpu_sched_ctx *sched_ctx, int worker)
{
	int workerid_ctx = _starpu_get_index_in_ctx_of_workerid(sched_ctx->sched_ctx_id, worker);
	return (workerid_ctx == -1 ? NULL : sched_ctx->sched_cond[workerid_ctx]);
}

void starpu_set_sched_ctx(unsigned *sched_ctx)
{
	pthread_setspecific(sched_ctx_key, (void*)sched_ctx);
}

unsigned starpu_get_sched_ctx()
{
	unsigned sched_ctx = *(unsigned*)pthread_getspecific(sched_ctx_key);
	STARPU_ASSERT(sched_ctx >= 0 && sched_ctx < STARPU_NMAX_SCHED_CTXS);
	return sched_ctx;
}

unsigned _starpu_get_nsched_ctxs()
{
	struct starpu_machine_config_s *config = (struct starpu_machine_config_s *)_starpu_get_machine_config();
	return config->topology.nsched_ctxs;
}
