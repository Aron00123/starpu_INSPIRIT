/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2011  Institut National de Recherche en Informatique et Automatique
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

#undef NDEBUG
#include <assert.h>

#include <starpu.h>
#include <stdlib.h>
#include <sys/types.h>

static void task(void **buffers, void *args)
{
	float *numbers;
	size_t size, i;

	numbers = (float *) STARPU_VECTOR_GET_PTR(buffers[0]);
	starpu_unpack_cl_args (args, &size);
	for(i = 0; i < size; i++)
	{
		numbers[i] = i;
	}
}

static starpu_codelet cl = {
	.where = STARPU_CPU,
	.cpu_func = task,
	.nbuffers = 1
};

static void test_lazy_allocation()
{
	static const size_t count = 123;

	size_t i;
	void *pointer;
	starpu_data_handle handle;

	/* Lazily-allocated vector.  */
	starpu_vector_data_register(&handle, -1, 0 /* NULL */,
				    count, sizeof(float));

	starpu_insert_task(&cl,
			   STARPU_W, handle,
			   STARPU_VALUE, &count, sizeof(size_t),
			   0);

	/* Acquire the handle, forcing a local allocation.  */
	starpu_data_acquire(handle, STARPU_R);

	/* Make sure we have a local pointer to it.  */
	pointer = starpu_handle_get_local_ptr(handle);
	assert(pointer != NULL);
	for(i = 0; i < count; i++)
	{
		float *numbers = (float *)pointer;
		assert(numbers[i] == i);
	}

	/* Make sure the pointer/handle mapping is up-to-date.  */
	assert(starpu_data_lookup(pointer) == handle);

	starpu_data_release(handle);
	starpu_data_unregister(handle);

	assert(starpu_data_lookup(pointer) == NULL);
}

#define VECTOR_COUNT    12
#define VARIABLE_COUNT  42

#define VECTOR_SIZE     123

static void test_filters()
{
#define CHILDREN_COUNT 10
	int err, i;
	void *ptr, *children_pointers[CHILDREN_COUNT];
	starpu_data_handle handle;

	err = starpu_malloc(&ptr, VECTOR_SIZE * sizeof(*ptr));
	assert(err == 0);

	starpu_vector_data_register(&handle, 0, (uintptr_t)ptr,
				    VECTOR_SIZE, sizeof(*ptr));

	struct starpu_data_filter f =
	{
		.filter_func = starpu_block_filter_func_vector,
		.nchildren = CHILDREN_COUNT,
		.get_nchildren = NULL,
		.get_child_ops = NULL
	};
	starpu_data_partition(handle, &f);
	assert(starpu_data_get_nb_children(handle) == CHILDREN_COUNT);

	for (i = 0; i < CHILDREN_COUNT; i++)
	{
                starpu_data_handle child;

		child = starpu_data_get_sub_data(handle, 1, i);
		children_pointers[i] = starpu_handle_get_local_ptr(child);
		assert(children_pointers[i] != NULL);

		/* Make sure we have a pointer -> handle mapping for CHILD.  */
		assert(starpu_data_lookup(children_pointers[i]) == child);
	}

	starpu_data_unpartition(handle, 0);

	for (i = 0; i < CHILDREN_COUNT; i++)
	{
		if (children_pointers[i] != ptr)
			/* Make sure the pointer -> handle mapping is gone.  */
			assert(starpu_data_lookup(children_pointers[i]) == NULL);
	}

	/* Make sure the parent's mapping is back.  */
	assert(starpu_data_lookup(ptr) == handle);

	starpu_data_unregister(handle);

#undef CHILDREN_COUNT
}

int main(int argc, char *argv[])
{
	int err;
	size_t i;
	void *vectors[VECTOR_COUNT], *variables[VARIABLE_COUNT];
	starpu_data_handle vector_handles[VECTOR_COUNT];
	starpu_data_handle variable_handles[VARIABLE_COUNT];

	starpu_init(NULL);

	/* Register data regions.  */

	for(i = 0; i < VARIABLE_COUNT; i++)
	{
		err = starpu_malloc(&variables[i], sizeof(float));
		assert(err == 0);
		starpu_variable_data_register(&variable_handles[i], 0,
					      (uintptr_t)variables[i],
					      sizeof(float));
	}

	for(i = 0; i < VECTOR_COUNT; i++)
	{
		err = starpu_malloc(&vectors[i], VECTOR_SIZE * sizeof(float));
		assert(err == 0);
		starpu_vector_data_register(&vector_handles[i], 0,
					    (uintptr_t)vectors[i],
					    VECTOR_SIZE, sizeof(float));
	}

	/* Look them up.  */

	for(i = 0; i < VARIABLE_COUNT; i++)
	{
		starpu_data_handle handle;

		handle = starpu_data_lookup(variables[i]);
		assert(handle == variable_handles[i]);
	}

	for(i = 0; i < VECTOR_COUNT; i++)
	{
		starpu_data_handle handle;

		handle = starpu_data_lookup(vectors[i]);
		assert(handle == vector_handles[i]);
	}

	/* Unregister them.  */

	for(i = 0; i < VARIABLE_COUNT; i++)
	{
		starpu_data_unregister(variable_handles[i]);
	}

	for(i = 0; i < VECTOR_COUNT; i++)
	{
		starpu_data_unregister(vector_handles[i]);
	}

	/* Make sure we can no longer find them.  */

	for(i = 0; i < VARIABLE_COUNT; i++)
	{
		starpu_data_handle handle;

		handle = starpu_data_lookup(variables[i]);
		assert(handle == NULL);
		starpu_free(variables[i]);
	}

	for(i = 0; i < VECTOR_COUNT; i++)
	{
		starpu_data_handle handle;

		handle = starpu_data_lookup(vectors[i]);
		assert(handle == NULL);
		starpu_free(vectors[i]);
	}

	test_lazy_allocation();
	test_filters();

	starpu_shutdown();

	return EXIT_SUCCESS;
}
