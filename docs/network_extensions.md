# Network extensions

Branch `custom_campaigns` (2026-11-14). Built: `port/linux/game/network_extensions.c`, tested by `tools/test_network_extensions.py`; not yet tried between two machines. A game on an Xbox multiplayer map of `custom_maps` requires `custom_xbox_multiplayer_maps`; `debug.network_extensions_require` requires any capability, for testing.

SlipstreamCE (and any build that adopts this) tells the machines of a game
what it can do beyond the shared OpenCE protocol, as a list of capabilities
named by strings. Builds need not agree on the list: a capability a machine
does not know is ignored. A game that needs a capability keeps out the
machines that do not have it, with a notice saying why; a build that does
not speak extensions (OpenCE) plays every game that needs none, as it does
now.

Nothing an OpenCE machine sends or expects changes: the join request, the
shared messages and `HALO_PORT_NETWORK_VERSION` stay as they are.

## What OpenCE does with it (checked against `upstream/main` 1519c65fa)

- A game packet of a type it does not know is logged and dropped, the
  connection kept: the client's packet switch (`network_client_message_handler.c`,
  "unknown packet type received") and the host's
  (`network_server_message_handler.c`, "bad or inappropriate packet type
  received from a client"). Stock types are 0 to 34
  (`enum network_game_message_type`, `network_messages.h`, the same upstream);
  a packet's type is the last byte of its message.
- The join request cannot carry more: its layout is the shared agreement's,
  checked by size (`DEFINE_NETWORK_GAME_MESSAGE(message_client_join_game_request, ...)`).
- A rejection carries a code alone, and a code a client does not know is
  shown as the generic "failed to join". Text reaches an OpenCE player only
  as a notice (`_distributed_message_notice`, a chat line), which upstream
  has.

## The message

One game packet type, **255** (`_message_extensions`, the highest a packet's
type byte holds, past any stock or upstream type), in both directions, over
the connection's stream only (never a datagram). Its bytes, before the type
byte that ends every packet:

| Bytes | Field |
|---|---|
| 4 | magic `SLXT` (a packet of type 255 without it is not this one, and is dropped) |
| 1 | format, 1 |
| 1 | kind: 0 the host's announcement, 1 a client's answer |
| 1 | count of entries |
| ... | the entries |

Each entry:

| Bytes | Field |
|---|---|
| 1 | key length, 1 to 63 |
| n | key: ASCII letters, digits, `_` and `.` (`custom_xbox_multiplayer_maps`) |
| 2 | version (little-endian): what of the capability the sender has |
| 1 | flags: bit 0 *required* (the host's announcement: the game needs it) |
| 1 | text length, 0 to 127 |
| n | text: printable ASCII, what a machine without the capability is told (the host's announcement; empty in an answer) |

A machine reads the entries it knows and skips the rest, but keeps the text
of one the game requires that it has not; a malformed message (a length past
its end, a bad key) is dropped whole.

## The handshake

1. A client joins as now.
2. When the host accepts a machine (`_message_server_machine_accepted`) it
   sends it its announcement: every capability it has, each marked
   *required* if the current game needs it. When the game's settings change
   what is required (another map), it sends every machine its announcement
   again, and checks each again.
3. A client that speaks extensions answers each announcement with every
   capability it has. A feature is used with a machine only when both have
   it (`network_extension_agreed`).
4. **A SlipstreamCE client that cannot play the game** (the announcement
   requires a capability it does not know, or a newer version of one than
   it has) leaves the game itself, with the entry's text in an error box
   (or the capability's key, with no text), as map sharing leaves a map it
   cannot download. It still answers, so the host's records are right. A
   notice from the host would not reach it: a client takes distributed
   messages in a game only (`network_distributed_handle_message`'s
   `game_in_progress`), in OpenCE too, so an OpenCE client is shown the
   generic "Failed to join" alone.
5. **An OpenCE host:** no announcement comes. The client turns every
   extension off for that game, and plays as an OpenCE client.
6. **A client that cannot play the game:** when the game requires a
   capability, the host waits for the machine's answer for
   `NETWORK_EXTENSIONS_ANSWER_SECONDS` (5) after its announcement. If none
   comes (an OpenCE client drops the announcement), or the answer lacks a
   required capability (or has an older version than required), the host
   sends that machine a notice with the capability's own text, and removes it three seconds later ("You need a
   client with support for custom OG Xbox multiplayer maps") and removes it
   (`network_game_server_kick_machine`). Until it has answered with every
   required capability, the host holds the game's settings back from it, so
   that it never learns a map it cannot play and fails on nothing of its own
   first (an OpenCE client would show its own "You don't have the map ...
   The host cannot share this map." before the notice came); a SlipstreamCE
   client is sent them as soon as its answer comes, a round trip after its
   acceptance. Map sharing does not offer such a machine a map only such
   clients can play.
7. A game that requires nothing removes no one: OpenCE clients join
   SlipstreamCE hosts as now.

## The code

`port/linux/game/network_extensions.c` (`.h`):

- the table of this build's capabilities: key, version, the notice for a
  machine without it, and whether the current game requires it (a function
  of the feature's);
- the announcement and answer, built and read (the packet framed by hand,
  `create_message`, outside the shared message tables);
- the host's record of each machine: announced when, answered, its
  capabilities; checked each tick, and on each change of settings;
- the client's record of its host's capabilities, emptied with each new
  connection;
- `network_extension_agreed(machine_index, key)` (host) and
  `network_extension_host_has(key)` (client), for the features.

Its first user: Xbox multiplayer maps in `custom_maps`
(`custom_xbox_multiplayer_maps`, docs/custom_edition_caches.md, "Xbox
multiplayer maps"), which an OpenCE machine cannot load
(`custom_edition_cache_present` refuses a cache that is not Custom
Edition's, and deletes such a download). The host requires it while its
game's map is one (`custom_xbox_multiplayer_maps_required`, map_share.c),
and map sharing offers such a map only to machines that have it.

## To offer upstream

The packet type and the format, so that OpenCE reserves 255 and can adopt
the mechanism.
