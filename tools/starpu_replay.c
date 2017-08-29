/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2016-2017  Université de Bordeaux
 * Copyright (C) 2017  Erwan Leria
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

/*
 * This reads a tasks.rec file and replays the recorded task graph.
 * Currently, this version is done to run with simgrid.
 *
 * For further information, contact erwan.leria@inria.fr
 */

#include <starpu.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>

#include <common/uthash.h>
#include <common/utils.h>
#include <starpu_scheduler.h>
#include <common/rbtree.h>


#define REPLAY_NMAX_DEPENDENCIES 8

#define ARRAY_DUP(in, out, n) memcpy(out, in, n * sizeof(*out))
#define ARRAY_INIT(array, n) memset(array, 0, n * sizeof(*array))

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Declarations of global variables, structures, pointers, ... *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* TODO: move to core header while moving starpu_replay_sched to core */
extern void schedRecInit(const char * filename);
extern void applySchedRec(struct starpu_task * starpu_task, unsigned long submit_order);

/* Enum for normal and "wontuse" tasks */
enum task_type {NormalTask, WontUseTask};

typedef unsigned long jobid_t;

enum task_type control;
static char *name = NULL;
static char *model = NULL;
static jobid_t jobid;
static jobid_t *dependson;
static long submitorder = -1;
static starpu_tag_t tag;
static int workerid;
static uint32_t footprint;
static double flops;

static double startTime; //start time (The instant when the task starts)
static double endTime; //end time (The instant when the task ends)

static int iteration = -1;

static starpu_data_handle_t handles[STARPU_NMAXBUFS];
static enum starpu_data_access_mode modes[STARPU_NMAXBUFS];
static char normal_reg_signal[STARPU_NMAXBUFS];

/* Use the following arrays when the number of data is greater than STARPU_NMAXBUFS */

starpu_data_handle_t * handles_ptr;
enum starpu_data_access_mode * modes_ptr;
size_t * sizes_set;

static size_t dependson_size;
static size_t ndependson;

static int nb_parameters = 0; /* Number of parameters */
static int alloc_mode; /* If alloc_mode value is 1, then the handles are stored in dyn_handles, else they are in handles */

static int priority = 0;

char * reg_signal = NULL; /* The register signal (0 or 1 coded on 8 bit) is used to know which handle of the task has to be registered in StarPU (in fact to avoid  handle twice)*/

static int device;

/* Record all tasks, hashed by jobid. */
static struct task
{
	struct starpu_rbtree_node node;
	UT_hash_handle hh;
	jobid_t jobid;
        int iteration;
	unsigned long submit_order;
	jobid_t *deps;
	size_t ndependson;
	struct starpu_task task;
	enum task_type type;

} *tasks;

/* Record handles */
static struct handle
{
	UT_hash_handle hh;
	starpu_data_handle_t mem_ptr; /* This value should be the registered handle */
	starpu_data_handle_t handle; /* The key is the original value of the handle in the file */
} * handles_hash;

/* Record models */

static struct perfmodel
{
	UT_hash_handle hh;
	struct starpu_perfmodel perfmodel;
	char * model_name;
} * model_hash;

/* [SUBMITORDER] The tree of the submit order */

static struct starpu_rbtree tree = STARPU_RBTREE_INITIALIZER;

/* the cmp_fn arg for rb_tree_insert() */
unsigned int diff(struct starpu_rbtree_node * left_elm, struct starpu_rbtree_node * right_elm)
{
	return ((struct task *) left_elm)->submit_order - ((struct task *) right_elm)->submit_order;
}

/* Settings for the perfmodel */
struct task_arg
{
	uint32_t footprint;
	double perf[];
};

uint32_t get_footprint(struct starpu_task * task)
{
	return ((struct task_arg*) (task->cl_arg))->footprint;
}

