# Structural audit (this pass)

What exists, what is actually verified, what is duplicated, and the
convergence plan. Written after the "playable demo" push, which merged
the game stack into the ROM-faithful renderer for the first time.

## The two stacks (the core structural finding)

The repository grew **two parallel universes**:

| | Stack A — "the framework" | Stack B — "the verified renderer" |
|---|---|---|
| app | `ff4_sdl` (`src/platform/sdl2/sdl_main.c`) | `ff4_world` (`src/platform/sdl2/world_view.c`) |
| field graphics | **debug grid** (`render_field`) | ROM-faithful packs (`wp_pass`, `sm_*`), emulator-guarded |
| game logic | `FF4_GameState`, `ff4_field_*`, `ff4_event_*`, `ff4_menu_*`, `ff4_scene_*` | its own walking/warps/NPCs, no party/inventory |
| verification | `test_events`, `test_field`, `test_menu`, `test_battle`, `test_scene` (unit, vs the rip) | 8 pixel guards incl. submap/world A/B, sprite sweep 338/338 |
| can you play? | yes, but as coloured rectangles | no party/menu — beautiful but inert |

Neither alone is the product. **This pass joined them**: `ff4_world`
now loads `FF4_AssetDB` + `FF4_GameState`, initialises the prologue
party, and adds talk, a paged message box, the real menu engine, and
save/load — all driven by *real SDL key events* (so a scripted run is
the same code path as a human).

## Bugs the audit found (fixed)

1. **The keyboard was dead.** In `world_view.c` the `SDL_KEYDOWN`
   branch that sets `held[]` came *before* the action handler and
   `else if` made the latter unreachable — T, V, B, 1/2/3, Esc and
   every new action never fired from a physical key. (All prior
   verification used environment variables, which is why no guard
   caught it.) Fixed by handling held-state and actions in one branch.
2. **`frame_counter` incremented twice per frame** in the sub-map
   branch, so scripted-input frames could be skipped. Fixed to one
   global increment.
3. **F5/F9 vs `SDLK_*`**: F-key symbols are masked scancodes; matching
   by scancode is the unambiguous test. (F9 is scancode 66, not 67.)
4. **Menu close semantics**: `ff4_menu_input`'s `res.done` means
   "selection taken", not "menu closed"; using it as the close signal
   exited the menu on the first Z. Now the `FF4_MenuCtx.open` flag is
   the truth.

## What is verified today (19 guards + unit tests)

- **Data**: palette tables, window font, animation graphics, layout
  recovery (308/308), pack reproduction (0 px vs reference PNGs),
  warp audit (0 mismatches).
- **Rendering**: sub-map views (8/8 exact), layout groups, field tile
  animation (4 phases), lava palette cycle, bridge z-order, world tile
  report, **world water shimmer (4/4 phases vs the emulator-proven
  model)**, full sprite sweep (338/338 NPCs, 0 mismatched).
- **Playable layer** (`tools/verify_play.py`, new): the game stack
  loads (Cecil+Kain, gil, items), Z talks to the ROM NPC text, the
  message box opens/closes at the locked geometry, Enter opens the
  menu with real rows, DOWN+Z descends, and F5→F9 round-trips the
  position.

## Gap list, in the order that gives the most playability next

1. **Event VM wiring to the field** — `ff4_event_run` exists and
   decodes 256/256 scripts (`test_events`), and the `.trig` files
   carry each trigger's event id, but `world_view` still applies warps
   directly and ignores the script. Wiring it unlocks: doors that
   need flags, arrival scenes, chests (`0xFE` records need the chest
   script path), story NPC changes. **The single biggest playability
   step.**
2. **Chests** — the `0xFE` trigger records exist; their contents come
   from the event scripts (no `treasureProperties` in the rip). Goes
   with (1).
3. **Battle rendering** — `ff4_battle_*` + `ff4_scene_*` are verified
   as logic and `ff4_field_battle_step` rolls encounters, but battles
   render as text. Needs the battle background/ATB window on top of
   the real renderer. Then the game is genuinely a game.
4. **NPC behaviour** — random walk, facing the player, speed tables
   (`npc.asm`), and the `$090a`/`$0edc` visibility rules instead of the
   two hard-coded story hides.
5. **Mode-7 world composer** — the last visual gap (underground water
   palette + the `$2400→screen` packing; see `docs/WATER-ANIM.md`).
6. **Core/platform split** — `ff4_world` is one 3,000-line file; the
   playable state machine should move to `src/engine/` so the null
   platform, SDL and a future Android host all drive the same tick.
   Do this *with* (1), since the event VM belongs in the core.

## The target architecture (converged)

```
src/core/    ff4_types, ff4_state, ff4_math, ff4_gfx     (no SDL)
src/engine/  asset_db, field, event, battle, menu, scene (no SDL)
src/engine/  field_scene.c  <- the playable state machine (to extract)
src/platform/sdl2/  presenters only: field view (packs), windows, audio
src/platform/null/  headless tick for tests
```

Guards are the contract at every step: a move is done only when the
guard that would catch its regression exists.

## How to run the playable demo

```
./demo.sh            # headless tour incl. the water GIF
./demo.sh --play     # interactive: arrows, Z talk/confirm, X cancel,
                     # Enter menu, F5 save, F9 load, 1/2/3 worlds
./tools/verify_play.py   # the playable-layer guard
```
