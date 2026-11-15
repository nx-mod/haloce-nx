/*
HALO_HOST_DASHBOARD.H

The host's screen (port/linux/game/host_dashboard.c): hosting from the menus,
the app is a dedicated server (server_mode.h's halo_server_host_begin, the
director of port/linux/game/server_director.c) with no player of its own,
and this screen is drawn over everything in place of the game, with the
server's controls.
*/

#ifndef __HALO_HOST_DASHBOARD_H
#define __HALO_HOST_DASHBOARD_H

#include <stddef.h>

/* hosting begun (Multiplayer's HOST SERVER, a playlist's A) with the
rotation (map:gametype entries separated by commas; NULL for
server.rotation's): FALSE with why in error if it cannot be now (a network
game being played, a map other than the main menu's, no round of it that
can be played) */
int host_dashboard_start(char const *rotation, char *error, size_t error_size);

/* a controller's buttons at this read (halo_overlay_menu.h's bits): nonzero
while the app hosts, when the game gets none of the controller */
unsigned char host_dashboard_take_pad(short port, unsigned long buttons);

/* the game's side (source/): */
/* each frame, after the input (main.c) */
void host_dashboard_update(void);
/* over the whole screen, under the overlay menu (interface.c) */
void host_dashboard_draw(void);

#endif
