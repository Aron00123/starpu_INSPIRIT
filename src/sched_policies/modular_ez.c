/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013-2015,2017                           Inria
 * Copyright (C) 2014-2015,2017                           CNRS
 * Copyright (C) 2013-2015,2017-2018                           Université de Bordeaux
 * Copyright (C) 2013                                     Simon Archipoff
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

#include <starpu_sched_component.h>
#include <starpu_scheduler.h>

/* The scheduling strategy may look like this :
 *
 *                                    |
 *                                fifo_above
 *                                    |
 *  decision_component <--push-- perfmodel_select_component --push--> eager_component
 *  |     |     |                                                  |
 * fifo  fifo  fifo                                                |
 *  |     |     |                                                  |
 * eager eager eager                                               |
 *  |     |     |                                                  |
 *  >--------------------------------------------------------------<
 *                    |                                |
 *              best_impl_component              best_impl_component
 *                    |                               |
 *               worker_component                   worker_component
 */

/* The two thresolds concerns the fifo components below, which contains queues
 * who can handle the priority of StarPU tasks. You can tune your
 * scheduling by benching those values and choose which one is the
 * best for your current application.
 * The current value of the ntasks_threshold is the best we found
 * so far across several types of applications (cholesky, LU, stencil).
 */
#define _STARPU_SCHED_NTASKS_THRESHOLD_DEFAULT 30
#define _STARPU_SCHED_EXP_LEN_THRESHOLD_DEFAULT 1000000000.0

