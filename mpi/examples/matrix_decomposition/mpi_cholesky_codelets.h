/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009, 2010  Université de Bordeaux 1
 * Copyright (C) 2010, 2011, 2012, 2013  Centre National de la Recherche Scientifique
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

#ifndef __MPI_CHOLESKY_CODELETS_H__
#define __MPI_CHOLESKY_CODELETS_H__


/* Returns the MPI node number where data indexes index is */
int my_distrib(int x, int y, int nb_nodes);

/*
 *	code to bootstrap the factorization
 *	and construct the DAG
 */
void dw_cholesky(float ***matA, unsigned size, unsigned ld, unsigned nblocks, int rank, int nodes, double *timing, double *flops);

void dw_cholesky_check_computation(float ***matA, unsigned size, int rank, int nodes, int *correctness, double *flops);

#endif /* __MPI_CHOLESKY_CODELETS_H__ */
