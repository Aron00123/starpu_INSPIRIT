/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
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

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>

#include <starpu.h>
#include <starpu_perfmodel.h>
#include <core/perfmodel/perfmodel.h> // we need to browse the list associated to history-based models

#ifdef __MINGW32__
#include <windows.h>
#endif

static struct starpu_perfmodel_t model;

/* what kernel ? */
static char *symbol = NULL;
/* which architecture ? (NULL = all)*/
static char *arch = NULL;
/* Unless a FxT file is specified, we just display the model */
static int no_fxt_file = 1;

#ifdef STARPU_USE_FXT
static struct starpu_fxt_codelet_event *dumped_codelets;
static long dumped_codelets_count;
static struct starpu_fxt_options options;
#endif

static int archtype_is_found[STARPU_NARCH_VARIATIONS];
static long dumped_per_archtype_count[STARPU_NARCH_VARIATIONS];

static char data_file_name[256];
static char gnuplot_file_name[256];

static void usage(char **argv)
{
	fprintf(stderr, "Usage: %s [ options ]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "One must specify a symbol with the -s option\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "   -s <symbol>         specify the symbol\n");
	fprintf(stderr, "   -i <Fxt files>      input FxT files generated by StarPU\n");
        fprintf(stderr, "   -a <arch>           specify the architecture (e.g. cpu, cpu:k, cuda, gordon)\n");
        fprintf(stderr, "\n");
}

static void parse_args(int argc, char **argv)
{
#ifdef STARPU_USE_FXT
	/* Default options */
	starpu_fxt_options_init(&options);

	options.out_paje_path = NULL;
	options.activity_path = NULL;
	options.distrib_time_path = NULL;
	options.dag_path = NULL;

	options.dumped_codelets = &dumped_codelets;
#endif

	/* We want to support arguments such as "-i trace_*" */
	unsigned reading_input_filenames = 0;

	int i;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0) {
			symbol = argv[++i];
			continue;
		}

		if (strcmp(argv[i], "-i") == 0) {
			reading_input_filenames = 1;
#ifdef STARPU_USE_FXT
			options.filenames[options.ninputfiles++] = argv[++i];
			no_fxt_file = 0;
#else
			fprintf(stderr, "Warning: FxT support was not enabled in StarPU: FxT traces will thus be ignored!\n");
#endif
			continue;
		}

		if (strcmp(argv[i], "-a") == 0) {
			arch = argv[++i];
			continue;
		}

		if (strcmp(argv[i], "-h") == 0) {
			usage(argv);
		        exit(-1);
		}

		/* If the reading_input_filenames flag is set, and that the
		 * argument does not match an option, we assume this may be
		 * another filename */
		if (reading_input_filenames)
		{
#ifdef STARPU_USE_FXT
			options.filenames[options.ninputfiles++] = argv[i];
#endif
			continue;
		}
	}
}

static void display_perf_model(FILE *gnuplot_file, struct starpu_perfmodel_t *model, enum starpu_perf_archtype arch, int *first)
{
	char arch_name[256];
	starpu_perfmodel_get_arch_name(arch, arch_name, 256);

#ifdef STARPU_USE_FXT
	if (!no_fxt_file && archtype_is_found[arch])
	{
		if (*first)
		{
			*first = 0;
		}
		else {
			fprintf(gnuplot_file, ",\\\n\t");
		}
	
		fprintf(gnuplot_file, "\"< grep -w \\^%d %s\" using 2:3 title \"%s\"", arch, data_file_name, arch_name);
	}
#endif

	struct starpu_per_arch_perfmodel_t *arch_model = &model->per_arch[arch];

	/* Only display the regression model if we could actually build a model */
	if (arch_model->regression.valid)
	{
		if (*first)
		{
			*first = 0;
		}
		else {
			fprintf(gnuplot_file, ",\\\n\t");
		}
	
		fprintf(stderr, "\tLinear: y = alpha size ^ beta\n");
		fprintf(stderr, "\t\talpha = %le\n", arch_model->regression.alpha * 0.001);
		fprintf(stderr, "\t\tbeta = %le\n", arch_model->regression.beta);

		fprintf(gnuplot_file, "0.001 * %f * x ** %f title \"Linear Regression %s\"",
			arch_model->regression.alpha, arch_model->regression.beta, arch_name);
	}

	if (arch_model->regression.nl_valid)
	{
		if (*first)
		{
			*first = 0;
		}
		else {
			fprintf(gnuplot_file, ",\\\n\t");
		}
	
		fprintf(stderr, "\tNon-Linear: y = a size ^b + c\n");
		fprintf(stderr, "\t\ta = %le\n", arch_model->regression.a * 0.001);
		fprintf(stderr, "\t\tb = %le\n", arch_model->regression.b);
		fprintf(stderr, "\t\tc = %le\n", arch_model->regression.c * 0.001);

		fprintf(gnuplot_file, "0.001 * %f * x ** %f + 0.001 * %f title \"Non-Linear Regression %s\"",
			arch_model->regression.a, arch_model->regression.b,  arch_model->regression.c, arch_name);
	}
}

