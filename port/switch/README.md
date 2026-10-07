# Switch

`ninja switch` builds the game for the Nintendo Switch, as a devkitA64
homebrew program. It needs two toolchains, and neither can be replaced by the
other.

The game is the same guest image as the Android build: the decompilation
compiled as ILP32 AArch64 code, with 32-bit pointers, because the game's data
formats embed pointers of the Xbox's size and they have to keep their
layout. Clang offers exactly one such target, `arm64_32-apple-watchos`, whose
Mach-O output `tools/android_asm_convert.py` rewrites into ELF. The Android
NDK supplies that clang along with `llvm-ar` and `ld.lld`; devkitA64 cannot
build the guest, because it is LP64 only.

The host is the Android host library (`port/android/host`) with three files
replaced and two more added, linked by devkitA64 against libnx, SDL2 and
Mesa. Refer to [port/android/README.md](../android/README.md) for the guest
and the platform layer, and [port/linux/README.md](../linux/README.md) for
the platform layer the two share.

The host changes are these:

| File | Why |
|---|---|
| `host_main.c` | no JNI and no APK; libnx start-up; the SD card for data |
| `host_sdl2.c` | answers the guest's SDL3 calls with devkitPro's SDL2 |
| `host_sdl3_events.c` | translates SDL2's events into the guest's SDL3 shapes |
| `host_gl.c` | Mesa is linked in, so there is no `libGLESv3.so` to open |
| `host_mman.c` | `mmap`/`mprotect` over the console's memory services |
| `host_futex.c` | futuxes over libnx's mutex and condition variable |
| `host_net_stub.c` | stands in for `posix_net.c`; see Internet play |

## Requirements

You do not need the Xbox SDK. You need the tools of the Linux build
(Python, ninja) and these:

- devkitPro, with `devkitA64`, `libnx`, `switch-sdl2`, `switch-mesa` and
  `switch-libdrm_nouveau`. `configure.py` looks for it in `DEVKITPRO`, then
  in `/opt/devkitpro` and `~/devkitpro`. The option `--devkitpro` selects a
  different one. `DEVKITA64` should point at the compiler; it is found
  without it under `$DEVKITPRO/devkitA64`.
- The Android NDK, for the guest. See port/android/README.md.
- A network connection for the first build: `configure.py` downloads musl
  1.2.5 and SDL 3.4.16 to `build/android/third_party`, which both ports share.

To install the devkitPro packages:

```
sudo dkp-pacman -S --needed devkitA64 libnx switch-sdl2 switch-mesa switch-libdrm_nouveau
```

## Build and install

1. Go to the root folder of the repository.
2. Enter `python configure.py` (`--release` for a release build).
3. Enter `ninja switch`.

That writes `build/switch/halo.nro`, with the game image (`halo_guest.elf`)
inside its RomFS. Copy it to `sdmc:/haloce-nx/` with an Xbox disc image
beside it; the root [README](../../README.md#install) has the rest. A
`halo_guest.elf` beside the NRO is loaded in place of the one inside, for
testing a build.

The game's own view of the console is that of a 1280x720 handheld or a
1920x1080 docked display, and it resizes on its own if the console is docked
or undocked while the game runs.

## Releasing (nx-mod/haloce-nx)

1. Bump `nacp_version` in `tools/switch_build.py` and commit it.
2. Tag the commit `v<nacp_version>` and push the tag:
   `git tag v1.1.0 && git push origin v1.1.0`.
3. The CI (`.github/workflows/switch.yml`) builds the release and debug
   builds and publishes them as the release `build-<run number>`, titled
   with the version: the form the game's updater reads
   (`host_update.c`). It stops with an error if the tag and
   `nacp_version` differ.

Only release tags build in this repository, and only the Switch.

## Game data

The game needs the `maps/` folder of an Xbox disc image (`.xiso` or `.iso`)
of any version of the game; neither is included. Put the image in the
program's folder (`sdmc:/haloce-nx/` by default: the folder `halo.nro` is
started from). The first start copies `maps/` and the movies (`bink/`) out
of it, about 1.7 GB; a `maps/` folder already there is used as it is.

The saves go in the same folder, named for what they hold: `profiles/`
(the Xbox's u:), `saves/` (z:) and `cache/` (z:'s map caches). The logs go
in `logs/`.

## The memory model

This is the part of the port that is not like the others, and it is worth
writing down because it shaped the host and because it is the one thing here
that was established by experiment rather than by reading a manual.

