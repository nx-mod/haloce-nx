# Map transfers: Workshop + host for MCC, CE sharing on the native host

Plan, 2026-11-13. Branch to be decided (not `bots-v2`). Nothing here changes
the wire: every stage must keep playing with upstream OpenCE builds at
network version 44.

## Goals

1. A joiner on an MCC map it lacks downloads the package from the host and
   from Steam's Workshop at once, automatically, when both hold the same
   version, so the host's upload carries only part of it. Otherwise from the
   host alone, as now.
2. Custom Edition map sharing (its own connections, port 5153) runs on the
   native host's threads: no frame rate, no stop while a map loads, uploads at
   the player's cap, downloads at whatever the line gives.
3. The internet tunnel does not cap a map-sharing stream below the uploader's
   cap.

## Caps

- `network.map_share_upload` (host, default 1024 KB/s, 16-20480) and
  `network.map_seed_upload` (a player seeding CE pieces, default 256 KB/s,
  16-10240) cap uploads only.
- Downloads have no cap (`network.map_download` is on/off). They take what the
  sources and the line give. The plan keeps it so.

## What OpenCE pins down (do not change)

- Network version 44 (`HALO_PORT_NETWORK_VERSION`).
- CE sharing: port 5153, the frames, `FRAME_MAGIC`/`FRAME_VERSION` (a hello
  of another version is closed: `frame_hello`), 256 KB pieces, SHA-256.
- MCC sharing: messages on the game's reliable connection
  (`_distributed_message_mcc_share_request/response`); chunks of at most 3072
  bytes; the host serves each file in order (an out-of-order request is
  ignored: `host_request_chunk`) and queues at most 32 requests a transfer
  (`MCC_SHARE_WINDOW * 2`; more stalls the transfer); `reserved` and unused
  `flags` must be zero.
- So MCC host uploads stay in the guest, on the game connection, a frame at a
  time. Moving them, larger chunks, or several MCC sources are protocol
  changes: an OpenCE PR for network version 45, not this plan.

## Part A: MCC from the host and the Workshop at once (joiner side)

Built per file, not front/back within a file: OpenCE's MCC protocol already
lets a joiner skip a whole resource map (`bitmaps.map`, `sounds.map`) it has
copied locally and checked (`MCC_SHARE_OP_HAVE`, `host_request_have`), and
then ends with the ordinary `DONE(1)`, so the host's finish and admission run
as always. The host's order is the level's map, `bitmaps.map`, `sounds.map`,
then metadata and credits; the resource maps are most of a package.

