/*
ONSCREEN_KEYBOARD.C (test)

The on-screen keyboard's keys (port/linux/game/onscreen_keyboard.c, which
the text chat and the menus' text fields type on): a key's character, the
key picked moved round its edges, a tap's key, a press's action, and a held
direction's repeats. test_onscreen_keyboard.py takes its constants and
structure (config.inc) and its code (under_test.inc).
*/

#include "harness.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, low, high) ((x) < (low) ? (low) : (x) > (high) ? (high) : (x))
typedef struct { short y0, x0, y1, x1; } rectangle2d;

#include "config.inc"
#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct onscreen_keyboard keyboard;
	char typed;

	/* a key's character: a letter in capitals with shift, nothing else
	changed; the symbols' page; none on the last row, or off the keyboard */
	CASE("keyboard-character")
	{
		onscreen_keyboard_reset(&keyboard);
		CHECK(onscreen_character(&keyboard, 1, 0) == 'q', "'%c'", onscreen_character(&keyboard, 1, 0));
		keyboard.shift = TRUE;
		CHECK(onscreen_character(&keyboard, 1, 0) == 'Q', "'%c'", onscreen_character(&keyboard, 1, 0));
		CHECK(onscreen_character(&keyboard, 0, 3) == '4', "a digit changed by shift");
		CHECK(onscreen_character(&keyboard, 3, 9) == '?', "a sign changed by shift");
		keyboard.shift = FALSE;
		keyboard.symbols = TRUE;
		CHECK(onscreen_character(&keyboard, 1, 0) == '!', "the symbols' page: '%c'", onscreen_character(&keyboard, 1, 0));
		CHECK(onscreen_character(&keyboard, ONSCREEN_KEYBOARD_ROWS - 1, 0) == 0, "the last row has a character");
		CHECK(onscreen_character(&keyboard, -1, 0) == 0 && onscreen_character(&keyboard, 0, ONSCREEN_KEYBOARD_COLUMNS) == 0,
			"a key off the keyboard");
	}

	/* moving: round the edges; on the last row a wide key at a time, and its
	first column */
	CASE("keyboard-move")
	{
		onscreen_keyboard_reset(&keyboard);
		keyboard.row = 0;
		onscreen_keyboard_move(&keyboard, -1, 0);
		CHECK(keyboard.row == ONSCREEN_KEYBOARD_ROWS - 1 && keyboard.column == 0, "up from the top: %d %d",
			keyboard.row, keyboard.column);
		onscreen_keyboard_move(&keyboard, 0, -1);
		CHECK(keyboard.column == ONSCREEN_KEYBOARD_COLUMNS - ONSCREEN_KEYBOARD_WIDE_COLUMNS,
			"left round the last row: %d", keyboard.column);
		keyboard.row = 3;
		keyboard.column = 5;
		onscreen_keyboard_move(&keyboard, 1, 0);
		CHECK(keyboard.row == ONSCREEN_KEYBOARD_ROWS - 1 && keyboard.column == 4, "down onto a wide key: %d %d",
			keyboard.row, keyboard.column);
		onscreen_keyboard_move(&keyboard, 0, 1);
		CHECK(keyboard.column == 4 + ONSCREEN_KEYBOARD_WIDE_COLUMNS, "right on the last row: %d", keyboard.column);
		onscreen_keyboard_move(&keyboard, 1, 0);
		CHECK(keyboard.row == 0 && keyboard.column == 4 + ONSCREEN_KEYBOARD_WIDE_COLUMNS, "down round the bottom: %d %d",
			keyboard.row, keyboard.column);
		keyboard.column = ONSCREEN_KEYBOARD_COLUMNS - 1;
		onscreen_keyboard_move(&keyboard, 0, 1);
		CHECK(keyboard.column == 0, "right round a row: %d", keyboard.column);
	}

	/* a tap: the key under it where it was drawn, a wide key's first
	column; none outside, nor before it is drawn */
	CASE("keyboard-tap")
	{
		onscreen_keyboard_reset(&keyboard);
		keyboard.keys.x0 = 100;
		keyboard.keys.y0 = 200;
		keyboard.keys.x1 = 300;
		keyboard.keys.y1 = 300;
		CHECK(!onscreen_keyboard_pick_at(&keyboard, 110, 210), "a tap taken before it was drawn");
		keyboard.drawn = TRUE;
		CHECK(onscreen_keyboard_pick_at(&keyboard, 110, 210) && keyboard.row == 0 && keyboard.column == 0, "%d %d",
			keyboard.row, keyboard.column);
		CHECK(onscreen_keyboard_pick_at(&keyboard, 290, 230) && keyboard.row == 1 && keyboard.column == 9, "%d %d",
			keyboard.row, keyboard.column);
		CHECK(onscreen_keyboard_pick_at(&keyboard, 210, 290) && keyboard.row == ONSCREEN_KEYBOARD_ROWS - 1 &&
			keyboard.column == 4, "the space key: %d %d", keyboard.row, keyboard.column);
		CHECK(!onscreen_keyboard_pick_at(&keyboard, 99, 250) && !onscreen_keyboard_pick_at(&keyboard, 200, 300),
			"a tap outside taken");
	}

	/* a press: a character, or a wide key's action (capitals and the page
	turned by themselves) */
	CASE("keyboard-press")
	{
		onscreen_keyboard_reset(&keyboard);
		CHECK(onscreen_keyboard_press(&keyboard, &typed) == _onscreen_press_character && typed == 'q', "'%c'", typed);
		keyboard.row = ONSCREEN_KEYBOARD_ROWS - 1;
		keyboard.column = _onscreen_key_shift * ONSCREEN_KEYBOARD_WIDE_COLUMNS;
		CHECK(onscreen_keyboard_press(&keyboard, &typed) == _onscreen_press_none && keyboard.shift, "shift");
		keyboard.column = _onscreen_key_symbols * ONSCREEN_KEYBOARD_WIDE_COLUMNS;
		CHECK(onscreen_keyboard_press(&keyboard, &typed) == _onscreen_press_none && keyboard.symbols, "the page");
		keyboard.column = _onscreen_key_space * ONSCREEN_KEYBOARD_WIDE_COLUMNS;
		CHECK(onscreen_keyboard_press(&keyboard, &typed) == _onscreen_press_character && typed == ' ', "space");
		keyboard.column = _onscreen_key_delete * ONSCREEN_KEYBOARD_WIDE_COLUMNS;
		CHECK(onscreen_keyboard_press(&keyboard, &typed) == _onscreen_press_delete, "delete");
		keyboard.column = _onscreen_key_done * ONSCREEN_KEYBOARD_WIDE_COLUMNS;
		CHECK(onscreen_keyboard_press(&keyboard, &typed) == _onscreen_press_done, "done");
	}

	/* a held direction: a move at once, none before the first delay, then
	one each repeat; let go, the next press moves at once */
	CASE("keyboard-steer")
	{
		onscreen_keyboard_reset(&keyboard);
		onscreen_keyboard_steer(&keyboard, 0, 1, 1000);
		CHECK(keyboard.column == 1, "the press did not move: %d", keyboard.column);
		onscreen_keyboard_steer(&keyboard, 0, 1, 1000 + ONSCREEN_KEYBOARD_REPEAT_FIRST_MILLISECONDS - 1);
		CHECK(keyboard.column == 1, "moved before the first delay: %d", keyboard.column);
		onscreen_keyboard_steer(&keyboard, 0, 1, 1000 + ONSCREEN_KEYBOARD_REPEAT_FIRST_MILLISECONDS);
		CHECK(keyboard.column == 2, "no repeat after the first delay: %d", keyboard.column);
		onscreen_keyboard_steer(&keyboard, 0, 0, 1500);
		onscreen_keyboard_steer(&keyboard, 0, 1, 1501);
		CHECK(keyboard.column == 3, "a new press waited: %d", keyboard.column);
	}

	return 0;
}
