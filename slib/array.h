#ifndef ARRAY_H_included
#define ARRAY_H_included

typedef struct {
    int len, max;
    void *data;
    int error;
    int eltsize;
    int growth;
    int amount;
} ARRAY;


/**
 * Initialize array.
 *
 * \param a array, must be non-NULL
 * \param eltsize element size
 * \param growth growth constant
 *
 * \note if growth constant is zero, then use exponential growth.
 */
void arr_init (ARRAY *a, int eltsize, int growth);


/**
 * Reset array length without dealocating storage.
 *
 * \param a array, must be non-NULL
 */
void arr_reset (ARRAY *a);


/**
 * Enlarge array.
 *
 * \param a array, must be non-NULL
 *
 * \return new element, or NULL if error
 */
void *arr_enlarge (ARRAY *a);


/**
 * Free array.
 *
 * \param a array, must be non-NULL
 * \param func function to be called on each element
 */
void arr_free (ARRAY *a, void (*func) (void *));

#endif
