/* Allocate and print sizes
 *
 * Copyright (C) 2019 OPTEYA SAS
 * Copyright (C) 2019 Yann Droneaud <ydroneaud@opteya.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

/* This try to mimic the output of libmallook */

#include <stdlib.h>
#include <stdio.h>

#define MAX (1024 * 1024)

int main(void)
{
	printf("@ Start\n");

	for (int s = 1; s <= MAX; s++)
	{
		for (int i = 0; i < MAX / s; i++)
		{
			void *p = malloc(s);

			__asm__ ("" : "=r" (p) : "0" (p)); // hide from compiler optimization

			free(p);

			printf("malloc %u\n", s);
		}
	}

	printf("@ End\n");

	return 0;
}
