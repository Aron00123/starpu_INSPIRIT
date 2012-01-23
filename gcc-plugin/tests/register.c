/* GCC-StarPU
   Copyright (C) 2011, 2012 Institut National de Recherche en Informatique et Automatique

   GCC-StarPU is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   GCC-StarPU is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC-StarPU.  If not, see <http://www.gnu.org/licenses/>.  */

/* Test whether `#pragma starpu register ...' generates the right code.  */

#undef NDEBUG

#include <mocks.h>

static void
foo (void)
{
  int x[] = { 1, 2, 3 };

  expected_register_arguments.pointer = x;
  expected_register_arguments.elements = sizeof x / sizeof x[0];
  expected_register_arguments.element_size = sizeof x[0];
#pragma starpu register x /* (warning "considered unsafe") */
}

static void
bar (float *p, int s)
{
  expected_register_arguments.pointer = p;
  expected_register_arguments.elements = s;
  expected_register_arguments.element_size = sizeof *p;
#pragma starpu register p s
}

int
main (int argc, char *argv[])
{
#pragma starpu initialize

  int x[123];
  double *y;
  static char z[345];
  static float m[7][42];
  static float m3d[14][11][80];
  short w[] = { 1, 2, 3 };
  size_t y_size = 234;

  y = malloc (234 * sizeof *y);

  expected_register_arguments.pointer = x;
  expected_register_arguments.elements = 123;
  expected_register_arguments.element_size = sizeof x[0];
#pragma starpu register x 123 /* (note "can be omitted") */

  expected_register_arguments.pointer = y;
  expected_register_arguments.elements = 234;
  expected_register_arguments.element_size = sizeof *y;
#pragma starpu register y 234

  expected_register_arguments.pointer = y;
  expected_register_arguments.elements = y_size;
  expected_register_arguments.element_size = sizeof *y;
#pragma starpu register y y_size

  expected_register_arguments.pointer = z;
  expected_register_arguments.elements = 345;
  expected_register_arguments.element_size = sizeof z[0];
#pragma starpu register z

  expected_register_arguments.pointer = w;
  expected_register_arguments.elements = 3;
  expected_register_arguments.element_size = sizeof w[0];
#pragma starpu register w

  expected_register_arguments.pointer = argv;
  expected_register_arguments.elements = 456;
  expected_register_arguments.element_size = sizeof argv[0];
#pragma starpu register argv 456

#define ARGV argv
#define N 456
  expected_register_arguments.pointer = argv;
  expected_register_arguments.elements = N;
  expected_register_arguments.element_size = sizeof argv[0];
#pragma starpu register   ARGV /* hello, world! */  N
#undef ARGV
#undef N

  foo ();
  bar ((float *) argv, argc);

  expected_register_arguments.pointer = argv;
  expected_register_arguments.elements = argc;
  expected_register_arguments.element_size = sizeof argv[0];

  int chbouib = argc;
#pragma starpu register argv chbouib

  expected_register_arguments.pointer = &argv[2];
  expected_register_arguments.elements = 3;
  expected_register_arguments.element_size = sizeof argv[0];
#pragma starpu register &argv[2] 3

  expected_register_arguments.pointer = &argv[argc + 3 / 2];
  expected_register_arguments.elements = argc * 4;
  expected_register_arguments.element_size = sizeof argv[0];
#pragma starpu register &argv[argc + 3 / 2] (argc * 4)

  expected_register_arguments.pointer = &y[y_size / 2];
  expected_register_arguments.elements = (y_size / 2 - 7);
  expected_register_arguments.element_size = sizeof y[0];
#pragma starpu register &y[y_size / 2] (y_size / 2 - 7)

  expected_register_arguments.pointer = m[6];
  expected_register_arguments.elements = 42;
  expected_register_arguments.element_size = sizeof m[0][0];
#pragma starpu register m[6]

  expected_register_arguments.pointer = m;
  expected_register_arguments.elements = 7;
  expected_register_arguments.element_size = sizeof m[0];
#pragma starpu register m

  expected_register_arguments.pointer = m3d;
  expected_register_arguments.elements = 14;
  expected_register_arguments.element_size = sizeof m3d[0];
#pragma starpu register m3d

  assert (data_register_calls == 16);

  free (y);

  return EXIT_SUCCESS;
}
