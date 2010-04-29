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

#include <starpu.h>
#include <starpu_opencl.h>

void opencl_codelet(void *descr[], void *_args)
{
	float *val = (float *)STARPU_GET_VECTOR_PTR(descr[0]);
	cl_kernel kernel;
	cl_command_queue queue;
	int id, devid, err;

        id = starpu_get_worker_id();
        devid = starpu_get_worker_devid(id);

	err = starpu_opencl_load_kernel(&kernel, &queue,
                                        "examples/variable/variable_kernels_opencl_codelet.cl", "variable", devid);
	if (err != CL_SUCCESS) STARPU_OPENCL_REPORT_ERROR(err);

	err = 0;
	err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &val);
	if (err) STARPU_OPENCL_REPORT_ERROR(err);

	{
		size_t global=1;
		size_t local=1;
		err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
		if (err != CL_SUCCESS) STARPU_OPENCL_REPORT_ERROR(err);
	}

	clFinish(queue);

	starpu_opencl_release(kernel);
}
