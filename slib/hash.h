#ifndef HASH_H_included
#define HASH_H_included

typedef unsigned int (*hash_func) (const void *);
typedef int (*compare_func) (const void *, const void *);
typedef void (*free_func) (const void *);
typedef void (*iter_func) (void *key, void *value, void *data);

typedef struct hash_table hash_table;

hash_table *hash_table_new(unsigned int size, hash_func hasher, compare_func key_cmp, free_func key_free, free_func val_free);
void hash_table_destroy(hash_table *h);
int hash_table_insert(hash_table *h, const void *key, const void *val);
void *hash_table_lookup(const hash_table *h, const void *key);
void hash_table_foreach(const hash_table *h, iter_func func, void *data);

#endif
