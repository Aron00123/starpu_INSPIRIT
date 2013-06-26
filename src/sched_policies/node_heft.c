#include "node_sched.h"
#include "fifo_queues.h"
#include <starpu_perfmodel.h>
#include <starpu_scheduler.h>
#include <float.h>


struct _starpu_dmda_data
{
	double alpha;
	double beta;
	double gamma;
	double idle_power;
	struct _starpu_sched_node * no_model_node;
};

static double compute_fitness_calibration(struct _starpu_sched_node * child,
					  struct _starpu_dmda_data * data STARPU_ATTRIBUTE_UNUSED,
					  struct _starpu_task_execute_preds *pred,
					  double best_exp_end STARPU_ATTRIBUTE_UNUSED,
					  double max_exp_end STARPU_ATTRIBUTE_UNUSED)
{
	if(pred->state == CALIBRATING)
		return child->estimated_load(child);
	return DBL_MAX;
}

static double compute_fitness_perf_model(struct _starpu_sched_node * child STARPU_ATTRIBUTE_UNUSED,
					 struct _starpu_dmda_data * data,
					 struct _starpu_task_execute_preds * preds,
					 double best_exp_end,
					 double max_exp_end)
{
	double fitness;
	switch(preds->state)
	{
	case CANNOT_EXECUTE:
	case NO_PERF_MODEL:
		return DBL_MAX;
	case PERF_MODEL:
		fitness = data->alpha * (preds->expected_finish_time - best_exp_end)
			+ data->beta  * preds->expected_transfer_length
			+ data->gamma * preds->expected_power;
		return fitness;
	case CALIBRATING:
		STARPU_ASSERT_MSG(0,"we should have calibrate this task");
	default:
		STARPU_ABORT();
		break;
	}
}

static int push_task(struct _starpu_sched_node * node, struct starpu_task * task)
{
	struct _starpu_task_execute_preds preds[node->nchilds];
	int i;
	int calibrating = 0;
	int perf_model = 0;
	int can_execute = 0;
	double best_exp_end = DBL_MAX;
	double max_exp_end = DBL_MIN;
	for(i = 0; i < node->nchilds; i++)
	{
		preds[i] = node->childs[i]->estimated_execute_preds(node->childs[i], task);
		switch(preds[i].state)
		{
		case PERF_MODEL:
			STARPU_ASSERT(!isnan(preds[i].expected_finish_time));
			perf_model = 1;
			can_execute = 1;
 			if(preds[i].expected_finish_time < best_exp_end)
				best_exp_end = preds[i].expected_finish_time;
			else if(preds[i].expected_finish_time > max_exp_end)
				max_exp_end = preds[i].expected_finish_time;
			break;
		case CALIBRATING:
			calibrating = 1;
			can_execute = 1;
			break;
		case NO_PERF_MODEL:
			can_execute = 1;
			break;
		case CANNOT_EXECUTE:
			break;
		}
	}
	if(!can_execute)
	{
		return -ENODEV;
	}

	struct _starpu_dmda_data * data = node->data;

	if(!calibrating && !perf_model)
	{
		int ret = data->no_model_node->push_task(data->no_model_node, task);
		return ret;
	}

	double (*fitness_fun)(struct _starpu_sched_node *,
			      struct _starpu_dmda_data *,
			      struct _starpu_task_execute_preds*,
			      double,
			      double) = compute_fitness_perf_model;

	if(calibrating)
		fitness_fun = compute_fitness_calibration;



	double best_fitness = DBL_MAX;
	int index_best_fitness = -1;
	for(i = 0; i < node->nchilds; i++)
	{
		double tmp = fitness_fun(node->childs[i],
					 node->data,
					 preds + i,
					 best_exp_end,
					 max_exp_end);
//		fprintf(stderr,"fitness for worker %d is %f\n",i,tmp == DBL_MAX ? -1 : tmp);
		if(tmp < best_fitness)
		{
			best_fitness = tmp;
			index_best_fitness = i;
		}
	}
//	fprintf(stderr,"push on worker %d\n",index_best_fitness);
	STARPU_ASSERT(best_fitness != DBL_MAX);

	struct _starpu_sched_node * c = node->childs[index_best_fitness];
	starpu_task_set_implementation(task, preds[index_best_fitness].impl);
	task->predicted = preds[index_best_fitness].expected_length;
	task->predicted_transfer = preds[index_best_fitness].expected_transfer_length;
	return c->push_task(c, task);
}
/*
static void update_helper_node(struct _starpu_sched_node * heft_node)
{
	struct _starpu_dmda_data * data = heft_node->data;
	struct _starpu_sched_node * node = data->no_model_node;
	node->nchilds = heft_node->nchilds;
	node->childs = realloc(node->childs, sizeof(struct _starpu_sched_node *) * node->nchilds);
	memcpy(node->childs, heft_node->childs, sizeof(struct _starpu_sched_node*) * node->nchilds);
	node->nworkers = heft_node->nworkers;
	memcpy(node->workerids, heft_node->workerids, sizeof(int) * node->nworkers);
}
*/


