# Hell Verdict — Build Guide
**Copyright © 2026 Neofilisoft / Studio Balmung**  
*Powered by Balmung Engine 2.3*

---

## Requirements

| Dependency | Version | Notes |
|---|---|---|
| Balmung Engine | 2.3 beta | Place adjacent to this folder as `balmung-engine/` |
| CMake | ≥ 3.25 | |
| C++ compiler | GCC 13+ / Clang 17+ / MSVC 2022+ | C++20 required |
| SDL3 | 3.x | `libsdl3-dev` on Ubuntu |
| Vulkan SDK | 1.3+ | Primary renderer (PC + Mobile) |
| OpenGL | 4.1+ | Fallback renderer |
| glslc | Vulkan SDK | Compiles GLSL → SPIR-V |
| Mono | 6.x **or** .NET 10 | C# game scripting |

---

## Directory Layout

```
hell_verdict/
├── CMakeLists.txt
├── BUILD.md
├── src/
│   ├── main.cpp                   ← Entry point + game loop
│   ├── game/
│   │   ├── types.hpp              ← Math, AABB, colors
│   │   ├── collision.hpp/.cpp     ← Sweep AABB, wall sliding, epsilon guards
│   │   ├── map_data.hpp/.cpp      ← ASCII map loader + geometry builder
│   │   ├── player.hpp/.cpp        ← FPS controller, weapons, interpolation
│   │   ├── enemy.hpp/.cpp         ← AI state machine (Zombie, Imp, Demon)
│   │   └── hell_game.hpp/.cpp     ← Fixed-step loop, events, pickup logic
│   ├── renderer/
│   │   ├── vk_renderer.hpp/.cpp   ← Vulkan 1.3 (PRIMARY)
│   │   └── doom_renderer.hpp/.cpp ← OpenGL 4.1 (FALLBACK)
│   └── scripting/
│       └── cs_bridge.hpp/.cpp     ← Mono/.NET bridge → C# scripts
├── scripts/
│   └── game/
│       ├── HellScriptContext.cs   ← C# game logic (events, audio, HUD text)
│       └── HellVerdict.Scripts.csproj
└── assets/
    ├── maps/
    │   └── e1m1.txt               ← Episode 1 Map 1 (ASCII grid)
    └── shaders/
        ├── world.vert / world.frag   ← World geometry (Doom fog + lighting)
        ├── bill.vert  / bill.frag    ← Enemy billboards
        └── hud.vert   / hud.frag     ← 2D HUD overlay
```

---

## Build (PC — Linux / Windows)

```bash
# 1. Clone / place Balmung Engine alongside this project
#    hell_verdict/ and balmung-engine/ should be siblings

# 2. Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DBALMUNG_DIR=../balmung-engine

# 3. Build
cmake --build build --parallel

# 4. Compile C# scripts (.NET 10)
cd scripts/game
dotnet build -c Release
cd ../..

# 5. Run
./build/bin/HellVerdict
```

### Windows (MSVC)
```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
      -DBALMUNG_DIR=..\balmung-engine
cmake --build build --config Release
cd scripts\game && dotnet build -c Release && cd ..\..
build\bin\Release\HellVerdict.exe
```

---

## Build (Android / Mobile)

```bash
# Requires Android NDK r26+ and Vulkan enabled device
cmake -B build-android \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-28 \
      -DHV_ENABLE_OPENGL=OFF \
      -DBALMUNG_DIR=../balmung-engine
cmake --build build-android --parallel
```

---

## Map Format

Maps are plain ASCII `.txt` files. Legend:

| Char | Meaning |
|---|---|
| `#` | Wall (solid, collision box) |
| `.` | Floor (walkable) |
| `P` | Player start |
| `Z` | Zombie spawn |
| `I` | Imp spawn |
| `D` | Demon spawn |
| `H` | Health pickup (+25 HP) |
| `A` | Ammo pickup (+12 shells) |
| `S` | Shotgun pickup |
| `X` | Level exit |

---

## Controls

| Key | Action |
|---|---|
| `W/A/S/D` or Arrow keys | Move |
| Mouse | Look |
| Left click | Fire |
| Scroll wheel | Switch weapon |
| `Esc` | Quit |

---

## Performance Notes

- **Fixed physics step**: 120 Hz (`FIXED_STEP = 1/120`). Render interpolates between ticks → smooth 60+ FPS regardless of frame rate.
- **Collision epsilon**: `0.001f` — prevents jitter/sinking without tunneling.
- **Sweep AABB**: Minkowski-sum ray cast resolves penetration cleanly without repeated broadphase pairs.
- **Wall sliding**: Up to 3 solver iterations per tick (Quake-style).
- **Vulkan swapchain**: `VK_PRESENT_MODE_FIFO_KHR` (VSync) — switch to `MAILBOX` in `vk_renderer.cpp` for uncapped FPS.
- **Map geometry**: Only exposed wall faces are emitted (backface culled at generation time), ~50% fewer triangles vs naïve approach.

---

## C# Scripting

The `HellScriptContext.cs` script receives engine events via `IBalmungScriptContext::OnEvent()`:

```
hell_verdict:game_start   { level, enemies }
hell_verdict:player_hit   { damage, health }
hell_verdict:player_dead  { score }
hell_verdict:enemy_hit    { damage, score }
hell_verdict:pickup       { type, amount? }
hell_verdict:victory      { score }
```

Add new game logic by handling events in `HellScriptContext.cs` — no C++ recompile needed.

---

*Hell Verdict — Copyright © 2026 Neofilisoft / Studio Balmung*
