/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013  Centre National de la Recherche Scientifique
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <common/config.h>
#include <core/perfmodel/perfmodel.h>
#include <ctype.h>

#ifdef STARPU_HAVE_WINDOWS
static
void _starpu_read_spaces(FILE *f)
{
	int c = getc(f);
	if (isspace(c))
	{
		while (isspace(c)) c = getc(f);
		ungetc(c, f);
	}
	else
	{
		ungetc(c, f);
	}
}
#endif /* STARPU_HAVE_WINDOWS */

int _starpu_read_double(FILE *f, char *format, double *val)
{
#ifdef STARPU_HAVE_WINDOWS
/** Windows cannot read NAN values, yes, it is really bad ... */
	int x1 = getc(f);

	if (x1 == 'n')
	{
	     int x2 = getc(f);
	     int x3 = getc(f);
	     if (x2 == 'a' && x3 == 'n')
	     {
#ifdef _MSC_VER
		     unsigned long long _mynan = 0x7fffffffffffffffull;
		     double mynan = *(double*)&_mynan;
#else
		     double mynan = NAN;
#endif
		     _starpu_read_spaces(f);
		     *val = mynan;
		     return 1;
	     }
	     else
	     {
		     return 0;
	     }
	}
	else
	{
		ungetc(x1, f);
		return fscanf(f, format, val);
	}
#else
	return fscanf(f, format, val);
#endif
}