double arch_cost_function(struct starpu_task *task, struct starpu_perfmodel_arch *arch, unsigned nimpl STARPU_ATTRIBUTE_UNUSED)
{
	device = starpu_perfmodel_arch_comb_get(arch->ndevices, arch->devices);
	STARPU_ASSERT(device != -1);

	/* Then, get the pointer to the value of the expected time */
	double * val = (double *) ((struct task_arg *) (task->cl_arg))->perf+device;

	if (!(*val == 0 || isnan(*val)))
		return *val;

	fprintf(stderr, "[starpu] Error, expected_time is 0 or lower (replay.c line : %d)", __LINE__- 6);

	return 0.0;
}

/* End of settings */

void dumb_kernel(void) {}

/* [CODELET] Initialization of an unique codelet for all the tasks*/
static int can_execute(unsigned worker_id, struct starpu_task *task, unsigned nimpl STARPU_ATTRIBUTE_UNUSED)
{
	struct starpu_perfmodel_arch * arch = starpu_worker_get_perf_archtype(worker_id, STARPU_NMAX_SCHED_CTXS);
	double expected_time = ((struct task_arg *) (task->cl_arg))->perf[(starpu_perfmodel_arch_comb_get(arch->ndevices, arch->devices))];
	if (!(expected_time == 0 || isnan(expected_time)))
	{
		return 1;
	}

	return 0;
}

static struct starpu_perfmodel myperfmodel =
{
	.type = STARPU_PER_ARCH,
	.arch_cost_function = arch_cost_function,
	.footprint = get_footprint,
};

static struct starpu_codelet cl =
{
	.cpu_funcs = { (void*) 1 },
	.cpu_funcs_name = { "dumb_kernel" },
	.cuda_funcs = { (void*) 1 },
	.opencl_funcs = { (void*) 1 },
	.nbuffers = STARPU_VARIABLE_NBUFFERS,
	.can_execute = can_execute,
	.model = &myperfmodel,
};


/* * * * * * * * * * * * * *
* * * * * Functions * * * * *
* * * * * * * * * * * * * * */


/* The following function checks if the program has to use static or dynamic arrays*/
static int set_alloc_mode(int total_parameters)
{
	return (total_parameters <= STARPU_NMAXBUFS);
}

/* According to the allocation mode, modify handles_ptr and modes_ptr in static or dynamic */
static void arrays_managing(int mode)
{
	if (mode)
	{
		handles_ptr = &handles[0];
		modes_ptr = &modes[0];
		reg_signal = &normal_reg_signal[0];
	}
	else
	{
		_STARPU_MALLOC(handles_ptr, sizeof(*handles_ptr) * nb_parameters);
		_STARPU_MALLOC(modes_ptr, sizeof(*modes_ptr) * nb_parameters);
		_STARPU_CALLOC(reg_signal, nb_parameters, sizeof(char *));

	}
}

/* Check if a handle hasn't been registered yet */
static void variable_data_register_check(size_t * array_of_size, int nb_handles)
{
	int h;

	for (h = 0 ; h < nb_handles ; h++)
	{
		if(reg_signal[h]) /* Get the register signal, if it's 1 do ... */
		{
			struct handle * handles_cell;

			_STARPU_MALLOC(handles_cell, sizeof(*handles_cell));
			STARPU_ASSERT(handles_cell != NULL);

			handles_cell->handle = handles_ptr[h]; /* Get the hidden key (initial handle from the file) to store it as a key*/
			
			starpu_variable_data_register(handles_ptr+h, STARPU_MAIN_RAM, (uintptr_t) 1, array_of_size[h]);
			
			handles_cell->mem_ptr = handles_ptr[h]; /* Store the new value of the handle into the hash table */

			HASH_ADD(hh, handles_hash, handle, sizeof(handles_ptr[h]), handles_cell);
		}
	}
}

