/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013  INRIA
 * Copyright (C) 2013  Simon Archipoff
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

#include <core/jobs.h>
#include <core/workers.h>
#include <starpu_sched_node.h>
#include <starpu_thread_util.h>

#include <float.h>

#include "sched_node.h"


/* wake up worker workerid
 * if called by a worker it dont try to wake up himself
 */
static void wake_simple_worker(int workerid)
{
	STARPU_ASSERT(0 <= workerid && (unsigned)  workerid < starpu_worker_get_count());
	starpu_pthread_mutex_t * sched_mutex;
	starpu_pthread_cond_t * sched_cond;
	if(workerid == starpu_worker_get_id())
		return;
	starpu_worker_get_sched_condition(workerid, &sched_mutex, &sched_cond);
	starpu_wakeup_worker(workerid, sched_cond, sched_mutex);
	/*
	STARPU_PTHREAD_MUTEX_LOCK(sched_mutex);
	STARPU_PTHREAD_COND_SIGNAL(sched_cond);
	STARPU_PTHREAD_MUTEX_UNLOCK(sched_mutex);
	*/
}

/* wake up all workers of a combined workers
 * this function must not be called during a pop (however this should not
 * even be possible) or you will have a dead lock
 */
static void wake_combined_worker(int workerid)
{
	STARPU_ASSERT( 0 <= workerid
		       && starpu_worker_get_count() <= (unsigned) workerid
		       && (unsigned) workerid < starpu_worker_get_count() + starpu_combined_worker_get_count());
	struct _starpu_combined_worker * combined_worker = _starpu_get_combined_worker_struct(workerid);
	int * list = combined_worker->combined_workerid;
	int size = combined_worker->worker_size;
	int i;
	for(i = 0; i < size; i++)
		wake_simple_worker(list[i]);
}


/* this function must not be called on worker nodes :
 * because this wouldn't have sense
 * and should dead lock
 */
void starpu_sched_node_wake_available_worker(struct starpu_sched_node * node, struct starpu_task * task)
{
	(void)node;
	STARPU_ASSERT(node);
	STARPU_ASSERT(!starpu_sched_node_is_worker(node));
#ifndef STARPU_NON_BLOCKING_DRIVERS
	int i;
	unsigned nimpl;
	for(i = starpu_bitmap_first(node->workers_in_ctx);
	    i != -1;
	    i = starpu_bitmap_next(node->workers_in_ctx, i))
	{
		for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		{
			if (starpu_worker_can_execute_task(i, task, nimpl))
			{
				if(i < (int) starpu_worker_get_count())
					wake_simple_worker(i);
				else
					wake_combined_worker(i);
				goto out;
			}
		}
	}
out:
#endif
}


/* this function must not be called on worker nodes :
 * because this wouldn't have sense
 * and should dead lock
 */
void starpu_sched_node_available(struct starpu_sched_node * node)
{
	(void)node;
	STARPU_ASSERT(node);
	STARPU_ASSERT(!starpu_sched_node_is_worker(node));
#ifndef STARPU_NON_BLOCKING_DRIVERS
	int i;
	for(i = starpu_bitmap_first(node->workers_in_ctx);
	    i != -1;
	    i = starpu_bitmap_next(node->workers_in_ctx, i))
	{
		if(i < (int) starpu_worker_get_count())
			wake_simple_worker(i);
		else
			wake_combined_worker(i);
	}
#endif
}

/* default implementation for node->pop_task()
 * just perform a recursive call on father
 */
static struct starpu_task * pop_task_node(struct starpu_sched_node * node)
{
	STARPU_ASSERT(node);
	struct starpu_task * task = NULL;
	int i;
	for(i=0; i < node->nfathers; i++)
	{
		if(node->fathers[i] == NULL)
			continue;
		else
		{
			task = node->fathers[i]->pop_task(node->fathers[i]);
			if(task)
				break;
		}
	}
	return task;
}

void starpu_sched_node_add_father(struct starpu_sched_node* node, struct starpu_sched_node * father)
{
	STARPU_ASSERT(node && father);
	int i;
	for(i = 0; i < node->nfathers; i++){
		STARPU_ASSERT(node->fathers[i] != node);
		STARPU_ASSERT(node->fathers[i] != NULL);
	}

	node->fathers = realloc(node->fathers, sizeof(struct starpu_sched_node *) * (node->nfathers + 1));
	node->fathers[node->nfathers] = father;
	node->nfathers++;
}

