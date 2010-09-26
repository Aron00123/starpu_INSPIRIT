/*
 * StarPU
 * Copyright (C) Université Bordeaux 1, CNRS 2010 (see AUTHORS file)
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct task {
	double start;
	double stop;
	int worker;
};

int main(int argc, char *argv[]) {
	int nw, nt;
	double tmax;
	int i, w, t, t2;
	int foo;
	double bar;
	unsigned long num;
	int b;
	unsigned long next = 1;

	if (argc != 3) {
		fprintf(stderr,"usage: %s nb_workers nb_tasks\n", argv[0]);
		exit(1);
	}
	nw = atoi(argv[1]);
	nt = atoi(argv[2]);
	fprintf(stderr,"%d workers, %d tasks\n", nw, nt);
	assert(scanf("Optimal - objective value       %lf", &tmax) == 1);
	printf(
"%%EventDef PajeDefineContainerType 1\n"
"%%  Alias         string\n"
"%%  ContainerType string\n"
"%%  Name          string\n"
"%%EndEventDef\n"
"%%EventDef PajeCreateContainer     2\n"
"%%  Time          date\n"
"%%  Alias         string\n"
"%%  Type          string\n"
"%%  Container     string\n"
"%%  Name          string\n"
"%%EndEventDef\n"
"%%EventDef PajeDefineStateType     3\n"
"%%  Alias         string\n"
"%%  ContainerType string\n"
"%%  Name          string\n"
"%%EndEventDef\n"
"%%EventDef PajeDestroyContainer    4\n"
"%%  Time          date\n"
"%%  Name          string\n"
"%%  Type          string\n"
"%%EndEventDef\n"
"%%EventDef PajeDefineEntityValue 5\n"
"%%  Alias         string\n"
"%%  EntityType    string\n"
"%%  Name          string\n"
"%%  Color         color\n"
"%%EndEventDef\n"
"%%EventDef PajeSetState 6\n"
"%%  Time          date\n"
"%%  Type          string\n"
"%%  Container     string\n"
"%%  Value         string\n"
"%%EndEventDef\n"
"1 W 0 Worker\n"
);
	printf("3 S W \"Worker State\"\n");
	printf("5 S S Running \"0.0 1.0 0.0\"\n");
	printf("5 F S Idle \"1.0 0.0 0.0\"\n");
	for (i = 0; i < nw; i++)
		printf("2 0 W%d W 0 \"%d\"\n", i, i);

	for (w = 0; w < nw; w++)
		printf("4 %f W%d W\n", tmax, w);

	assert(scanf("%d C%d %lf %lf", &foo, &foo, &tmax, &bar) == 4);
	next++;
	{
		struct task task[nt];
		memset(&task, 0, sizeof(task));
		for (t = 0; t < nt; t++) {
			assert(scanf("%d C%d %lf %lf", &foo, &foo, &task[t].stop, &bar) == 4);
			next++;
		}

		while (1) {
			assert(scanf("%d C%lu", &foo, &num) == 2);
			if (num >= next +

				/* FIXME */
				//nw*nt
				8*20 + 5*16

				) {
				next+= 8*20+5*16;
				break;
			}
			/* FIXME */
			if (num-next < 8*20) {
				t = (num - next) / nw;
				w = (num - next) % nw;
			} else {
				unsigned long nnum = (num-next)-8*20;
				t = (nnum / 5) + 20;
				w = (nnum % 5)+3;
			}

			assert(scanf("%d %lf", &b, &bar) == 2);
			if (b) {
				task[t].worker = w;
				fprintf(stderr,"%lu: task %d on %d: %f\n", num, t, w, task[t].stop);
			}
		}
		while(1) {
			t = num - next;
			if (t > nt)
				break;
			assert(scanf("%lf %lf", &task[t].start, &bar) == 2);
			assert(scanf("%d C%lu", &foo, &num) == 2);
		}

		for (t = 0; t < nt; t++) {
			printf("6 %f S W%d S\n", task[t].start, task[t].worker);
			printf("6 %f S W%d F\n", task[t].stop, task[t].worker);
		}

		for (t = 0; t < nt; t++) {
			for (t2 = 0; t2 < nt; t2++) {
				if (t != t2 && task[t].worker == task[t2].worker) {
					if (!(task[t].start >= task[t2].stop
					    || task[t2].start >= task[t].stop)) {
						fprintf(stderr,"oops, %d and %d sharing worker %d !!\n", t, t2, task[t].worker);
					}
				}
			}
		}
	}

	return 0;
}
