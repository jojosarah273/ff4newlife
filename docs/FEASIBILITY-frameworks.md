# Feasibility: what framework should FF4-C hang on?

**Question.** We are not building a SNES emulator. We are building a **2D JRPG
that uses FF4's logic and assets**. We already have ~10k LOC of verified C
logic (event VM, battle, field, math, menus) plus a rip pipeline and an
emulator-diff harness. The reference project — `bryanthaboi/gen1recomp` —
hangs its game on **LÖVE2D + Lua**. Should we do the same, or pick something
else?

---

## 0. TL;DR

**Recommendation: keep C, adopt an established C host framework — SDL3
(current) with an option on raylib — and copy gen1recomp's *architecture*,
not its language. Add Lua only as an embedded content/mod layer later.**

Why not a Lua rewrite: we would throw away the single most expensive asset we
own (the verified logic core) and re-earn it in a language with no benefit for
this workload. Why not stay bespoke: we are currently hand-rolling exactly the
things an established framework gives away (window/input/audio/texture/save/
packaging/cross-platform).

The one place FF4 is genuinely harder than Pokémon Gen 1 is **audio** (SPC700 +
BRR vs. a tiny 4-channel GB APU). That difficulty *favours C*, where a proven
SPC core drops in directly.

---

## 1. What we're actually building

| | |
| --- | --- |
| Genre | 2D JRPG (top-down field, menus, ATB battles, cutscenes) |
| Presentation | 256×224 logical screen, integer-scaled; tile backgrounds, sprite overlays |
| Content | 383 maps, 256 event scripts, 254 monster scripts, 72 spells, 256 items, 2 world maps |
| Logic | already ported & tested in C (event VM, battle engine, field engine, menu engine, math) |
| Assets | ripped to a 4.7 MB JSON + 13 MB bundle; decode codecs verified against snes9x |
| Target | desktop first; mobile/console desirable; mod-friendly desirable |

The engine work that remains is **host work**, not simulation work: present a
frame, read input, mix audio, load assets, save, package, and expose a mod
surface.

---

## 2. The reference: gen1recomp

Cloned and measured (`bryanthaboi/gen1recomp`, GPL-3.0, rev. 2026):

| Metric | Value |
| --- | --- |
| Language / runtime | Lua 5.1 / **LuaJIT**, framework **LÖVE2D 11.x** |
| Total Lua | ~1.16 M LOC / 1 943 files in `src/` |
| — `src/import/` | 489 k LOC / 409 files (per-game ROM decode metadata) |
| — `src/core/` | 326 k LOC / 772 files (services, platform, save, net) |
| — `src/ui/` | 184 k LOC / 433 files (launcher, menus, editor) |
| — actual game engine | render 12 k, world 33 k, battle 30 k, script 11 k, mods 21 k ≈ **107 k LOC** |
| Games shipped | 11 (Gen 1–3) |
| Platforms | Windows, macOS, Linux (AppImage/Flatpak/deb), Android, iOS, Switch, PS4, Xbox, ARM handhelds |
| Modding | first-class: registries, events/hooks, per-mod saves/options, in-game manager, shipped example mods |
| Distribution | LÖVE runtime + game archive appended → single executable |
| Assets | **no ROM shipped**: user supplies a ROM; `RomExtractor` validates SHA-1 and decodes graphics/tables/audio on first import into a private cache |
| Audio | GB APU reimplemented (`ChipAudio`/`ChipSynth`), music programs streamed from ROM, SFX synthesized |
| Tests | `lua5.4 tests/run_tests.lua` against a **`love_stub`** (headless, no GPU); autopilot e2e run with screenshots |

Architecture worth stealing (language-independent):

```
RomImporter -> RomExtractor -> private generated cache
                                  |
LÖVE2D engine  Game (service hub) |  FixedStep 60 Hz | Input | StateStack | SaveData
               Renderer (canvas + integer nearest scale)
                 TileRenderer  (one SpriteBatch per map, 8x8 quads + ring)
                 SpriteRenderer / Font / TextBox (typewriter, \n \v \f) / Camera / Transition
               world: Map, Collision, Warp, Encounter, OverworldController
               script: ScriptRunner (coroutines) + Commands + Flags
               battle: BattleState + Damage/TypeChart/TurnOrder/Status/...
               ui: start menu, generic menu, yes/no, lists
```