void starpu_sched_node_remove_father(struct starpu_sched_node * node, struct starpu_sched_node * father)
{
	STARPU_ASSERT(node && father);
	int pos;
	for(pos = 0; pos < node->nfathers; pos++)
		if(node->fathers[pos] == father)
			break;
	STARPU_ASSERT(pos != node->nfathers);
	node->fathers[pos] = node->fathers[--node->nfathers];
}


/******************************************************************************
 *          functions for struct starpu_sched_policy interface                *
 ******************************************************************************/
int starpu_sched_tree_push_task(struct starpu_task * task)
{
	STARPU_ASSERT(task);
	unsigned sched_ctx_id = task->sched_ctx;
	struct starpu_sched_tree *tree = starpu_sched_ctx_get_policy_data(sched_ctx_id);
	int workerid = starpu_worker_get_id();
	/* application should take tree->lock to prevent concurent acces from hypervisor
	 * worker take they own mutexes
	 */
	if(-1 == workerid)
		STARPU_PTHREAD_MUTEX_LOCK(&tree->lock);
	else
		_starpu_sched_node_lock_worker(workerid);
		
	int ret_val = tree->root->push_task(tree->root,task);
	if(-1 == workerid)
		STARPU_PTHREAD_MUTEX_UNLOCK(&tree->lock);
	else
		_starpu_sched_node_unlock_worker(workerid);
	return ret_val;
}

struct starpu_task * starpu_sched_tree_pop_task(unsigned sched_ctx STARPU_ATTRIBUTE_UNUSED)
{
	int workerid = starpu_worker_get_id();
	struct starpu_sched_node * node = starpu_sched_node_worker_get(workerid);

	/* _starpu_sched_node_lock_worker(workerid) is called by node->pop_task()
	 */
	struct starpu_task * task = node->pop_task(node);
	return task;
}

void starpu_sched_tree_add_workers(unsigned sched_ctx_id, int *workerids, unsigned nworkers)
{
	STARPU_ASSERT(sched_ctx_id < STARPU_NMAX_SCHED_CTXS);
	STARPU_ASSERT(workerids);
	struct starpu_sched_tree * t = starpu_sched_ctx_get_policy_data(sched_ctx_id);

	STARPU_PTHREAD_MUTEX_LOCK(&t->lock);
	_starpu_sched_node_lock_all_workers();

	unsigned i;
	for(i = 0; i < nworkers; i++)
		starpu_bitmap_set(t->workers, workerids[i]);

	starpu_sched_tree_update_workers_in_ctx(t);

	_starpu_sched_node_unlock_all_workers();
	STARPU_PTHREAD_MUTEX_UNLOCK(&t->lock);
}

void starpu_sched_tree_remove_workers(unsigned sched_ctx_id, int *workerids, unsigned nworkers)
{
	STARPU_ASSERT(sched_ctx_id < STARPU_NMAX_SCHED_CTXS);
	STARPU_ASSERT(workerids);
	struct starpu_sched_tree * t = starpu_sched_ctx_get_policy_data(sched_ctx_id);

	STARPU_PTHREAD_MUTEX_LOCK(&t->lock);
	_starpu_sched_node_lock_all_workers();

	unsigned i;
	for(i = 0; i < nworkers; i++)
		starpu_bitmap_unset(t->workers, workerids[i]);

	starpu_sched_tree_update_workers_in_ctx(t);

	_starpu_sched_node_unlock_all_workers();
	STARPU_PTHREAD_MUTEX_UNLOCK(&t->lock);
}




void starpu_sched_node_destroy_rec(struct starpu_sched_node * node)
{
	if(node == NULL)
		return;

	int i;
	if(node->nchilds > 0)
	{
		for(i=0; i < node->nchilds; i++)
			starpu_sched_node_destroy_rec(node->childs[i]);
	}

	starpu_sched_node_destroy(node);
}

struct starpu_sched_tree * starpu_sched_tree_create(unsigned sched_ctx_id)
{
	STARPU_ASSERT(sched_ctx_id < STARPU_NMAX_SCHED_CTXS);
	struct starpu_sched_tree * t = malloc(sizeof(*t));
	memset(t, 0, sizeof(*t));
	t->sched_ctx_id = sched_ctx_id;
	t->workers = starpu_bitmap_create();
	STARPU_PTHREAD_MUTEX_INIT(&t->lock,NULL);
	return t;
}

void starpu_sched_tree_destroy(struct starpu_sched_tree * tree)
{
	STARPU_ASSERT(tree);
	if(tree->root)
		starpu_sched_node_destroy_rec(tree->root);
	starpu_bitmap_destroy(tree->workers);
	STARPU_PTHREAD_MUTEX_DESTROY(&tree->lock);
	free(tree);
}

