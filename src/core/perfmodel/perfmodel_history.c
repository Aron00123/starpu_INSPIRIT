/*
 * StarPU
 * Copyright (C) INRIA 2008-2010 (see AUTHORS file)
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

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <common/config.h>
#include <core/perfmodel/perfmodel.h>
#include <core/jobs.h>
#include <core/workers.h>
#include <pthread.h>
#include <datawizard/datawizard.h>
#include <core/perfmodel/regression.h>
#include <common/config.h>

/*
 * History based model
 */


static void insert_history_entry(struct starpu_history_entry_t *entry, struct starpu_history_list_t **list, struct starpu_htbl32_node_s **history_ptr)
{
	struct starpu_history_list_t *link;
	struct starpu_history_entry_t *old;

	link = malloc(sizeof(struct starpu_history_list_t));
	link->next = *list;
	link->entry = entry;
	*list = link;

	old = htbl_insert_32(history_ptr, entry->footprint, entry);
	/* that may fail in case there is some concurrency issue */
	STARPU_ASSERT(old == NULL);
}


static void dump_reg_model(FILE *f, struct starpu_regression_model_t *reg_model)
{
	fprintf(f, "# sumlnx\tsumlnx2\t\tsumlny\t\tsumlnxlny\talpha\t\tbeta\t\tn\n");
	fprintf(f, "%-15le\t%-15le\t%-15le\t%-15le\t%-15le\t%-15le\t%u\n", reg_model->sumlnx, reg_model->sumlnx2, reg_model->sumlny, reg_model->sumlnxlny, reg_model->alpha, reg_model->beta, reg_model->nsample);
}

static void scan_reg_model(FILE *f, struct starpu_regression_model_t *reg_model)
{
	int res;

	starpu_drop_comments(f);

	res = fscanf(f, "%le\t%le\t%le\t%le\t%le\t%le\t%u\n", &reg_model->sumlnx, &reg_model->sumlnx2, &reg_model->sumlny, &reg_model->sumlnxlny, &reg_model->alpha, &reg_model->beta, &reg_model->nsample);
	STARPU_ASSERT(res == 7);
}


static void dump_history_entry(FILE *f, struct starpu_history_entry_t *entry)
{
	fprintf(f, "%08x\t%-15lu\t%-15le\t%-15le\t%-15le\t%-15le\t%u\n", entry->footprint, (unsigned long) entry->size, entry->mean, entry->deviation, entry->sum, entry->sum2, entry->nsample);
}

static void scan_history_entry(FILE *f, struct starpu_history_entry_t *entry)
{
	int res;

	starpu_drop_comments(f);

	res = fscanf(f, "%x\t%zu\t%le\t%le\t%le\t%le\t%u\n", &entry->footprint, &entry->size, &entry->mean, &entry->deviation, &entry->sum, &entry->sum2, &entry->nsample);
	STARPU_ASSERT(res == 7);
}

static void parse_per_arch_model_file(FILE *f, struct starpu_per_arch_perfmodel_t *per_arch_model, unsigned scan_history)
{
	unsigned nentries;

	starpu_drop_comments(f);

	int res = fscanf(f, "%u\n", &nentries);
	STARPU_ASSERT(res == 1);

	scan_reg_model(f, &per_arch_model->regression);

	starpu_drop_comments(f);

	res = fscanf(f, "%le\t%le\t%le\n", 
		&per_arch_model->regression.a,
		&per_arch_model->regression.b,
		&per_arch_model->regression.c);
	STARPU_ASSERT(res == 3);

	if (isnan(per_arch_model->regression.a)||isnan(per_arch_model->regression.b)||isnan(per_arch_model->regression.c))
	{
		per_arch_model->regression.valid = 0;
	}
	else {
		per_arch_model->regression.valid = 1;
	}

	if (!scan_history)
		return;

	/* parse cpu entries */
	unsigned i;
	for (i = 0; i < nentries; i++) {
		struct starpu_history_entry_t *entry = malloc(sizeof(struct starpu_history_entry_t));
		STARPU_ASSERT(entry);

		scan_history_entry(f, entry);
		
		/* insert the entry in the hashtable and the list structures  */
		insert_history_entry(entry, &per_arch_model->list, &per_arch_model->history);
	}
}