Two design decisions are the whole trick:

1. **The engine is hand-written; the data is decoded from the user's ROM at
   import time.** No copyrighted bytes ship in the repo.
2. **Everything is a small, named service** with a fixed-step loop and a state
   stack — which is exactly what makes the mod platform possible.

---

## 3. What transfers regardless of framework

- **The resource bundle + rip pipeline.** Already built (`tools/export_resources.js`
  → `data/gfx/` + manifest, `ff4_bundle`). Framework-agnostic.
- **The decode codecs** (SNES 2/3/4bpp planes, tilemap entries, anchored
  palettes, RLE layouts, sprite sheets) — the *knowledge* is the hard part;
  porting 300 lines of codec to any language is a day.
- **The emulator-diff verification loop** (`tools/retro_dump.c` + capture/diff)
  — language-agnostic and rare; keep it forever.
- **The architecture**: service hub, fixed-step loop, state stack, coroutine
  scripts, map/battle/menu separation. Ports 1:1 conceptually.
- **Our C logic itself** transfers *only* to a C/C++ host.

What does **not** transfer: LÖVE-specific implementation (canvases, shaders,
SpriteBatcher, `love.filesystem`), and the Lua-only mod ecosystem.

---

## 4. Options

### A. LÖVE2D + Lua / LuaJIT (the gen1recomp path)
Port the logic to Lua and hang it on LÖVE.

- **+** Proven at exactly this task; the entire architecture above is a
  reference implementation, and there is a large Pokémon-recomp community
  (and mod authors) to recruit from.
- **+** Free: window/input/gamepad/audio/canvas/shader, cross-platform builds
  (desktop, mobile, Switch/PS4 homebrew), single-file packaging, save dirs.
- **+** Mod platform becomes trivial and safe (sandboxed Lua, hot reload).
- **−** **Throw away ~10 k LOC of verified, tested C logic** and the tuning
  effort behind it. The port is mechanical but not free (est. 2–4 weeks to
  parity on the parts we already have).
- **−** **SPC audio**: FF4 music is SPC700 code + BRR samples. In Lua this
  needs a LuaJIT-FFI binding to a C SPC core (doable) or a from-scratch
  sequencer; gen1recomp's own APU reimplementation does not apply.
- **−** Tooling friction we just hit: LÖVE needs a real GL context, so headless
  CI needs either `love_stub`-style mocking (they built one) or Xvfb +
  software GL. Our current C suite runs anywhere, instantly.
- **−** Whole-program Lua means GC pauses and a big surface to keep
  deterministic; not a blocker, but a tax on a "faithful" project.

### B. SDL3 + C (keep the code, upgrade the host) — **recommended**
Keep the logic; replace the ad-hoc host with SDL3's stable API.

- **+** **Zero logic rewrite.** Our `ff4_core` static library compiles as-is.
- **+** SDL3 adds what we lacked in SDL2: a unified audio stream API (perfect
  for a mixer + SPC core), a GPU abstraction, camera, dialogs, and an
  actively maintained cross-platform story (desktop, Android, iOS, web via
  Emscripten).
- **+** **Audio solved in-language**: drop in blargg's `snes_spc` (or a
  compact SPC emulator, ~5–8 k LOC C) and stream its output through SDL audio.
  This is the single strongest argument for C here.
- **+** Our existing tests, cmake, and emulator harness continue to work.
- **−** We still own rendering composition (no SpriteBatch/atlas helpers),
  save, packaging, and a mod surface. Mitigation: our PPU is already written;
  packaging is per-platform work; mods can come later via embedded Lua.
- **−** Console ports need per-console SDL forks (SDL2 ecosystem did; SDL3
  support is younger). Same is true of raylib.

