cmake_minimum_required(VERSION 3.16)

project(ff4_c
    VERSION 0.1.0
    DESCRIPTION "Final Fantasy IV (SNES) C99 source port - core framework"
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

# ---------------------------------------------------------------------------
# Vendored third-party libraries
# ---------------------------------------------------------------------------
add_library(cjson STATIC external/cjson/cJSON.c)
target_include_directories(cjson PUBLIC external/cjson)

# ---------------------------------------------------------------------------
# Core: platform-agnostic game logic (phase 1: state management)
# ---------------------------------------------------------------------------
add_library(ff4_core STATIC
    src/core/ff4_state.c
    src/core/ff4_math.c
    src/core/ff4_gfx.c
    src/resource/ff4_bundle.c
    src/engine/asset_db.c
    src/engine/event_engine.c
    src/engine/battle_engine.c
    src/engine/field_engine.c
    src/engine/menu_engine.c
    src/engine/audio_engine.c
    src/engine/scene_machine.c
)
target_include_directories(ff4_core PUBLIC include PRIVATE external/cjson external/stb)
target_link_libraries(ff4_core PUBLIC cjson m)

# ---------------------------------------------------------------------------
# Platform backends
# ---------------------------------------------------------------------------
# null: headless host used by tests and CI.
add_executable(ff4_null src/platform/null/null_platform.c)
target_link_libraries(ff4_null PRIVATE ff4_core)

# sdl2: primary interactive backend (optional; requires SDL2 dev headers).
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    add_executable(ff4_sdl src/platform/sdl2/sdl_main.c
                           src/platform/sdl2/sdl_render.c
                           src/platform/sdl2/sdl_audio.c
                           src/platform/sdl2/sdl_input.c)
    target_link_libraries(ff4_sdl PRIVATE ff4_core SDL2::SDL2)
else()
    message(STATUS "SDL2 not found - interactive backend disabled")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE ff4_core cjson)

add_executable(test_assets tests/test_assets.c)
target_link_libraries(test_assets PRIVATE ff4_core cjson)
target_include_directories(test_assets PRIVATE include)

add_test(NAME core COMMAND test_core ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json)
add_test(NAME assets COMMAND test_assets ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json
                                                ${CMAKE_SOURCE_DIR}/third_party/ff4/vanilla/ff4-en-rip.json)

add_executable(test_math tests/test_math.c)
target_link_libraries(test_math PRIVATE ff4_core cjson)
add_test(NAME math COMMAND test_math ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json)

add_executable(test_events tests/test_events.c)
target_link_libraries(test_events PRIVATE ff4_core cjson)
add_test(NAME events COMMAND test_events ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json)

add_executable(test_battle tests/test_battle.c)
target_link_libraries(test_battle PRIVATE ff4_core cjson)
add_test(NAME battle COMMAND test_battle ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json)

add_executable(test_field tests/test_field.c)
target_link_libraries(test_field PRIVATE ff4_core cjson)
add_test(NAME field COMMAND test_field ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json)

add_executable(test_menu tests/test_menu.c)
target_link_libraries(test_menu PRIVATE ff4_core cjson)
add_test(NAME menu COMMAND test_menu ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json)

add_executable(test_audio tests/test_audio.c)
target_link_libraries(test_audio PRIVATE ff4_core cjson)
add_test(NAME audio COMMAND test_audio ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json)

add_executable(test_scene tests/test_scene.c)
target_link_libraries(test_scene PRIVATE ff4_core cjson)
add_test(NAME scene COMMAND test_scene ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json)

add_executable(test_gfx tests/test_gfx.c)
target_link_libraries(test_gfx PRIVATE ff4_core cjson)
add_test(NAME gfx COMMAND test_gfx ${CMAKE_SOURCE_DIR}/data/ff4-en-data.json
         ${CMAKE_SOURCE_DIR}/data/graphics)
# ---- PPU verification tool (renders a map view to PNG for emulator diffs)
if(EXISTS tools/render_verify.c)
add_executable(render_verify tools/render_verify.c)
target_link_libraries(render_verify PRIVATE ff4_core cjson)
endif()

# ---- message box lock (window tile bank + measured layout) ----
add_executable(test_dialog tests/test_dialog.c)
target_include_directories(test_dialog PRIVATE include)
add_test(NAME dialog COMMAND test_dialog ${CMAKE_SOURCE_DIR}/data/gfx/window)

add_executable(test_bundle tests/test_bundle.c)
target_link_libraries(test_bundle PRIVATE ff4_core cjson)
add_test(NAME bundle COMMAND test_bundle ${CMAKE_SOURCE_DIR}/data/gfx)

# ---- world map viewer (first engine piece): ff4_world
find_package(SDL2_ttf QUIET)
add_executable(ff4_world src/platform/sdl2/world_view.c
                         src/platform/sdl2/sdl_render.c)
target_link_libraries(ff4_world PRIVATE ff4_core SDL2::SDL2)
if(SDL2_ttf_FOUND)
  target_link_libraries(ff4_world PRIVATE SDL2_ttf::SDL2_ttf)
  target_compile_definitions(ff4_world PRIVATE FF4_HAVE_SDL2_TTF=1)
endif()

# ---- sprite/physics rule validation (needs the ROM) ----
add_executable(test_world_rules tests/test_world_rules.c)
if(EXISTS ${CMAKE_SOURCE_DIR}/ff2.smc)
  add_test(NAME world_rules COMMAND test_world_rules ${CMAKE_SOURCE_DIR}/ff2.smc)
endif()