static void parse_model_file(FILE *f, struct starpu_perfmodel_t *model, unsigned scan_history)
{
	unsigned arch;
	for (arch = 0; arch < STARPU_NARCH_VARIATIONS; arch++)
		parse_per_arch_model_file(f, &model->per_arch[arch], scan_history);
}

static void dump_per_arch_model_file(FILE *f, struct starpu_per_arch_perfmodel_t *per_arch_model)
{
	/* count the number of elements in the lists */
	struct starpu_history_list_t *ptr;
	unsigned nentries = 0;

	ptr = per_arch_model->list;
	while(ptr) {
		nentries++;
		ptr = ptr->next;
	}

	/* header */
	fprintf(f, "# number of entries\n%u\n", nentries);

	dump_reg_model(f, &per_arch_model->regression);

	double a,b,c;
	_starpu_regression_non_linear_power(per_arch_model->list, &a, &b, &c);
	fprintf(f, "# a\t\tb\t\tc\n");
	fprintf(f, "%-15le\t%-15le\t%-15le\n", a, b, c);

	fprintf(f, "# hash\t\tsize\t\tmean\t\tdev\t\tsum\t\tsum2\t\tn\n");
	ptr = per_arch_model->list;
	while (ptr) {
		//memcpy(&entries_array[i++], ptr->entry, sizeof(struct starpu_history_entry_t));
		dump_history_entry(f, ptr->entry);
		ptr = ptr->next;
	}
}

static void dump_model_file(FILE *f, struct starpu_perfmodel_t *model)
{
	fprintf(f, "#################\n");

	unsigned arch;
	for (arch = 0; arch < STARPU_NARCH_VARIATIONS; arch++)
	{
		char archname[32];
		starpu_perfmodel_get_arch_name(arch, archname, 32);
		fprintf(f, "# Model for %s\n", archname);
		dump_per_arch_model_file(f, &model->per_arch[arch]);
		fprintf(f, "\n##################\n");
	}
}

static void initialize_per_arch_model(struct starpu_per_arch_perfmodel_t *per_arch_model)
{
	per_arch_model->history = NULL;
	per_arch_model->list = NULL;
}

static void initialize_model(struct starpu_perfmodel_t *model)
{
	unsigned arch;
	for (arch = 0; arch < STARPU_NARCH_VARIATIONS; arch++)
		initialize_per_arch_model(&model->per_arch[arch]);
}

static struct starpu_model_list_t *registered_models = NULL;
//static unsigned debug_modelid = 0;

static void get_model_debug_path(struct starpu_perfmodel_t *model, const char *arch, char *path, size_t maxlen)
{
	STARPU_ASSERT(path);

	_starpu_get_perf_model_dir_debug(path, maxlen);
	strncat(path, model->symbol, maxlen);
	
	char hostname[32];
	gethostname(hostname, 32);
	strncat(path, ".", maxlen);
	strncat(path, hostname, maxlen);
	strncat(path, ".", maxlen);
	strncat(path, arch, maxlen);
	strncat(path, ".debug", maxlen);
}

void _starpu_register_model(struct starpu_perfmodel_t *model)
{
	/* add the model to a linked list */
	struct starpu_model_list_t *node = malloc(sizeof(struct starpu_model_list_t));

	node->model = model;
	//model->debug_modelid = debug_modelid++;

	/* put this model at the beginning of the list */
	node->next = registered_models;
	registered_models = node;

#ifdef STARPU_MODEL_DEBUG
	_starpu_create_sampling_directory_if_needed();

	unsigned arch;
	for (arch = 0; arch < NARCH_VARIATIONS; arch++)
	{
		char debugpath[256];
		starpu_perfmodel_debugfilepath(model, arch, debugpath, 256);
		model->per_arch[arch].debug_file = fopen(debugpath, "a+");
		STARPU_ASSERT(model->per_arch[arch].debug_file);
	}
#endif

	return;
}

