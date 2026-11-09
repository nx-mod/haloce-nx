"""Objects drawn between ticks: blended while they move, drawn as they are once both snapshots hold one pose, and a
still pose found again at a tick kept rather than copied."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

CASES = ['moving', 'stops', 'still', 'starts']
CONTROLS = [
    # skips a pose found again before both snapshots hold it: blended from where it was
    ('if (unchanged && record->still)', 'if (unchanged)', 'stops'),
    # never copies once still: a still object that moves is not seen to
    ('unchanged = continuing && !memcmp(', 'unchanged = continuing && 0 == 0 * memcmp(', 'starts'),
    # never still: always copied
    ('record->still = unchanged;', 'record->still = FALSE;', 'still'),
]


def generated(fault=None):
    source = read('port/linux/game/render_interpolation.c')
    maths = read('source/math/real_math.h')
    matrices = read('source/math/matrix_math.c')
    config = '\n'.join([
        '#define MAXIMUM_LOCAL_PLAYERS 4',
        '#define MAXIMUM_INTERPOLATED_OBJECTS 16',
        *(re.search(r'^#define ' + name + ' .*', source, re.M).group() for name in
          ('MAXIMUM_INTERPOLATED_NODES', 'OBJECT_SNAP_DISTANCE', 'NODE_SNAP_DISTANCE', 'CORRECTION_DECAY',
           'CORRECTION_NEGLIGIBLE')),
        structure(maths, 'real_matrix4x3'),
        'typedef struct real_matrix4x3 real_matrix4x3;',
    ]) + '\n'
    code = '\n'.join(
        [function(matrices, 'matrix4x3_inverse_transform_point')] +
        [structure(source, name) for name in
         ['interpolation_quaternion', 'interpolation_rotation', 'interpolated_object', 'interpolated_camera']] +
        ['static struct interpolated_object *interpolated_objects;',
         'static struct interpolated_camera interpolated_cameras[MAXIMUM_LOCAL_PLAYERS];'] +
        [function(source, name) for name in
         ['lerp', 'point_lerp', 'vector_length', 'basis_is_rotation', 'quaternion_from_basis', 'basis_from_quaternion',
          'rotation_from_matrix', 'matrix_blend_rotations', 'correction_significant', 'correction_advance',
          'correction_drawn', 'distance_squared', 'render_interpolation_tick', 'interpolated_object_node_matrices']])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('object_interpolation', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('object_interpolation', generated((before, after))), case)
    assert status == CHECK_FAILED, output
