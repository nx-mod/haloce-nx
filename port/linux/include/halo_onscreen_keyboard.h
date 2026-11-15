/*
HALO_ONSCREEN_KEYBOARD.H

The on-screen keyboard (port/linux/game/onscreen_keyboard.c), OpenCE's
text chat's, in the menus' look: a QWERTY page and a symbols page of ten
columns, and a last row of wide keys (capitals, the page, space, delete,
and done). A controller moves through its keys and presses the one
picked, a tap presses the one under it. Its callers type with it: the
text chat in a game (network_chat.c) and the menus' text fields
(virtual_keyboard.c's free text, menu_functions.c's text fields); each
draws it where it goes, and keeps its own text.
*/

#ifndef __HALO_ONSCREEN_KEYBOARD_H
#define __HALO_ONSCREEN_KEYBOARD_H

#include "cseries.h"

enum
{
	ONSCREEN_KEYBOARD_COLUMNS = 10,
	ONSCREEN_KEYBOARD_ROWS = 5,
	/* the last row's keys are this many columns wide */
	ONSCREEN_KEYBOARD_WIDE_COLUMNS = 2,
	/* a direction held: the key moves once, then again after the first
	delay and at each repeat */
	ONSCREEN_KEYBOARD_REPEAT_FIRST_MILLISECONDS = 350,
	ONSCREEN_KEYBOARD_REPEAT_MILLISECONDS = 110,
};

/* the last row's wide keys */
enum
{
	_onscreen_key_shift,
	_onscreen_key_symbols,
	_onscreen_key_space,
	_onscreen_key_delete,
	_onscreen_key_done,
	NUMBER_OF_ONSCREEN_WIDE_KEYS
};

/* what a key's press did (onscreen_keyboard_press) */
enum
{
	_onscreen_press_none,
	/* a character to type: its in typed */
	_onscreen_press_character,
	_onscreen_press_delete,
	_onscreen_press_done,
};

struct onscreen_keyboard
{
	/* the key picked */
	short row;
	short column;
	/* capitals; the symbols' page */
	boolean shift;
	boolean symbols;
	/* (system_milliseconds) when a held direction moves the key again */
	unsigned long repeat_at;
	/* where its keys were last drawn, in the drawing's own units */
	boolean drawn;
	rectangle2d keys;
};

/* a keyboard as it opens: on Q, no capitals, the letters */
void onscreen_keyboard_reset(struct onscreen_keyboard *keyboard);

/* the key picked moved by a direction held (rows down, columns right; both
0 when none is): at once as it is pressed, then again after a while and
quickly */
void onscreen_keyboard_steer(struct onscreen_keyboard *keyboard, short rows_down, short columns_right,
	unsigned long now);

/* the key picked moved by rows down and columns right, round its edges */
void onscreen_keyboard_move(struct onscreen_keyboard *keyboard, short rows_down, short columns_right);

/* the key picked pressed: a character (typed[0]), a space is one, delete,
or done; capitals and the page change by themselves (_onscreen_press_none) */
short onscreen_keyboard_press(struct onscreen_keyboard *keyboard, char *typed);

/* the key under a point of where it was last drawn picked: FALSE if none is */
boolean onscreen_keyboard_pick_at(struct onscreen_keyboard *keyboard, short x, short y);

/* the keyboard drawn with its keys in keys (the drawing's units: a menu's
640x480, a game's window), in the font, its last key named done_label
("DONE", "SEND"); translucent, the game showing through its panel and keys */
void onscreen_keyboard_draw(struct onscreen_keyboard *keyboard, rectangle2d const *keys, long font_index,
	short line_height, char const *done_label, boolean translucent);

/* the text typed, in its box: its end where it is too long for it, a
password's as stars, the caret blinking after it */
void onscreen_keyboard_draw_field(rectangle2d const *box, long font_index, short line_height, char const *text,
	boolean masked, boolean translucent);

/* what the keyboard's screen shows: its caption, the text typed (a
password's as stars), its last key's name, whether the game shows through
it, and the buttons under it (each a hint's text, its picture's token first,
and its words alone, which its width is measured by) */
struct onscreen_keyboard_screen
{
	char const *caption;
	char const *text;
	boolean masked;
	char const *done_label;
	boolean translucent;
	wchar_t const *const *hints;
	char const *const *hint_words;
	short hint_count;
};

/* the keyboard's whole screen, the menus' text fields' and the text chat's
alike, laid out in area as in the menus' 640x480 (area is that, scaled):
the caption, the text's box, the keys and the buttons; in the menus' fonts
(ui\large_ui, ui\small_ui), else fallback_font */
void onscreen_keyboard_draw_screen(struct onscreen_keyboard *keyboard, rectangle2d const *area,
	struct onscreen_keyboard_screen const *screen, long fallback_font);

#endif
