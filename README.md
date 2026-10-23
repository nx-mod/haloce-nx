# Halo: Combat Evolved for Nintendo Switch

A native Switch port of Halo: Combat Evolved, built from the decompilation
of the Xbox game, drawn with deko3d. It plays online with
[OpenCE](https://github.com/OpenCommunityEdition/OpenCE)'s other platforms.

**No game data is included:** you need your own Xbox copy of the game.

## Install

1. Copy `halo.nro` from the
   [latest release](https://github.com/nx-mod/haloce-nx/releases/latest) into
   `sdmc:/haloce-nx/`.
2. Put your Xbox disc image (`.iso` or `.xiso`) in the same folder.
3. Start `halo.nro` with full memory: a
   [Sphaira](https://github.com/ITotalJustice/sphaira) forwarder, or a game's
   title takeover (hold R while it starts).

The first start copies the maps and movies out of the disc image, then
starts the game. After that the folder holds:

| | |
|---|---|
| `icon.jpg` | the game's icon, for the forwarder |
| `config.toml` | the settings |
| `profiles/` | player profiles |
| `saves/` | checkpoint and saved games |
| `cache/` | map caches (safe to delete) |
| `logs/` | `halo.log`, `debug.txt`, `gamestate.txt` |
| `maps/`, `bink/` | game data and movies |

The game updates itself from this repository's releases, asking first.

## Features

- deko3d renderer at 720 lines by default, with dynamic resolution
- 60 fps; shaders compiled on two threads, shared shader keys downloaded so a
  new install has them, and a map's load waits for its shaders
- online play with OpenCE (network version 32): server browser, internet games
  through UPnP, hosting up to 48 players, joining a game in progress
- co-op campaign over the network
- the disc's movies (intro, menus, credits, cutscene sound)
- gyro aiming, controllers for players 1–4 and handheld
- 4x anisotropic filtering

## Settings

`config.toml`, beside `halo.nro`. The Switch's own:

| Setting | Default | |
|---|---|---|
| `display.resolution_limit` | `"720"` | lines drawn: `"480"`, `"720"`, `"900"`, `"1080"` |
| `display.dynamic_resolution` | `false` | a step down while the GPU falls behind |
| `display.anisotropy` | `4` | texture filtering at a slant, 1–16 |
| `performance.cpu_clock` | `"1785"` | `"1785"`, `"1020"` (cooler), or `"stock"` (the console's or sys-clk's) |
| `input.gyro_aim` | `"false"` | `"true"`, or `"zoom"` for zoomed aiming only |

## What nx-mod adds to thelinkin3000's port

- one-file install: the game image inside `halo.nro`, and the folder layout
  above (older `save/` folders are moved in)
- the maps and movies copied out of the disc image on the console at first
  start, then a fresh start of the game
- the disc's movies, and the forwarder icon from the disc
- updates from this repository's releases (the shared shader keys still come
  from thelinkin3000's)
- 4x anisotropic filtering (`display.anisotropy`)
- the last multiplayer map kept in memory, not read from the card every frame
- START NOW changes the start time for the pressing player's machine
- interpolation: an object an animation carries is not snapped every tick
- menu placeholder pictures drawn from the Xbox map's

## Build

```sh
python3 configure.py    # --release for a release build
ninja switch            # build/switch/halo.nro
```

Requirements and design: [port/switch/README.md](port/switch/README.md).

## Credits

- **Bungie**: Halo: Combat Evolved. Halo is a trademark of Microsoft.
- **[punpckhdq/halo](https://github.com/punpckhdq/halo)**,
  **[bnunu/halo-1](https://github.com/bnunu/halo-1)**: the decompilation.
- **[OpenCE](https://github.com/OpenCommunityEdition/OpenCE)**: the native
  port, netcode, co-op and menus.
- **[thelinkin3000/halo-ce-universal](https://github.com/thelinkin3000/halo-ce-universal)**:
  the Switch port, its deko3d renderer and its online and performance work,
  which this is a fork of.
- **nx-mod**: the disc install and movies, the icon, the folder layout,
  releases and updates.
- **[devkitPro](https://devkitpro.org)**, **[FFmpeg](https://ffmpeg.org)**,
  **[musl](https://musl.libc.org)**.

Not affiliated with Microsoft or Bungie.
