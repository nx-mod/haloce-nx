/*
HALO_OVERLAY_MENU.H

The game's own overlay menu (port/linux/game/overlay_menu.c), as the
platform layer (port/linux/src) sees it: the controllers' buttons it reads,
whether it is open (when the game gets none of the controllers, the
keyboard, the mouse or the touchscreen), and the touchscreen's ways to
open it.
*/

#ifndef HALO_OVERLAY_MENU_H
#define HALO_OVERLAY_MENU_H

/* a controller's buttons as the overlay reads them: a bit for each */
enum halo_overlay_pad_button
{
	/* the d-pad, or the left stick pushed most of the way */
	HALO_OVERLAY_PAD_UP,
	HALO_OVERLAY_PAD_DOWN,
	HALO_OVERLAY_PAD_LEFT,
	HALO_OVERLAY_PAD_RIGHT,
	HALO_OVERLAY_PAD_A,
	HALO_OVERLAY_PAD_B,
	HALO_OVERLAY_PAD_START,
	HALO_OVERLAY_PAD_BACK,
	/* the shoulders (white and black) and the d-pad's down alone (not the
	stick's), which together open and close it */
	HALO_OVERLAY_PAD_LEFT_SHOULDER,
	HALO_OVERLAY_PAD_RIGHT_SHOULDER,
	HALO_OVERLAY_PAD_DPAD_DOWN,
	NUMBER_OF_HALO_OVERLAY_PAD_BUTTONS
};

/* a controller's buttons at this read (port 0 has the keyboard's and the
touch controls' in it): opens the overlay when L, R and the d-pad's down are held;
nonzero while it is open, when the game gets none of the controller */
unsigned char overlay_menu_take_pad(short port, unsigned long buttons);
/* nonzero while the overlay is open */
unsigned char overlay_menu_open(void);
/* opens it, or closes it if it is open (three or four fingers on Android's
touchscreen, or on its touch controls); taken at the next frame */
void overlay_menu_toggle_request(void);

/* the game's side (source/): */
struct halo_ui_pointer;
/* the menus' pointer while the overlay is open, in the menus' centred 640
(ui_widget.c), which takes it instead of the menus */
void overlay_menu_pointer(struct halo_ui_pointer const *pointer);
/* each frame, after the input (main.c) */
void overlay_menu_update(void);
/* over the whole screen, last (interface.c) */
void overlay_menu_draw(void);

#endif
