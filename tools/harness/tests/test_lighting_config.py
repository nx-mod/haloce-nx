"""Lighting preference cache observes settings changes without caching policy."""
import re
import sys
import pytest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED,build,read,run


def test_lighting_preferences_refresh_on_change():
    code=re.sub(r'^#include.*$','',read('port/linux/game/lighting_config.c'),flags=re.M)
    status,output=run(build('lighting_config',(('under_test.inc',code),)),'settings')
    assert status==0,output


def test_a_platform_can_leave_a_key_out():
    code=re.sub(r'^#include.*$','',read('port/linux/game/lighting_config.c'),flags=re.M)
    assert code.count('\tNULL\n};')==1
    limited=code.replace('\tNULL\n};','\t"display.muzzle_lighting",\n\tNULL\n};')
    status,output=run(build('lighting_config',(('under_test.inc',limited),)),'limited')
    assert status==0,output


def test_forcing_a_setting_off_leaves_the_config_alone():
    code=re.sub(r'^#include.*$','',read('port/linux/game/lighting_config.c'),flags=re.M)
    assert 'config_write' not in code and 'config_set' not in code
    status,output=run(build('lighting_config',(('under_test.inc',code),)),'forced')
    assert status==0,output


@pytest.mark.parametrize('flags,expected', [
    (('-DHALO_ANDROID=1', '-DHALO_SWITCH'), 'BUDGET 3 1800 0'),
    (('-DHALO_ANDROID=1', '-DHALO_PS5=1'), 'BUDGET 8 0 0'),
    (('-DHALO_ANDROID=1',), 'BUDGET 8 0 4000'),
])
def test_each_platform_has_its_budget(flags, expected):
    code=re.sub(r'^#include.*$','',read('port/linux/game/lighting_config.c'),flags=re.M)
    status,output=run(build('lighting_config',(('under_test.inc',code),),flags),'budget')
    assert status==0 and expected in output,output


def test_lookups_compare_no_strings():
    code=re.sub(r'^#include.*$','',read('port/linux/game/lighting_config.c'),flags=re.M)
    status,output=run(build('lighting_config',(('under_test.inc',code),)),'lookups')
    assert status==0,output


@pytest.mark.parametrize('before,after,expected', [
    # never remembered: every lookup compares strings again
    ('preference_addresses[slot].name = name;', '(void)0;', 'a name seen before needs no string comparison'),
    # one address a table slot and no probing: copies collide, or evict each other
    ('preference_addresses[slot].preference = preference_by_name(name);', 'preference_addresses[slot].preference = 0;', 'each address has its own setting'),
    # nowhere to go past the table's room
    ('\treturn preference_by_name(name);\n}\n\nint lighting', '\treturn 0;\n}\n\nint lighting', 'each address has its own setting'),
])
def test_lookups_negative_controls(before,after,expected):
    code=re.sub(r'^#include.*$','',read('port/linux/game/lighting_config.c'),flags=re.M)
    assert code.count(before)==1
    status,output=run(build('lighting_config',(('under_test.inc',code.replace(before,after)),)),'lookups')
    assert status==CHECK_FAILED and expected in output,output
