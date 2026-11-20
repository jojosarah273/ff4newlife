/* test_bundle: resource framework smoke test. */
#include <stdio.h>
#include <string.h>

#include "ff4/resource/ff4_bundle.h"

static int checks, failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++checks;                                                          \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);\
        }                                                                  \
    } while (0)

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "data/gfx";
    FF4_Bundle b;
    size_t len = 0;
    const uint8_t *p;

    CHECK(ff4_bundle_load(&b, dir) == 0);
    CHECK(b.loaded);
    CHECK(b.count > 100);            /* many mirrored blobs */
    p = ff4_bundle_bytes(&b, "pal/map_pal.bin", &len);
    CHECK(p != NULL && len == 4096);
    p = ff4_bundle_bytes(&b, "pal/window_pal.bin", &len);
    CHECK(p != NULL && len == 32);
    /* every manifest entry must exist on disk and load */
    {
        int i, missing = 0;

        for (i = 0; i < b.count; ++i)
            if (!b.entries[i].data)
                ++missing;
        CHECK(missing == 0);
    }
    printf("%d checks, %d failure(s)\n", checks, failures);
    ff4_bundle_free(&b);
    return failures == 0 ? 0 : 1;
}