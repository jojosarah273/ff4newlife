/**
 * @file ff4_bundle.c
 * @brief Resource bundle loader (manifest-driven).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff4/resource/ff4_bundle.h"

#include "cJSON.h"

static FF4_BundleEntry *find_slot(FF4_Bundle *b, const char *name)
{
    int i;

    for (i = 0; i < b->count; ++i)
        if (strcmp(b->entries[i].name, name) == 0)
            return &b->entries[i];
    return NULL;
}

static const FF4_BundleEntry *find(const FF4_Bundle *b, const char *name)
{
    int i;

    for (i = 0; i < b->count; ++i)
        if (strcmp(b->entries[i].name, name) == 0)
            return &b->entries[i];
    return NULL;
}

static int bundle_add_file(FF4_Bundle *b, const char *name,
                           const char *type, const char *source)
{
    char path[512];
    FILE *f;
    long n;
    FF4_BundleEntry *e;

    if (find_slot(b, name))
        return 0;
    snprintf(path, sizeof path, "%s/%s", b->root, name);
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[bundle] missing %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (b->count >= b->capacity) {
        int cap = b->capacity ? b->capacity * 2 : 64;
        FF4_BundleEntry *ne = (FF4_BundleEntry *)realloc(
            b->entries, (size_t)cap * sizeof *ne);

        if (!ne) {
            fclose(f);
            return -1;
        }
        b->entries = ne;
        b->capacity = cap;
    }
    e = &b->entries[b->count++];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    snprintf(e->type, sizeof e->type, "%s", type);
    snprintf(e->source, sizeof e->source, "%s", source);
    e->len = (size_t)n;
    e->data = (uint8_t *)malloc((size_t)n);
    if (e->data && fread(e->data, 1, (size_t)n, f) != (size_t)n) {
        free(e->data);
        e->data = NULL;
    }
    fclose(f);
    return 0;
}

int ff4_bundle_load(FF4_Bundle *b, const char *root)
{
    char path[512];
    char *json;
    long n;
    FILE *f;
    cJSON *arr, *item;

    if (!b)
        return -1;
    memset(b, 0, sizeof *b);
    snprintf(b->root, sizeof b->root, "%s", root);
    snprintf(path, sizeof path, "%s/manifest.json", root);
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[bundle] no manifest at %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    json = (char *)malloc((size_t)n + 1);
    if (!json) {
        fclose(f);
        return -1;
    }
    if (fread(json, 1, (size_t)n, f) != (size_t)n) {
        free(json);
        fclose(f);
        return -1;
    }
    fclose(f);
    json[n] = 0;

    arr = cJSON_Parse(json);
    if (!arr) {
        free(json);
        return -1;
    }
    cJSON_ArrayForEach(item, arr) {
        const char *name = cJSON_GetObjectItem(item, "name")
                               ? cJSON_GetObjectItem(item, "name")->valuestring
                               : NULL;
        const char *type = cJSON_GetObjectItem(item, "type")
                               ? cJSON_GetObjectItem(item, "type")->valuestring
                               : "";
        const char *src = cJSON_GetObjectItem(item, "source")
                              ? cJSON_GetObjectItem(item, "source")->valuestring
                              : "";

        if (name)
            bundle_add_file(b, name, type, src);
    }
    cJSON_Delete(arr);
    free(json);
    b->loaded = true;
    return 0;
}

const FF4_BundleEntry *ff4_bundle_get(const FF4_Bundle *b, const char *name)
{
    if (!b || !b->loaded)
        return NULL;
    return find(b, name);
}

const uint8_t *ff4_bundle_bytes(const FF4_Bundle *b, const char *name,
                                size_t *len)
{
    const FF4_BundleEntry *e = ff4_bundle_get(b, name);

    if (!e) {
        if (len)
            *len = 0;
        return NULL;
    }
    if (len)
        *len = e->len;
    return e->data;
}

void ff4_bundle_free(FF4_Bundle *b)
{
    int i;

    if (!b)
        return;
    for (i = 0; i < b->count; ++i)
        free(b->entries[i].data);
    free(b->entries);
    memset(b, 0, sizeof *b);
}