#include <starpu_sched_node.h>
#include "prio_deque.h"
#include <starpu_scheduler.h>


struct _starpu_fifo_data
{
	struct _starpu_prio_deque fifo;
	starpu_pthread_mutex_t mutex;
};

void _fifo_node_deinit_data(struct starpu_sched_node * node)
{
	struct _starpu_fifo_data * f = node->data;
	_starpu_prio_deque_destroy(&f->fifo);
	STARPU_PTHREAD_MUTEX_LOCK(&f->mutex);
	free(f);
}

static double fifo_estimated_end(struct starpu_sched_node * node)
{
	struct _starpu_fifo_data * data = node->data;
	struct _starpu_prio_deque * fifo = &data->fifo;
	starpu_pthread_mutex_t * mutex = &data->mutex;
	int card = starpu_bitmap_cardinal(node->workers_in_ctx);

	STARPU_PTHREAD_MUTEX_LOCK(mutex);
	double estimated_end = fifo->exp_start + fifo->exp_len / card;
	STARPU_PTHREAD_MUTEX_UNLOCK(mutex);

	return estimated_end;
}

static double estimated_load(struct starpu_sched_node * node)
{
	struct _starpu_fifo_data * data = node->data;
	struct _starpu_prio_deque * fifo = &data->fifo;
	starpu_pthread_mutex_t * mutex = &data->mutex;
	double relative_speedup = 0.0;
	double load;

	if(node->is_homogeneous)
	{
		relative_speedup = starpu_worker_get_relative_speedup(starpu_worker_get_perf_archtype(starpu_bitmap_first(node->workers)));
		STARPU_PTHREAD_MUTEX_LOCK(mutex);
		load = fifo->ntasks / relative_speedup;
		STARPU_PTHREAD_MUTEX_UNLOCK(mutex);
		return load;
	}
	else
	{
		int i;
		for(i = starpu_bitmap_first(node->workers);
		    i != -1;
		    i = starpu_bitmap_next(node->workers, i))
			relative_speedup += starpu_worker_get_relative_speedup(starpu_worker_get_perf_archtype(i));
		relative_speedup /= starpu_bitmap_cardinal(node->workers);
			STARPU_ASSERT(!_STARPU_IS_ZERO(relative_speedup));
			STARPU_PTHREAD_MUTEX_LOCK(mutex);
			load = fifo->ntasks / relative_speedup;
			STARPU_PTHREAD_MUTEX_UNLOCK(mutex);
	}
	int i;
	for(i = 0; i < node->nchilds; i++)
	{
		struct starpu_sched_node * c = node->childs[i];
		load += c->estimated_load(c);
	}
	return load;
}

static int push_task(struct starpu_sched_node * node, struct starpu_task * task)
{
	STARPU_ASSERT(starpu_sched_node_can_execute_task(node,task));
	struct _starpu_fifo_data * data = node->data;
	struct _starpu_prio_deque * fifo = &data->fifo;
	starpu_pthread_mutex_t * mutex = &data->mutex;
	STARPU_PTHREAD_MUTEX_LOCK(mutex);
	STARPU_ASSERT(!isnan(fifo->exp_end));
	STARPU_ASSERT(!isnan(fifo->exp_len));
	STARPU_ASSERT(!isnan(fifo->exp_start));
	int ret = _starpu_prio_deque_push_task(fifo, task);
	if(!isnan(task->predicted))
	{
		fifo->exp_len += task->predicted;
		fifo->exp_end = fifo->exp_start + fifo->exp_len;
	}
	STARPU_ASSERT(!isnan(fifo->exp_end));
	STARPU_ASSERT(!isnan(fifo->exp_len));
	STARPU_ASSERT(!isnan(fifo->exp_start));
	STARPU_PTHREAD_MUTEX_UNLOCK(mutex);


	starpu_sched_node_available(node);
//	node->available(node);
	return ret;
}

static struct starpu_task * pop_task(struct starpu_sched_node * node, unsigned sched_ctx_id)
{
	struct _starpu_fifo_data * data = node->data;
	struct _starpu_prio_deque * fifo = &data->fifo;
	starpu_pthread_mutex_t * mutex = &data->mutex;
	STARPU_PTHREAD_MUTEX_LOCK(mutex);
	STARPU_ASSERT(!isnan(fifo->exp_end));
	STARPU_ASSERT(!isnan(fifo->exp_len));
	STARPU_ASSERT(!isnan(fifo->exp_start));
	struct starpu_task * task  = node->is_homogeneous ?
		_starpu_prio_deque_pop_task(fifo):
		_starpu_prio_deque_pop_task_for_worker(fifo, starpu_worker_get_id());
	if(task)
	{

		if(!isnan(task->predicted))
		{
			fifo->exp_start = starpu_timing_now() + task->predicted;
			fifo->exp_len -= task->predicted;
		}
		fifo->exp_end = fifo->exp_start + fifo->exp_len;
		if(fifo->ntasks == 0)
			fifo->exp_len = 0.0;
	}
	STARPU_ASSERT(!isnan(fifo->exp_end));
	STARPU_ASSERT(!isnan(fifo->exp_len));
	STARPU_ASSERT(!isnan(fifo->exp_start));
	STARPU_PTHREAD_MUTEX_UNLOCK(mutex);
	if(task)
		return task;
	struct starpu_sched_node * father = node->fathers[sched_ctx_id];
	if(father)
		return father->pop_task(father,sched_ctx_id);
	return NULL;
}

int starpu_sched_node_is_fifo(struct starpu_sched_node * node)
{
	return node->push_task == push_task;
}

struct starpu_sched_node * starpu_sched_node_fifo_create(void * arg STARPU_ATTRIBUTE_UNUSED)
{
	struct starpu_sched_node * node = starpu_sched_node_create();
	struct _starpu_fifo_data * data = malloc(sizeof(*data));
	_starpu_prio_deque_init(&data->fifo);
	STARPU_PTHREAD_MUTEX_INIT(&data->mutex,NULL);
	node->data = data;
	node->estimated_end = fifo_estimated_end;
	node->estimated_load = estimated_load;
	node->push_task = push_task;
	node->pop_task = pop_task;
	node->deinit_data = _fifo_node_deinit_data;
	return node;
}
