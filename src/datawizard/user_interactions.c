/*
 * StarPU
 * Copyright (C) INRIA 2008-2009 (see AUTHORS file)
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

#include <common/config.h>
#include <common/utils.h>
#include <datawizard/coherency.h>
#include <datawizard/copy_driver.h>
#include <datawizard/write_back.h>
#include <core/dependencies/data_concurrency.h>

int starpu_data_request_allocation(starpu_data_handle handle, uint32_t node)
{
	starpu_data_request_t r;

	STARPU_ASSERT(handle);

	r = _starpu_create_data_request(handle, 0, node, node, 0, 0, 1);

	/* we do not increase the refcnt associated to the request since we are
	 * not waiting for its termination */

	_starpu_post_data_request(r, node);

	return 0;
}

struct state_and_node {
	starpu_data_handle state;
	starpu_access_mode mode;
	unsigned node;
	pthread_cond_t cond;
	pthread_mutex_t lock;
	unsigned finished;
	unsigned async;
	unsigned non_blocking;
	void (*callback)(void *);
	void *callback_arg;
};

/* put the current value of the data into RAM */
static inline void _starpu_sync_data_with_mem_continuation(void *arg)
{
	int ret;
	struct state_and_node *statenode = arg;

	starpu_data_handle handle = statenode->state;

	STARPU_ASSERT(handle);

	unsigned r = (statenode->mode & STARPU_R);
	unsigned w = (statenode->mode & STARPU_W);

	ret = _starpu_fetch_data_on_node(handle, 0, r, w, 0);
	STARPU_ASSERT(!ret);
	
	if (statenode->non_blocking)
	{
		/* continuation of starpu_data_sync_with_mem_non_blocking: we
		 * execute the callback if any  */
		if (statenode->callback)
			statenode->callback(statenode->callback_arg);

		free(statenode);
	}
	else {
		/* continuation of starpu_data_sync_with_mem */
		PTHREAD_MUTEX_LOCK(&statenode->lock);
		statenode->finished = 1;
		PTHREAD_COND_SIGNAL(&statenode->cond);
		PTHREAD_MUTEX_UNLOCK(&statenode->lock);
	}
}

/* The data must be released by calling starpu_data_release_from_mem later on */
int starpu_data_sync_with_mem(starpu_data_handle handle, starpu_access_mode mode)
{
	STARPU_ASSERT(handle);

	/* it is forbidden to call this function from a callback or a codelet */
	if (STARPU_UNLIKELY(!_starpu_worker_may_perform_blocking_calls()))
		return -EDEADLK;

	struct state_and_node statenode =
	{
		.state = handle,
		.mode = mode,
		.node = 0, // unused
		.non_blocking = 0,
		.cond = PTHREAD_COND_INITIALIZER,
		.lock = PTHREAD_MUTEX_INITIALIZER,
		.finished = 0
	};

	/* we try to get the data, if we do not succeed immediately, we set a
 	* callback function that will be executed automatically when the data is
 	* available again, otherwise we fetch the data directly */
	if (!_starpu_attempt_to_submit_data_request_from_apps(handle, mode,
			_starpu_sync_data_with_mem_continuation, &statenode))
	{
		/* no one has locked this data yet, so we proceed immediately */
		_starpu_sync_data_with_mem_continuation(&statenode);
	}
	else {
		PTHREAD_MUTEX_LOCK(&statenode.lock);
		while (!statenode.finished)
			PTHREAD_COND_WAIT(&statenode.cond, &statenode.lock);
		PTHREAD_MUTEX_UNLOCK(&statenode.lock);
	}

	return 0;
}

/* The data must be released by calling starpu_data_release_from_mem later on */
int starpu_data_sync_with_mem_non_blocking(starpu_data_handle handle,
		starpu_access_mode mode, void (*callback)(void *), void *arg)
{
	STARPU_ASSERT(handle);

	struct state_and_node *statenode = malloc(sizeof(struct state_and_node));
	STARPU_ASSERT(statenode);

	statenode->state = handle;
	statenode->mode = mode;
	statenode->non_blocking = 1;
	statenode->callback = callback;
	statenode->callback_arg = arg;
	PTHREAD_COND_INIT(&statenode->cond, NULL);
	PTHREAD_MUTEX_INIT(&statenode->lock, NULL);
	statenode->finished = 0;

	/* we try to get the data, if we do not succeed immediately, we set a
 	* callback function that will be executed automatically when the data is
 	* available again, otherwise we fetch the data directly */
	if (!_starpu_attempt_to_submit_data_request_from_apps(handle, mode,
			_starpu_sync_data_with_mem_continuation, statenode))
	{
		/* no one has locked this data yet, so we proceed immediately */
		_starpu_sync_data_with_mem_continuation(statenode);
	}

	return 0;
}