Everything the guest can address has to be below 4 GB, because the guest's
pointers are 32 bits. That is not a restriction the console imposes: it is
the same restriction the game's data formats impose, and it is why the
guest image is linked at `HALO_GUEST_IMAGE_BASE` (0x40000000) with the Xbox
contiguous window at 0x80000000 (port/android/include/halo_android_abi.h).

The answers below were measured on a console running Horizon 21.2.0 with
Atmosphère 1.10.2, by the programs in `port/switch/probe`. They are:

- The address space below 4 GB is free. There is nothing in the way.
- `svcMapPhysicalMemory` (SVC 0x2C), which sounds like the call this needs,
  is refused on this firmware. `svcMapMemory` (SVC 0x05) instead places
  memory at an address the caller chooses.
- No mapping is ever both writable and executable.
  `svcSetMemoryPermission` answers 0xd801 for `Perm_X`, as its documentation
  says it must. Executable memory is a separate kind, made with
  `svcCreateCodeMemory` (0x4B) and `svcControlCodeMemory` (0x4C).
- A code memory object has a writable owner view and a read-execute slave
  view, and the two are one memory: bytes written through one are read
  through the other.

So the guest image cannot be given one writable-and-executable mapping, the
way the Android host gives it one and then `mprotect`s it. `host_mman.c`
splits the image by permission instead, placing each part at the offset the
guest was linked for:

- executable pages become a code memory slave at `0x40000000` plus their
  offset, filled in through a writable view at a scratch address;
- `.data`, `.bss`, the guest's arenas and the Xbox window become ordinary
  `svcMapMemory` regions.

The guest is not changed by this. It still sees one contiguous range,
because the segments land at exactly the addresses it was linked for.

`mprotect` is correspondingly not cheap here: adding `PROT_EXEC` copies the
range into a code memory object, maps it, and attends to both caches, which
for a large segment is tens of milliseconds. The loader does this a few times
while loading the image and never again; the calls it makes during play are
between read and write, and those are a single syscall on a page.

## SDL2, and why there is a shim

The guest was built against SDL3's headers. There is no SDL3 for the Switch:
devkitPro's SDL has `switch-sdl-3.2` and `switch-sdl-3.4` branches, but no
packaged, working port, and the repository of Switch examples this port
started from uses `pkg-config sdl2` throughout.

So `host_sdl2.c` answers the guest's SDL3 calls with devkitPro's SDL2, which
is the same arrangement the Anbernic port uses for its own reasons. SDL
objects are 64-bit pointers that the guest cannot hold, so the guest gets
small integer handles into a table. Most of the values agree between the two
versions; `host_sdl3_events.c` translates the ones that do not.

The graphics are deko3d's (`port/switch/guest/d3d8_dk.c`, `host_dk.c`):
the game's Direct3D is translated into deko3d command lists, its shaders into
GLSL compiled on the console by UAM and cached on the card. The game draws its
480-line picture at the render resolution (`display.render_resolution`: 720
lines in handheld, 1080 docked).

## Internet play

`host_net.c` answers the game's network calls with libnx's BSD sockets
(the console's bsd service), as `posix_net.c` does on Linux. The
differences are the console's: socket addresses carry a length byte and an
8-bit family, Linux's `MSG_NOSIGNAL` is refused, and socket buffers come out
of one budget per process, which `host_main.c` starts larger than libnx's
default. Internet play's brokers (`brokers.txt`) ride in the NRO and are
written beside it at start. A host may start alone
(`network.host_minimum_players`); others join the game under way.

## What the port does not have

Three things in the Android host are absent here, all of them development
aids rather than part of the game, and all for the same reason: libnx
exposes no syscall for installing a signal handler.

- The crash handler in `host_memory.c`, which logged the faulting address,
  the program counter and the guest's frame chain.
- The thread sampler in `host_debug.c`, which `debug.sample_seconds` in
  `config.toml` starts. It is worth having; see each file for how it could be
  built on the console's thread context syscalls instead.
- `host_probe.c`, which walks the memory window looking for the pointers
  that would have to move if the window moved. Its question is already
  answered, and the answer is in port/android/README.md.

## The probes

`port/switch/probe` holds the programs the memory model above was measured
with. They are standalone devkitA64 projects, not part of the port's build,
and they are kept because the answers they gave are the reason the port is
shaped the way it is. Each writes its findings to `sdmc:/`, so they can be
read back off the card:

```
cd port/switch/probe/mem
make
```

`mem` is at its fifth version; the earlier ones found the same things less
directly, and the comments say what each was for.