void starpu_sched_component_initialize_simple_scheduler(starpu_sched_component_create_t create_decision_component, void *data, unsigned flags, unsigned sched_ctx_id)
{
	struct starpu_sched_tree * t;
	struct starpu_sched_component *last = NULL;	/* Stores the last created component, from top to bottom */
	unsigned i, j, n;
	struct starpu_sched_component *decision_component = NULL;
	struct starpu_sched_component *no_perfmodel_component = NULL;
	struct starpu_sched_component *calibrator_component = NULL;

	/* Components parameters */

	if (flags & STARPU_SCHED_SIMPLE_FIFO_ABOVE_PRIO || flags & STARPU_SCHED_SIMPLE_FIFOS_BELOW_PRIO)
	{
		/* The application may use any integer */
		if (starpu_sched_ctx_min_priority_is_set(sched_ctx_id) == 0)
			starpu_sched_ctx_set_min_priority(sched_ctx_id, INT_MIN);
		if (starpu_sched_ctx_max_priority_is_set(sched_ctx_id) == 0)
			starpu_sched_ctx_set_max_priority(sched_ctx_id, INT_MAX);
	}

	struct starpu_sched_component_prio_data prio_data =
		{
			.ntasks_threshold = starpu_get_env_number_default("STARPU_NTASKS_THRESHOLD", _STARPU_SCHED_NTASKS_THRESHOLD_DEFAULT),
			.exp_len_threshold = starpu_get_env_float_default("STARPU_EXP_LEN_THRESHOLD", _STARPU_SCHED_EXP_LEN_THRESHOLD_DEFAULT),
		};


	/* See what the component will decide */
	unsigned nbelow;
	unsigned nummaxids;
	nummaxids = starpu_worker_get_count() + starpu_combined_worker_get_count();
	if (starpu_memory_nodes_get_count() > nummaxids)
		nummaxids = starpu_memory_nodes_get_count();
	if (STARPU_ANY_WORKER > nummaxids)
		nummaxids = STARPU_ANY_WORKER;
	unsigned below_id[nummaxids];

	switch (flags & STARPU_SCHED_SIMPLE_DECIDE_MASK)
	{
		case STARPU_SCHED_SIMPLE_DECIDE_WORKERS:
			/* Count workers */
			nbelow = starpu_worker_get_count() + starpu_combined_worker_get_count();
			/* and no need for IDs */
			break;
		case STARPU_SCHED_SIMPLE_DECIDE_MEMNODES:
		{
			/* Count memory nodes */
			n = starpu_memory_nodes_get_count();
			nbelow = 0;
			for(i = 0; i < n; i++)
			{
				for(j = 0; j < starpu_worker_get_count() + starpu_combined_worker_get_count(); j++)
					if (starpu_worker_get_memory_node(j) == i)
						break;
				if (j >= starpu_worker_get_count() + starpu_combined_worker_get_count())
					/* Don't create a component string for this memory node with no worker */
					continue;
				below_id[nbelow] = i;
				nbelow++;
			}
			break;
		}
		case STARPU_SCHED_SIMPLE_DECIDE_ARCHS:
		{
			/* Count available architecture types */
			enum starpu_worker_archtype type;
			nbelow = 0;
			for (type = STARPU_CPU_WORKER; type < STARPU_ANY_WORKER; type++)
			{
				if (starpu_worker_get_count_by_type(type))
				{
					below_id[nbelow] = type;
					nbelow++;
				}
			}
			break;
		}
		default:
			STARPU_ABORT();
	}
	STARPU_ASSERT(nbelow > 0);

	/* Start building the tree */
	t = starpu_sched_tree_create(sched_ctx_id);
	t->root = NULL;

	if (nbelow == 1)
	{
		/* Oh, no choice, we don't actually need to decide, just
		 * use an eager scheduler */
		decision_component = starpu_sched_component_eager_create(t, NULL);
	}
	else
	{
		decision_component = create_decision_component(t, data);
	}

	/* First, a fifo if requested */
	if (flags & STARPU_SCHED_SIMPLE_FIFO_ABOVE)
	{
		struct starpu_sched_component *fifo_above;
		if (flags & STARPU_SCHED_SIMPLE_FIFO_ABOVE_PRIO)
		{
			fifo_above = starpu_sched_component_prio_create(t, NULL);
		}
		else
		{
			fifo_above = starpu_sched_component_fifo_create(t, NULL);
		}
		last = t->root = fifo_above;
	}

	/* Then, perfmodel calibration if requested, and plug the scheduling decision-making component to it */
	if (flags & STARPU_SCHED_SIMPLE_PERFMODEL)
	{
		no_perfmodel_component = starpu_sched_component_eager_create(t, NULL);
		calibrator_component = starpu_sched_component_eager_calibration_create(t, NULL);

		struct starpu_sched_component_perfmodel_select_data perfmodel_select_data =
			{
				.calibrator_component = calibrator_component,
				.no_perfmodel_component = no_perfmodel_component,
				.perfmodel_component = decision_component,
			};

		struct starpu_sched_component * perfmodel_select_component = starpu_sched_component_perfmodel_select_create(t, &perfmodel_select_data);

		if (!t->root)
			t->root = perfmodel_select_component;
		else
			starpu_sched_component_connect(last, perfmodel_select_component);

		starpu_sched_component_connect(perfmodel_select_component, decision_component);
		starpu_sched_component_connect(perfmodel_select_component, calibrator_component);
		starpu_sched_component_connect(perfmodel_select_component, no_perfmodel_component);
	}
	else
	{
		/* No perfmodel calibration */
		if (!t->root)
			/* Plug decision_component directly */
			t->root = decision_component;
		else
			/* Plug decision_component to fifo */
			starpu_sched_component_connect(last, decision_component);
	}

	/* Create one fifo+eager component pair per choice, below scheduling decision */
	struct starpu_sched_component *last_below[nbelow];
	memset(&last_below, 0, sizeof(last_below));
	for(i = 0; i < nbelow; i++)
	{
		last = decision_component;
		if (flags & STARPU_SCHED_SIMPLE_FIFOS_BELOW)
		{
			struct starpu_sched_component *fifo_below;
			if (flags & STARPU_SCHED_SIMPLE_FIFOS_BELOW_PRIO)
			{
				fifo_below = starpu_sched_component_prio_create(t, &prio_data);
			}
			else
			{
				fifo_below = starpu_sched_component_fifo_create(t, NULL);
			}
			starpu_sched_component_connect(last, fifo_below);
			last = fifo_below;
		}
		switch (flags & STARPU_SCHED_SIMPLE_DECIDE_MASK)
		{
			case STARPU_SCHED_SIMPLE_DECIDE_WORKERS:
				/* 1-1 mapping between choice and worker, no need for an eager component */
				n = 1;
				break;
			case STARPU_SCHED_SIMPLE_DECIDE_MEMNODES:
				n = 0;
				for (j = 0; j < starpu_worker_get_count() + starpu_combined_worker_get_count(); j++)
					if (starpu_worker_get_memory_node(j) == below_id[i])
						n++;
				break;
			case STARPU_SCHED_SIMPLE_DECIDE_ARCHS:
				n = starpu_worker_get_count_by_type(i);
				break;
			default:
				STARPU_ABORT();
		}
		STARPU_ASSERT(n >= 1);
		if (n > 1) {
			/* Several workers for this choice, need to introduce
			 * a component to distribute the work */
			struct starpu_sched_component *distribute;
			if (flags & STARPU_SCHED_SIMPLE_WS_BELOW)
			{
				distribute = starpu_sched_component_work_stealing_create(t, NULL);
			}
			else
			{
				distribute = starpu_sched_component_eager_create(t, NULL);
			}

			starpu_sched_component_connect(last, distribute);
			last = distribute;
		}
		last_below[i] = last;
	}

	/* Finish by creating components per worker */
	for(i = 0; i < starpu_worker_get_count() + starpu_combined_worker_get_count(); i++)
	{
		/* Start from the bottom */
		struct starpu_sched_component * worker_component = starpu_sched_component_worker_new(sched_ctx_id, i);
		struct starpu_sched_component * worker = worker_component;
		unsigned id;

		/* Create implementation chooser if requested */
		if (flags & STARPU_SCHED_SIMPLE_IMPL)
		{
			struct starpu_sched_component * impl_component = starpu_sched_component_best_implementation_create(t, NULL);
			starpu_sched_component_connect(impl_component, worker_component);
			/* Reroute components above through it */
			worker = impl_component;
		}

		switch (flags & STARPU_SCHED_SIMPLE_DECIDE_MASK)
		{
			case STARPU_SCHED_SIMPLE_DECIDE_WORKERS:
				id = i;
				break;
			case STARPU_SCHED_SIMPLE_DECIDE_MEMNODES:
				id = below_id[starpu_worker_get_memory_node(i)];
				break;
			case STARPU_SCHED_SIMPLE_DECIDE_ARCHS:
				id = below_id[starpu_worker_get_type(i)];
				break;
			default:
				STARPU_ABORT();
		}
		STARPU_ASSERT(id < nbelow);
		last = last_below[id];
		if (!last)
			last = decision_component;

		starpu_sched_component_connect(last, worker);

		/* Plug perfmodel calibrator if requested */
		if (flags & STARPU_SCHED_SIMPLE_PERFMODEL)
		{
			starpu_sched_component_connect(no_perfmodel_component, worker);
			/* Calibrator needs to choose the implementation */
			starpu_sched_component_connect(calibrator_component, worker_component);
		}
	}

	starpu_sched_tree_update_workers(t);
	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)t);
}
