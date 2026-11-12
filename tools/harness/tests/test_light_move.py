"""A light on a moving object only rejoins its clusters once it has gone far enough."""
import pytest
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, read, run


def under_test():
    source = read('source/objects/object_lights.c')
    return function(source, 'light_stays_connected') + '\n' + function(source, 'light_move_with_object')


def test_light_move():
    status, output = run(build('light_move', (('under_test.inc', under_test()),)), 'move')
    assert status == 0, output


@pytest.mark.parametrize('before,after,expected', [
    # never stays: every tick leaves and joins again, as before
    ('return dx * dx + dy * dy + dz * dz <= reach * reach;', 'return (dx + dy + dz + reach) * 0 != 0;', "a light that didn't move stays"),
    # always stays
    ('return dx * dx + dy * dy + dz * dz <= reach * reach;', 'return (dx + dy + dz + reach) * 0 == 0;', 'small moves add up'),
    ('if (radius > connection->radius)', 'if (FALSE)', 'a new radius joins again'),
    ('if (radius > connection->radius)', 'if (radius != connection->radius)', 'a smaller sphere stays'),
    ('MIN(0.25f, radius * 0.25f)', '0.25f', 'but only a part of its radius'),
    ('connection->light_index != light_index ||', '', 'another light at that slot'),
    ('connection->cluster_reference != light->cluster_reference ||', '', "a restored game's lists"),
    # what lights_port_cluster kept goes on saying the old position
    ('lights_port.radius_writes++;', '(void)0;', 'what was kept for its clusters is dropped'),
    # the position waits for the clusters
    ('light_compute_pose(light);\n', '', 'a short move takes the new position'),
])
def test_light_move_negative_controls(before, after, expected):
    code = under_test()
    assert code.count(before) == 1, before
    status, output = run(build('light_move', (('under_test.inc', code.replace(before, after)),)), 'move')
    assert status == CHECK_FAILED and expected in output, output


def test_every_way_of_leaving_clusters_forgets_the_record():
    source = read('source/objects/object_lights.c')
    assert 'lights_port_connections[DATUM_INDEX_TO_ABSOLUTE_INDEX(light_index)].light_index = NONE;' in function(source, 'light_disconnect_from_map')
    connect = function(source, 'light_connect_to_clusters')
    for field in ('light_index = light_index', 'cluster_reference = light->cluster_reference', 'center = position', 'radius = radius'):
        assert 'connection->' + field in connect, field
    # a light that dies: its datum's next owner has another identifier, so the record can't match
    assert 'memset(lights_port_connections' in function(source, 'lights_initialize_for_new_map')
    assert 'memset(lights_port_connections' in function(source, 'lights_dispose_from_old_map')


def test_an_object_that_moved_uses_it():
    body = function(read('source/objects/objects.c'), 'object_connect_lights')
    assert 'if (disconnect && reconnect)' in body and 'light_move_with_object(' in body
