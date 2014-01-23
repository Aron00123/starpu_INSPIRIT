
/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2011 - 2013  INRIA
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

#include "sc_hypervisor_lp.h"
#include "sc_hypervisor_policy.h"
#include <starpu_config.h>
#include <sys/time.h>

int resize_no = 0;
#ifdef STARPU_HAVE_GLPK_H
static void _try_resizing(unsigned *sched_ctxs, int nsched_ctxs, int *workers, int nworkers)
{
	/* for vite */
	printf("resize_no = %d\n", resize_no);
	int ns = sched_ctxs == NULL ? sc_hypervisor_get_nsched_ctxs() : nsched_ctxs;

//	if(ns <= 1) return;

	unsigned *curr_sched_ctxs = sched_ctxs == NULL ? sc_hypervisor_get_sched_ctxs() : sched_ctxs;
	unsigned curr_nworkers = nworkers == -1 ? starpu_worker_get_count() : (unsigned)nworkers;
	
	struct types_of_workers *tw = sc_hypervisor_get_types_of_workers(workers, curr_nworkers);
	int nw = tw->nw;
	double nworkers_per_ctx[ns][nw];

	int total_nw[nw];
	sc_hypervisor_group_workers_by_type(tw, total_nw);
	
	
	struct timeval start_time;
	struct timeval end_time;
	gettimeofday(&start_time, NULL);
	
	double vmax = sc_hypervisor_lp_get_nworkers_per_ctx(ns, nw, nworkers_per_ctx, total_nw, tw, sched_ctxs);
	gettimeofday(&end_time, NULL);
	
	long diff_s = end_time.tv_sec  - start_time.tv_sec;
	long diff_us = end_time.tv_usec  - start_time.tv_usec;
	
	__attribute__((unused))	float timing = (float)(diff_s*1000000 + diff_us)/1000;
	
	if(vmax != 0.0)
	{
		int nworkers_per_ctx_rounded[nsched_ctxs][nw];
		sc_hypervisor_lp_round_double_to_int(ns, nw, nworkers_per_ctx, nworkers_per_ctx_rounded);
//		sc_hypervisor_lp_redistribute_resources_in_ctxs(ns, nw, nworkers_per_ctx_rounded, nworkers_per_ctx, curr_sched_ctxs, tw);
		sc_hypervisor_lp_distribute_resources_in_ctxs(curr_sched_ctxs, ns, nw, nworkers_per_ctx_rounded, nworkers_per_ctx, workers, curr_nworkers, tw);
		sc_hypervisor_lp_share_remaining_resources(ns, curr_sched_ctxs, curr_nworkers, workers);
	}
}

static void _try_resizing_hierarchically(unsigned levels, unsigned current_level, unsigned *sched_ctxs, unsigned nsched_ctxs, int *pus, int npus)
{
	if(levels == 0)
		return;

	_try_resizing(sched_ctxs, nsched_ctxs, pus, npus);

	int s;
	for(s = 0; s < nsched_ctxs; s++)
	{
		unsigned *sched_ctxs_child;
		int nsched_ctxs_child = 0;
		sc_hypervisor_get_ctxs_on_level(&sched_ctxs_child, &nsched_ctxs_child, current_level+1, sched_ctxs[s]);
		if(nsched_ctxs_child > 0)
		{
			int *pus_father;
			unsigned npus_father = 0;
			npus_father = starpu_sched_ctx_get_workers_list(sched_ctxs[s], &pus_father);
			
			_try_resizing_hierarchically(levels-1, current_level+1, sched_ctxs_child, nsched_ctxs_child, pus_father, npus_father);

			free(pus_father);
		}
	}
	return;
}

static int _get_min_level(unsigned *sched_ctxs, int nsched_ctxs)
{
	int min = sc_hypervisor_get_nhierarchy_levels();
	int s;
	for(s = 0; s < nsched_ctxs; s++)
	{
		int level = starpu_sched_ctx_get_hierarchy_level(sched_ctxs[s]);
		if(level < min)
			min = level;
	}
	return min;
}

static int _get_first_level(unsigned *sched_ctxs, int nsched_ctxs, unsigned *first_level, int *nsched_ctxs_first_level)
{
	int min = _get_min_level(sched_ctxs, nsched_ctxs);
	int s;
	for(s = 0; s < nsched_ctxs; s++)
		if(starpu_sched_ctx_get_hierarchy_level(sched_ctxs[s]) == min)
			first_level[(*nsched_ctxs_first_level)++] = sched_ctxs[s];
	return min;
}

static void _resize(unsigned *sched_ctxs, int nsched_ctxs, int *workers, int nworkers)
{
	starpu_fxt_trace_user_event(resize_no);
	unsigned nhierarchy_levels = sc_hypervisor_get_nhierarchy_levels();
	if(nhierarchy_levels > 1)
	{
		if(nsched_ctxs == -1)
		{
			unsigned *sched_ctxs2;
			int nsched_ctxs2;
			sc_hypervisor_get_ctxs_on_level(&sched_ctxs2, &nsched_ctxs2, 0, STARPU_NMAX_SCHED_CTXS);
			
			if(nsched_ctxs2  > 0)
			{
				_try_resizing_hierarchically(nhierarchy_levels, 0, sched_ctxs2, nsched_ctxs2, workers, nworkers);
				free(sched_ctxs2);
			}
		}
		else
		{
			unsigned first_level[nsched_ctxs];
			int nsched_ctxs_first_level = 0;
			int min = _get_first_level(sched_ctxs, nsched_ctxs, first_level, &nsched_ctxs_first_level);
			
			_try_resizing_hierarchically(nhierarchy_levels, min, first_level, nsched_ctxs_first_level, workers, nworkers);
		}
	}
	else
		_try_resizing(sched_ctxs, nsched_ctxs, workers, nworkers);
	resize_no++;
}

