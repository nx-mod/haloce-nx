"""debug.benchmark_weapon: every name is a loadout weapon, and only then is the variant changed."""
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read


def test_every_name_is_a_loadout_weapon():
    names = re.findall(r'\{ "(\w+)", (_loadout_weapon_\w+) \}', function(read('port/linux/game/benchmark.c'), 'benchmark_loadout_weapon'))
    assert len(names) == 8
    enum = read('source/game/game_engine.h')
    for name, weapon in names:
        assert weapon == '_loadout_weapon_' + name
        assert weapon in enum


def test_a_weapon_makes_a_custom_loadout_and_no_weapon_changes_nothing():
    setup = function(read('port/linux/game/benchmark.c'), 'benchmark_multiplayer_setup')
    guarded = setup[setup.index('if (weapon != _loadout_weapon_none)\n\t\t{'):]
    for line in ('options.loadout = _loadout_custom;', 'options.primary_weapon = weapon;', 'options.no_map_weapons = TRUE;', 'player_ui_set_game_variant_options(&options);'):
        assert line in guarded
    assert setup.index('player_ui_set_game_variant(&variant);') < setup.index('player_ui_set_game_variant_options(&options);') < setup.index('network_game_server_change_game_variant(')
