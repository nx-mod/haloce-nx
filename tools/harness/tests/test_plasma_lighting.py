"""Render-only attachment refresh must not survive a frame or map boundary."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run


def generated(fault=None):
    code = function(read('source/objects/object_lights.c'), 'light_port_refresh_attachment')
    code += function(read('source/objects/object_lights.c'), 'light_port_dynamic_set')
    code += function(read('source/objects/object_lights.c'), 'light_port_attachment_radius')
    policy = read('port/linux/game/covenant_lighting.c')
    policy = policy[policy.index('static const struct covenant_profile'):policy.index('static const real_rgb_color covenant_black')]
    code += 'typedef struct { real red, green, blue; } real_rgb_color;\n' + policy
    code += re.sub(r'^#include.*$', '', read('port/linux/game/plasma_lighting.c'), flags=re.M)
    code += function(read('source/objects/object_lights.c'), 'light_compute_bounding_sphere')
    code += function(read('port/linux/game/render_interpolation.c'), 'render_interpolation_frame_end')
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['view', 'cap', 'shield', 'counter_cache', 'covenant', 'muzzle', 'radius', 'rocket', 'pistol', 'model', 'off', 'filter', 'restore', 'split', 'capacity', 'unload', 'missing', 'stale', 'selection', 'disconnected', 'existing'])
def test_case(case):
    status, output = run(build('plasma_lighting', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', [
    ('strcmp(name, "weapons\\\\rocket launcher\\\\rocket")', '1', 'rocket'),
    ('strcmp(name, "weapons\\\\plasma pistol\\\\bolt") &&', '1 &&', 'pistol'),
    ('return radius * plasma_lighting_attachments[index].radius_scale;', 'return radius;', 'radius'),
    # every light is promoted, in view or not
    ('if (!light_port_can_reach_view(light_index))', 'if (FALSE)', 'view'),
    # the cap does nothing
    ('if (!cap || plasma_lighting_count <= cap)', 'if (TRUE)', 'cap'),
    # the farthest kept, not the nearest
    ('distances[index] < distances[nearest]', 'distances[index] > distances[nearest]', 'cap'),
    # a shrunk light never gets its reach back
    ('plasma_lighting_attachments[index].radius_scale = plasma_lighting_attachments[index].full_scale;', '(void)0;', 'cap'),
    # past its far distance, it goes on shrinking
    ('fade > 1.0f ? 1.0f : fade', 'fade', 'cap'),
    ('interpolation_rendering = FALSE;', 'interpolation_rendering = TRUE;', 'restore'),
    ('plasma_lighting_count = 0;', '(void)0;', 'unload'),
])
def test_negative_control(before, after, case):
    status, output = run(build('plasma_lighting', generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_disabled_projectile_policy_is_required():
    # Authored emitters add a second category gate. Removing only the early
    # return is no longer a fault: the second gate still preserves stock.
    name, code = generated()[0]
    assert code.count('lighting_config_boolean("display.plasma_lighting")') > 1
    code = code.replace('lighting_config_boolean("display.plasma_lighting")', 'TRUE')
    status, output = run(build('plasma_lighting', ((name, code),)), 'off')
    assert status == CHECK_FAILED, output


def test_no_scale_passes_what_the_view_test_assumes():
    limit = float(re.search(r'PLASMA_LIGHTING_MAXIMUM_SCALE ([\d.]+)f', read('port/linux/game/plasma_lighting.h')).group(1))
    authored = read('port/linux/game/authored_emitter_lighting.c')
    authored = authored[authored.index('profiles[] = {'):authored.index('};', authored.index('profiles[] = {'))]
    covenant = read('port/linux/game/covenant_lighting.c')
    covenant = covenant[covenant.index('static const struct covenant_profile'):covenant.index('};', covenant.index('static const struct covenant_profile'))]
    prepare = function(read('port/linux/game/plasma_lighting.c'), 'plasma_lighting_prepare')
    scales = re.findall(r'", ([\d.]+)f, \d\}', authored) + re.findall(r'", ([\d.]+)f, [\d.]+f, \{', covenant) + re.findall(r'\? ([\d.]+)f', prepare) + re.findall(r': ([\d.]+)f;', prepare)
    assert len(scales) > 20, scales
    assert max(map(float, scales)) <= limit


def test_the_view_test_uses_the_limit():
    body = function(read('source/objects/object_lights.c'), 'light_port_can_reach_view')
    assert 'PLASMA_LIGHTING_MAXIMUM_SCALE' in body and 'render_frustum_sphere_visible' in body

