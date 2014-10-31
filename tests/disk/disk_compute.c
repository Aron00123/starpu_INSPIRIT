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

#include <fcntl.h>
#include <starpu.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <common/config.h>
#include "../helper.h"

#ifdef STARPU_HAVE_WINDOWS
        #include <io.h>
#if defined(_WIN32) && !defined(__CYGWIN__)
#define mkdir(path, mode) mkdir(path)
#endif
#endif

#define NX (1024)

int dotest(struct starpu_disk_ops *ops, char *base)
{
	int *A, *C;

	/* Initialize StarPU with default configuration */
	int ret = starpu_init(NULL);

	if (ret == -ENODEV) goto enodev;

	/* Initialize path and name */
	char pid_str[16];
	int pid = getpid();
	snprintf(pid_str, 16, "%d", pid);

	const char *name_file_start = "STARPU_DISK_COMPUTE_DATA_";
	const char *name_file_end = "STARPU_DISK_COMPUTE_DATA_RESULT_";

	char * path_file_start = malloc(strlen(base) + 1 + strlen(name_file_start) + 1);
	strcpy(path_file_start, base);
	strcat(path_file_start, "/");
	strcat(path_file_start, name_file_start);

	char * path_file_end = malloc(strlen(base) + 1 + strlen(name_file_end) + 1);
	strcpy(path_file_end, base);
	strcat(path_file_end, "/");
	strcat(path_file_end, name_file_end);

	/* register a disk */
	int new_dd = starpu_disk_register(ops, (void *) base, 1024*1024*1);
	/* can't write on /tmp/ */
	if (new_dd == -ENOENT) goto enoent;

	unsigned dd = (unsigned) new_dd;

	/* allocate two memory spaces */
	starpu_malloc_flags((void **)&A, NX*sizeof(int), STARPU_MALLOC_COUNT);
	starpu_malloc_flags((void **)&C, NX*sizeof(int), STARPU_MALLOC_COUNT);

	FPRINTF(stderr, "TEST DISK MEMORY \n");

	unsigned int j;
	/* you register them in a vector */
	for(j = 0; j < NX; ++j)
	{
		A[j] = j;
		C[j] = 0;
	}

	/* you create a file to store the vector ON the disk */
	FILE * f = fopen(path_file_start, "wb+");
	if (f == NULL)
		goto enoent2;

	/* store it in the file */
	fwrite(A, sizeof(int), NX, f);

	/* close the file */
	fclose(f);

	int descriptor = open(path_file_start, O_RDWR);
#ifdef STARPU_HAVE_WINDOWS
	_commit(descriptor);
#else
	fsync(descriptor);
#endif
	close(descriptor);

	/* create a file to store result */
	f = fopen(path_file_end, "wb+");
	if (f == NULL)
		goto enoent;

	/* replace all datas by 0 */
	fwrite(C, sizeof(int), NX, f);

	/* close the file */
	fclose(f);

        descriptor = open(path_file_end, O_RDWR);
#ifdef STARPU_HAVE_WINDOWS
        _commit(descriptor);
#else
        fsync(descriptor);
#endif
	close(descriptor);

	/* And now, you want to use your datas in StarPU */
	/* Open the file ON the disk */
	void * data = starpu_disk_open(dd, (void *) name_file_start, NX*sizeof(int));
	void * data_result = starpu_disk_open(dd, (void *) name_file_end, NX*sizeof(int));

	starpu_data_handle_t vector_handleA, vector_handleC;

	/* register vector in starpu */
	starpu_vector_data_register(&vector_handleA, dd, (uintptr_t) data, NX, sizeof(int));

	/* and do what you want with it, here we copy it into an other vector */
	starpu_vector_data_register(&vector_handleC, dd, (uintptr_t) data_result, NX, sizeof(int));

	starpu_data_cpy(vector_handleC, vector_handleA, 0, NULL, NULL);

	/* free them */
	starpu_data_unregister(vector_handleA);
	starpu_data_unregister(vector_handleC);

	/* close them in StarPU */
	starpu_disk_close(dd, data, NX*sizeof(int));
	starpu_disk_close(dd, data_result, NX*sizeof(int));

	/* check results */
	f = fopen(path_file_end, "rb+");
	if (f == NULL)
		goto enoent;
	/* take datas */
	int size = fread(C, sizeof(int), NX, f);

	/* close the file */
	fclose(f);

	int try = 1;
	for (j = 0; j < NX; ++j)
		if (A[j] != C[j])
		{
			FPRINTF(stderr, "Fail A %d != C %d \n", A[j], C[j]);
			try = 0;
		}

	starpu_free_flags(A, NX*sizeof(int), STARPU_MALLOC_COUNT);
	starpu_free_flags(C, NX*sizeof(int), STARPU_MALLOC_COUNT);

	unlink(path_file_start);
	unlink(path_file_end);

	free(path_file_start);
	free(path_file_end);

	/* terminate StarPU, no task can be submitted after */
	starpu_shutdown();

	if(try)
		FPRINTF(stderr, "TEST SUCCESS\n");
	else
		FPRINTF(stderr, "TEST FAIL\n");
	return (try ? EXIT_SUCCESS : EXIT_FAILURE);

enodev:
	return STARPU_TEST_SKIPPED;
enoent2:
	starpu_free_flags(A, NX*sizeof(int), STARPU_MALLOC_COUNT);
	starpu_free_flags(C, NX*sizeof(int), STARPU_MALLOC_COUNT);
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
