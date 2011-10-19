/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
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

#include <pthread.h>
#include <stdio.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/time.h>

static pthread_t thread[2];
static unsigned thread_is_initialized[2];

static pthread_cond_t cond;
static pthread_mutex_t mutex;

static size_t buffer_size = 4;
static void *cpu_buffer;
static void *gpu_buffer[2];

static pthread_cond_t cond_go;
static unsigned ready = 0;
static unsigned nready_gpu = 0;

static unsigned niter = 250000;

static pthread_cond_t cond_gpu;
static pthread_mutex_t mutex_gpu;
static unsigned data_is_available[2];

static cudaStream_t stream[2];

#define ASYNC	1
#define DO_TRANSFER_GPU_TO_RAM	1
#define DO_TRANSFER_RAM_TO_GPU	1

void send_data(unsigned src, unsigned dst)
{
	cudaError_t cures;

	/* Copy data from GPU to RAM */
#ifdef DO_TRANSFER_GPU_TO_RAM
#ifdef ASYNC
	cures = cudaMemcpyAsync(cpu_buffer, gpu_buffer[src], buffer_size, cudaMemcpyDeviceToHost, stream[src]);
	assert(!cures);

	cures = cudaStreamSynchronize(stream[src]);
	assert(!cures);
#else
	cures = cudaMemcpy(cpu_buffer, gpu_buffer[src], buffer_size, cudaMemcpyDeviceToHost);
	assert(!cures);

	cures = cudaThreadSynchronize();
	assert(!cures);
#endif
#endif

	/* Tell the other GPU that data is in RAM */
	PTHREAD_MUTEX_LOCK(&mutex_gpu);
	data_is_available[src] = 0;
	data_is_available[dst] = 1;
	PTHREAD_COND_SIGNAL(&cond_gpu);
	PTHREAD_MUTEX_UNLOCK(&mutex_gpu);
	//fprintf(stderr, "SEND on %d\n", src);
}

void recv_data(unsigned src, unsigned dst)
{
	cudaError_t cures;

	/* Wait for the data to be in RAM */
	PTHREAD_MUTEX_LOCK(&mutex_gpu);
	while (!data_is_available[dst])
	{
		PTHREAD_COND_WAIT(&cond_gpu, &mutex_gpu);
	}
	PTHREAD_MUTEX_UNLOCK(&mutex_gpu);
	//fprintf(stderr, "RECV on %d\n", dst);

	/* Upload data */
#ifdef DO_TRANSFER_RAM_TO_GPU
#ifdef ASYNC
	cures = cudaMemcpyAsync(gpu_buffer[dst], cpu_buffer, buffer_size, cudaMemcpyHostToDevice, stream[dst]);
	assert(!cures);
	cures = cudaThreadSynchronize();
	assert(!cures);
#else
	cures = cudaMemcpy(gpu_buffer[dst], cpu_buffer, buffer_size, cudaMemcpyHostToDevice);
	assert(!cures);

	cures = cudaThreadSynchronize();
	assert(!cures);
#endif
#endif
}

void *launch_gpu_thread(void *arg)
{
	unsigned *idptr = arg;
	unsigned id = *idptr;

	cudaSetDevice(id);
	cudaFree(0);

	cudaMalloc(&gpu_buffer[id], buffer_size);
	cudaStreamCreate(&stream[id]);

	PTHREAD_MUTEX_LOCK(&mutex);
	thread_is_initialized[id] = 1;
	PTHREAD_COND_SIGNAL(&cond);

	if (id == 0)
	{
		cudaError_t cures;
		cures = cudaHostAlloc(&cpu_buffer, buffer_size, cudaHostAllocPortable);
		assert(!cures);
		cudaThreadSynchronize();
	}

	nready_gpu++;

	while (!ready)
		PTHREAD_COND_WAIT(&cond_go, &mutex);

	PTHREAD_MUTEX_UNLOCK(&mutex);

	unsigned iter;
	for (iter = 0; iter < niter; iter++)
	{
		if (id == 0) {
			send_data(0, 1);
			recv_data(1, 0);
		}
		else {
			recv_data(0, 1);
			send_data(1, 0);
		}
	}

	PTHREAD_MUTEX_LOCK(&mutex);
	nready_gpu--;
	PTHREAD_COND_SIGNAL(&cond_go);
	PTHREAD_MUTEX_UNLOCK(&mutex);

	return NULL;
}

int main(int argc, char **argv)
{

	PTHREAD_MUTEX_INIT(&mutex, NULL);
	PTHREAD_COND_INIT(&cond, NULL);
	PTHREAD_COND_INIT(&cond_go, NULL);

	unsigned id;
	for (id = 0; id < 2; id++)
	{
		thread_is_initialized[id] = 0;
		pthread_create(&thread[0], NULL, launch_gpu_thread, &id);

		PTHREAD_MUTEX_LOCK(&mutex);
		while (!thread_is_initialized[id])
		{
			 PTHREAD_COND_WAIT(&cond, &mutex);
		}
		PTHREAD_MUTEX_UNLOCK(&mutex);
	}

	struct timeval start;
	struct timeval end;

	/* Start the ping pong */
	gettimeofday(&start, NULL);

	PTHREAD_MUTEX_LOCK(&mutex);
	ready = 1;
	PTHREAD_COND_BROADCAST(&cond_go);
	PTHREAD_MUTEX_UNLOCK(&mutex);

	/* Wait for the end of the ping pong */
	PTHREAD_MUTEX_LOCK(&mutex);
	while (nready_gpu > 0)
	{
		PTHREAD_COND_WAIT(&cond_go, &mutex);
	}
	PTHREAD_MUTEX_UNLOCK(&mutex);

	gettimeofday(&end, NULL);
	
	double timing = (double)((end.tv_sec - start.tv_sec)*1000000 +
		(end.tv_usec - start.tv_usec));

	fprintf(stderr, "Took %.0f ms for %d iterations\n", timing/1000, niter);
	fprintf(stderr, "Latency: %.2f us\n", timing/(2*niter));

	return 0;
}
