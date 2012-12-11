/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010,2011 University of Bordeaux
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

#include "socl.h"
#include "gc.h"
#include "event.h"

static void task_release_callback(void *arg) {
  cl_command cmd = (cl_command)arg;
  starpu_task task = cmd->task;
  
  cl_event ev = command_event_get(cmd);
  ev->status = CL_COMPLETE;

  /* Trigger the tag associated to the command event */
  starpu_tag_notify_from_apps(ev->id);

  if (task->profiling_info != NULL && (intptr_t)task->profiling_info != -ENOSYS) {
    ev->profiling_info = malloc(sizeof(*task->profiling_info));
    memcpy(ev->profiling_info, task->profiling_info, sizeof(*task->profiling_info));
  }

  gc_entity_release(ev);

  /* Release the command */
  //FIXME
  //free(cmd);
}


/*
 * Create a StarPU task
 */
starpu_task task_create() {
  struct starpu_task * task;

  /* Create StarPU task */
  task = starpu_task_create();

  /* Set task common settings */
  task->destroy = 0;
  task->detach = 0;

  task->use_tag = 1;
  task->tag_id = event_unique_id();

  return task;
}


void task_depends_on(starpu_task task, cl_uint num_events, cl_event *events) {

  if (num_events != 0) {
    cl_uint i;

    starpu_tag_t * tags = malloc(num_events * sizeof(starpu_tag_t));	

    for (i=0; i<num_events; i++) {
       tags[i] = events[i]->id;
       DEBUG_MSG_NOHEAD(" %u", events[i]->id);
    }
    DEBUG_MSG_NOHEAD("\n");

    starpu_tag_declare_deps_array(task->tag_id, num_events, tags);

    free(tags);
  }
}

cl_int task_submit_ex(starpu_task task, cl_command cmd) {

  /* Associated the task to the command */
  cmd->task = task;

  task_depends_on(task, command_num_events_get(cmd), command_events_get(cmd));

  task->callback_func = task_release_callback;
  task->callback_arg = cmd;

  /* Submit task */
  int ret = (task->cl != NULL && task->cl->where == STARPU_OPENCL ?
        starpu_task_submit_to_ctx(task, cmd->cq->context->sched_ctx) :
        starpu_task_submit(task));

  if (ret != 0)
     DEBUG_ERROR("Unable to submit a task. Error %d\n", ret);

  return CL_SUCCESS;
}


/*********************************
 * CPU task helper
 *********************************/

struct cputask_arg {
  void (*callback)(void*);
  void * arg;
  int free_arg;
};

static void cputask_task(__attribute__((unused)) void *descr[], void *args) {
  struct cputask_arg * arg = (struct cputask_arg*)args;

  arg->callback(arg->arg);

  if (arg->free_arg) {
    assert(arg->arg != NULL);
    free(arg->arg);
    arg->arg = NULL;
  }

  free(arg);

}

void cpu_task_submit_ex(cl_command cmd, void (*callback)(void*), void *arg, int free_arg, struct starpu_codelet * codelet, unsigned num_events, cl_event * events) {
  
  struct cputask_arg * a = malloc(sizeof(struct cputask_arg));
  a->callback = callback;
  a->arg = arg;
  a->free_arg = free_arg;

  codelet->where = STARPU_CPU;
  codelet->cpu_funcs[0] = &cputask_task;

  starpu_task task = task_create();
  task->cl = codelet;
  task->cl_arg = a;

  if (num_events != 0) {
     task_depends_on(task, num_events, events);
  }

  task_submit(task, cmd);
}