### C. raylib + C
- **+** Batteries-included, tiny API: textures, sprites, fonts, audio, input,
  camera, shaders. A JRPG's needs are ~covered. Single-file-ish distribution.
- **+** Keeps our C logic unchanged; very fast to a playable slice.
- **+** Cross-platform incl. web and Android; big community.
- **−** Audio API is simple (no mixer graphs) — fine for us: we mix SPC/BRR
  ourselves into a ring buffer.
- **−** Not in Debian bookworm (build from source or vendor); no first-party
  console support; fewer "production" niceties than SDL.
- **−** We still write atlas/tilemap helpers (small).

### D. Godot 4 (+ GDExtension)
- **+** Best *editor* tooling for a JRPG: TileMapLayer, atlases, animation
  player, UI system, scene composition, built-in audio buses. Tilemaps and
  menus would take days instead of weeks.
- **+** GDExtension can host our C core, so the logic survives.
- **−** Two-language project (GDScript + C) and a large engine dependency;
  build/export pipeline is heavier; console export is gated (W4 Games).
- **−** Godot's data model (Resources/`.tres`, `res://`) pushes us to convert
  our bundle into Godot assets — a new pipeline on top of the one we have.
- **−** Mod access is less sandboxable than Lua-in-LÖVE, and Godot version
  churn is real.

### E. MonoGame / FNA (C#)
- **+** Mature 2D XNA-style stack; Stardew/Terraria-class games ship on it;
  good tooling (MGCB), C# productivity.
- **−** Rewrite in C#; ties to .NET runtime; console support via FNA (good)
  but the ecosystem expects C# natives.
- **−** Same SPC problem, solved with a C# SPC port or P/Invoke.

### F. Also-rans
- **libGDX (Java/Kotlin)**: capable, JVM tax, mobile-first, modding awkward.
- **Bevy (Rust)**: excellent ECS, wrong shape for a data-driven JRPG port;
  steep, churny.
- **Ebitengine (Go)**: lovely, but another rewrite and a thinner JRPG story.

---

## 5. Decision matrix

Scores 1 (poor) – 5 (excellent), weighted by what this project needs.

| Criterion | W | LÖVE+Lua | **SDL3+C** | raylib+C | Godot4 | MonoGame |
| --- | --- | --- | --- | --- | --- | --- |
| Reuses our verified C logic | 5 | 1 | **5** | **5** | 4¹ | 1 |
| Rendering fit (tiles/palettes/layers) | 4 | 5 | 4 | 4 | 5 | 4 |
| **Audio (SPC700 + BRR)** | 4 | 2 | **5** | **5** | 4² | 4² |
| Iteration speed for content/UI | 4 | 5 | 3 | 3 | 4 | 3 |
| Cross-platform reach | 3 | 5 | 4 | 4 | 4 | 4 |
| Mod platform (long-term) | 3 | 5 | 3³ | 3³ | 3 | 2 |
| Tooling/tests (headless CI) | 3 | 3 | **5** | **5** | 3 | 4 |
| Ecosystem/community | 2 | 5 | 4 | 4 | 5 | 3 |
| Licensing freedom | 3 | (LÖVE: zlib) | (SDL: zlib) | (raylib: zlib) | (MIT) | (MS-PL) |
| Console/homebrew path | 2 | 4 | 3 | 2 | 2 | 4 |

¹ via GDExtension (extra plumbing). ² via an emulator/port we still write;
³ via embedding Lua (planned).

---

## 6. The audio question (the real fork)

FF4's soundtrack is **SPC700 code running against a 64 KB ROM image**, driving
the SNES DSP with **BRR samples** — not a note list. The rip already gives us
`brrSamples`, `songScripts`, `spcData`, and `brrSample*`.

Three ways to play it:

1. **Run the SPC program** with a C SPC core (blargg `snes_spc`, ~8 k LOC,
   permissive licence). Accurate to the original, including effects.
   Trivial in C; needs an FFI bridge in Lua.
2. **Reimplement a sequencer** over the ripped `songScripts` + decoded BRR.
   More control, no emulator; more work; risk of subtle divergence.