static void get_model_path(struct starpu_perfmodel_t *model, char *path, size_t maxlen)
{
	_starpu_get_perf_model_dir_codelets(path, maxlen);
	strncat(path, model->symbol, maxlen);
	
	char hostname[32];
	gethostname(hostname, 32);
	strncat(path, ".", maxlen);
	strncat(path, hostname, maxlen);
}

static void save_history_based_model(struct starpu_perfmodel_t *model)
{
	STARPU_ASSERT(model);
	STARPU_ASSERT(model->symbol);

	/* TODO checks */

	/* filename = $STARPU_PERF_MODEL_DIR/codelets/symbol.hostname */
	char path[256];
	get_model_path(model, path, 256);

#ifdef STARPU_VERBOSE
	fprintf(stderr, "Opening performance model file %s for model %s\n", path, model->symbol);
#endif

	/* overwrite existing file, or create it */
	FILE *f;
	f = fopen(path, "w+");
	STARPU_ASSERT(f);

	dump_model_file(f, model);

	fclose(f);

#ifdef DEBUG_MODEL
	fclose(model->gordon_debug_file);
	fclose(model->cuda_debug_file);
	fclose(model->cpu_debug_file);
#endif
}

void _starpu_dump_registered_models(void)
{
	struct starpu_model_list_t *node;
	node = registered_models;

#ifdef STARPU_VERBOSE
	fprintf(stderr, "DUMP MODELS !\n");
#endif

	while (node) {
		save_history_based_model(node->model);		
		node = node->next;

		/* XXX free node */
	}
}


static void load_history_based_model(struct starpu_perfmodel_t *model, unsigned scan_history)
{
	STARPU_ASSERT(model);
	STARPU_ASSERT(model->symbol);

	unsigned have_to_load;
	have_to_load = STARPU_BOOL_COMPARE_AND_SWAP (&model->is_loaded, 
				STARPU_PERFMODEL_NOT_LOADED,
				STARPU_PERFMODEL_LOADING);
	if (!have_to_load)
	{
		/* someone is already loading the model, we wait until it's finished */
		while (model->is_loaded != STARPU_PERFMODEL_LOADED)
		{
			STARPU_SYNCHRONIZE();
		}
		return;
	}
	
	int res;
	res = pthread_rwlock_init(&model->model_rwlock, NULL);
	if (STARPU_UNLIKELY(res))
	{
		perror("pthread_rwlock_init failed");
		STARPU_ABORT();
	}

	res = pthread_rwlock_wrlock(&model->model_rwlock);
	if (STARPU_UNLIKELY(res))
	{
		perror("pthread_rwlock_wrlock failed");
		STARPU_ABORT();
	}

	/* make sure the performance model directory exists (or create it) */
	_starpu_create_sampling_directory_if_needed();

	/*
	 * We need to keep track of all the model that were opened so that we can 
	 * possibly update them at runtime termination ...
	 */
	_starpu_register_model(model);

	char path[256];
	get_model_path(model, path, 256);

#ifdef STARPU_VERBOSE
	fprintf(stderr, "Opening performance model file %s for model %s ... ", path, model->symbol);
#endif
	
	/* try to open an existing file and load it */
	res = access(path, F_OK); 
	if (res == 0) {
#ifdef STARPU_VERBOSE
		fprintf(stderr, "File exists !\n");
#endif

		FILE *f;
		f = fopen(path, "r");
		STARPU_ASSERT(f);

		parse_model_file(f, model, scan_history);

		fclose(f);
	}
	else {
#ifdef STARPU_VERBOSE
		fprintf(stderr, "File does not exists !\n");
#endif
		initialize_model(model);
	}


	if (starpu_get_env_number("STARPU_CALIBRATE") != -1)
	{
		fprintf(stderr, "STARPU_CALIBRATE model %s\n", model->symbol);
		model->benchmarking = 1;
	}
	else {
		model->benchmarking = 0;
	}

	model->is_loaded = STARPU_PERFMODEL_LOADED;

	res = pthread_rwlock_unlock(&model->model_rwlock);
	if (STARPU_UNLIKELY(res))
	{
		perror("pthread_rwlock_unlock");
		STARPU_ABORT();
	}
}

