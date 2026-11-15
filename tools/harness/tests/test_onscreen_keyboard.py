"""The on-screen keyboard's keys (port/linux/game/onscreen_keyboard.c, the text chat's and the menus' text fields'): a
key's character, moving round its edges, a tap's key, a press's action and a held direction's repeats."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["keyboard-character", "keyboard-move", "keyboard-tap", "keyboard-press", "keyboard-steer"]
FUNCTIONS = ("onscreen_keyboard_reset", "onscreen_keyboard_move", "onscreen_keyboard_steer", "onscreen_character",
             "onscreen_keyboard_press", "onscreen_keyboard_pick_at")

# a fault in onscreen_keyboard.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "shift-not-letters": (("if (keyboard->shift && character >= 'a' && character <= 'z')", "if (keyboard->shift)"),
                          "onscreen_character", "keyboard-character"),
    "last-row-read": (("row >= ONSCREEN_KEYBOARD_ROWS - 1 ||", "row >= ONSCREEN_KEYBOARD_ROWS ||"),
                      "onscreen_character", "keyboard-character"),
    "move-no-wrap": (("rows_down % ONSCREEN_KEYBOARD_ROWS + ONSCREEN_KEYBOARD_ROWS) %", "rows_down) %"),
                     "onscreen_keyboard_move", "keyboard-move"),
    "wide-not-snapped": (("keyboard->column = (short)(keyboard->column - keyboard->column % width);", ""),
                         "onscreen_keyboard_move", "keyboard-move"),
    "tap-outside-taken": (("x < keyboard->keys.x0 || x >= keyboard->keys.x1 ||", ""), "onscreen_keyboard_pick_at",
                          "keyboard-tap"),
    "tap-wide-not-snapped": (("column = (short)(column - column % ONSCREEN_KEYBOARD_WIDE_COLUMNS);", ";"),
                             "onscreen_keyboard_pick_at", "keyboard-tap"),
    "space-not-typed": (("*typed = ' ';", ""), "onscreen_keyboard_press", "keyboard-press"),
    "steer-no-delay": (("else if (!keyboard->repeat_at || (long)(now - keyboard->repeat_at) >= 0)", "else"),
                       "onscreen_keyboard_steer", "keyboard-steer"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/game/onscreen_keyboard.c")
    header = read("port/linux/include/halo_onscreen_keyboard.h")
    config = enum_with(header, "ONSCREEN_KEYBOARD_COLUMNS = ") + "\n"
    config += enum_with(header, "_onscreen_key_shift,") + "\n"
    config += enum_with(header, "_onscreen_press_none,") + "\n"
    config += structure(header, "onscreen_keyboard") + "\n"
    # (its pages' characters, each with a row past them that no key reads,
    # so that a key of the last row read as a character shows)
    for page in ("onscreen_letters", "onscreen_symbols"):
        table = re.search(r"static char const \*const " + page + r"\[.*?;\n", source, re.S).group(0)
        table = table.replace("[ONSCREEN_KEYBOARD_ROWS - 1]", "[ONSCREEN_KEYBOARD_ROWS]", 1)
        config += table[:table.rindex("}")] + ', "##########" };\n'

    text = ""
    for name in FUNCTIONS:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("onscreen_keyboard", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("onscreen_keyboard", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"onscreen_keyboard.c with a fault ({control}) passed '{case}': the test cannot see it"
