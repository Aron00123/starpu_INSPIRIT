/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013 Corentin Salingue
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

/* Try to write into disk memory
 * Use mechanism to push datas from main ram to disk ram
 */

#include <starpu.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <common/config.h>
#include "../helper.h"

#ifdef STARPU_HAVE_WINDOWS
#if defined(_WIN32) && !defined(__CYGWIN__)
#define mkdir(path, mode) mkdir(path)
#endif
#endif

/* size of one vector */
#if SIZEOF_VOID_P == 4
#define	NX	(32*1024/sizeof(double))
#else
#define	NX	(32*1048576/sizeof(double))
#endif

#if !defined(STARPU_HAVE_SETENV)
#warning setenv is not defined. Skipping test
int main(int argc, char **argv)
{
	return STARPU_TEST_SKIPPED;
}
#else

int dotest(struct starpu_disk_ops *ops, void *param)
{
	double *A,*B,*C,*D,*E,*F;
	int ret;

	/* limit main ram to force to push in disk */
	setenv("STARPU_LIMIT_CPU_MEM", "160", 1);

	/* Initialize StarPU without GPU devices to make sure the memory of the GPU devices will not be used */
	struct starpu_conf conf;
	ret = starpu_conf_init(&conf);
	if (ret == -EINVAL)
		return EXIT_FAILURE;
	conf.ncuda = 0;
	conf.nopencl = 0;
	ret = starpu_init(&conf);
	if (ret == -ENODEV) goto enodev;

	/* register a disk */
	int new_dd = starpu_disk_register(ops, param, 1024*1024*200);
	/* can't write on /tmp/ */
	if (new_dd == -ENOENT) goto enoent;

	unsigned dd = (unsigned) new_dd;

	/* allocate two memory spaces */
	starpu_malloc_flags((void **)&A, NX*sizeof(double), STARPU_MALLOC_COUNT);
	starpu_malloc_flags((void **)&F, NX*sizeof(double), STARPU_MALLOC_COUNT);

	FPRINTF(stderr, "TEST DISK MEMORY \n");

	unsigned int j;
	/* initialization with bad values */
	for(j = 0; j < NX; ++j)
	{
		A[j] = j;
		F[j] = -j;
	}

	starpu_data_handle_t vector_handleA, vector_handleB, vector_handleC, vector_handleD, vector_handleE, vector_handleF;

	/* register vector in starpu */
	starpu_vector_data_register(&vector_handleA, STARPU_MAIN_RAM, (uintptr_t)A, NX, sizeof(double));
	starpu_vector_data_register(&vector_handleB, -1, (uintptr_t) NULL, NX, sizeof(double));
	starpu_vector_data_register(&vector_handleC, -1, (uintptr_t) NULL, NX, sizeof(double));
	starpu_vector_data_register(&vector_handleD, -1, (uintptr_t) NULL, NX, sizeof(double));
	starpu_vector_data_register(&vector_handleE, -1, (uintptr_t) NULL, NX, sizeof(double));
	starpu_vector_data_register(&vector_handleF, STARPU_MAIN_RAM, (uintptr_t)F, NX, sizeof(double));

	/* copy vector A->B, B->C... */
	starpu_data_cpy(vector_handleB, vector_handleA, 0, NULL, NULL);
	starpu_data_cpy(vector_handleC, vector_handleB, 0, NULL, NULL);
	starpu_data_cpy(vector_handleD, vector_handleC, 0, NULL, NULL);
	starpu_data_cpy(vector_handleE, vector_handleD, 0, NULL, NULL);
	starpu_data_cpy(vector_handleF, vector_handleE, 0, NULL, NULL);

	/* StarPU does not need to manipulate the array anymore so we can stop
 	 * monitoring it */

	/* free them */
	starpu_data_unregister(vector_handleA);
	starpu_data_unregister(vector_handleB);
	starpu_data_unregister(vector_handleC);
	starpu_data_unregister(vector_handleD);
	starpu_data_unregister(vector_handleE);
	starpu_data_unregister(vector_handleF);

	/* check if computation is correct */
	int try = 1;
	for (j = 0; j < NX; ++j)
		if (A[j] != F[j])
		{
			FPRINTF(stderr, "Fail A %f != F %f \n", A[j], F[j]);
			try = 0;
		}

	starpu_free_flags(A, NX*sizeof(double), STARPU_MALLOC_COUNT);
	starpu_free_flags(F, NX*sizeof(double), STARPU_MALLOC_COUNT);

	/* terminate StarPU, no task can be submitted after */
	starpu_shutdown();

	if(try)
		FPRINTF(stderr, "TEST SUCCESS\n");
	else
		FPRINTF(stderr, "TEST FAIL\n");
	return (try ? EXIT_SUCCESS : EXIT_FAILURE);

enodev:
	return STARPU_TEST_SKIPPED;
enoent:
	FPRINTF(stderr, "Couldn't write data: ENOENT\n");
	starpu_shutdown();
	return STARPU_TEST_SKIPPED;
}

static int merge_result(int old, int new)
{
	if (new == EXIT_FAILURE)
		return EXIT_FAILURE;
	if (old == 0)
		return 0;
	return new;
}

int main(void)
{
	int ret = 0;
	char s[128];
	snprintf(s, sizeof(s), "/tmp/%s-disk", getenv("USER"));
	mkdir(s, 0777);
	ret = merge_result(ret, dotest(&starpu_disk_stdio_ops, s));
	ret = merge_result(ret, dotest(&starpu_disk_unistd_ops, s));
#ifdef STARPU_LINUX_SYS
	ret = merge_result(ret, dotest(&starpu_disk_unistd_o_direct_ops, s));
#endif
	return ret;
}
#endif