/* This function must be called after starpu_data_sync_with_mem so that the
 * application release the data */
void starpu_data_release_from_mem(starpu_data_handle handle)
{
	STARPU_ASSERT(handle);

	/* The application can now release the rw-lock */
	_starpu_release_data_on_node(handle, 0, 0);
}



static void _prefetch_data_on_node(void *arg)
{
	struct state_and_node *statenode = arg;

	_starpu_fetch_data_on_node(statenode->state, statenode->node, 1, 0, statenode->async);

	PTHREAD_MUTEX_LOCK(&statenode->lock);
	statenode->finished = 1;
	PTHREAD_COND_SIGNAL(&statenode->cond);
	PTHREAD_MUTEX_UNLOCK(&statenode->lock);

	if (!statenode->async)
	{
		_starpu_spin_lock(&statenode->state->header_lock);
		_starpu_notify_data_dependencies(statenode->state);
		_starpu_spin_unlock(&statenode->state->header_lock);
	}

}

int _starpu_prefetch_data_on_node_with_mode(starpu_data_handle handle, unsigned node, unsigned async, starpu_access_mode mode)
{
	STARPU_ASSERT(handle);

	/* it is forbidden to call this function from a callback or a codelet */
	if (STARPU_UNLIKELY(!_starpu_worker_may_perform_blocking_calls()))
		return -EDEADLK;

	struct state_and_node statenode =
	{
		.state = handle,
		.node = node,
		.async = async,
		.cond = PTHREAD_COND_INITIALIZER,
		.lock = PTHREAD_MUTEX_INITIALIZER,
		.finished = 0
	};

	if (!_starpu_attempt_to_submit_data_request_from_apps(handle, mode, _prefetch_data_on_node, &statenode))
	{
		/* we can immediately proceed */
		uint8_t read = (mode & STARPU_R);
		uint8_t write = (mode & STARPU_W);
		_starpu_fetch_data_on_node(handle, node, read, write, async);

		/* remove the "lock"/reference */
		if (!async)
		{
			_starpu_spin_lock(&handle->header_lock);
			_starpu_notify_data_dependencies(handle);
			_starpu_spin_unlock(&handle->header_lock);
		}
	}
	else {
		PTHREAD_MUTEX_LOCK(&statenode.lock);
		while (!statenode.finished)
			PTHREAD_COND_WAIT(&statenode.cond, &statenode.lock);
		PTHREAD_MUTEX_UNLOCK(&statenode.lock);
	}

	return 0;
}

int starpu_data_prefetch_on_node(starpu_data_handle handle, unsigned node, unsigned async)
{
	return _starpu_prefetch_data_on_node_with_mode(handle, node, async, STARPU_R);
}

/*
 *	It is possible to specify that a piece of data can be discarded without
 *	impacting the application.
 */
void starpu_data_advise_as_important(starpu_data_handle handle, unsigned is_important)
{
	_starpu_spin_lock(&handle->header_lock);

	/* first take all the children lock (in order !) */
	unsigned child;
	for (child = 0; child < handle->nchildren; child++)
	{
		/* make sure the intermediate children is advised as well */
		struct starpu_data_state_t *child_handle = &handle->children[child];
		if (child_handle->nchildren > 0)
			starpu_data_advise_as_important(child_handle, is_important);
	}

	handle->is_not_important = !is_important;

	/* now the parent may be used again so we release the lock */
	_starpu_spin_unlock(&handle->header_lock);

}

void starpu_data_set_sequential_consistency_flag(starpu_data_handle handle, unsigned flag)
{
	_starpu_spin_lock(&handle->header_lock);

	unsigned child;
	for (child = 0; child < handle->nchildren; child++)
	{
		/* make sure that the flags are applied to the children as well */
		struct starpu_data_state_t *child_handle = &handle->children[child];
		if (child_handle->nchildren > 0)
			starpu_data_set_sequential_consistency_flag(child_handle, flag);
	}

	PTHREAD_MUTEX_LOCK(&handle->sequential_consistency_mutex);
	handle->sequential_consistency = flag;
	PTHREAD_MUTEX_UNLOCK(&handle->sequential_consistency_mutex);

	_starpu_spin_unlock(&handle->header_lock);
}