#define _STARPU_SCHED_ALPHA_DEFAULT 1.0
#define _STARPU_SCHED_BETA_DEFAULT 1.0
#define _STARPU_SCHED_GAMMA_DEFAULT 1000.0
static double alpha = _STARPU_SCHED_ALPHA_DEFAULT;
static double beta = _STARPU_SCHED_BETA_DEFAULT;
static double _gamma = _STARPU_SCHED_GAMMA_DEFAULT;

#ifdef STARPU_USE_TOP
static const float alpha_minimum=0;
static const float alpha_maximum=10.0;
static const float beta_minimum=0;
static const float beta_maximum=10.0;
static const float gamma_minimum=0;
static const float gamma_maximum=10000.0;
static const float idle_power_minimum=0;
static const float idle_power_maximum=10000.0;
#endif /* !STARPU_USE_TOP */

static double idle_power = 0.0;

#ifdef STARPU_USE_TOP
static void param_modified(struct starpu_top_param* d)
{
#ifdef STARPU_DEVEL
#warning FIXME: get sched ctx to get alpha/beta/gamma/idle values
#endif
	/* Just to show parameter modification. */
	fprintf(stderr,
		"%s has been modified : "
		"alpha=%f|beta=%f|gamma=%f|idle_power=%f !\n",
		d->name, alpha,beta,_gamma, idle_power);
}
#endif /* !STARPU_USE_TOP */

void init_heft_data(struct _starpu_sched_node *node)
{

	const char *strval_alpha = getenv("STARPU_SCHED_ALPHA");
	if (strval_alpha)
		alpha = atof(strval_alpha);

	const char *strval_beta = getenv("STARPU_SCHED_BETA");
	if (strval_beta)
		beta = atof(strval_beta);

	const char *strval_gamma = getenv("STARPU_SCHED_GAMMA");
	if (strval_gamma)
		_gamma = atof(strval_gamma);

	const char *strval_idle_power = getenv("STARPU_IDLE_POWER");
	if (strval_idle_power)
		idle_power = atof(strval_idle_power);

#ifdef STARPU_USE_TOP
	starpu_top_register_parameter_float("DMDA_ALPHA", &alpha,
					    alpha_minimum, alpha_maximum, param_modified);
	starpu_top_register_parameter_float("DMDA_BETA", &beta,
					    beta_minimum, beta_maximum, param_modified);
	starpu_top_register_parameter_float("DMDA_GAMMA", &_gamma,
					    gamma_minimum, gamma_maximum, param_modified);
	starpu_top_register_parameter_float("DMDA_IDLE_POWER", &idle_power,
					    idle_power_minimum, idle_power_maximum, param_modified);
#endif /* !STARPU_USE_TOP */


	struct _starpu_dmda_data * data = malloc(sizeof(*data));
	memset(data, 0, sizeof(*data));
	data->alpha = alpha;
	data->beta = beta;
	data->gamma = _gamma;
	data->idle_power = idle_power;

	node->data = data;

	_starpu_sched_node_heft_set_no_model_node(node, _starpu_sched_node_random_create,NULL);
}

