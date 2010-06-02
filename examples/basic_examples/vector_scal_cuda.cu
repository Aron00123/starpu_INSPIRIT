/*
 * StarPU
 * Copyright (C) INRIA 2008-2009 (see AUTHORS file)
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

#include <starpu.h>

static __global__ void vector_mult_cuda(float *val, unsigned n,
                                        float factor)
{
        unsigned i;
        for(i = 0 ; i < n ; i++)
               val[i] *= factor;
}

extern "C" void scal_cuda_func(void *buffers[], void *_args)
{
        float *factor = (float *)_args;
        struct starpu_vector_interface_s *vector = (struct starpu_vector_interface_s *) buffers[0];

        /* length of the vector */
        unsigned n = STARPU_GET_VECTOR_NX(vector);
        /* local copy of the vector pointer */
        float *val = (float *)STARPU_GET_VECTOR_PTR(vector);

        /* TODO: use more blocks and threads in blocks */
        vector_mult_cuda<<<1,1>>>(val, n, *factor);

	cudaThreadSynchronize();
}
