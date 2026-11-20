# Final Fantasy IV Source Port - Project Structure Guide

This directory layout separates game logic, asset parsing, and platform abstraction into modular, clean layers.

```text
ff4-c/
├── CMakeLists.txt                # Main CMake build system
├── README.md                     # Project documentation
├── PROJECT_STRUCTURE.md          # Architectural outline (this file)
├── data/                         # Ripped assets and metadata
│   ├── ff4-en-data.json          # Main game database (from everything8215/ff4)
│   ├── graphics/                 # Extracted sprite sheets, tilemaps, fonts
│   ├── audio/                    # Extracted sound samples, SPC track data
│   └── maps/                     # Raw map data, collision maps, triggers
├── external/                     # Embedded third-party dependencies
│   └── cjson/                    # Lightweight C JSON parser (cJSON.c / cJSON.h)
├── include/                      # Public Header Files
│   └── ff4/
│       ├── core/                 # State management, data types, math
│       │   ├── ff4_types.h       # Base integer types, enums, constants
│       │   ├── ff4_state.h       # RAM structs (Party, World, Battle state)
│       │   ├── ff4_math.h        # Formula math (damage, hit %, turn queues)
│       │   └── ff4_save.h        # Save file serialization
│       ├── engine/               # Game Systems Logic
│       │   ├── asset_loader.h    # JSON / binary asset loader interface
│       │   ├── event_engine.h    # Script interpreter and event triggers
│       │   ├── battle_engine.h   # ATB queue, combat actions, enemy AI
│       │   ├── field_engine.h    # Tilemaps, collision, camera, map switches
│       │   └── menu_engine.h     # UI navigation, inventory, status screens
│       └── platform/             # Hardware / OS Abstraction Layers
│           ├── render_api.h      # Abstract draw calls (tilemaps, sprites, fade)
│           ├── audio_api.h       # Abstract audio queue (BGM tracks, SFX)
│           └── input_api.h       # Unified button input states
├── src/                          # Source Code Implementation
│   ├── core/
│   │   ├── ff4_state.c
│   │   ├── ff4_math.c
│   │   └── ff4_save.c
│   ├── engine/
│   │   ├── asset_loader.c
│   │   ├── event_engine.c
│   │   ├── battle_engine.c
│   │   ├── field_engine.c
│   │   └── menu_engine.c
│   └── platform/
│       ├── sdl2/                 # Primary modern backend (PC / Handhelds)
│       │   ├── sdl_main.c        # Main game loop, FPS governor
│       │   ├── sdl_render.c      # SDL_Renderer / Texture handling
│       │   ├── sdl_audio.c       # Audio queue playback
│       │   └── sdl_input.c       # Keyboard / Gamepad mapping
│       └── null/                 # Headless backend (for unit testing / CLI)
│           └── null_platform.c
└── tests/                        # Unit tests for game logic
    ├── test_core.c               # Phase 1: state, statuses, inventory, ATB,
    │                             #         + rip-data constant validation
    ├── test_math.c               # Verify damage math against SNES hardware tests
    └── test_events.c             # Verify script engine state transitions
```

## The 4-Phase Conversion Strategy

```
 1. Assets & Data Layer    everything8215/ff4 JSON/rip outputs
        │
 2. Core RAM & State Maps  SNES $0000-$1FFF -> C structs (done in Phase 1)
        │
 3. Subsystem Logic        damage/math -> event engine -> battle queue
        │
 4. Platform Abstraction   render/audio/input APIs + SDL2 host
```

Everything the game does is recreated faithfully — including the original
bugs (long-range flag never reset, elemental strong-weakness never cleared,
Adamant armour weakness flip, ...). If the SNES code does it, this port does
it; fidelity over "fixing".