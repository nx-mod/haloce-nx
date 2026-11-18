/*
NETWORK_EXTENSIONS.H

What a machine can do beyond the shared OpenCE protocol, told as a list of
capabilities named by strings (docs/network_extensions.md): the host
announces its own to each machine it accepts, marking those its game
requires, and a client that speaks extensions answers with its own. A
machine that cannot play the game (no answer: an OpenCE client; or one
without a capability the game requires) is told why and removed. A client
of a host that announces nothing (an OpenCE host) uses no extension.

The message is game packet 255 (NETWORK_EXTENSIONS_PACKET_TYPE), which a
build without it logs and drops.
*/

#ifndef __NETWORK_EXTENSIONS_H
#define __NETWORK_EXTENSIONS_H

/* ---------- constants */

/* the highest type a game packet's type byte holds: past any stock (0 to
34) or upstream type */
#define NETWORK_EXTENSIONS_PACKET_TYPE 255

/* the capabilities of this build (network_extensions.c's table) */
#define NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS "custom_xbox_multiplayer_maps"
/* files over a connection of their own (port/android/host/host_file_share.c,
docs/file_share_tcp_plan.md) */
#define NETWORK_CAPABILITY_FILE_SHARE_TCP "file_share_tcp"

/* ---------- prototypes/NETWORK_EXTENSIONS.C */

/* the host: a machine accepted into the game (network_server_message_handler.c,
after its acceptance): its announcement sent */
void network_extensions_host_machine_accepted(
	long machine_index);
/* the host, each update (network_server_manager.c, lobby and game): a
change of what the game requires announced again; a machine that has not
answered in time, or has answered without a required capability, told and
removed */
void network_extensions_host_update(
	void);
/* the host: whether the game's settings are held from the machine: its game
requires a capability, and the machine has not answered yet (or answered
without it, and is being removed). It is sent them once it has answered with
every capability required (network_extensions_server_send_settings), so
that a machine that cannot play the game never learns its map, and fails on
nothing of its own before it is told why. */
boolean network_extensions_host_machine_held(
	long machine_index);
/* the host: a client's answer, a packet of NETWORK_EXTENSIONS_PACKET_TYPE
(its bytes after the message header, the type byte included) */
void network_extensions_host_handle(
	long machine_index,
	void const *data,
	long size);
/* the host: whether the machine has answered with the capability `key` of
`version` or later (so a feature may use it with that machine) */
boolean network_extension_agreed(
	long machine_index,
	char const *key,
	unsigned short version);

/* the client: a new game joined, of a host whose capabilities are not known
(none until it announces them) */
void network_extensions_client_reset(
	void);
/* the client: the host's announcement (as network_extensions_host_handle
takes it): kept, and answered with this build's capabilities */
void network_extensions_client_handle(
	void const *data,
	long size);
/* the client: whether the host has announced the capability `key` of
`version` or later; and whether its game requires it */
boolean network_extension_host_has(
	char const *key,
	unsigned short version);
boolean network_extension_host_requires(
	char const *key);

/* ---------- data for a capability (kind 2: the key, then its bytes)

Sent only where the capability is agreed: the host to a machine that answered
with it, a client to a host that announced it; received the same way, and
handed to the capability's own handler (network_extensions.c's table). */
boolean network_extensions_send_data(
	long machine_index,
	char const *key,
	void const *payload,
	long size);

#endif