#ifdef STARPU_USE_FXT
static void dump_data_file(FILE *data_file)
{
	memset(archtype_is_found, 0, STARPU_NARCH_VARIATIONS*sizeof(int));

	int i;
	for (i = 0; i < options.dumped_codelets_count; i++)
	{
		/* Dump only if the symbol matches user's request */
		if (strcmp(dumped_codelets[i].symbol, symbol) == 0) {
			int workerid = dumped_codelets[i].workerid;
			enum starpu_perf_archtype archtype = options.worker_archtypes[workerid];
			archtype_is_found[archtype] = 1;

			size_t size = dumped_codelets[i].size;
			float time = dumped_codelets[i].time;

			fprintf(data_file, "%d	%f	%f\n", archtype, (float)size, time);
		}
	}
}
#endif

static void display_selected_models(FILE *gnuplot_file, struct starpu_perfmodel_t *model)
{
	fprintf(gnuplot_file, "#!/usr/bin/gnuplot -persist\n");
	fprintf(gnuplot_file, "\n");
	fprintf(gnuplot_file, "set term postscript eps enhanced color\n");
	fprintf(gnuplot_file, "set output \"regression_%s.eps\"\n", symbol);
	fprintf(gnuplot_file, "set title \"Model for codelet %s\"\n", symbol);
	fprintf(gnuplot_file, "set xlabel \"Size\"\n");
	fprintf(gnuplot_file, "set ylabel \"Time\"\n");
	fprintf(gnuplot_file, "\n");
	fprintf(gnuplot_file, "set logscale x\n");
	fprintf(gnuplot_file, "set logscale y\n");
	fprintf(gnuplot_file, "\n");

	/* If no input data is given to gnuplot, we at least need to specify an
	 * arbitrary range. */
	if (no_fxt_file)
		fprintf(gnuplot_file, "set xrange [10**3:10**9]\n\n");

	int first = 1;
	fprintf(gnuplot_file, "plot\t");

	if (arch == NULL)
	{
		/* display all architectures */
		unsigned archid;
		for (archid = 0; archid < STARPU_NARCH_VARIATIONS; archid++)
			display_perf_model(gnuplot_file, model, archid, &first);
	}
	else {
		if (strcmp(arch, "cpu") == 0) {
			display_perf_model(gnuplot_file, model, STARPU_CPU_DEFAULT, &first);
			return;
		}

		int k;
		if (sscanf(arch, "cpu:%d", &k) == 1)
		{
			/* For combined CPU workers */
			if ((k < 1) || (k > STARPU_NMAXCPUS))
			{
				fprintf(stderr, "Invalid CPU size\n");
				exit(-1);
			}

			display_perf_model(gnuplot_file, model, STARPU_CPU_DEFAULT + k - 1, &first);
			return;
		}

		if (strcmp(arch, "cuda") == 0) {
			unsigned archid;
			for (archid = STARPU_CUDA_DEFAULT; archid < STARPU_CUDA_DEFAULT + STARPU_MAXCUDADEVS; archid++)
			{
				char archname[32];
				starpu_perfmodel_get_arch_name(archid, archname, 32);
				display_perf_model(gnuplot_file, model, archid, &first);
			}
			return;
		}

		/* There must be a cleaner way ! */
		int gpuid;
		int nmatched;
		nmatched = sscanf(arch, "cuda_%d", &gpuid);
		if (nmatched == 1)
		{
			unsigned archid = STARPU_CUDA_DEFAULT+ gpuid;
			display_perf_model(gnuplot_file, model, archid, &first);
			return;
		}

		if (strcmp(arch, "gordon") == 0) {
			display_perf_model(gnuplot_file, model, STARPU_GORDON_DEFAULT, &first);
			return;
		}

		fprintf(stderr, "Unknown architecture requested, aborting.\n");
		exit(-1);
	}
}

int main(int argc, char **argv)
{
	int ret;

#ifdef __MINGW32__
	WSADATA wsadata;
	WSAStartup(MAKEWORD(1,0), &wsadata);
#endif

	parse_args(argc, argv);

	/* We need at least a symbol name */
	if (!symbol)
	{
		fprintf(stderr, "No symbol was specified\n");
		return 1;
	}

	/* Load the performance model associated to the symbol */
	ret = starpu_load_history_debug(symbol, &model);
	if (ret == 1)
	{
		fprintf(stderr, "The performance model could not be loaded\n");
		return 1;
	}

	/* If some FxT input was specified, we put the points on the graph */
#ifdef STARPU_USE_FXT
	if (!no_fxt_file)
	{
		starpu_fxt_generate_trace(&options);

		snprintf(data_file_name, 256, "starpu_%s.data", symbol);

		FILE *data_file = fopen(data_file_name, "w+");
		STARPU_ASSERT(data_file);
		dump_data_file(data_file);
		fclose(data_file);
	}
#endif

	snprintf(gnuplot_file_name, 256, "starpu_%s.gp", symbol);

	FILE *gnuplot_file = fopen(gnuplot_file_name, "w+");
	STARPU_ASSERT(gnuplot_file);
	display_selected_models(gnuplot_file, &model);
	fclose(gnuplot_file);

	/* Retrieve the current mode of the gnuplot executable */
	struct stat sb;
	ret = stat(gnuplot_file_name, &sb);
	if (ret)
	{
		perror("stat");
		STARPU_ABORT();
	}

	/* Make the gnuplot scrit executable for the owner */
	ret = chmod(gnuplot_file_name, sb.st_mode|S_IXUSR);
	if (ret)
	{
		perror("chmod");
		STARPU_ABORT();
	}

	return 0;
}
