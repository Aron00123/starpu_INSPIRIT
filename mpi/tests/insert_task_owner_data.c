/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2011  Centre National de la Recherche Scientifique
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

#include <starpu_mpi.h>
#include <starpu_mpi_datatype.h>
#include <math.h>

void func_cpu(void *descr[], __attribute__ ((unused)) void *_args)
{
	int *x0 = (int *)STARPU_VARIABLE_GET_PTR(descr[0]);
	int *x1 = (int *)STARPU_VARIABLE_GET_PTR(descr[1]);

	*x0 += 1;
	*x1 *= *x1;
}

starpu_codelet mycodelet = {
	.where = STARPU_CPU,
	.cpu_func = func_cpu,
        .nbuffers = 2
};

int main(int argc, char **argv)
{
        int rank, size, err;
        int x[2];
        int i;
        starpu_data_handle_t data_handles[2];
	int values[2];

	starpu_init(NULL);
	starpu_mpi_initialize_extended(&rank, &size);

        if (rank > 1) {
                starpu_mpi_shutdown();
                starpu_shutdown();
                return 0;
        }

        if (rank == 0) {
		x[0] = 11;
		starpu_variable_data_register(&data_handles[0], 0, (uintptr_t)&x[0], sizeof(x[0]));
		starpu_data_set_rank(data_handles[0], 0);
		starpu_data_set_tag(data_handles[0], 0);
		starpu_variable_data_register(&data_handles[1], -1, (uintptr_t)NULL, sizeof(x[1]));
		starpu_data_set_rank(data_handles[1], 1);
		starpu_data_set_tag(data_handles[1],10);
        }
        else if (rank == 1) {
		x[1] = 12;
		starpu_variable_data_register(&data_handles[0], -1, (uintptr_t)NULL, sizeof(x[0]));
		starpu_data_set_rank(data_handles[0], 0);
		starpu_data_set_tag(data_handles[0], 0);
		starpu_variable_data_register(&data_handles[1], 0, (uintptr_t)&x[1], sizeof(x[1]));
		starpu_data_set_rank(data_handles[1], 1);
		starpu_data_set_tag(data_handles[1], 1);
        }

        err = starpu_mpi_insert_task(MPI_COMM_WORLD, &mycodelet,
                                     STARPU_RW, data_handles[0], STARPU_RW, data_handles[1],
                                     STARPU_EXECUTE_ON_DATA, data_handles[1],
				     0);
        assert(err == 0);
        starpu_task_wait_for_all();

        for(i=0 ; i<2 ; i++) {
                starpu_mpi_get_data_on_node(MPI_COMM_WORLD, data_handles[i], 0);
                starpu_data_acquire(data_handles[i], STARPU_R);
                values[i] = *((int *)starpu_mpi_handle_to_ptr(data_handles[i]));
        }
	assert(values[0] == 12 && values[1] == 144);
        fprintf(stderr, "[%d][local ptr] VALUES: %d %d\n", rank, values[0], values[1]);

	starpu_mpi_shutdown();
	starpu_shutdown();

	return 0;
}