void reset(void)
{
	control = NormalTask;

	if (name != NULL)
	{
		free(name);
		name = NULL;
	}

	if (model != NULL)
	{
		free(model);
		model = NULL;
	}

	if (sizes_set != NULL)
	{
		free(sizes_set);
		sizes_set = NULL;
	}

	if (reg_signal != NULL)
	{
		if (!alloc_mode)
		{
			free(reg_signal);
			reg_signal = NULL;
		}
		else
		{
			ARRAY_INIT(reg_signal, nb_parameters);
		}
	}

	jobid = 0;
	ndependson = 0;
	tag = -1;
	workerid = -1;
	footprint = 0;
	startTime = 0.0;
	endTime = 0.0;

	if (submitorder != -1)
		submitorder = -1;

	iteration = -1;
	nb_parameters = 0;
	alloc_mode = 1;
}

void fix_wontuse_handle(struct task * wontuseTask)
{
	struct handle * handle_tmp;
	HASH_FIND(hh, handles_hash, &wontuseTask->task.handles[0], sizeof(wontuseTask->task.handles[0]), handle_tmp);
	STARPU_ASSERT(wontuseTask);
	wontuseTask->task.handles[0] = handle_tmp->mem_ptr;
}

/* Function that submits all the tasks (used when the program reaches EOF) */
int submit_tasks(void)
{
	/* Add dependencies */
	
	const struct starpu_rbtree * tmptree = &tree;
	struct starpu_rbtree_node * currentNode = starpu_rbtree_first(tmptree);
	unsigned long last_submitorder = 0;

	while (currentNode != NULL)
	{
		struct task * currentTask = (struct task *) currentNode;

		if (currentTask->type == NormalTask)
		{
			STARPU_ASSERT(currentTask->submit_order >= last_submitorder + 1);

			while (currentTask->submit_order > last_submitorder + 1)
			{
				/* Oops, some tasks were not submitted by original application, fake some */
				struct starpu_task *task = starpu_task_create();
				int ret;
				task->cl = NULL;
				ret = starpu_task_submit(task);
				STARPU_ASSERT(ret == 0);
				last_submitorder++;
			}

			if (currentTask->ndependson > 0)
			{
				struct starpu_task * taskdeps[currentTask->ndependson];
				unsigned i;

				for (i = 0; i < currentTask->ndependson; i++)
				{
					struct task * taskdep;

					/*  Get the ith jobid of deps_jobid */
					HASH_FIND(hh, tasks, &currentTask->deps[i], sizeof(jobid), taskdep);

					STARPU_ASSERT(taskdep);

					taskdeps[i] = &taskdep->task;
				}

				starpu_task_declare_deps_array(&currentTask->task, currentTask->ndependson, taskdeps);
			}

			if (!(currentTask->iteration == -1))
				starpu_iteration_push(currentTask->iteration);

			applySchedRec(&currentTask->task, currentTask->submit_order);
			int ret_val = starpu_task_submit(&currentTask->task);

			if (!(currentTask->iteration == -1))
				starpu_iteration_pop();

			if (ret_val != 0)
				return -1;


			//printf("submitting task %s (%lu, %llu)\n", currentTask->task.name?currentTask->task.name:"anonymous", currentTask->jobid, (unsigned long long) currentTask->task.tag_id);
			printf("\rsubmitting task %lu", currentTask->submit_order);
			last_submitorder++;
		}

		else
		{
			fix_wontuse_handle(currentTask); /* Add the handle in the wontuse task */
                        /* FIXME: can not actually work properly since we have
                         * disabled sequential consistency, so we don't have any
                         * easy way to make this wait for the last task that
                         * wrote to the handle. */
			starpu_data_wont_use(currentTask->task.handles[0]);
		}

		currentNode = starpu_rbtree_next(currentNode);

	}
	printf("\n");

	return 1;
}


/* * * * * * * * * * * * * * * */
/* * * * * * MAIN * * * * * * */
/* * * * * * * * * * * * * * */

