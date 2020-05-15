/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009-2020  Université de Bordeaux, CNRS (LaBRI UMR 5800), Inria
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
 * Program to be executed with trace recording to watch the impact of
 * computations (or task polling) on communications.
 */
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <starpu_mpi.h>
#include <starpu_fxt.h>

#include "helper.h"
#include "gemm_helper.h"
#include "burst_helper.h"

void parse_args(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-nblocks") == 0)
		{
			char *argptr;
			nslices = strtol(argv[++i], &argptr, 10);
			matrix_dim = 320 * nslices;
		}
		else if (strcmp(argv[i], "-size") == 0)
		{
			char *argptr;
			unsigned matrix_dim_tmp = strtol(argv[++i], &argptr, 10);
			if (matrix_dim_tmp % 320 != 0)
			{
				fprintf(stderr, "Matrix size has to be a multiple of 320\n");
			}
			else
			{
				matrix_dim = matrix_dim_tmp;
				nslices = matrix_dim / 320;
			}
		}
		else if (strcmp(argv[i], "-check") == 0)
		{
			check = 1;
		}
		else if (strcmp(argv[i], "-nreqs") == 0)
		{
			burst_nb_requests = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
			fprintf(stderr,"Usage: %s [-nblocks n] [-size size] [-check] [-nreqs nreqs]\n", argv[0]);
			fprintf(stderr,"Currently selected: matrix size: %u - %u blocks - %d requests in each burst\n", matrix_dim, nslices, burst_nb_requests);
			exit(EXIT_SUCCESS);
		}
		else
		{
			fprintf(stderr,"Unrecognized option %s\n", argv[i]);
			exit(EXIT_FAILURE);
		}
	}
}

int main(int argc, char **argv)
{
	int ret, mpi_init, worldsize, mpi_rank;

	parse_args(argc, argv);

	MPI_INIT_THREAD(&argc, &argv, MPI_THREAD_SERIALIZED, &mpi_init);
	ret = starpu_mpi_init_conf(&argc, &argv, mpi_init, MPI_COMM_WORLD, NULL);
	if (ret == -ENODEV)
		return 77;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_mpi_init_conf");

	starpu_mpi_comm_rank(MPI_COMM_WORLD, &mpi_rank);
	starpu_mpi_comm_size(MPI_COMM_WORLD, &worldsize);

	if (worldsize < 2)
	{
		if (mpi_rank == 0)
			FPRINTF(stderr, "We need 2 processes.\n");

		starpu_mpi_shutdown();
		if (!mpi_init)
			MPI_Finalize();
		return STARPU_TEST_SKIPPED;
	}

	gemm_alloc_data();
	if (gemm_init_data() == -ENODEV)
		goto enodev;

	burst_init_data(mpi_rank);

	/* Wait for everything and everybody: */
	starpu_task_wait_for_all();
	starpu_mpi_barrier(MPI_COMM_WORLD);


	FPRINTF(stderr, "** Burst warmup **\n");
	burst_all(mpi_rank);

	starpu_sleep(0.3); // sleep to easily distinguish different bursts in traces

	FPRINTF(stderr, "** Burst while there is no task available, but workers are polling **\n");
	burst_all(mpi_rank);

	starpu_sleep(0.3); // sleep to easily distinguish different bursts in traces

	FPRINTF(stderr, "** Burst while there is no task available, workers are paused **\n");
	starpu_pause();
	burst_all(mpi_rank);

	starpu_sleep(0.3); // sleep to easily distinguish different bursts in traces

	FPRINTF(stderr, "** Burst while workers are really working **\n");
	if(gemm_submit_tasks() == -ENODEV)
		goto enodev;
	starpu_resume();

	burst_all(mpi_rank);

	FPRINTF(stderr, "Burst done, now waiting for computing tasks to finish\n");

	/* Wait for everything and everybody: */
	starpu_task_wait_for_all();
	starpu_mpi_barrier(MPI_COMM_WORLD);

enodev:
	gemm_release();
	burst_free_data(mpi_rank);

	starpu_mpi_shutdown();
	if (!mpi_init)
		MPI_Finalize();

	return ret;
}