void starpu_sched_node_add_child(struct starpu_sched_node* node, struct starpu_sched_node * child)
{
	STARPU_ASSERT(node && child);
	STARPU_ASSERT(!starpu_sched_node_is_worker(node));
	int i;
	for(i = 0; i < node->nchilds; i++){
		STARPU_ASSERT(node->childs[i] != node);
		STARPU_ASSERT(node->childs[i] != NULL);
	}

	node->childs = realloc(node->childs, sizeof(struct starpu_sched_node *) * (node->nchilds + 1));
	node->childs[node->nchilds] = child;
	node->nchilds++;
}
void starpu_sched_node_remove_child(struct starpu_sched_node * node, struct starpu_sched_node * child)
{
	STARPU_ASSERT(node && child);
	STARPU_ASSERT(!starpu_sched_node_is_worker(node));
	int pos;
	for(pos = 0; pos < node->nchilds; pos++)
		if(node->childs[pos] == child)
			break;
	STARPU_ASSERT(pos != node->nchilds);
	node->childs[pos] = node->childs[--node->nchilds];
}

struct starpu_bitmap * _starpu_get_worker_mask(unsigned sched_ctx_id)
{
	STARPU_ASSERT(sched_ctx_id < STARPU_NMAX_SCHED_CTXS);
	struct starpu_sched_tree * t = starpu_sched_ctx_get_policy_data(sched_ctx_id);
	STARPU_ASSERT(t);
	return t->workers;
}

static double estimated_load(struct starpu_sched_node * node)
{
	double sum = 0.0;
	int i;
	for( i = 0; i < node->nchilds; i++)
	{
		struct starpu_sched_node * c = node->childs[i];
		sum += c->estimated_load(c);
	}
	return sum;
}

static double _starpu_sched_node_estimated_end_min(struct starpu_sched_node * node)
{
	STARPU_ASSERT(node);
	double min = DBL_MAX;
	int i;
	for(i = 0; i < node->nchilds; i++)
	{
		double tmp = node->childs[i]->estimated_end(node->childs[i]);
		if(tmp < min)
			min = tmp;
	}
	return min;
}

/* this function find the best implementation or an implementation that need to be calibrated for a worker available
 * and set prediction in *length. nan if a implementation need to be calibrated, 0.0 if no perf model are available
 * return false if no worker on the node can execute that task
 */
