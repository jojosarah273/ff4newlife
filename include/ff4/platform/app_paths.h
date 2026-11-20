/**
 * @file app_paths.h
 * @brief Small path resolver so binaries work from any working directory.
 *
 * Hosts probe, in order:
 *   1. CWD-relative        data/...
 *   2. parent-relative     ../data/...   (when launched from build/)
 *   3. bare                ./...
 *   4. env FF4_DATA_DIR/...
 */
#ifndef FF4_APP_PATHS_H
#define FF4_APP_PATHS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Resolve a data-file path; returns true and fills `out` on success. */
static int ff4_resolve_path(const char *rel, char *out, size_t cap)
{
    const char *env = getenv("FF4_DATA_DIR");
    char probe[512];
    FILE *f;
    int i;

    for (i = 0; i < 3; ++i) {
        switch (i) {
        case 0: snprintf(probe, sizeof probe, "%s", rel); break;
        case 1: snprintf(probe, sizeof probe, "../%s", rel); break;
        default:
            if (!env)
                continue;
            snprintf(probe, sizeof probe, "%s/%s", env, rel);
            break;
        }
        f = fopen(probe, "rb");
        if (f) {
            fclose(f);
            snprintf(out, cap, "%s", probe);
            return 1;
        }
    }
    return 0;
}

#endif /* FF4_APP_PATHS_H */