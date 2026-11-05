# Novuss for PlayStation 3 & Desktop

Novuss is the traditional Latvian cue-and-disc board game, often described as a compact pocket billiards game played with wooden pucks on a square wooden table. 

This repository contains a full C++ port of Novuss for the **Sony PlayStation 3** (built with PSL1GHT v2) and **Desktop** (macOS and Linux via SDL2). 

The game logic and physics are based on the original web implementation created by [laacz.lv](https://laacz.lv) as part of [didnt.work](https://didnt.work), faithfully ported to PS3 homebrew by [ciematnieks.lv](https://ciematnieks.lv).

---

## Features

- **Official LNF Rules**: Fully compliant with the 2024/2025 Latvian Novuss Federation (Latvijas Novusa Federācija) regulations:
  - 1000 × 1000 mm regulation field with 45 mm hardwood rails and 100 mm corner pockets.
  - Correct fouls, penalty puck placements (Rule 7.3), dark puck restrictions (Rule 4.1 / 4.3), and turn-pass logic.
  - Tie-break and set replay conditions (Rules 12.4, 12.5, 12.6).
- **Smooth 60 FPS Rendering**: Software renderer optimized for the PS3 Cell PPU with smoothstep table rotation animations between sets.
- **AI Opponent**: 3 difficulty levels (Easy, Medium, Hard) with simulated aim rotation, power charging, and cue pullback.
- **Local Multiplayer**: 2-player hotseat mode.
- **Custom Board Skins**: Drop any PNG into `assets/board.png` (or `USRDIR/board.png` on PS3) to use your own custom table art with bilinear filtering.
- **Audio & Haptics**: Authentic disc strikes, cushion bounces, pocket drops, menu background music, and DualShock 3 rumble support.
- **Bilingual**: Latvian and English interface, in-game rules reference, and virtual onscreen keyboard.

---

## Controls

### DualShock 3 (PS3)

| Button | Action |
| :--- | :--- |
| **D-Pad Left / Right** | Slide striker along the baseline (skips over blocking pucks) |
| **Left Analog Stick** | Aim cue angle |
| **L1 / R1** | Fine aim adjustment |
| **Hold Cross (✕) / R2** | Charge shot power |
| **Release Cross (✕) / R2**| Take shot |
| **Circle (○)** | Cancel charged shot / Close menu |
| **Triangle (△)** | Open in-game menu |
| **Square (□)** | Open rules reference |

### Keyboard (Desktop)

| Key | Action |
| :--- | :--- |
| **Arrow Left / Right** | Move striker |
| **Arrow Up / Down** | Aim cue angle |
| **A / D** | Fine aim angle |
| **Spacebar (Hold & Release)** | Charge power and shoot |
| **Escape / Backspace** | Cancel charge / Open menu |

---

## Custom Table Skins

You can replace the table appearance with any custom design:

1. Create a square image (**1090 × 1090 px** or **2180 × 2180 px** for 2× scale) saved as PNG.
2. Layout specifications:
   - Outer rails: 45 px width along all 4 outer edges.
   - Playing field: 1000 × 1000 px in the center.
   - 4 corner pocket centers: at $(110, 110)$, $(980, 110)$, $(110, 980)$, $(980, 980)$ from outer board corners (diameter 100 px).
   - Center circle: center at $(545, 545)$, diameter 250 px.
   - Baseline zone lines: 130 px inside cushion rails.
3. Save the image to `assets/board.png` (desktop) or `USRDIR/board.png` inside the PS3 package.
4. If no custom image is present, the game falls back to procedural birch and mahogany rendering.

---

## Building

### Desktop (macOS / Linux)

Prerequisites: A C++17 compiler and SDL2 development headers.

```bash
# macOS (via Homebrew)
brew install sdl2

# Ubuntu / Debian
sudo apt-get install libsdl2-dev build-essential

# Build desktop binary
make

# Run tests
make test

# Launch game
./build/novuss
```

### PlayStation 3 (`NOVUSS.pkg`)

Prerequisites: [Docker](https://www.docker.com/) (uses the prebuilt `zeldin/ps3dev-docker` PSL1GHT SDK image).

```bash
# Compile and build the installable PKG package
docker run --rm -v "$PWD":/src -w /src zeldin/ps3dev-docker:arm64 bash -c '\
  export PS3DEV=/usr/local/ps3dev; \
  export PSL1GHT=/usr/local/ps3dev; \
  export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH; \
  make -f Makefile.ps3 pkg'
```

This generates `NOVUSS.pkg` in the project root.

---

## Installation on PS3

1. Copy `NOVUSS.pkg` to the root of a FAT32-formatted USB flash drive.
2. Insert the USB drive into your PlayStation 3 running Custom Firmware (CFW) or PS3HEN.
3. On the XMB menu, navigate to **Game** $\to$ **Package Manager** $\to$ **Install Package Files** $\to$ **Standard**.
4. Select `NOVUSS.pkg` to install.
5. Launch **Novuss** from the Game column.

---

## Credits & License

- **Original Novuss web game**: [laacz.lv](https://laacz.lv) ([didnt.work](https://didnt.work))
- **PlayStation 3 port & C++ engine**: [ciematnieks.lv](https://ciematnieks.lv)
- **PSL1GHT SDK**: Open-source PS3 toolchain by the PSL1GHT contributors.
- **stb_image**: Sean Barrett ([public domain](https://github.com/nothings/stb)).

Licensed under the MIT License.
