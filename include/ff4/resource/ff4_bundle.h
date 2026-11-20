/**
 * @file ff4_bundle.h
 * @brief Resource bundle framework.
 *
 * The engine consumes a *bundle* built by tools/export_resources.js
 * (data/gfx/): a manifest.json plus the authoritative assets - the
 * palettes parsed from the disassembly .asm files (the rip JSON's
 * field palettes were verified wrong) and the rip's graphics blobs
 * mirrored. The bundle is the single resource namespace the logic and
 * renderer address ("pal/map_pal.bin", "gfx/eventScript[16].bin").
 */
#ifndef FF4_BUNDLE_H
#define FF4_BUNDLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FF4_BundleEntry {
    char name[192];       /**< "pal/map_pal.bin" style path. */
    uint8_t *data;
    size_t len;
    char type[16];
    char source[96];
} FF4_BundleEntry;

typedef struct FF4_Bundle {
    bool loaded;
    char root[256];
    FF4_BundleEntry *entries;
    int count;
    int capacity;
} FF4_Bundle;

/** Load every resource named in <root>/manifest.json. 0 on success. */
int ff4_bundle_load(FF4_Bundle *b, const char *root);

/** Look up a resource by path. NULL if absent. */
const FF4_BundleEntry *ff4_bundle_get(const FF4_Bundle *b, const char *name);

/** Convenience: raw pointer + length for a resource. */
const uint8_t *ff4_bundle_bytes(const FF4_Bundle *b, const char *name,
                                size_t *len);

void ff4_bundle_free(FF4_Bundle *b);

#ifdef __cplusplus
}
#endif

#endif /* FF4_BUNDLE_H */