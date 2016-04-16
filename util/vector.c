/**
 * Demand realloc'd arrays.
 *
 * David Leonard, 2016. Released to public domain.
 */

#include <glib.h>
#include <string.h>

#include "qemu/vector.h"

void vector_init(struct vector *v)
{
    v->base = NULL;
    v->len = 0;
    v->alloc = 0;
}

void vector_free(struct vector *v)
{
    g_free(v->base);
    vector_init(v);
}

/* Ensures the array can hold n elements. Returns 0 on failure */
int vector_grow(struct vector *v, unsigned n, size_t elemsz)
{
    unsigned newalloc;
    void *newbase;

    if (n > v->alloc) {
        newalloc = (n + 127) & ~127;
        newbase = g_realloc(v->base, elemsz * newalloc);
        if (!newbase)
            return 0;
        v->base = newbase;
        v->alloc = newalloc;
    }
    return 1;
}

/* Left-shifts by one rightmost elements after i.  */
void vector_del(struct vector *v, unsigned i, size_t elemsz)
{
    char *base;

    if (i < v->len) {
        base = v->base;
        memmove(base + i * elemsz,
                base + (i + 1) * elemsz,
                (v->len - i - 1) * elemsz);
        v->len--;
    }
}

/* Right-shifts by one rightmost elements from index i. Returns i */
unsigned vector_ins(struct vector *v, unsigned i, size_t elemsz)
{
    char *base;

    if (i <= v->len) {
        base = v->base;
        memmove(base + (i + 1) * elemsz,
                base + i * elemsz,
                (v->len - i) * elemsz);
        v->len++;
    }
    return i;
}