static void _resize_if_speed_diff(unsigned sched_ctx, int worker)
{
	unsigned nhierarchy_levels = sc_hypervisor_get_nhierarchy_levels();
	if(nhierarchy_levels > 1)
	{
		
		unsigned current_level = starpu_sched_ctx_get_hierarchy_level(sched_ctx);
		unsigned father = starpu_sched_ctx_get_inheritor(sched_ctx);
		int level;
		int *pus_father_old = NULL;
		unsigned npus_father_old = 0;
		unsigned *sched_ctxs_old = NULL;
		int nsched_ctxs_old = 0;
		unsigned is_speed_diff = 0;
		unsigned last_level_diff = 0;

		for(level = current_level ; level >= 0; level--)
		{
			int *pus_father;
			unsigned npus_father = 0;
			npus_father = starpu_sched_ctx_get_workers_list(father, &pus_father);
			
			
			unsigned *sched_ctxs;
			int nsched_ctxs;
			is_speed_diff = sc_hypervisor_check_speed_gap_btw_ctxs_on_level(level, pus_father, npus_father, father, &sched_ctxs, &nsched_ctxs);
			if(!is_speed_diff)
			{
				if(level == current_level)
					break;
				else
				{
					_resize(sched_ctxs_old, nsched_ctxs_old, pus_father_old, npus_father_old);
					free(pus_father_old);
					free(sched_ctxs_old);
					pus_father_old = NULL;
					sched_ctxs_old = NULL;
					free(pus_father);
					free(sched_ctxs);
					break;
				}
			}	
			if(pus_father_old)
				free(pus_father_old);
			if(sched_ctxs_old)
				free(sched_ctxs_old);
			pus_father_old = pus_father;
			sched_ctxs_old = sched_ctxs;
			npus_father_old = npus_father;
			nsched_ctxs_old = nsched_ctxs;
			
			father = starpu_sched_ctx_get_inheritor(father);
		}
		if(is_speed_diff)
		{
			_resize(NULL, -1, NULL, -1);
		}
	}
	else if(sc_hypervisor_check_speed_gap_btw_ctxs(NULL, -1, NULL, -1))
	{
		_resize(NULL, -1, NULL, -1);
	}
}

static void feft_lp_handle_poped_task(unsigned sched_ctx, int worker, 
				      __attribute__((unused))struct starpu_task *task, __attribute__((unused))uint32_t footprint)
{
	unsigned criteria = sc_hypervisor_get_resize_criteria();
	if(criteria != SC_NOTHING && criteria == SC_SPEED)
	{

		int ret = starpu_pthread_mutex_trylock(&act_hypervisor_mutex);
		if(ret != EBUSY)
		{
			_resize_if_speed_diff(sched_ctx, worker);
			starpu_pthread_mutex_unlock(&act_hypervisor_mutex);
		}
	}
}

static void feft_lp_size_ctxs(unsigned *sched_ctxs, int nsched_ctxs, int *workers, int nworkers)
{
	starpu_pthread_mutex_lock(&act_hypervisor_mutex);

	struct sc_hypervisor_wrapper* sc_w  = NULL;
	int s = 0;
	for(s = 0; s < nsched_ctxs; s++)
	{
		sc_w = sc_hypervisor_get_wrapper(sched_ctxs[s]);
		sc_w->to_be_sized = 1;
	}

	_resize(sched_ctxs, nsched_ctxs, workers, nworkers);
	printf("finished size ctxs\n");
	starpu_pthread_mutex_unlock(&act_hypervisor_mutex);
}

static void feft_lp_handle_idle_cycle(unsigned sched_ctx, int worker)
{
	unsigned criteria = sc_hypervisor_get_resize_criteria();
	if(criteria != SC_NOTHING)// && criteria == SC_IDLE)
	{
		int ret = starpu_pthread_mutex_trylock(&act_hypervisor_mutex);
		if(ret != EBUSY)
		{
//			printf("trigger idle \n");
			_resize_if_speed_diff(sched_ctx, worker);
			starpu_pthread_mutex_unlock(&act_hypervisor_mutex);
		}
	}
}

static void feft_lp_resize_ctxs(unsigned *sched_ctxs, int nsched_ctxs , 
				int *workers, int nworkers)
{
	int ret = starpu_pthread_mutex_trylock(&act_hypervisor_mutex);
	if(ret != EBUSY)
	{
		struct sc_hypervisor_wrapper* sc_w  = NULL;
		int s = 0;
		for(s = 0; s < nsched_ctxs; s++)
		{
			 sc_w = sc_hypervisor_get_wrapper(sched_ctxs[s]);
			
			 if((sc_w->submitted_flops + (0.1*sc_w->total_flops)) < sc_w->total_flops)
			 {
				 starpu_pthread_mutex_unlock(&act_hypervisor_mutex);
				 return;
			 }
		}

		_resize(sched_ctxs, nsched_ctxs, workers, nworkers);

		starpu_pthread_mutex_unlock(&act_hypervisor_mutex);
	}
}

struct sc_hypervisor_policy feft_lp_policy = {
	.size_ctxs = feft_lp_size_ctxs,
	.resize_ctxs = feft_lp_resize_ctxs,
	.handle_poped_task = feft_lp_handle_poped_task,
	.handle_pushed_task = NULL,
	.handle_idle_cycle = feft_lp_handle_idle_cycle,
	.handle_idle_end = NULL,
	.handle_post_exec_hook = NULL,
	.handle_submitted_job = NULL,
	.end_ctx = NULL,
	.custom = 0,
	.name = "feft_lp"
};

#endif /* STARPU_HAVE_GLPK_H */
