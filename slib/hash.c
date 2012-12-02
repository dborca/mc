/*
 * Copyright (c) 2010 Daniel Borca  All rights reserved.
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


#include <stddef.h>
#include <stdlib.h>
#include "hash.h"


#define HASH_SIZE 127 /* 251 509 1021 */


typedef struct hash_elt {
    struct hash_elt *next;
    unsigned int hash;
    void *key;
    void *val;
} hash_elt;

struct hash_table {
    unsigned int size;
    hash_func hasher;
    compare_func key_cmp;
    free_func key_free;
    free_func val_free;
    hash_elt *data[];
};


static unsigned int
direct_hash(const void *p)
{
    return (unsigned int)p;
}


static int
direct_compare (const void *p, const void *q)
{
    if (p < q) {
	return -1;
    }
    if (p > q) {
	return 1;
    }
    return 0;
}


hash_table *
hash_table_new(unsigned int size, hash_func hasher, compare_func key_cmp, free_func key_free, free_func val_free)
{
    hash_table *h;
    if (size == 0) {
	size = HASH_SIZE;
    }
    h = malloc(offsetof(hash_table, data) + size * sizeof(hash_elt *));
    if (h != NULL) {
	unsigned int i;
	for (i = 0; i < size; i++) {
	    h->data[i] = NULL;
	}
	h->hasher = hasher ? hasher : direct_hash;
	h->key_cmp = key_cmp ? key_cmp : direct_compare;
	h->key_free = key_free;
	h->val_free = val_free;
	h->size = size;
    }
    return h;
}


void
hash_table_destroy(hash_table *h)
{
    if (h != NULL) {
	unsigned int i;
	for (i = 0; i < h->size; i++) {
	    hash_elt *p = h->data[i];
	    while (p != NULL) {
		hash_elt *tmp = p;
		p = p->next;
		if (h->key_free) {
		    h->key_free(tmp->key);
		}
		if (h->val_free) {
		    h->val_free(tmp->val);
		}
		free(tmp);
	    }
	}
	free(h);
    }
}


int
hash_table_insert(hash_table *h, const void *key, const void *val)
{
    if (h != NULL) {
	unsigned int hash = h->hasher(key);
	unsigned int bucket = hash % h->size;
	hash_elt *p, *entry = h->data[bucket];
	for (p = entry; p != NULL; p = p->next) {
	    if (p->hash == hash && h->key_cmp(p->key, key) == 0) {
		if (h->key_free) {
		    h->key_free(p->key);
		}
		if (h->val_free) {
		    h->val_free(p->val);
		}
		p->key = (void *)key;
		p->val = (void *)val;
		return 0;
	    }
	}
	p = malloc(sizeof(hash_elt));
	if (p != NULL) {
	    p->next = entry;
	    p->hash = hash;
	    p->key = (void *)key;
	    p->val = (void *)val;
	    h->data[bucket] = p;
	    return 0;
	}
    }
    return -1;
}


void *
hash_table_lookup(const hash_table *h, const void *key)
{
    if (h != NULL) {
	unsigned int hash = h->hasher(key);
	unsigned int bucket = hash % h->size;
	hash_elt *p, *entry = h->data[bucket];
	for (p = entry; p != NULL; p = p->next) {
	    if (p->hash == hash && h->key_cmp(p->key, key) == 0) {
		return p->val;
	    }
	}
    }
    return NULL;
}


void
hash_table_foreach(const hash_table *h, iter_func func, void *data)
{
    if (h != NULL) {
	unsigned int i;
	for (i = 0; i < h->size; i++) {
	    hash_elt *p;
	    for (p = h->data[i]; p != NULL; p = p->next) {
		func(p->key, p->val, data);
	    }
	}
    }
}
