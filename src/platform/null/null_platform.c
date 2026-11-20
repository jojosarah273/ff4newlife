/**
 * @file null_platform.c
 * @brief Headless host backend: boots the game state, runs a fixed number
 *        of field frames, then exits. Used for CI / unit-testing and as the
 *        template the real backends (SDL2 etc.) replace in Phase 9.
 */
#include <stdio.h>
#include <stdlib.h>

#include "ff4/core/ff4_state.h"
#include "ff4/platform/app_paths.h"

int main(int argc, char **argv)
{
    FF4_GameState g;
    long frames = (argc > 1) ? strtol(argv[1], NULL, 10) : 120;
    char data_path[512];

    (void)data_path;
    if (!ff4_resolve_path("data/ff4-en-data.json", data_path,
                          sizeof data_path)) {
        fprintf(stderr, "cannot find data/ff4-en-data.json\n");
        return 1;
    }

    ff4_state_init(&g);

    ff4_party_add(&g, FF4_CHAR_CECIL_DK, 0);
    ff4_party_add(&g, FF4_CHAR_KAIN_1, 1);
    ff4_party_add(&g, FF4_CHAR_RYDIA_CHILD, 2);

    while (frames-- > 0)
        g.world.frame_counter++;

    printf("ff4 null backend ok: party[%u,%u,%u] world=%u map=%u frames=%u\n",
           g.party[0].id, g.party[1].id, g.party[2].id,
           (unsigned)g.world.world, (unsigned)g.world.map_id,
           (unsigned)g.world.frame_counter);
    return 0;
}