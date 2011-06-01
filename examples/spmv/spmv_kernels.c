/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009, 2010, 2011  Université de Bordeaux 1
 * Copyright (C) 2010  Mehdi Juhoor <mjuhoor@gmail.com>
 * Copyright (C) 2010, 2011  Centre National de la Recherche Scientifique
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

#include "spmv.h"

#ifdef STARPU_USE_OPENCL
struct starpu_opencl_program opencl_codelet;

void spmv_kernel_opencl(void *descr[], void *args)
{
	cl_kernel kernel;
	cl_command_queue queue;
	cl_event event;
	int id, devid, err, n;

	uint32_t nnz = STARPU_CSR_GET_NNZ(descr[0]);
	uint32_t nrow = STARPU_CSR_GET_NROW(descr[0]);
	cl_mem nzval = (cl_mem)STARPU_CSR_GET_NZVAL(descr[0]);
	uint32_t *colind = STARPU_CSR_GET_COLIND(descr[0]);
	uint32_t *rowptr = STARPU_CSR_GET_ROWPTR(descr[0]);
	uint32_t firstentry = STARPU_CSR_GET_FIRSTENTRY(descr[0]);

	cl_mem vecin = (cl_mem)STARPU_VECTOR_GET_PTR(descr[1]);
	uint32_t nx_in = STARPU_VECTOR_GET_NX(descr[1]);

	cl_mem vecout = (cl_mem)STARPU_VECTOR_GET_PTR(descr[2]);
	uint32_t nx_out = STARPU_VECTOR_GET_NX(descr[2]);

        id = starpu_worker_get_id();
        devid = starpu_worker_get_devid(id);

        err = starpu_opencl_load_kernel(&kernel, &queue, &opencl_codelet, "spmv", devid);
        if (err != CL_SUCCESS) STARPU_OPENCL_REPORT_ERROR(err);

	err = 0;
        n=0;
	err = clSetKernelArg(kernel, n++, sizeof(nnz), &nnz);
	err = clSetKernelArg(kernel, n++, sizeof(nrow), &nrow);
	err = clSetKernelArg(kernel, n++, sizeof(nzval), &nzval);
	err = clSetKernelArg(kernel, n++, sizeof(colind), &colind);
	err = clSetKernelArg(kernel, n++, sizeof(rowptr), &rowptr);
	err = clSetKernelArg(kernel, n++, sizeof(firstentry), &firstentry);
	err = clSetKernelArg(kernel, n++, sizeof(vecin), &vecin);
	err = clSetKernelArg(kernel, n++, sizeof(nx_in), &nx_in);
	err = clSetKernelArg(kernel, n++, sizeof(vecout), &vecout);
	err = clSetKernelArg(kernel, n++, sizeof(nx_out), &nx_out);
        if (err) STARPU_OPENCL_REPORT_ERROR(err);

	{
                size_t global=1024;
		err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, NULL, 0, NULL, &event);
		if (err != CL_SUCCESS) STARPU_OPENCL_REPORT_ERROR(err);
	}

	clFinish(queue);
	starpu_opencl_collect_stats(event);
	clReleaseEvent(event);

        starpu_opencl_release_kernel(kernel);
}

void compile_spmv_opencl_kernel(void)
{
	int ret;
	ret = starpu_opencl_load_opencl_from_file("examples/spmv/spmv_opencl.cl", &opencl_codelet, NULL);
	if (ret)
	{
		FPRINTF(stderr, "Failed to compile OpenCL codelet\n");
		exit(ret);
	}
}
#endif

void spmv_kernel_cpu(void *descr[], __attribute__((unused))  void *arg)
{
	float *nzval = (float *)STARPU_CSR_GET_NZVAL(descr[0]);
	uint32_t *colind = STARPU_CSR_GET_COLIND(descr[0]);
	uint32_t *rowptr = STARPU_CSR_GET_ROWPTR(descr[0]);

	float *vecin = (float *)STARPU_VECTOR_GET_PTR(descr[1]);
	float *vecout = (float *)STARPU_VECTOR_GET_PTR(descr[2]);

	uint32_t firstelem = STARPU_CSR_GET_FIRSTENTRY(descr[0]);

	uint32_t nrow;

	nrow = STARPU_CSR_GET_NROW(descr[0]);

	STARPU_ASSERT(nrow == STARPU_VECTOR_GET_NX(descr[2]));

	unsigned row;
	for (row = 0; row < nrow; row++)
	{
		float tmp = 0.0f;
		unsigned index;

		unsigned firstindex = rowptr[row] - firstelem;
		unsigned lastindex = rowptr[row+1] - firstelem;

		for (index = firstindex; index < lastindex; index++)
		{
			unsigned col;

			col = colind[index];
			tmp += nzval[index]*vecin[col];
		}

		vecout[row] = tmp;
	}

}


