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

#include <starpu.h>
#include <core/jobs.h>
#include <core/workers.h>
#include <datawizard/coherency.h>
#include <profiling/bound.h>

void _starpu_debug_display_structures_size(void)
{
	fprintf(stderr, "struct starpu_task\t\t%d bytes\t(%x)\n",
			sizeof(struct starpu_task), sizeof(struct starpu_task));
	fprintf(stderr, "struct starpu_job_s\t\t%d bytes\t(%x)\n",
			sizeof(struct starpu_job_s), sizeof(struct starpu_job_s));
	fprintf(stderr, "struct starpu_data_state_t\t%d bytes\t(%x)\n",
			sizeof(struct starpu_data_state_t), sizeof(struct starpu_data_state_t));
	fprintf(stderr, "struct starpu_tag_s\t\t%d bytes\t(%x)\n",
			sizeof(struct starpu_tag_s), sizeof(struct starpu_tag_s));
	fprintf(stderr, "struct starpu_cg_s\t\t%d bytes\t(%x)\n",
			sizeof(struct starpu_cg_s), sizeof(struct starpu_cg_s));
	fprintf(stderr, "struct starpu_worker_s\t\t%d bytes\t(%x)\n",
			sizeof(struct starpu_worker_s), sizeof(struct starpu_worker_s));
}