int starpu_sched_node_execute_preds(struct starpu_sched_node * node, struct starpu_task * task, double * length)
{
	STARPU_ASSERT(node && task);
	int can_execute = 0;
	starpu_task_bundle_t bundle = task->bundle;
	double len = DBL_MAX;
	

	int workerid;
	for(workerid = starpu_bitmap_first(node->workers_in_ctx);
	    workerid != -1;
	    workerid = starpu_bitmap_next(node->workers_in_ctx, workerid))
	{
		struct starpu_perfmodel_arch* archtype = starpu_worker_get_perf_archtype(workerid);
		int nimpl;
		for(nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		{
			if(starpu_worker_can_execute_task(workerid,task,nimpl)
			   || starpu_combined_worker_can_execute_task(workerid, task, nimpl))
			{
				double d;
				can_execute = 1;
				if(bundle)
					d = starpu_task_bundle_expected_length(bundle, archtype, nimpl);
				else
					d = starpu_task_expected_length(task, archtype, nimpl);
				if(isnan(d))
				{
					/* TODO: this is not supposed to
					 * happen, the perfmodel selector
					 * should have forced it to a proper
					 * worker already */
					*length = d;
					return can_execute;
						
				}
				if(_STARPU_IS_ZERO(d) && !can_execute)
				{
					can_execute = 1;
					continue;
				}
				if(d < len)
				{
					len = d;
				}
			}
		}
		if(STARPU_SCHED_NODE_IS_HOMOGENEOUS(node))
			break;
	}

	if(len == DBL_MAX) /* we dont have perf model */
		/* TODO: this is not supposed to happen, the perfmodel selector
		 * should have forced it to a proper worker already */
		len = 0.0; 
	if(length)
		*length = len;
	return can_execute;
}

/* very similar function that dont compute prediction */
int starpu_sched_node_can_execute_task(struct starpu_sched_node * node, struct starpu_task * task)
{
	STARPU_ASSERT(task);
	STARPU_ASSERT(node);
	unsigned nimpl;
	int worker;
	for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		for(worker = starpu_bitmap_first(node->workers_in_ctx);
		    -1 != worker;
		    worker = starpu_bitmap_next(node->workers_in_ctx, worker))
			if (starpu_worker_can_execute_task(worker, task, nimpl)
			     || starpu_combined_worker_can_execute_task(worker, task, nimpl))
			    return 1;
	return 0;
}

/* compute the average of transfer length for tasks on all workers
 * maybe this should be optimised if all workers are under the same numa node
 */
double starpu_sched_node_transfer_length(struct starpu_sched_node * node, struct starpu_task * task)
{
	STARPU_ASSERT(node && task);
	int nworkers = starpu_bitmap_cardinal(node->workers_in_ctx);
	double sum = 0.0;
	int worker;
	if(STARPU_SCHED_NODE_IS_SINGLE_MEMORY_NODE(node))
	{
		unsigned memory_node  = starpu_worker_get_memory_node(starpu_bitmap_first(node->workers_in_ctx));
		if(task->bundle)
			return starpu_task_bundle_expected_data_transfer_time(task->bundle,memory_node);
		else
			return starpu_task_expected_data_transfer_time(memory_node, task);
	}

	for(worker = starpu_bitmap_first(node->workers_in_ctx);
	    worker != -1;
	    worker = starpu_bitmap_next(node->workers_in_ctx, worker))
	{
		unsigned memory_node  = starpu_worker_get_memory_node(worker);
		if(task->bundle)
		{
			sum += starpu_task_bundle_expected_data_transfer_time(task->bundle,memory_node);
		}
		else
		{
			sum += starpu_task_expected_data_transfer_time(memory_node, task);
			/* sum += starpu_task_expected_conversion_time(task, starpu_worker_get_perf_archtype(worker), impl ?)
			 * I dont know what to do as we dont know what implementation would be used here...
			 */
		}
	}
	return sum / nworkers;
}

/* This function can be called by nodes when they think that a prefetching request can be submitted.
 * For example, it is currently used by the MCT node to begin the prefetching on accelerators 
 * on which it pushed tasks as soon as possible.
 */
void starpu_sched_node_prefetch_on_node(struct starpu_sched_node * node, struct starpu_task * task)
{
	if (starpu_get_prefetch_flag() && (!task->prefetched)
		&& (node->properties >= STARPU_SCHED_NODE_SINGLE_MEMORY_NODE))
	{
		int worker = starpu_bitmap_first(node->workers_in_ctx);
		unsigned memory_node = starpu_worker_get_memory_node(worker);
		starpu_prefetch_task_input_on_node(task, memory_node);
		task->prefetched = 1;
	}
}

/* The default implementation of the room function is a recursive call to its fathers.
 * A personally-made room in a node (like in prio nodes) is necessary to catch
 * this recursive call somewhere, if the user wants to exploit it.
 */
int starpu_sched_node_room(struct starpu_sched_node * node)
{
	STARPU_ASSERT(node);
	int ret = 0;
	if(node->nfathers > 0)
	{
		int i;
		for(i=0; i < node->nfathers; i++)
		{
			struct starpu_sched_node * father = node->fathers[i];
			if(father != NULL)
				ret = father->room(father);
			if(ret)
				break;
		}
	}
	return ret;
}

/* An avail call will try to wake up one worker associated to the childs of the
 * node. It is currenly called by nodes which holds a queue (like fifo and prio
 * nodes) to signify its childs that a task has been pushed on its local queue.
 */
int starpu_sched_node_avail(struct starpu_sched_node * node)
{
	STARPU_ASSERT(node);
	int ret;
	if(node->nchilds > 0)
	{
		int i;
		for(i = 0; i < node->nchilds; i++)
		{
			struct starpu_sched_node * child = node->childs[i];
			ret = child->avail(child);
			if(ret)
				break;
		}
	}
	return 0;
}

void take_node_and_does_nothing(struct starpu_sched_node * node STARPU_ATTRIBUTE_UNUSED)
{
}

struct starpu_sched_node * starpu_sched_node_create(void)
{
	struct starpu_sched_node * node = malloc(sizeof(*node));
	memset(node,0,sizeof(*node));
	node->workers = starpu_bitmap_create();
	node->workers_in_ctx = starpu_bitmap_create();
	node->add_child = starpu_sched_node_add_child;
	node->remove_child = starpu_sched_node_remove_child;
	node->add_father = starpu_sched_node_add_father;
	node->remove_father = starpu_sched_node_remove_father;
	node->pop_task = pop_task_node;
	node->room = starpu_sched_node_room;
	node->avail = starpu_sched_node_avail;
	node->estimated_load = estimated_load;
	node->estimated_end = _starpu_sched_node_estimated_end_min;
	node->deinit_data = take_node_and_does_nothing;
	node->notify_change_workers = take_node_and_does_nothing;
	return node;
}

/* remove all child
 * for all child of node, if child->fathers[x] == node, set child->fathers[x] to null 
 * call node->deinit_data
 */
void starpu_sched_node_destroy(struct starpu_sched_node *node)
{
	STARPU_ASSERT(node);
	if(starpu_sched_node_is_worker(node))
		return;
	int i,j;
	for(i = 0; i < node->nchilds; i++)
	{
		struct starpu_sched_node * child = node->childs[i];
		for(j = 0; j < child->nfathers; j++)
			if(child->fathers[j] == node)
				child->remove_father(child,node);

	}
	while(node->nchilds != 0)
		node->remove_child(node, node->childs[0]);
	for(i = 0; i < node->nfathers; i++)
	{
		struct starpu_sched_node * father = node->fathers[i];
		for(j = 0; j < father->nchilds; j++)
			if(father->childs[j] == node)
				father->remove_child(father,node);

	}
	while(node->nfathers != 0)
		node->remove_father(node, node->fathers[0]);
	node->deinit_data(node);
	free(node->childs);
	free(node->fathers);
	starpu_bitmap_destroy(node->workers);
	starpu_bitmap_destroy(node->workers_in_ctx);
	free(node);
}

static void set_properties(struct starpu_sched_node * node)
{
	STARPU_ASSERT(node);
	node->properties = 0;

	int worker = starpu_bitmap_first(node->workers_in_ctx);
	if (worker == -1)
		return;
	uint32_t first_worker = _starpu_get_worker_struct(worker)->worker_mask;
	unsigned first_memory_node = _starpu_get_worker_struct(worker)->memory_node;
	int is_homogeneous = 1;
	int is_all_same_node = 1;
	for(;
	    worker != -1;
	    worker = starpu_bitmap_next(node->workers_in_ctx, worker))		
	{
		if(first_worker != _starpu_get_worker_struct(worker)->worker_mask)
			is_homogeneous = 0;
		if(first_memory_node != _starpu_get_worker_struct(worker)->memory_node)
			is_all_same_node = 0;
	}
	

	if(is_homogeneous)
		node->properties |= STARPU_SCHED_NODE_HOMOGENEOUS;
	if(is_all_same_node)
		node->properties |= STARPU_SCHED_NODE_SINGLE_MEMORY_NODE;
}


/* recursively set the node->workers member of node's subtree
 */
void _starpu_sched_node_update_workers(struct starpu_sched_node * node)
{
	STARPU_ASSERT(node);
	if(starpu_sched_node_is_worker(node))
		return;
	starpu_bitmap_unset_all(node->workers);
	int i;
	for(i = 0; i < node->nchilds; i++)
	{
		_starpu_sched_node_update_workers(node->childs[i]);
		starpu_bitmap_or(node->workers, node->childs[i]->workers);
		node->notify_change_workers(node);
	}
}

/* recursively set the node->workers_in_ctx in node's subtree
 */
void _starpu_sched_node_update_workers_in_ctx(struct starpu_sched_node * node, unsigned sched_ctx_id)
{
	STARPU_ASSERT(node);
	if(starpu_sched_node_is_worker(node))
		return;
	struct starpu_bitmap * workers_in_ctx = _starpu_get_worker_mask(sched_ctx_id);
	starpu_bitmap_unset_and(node->workers_in_ctx,node->workers, workers_in_ctx);
	int i,j;
	for(i = 0; i < node->nchilds; i++)
	{
		struct starpu_sched_node * child = node->childs[i];
		_starpu_sched_node_update_workers_in_ctx(child, sched_ctx_id);
		for(j = 0; j < STARPU_NMAX_SCHED_CTXS; j++)
			if(child->fathers[j] == node)
			{
				starpu_bitmap_or(node->workers_in_ctx, child->workers_in_ctx);
				break;
			}
	}
	set_properties(node);
	node->notify_change_workers(node);
}

void starpu_sched_tree_update_workers_in_ctx(struct starpu_sched_tree * t)
{
	STARPU_ASSERT(t);
	_starpu_sched_node_update_workers_in_ctx(t->root, t->sched_ctx_id);
}

void starpu_sched_tree_update_workers(struct starpu_sched_tree * t)
{
	STARPU_ASSERT(t);
	_starpu_sched_node_update_workers(t->root);
}
