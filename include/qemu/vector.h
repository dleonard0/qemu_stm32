/**
 * Summary: dynamically resized array utilities
 * Description: a vector is an array auto-sized with realloc
 *
 * Author: David Leonard, 2016. Public domain.
 */

#ifndef QEMU_VECTOR_H
#define QEMU_VECTOR_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A vector is a demand-realloc'd array.
 */
struct vector {
    void *base;
    unsigned len, alloc;
};

/*
 * VECTOR_OF(T)         - type declaration for a vector of elements T
 *
 * VECTOR_INIT(v)       - initialises a vector to empty
 * VECTOR_FREE(v)       - releases the vector array storage
 * VECTOR_AT(v, i)      - acccess i'th element
 * VECTOR_LEN(v)        - number of elements in vector
 * VECTOR_DEL(v, i)     - v[i:n] := v[i+1:n]
 * VECTOR_APPEND(v, e)  - increments length and stores e (returns 0 on fail)
 * VECTOR_INSERT(v,i,e) - inserts e at element i (returns 0 on fail)
 * VECTOR_POP(v)        - delets and returns last element (assumes len>0)
 */
#define VECTOR_OF(T)            union { T *elem; struct vector _v; }
#define VECTOR_INIT(v)          vector_init(&(v)._v)
#define VECTOR_FREE(v)          vector_free(&(v)._v)
#define VECTOR_AT(v, i)         (v).elem[i]
#define VECTOR_LAST(v)          VECTOR_AT(v, VECTOR_LEN(v) - 1)
#define VECTOR_LEN(v)           (v)._v.len
#define VECTOR_DEL(v, i)        vector_del(&(v)._v, i, sizeof (v).elem[0])
#define VECTOR_GROW(v, n)       vector_grow(&(v)._v, n, sizeof (v).elem[0])
#define VECTOR_APPEND(v, e)     (VECTOR_GROW(v, (v)._v.len + 1)         \
                                  ? ((v).elem[(v)._v.len++] = (e), 1)   \
                                  : 0)
#define VECTOR_INSERT(v, i, e)  (VECTOR_GROW(v, (v)._v.len + 1)         \
                                  ?  ((v).elem[vector_ins(&(v)._v,      \
                                      i, sizeof (v).elem[0])] = (e), 1) \
                                  : 0)
#define VECTOR_POP(v)           (v).elem[--(v)._v.len]

/* Internal functions */
void vector_init(struct vector *v);
void vector_free(struct vector *v);
int vector_grow(struct vector *v, unsigned n, size_t elemsz);
void vector_del(struct vector *v, unsigned i, size_t elemsz);
unsigned vector_ins(struct vector *v, unsigned i, size_t elemsz);

#ifdef __cplusplus
}
#endif
#endif /* QEMU_VECTOR_H */
