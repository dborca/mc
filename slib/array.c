/*
 * Copyright (c) 2007 Daniel Borca  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <stdlib.h>
#include "array.h"


void
arr_init (ARRAY *a, int eltsize, int growth)
{
    a->len = a->max = 0;
    a->data = NULL;
    a->error = 0;
    a->eltsize = eltsize;
    a->growth = growth;
    a->amount = growth ? growth : 16;
}


void
arr_reset (ARRAY *a)
{
    if (a->error) {
	return;
    }
    a->len = 0;
}


void *
arr_enlarge (ARRAY *a)
{
    void *p;
    if (a->error) {
	return NULL;
    }
    if (a->len == a->max) {
	int max = a->max + a->amount;
	p = realloc(a->data, max * a->eltsize);
	if (p == NULL) {
	    a->error = 1;
	    return NULL;
	}
	a->max = max;
	a->data = p;
	if (a->growth == 0) {
	    a->amount = max;
	}
    }
    p = (void *)((char *)a->data + a->eltsize * a->len++);
    return p;
}


void
arr_free (ARRAY *a, void (*func) (void *))
{
    if (func != NULL) {
	int i;
	for (i = 0; i < a->len; i++) {
	    func((void *)((char *)a->data + a->eltsize * i));
	}
    }
    free(a->data);
    arr_init(a, a->eltsize, a->growth);
}
