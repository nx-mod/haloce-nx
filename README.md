# Halo: Combat Evolved for Nintendo Switch

A native Switch port of Halo: Combat Evolved, built from the decompilation
of the Xbox game, drawn with deko3d. It plays online with
[OpenCE](https://github.com/OpenCommunityEdition/OpenCE)'s other platforms.

Tested on firmware 22.5.0 with Atmosphère 1.11.2. **No game data is
included:** you need your own Xbox copy of the game.

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

- deko3d renderer
- online play: server browser, public games, invite links, hosting
- the disc's movies (intro, menus, credits, cutscene sound)
- controllers for players 1–4 and handheld

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
  port, netcode and menus.
- **[thelinkin3000/halo-ce-universal](https://github.com/thelinkin3000/halo-ce-universal)**:
  the Switch port and its deko3d renderer, which this is a fork of.
- **nx-mod**: movies, icon, the one-file install and folder layout, releases.
- **[devkitPro](https://devkitpro.org)**, **[FFmpeg](https://ffmpeg.org)**,
  **[musl](https://musl.libc.org)**.

Not affiliated with Microsoft or Bungie.