3. **Pre-render** long loops at install time with option 1 and ship audio
   streams. Cheapest, breaks dynamic SFX.

Options 1–2 both want to be written in C. This is why the C path is the
low-risk path for this specific game.

---

## 7. Recommendation

**Two-track, C-first:**

1. **Immediate (this repo):** stay C. Upgrade the host to **SDL3** (falling
   back to SDL2 where SDL3 is unavailable), keeping `ff4_core` untouched.
   Introduce the gen1recomp patterns we lack: a single `Game` service hub, a
   fixed-step loop, a state stack, and a renderer that bakes map atlases the
   way their `AtlasBake`/`TileRenderer` do.
2. **Audio:** vendor a small SPC core and mix through SDL3's audio stream.
   This is a 1–2 week, self-contained task with an objective test (render
   known SPC state, compare to snes9x audio capture).
3. **Later, if mods matter:** embed Lua (liblua5.4/LuaJIT) as the content
   scripting layer over the C core — event scripts, dialogue, data-driven
   mods — mirroring gen1recomp's mod surface without its full rewrite.

**When to choose LÖVE2D instead:** if the goal shifts from "the most faithful
FF4 we can build" to "a fast-moving, mod-first community game with the
Pokémon-recomp crowd", and the team is happy to work in Lua. That is a valid
product decision — it is just a different project from the one this repo has
been building.

---

## 8. Migration plan (C path, ~milestones)

| M | Deliverable | Touches |
| --- | --- | --- |
| R0 | Host abstraction layer (`platform.h`): window, input, audio, time | new, ~600 LOC |
| R1 | SDL3 backend behind that layer (SDL2 fallback) | `src/platform/sdl3/` |
| R2 | Packet the game as a single artifact (data bundle + binary) | CMake/packaging |
| F1 | Field render fix: maps are **32×32**, camera clamps, sprites | `ff4_ppu`, `field` |
| F2 | Field palette settled via verifier; Baron pixel-exact | `ff4_ppu`, `tools/` |
| A1 | SPC core + mixer | `src/audio/`, vendored C |
| S1 | Slice: walk → talk (real window) → battle → level up | `scene_machine` |
| X1 | Embedded Lua for event scripts/mods (optional) | `src/script/` |

---

## 9. Risks

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| Field palette source unresolved | med | blocks pixel fidelity | verifier loop already built; try colour-math/layer model next |
| SPC audio complexity | med | no music | vendor `snes_spc`; fall back to sequencer/pre-render |
| Host rewrite scope creep | med | delays gameplay | keep `ff4_core` frozen behind a thin `platform.h` |
| Console targets demanded early | low | rework | SDL2/3 homebrew ports exist; LÖVE list shows the hardware classes |
| Mod expectations set by gen1recomp | low | scope | Lua layer is a later milestone; design data as tables now |
| Licence contamination | low | legal | gen1recomp is GPL-3.0: **copy architecture, not code**; LÖVE is zlib, SDL/raylib zlib, Godot MIT |

## 10. Appendix — measurements taken

- `gen1recomp`: 1.16 M Lua LOC; engine subset ≈ 107 k LOC; clone 408 MB.
- FF4-C today: **9 933 LOC C** (+2 816 LOC tests), 4.7 MB data JSON, 13 MB
  resource bundle, 11/11 test suites green.
- LÖVE2D 11.4 + LuaJIT 2.1 available via apt; runs here only under
  Xvfb + software GL (no headless GL), which is a CI consideration.
- Framework availability on this host: SDL2 ✔ (in use), Godot 3.2 ✔ (apt;
  Godot 4 not packaged), raylib ✖ (source/vendor), SDL3 ✖ (source/vendor),
  Lua 5.4 dev ✔.
- Bonus bug found while prototyping the Lua path: **FF4 field layouts are
  32×32 tiles (1024 bytes after RLE)**, not 32×256. Our C field compositor
  wraps at 256 and therefore reads past the map (the black lower half).
  Fix: wrap/clamp at 32 rows.