int main(int argc, char **argv)
{
	starpu_data_set_default_sequential_consistency_flag(0);

	FILE *rec;
	char *s;
	size_t s_allocated = 128;

	_STARPU_MALLOC(s, s_allocated);
	dependson_size = REPLAY_NMAX_DEPENDENCIES; /* Change the value of REPLAY_NMAX_DEPENCIES to modify the number of dependencies */
	_STARPU_MALLOC(dependson, dependson_size * sizeof (* dependson));
	alloc_mode = 1;

	if (argc <= 1)
	{
		fprintf(stderr,"Usage: %s tasks.rec [sched.rec]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (argc >= 3)
		schedRecInit(argv[2]);

	rec = fopen(argv[1], "r");
	if (!rec)
	{
		fprintf(stderr,"unable to open file %s: %s\n", argv[1], strerror(errno));
		exit(EXIT_FAILURE);
	}

	int ret = starpu_init(NULL);
	if (ret == -ENODEV) goto enodev;

	/* Read line by line, and on empty line submit the task with the accumulated information */
	reset();

	while(1)
	{
		char *ln;

		if (!fgets(s, s_allocated, rec))
		{
			int submitted = submit_tasks();

			if (submitted == -1)
			{
				goto enodev;
			}

			goto eof;
		}

		while (!(ln = strchr(s, '\n')))
		{
			/* fprintf(stderr,"buffer size %d too small, doubling it\n", s_allocated); */
			_STARPU_REALLOC(s, s_allocated * 2);

			if (!fgets(s + s_allocated-1, s_allocated+1, rec))
			{
				int submitted = submit_tasks();

				if (submitted == -1)
				{
					goto enodev;
				}

				goto eof;
			}

			s_allocated *= 2;
		}

		if (ln == s)
		{
			/* Empty line, do task */

			struct task * task;
			_STARPU_MALLOC(task, sizeof(*task));

			starpu_task_init(&task->task);
			task->deps = NULL;

			if (submitorder != -1)
			{
				task->submit_order = submitorder;

				starpu_rbtree_node_init(&task->node);
				starpu_rbtree_insert(&tree, &task->node, diff);
			}


			task->jobid = jobid;
			task->iteration = iteration;

			if (name != NULL)
				task->task.name = strdup(name);

			task->type = control;

			if (control == NormalTask)
			{
				if (workerid >= 0)
				{
					task->task.priority = priority;
					task->task.cl = &cl;
					task->task.workerid = workerid;

					if (alloc_mode)
					{
						/* Duplicating the handles stored (and registered in the current context) into the task */

						ARRAY_DUP(modes_ptr, task->task.modes, nb_parameters);
						ARRAY_DUP(modes_ptr, task->task.cl->modes, nb_parameters);
						variable_data_register_check(sizes_set, nb_parameters);
						ARRAY_DUP(handles_ptr, task->task.handles, nb_parameters);
					}
					else
					{
						task->task.dyn_modes = modes_ptr;
						_STARPU_MALLOC(task->task.cl->dyn_modes, (sizeof(*task->task.cl->dyn_modes) * nb_parameters));
						ARRAY_DUP(modes_ptr, task->task.cl->dyn_modes, nb_parameters);
						variable_data_register_check(sizes_set, nb_parameters);
						task->task.dyn_handles = handles_ptr;
					}

					task->task.nbuffers = nb_parameters;

					struct perfmodel * realmodel;

					HASH_FIND_STR(model_hash, model, realmodel);

					if (realmodel == NULL)
					{
						int len = strlen(model);
						_STARPU_CALLOC(realmodel, 1, sizeof(struct perfmodel));

						_STARPU_MALLOC(realmodel->model_name, sizeof(char) * (len+1));
						realmodel->model_name = strcpy(realmodel->model_name, model);

						starpu_perfmodel_init(&realmodel->perfmodel);

						HASH_ADD_STR(model_hash, model_name, realmodel);

						int error = starpu_perfmodel_load_symbol(model, &realmodel->perfmodel);

						if (error)
						{

							fprintf(stderr, "[starpu][Warning] Error loading perfmodel symbol %s\n", model);
							exit(EXIT_FAILURE);
						}

					}

					int narch = starpu_perfmodel_get_narch_combs();

					struct task_arg *arg;
					_STARPU_MALLOC(arg, sizeof(struct task_arg) + sizeof(double) * narch);
					arg->footprint = footprint;
					double * perfTime  = arg->perf;
					int i;

					for (i = 0; i < narch ; i++)
					{
						struct starpu_perfmodel_arch *arch = starpu_perfmodel_arch_comb_fetch(i);
						perfTime[i] = starpu_perfmodel_history_based_expected_perf(&realmodel->perfmodel, arch, footprint);
					}

					task->task.cl_arg = arg;
					task->task.flops = flops;
				}

				task->task.cl_arg_size = 0;
				task->task.tag_id = tag;
				task->task.use_tag = 1;

				task->ndependson = ndependson;
				if (ndependson > 0)
				{
					_STARPU_MALLOC(task->deps, ndependson * sizeof (* task->deps));
					ARRAY_DUP(dependson, task->deps, ndependson);
				}
			}

			else
			{
				ARRAY_DUP(handles_ptr, task->task.handles, nb_parameters);
			}

			/* Add this task to task hash */
			HASH_ADD(hh, tasks, jobid, sizeof(jobid), task);

			reset();
		}

		/* Record various information */
#define TEST(field) (!strncmp(s, field": ", strlen(field) + 2))

		else if(TEST("Control"))
		{
			char * c = s+9;

			if(!strncmp(c, "WontUse", 7))
			{
				control = WontUseTask;
				nb_parameters = 1;
				alloc_mode = set_alloc_mode(nb_parameters);
				arrays_managing(alloc_mode);
			}
			else
				control = NormalTask;
		}
		else if (TEST("Name"))
		{
			*ln = 0;
			name = strdup(s+6);
		}
		else if (TEST("Model"))
		{
			*ln = 0;
			model = strdup(s+7);
		}
		else if (TEST("JobId"))
			jobid = atol(s+7);
		else if(TEST("SubmitOrder"))
			submitorder = atoi(s+13);
		else if (TEST("DependsOn"))
		{
			char *c = s + 11;

			for (ndependson = 0; *c != '\n'; ndependson++)
			{
				if (ndependson >= dependson_size)
				{
					dependson_size *= 2;
					_STARPU_REALLOC(dependson, dependson_size * sizeof(*dependson));
				}
				dependson[ndependson] = strtol(c, &c, 10);
			}
		}
		else if (TEST("Tag"))
			tag = strtol(s+5, NULL, 16);
		else if (TEST("WorkerId"))
			workerid = atoi(s+10);
		else if (TEST("Footprint"))
		{
			footprint = strtoul(s+11, NULL, 16);
		}
		else if (TEST("Parameters"))
		{
			/* Parameters line format is PARAM1_PARAM2_(...)PARAMi_(...)PARAMn */
			char * param_str = s + 12;
			unsigned i;
			int count = 0;

			for (i = 0 ; param_str[i] != '\n'; i++)
			{
				if (param_str[i] == '_') /* Checking the number of '_' (underscore), assuming that the file is not corrupted */
				{
					count++;
				}
			}

			nb_parameters = count + 1; /* There is one underscore per paramater execept for the last one, that's why we have to add +1 (dirty programming) */

			/* This part of the algorithm will determine if it needs static or dynamic arrays */
			alloc_mode = set_alloc_mode(nb_parameters);
			arrays_managing(alloc_mode);

		}
		else if (TEST("Handles"))
		{
			char *buffer = s + 9;
			const char *delim = " ";
			char *token = strtok(buffer, delim);

			while (token != NULL)
			{
				int i;

				for (i = 0 ; i < nb_parameters ; i++)
				{
					struct handle *handles_cell; /* A cell of the hash table for the handles */
					starpu_data_handle_t  handle_value = (starpu_data_handle_t) strtol(token, NULL, 16); /* Get the ith handle on the line (in the file) */

					HASH_FIND(hh, handles_hash, &handle_value, sizeof(handle_value), handles_cell); /* Find if the handle_value was already registered as a key in the hash table */

					/* If it wasn't, then add it to the hash table */
					if (handles_cell == NULL)
					{
						/* Hide the initial handle from the file into the handles array to find it when necessary */
						handles_ptr[i] = handle_value;
						reg_signal[i] = 1;
					}
					else
					{
						handles_ptr[i] = handles_cell->mem_ptr;
					}

					token = strtok(NULL, delim);
				}
			}
		}
		else if (TEST("Modes"))
		{
			char * buffer = s + 7;
			int mode_i = 0;
			int i = 0;
			const char * delim = " ";
			char * token = strtok(buffer, delim);

			while (token != NULL)
			{
				/* Subject to the names of starpu modes enumerator are not modified */
				if (!strncmp(token, "RW", 2))
				{
					*(modes_ptr+mode_i) = STARPU_RW;
					mode_i++;
					i++;
				}
				else if (!strncmp(token, "R", 1))
				{
					*(modes_ptr+mode_i) = STARPU_R;
					mode_i++;
				}
				else if (!strncmp(token, "W", 1))
				{
					*(modes_ptr+mode_i) = STARPU_W;
					mode_i++;
				}
				/* Other cases produce a warning*/
				else
				{
					fprintf(stderr, "[Warning] A mode is different from R/W (jobid task : %lu)", jobid);
				}
				token = strtok(NULL, delim);
			}
		}
		else if (TEST("Sizes"))
		{
			char *  buffer = s + 7;
			const char * delim = " ";
			char * token = strtok(buffer, delim);
			int k = 0;

			_STARPU_MALLOC(sizes_set, nb_parameters * sizeof(size_t));

			while (token != NULL)
			{
				sizes_set[k] = strtol(token, NULL, 10);
				token = strtok(NULL, delim);

				k++;
			}
		}
		else if (TEST("StartTime"))
		{
			startTime = strtod(s+11, NULL);
		}
		else if (TEST("EndTime"))
		{
			endTime = strtod(s+9, NULL);
		}
		else if (TEST("GFlop"))
		{
			flops = 1000000000 * strtod(s+7, NULL);
		}
		else if (TEST("Iteration"))
		{
			iteration = (unsigned) strtol(s+11, NULL, 10);
		}
		else if (TEST("Priority"))
		{
			priority = strtol(s + 10, NULL, 10);
		}
	}

eof:

	starpu_task_wait_for_all();

	/* FREE allocated memory */

	free(dependson);
	free(s);

	/* End of FREE */

	struct handle * handle,* handletmp;
	HASH_ITER(hh, handles_hash, handle, handletmp)
	{
		starpu_data_unregister(handle->mem_ptr);
		HASH_DEL(handles_hash, handle);
		free(handle);
        }
	
	struct perfmodel * model_s, * modeltmp;
	HASH_ITER(hh, model_hash, model_s, modeltmp)
	{
		starpu_perfmodel_unload_model(&model_s->perfmodel);
		HASH_DEL(model_hash, model_s);
		free(model_s->model_name);
		free(model_s);
        }

	struct task * task, *tasktmp;
	HASH_ITER(hh, tasks, task, tasktmp)
	{
		free(task->task.cl_arg);
		free((char*)task->task.name);

		if (task->task.dyn_handles != NULL)
		{
			free(task->task.dyn_handles);
			free(task->task.dyn_modes);
		}

		HASH_DEL(tasks, task);
		starpu_task_clean(&task->task);
		free(task->deps);
		starpu_rbtree_remove(&tree, &task->node);
		free(task);
        }

	starpu_shutdown();

	return 0;

enodev:
	return 77;
}
