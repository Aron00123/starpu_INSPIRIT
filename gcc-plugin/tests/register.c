/* GCC-StarPU
   Copyright (C) 2011 Institut National de Recherche en Informatique et Automatique

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

#include <lib.h>

int
main (int argc, char *argv[])
{
  int x[123];
  double *y;
  static char z[345];
  short w[] = { 1, 2, 3 };

  y = malloc (234 * sizeof *y);

  expected_register_arguments.pointer = x;
  expected_register_arguments.elements = 123;
  expected_register_arguments.element_size = sizeof x[0];
#pragma starpu register x 123

  expected_register_arguments.pointer = y;
  expected_register_arguments.elements = 234;
  expected_register_arguments.element_size = sizeof *y;
#pragma starpu register y 234

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

  /* FIXME: Uncomment the example below when macros are suitably
     expanded.  */
#if 0
#define ARGV argv
#define N 456
  expected_register_arguments.pointer = argv;
  expected_register_arguments.elements = N;
  expected_register_arguments.element_size = sizeof argv[0];
#pragma starpu register ARGV N
#undef ARGV
#undef N
#endif

  assert (data_register_calls == 5);

  free (y);

  return EXIT_SUCCESS;
}
