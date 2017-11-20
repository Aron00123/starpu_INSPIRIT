/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2011, 2012, 2013, 2014, 2017  CNRS
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

//#include "fxt_tool.h"

#include <search.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <common/fxt.h>
#include <common/list.h>
#include <starpu.h>

static fxt_t fut;
struct fxt_ev_64 ev;

static uint64_t transfers[16][16];

#define PROGNAME "starpu_fxt_stat"

static void usage()
{
	fprintf(stderr, "Parse the log generated by FxT\n\n");
	fprintf(stderr, "Usage: %s [ options ]\n", PROGNAME);
        fprintf(stderr, "\n");
        fprintf(stderr, "Options:\n");
	fprintf(stderr, "   -i <input file>     specify the input file.\n");
        fprintf(stderr, "   -o <output file>    specify the output file\n");
	fprintf(stderr, "   -h, --help          display this help and exit\n");
	fprintf(stderr, "   -v, --version       output version information and exit\n\n");
        fprintf(stderr, "Report bugs to <%s>.", PACKAGE_BUGREPORT);
        fprintf(stderr, "\n");
}

static int parse_args(int argc, char **argv, char **fin, char **fout)
{
	int i;

	*fin = NULL;
	*fout = NULL;
	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-o") == 0)
		{
			*fout = argv[++i];
			continue;
		}

		if (strcmp(argv[i], "-i") == 0)
		{
			*fin = argv[++i];
			continue;
		}

		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
		{
			usage();
			return EXIT_SUCCESS;
		}

		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
		{
		        fputs(PROGNAME " (" PACKAGE_NAME ") " PACKAGE_VERSION "\n", stderr);
			return EXIT_SUCCESS;
		}
	}

	if (!*fin)
	{
		fprintf(stderr, "Incorrect usage, aborting\n");
                usage();
		return 77;
	}
	return 0;
}

static void handle_data_copy(void)
{
	unsigned src = ev.param[0];
	unsigned dst = ev.param[1];
	unsigned size = ev.param[2];

	transfers[src][dst] += size;

//	printf("transfer %d -> %d : %d \n", src, dst, size);
}

/*
 * This program should be used to parse the log generated by FxT
 */
int main(int argc, char **argv)
{
	char *fin, *fout;
	int ret;
	int fd_in;
	FILE *fd_out;

	ret = parse_args(argc, argv, &fin, &fout);
	if (ret) return ret;

	fd_in = open(fin, O_RDONLY);
	if (fd_in < 0)
	{
	        perror("open failed :");
	        exit(-1);
	}

	fut = fxt_fdopen(fd_in);
	if (!fut)
	{
	        perror("fxt_fdopen :");
	        exit(-1);
	}

	if (!fout)
	{
		fd_out = stdout;
	}
	else
	{
		fd_out = fopen(fout, "w");
		if (fd_out == NULL)
		{
			perror("open failed :");
			exit(-1);
		}
	}

	fxt_blockev_t block;
	block = fxt_blockev_enter(fut);

	unsigned njob = 0;
	unsigned nws = 0;

	double start_time = 10e30;
	double end_time = -10e30;

	while(1)
	{
		ret = fxt_next_ev(block, FXT_EV_TYPE_64, (struct fxt_ev *)&ev);
		if (ret != FXT_EV_OK)
		{
			fprintf(stderr, "no more block ...\n");
			break;
		}

		end_time = STARPU_MAX(end_time, ev.time);
		start_time = STARPU_MIN(start_time, ev.time);

		STARPU_ATTRIBUTE_UNUSED int nbparam = ev.nb_params;

		switch (ev.code)
		{
			case _STARPU_FUT_DATA_COPY:
				handle_data_copy();
				break;
			case _STARPU_FUT_JOB_POP:
				njob++;
				break;
			case _STARPU_FUT_WORK_STEALING:
				nws++;
				break;
			default:
				break;
		}
	}

	fprintf(fd_out, "Start : start time %e end time %e length %e\n", start_time, end_time, end_time - start_time);

	unsigned src, dst;
	for (src = 0; src < 16; src++)
	{
		for (dst = 0; dst < 16; dst++)
		{
			if (transfers[src][dst] != 0)
			{
				fprintf(fd_out, "%u -> %u \t %lu MB\n", src, dst, (unsigned long)(transfers[src][dst]/(1024*1024)));
			}
		}
	}

	fprintf(fd_out, "There was %u tasks and %u work stealing\n", njob, nws);
	if (fd_out != stdout)
		fclose(fd_out);

	return 0;
}
