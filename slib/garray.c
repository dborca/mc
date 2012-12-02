#include <config.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "glib.h"


ARRAY *
g_ptr_array_new(void)
{
    ARRAY *a = malloc(sizeof(ARRAY));
    if (a != NULL) {
	arr_init(a, sizeof(void *), 32);
    }
    assert(a);	/* XXX unlike glib, malloc() may fail without aborting */
    return a;
}


void
g_ptr_array_free(ARRAY *a, int free_seg)
{
    if (free_seg) {
	arr_free(a, NULL);
    }
    free(a);
}


int
g_ptr_array_add(ARRAY *a, const void *p)
{
    const void **d = arr_enlarge(a);
    if (d != NULL) {
	*d = p;
	return d - (const void **)a->data;
    }
    assert(d);	/* XXX unlike glib, arr_enlarge() may fail without aborting */
    return -1;
}


void *
g_ptr_array_remove_index(ARRAY *a, unsigned int i)
{
    void *p, **d;
    if (a->error) {
	return NULL;
    }
    if (i >= a->len) {
	return NULL;
    }
    d = a->data;
    p = d[i];
    a->len--;
    memmove(d, d + 1, (a->len - i) * sizeof(void *));
    return p;
}


ARRAY *
g_array_new(int z, int c, int eltsize)
{
    ARRAY *a = malloc(sizeof(ARRAY));
    if (a != NULL) {
	arr_init(a, eltsize, 0);
    }
    assert(a);	/* XXX unlike glib, malloc() may fail without aborting */
    assert(!z && !c);	/* XXX forbid zero_terminated and/or auto-clear */
    return a;
}


void
g_array_free(ARRAY *a, int free_seg)
{
    if (free_seg) {
	arr_free(a, NULL);
    }
    free(a);
}


int
g_array_append_val_(ARRAY *a, const void *p)
{
    char *d = arr_enlarge(a);
    if (d == NULL) {
	assert(d);	/* XXX unlike glib, arr_enlarge() may fail without aborting */
	return -1;
    }
    memcpy(d, p, a->eltsize);
    return 0;
}