/* This function is intended to be used by external tools that should read
 * the performance model files */
int starpu_list_models(void)
{
#ifdef DT_REG
        char path[256];
        DIR *dp;
        struct dirent *ep;

	char perf_model_dir_codelets[256];
	_starpu_get_perf_model_dir_codelets(perf_model_dir_codelets, 256);

        strncpy(path, perf_model_dir_codelets, 256);
        dp = opendir(path);
        if (dp != NULL) {
                while ((ep = readdir(dp))) {
                        if (ep->d_type == DT_REG) {
                                fprintf(stdout, "file: <%s>\n", ep->d_name);
                        }
                }
                closedir (dp);
                return 0;
        }
        else {
                perror ("Couldn't open the directory");
                return 1;
        }
#else
	perror ("not supported\n");
	return 1;
#endif
}

/* This function is intended to be used by external tools that should read the
 * performance model files */
int starpu_load_history_debug(const char *symbol, struct starpu_perfmodel_t *model)
{
	model->symbol = symbol;

	/* where is the file if it exists ? */
	char path[256];
	get_model_path(model, path, 256);

//	fprintf(stderr, "get_model_path -> %s\n", path);

	/* does it exist ? */
	int res;
	res = access(path, F_OK);
	if (res) {
		fprintf(stderr, "There is no performance model for symbol %s\n", symbol);
		return 1;
	}

	FILE *f = fopen(path, "r");
	STARPU_ASSERT(f);

	parse_model_file(f, model, 1);

	return 0;
}

void starpu_perfmodel_get_arch_name(enum starpu_perf_archtype arch, char *archname, size_t maxlen)
{
	if (arch == STARPU_CPU_DEFAULT)
	{
		snprintf(archname, maxlen, "cpu");
	}
	else if ((STARPU_CUDA_DEFAULT <= arch)
		&& (arch < STARPU_CUDA_DEFAULT + STARPU_MAXCUDADEVS))
	{
		int devid = arch - STARPU_CUDA_DEFAULT;
		snprintf(archname, maxlen, "cuda_%d", devid);
	}
	else if (arch == STARPU_GORDON_DEFAULT)
	{
		snprintf(archname, maxlen, "gordon");
	}
	else
	{
		STARPU_ABORT();
	}
}

void starpu_perfmodel_debugfilepath(struct starpu_perfmodel_t *model,
		enum starpu_perf_archtype arch, char *path, size_t maxlen)
{
	char archname[32];
	starpu_perfmodel_get_arch_name(arch, archname, 32);

	STARPU_ASSERT(path);

	get_model_debug_path(model, archname, path, maxlen);
}

double _starpu_regression_based_job_expected_length(struct starpu_perfmodel_t *model, enum starpu_perf_archtype arch, struct starpu_job_s *j)
{
	double exp = -1.0;
	size_t size = _starpu_job_get_data_size(j);
	struct starpu_regression_model_t *regmodel;

	if (STARPU_UNLIKELY(model->is_loaded != STARPU_PERFMODEL_LOADED))
		load_history_based_model(model, 0);

	regmodel = &model->per_arch[arch].regression;

	if (regmodel->valid)
		exp = regmodel->a*pow(size, regmodel->b) + regmodel->c;

	return exp;
}

double _starpu_history_based_job_expected_length(struct starpu_perfmodel_t *model, enum starpu_perf_archtype arch, struct starpu_job_s *j)
{
	double exp;
	struct starpu_per_arch_perfmodel_t *per_arch_model;
	struct starpu_history_entry_t *entry;
	struct starpu_htbl32_node_s *history;

	if (STARPU_UNLIKELY(model->is_loaded != STARPU_PERFMODEL_LOADED))
		load_history_based_model(model, 1);

	if (STARPU_UNLIKELY(!j->footprint_is_computed))
		_starpu_compute_buffers_footprint(j);
		
	uint32_t key = j->footprint;

	per_arch_model = &model->per_arch[arch];

	history = per_arch_model->history;
	if (!history)
		return -1.0;

	pthread_rwlock_rdlock(&model->model_rwlock);
	entry = htbl_search_32(history, key);
	pthread_rwlock_unlock(&model->model_rwlock);

	exp = entry?entry->mean:-1.0;

	return exp;
}

