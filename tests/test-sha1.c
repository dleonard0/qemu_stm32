#include "qemu/sha1.h"
#include <glib.h>

static const struct {
    const char *input;
    size_t inputlen;
    const char expected[20];
} test_vector[] = {
    { "abc", 3,
      { 0xA9,0x99,0x3E,0x36,
        0x47,0x06,0x81,0x6A,
        0xBA,0x3E,0x25,0x71,
        0x78,0x50,0xC2,0x6C,
        0x9C,0xD0,0xD8,0x9D }},
    { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
      { 0x84,0x98,0x3E,0x44,
        0x1C,0x3B,0xD2,0x6E,
        0xBA,0xAE,0x4A,0xA1,
        0xF9,0x51,0x29,0xE5,
        0xE5,0x46,0x70,0xF1 }}
};
#define lengthof(a) (sizeof (a) / sizeof (a)[0])

static void test_sha1(void)
{
        SHA1_CTX ctx;
        char digest[20];
        unsigned i;

        for (i = 0; i < lengthof(test_vector); i++) {
            SHA1Init(&ctx);
            SHA1Update(&ctx, test_vector[i].input, test_vector[i].inputlen);
            SHA1Final(digest, &ctx);
            g_assert(memcmp(test_vector[i].expected, digest, 20) == 0);
        }
        g_assert(strcmp(test_vector[0].input, "abc") == 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/sha1", test_sha1);

    g_test_run();

    return 0;
}
