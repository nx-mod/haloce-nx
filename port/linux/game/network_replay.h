/*
NETWORK_REPLAY.H

The host's replay recording (port/linux/REPLAY.md, phase 0): what a client
that sees everything would be sent, written to a file instead.
*/

#ifndef __NETWORK_REPLAY_H
#define __NETWORK_REPLAY_H

struct observer_result;

/* a record's kind (REPLAY.md, "The file") */
enum
{
	/* the recorder's datagram: a batch of the distributed netcode's
	unreliable messages, as a client is sent one */
	_network_replay_datagram = 1,
	/* a distributed message the host sent every client reliably */
	_network_replay_reliable,
	/* ... or the recorder's machine alone, reliably */
	_network_replay_reliable_to_machine,
	/* a message of the game's own (network_server_message_handler.c) the
	host sent every machine in the game */
	_network_replay_game_message,
	/* (format 2, the first record) the game as the host had it at its first
	tick, struct network_game: its map, game type, players and random seed,
	which a playback loads the game from (network_replay_playback.c) */
	_network_replay_game,
	/* (a client's recording) its own players' input of a tick, which the host
	never sends it: for each, its tick (32 bits) and the action as the host
	relays a player's (network_distributed.c's network_distributed_own_actions) */
	_network_replay_own_actions,

	NUMBER_OF_NETWORK_REPLAY_RECORDS
};

/* (the host) whether a replay is being recorded */
boolean network_replay_recording(void);

/* (the host) once a tick, before anything is sent: starts the recording at
the game's first tick when network.record_replays is on */
void network_replay_host_tick(void);

/* (the host) a record of the kind, at this tick, when recording */
void network_replay_record(byte kind, void const *data, long size);

/* the recording's end: the map is unloaded (or a new game starts) */
void network_replay_stop(void);

/* (a client) once a tick: starts the recording of what it reads of its
host at its first tick when network.record_replays is on (not in a
replay's playback) */
void network_replay_client_tick(void);
/* (a client) whether it is recording */
boolean network_replay_recording_as_client(void);
/* (a client) a message it read of its host, as it came: recorded when it
is recording (network_client_manager.c) */
void network_replay_record_received(void const *message, long size, boolean unreliable);

/* the folder replays are written to (the data folder's replays/), into
path */
void network_replay_folder(char *path, long size);

/* the replays in that folder: how many, and their bytes (the Theater
screen, menu_functions.c) */
void network_replay_summary(long *count, unsigned long long *bytes);

/* the replays in the folder, newest first (their names begin with when
they were recorded), at most `maximum`, each name in names[index] (up to
NETWORK_REPLAY_NAME_SIZE bytes); how many */
#define NETWORK_REPLAY_NAME_SIZE 96
long network_replay_list(char (*names)[NETWORK_REPLAY_NAME_SIZE], long maximum);

/* what a replay is, from its header */
struct network_replay_info
{
	/* a replay's header (else why), and one this build plays (else why) */
	boolean valid;
	boolean playable;
	char map[48];
	char variant[16];
	/* when (2026-10-28 02:16), and shorter (10-28 02:16) */
	char date[24];
	char short_date[16];
	unsigned long long bytes;
	/* (format 4) its length, the server's name, the recording player's,
	and whether a client recorded it */
	boolean has_metadata;
	long seconds;
	char server[65];
	char player[33];
	boolean client;
	char why[96];
};

/* the replay of that name in the folder, from its header; whether this
build can play it */
boolean network_replay_info(char const *name, struct network_replay_info *info);

/* what it is, in lines of text: its map, game type, when, length, server,
player and size, and why it cannot be played */
void network_replay_info_text(struct network_replay_info const *info, char *text, long size);

/* the two of them: what the replay of that name in the folder is, in lines
of text; whether this build can play it */
boolean network_replay_describe(char const *name, char *text, long size);

/* the replay of that name in the folder deleted; FALSE if it could not be */
boolean network_replay_delete(char const *name);

/* ---------- the replay page (network_replay_page.c): the Switch's and the
PS5's, on the local network while the Theater is up */

/* the Theater's, each frame: the page while it is up */
void network_replay_page_wanted(void);
/* main_loop's, each frame */
void network_replay_page_update(void);
/* the page's address ("http://192.168.2.173:8090/"), "" while none is served */
char const *network_replay_page_address(void);

/* ---------- playback (network_replay_playback.c) */

/* plays the replay at path (its game is loaded at the next frame); FALSE,
with why in why, for one that cannot be played (not a replay, an older
format, another network version) */
boolean network_replay_watch(char const *path, char *why, long why_size);

/* whether a replay is being played (or its game loaded) */
boolean network_replay_playing(void);

/* whether a skip ahead is under way, its dimmed frame drawn: no frame is
drawn or shown until it is there, the screen held (main.c) */
boolean network_replay_frozen(void);

/* the most ticks a frame may run (a network client's, ticks, given): more
while a skip ahead plays fast, never past where it goes (game_time.c) */
long network_replay_ticks_per_frame(long ticks);

/* before each tick a replay's playback runs, the records of the ticks the
clock has reached, as a client reads its host's between ticks (game_time.c):
a frame may run several */
void network_replay_playback_tick(void);

/* the playback's view made for this frame (main.c, before the sound and
the windows: each asks network_replay_camera for it) */
void network_replay_playback_frame(void);

/* the view, for the frame's first window (main.c) and the sound's listener
(sound_manager.c); NULL when no replay plays, or before it has one */
struct observer_result const *network_replay_camera(void);

/* the player watched in a replay's playback (a datum index), NONE when no
replay plays: the announcer speaks to them (game_engine.c), and the view
through their eyes draws them as a player's own (render_objects.c) */
long network_replay_watched_player(void);

/* whether the view is through the watched player's eyes, first person (not
a seat's third-person view): their body hidden (render_objects.c), and
their arms and weapon drawn as local player 0's (first_person_weapons.c,
render_particles.c) */
boolean network_replay_viewing_eyes(void);

/* the line at the top of the screen (who is watched, the view, the speed)
and the buttons' bar at the bottom (interface.c) */
void network_replay_playback_draw(void);

#endif
