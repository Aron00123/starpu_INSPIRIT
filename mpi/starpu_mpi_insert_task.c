/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2011, 2012  Centre National de la Recherche Scientifique
 * Copyright (C) 2011  Université de Bordeaux 1
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

#include <stdarg.h>
#include <mpi.h>

#include <starpu.h>
#include <starpu_data.h>
#include <common/utils.h>
#include <starpu_hash.h>
#include <common/htable32.h>
#include <util/starpu_insert_task_utils.h>

//#define STARPU_MPI_VERBOSE	1
#include <starpu_mpi_private.h>

/* Whether we are allowed to keep copies of remote data. Does not work
 * yet: the sender has to know whether the receiver has it, keeping it
 * in an array indexed by node numbers. */
//#define MPI_CACHE

#ifdef MPI_CACHE
static struct starpu_htbl32_node **sent_data = NULL;
static struct starpu_htbl32_node **received_data = NULL;

static void _starpu_mpi_task_init(int nb_nodes)
{
        int i;

        _STARPU_MPI_DEBUG("Initialising hash table for cache\n");
        sent_data = malloc(nb_nodes * sizeof(struct starpu_htbl32_node *));
        for(i=0 ; i<nb_nodes ; i++) sent_data[i] = NULL;
        received_data = malloc(nb_nodes * sizeof(struct starpu_htbl32_node *));
        for(i=0 ; i<nb_nodes ; i++) received_data[i] = NULL;
}

typedef struct _starpu_mpi_clear_cache_s {
        starpu_data_handle_t data;
        int rank;
        int mode;
} _starpu_mpi_clear_cache_t;

#define _STARPU_MPI_CLEAR_SENT_DATA     0
#define _STARPU_MPI_CLEAR_RECEIVED_DATA 1

void _starpu_mpi_clear_cache_callback(void *callback_arg)
{
        _starpu_mpi_clear_cache_t *clear_cache = (_starpu_mpi_clear_cache_t *)callback_arg;
        uint32_t key = starpu_crc32_be((uintptr_t)clear_cache->data, 0);

        if (clear_cache->mode == _STARPU_MPI_CLEAR_SENT_DATA) {
                _STARPU_MPI_DEBUG("Clearing sent cache for data %p and rank %d\n", clear_cache->data, clear_cache->rank);
                _starpu_htbl_insert_32(&sent_data[clear_cache->rank], key, NULL);
        }
        else if (clear_cache->mode == _STARPU_MPI_CLEAR_RECEIVED_DATA) {
                _STARPU_MPI_DEBUG("Clearing received cache for data %p and rank %d\n", clear_cache->data, clear_cache->rank);
                _starpu_htbl_insert_32(&received_data[clear_cache->rank], key, NULL);
        }

        free(clear_cache);
}

double _starpu_mpi_clear_cache_cost_function(struct starpu_task *task, unsigned nimpl)
{
	return 0;
}

static struct starpu_perfmodel _starpu_mpi_clear_cache_model =
{
	.cost_function = _starpu_mpi_clear_cache_cost_function,
	.type = STARPU_COMMON,
};

static void _starpu_mpi_clear_cache_func(void *descr[] __attribute__ ((unused)), void *arg __attribute__ ((unused)))
{
}

static struct starpu_codelet _starpu_mpi_clear_cache_codelet =
{
	.where = STARPU_CPU|STARPU_CUDA|STARPU_OPENCL,
	.cpu_funcs = {_starpu_mpi_clear_cache_func, NULL},
	.cuda_funcs = {_starpu_mpi_clear_cache_func, NULL},
	.opencl_funcs = {_starpu_mpi_clear_cache_func, NULL},
	.nbuffers = 1,
	.modes = {STARPU_RW},
	.model = &_starpu_mpi_clear_cache_model
	// The model has a cost function which returns 0 so as to allow the codelet to be scheduled anywhere
};

