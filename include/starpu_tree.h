/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009-2013  Université de Bordeaux 1
 * Copyright (C) 2010-2013  Centre National de la Recherche Scientifique
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

#ifndef __STARPU_TREE_H__
#define __STARPU_TREE_H__

#ifdef __cplusplus
extern "C"
{
#endif


#include <stdlib.h>
struct starpu_tree {
	struct starpu_tree **nodes;
	struct starpu_tree *father;
	int arity;
	int id;
	int level;
	int is_pu;
};

void starpu_tree_reset_visited(struct starpu_tree *tree, int *visited);
 
void starpu_tree_insert(struct starpu_tree *tree, int id, int level, int is_pu, int arity, struct starpu_tree *father);

struct starpu_tree* starpu_tree_get(struct starpu_tree *tree, int id);

struct starpu_tree* starpu_tree_get_neighbour (struct starpu_tree *tree, struct starpu_tree *node, int *visited, int *present);

int starpu_tree_free(struct starpu_tree *tree);

#ifdef __cplusplus
}
#endif

#endif /* __STARPU_TREE_H__ */

