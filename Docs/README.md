# Hell Verdict

**Hell Verdict** is a retro-style first-person shooter inspired by classic Doom, built entirely in Unity. It features procedurally generated maze levels, 2D sprite enemies in a 3D environment (2.5D style), and progressively challenging stages.

**Developer:** Studio Balmung  
**Version:** 1.2.2  
**Engine:** Unity 6 (Unity 6000.4.3f1)

---

## Tech Stack

| Component           | Technology                                      |
|---------------------|------------------------------------------------|
| **Engine**          | Unity 6 (6000.x LTS)                          |
| **Language**        | C# 12                                          |
| **Runtime**         | CoreCLR (.NET)                                 |
| **Architecture**    | Object-Oriented Programming (OOP/MonoBehaviour)|
| **Graphics API**    | DirectX 12 (Windows default)                   |
| **Render Pipeline** | Universal Render Pipeline (URP)                |
| **Input System**    | Unity New Input System (`UnityEngine.InputSystem`) |
| **UI Framework**    | Unity UI (uGUI) — runtime-generated via code   |
| **Level Generation**| Procedural maze generation (custom algorithm)  |
| **Build Target**    | Windows x86_64 standalone                      |

### Third-Party Dependencies

| Package                | License                  |
|------------------------|--------------------------|
| Unity Engine           | Unity Proprietary        |
| Universal Render Pipeline (URP) | Unity Companion License |
| Unity Input System     | Unity Companion License  |
| Convai SDK for Unity   | Convai Inc. License      |
| Newtonsoft.Json (Json.NET) | MIT License          |

---

## Project Structure

```
Hell Verdict/
├── Assets/
│   ├── Scripts/
│   │   ├── Core/           # Game bootstrap, managers, input handling
│   │   ├── Gameplay/       # Player controller, weapon system
│   │   ├── Enemy/          # Enemy AI, animation, damage
│   │   └── UI/             # HUD, menus, minimap (all runtime-generated)
│   └── resources/
│       ├── sprites/npc/    # Enemy sprite animations
│       ├── textures/       # Wall textures, splash screens
│       ├── materials/      # URP materials
│       └── ui/             # UI assets
├── Docs/                   # This documentation
└── ProjectSettings/        # Unity project configuration
```

### Core Scripts

| Script                    | Purpose                                           |
|---------------------------|---------------------------------------------------|
| `HellVerdictBootstrap.cs` | Level building, enemy spawning, maze generation, material caching |
| `GameManager.cs`          | Game state, level progression, pause, save/load    |
| `InputManager.cs`         | Centralized input handling (new Input System)      |
| `PlayerController.cs`     | Player movement, jumping, sprinting, healing       |
| `WeaponSystem.cs`         | Shooting, ammo management, raycasting              |
| `EnemyController.cs`      | Enemy AI, pathfinding, animations, death sequences |
| `UIManager.cs`            | All UI panels, HUD, menus (created at runtime)     |
| `AudioManager.cs`         | Sound effects and music management                 |

---

## How to Play

### Controls

| Action           | Key / Input          |
|------------------|----------------------|
| **Move Forward** | W / Up Arrow         |
| **Move Backward**| S / Down Arrow       |
| **Strafe Left**  | A / Left Arrow       |
| **Strafe Right** | D / Right Arrow      |
| **Look Around**  | Mouse Movement       |
| **Shoot**        | Left Mouse Button    |
| **Reload**       | R                    |
| **Jump**         | Space                |
| **Sprint**       | Hold Left Shift (while moving forward) |
| **Heal**         | F                    |
| **Toggle Minimap** | M                  |
| **Pause / Menu** | Enter                |
| **Options**      | Escape               |

### Objective

Navigate through procedurally generated maze levels and eliminate all enemies to advance. There are **10 levels** of increasing size and difficulty.

- Clear all enemies in a level to automatically advance to the next stage.
- A **win screen** is displayed for 2 seconds between levels.
- Clearing the **final level (Level 10)** shows the win screen for 3 seconds, then returns to the title screen.

### Enemies

| Enemy        | HP  | Attack Type | Attack Range |
|--------------|-----|-------------|--------------|
| **Soldier**  | 20  | Ranged (gun)| 8 units      |
| **Caco Demon** | 50 | Melee     | 1.5 units    |
| **Cyber Demon** | 80 | Ranged   | 6 units      |

- All enemies always know the player's position and will actively navigate toward you.
- Enemies will not clip through walls.
- Player weapon damage: **10 per shot**, range: **12 units**.
- Ammo is **infinite** (no need to manage ammo count).

### Healing System

- Press **F** to heal for **50% of max HP** (50 HP).
- You start each level with **3 heal charges**.
- Once depleted, defeating **5 enemies** grants **+1 heal charge**.
- Cannot heal when already at full health.
- Heal charges are displayed on the HUD as `Heal [F]: X`.

### HUD

The gameplay HUD displays:
- **Health** (top-left)
- **Heal charges** (below health)
- **Level** (top-center)
- **Ammo** (top-right, displayed as ∞)
- **Minimap** (toggle with M)

---

## Building & Sharing

### Build the Game

1. Open the project in Unity 6.
2. Go to **File → Build Settings**.
3. Select **Windows, Mac, Linux** platform.
4. Click **Build**.

### Distributing the Build

**Keep these files/folders:**
- `Hell Verdict.exe`
- `Hell Verdict_Data/` folder
- `UnityPlayer.dll`
- `UnityCrashHandler64.exe`
- Any `.dll` files in the root

**Delete these before sharing:**
- `_BurstDebugInformation_DoNotShip/`
- `_BackUpThisFolder_ButDontShipItWithYourGame/`
- Any `.pdb` files

---

## System Requirements

- **OS:** Windows 10/11 (64-bit)
- **Graphics:** DirectX 12 compatible GPU
- **RAM:** 4 GB minimum
- **Storage:** ~200 MB

---

## License

Hell Verdict © Neofilisoft / Studio Balmung. All rights reserved.