- Native host (`steam_downloads.c`, "a joiner's fetch"): the guest names the
  Workshop item and the resource files of the host's list (leaf, size,
  SHA-256). A thread of its own looks the item up by id, reads its manifest,
  finds each file by leaf name and size, downloads it to
  `mcc_staging/steam_fetch`, and checks it against the host's SHA-256
  (`host_sha256_file`, the CPU's SHA instructions). Imports:
  `host_steam_fetch_start/_status/_skip/_stop`. The Steam threads start
  without the Workshop screen (`threads_start`).
- MCC's shared sets (the base resources no host sends) are queued on the
  existing downloader when missing (`host_steam_shared_fetch/_state`); the
  staged download's identity check waits while they download.
- Guest session (`mcc_share_session.c`): a new optional `reuse_wait`
  callback. When the host's turn comes to a resource the local lookup did not
  find, a nonzero answer holds the host's request for it (heartbeats keep the
  host's transfer), asked again each update, at most `MCC_SHARE_HOLD_LIMIT`
  (60 s, under the host's 80 s without progress). The host's rate is measured
  for the decision.
- Guest adapter (`mcc_share.c`): starts the fetch when the download is
  accepted (Workshop item, Steam sign-in); `reuse_file` returns the
  Workshop's file once ready; `reuse_wait` holds while the Workshop would
  finish sooner than the host (and under 55 s), else tells the fetch to skip
  the file and the host sends it. Progress shows the Workshop's part.
- Within what OpenCE allows: the joiner keeps 32 chunk requests in flight
  (every host queues 32); our hosts keep a served file open across chunks
  (it was opened, sized and closed for every 3 KB); a local copy into the
  stage runs up to 4 MB or 12 ms an update (was 1 MB).
- Tests: `tools/test_mcc_share.py` cases hold_reuse, hold_limit,
  hold_declined, window, with negative controls; `tools/test_steam_client.py`
  builds the host code with -Werror.

## Part B: CE map sharing off the frame

Built as the same engine on a thread of its own, not a port to native-host
code. On Android, the Switch and the PS5 the guest is the game's image in
the app's own process, and its threads are the system's (host_thread_create:
the internet play tunnel and the CE maps' hashing already run on them). What
tied map sharing to the frame rate was that its connections were read,
served and written from map_share_update, once a frame, not where its code
lives; a port to native-host code would have rewritten ~3600 lines of a
protocol OpenCE players must still speak byte for byte. The thread keeps the
very code that sends, so the wire cannot change.

- map_share.c: `map_share_connections_thread` waits on the listener and the
  connections (select, 3 ms while pieces go either way, 25 ms when quiet),
  then accepts, reads, answers, serves (the upload's allowance by the clock)
  and writes. The game's thread keeps the messages over the game's
  connection, the host's offers, sources and tracker, a download's start,
  end and install, and the screen.
- One recursive lock (`map_share_lock`) is held by each side while it looks
  at what they share; every entry point takes it, and the catalog's changes
  (map_share_files.c) too.
- What the thread needs of the game (host or not, sending or not, the
  upload's cap, the game's machines' addresses) the game's thread writes down
  at each entry (`map_share_facts`).
- The thread uses plain sockets (xnet.c's Winsock: thread-local errors, its
  socket port cache by socket), not the game's transport endpoints, which
  allocate from the game's allocator (no lock). xnet.c's accept still turns
  a tunnel stand-in into the peer's address.
- What only the game's thread may do it leaves for it: a write that fails
  (download_fail sends on the game's connection, aborts the game) is a
  reason the next update fails the download with; a file whose pieces'
  hashes came (opening it may start the hashing thread) is `_file_opening`,
  opened by the next update.
- If the thread cannot be started, the game's thread runs the connections
  each frame as before.
- Tests: the map sharing harness (read, restart, trust, names) follows the
  sockets, the deferred open and the locked entries.

## Part C: the tunnel does not cap map sharing

All local: KCP advertises its window in every packet and each side limits
only its own sending, so no OpenCE player sees a difference but speed.

### C1. Map-sharing streams get their own settings

- `p2p.c`: streams whose port is 5153 get a send window sized to the cap
  times the stream's smoothed round trip (`rx_srtt`) times 2, between 128 and
  1024 messages; a receive window and `STREAM_BUFFER_SIZE` to match; more than
  8 KB read a pass (`stream_readable`); larger messages. The game's streams
  keep today's settings (MCC chunks ride the game's stream: unchanged).
- An OpenCE receiver advertises 256 segments, so our sender reaches about
  300 KB in flight to it, our receivers more.
- Memory: a 1024-message window is about 1.2 MB a stream on the guest's heap:
  at most a few large streams at once (Switch first).

### C2. Back off before the line does

- In `p2p.c`, per map-sharing stream: keep the lowest round trip seen; when
  the smoothed one rises well above it (more than 50 ms or 2x) or resends
  climb, halve the stream's send rate and grow it back slowly. The uploader's
  cap stays the ceiling.

### C3. The game first

- While a game is being played on this machine, map uploads run at a part of
  the cap (setting, default 25%); in the lobby and postgame, the whole cap.
  The guest tells the engine (B4) and the MCC host serve (A6).

### C tests

- Harness: window sizing from cap and round trip, the back-off on a rising
  round trip, the game's streams unchanged.
- Device: a CE map over the internet between two phones with the cap at
  5 MB/s; the game's ping in a match while a joiner downloads.

## Order

1. A1-A6 (Workshop + host for MCC): the largest gain, all joiner side.
2. C1 (tunnel settings for 5153): small, lifts CE over the internet now.
3. B (the connections on their own thread).
4. C2-C3 (back-off, game first), before raising the default caps.

Each stage: its own commit, Android/Switch/PS5 builds, the harness compared
with main, and the log lines above for the device test.

## Later, with OpenCE (network version 45)

- MCC transfers on the native host's connections (as CE's), out of the game's
  reliable stream; larger chunks; several sources.
- A capability field in both hellos, so later changes can fall back.