void _starpu_mpi_clear_cache_request(starpu_data_handle_t data_handle, int rank, int mode)
{
        struct starpu_task *task = starpu_task_create();

	// We have a codelet with a empty function just to force the
	// task being created to have a dependency on data_handle
        task->cl = &_starpu_mpi_clear_cache_codelet;
        task->handles[0] = data_handle;

        _starpu_mpi_clear_cache_t *clear_cache = malloc(sizeof(_starpu_mpi_clear_cache_t));
        clear_cache->data = data_handle;
        clear_cache->rank = rank;
        clear_cache->mode = mode;

        task->callback_func = _starpu_mpi_clear_cache_callback;
        task->callback_arg = clear_cache;
        starpu_task_submit(task);
}
#endif

void _starpu_data_deallocate(starpu_data_handle_t data_handle)
{
#ifdef STARPU_DEVEL
#warning _starpu_data_deallocate not implemented yet
#endif
}

int starpu_mpi_insert_task(MPI_Comm comm, struct starpu_codelet *codelet, ...)
{
        int arg_type;
        va_list varg_list;
        int me, do_execute;
	size_t arg_buffer_size = 0;
	char *arg_buffer;
        int dest=0, execute, inconsistent_execute;

        _STARPU_MPI_LOG_IN();

	MPI_Comm_rank(comm, &me);

#ifdef MPI_CACHE
        if (sent_data == NULL) {
                int size;
                MPI_Comm_size(comm, &size);
                _starpu_mpi_task_init(size);
        }
#endif

        /* Get the number of buffers and the size of the arguments */
	va_start(varg_list, codelet);
        arg_buffer_size = _starpu_insert_task_get_arg_size(varg_list);

	va_start(varg_list, codelet);
	_starpu_pack_cl_args(arg_buffer_size, &arg_buffer, varg_list);

        /* Finds out if the property STARPU_EXECUTE_ON_NODE or STARPU_EXECUTE_ON_DATA is specified */
        execute = -1;
	va_start(varg_list, codelet);
	while ((arg_type = va_arg(varg_list, int)) != 0) {
		if (arg_type==STARPU_EXECUTE_ON_NODE) {
                        execute = va_arg(varg_list, int);
                }
		else if (arg_type==STARPU_EXECUTE_ON_DATA) {
			starpu_data_handle_t data = va_arg(varg_list, starpu_data_handle_t);
                        execute = starpu_data_get_rank(data);
			_STARPU_MPI_DEBUG("Executing on node %d\n", execute);
                }
		else if (arg_type==STARPU_R || arg_type==STARPU_W || arg_type==STARPU_RW || arg_type == STARPU_SCRATCH || arg_type == STARPU_REDUX) {
                        va_arg(varg_list, starpu_data_handle_t);
                }
		else if (arg_type==STARPU_VALUE) {
			va_arg(varg_list, void *);
			va_arg(varg_list, size_t);
		}
		else if (arg_type==STARPU_CALLBACK) {
			va_arg(varg_list, void (*)(void *));
		}
		else if (arg_type==STARPU_CALLBACK_WITH_ARG) {
			va_arg(varg_list, void (*)(void *));
			va_arg(varg_list, void *);
		}
		else if (arg_type==STARPU_CALLBACK_ARG) {
			va_arg(varg_list, void *);
		}
		else if (arg_type==STARPU_PRIORITY) {
			va_arg(varg_list, int);
		}
        }
	va_end(varg_list);

	/* Find out whether we are to execute the data because we own the data to be written to. */
        inconsistent_execute = 0;
        do_execute = -1;
	va_start(varg_list, codelet);
	while ((arg_type = va_arg(varg_list, int)) != 0) {
		if (arg_type==STARPU_R || arg_type==STARPU_W || arg_type==STARPU_REDUX || arg_type==STARPU_RW || arg_type == STARPU_SCRATCH) {
                        starpu_data_handle_t data = va_arg(varg_list, starpu_data_handle_t);
                        if (arg_type & STARPU_W || arg_type & STARPU_REDUX) {
                                if (!data) {
                                        /* We don't have anything allocated for this.
                                         * The application knows we won't do anything
                                         * about this task */
                                        /* Yes, the app could actually not call
                                         * insert_task at all itself, this is just a
                                         * safeguard. */
                                        _STARPU_MPI_DEBUG("oh oh\n");
                                        _STARPU_MPI_LOG_OUT();
                                        return -EINVAL;
                                }
                                int mpi_rank = starpu_data_get_rank(data);
                                if (mpi_rank == me) {
                                        if (do_execute == 0) {
                                                inconsistent_execute = 1;
                                        }
                                        else {
                                                do_execute = 1;
                                        }
                                }
                                else if (mpi_rank != -1) {
                                        if (do_execute == 1) {
                                                inconsistent_execute = 1;
                                        }
                                        else {
                                                do_execute = 0;
                                                dest = mpi_rank;
                                                /* That's the rank which needs the data to be sent to */
                                        }
                                }
                                else {
                                        _STARPU_ERROR("rank invalid\n");
                                }
                        }
                }
		else if (arg_type==STARPU_VALUE) {
			va_arg(varg_list, void *);
			va_arg(varg_list, size_t);
		}
		else if (arg_type==STARPU_CALLBACK) {
			va_arg(varg_list, void (*)(void *));
		}
		else if (arg_type==STARPU_CALLBACK_WITH_ARG) {
			va_arg(varg_list, void (*)(void *));
			va_arg(varg_list, void *);
		}
		else if (arg_type==STARPU_CALLBACK_ARG) {
			va_arg(varg_list, void *);
		}
		else if (arg_type==STARPU_PRIORITY) {
			va_arg(varg_list, int);
		}
		else if (arg_type==STARPU_EXECUTE_ON_NODE) {
			va_arg(varg_list, int);
		}
		else if (arg_type==STARPU_EXECUTE_ON_DATA) {
			va_arg(varg_list, int);
		}
	}
	va_end(varg_list);
	assert(do_execute != -1 && "StarPU needs to see a W or a REDUX data which will tell it where to execute the task");

        if (inconsistent_execute == 1) {
                if (execute == -1) {
                        _STARPU_MPI_DEBUG("Different tasks are owning W data. Needs to specify which one is to execute the codelet, using STARPU_EXECUTE_ON_NODE or STARPU_EXECUTE_ON_DATA\n");
			return -EINVAL;
                }
                else {
                        do_execute = (me == execute);
                        dest = execute;
                }
        }
        else if (execute != -1) {
                _STARPU_MPI_DEBUG("Property STARPU_EXECUTE_ON_NODE or STARPU_EXECUTE_ON_DATA ignored as W data are all owned by the same task\n");
        }

        /* Send and receive data as requested */
	va_start(varg_list, codelet);
	while ((arg_type = va_arg(varg_list, int)) != 0) {
		if (arg_type==STARPU_R || arg_type==STARPU_W || arg_type==STARPU_RW || arg_type == STARPU_SCRATCH) {
                        starpu_data_handle_t data = va_arg(varg_list, starpu_data_handle_t);
                        if (data && arg_type & STARPU_R) {
                                int mpi_rank = starpu_data_get_rank(data);
				int mpi_tag = starpu_data_get_tag(data);
				STARPU_ASSERT(mpi_tag >= 0 && "StarPU needs to be told the MPI rank of this data, using starpu_data_set_rank");
                                /* The task needs to read this data */
                                if (do_execute && mpi_rank != me && mpi_rank != -1) {
                                        /* I will have to execute but I don't have the data, receive */
#ifdef MPI_CACHE
                                        uint32_t key = starpu_crc32_be((uintptr_t)data, 0);
                                        void *already_received = _starpu_htbl_search_32(received_data[mpi_rank], key);
                                        if (!already_received) {
                                                _starpu_htbl_insert_32(&received_data[mpi_rank], key, data);
                                        }
                                        else {
                                                _STARPU_MPI_DEBUG("Do not receive data %p from node %d as it is already available\n", data, mpi_rank);
                                        }
                                        if (!already_received)
#endif
					{
						_STARPU_MPI_DEBUG("Receive data %p from %d\n", data, mpi_rank);
						starpu_mpi_irecv_detached(data, mpi_rank, mpi_tag, comm, NULL, NULL);
					}
                                }
                                if (!do_execute && mpi_rank == me) {
                                        /* Somebody else will execute it, and I have the data, send it. */
#ifdef MPI_CACHE
                                        uint32_t key = starpu_crc32_be((uintptr_t)data, 0);
                                        void *already_sent = _starpu_htbl_search_32(sent_data[dest], key);
                                        if (!already_sent) {
                                                _starpu_htbl_insert_32(&sent_data[dest], key, data);
                                        }
                                        else {
                                                _STARPU_MPI_DEBUG("Do not sent data %p to node %d as it has already been sent\n", data, dest);
                                        }
                                        if (!already_sent)
#endif
					{
						_STARPU_MPI_DEBUG("Send data %p to %d\n", data, dest);
						starpu_mpi_isend_detached(data, dest, mpi_tag, comm, NULL, NULL);
					}
                                }
                        }
                }
		else if (arg_type==STARPU_VALUE) {
			va_arg(varg_list, void *);
			va_arg(varg_list, size_t);
		}
		else if (arg_type==STARPU_CALLBACK) {
			va_arg(varg_list, void (*)(void *));
		}
		else if (arg_type==STARPU_CALLBACK_WITH_ARG) {
			va_arg(varg_list, void (*)(void *));
			va_arg(varg_list, void *);
		}
		else if (arg_type==STARPU_CALLBACK_ARG) {
			va_arg(varg_list, void *);
		}
		else if (arg_type==STARPU_PRIORITY) {
			va_arg(varg_list, int);
		}
		else if (arg_type==STARPU_EXECUTE_ON_NODE) {
			va_arg(varg_list, int);
		}
		else if (arg_type==STARPU_EXECUTE_ON_DATA) {
			va_arg(varg_list, starpu_data_handle_t);
		}
        }
	va_end(varg_list);

	if (do_execute) {
                _STARPU_MPI_DEBUG("Execution of the codelet %p (%s)\n", codelet, codelet->name);
                va_start(varg_list, codelet);
                struct starpu_task *task = starpu_task_create();
                int ret = _starpu_insert_task_create_and_submit(arg_buffer, codelet, &task, varg_list);
                _STARPU_MPI_DEBUG("ret: %d\n", ret);
                STARPU_ASSERT(ret==0);
        }

        if (inconsistent_execute) {
                va_start(varg_list, codelet);
                while ((arg_type = va_arg(varg_list, int)) != 0) {
                        if (arg_type==STARPU_R || arg_type==STARPU_W || arg_type==STARPU_RW || arg_type == STARPU_SCRATCH) {
                                starpu_data_handle_t data = va_arg(varg_list, starpu_data_handle_t);
                                if (arg_type & STARPU_W) {
                                        int mpi_rank = starpu_data_get_rank(data);
					int mpi_tag = starpu_data_get_tag(data);
					STARPU_ASSERT(mpi_tag >= 0 && "StarPU needs to be told the MPI rank of this data, using starpu_data_set_rank");
                                        if (mpi_rank == me) {
                                                if (execute != -1 && me != execute) {
                                                        _STARPU_MPI_DEBUG("Receive data %p back from the task %d which executed the codelet ...\n", data, dest);
                                                        starpu_mpi_irecv_detached(data, dest, mpi_tag, comm, NULL, NULL);
                                                }
                                        }
                                        else if (do_execute) {
                                                _STARPU_MPI_DEBUG("Send data %p back to its owner %d...\n", data, mpi_rank);
                                                starpu_mpi_isend_detached(data, mpi_rank, mpi_tag, comm, NULL, NULL);
                                        }
                                }
                        }
                        else if (arg_type==STARPU_VALUE) {
                                va_arg(varg_list, void *);
				va_arg(varg_list, size_t);
                        }
                        else if (arg_type==STARPU_CALLBACK) {
                                va_arg(varg_list, void (*)(void *));
                        }
                        else if (arg_type==STARPU_CALLBACK_WITH_ARG) {
                                va_arg(varg_list, void (*)(void *));
                                va_arg(varg_list, void *);
                        }
                        else if (arg_type==STARPU_CALLBACK_ARG) {
                                va_arg(varg_list, void *);
                        }
                        else if (arg_type==STARPU_PRIORITY) {
                                va_arg(varg_list, int);
                        }
                        else if (arg_type==STARPU_EXECUTE_ON_NODE) {
                                va_arg(varg_list, int);
                        }
                        else if (arg_type==STARPU_EXECUTE_ON_DATA) {
                                va_arg(varg_list, starpu_data_handle_t);
                        }
                }
                va_end(varg_list);
        }

	va_start(varg_list, codelet);
	while ((arg_type = va_arg(varg_list, int)) != 0) {
		if (arg_type==STARPU_R || arg_type==STARPU_W || arg_type==STARPU_RW || arg_type == STARPU_SCRATCH) {
                        starpu_data_handle_t data = va_arg(varg_list, starpu_data_handle_t);
#ifdef MPI_CACHE
                        if (arg_type & STARPU_W) {
                                uint32_t key = starpu_crc32_be((uintptr_t)data, 0);
                                if (do_execute) {
                                        /* Note that all copies I've sent to neighbours are now invalid */
                                        int n, size;
                                        MPI_Comm_size(comm, &size);
                                        for(n=0 ; n<size ; n++) {
                                                void *already_sent = _starpu_htbl_search_32(sent_data[n], key);
                                                if (already_sent) {
                                                        _STARPU_MPI_DEBUG("Posting request to clear send cache for data %p\n", data);
                                                        _starpu_mpi_clear_cache_request(data, n, _STARPU_MPI_CLEAR_SENT_DATA);
                                                }
                                        }
                                }
                                else {
                                        int mpi_rank = starpu_data_get_rank(data);
                                        void *already_received = _starpu_htbl_search_32(received_data[mpi_rank], key);
                                        if (already_received) {
                                                /* Somebody else will write to the data, so discard our cached copy if any */
                                                /* TODO: starpu_mpi could just remember itself. */
                                                _STARPU_MPI_DEBUG("Posting request to clear receive cache for data %p\n", data);
                                                _starpu_mpi_clear_cache_request(data, mpi_rank, _STARPU_MPI_CLEAR_RECEIVED_DATA);
                                                _starpu_data_deallocate(data);
                                        }
                                }
                        }
#else
                        /* We allocated a temporary buffer for the received data, now drop it */
                        if ((arg_type & STARPU_R) && do_execute) {
                                int mpi_rank = starpu_data_get_rank(data);
                                if (mpi_rank != me && mpi_rank != -1) {
                                        _starpu_data_deallocate(data);
                                }
                        }
#endif
                }
		else if (arg_type==STARPU_VALUE) {
			va_arg(varg_list, void *);
			va_arg(varg_list, size_t);
		}
		else if (arg_type==STARPU_CALLBACK) {
			va_arg(varg_list, void (*)(void *));
		}
		else if (arg_type==STARPU_CALLBACK_WITH_ARG) {
			va_arg(varg_list, void (*)(void *));
			va_arg(varg_list, void *);
		}
		else if (arg_type==STARPU_CALLBACK_ARG) {
			va_arg(varg_list, void *);
		}
		else if (arg_type==STARPU_PRIORITY) {
			va_arg(varg_list, int);
		}
		else if (arg_type==STARPU_EXECUTE_ON_NODE) {
			va_arg(varg_list, int);
		}
		else if (arg_type==STARPU_EXECUTE_ON_DATA) {
			va_arg(varg_list, starpu_data_handle_t);
		}
        }
	va_end(varg_list);
        _STARPU_MPI_LOG_OUT();
        return 0;
}

void starpu_mpi_get_data_on_node(MPI_Comm comm, starpu_data_handle_t data_handle, int node)
{
        int me, rank;

        rank = starpu_data_get_rank(data_handle);
	MPI_Comm_rank(comm, &me);

        if (node == rank) return;

        if (me == node)
        {
                starpu_mpi_irecv_detached(data_handle, rank, 42, comm, NULL, NULL);
        }
        else if (me == rank)
        {
                starpu_mpi_isend_detached(data_handle, node, 42, comm, NULL, NULL);
        }
        starpu_task_wait_for_all();
}