static void destroy_no_model_node(struct _starpu_sched_node * heft_node)
{
	struct _starpu_dmda_data * data = heft_node->data;
	if(data->no_model_node)
	{
		_starpu_sched_node_destroy(data->no_model_node);
	}
}

void deinit_heft_data(struct _starpu_sched_node * node)
{
	destroy_no_model_node(node);
	free(node->data);
}

void _starpu_sched_node_heft_set_no_model_node(struct _starpu_sched_node * heft_node,
					       struct _starpu_sched_node * (*create_no_model_node)(void *),void * arg)
{
	destroy_no_model_node(heft_node);
	struct _starpu_dmda_data * data = heft_node->data;
	struct _starpu_sched_node * no_model_node = create_no_model_node(arg);
	no_model_node->childs = malloc(heft_node->nchilds * sizeof(struct _starpu_sched_node *));
	memcpy(no_model_node->childs, heft_node->childs, heft_node->nchilds * sizeof(struct _strapu_sched_node *));

	no_model_node->nchilds = heft_node->nchilds;
	no_model_node->init_data(no_model_node);
	data->no_model_node = no_model_node;
}

struct _starpu_sched_node * _starpu_sched_node_heft_create(void * arg STARPU_ATTRIBUTE_UNUSED)
{
	struct _starpu_sched_node * node = _starpu_sched_node_create();

	node->push_task = push_task;
	node->init_data = init_heft_data;
	node->deinit_data = deinit_heft_data;

	return node;
}

int _starpu_sched_node_is_heft(struct _starpu_sched_node * node)
{
	return node->init_data == init_heft_data;
}



static void initialize_heft_center_policy(unsigned sched_ctx_id)
{
	starpu_sched_ctx_create_worker_collection(sched_ctx_id, STARPU_WORKER_LIST);
	
	struct _starpu_sched_tree * t = _starpu_sched_tree_create();
	t->root = _starpu_sched_node_heft_create(NULL);
	
	unsigned i;
	for(i = 0; i < starpu_worker_get_count() + starpu_combined_worker_get_count(); i++)
	{
		struct _starpu_sched_node * worker_node = _starpu_sched_node_worker_get(i);
		STARPU_ASSERT(worker_node);
/*
		struct _starpu_sched_node * fifo_node = _starpu_sched_node_fifo_create(NULL);
		_starpu_sched_node_add_child(fifo_node, worker_node);
		_starpu_sched_node_set_father(worker_node, fifo_node, sched_ctx_id);
*/
		_starpu_sched_node_add_child(t->root, worker_node);
		_starpu_sched_node_set_father(worker_node, t->root, sched_ctx_id);
	}
	
	_starpu_set_workers_bitmaps();
	_starpu_tree_call_init_data(t);

	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)t);
}

static void deinitialize_heft_center_policy(unsigned sched_ctx_id)
{
	struct _starpu_sched_tree *t = (struct _starpu_sched_tree*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	_starpu_sched_tree_destroy(t, sched_ctx_id);
	starpu_sched_ctx_delete_worker_collection(sched_ctx_id);
}





struct starpu_sched_policy _starpu_sched_tree_heft_policy =
{
	.init_sched = initialize_heft_center_policy,
	.deinit_sched = deinitialize_heft_center_policy,
	.add_workers = _starpu_tree_add_workers,
	.remove_workers = _starpu_tree_remove_workers,
	.push_task = _starpu_tree_push_task,
	.pop_task = _starpu_tree_pop_task,
	.pre_exec_hook = NULL,
	.post_exec_hook = NULL,
	.pop_every_task = NULL,
	.policy_name = "tree-heft",
	.policy_description = "heft tree policy"
};