void _starpu_update_perfmodel_history(starpu_job_t j, enum starpu_perf_archtype arch, unsigned cpuid __attribute__((unused)), double measured)
{
	struct starpu_perfmodel_t *model = j->task->cl->model;

	if (model)
	{
		struct starpu_per_arch_perfmodel_t *per_arch_model = &model->per_arch[arch];

		if (model->type == STARPU_HISTORY_BASED || model->type == STARPU_REGRESSION_BASED)
		{
			uint32_t key = j->footprint;
			struct starpu_history_entry_t *entry;

			struct starpu_htbl32_node_s *history;
			struct starpu_htbl32_node_s **history_ptr;
			struct starpu_regression_model_t *reg_model;

			struct starpu_history_list_t **list;


			history = per_arch_model->history;
			history_ptr = &per_arch_model->history;
			reg_model = &per_arch_model->regression;
			list = &per_arch_model->list;

			pthread_rwlock_wrlock(&model->model_rwlock);
	
				entry = htbl_search_32(history, key);
	
				if (!entry)
				{
					/* this is the first entry with such a footprint */
					entry = malloc(sizeof(struct starpu_history_entry_t));
					STARPU_ASSERT(entry);
						entry->mean = measured;
						entry->sum = measured;
	
						entry->deviation = 0.0;
						entry->sum2 = measured*measured;
	
						entry->size = _starpu_job_get_data_size(j);
	
						entry->footprint = key;
						entry->nsample = 1;
	
					insert_history_entry(entry, list, history_ptr);
	
				}
				else {
					/* there is already some entry with the same footprint */
					entry->sum += measured;
					entry->sum2 += measured*measured;
					entry->nsample++;
	
					unsigned n = entry->nsample;
					entry->mean = entry->sum / n;
					entry->deviation = sqrt((entry->sum2 - (entry->sum*entry->sum)/n)/n);
				}
			
				STARPU_ASSERT(entry);
			
			/* update the regression model as well */
			double logy, logx;
			logx = log(entry->size);
			logy = log(measured);

			reg_model->sumlnx += logx;
			reg_model->sumlnx2 += logx*logx;
			reg_model->sumlny += logy;
			reg_model->sumlnxlny += logx*logy;
			reg_model->nsample++;

			unsigned n = reg_model->nsample;
			
			double num = (n*reg_model->sumlnxlny - reg_model->sumlnx*reg_model->sumlny);
			double denom = (n*reg_model->sumlnx2 - reg_model->sumlnx*reg_model->sumlnx);

			reg_model->beta = num/denom;
			reg_model->alpha = exp((reg_model->sumlny - reg_model->beta*reg_model->sumlnx)/n);
			
			pthread_rwlock_unlock(&model->model_rwlock);
		}

#ifdef STARPU_MODEL_DEBUG
		FILE * debug_file = per_arch_model->debug_file;

		pthread_rwlock_wrlock(&model->model_rwlock);

		STARPU_ASSERT(j->footprint_is_computed);

		fprintf(debug_file, "0x%x\t%lu\t%lf\t%lf\t%d\t\t", j->footprint, (unsigned long) _starpu_job_get_data_size(j), measured, j->predicted, cpuid);
		unsigned i;
			
		struct starpu_task *task = j->task;
		for (i = 0; i < task->cl->nbuffers; i++)
		{
			struct starpu_data_state_t *state = task->buffers[i].handle;

			STARPU_ASSERT(state->ops);
			STARPU_ASSERT(state->ops->display);
			state->ops->display(state, debug_file);
		}
		fprintf(debug_file, "\n");	

		pthread_rwlock_unlock(&model->model_rwlock);
#endif
	}
}
