"""A round's own options as rotation rules (port/linux/game/server_gametypes.c's server_gametype_rules_text, for Host
Server's Round Options): the rules written for what differs from the gametype, read back by server_rotation.c and
applied as the server applies them (its rules, the teams' vehicles settled, blue's own last), giving the same options;
red's and blue's vehicles each their own; a value no rule can hold refused."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["unchanged", "universal", "options", "vehicles-sides", "vehicles-same", "vehicles-presets",
         "vehicle-set", "refused", "parsed"]

FUNCTIONS = ("server_gametype_engine_name", "server_gametype_flag", "server_gametype_team_vehicles",
             "server_gametype_rule", "server_gametype_vehicles_settle", "server_gametype_apply_blue",
             "server_gametype_apply_rules", "server_gametype_rule_get", "server_gametype_rule_append",
             "server_gametype_team_vehicles_word", "server_gametype_team_vehicles_differ",
             "server_gametype_rules_text")

# a fault in server_gametypes.c and the case that must catch it
NEGATIVE_CONTROLS = {
    "blue-unwritten": (("if (options->vehicle_set[1] != options->vehicle_set[0])", "if (0)"), "vehicles-sides"),
    "blue-counts-unwritten": (("snprintf(key, sizeof(key), \"blue_%s\", vehicles[vehicle]);",
                               "snprintf(key, sizeof(key), \"%s\", vehicles[vehicle]);"), "vehicles-sides"),
    "blue-counts-red": (("options->vehicle_counts[1][vehicle] = (byte)rules[index].value;",
                         "options->vehicle_counts[0][vehicle] = (byte)rules[index].value;"), "vehicles-sides"),
    "respawn-ticks": (("*value = universal->respawn_time / TICKS_PER_SECOND;", "*value = universal->respawn_time;"),
                      "universal"),
    "presets-lost": (("value >= 3 ? (byte)(value - 3) :", "value >= 3 ? 0 :"), "vehicles-presets"),
    "range-unchecked": (("if (value < definition->minimum || value > definition->maximum)\n\t\t\treturn FALSE;", ""),
                        "refused"),
    "force-spawns-unread": (("else if (!strcmp(key, \"force_team_spawns\")) *value = options->force_team_spawns != 0;",
                             ""), "options"),
}


def generated(fault=None):
    engine = read("source/game/game_engine.h")
    gametypes = read("port/linux/game/server_gametypes.c")
    enums = "\n".join([enum_with(engine, "_game_variant_draw_object_in_motion_sensor_bit = 0"),
                       enum_with(engine, "game_engine_none = 0"), enum_with(engine, "_loadout_weapon_none = 0"),
                       enum_with(engine, "_variant_vehicle_warthog = 0")]) + "\n"
    structures = "\n".join([structure(engine, name) for name in
                            ("universal_variant", "ctf_variant", "slayer_variant", "king_variant", "oddball_variant",
                             "race_variant")] +
                           [read("source/game/game_engine.h")[engine.index("union game_engine_variant"):]
                            .split("};", 1)[0] + "};",
                            structure(engine, "game_variant").replace("wchar_t", "unsigned short"),
                            structure(engine, "game_variant_options")]) + "\n"
    code = "\n".join(function(gametypes, name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    return (("server_rotation.h", read("port/linux/src/server_rotation.h")),
            ("rotation.inc", read("port/linux/src/server_rotation.c")),
            ("enums.inc", enums), ("structures.inc", structures), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("server_gametype_rules", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("server_gametype_rules", generated(fault)), case)
    assert status == CHECK_FAILED, f"server_gametypes.c with a fault ({control}) passed '{case}': the test cannot see it"
