/*
 * StarPU
 * Copyright (C) Université Bordeaux 1, CNRS 2008-2010 (see AUTHORS file)
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

#include <starpu_mpi.h>

#define NITER	2048

int main(int argc, char **argv)
{
	int rank, size;
        unsigned send[2] = {42, 11};
        unsigned recv[2] = {33, 33};
        starpu_mpi_req req[2];
        starpu_data_handle send_handle[2];
        starpu_data_handle recv_handle[2];

	starpu_init(NULL);
	starpu_mpi_initialize_extended(1, &rank, &size);

	if (size < 2)
	{
		if (rank == 0)
			fprintf(stderr, "We need at least 2 processes.\n");

                starpu_mpi_shutdown();
                starpu_shutdown();
		return 0;
	}

	starpu_variable_data_register(&send_handle[0], 0, (uintptr_t)&send[0], sizeof(unsigned));
	starpu_variable_data_register(&send_handle[1], 0, (uintptr_t)&send[1], sizeof(unsigned));
	starpu_variable_data_register(&recv_handle[0], 0, (uintptr_t)&recv[0], sizeof(unsigned));
	starpu_variable_data_register(&recv_handle[1], 0, (uintptr_t)&recv[1], sizeof(unsigned));

        if (rank == 0) {
                starpu_mpi_isend(send_handle[0], &(req[0]), 1, 12, MPI_COMM_WORLD);
                starpu_mpi_isend(send_handle[1], &(req[1]), 1, 13, MPI_COMM_WORLD);
        }
        else if (rank == 1) {
                starpu_mpi_irecv(recv_handle[0], &(req[0]), 0, 12, MPI_COMM_WORLD);
                starpu_mpi_irecv(recv_handle[1], &(req[1]), 0, 13, MPI_COMM_WORLD);
        }

        if (rank == 0 || rank == 1) {
                int nb_req=2;
                while (nb_req) {
                        int r=0;
                        for(r=0 ; r<2 ; r++) {
                                if (req[r]) {
                                        int finished = 0;
                                        MPI_Status status;
                                        starpu_mpi_test(&req[r], &finished, &status);
                                        STARPU_ASSERT(finished != -1);
                                        if(finished) {
                                                fprintf(stderr, "[%d] Request %d finished\n", rank, r);
                                                req[r] = NULL;
                                                nb_req--;
                                        }
                                }
                        }
                }
        }
        fprintf(stderr, "[%d] All requests finished\n", rank);

	starpu_mpi_shutdown();
	starpu_shutdown();

	return 0;
}
