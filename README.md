# HELL VERDICT
**Copyright © 2026 Neofilisoft / Studio Balmung**  
*Powered by Balmung Engine 2.3*

A Doom-style first-person shooter — 5 levels of escalating demon carnage.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C++20 + C# (.NET 10 / Mono) |
| Engine | Balmung Engine 2.3 (In-house, Neofilisoft) |
| ECS | EnTT (header-only) |
| Math | GLM (column-major, matches Vulkan) |
| Windowing | SDL2 |
| Renderer (primary) | Vulkan 1.3 — dynamic rendering, timeline semaphores, bindless textures |
| Renderer (fallback) | OpenGL 4.1 |
| Text | Signed Distance Field fonts (sharp at 720p and 1080p) |
| Scripting | C# via Mono bridge (events, audio, UI) |
| Textures | PNG/JPG from `assets/textures/` (moddable by filename) |
| Platforms | Linux · Windows · macOS · Android (WebGPU planned) |

---

## Build

### Linux (Ubuntu/Debian)

```bash
# Dependencies
sudo apt install build-essential cmake libsdl2-dev libvulkan-dev libglm-dev \
                 libmono-dev glslang-tools vulkan-tools

# Get header-only deps
git clone --depth 1 https://github.com/skypjack/entt.git deps/entt
git clone --depth 1 https://github.com/nothings/stb.git  deps/stb

# Configure + build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBALMUNG_DIR=../balmung-engine
cmake --build build --parallel $(nproc)

# Build C# scripts
cd scripts/game && dotnet build -c Release && cd ../..

# Run
./build/bin/HellVerdict
```

### Windows (MSVC 2022)

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DBALMUNG_DIR=..\balmung-engine
cmake --build build --config Release
cd scripts\game && dotnet build -c Release && cd ..\..
.\build\bin\Release\HellVerdict.exe
```

### macOS

```bash
brew install cmake sdl2 glm molten-vk
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBALMUNG_DIR=../balmung-engine
cmake --build build --parallel
./build/bin/HellVerdict
```

### Android (NDK r26+)

```bash
cmake -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DHV_MOBILE=ON -DHV_ENABLE_OPENGL=OFF \
  -DBALMUNG_DIR=../balmung-engine
cmake --build build-android --parallel
```

---

## Controls

| Input | Action |
|---|---|
| W / A / S / D | Move |
| Mouse | Look |
| Left click | Fire |
| Scroll wheel | Switch weapon |
| ESC | Quit |

---

## Levels

| Level | File | Enemies | Notes |
|---|---|---|---|
| E1M1 | e1m1.txt | Zombie, Imp | Tutorial pace, introduces shotgun |
| E1M2 | e1m2.txt | Zombie, Imp | Larger corridors, more ammo pickups |
| E1M3 | e1m3.txt | Zombie, Imp, **Demon** | Demons introduced — fight in open rooms |
| E1M4 | e1m4.txt | Imp, Demon, **Baron of Hell** | Grid-arena layout, high enemy density |
| E1M5 | e1m5.txt | Baron, Demon, **Cacodemon** | Final boss rush — all types, tight spaces |

---

## Moddable Textures

Drop PNG or JPG files into `assets/textures/` using these exact names:

| Filename | Used for |
|---|---|
| `wall.png` | Primary wall surface |
| `wall2.png` | Secondary wall (W cells in map) |
| `ground.png` | Floor |
| `ceil.png` | Ceiling |
| `enemy_zombie.png` | Zombie billboard |
| `enemy_imp.png` | Imp billboard |
| `enemy_demon.png` | Demon billboard |
| `enemy_baron.png` | Baron of Hell billboard |
| `pickup_health.png` | Health pickup sprite |
| `pickup_ammo.png` | Ammo pickup sprite |
| `pickup_shotgun.png` | Shotgun pickup sprite |

Missing textures → magenta/black checkerboard fallback. Never crashes.

---

## Map Format

ASCII `.txt` files in `assets/maps/`:

```
# = Wall (wall.png)      W = Wall2 (wall2.png)
. = Floor                P = Player start
Z = Zombie               I = Imp
D = Demon                B = Baron of Hell
C = Cacodemon            H = Health (+25 HP)
A = Ammo (+12 shells)    S = Shotgun pickup
X = Level exit
```

---

## Architecture

```
src/
├── main.cpp                    ← SDL2 window + game loop
├── ecs/
│   ├── components.hpp          ← All EnTT components (GLM types)
│   └── ecs_world.cpp/hpp       ← Registry + systems (movement, AI, LOD, pickups)
├── world/
│   └── world_map.cpp/hpp       ← ASCII → chunk geometry (3 LOD tiers)
├── render/
│   ├── vk_frame_renderer       ← Full Vulkan frame loop (begin→world→billboard→HUD→text→end)
│   ├── vk_pipeline             ← Pipeline cache, SPIR-V loader, hot-reload
│   ├── texture_cache           ← PNG/JPG → VkImage with mipmaps
│   ├── sync/vk_sync            ← Barriers, timeline semaphores, FrameSyncManager
│   ├── font/sdf_font           ← SDF text renderer (sharp at any scale)
│   └── warmup/shader_warmup    ← Zero-pixel warmup draws, disk cache
├── renderer/
│   └── doom_renderer           ← OpenGL 4.1 fallback
├── game/
│   ├── collision               ← Sweep AABB + wall-slide (ε = 0.001)
│   ├── player                  ← FPS controller, weapons, interpolation
│   ├── enemy                   ← AI state machine (Idle→Chase→Attack→Pain→Dead)
│   └── enemy_defs              ← Stat tables for all 5 enemy types
└── scripting/
    └── cs_bridge               ← Mono/.NET bridge → C# OnEvent/OnUpdate

assets/
├── maps/          e1m1–e1m5.txt
├── shaders/       *.vert + *.frag (GLSL) → *.spv (SPIR-V, compiled by glslc)
├── textures/      wall/floor/ceil/enemies (moddable PNG/JPG)
└── fonts/         hell_verdict.png + .json (SDF atlas)

scripts/game/
├── HellScriptContext.cs        ← C# game events (audio, HUD text, score)
└── HellVerdict.Scripts.csproj
```

---

## Vulkan Sync Notes

All synchronization is documented inline in `src/render/sync/vk_sync.hpp`.
Key decisions:

- **Timeline semaphores** replace VkFence for CPU–GPU sync (allows true frame pipelining)
- **Swapchain barriers** use `TOP_OF_PIPE→COLOR_ATTACHMENT_OUTPUT` (not TOP_OF_PIPE→TOP_OF_PIPE, which would stall the whole pipeline unnecessarily)
- **Dynamic buffers** (billboards, HUD) use `HOST_COHERENT` memory — no vkFlushMappedMemoryRanges needed
- **VkSubmitInfo2** (VK 1.3) used for per-semaphore stage masks

## Performance

- Fixed physics: 120 Hz, render interpolates between ticks → smooth 60+ FPS
- Chunk LOD: Full (<14m) / Half (<26m) / Quarter (<38m) / Culled (frustum)
- Shader warmup: zero-pixel draws at level load, disk-cached pipeline cache
- SDF fonts: single 1-channel atlas, sharp at 720p/1080p via fwidth AA

---

*HELL VERDICT — © 2026 Neofilisoft / Studio Balmung*
