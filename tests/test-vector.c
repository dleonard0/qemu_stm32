#include "qemu/vector.h"
#include <glib.h>

static void test_vector(void)
{
        VECTOR_OF(int) vi;
        int i;

        VECTOR_INIT(vi);
        g_assert(VECTOR_LEN(vi) == 0);

        VECTOR_APPEND(vi, 8);                   /* [8] */
        g_assert(VECTOR_LEN(vi) == 1);
        g_assert(VECTOR_AT(vi, 0) == 8);

        VECTOR_APPEND(vi, 9);                   /* [8,9] */
        g_assert(VECTOR_LEN(vi) == 2);
        g_assert(VECTOR_AT(vi, 0) == 8);
        g_assert(VECTOR_AT(vi, 1) == 9);

        VECTOR_INSERT(vi, 0, 7);                /* [7,8,9] */
        g_assert(VECTOR_LEN(vi) == 3);
        g_assert(VECTOR_AT(vi, 0) == 7);
        g_assert(VECTOR_AT(vi, 1) == 8);
        g_assert(VECTOR_AT(vi, 2) == 9);

        VECTOR_DEL(vi, 1);                      /* [7,9] */
        g_assert(VECTOR_LEN(vi) == 2);
        g_assert(VECTOR_AT(vi, 0) == 7);
        g_assert(VECTOR_AT(vi, 1) == 9);

        g_assert(&VECTOR_LAST(vi) == &VECTOR_AT(vi, 1));

        i = VECTOR_POP(vi);                     /* [7] */
        g_assert(VECTOR_LEN(vi) == 1);
        g_assert(VECTOR_AT(vi, 0) == 7);
        g_assert(i == 9);

        i = VECTOR_POP(vi);                     /* [] */
        g_assert(VECTOR_LEN(vi) == 0);
        g_assert(i == 7);

        VECTOR_INSERT(vi, 0, 6);                /* [6] */
        g_assert(VECTOR_LEN(vi) == 1);
        g_assert(VECTOR_AT(vi, 0) == 6);

        VECTOR_FREE(vi);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/vector", test_vector);

    g_test_run();

    return 0;
}
