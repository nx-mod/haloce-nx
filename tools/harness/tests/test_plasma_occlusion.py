"""Exercise the actual visibility field with synthetic collision geometry."""
import re
import shutil
import subprocess
import sys
import pytest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, SSE_MATH, build, read, run


def test_visibility_field():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),)), 'visibility')
    assert status == 0, output


def test_fade_underflow_guard_is_required():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    guard = 'if (value < 1.0e-20f) value = 0;'
    assert code.count(guard) == 1
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(guard, '')),)), 'visibility')
    assert status == CHECK_FAILED and 'blocked history settles' in output, output


def test_clear_depth_shortcut_is_exact():
    from harness import function
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    body = function(code, 'depth_visibility')
    reference = body.replace('depth_visibility(', 'reference_depth_visibility(', 1).replace('if (clear)', 'if (clear && FALSE)')
    fast = body.replace('depth_visibility(', 'fast_depth_visibility(', 1)
    wrapper = r'''
static real depth_visibility(real depths[6][OCC_SIZE * OCC_SIZE], real_vector3d const *ray, real extent, int resolution)
{
    real result = fast_depth_visibility(depths, ray, extent, resolution);
    CHECK(result == reference_depth_visibility(depths, ray, extent, resolution), "clear depth shortcut retains float result");
    return result;
}
'''
    code = code.replace(body, reference + '\n' + fast + wrapper).replace('#if defined(__SSE2__)', '#if 0')
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),), SSE_MATH), 'visibility')
    assert status == 0, output


def test_stale_pass_upload_is_detected():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    assert code.count('field->texture_mask = 0;') == 2
    code = code.replace('field->texture_mask = 0;', '(void)0;')
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),)), 'visibility')
    assert status == CHECK_FAILED, output


def test_directional_mask_is_required():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    code = code.replace('return t*t*(3.0f - 2.0f*t);', '(void)t; return 1.0f;')
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),)), 'visibility')
    assert status == CHECK_FAILED, output


def test_needler_filtered_foot_pool_is_detected():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    # The previous long fade must no longer satisfy the lower-spill contract.
    old = code.replace('(-height - 0.06f) / 0.06f', '(-height - 0.06f) / 0.18f')
    status, output = run(build('plasma_occlusion', (('under_test.inc', old),)), 'visibility')
    assert status == CHECK_FAILED, output


def test_stationary_history_matches_reprojection():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    # Compare every updated cell, including moving lights, door changes, changed
    # extents and the 30/60/120 Hz sequences exercised by the fixture.
    marker = '\tfield->valid = 1;'
    assert code.count(marker) == 1
    code = code.replace(marker, marker + '\n\tfor (i = 0; i < OCC_COUNT; i++) printf("CELL %.9g\\n", field->visibility[i]);')
    # (every update is compared, and the reference never settles: nor does this)
    assert code.count('field->settled = TRUE;') == 1
    code = code.replace('field->settled = TRUE;', '')
    samples = []
    reference = code.replace('if (stationary)', 'if (stationary && FALSE)')
    reference = reference.replace('if (stationary && !retraced)', 'if (stationary && !retraced && FALSE)')
    # The reference also needs coordinates for stationary fields now that
    # reprojection prepares its separable axes before visiting the cells.
    reference = reference.replace('if (!stationary && field->valid', 'if (field->valid')
    for version in (code, reference):
        status, output = run(build('plasma_occlusion', (('under_test.inc', version),)), 'visibility')
        assert status == 0, output[-2000:]
        samples.append([float(x) for x in re.findall(r'CELL (\S+)', output)])
    assert len(samples[0]) > 4096
    assert samples[0] == pytest.approx(samples[1], abs=0.00001)


def test_precomputed_history_matches_cell_coordinates():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    fast = 'history = sample_history_rows(projected, x, y, &history_axes[2][z]);'
    assert code.count(fast) == 1
    # Check the actual fast result against the original per-cell coordinate
    # arithmetic throughout movement, extent changes and all three rates.
    reference = '''sample_field(previous,
        0.5f + (light->position.x + ((2.0f * (x + 0.5f) / OCC_SIZE - 1) * extent) - field->center[0]) / (2 * field->extent),
        0.5f + (light->position.y + ((2.0f * (y + 0.5f) / OCC_SIZE - 1) * extent) - field->center[1]) / (2 * field->extent),
        0.5f + (light->position.z + ((2.0f * (z + 0.5f) / OCC_SIZE - 1) * extent) - field->center[2]) / (2 * field->extent))'''
    code = code.replace(fast, '{ ' + fast + '\nCHECK(history == ' + reference + ', "precomputed coordinates preserve every sample"); }')
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),), SSE_MATH), 'visibility')
    assert status == 0, output


def test_interpolation_matches_weighted_visibility():
    from harness import function
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    body = function(code, 'sample_axes')
    wrapper = r'''
static real sample_axes(const real *values, const struct sample_axis *x,
    const struct sample_axis *y, const struct sample_axis *z)
{
    int i,j,k;
    double expected=0;
    real result=fast_sample_axes(values,x,y,z);
    for(k=0;k<2;k++) for(j=0;j<2;j++) for(i=0;i<2;i++)
        expected += (double)values[((k?z->b:z->a)*OCC_SIZE+(j?y->b:y->a))*OCC_SIZE+(i?x->b:x->a)] *
            (i?x->t:1-x->t)*(j?y->t:1-y->t)*(k?z->t:1-z->t);
    CHECK(fabs(result-expected)<0.000001, "seven interpolations agree with weighted visibility");
    return result;
}
'''
    code = code.replace(body, body.replace('sample_axes(', 'fast_sample_axes(', 1) + wrapper)
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),)), 'visibility')
    assert status == 0, output

def test_half_scale_resampling_is_exact():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    anchor = '\t\t\t\t\tvalue = 1 - field->admission + field->admission * value;\n\t\t\t\t\tif (directional)'
    assert code.count(anchor) == 1
    check = r'''
    if (pass && remaining > 0 && scale == 0.5f) {
    real expected=sample_field(field->visibility, 0.5f+(2.0f*(x+0.5f)/16-1)*0.25f,
        0.5f+(2.0f*(y+0.5f)/16-1)*0.25f, 0.5f+(2.0f*(z+0.5f)/16-1)*0.25f);
    CHECK(value==expected, "half-scale resampling preserves the original float result");
    }
'''
    code = code.replace(anchor, check + anchor)
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),)), 'visibility')
    assert status == 0, output


def test_specialized_masks_match_generic_pixels():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    start = code.index('\telse\n\t{', code.index('The ordinary model mask needs'))
    opening = code.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (code[end] == '{') - (code[end] == '}')
        end += 1
    # Execute the actual generic conversion beside the flat path, comparing
    # all uploaded cells rather than only representative sample positions.
    generic = code[opening + 1:end - 1].replace('pixels[i]', 'reference_pixels[i]')
    check = ('\nif (!directional && (!pass || scale == 0.5f || scale == 1.0f)) { unsigned long reference_pixels[OCC_COUNT];\n' +
             generic + '\nCHECK(!memcmp(pixels, reference_pixels, sizeof(pixels)), "specialized mask matches every generic pixel");\n}\n')
    marker = '\tif (!(field->uploaded_mask & (1 << pass))'
    assert code.count(marker) == 1
    code = code.replace(marker, check + marker)
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),)), 'visibility')
    assert status == 0, output


def test_runtime_texture_comparison_preserves_uploads(tmp_path, monkeypatch):
    from lighting_texture_check import instrument
    code = read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c')
    code = instrument(code).replace('if (memcmp(fast, generic, sizeof(fast))) mismatches++;',
        'CHECK(!memcmp(fast, generic, sizeof(fast)), "runtime comparison preserves every texture byte");')
    clocks = '''
typedef struct { long long QuadPart; } LARGE_INTEGER;
static void QueryPerformanceCounter(LARGE_INTEGER *v) { static long long tick; v->QuadPart=++tick; }
static void QueryPerformanceFrequency(LARGE_INTEGER *v) { v->QuadPart=1000000; }
'''
    code = clocks + re.sub(r'^#include.*$', '', code, flags=re.M)
    monkeypatch.chdir(tmp_path)
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),)), 'visibility')
    assert status == 0, output


def test_packed_visibility_matches_scalar_fields():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    samples = []
    marker = '\tfield->valid = 1;'
    code = code.replace(marker, marker + '\n\tfor (i = 0; i < OCC_COUNT; i++) printf("CELL %.9g\\n", field->visibility[i]);')
    for version in (code, code.replace('#if defined(__SSE2__)', '#if 0')):
        status, output = run(build('plasma_occlusion', (('under_test.inc', version),)), 'visibility')
        assert status == 0, output[-2000:]
        samples.append(re.findall(r'CELL (\S+)', output))
    assert len(samples[0]) > 4096
    assert samples[0] == samples[1]


def full_code():
    return re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)


FIELD_CELLS = '\tfield->valid = 1;'


def cells(code):
    """every cell of every field a run of the visibility case builds"""
    assert code.count(FIELD_CELLS) == 1
    return code.replace(FIELD_CELLS, FIELD_CELLS + '\n\tfor (i = 0; i < OCC_COUNT; i++) printf("CELL %.9g\\n", field->visibility[i]);')


def run_on_arm(code, tmp_path):
    """the code built for 64-bit ARM, as the Switch and Android run it, under qemu"""
    compiler, qemu = shutil.which('aarch64-linux-gnu-gcc'), shutil.which('qemu-aarch64-static')
    if not compiler or not qemu:
        pytest.skip('needs aarch64-linux-gnu-gcc and qemu-aarch64-static')
    work = tmp_path / str(abs(hash(code)))
    work.mkdir()
    (work / 'under_test.inc').write_text(code)
    harness = Path(__file__).resolve().parents[1]
    # (no contraction: fused multiplies would differ from SSE's rounding, and from the guest's intrinsics)
    result = subprocess.run([compiler, '-static', '-std=gnu99', '-O2', '-ffp-contract=off', '-w', '-include', 'arm_neon.h',
                             '-I', str(harness / 'include'), '-I', str(work), str(harness / 'tests' / 'plasma_occlusion.c'),
                             '-o', str(work / 'test'), '-lm'], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr[-3000:]
    result = subprocess.run([qemu, str(work / 'test'), 'visibility'], capture_output=True, text=True, errors='replace', timeout=240)
    return result.returncode, result.stdout + result.stderr


def test_neon_matches_scalar_and_sse(tmp_path):
    code = cells(full_code())
    status, neon = run_on_arm(code + '\n#ifndef VISIBILITY_VECTOR\n#error no NEON path\n#endif\n', tmp_path)
    assert status == 0, neon[-2000:]
    status, scalar = run_on_arm(code.replace('#define VISIBILITY_VECTOR\ntypedef float32x4_t', '#undef VISIBILITY_VECTOR\ntypedef float32x4_t'), tmp_path)
    assert status == 0, scalar[-2000:]
    status, sse = run(build('plasma_occlusion', (('under_test.inc', code),), SSE_MATH + ('-ffp-contract=off',)), 'visibility')
    assert status == 0, sse[-2000:]
    neon, scalar, sse = (re.findall(r'CELL (\S+)', output) for output in (neon, scalar, sse))
    assert len(neon) > 4096
    assert neon == scalar
    assert neon == sse


FAST_MATH = 'int fast = lighting_fast_occlusion_math();'


def fast_and_exact(flags):
    """every cell of the visibility case, exactly and with Fast Occlusion Math"""
    code = cells(full_code())
    assert code.count(FAST_MATH) == 1
    outputs = []
    for version in (code, code.replace(FAST_MATH, 'int fast = 1;')):
        status, output = run(build('plasma_occlusion', (('under_test.inc', version),), flags), 'visibility')
        assert status == 0, output[-2000:]
        outputs.append([float(cell) for cell in re.findall(r'CELL (\S+)', output)])
    return outputs


def test_fast_occlusion_math_is_near_the_exact():
    exact, fast = fast_and_exact(SSE_MATH + ('-ffp-contract=off',))
    assert len(exact) > 4096 and len(fast) == len(exact)
    worst = max(abs(a - b) for a, b in zip(exact, fast))
    # (under half an 8-bit texture step, all the upload keeps of a cell)
    assert worst < 0.5 / 255, worst
    assert exact != fast, 'the fast path is the one tested'


@pytest.mark.parametrize('before,after', [
    # (not vis_all_set: the path it chooses is an exact shortcut, so breaking it changes no cell)
    ('return vcvtq_f32_s32(vcvtq_s32_f32(value));', 'return value;'),
    ('return vis_choose(vis_greater(a, b), a, b);', 'return vis_choose(vis_less(a, b), a, b);'),
    ('return vbslq_f32(vreinterpretq_u32_f32(mask), a, b);', 'return vbslq_f32(vreinterpretq_u32_f32(mask), b, a);'),
])
def test_neon_comparison_finds_a_wrong_helper(before, after, tmp_path):
    code = cells(full_code())
    assert code.count(before) == 1
    status, good = run_on_arm(code, tmp_path)
    assert status == 0, good[-2000:]
    status, bad = run_on_arm(code.replace(before, after), tmp_path)
    assert status != 0 or re.findall(r'CELL (\S+)', bad) != re.findall(r'CELL (\S+)', good)


def test_prong_mask_is_the_factor():
    status, output = run(build('plasma_occlusion', (('under_test.inc', full_code()),), SSE_MATH), 'prong')
    assert status == 0, output


def test_prong_mask_negative_control():
    code = full_code()
    before = 'lengths[i] + 0.2f) / 0.7f, 0.0f, 1.0f);'
    assert code.count(before) == 1
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, 'lengths[i] + 0.2f) / 0.75f, 0.0f, 1.0f);')),), SSE_MATH), 'prong')
    assert status == CHECK_FAILED and 'a prong mask' in output, output


def test_runtime_visibility_comparison_preserves_fields(tmp_path, monkeypatch):
    from lighting_visibility_check import instrument
    code = read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c')
    code = instrument(code).replace('if (memcmp(output, reference, sizeof(reference))) mismatches++;',
        'CHECK(!memcmp(output, reference, sizeof(reference)), "runtime comparison preserves every visibility byte");')
    clocks = '''
typedef struct { long long QuadPart; } LARGE_INTEGER;
static void QueryPerformanceCounter(LARGE_INTEGER *v) { static long long tick; v->QuadPart=++tick; }
static void QueryPerformanceFrequency(LARGE_INTEGER *v) { v->QuadPart=1000000; }
'''
    code = clocks + re.sub(r'^#include.*$', '', code, flags=re.M)
    monkeypatch.chdir(tmp_path)
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),)), 'visibility')
    assert status == 0, output


def test_scalar_path():
    """The ARM images have no SSE2 path: the whole case over the scalar reconstruction."""
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace('#if defined(__SSE2__)', '#if 0')),)), 'visibility')
    assert status == 0, output


@pytest.mark.parametrize('before,after,expected', [
    # the scene asked on every call again (the review's finding)
    ('if (!retrace && due)\n', 'if (!retrace && (due || 1))\n', 'before the cadence'),
    # an extent change retraces at once again (the review's finding)
    ('retrace = !field->valid ||', 'retrace = field->trace_extent != extent || !field->valid ||', 'does not trace'),
    ('rescale_depths(field->depths, field->trace_extent, extent);', '(void)0;', 'same hits'),
])
def test_fix_negative_controls(before, after, expected):
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    assert code.count(before) == 1, before
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, after)),)), 'visibility')
    assert status == CHECK_FAILED and expected in output, output


def test_light_occlusion_off_negative_control():
    """Muzzle and vehicle lights tracing with the toggle off, GulchCE's behaviour: both gates opened."""
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    gate = 'lighting_config_boolean("display.plasma_occlusion") &&\n'
    assert code.count(gate) == 2
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(gate, '1 &&\n')),)), 'visibility')
    assert status == CHECK_FAILED and 'traces no rays' in output, output


def test_stale_depths_need_their_own_grid_negative_control():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    before = 'reconstruct_visibility(field->depths, extent, field->trace_resolution, trace_dx'
    assert code.count(before) == 1
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, 'reconstruct_visibility(field->depths, extent, resolution, trace_dx')),)), 'visibility')
    assert status == CHECK_FAILED and 'read as what they were' in output, output


@pytest.mark.parametrize('before,after,expected', [
    # draws decide who gets the rays again: whichever is drawn first wins
    ('plan_traces(now);', '(void)now;', 'one a frame'),
    # the least out-of-date first: the same light wins, and the others never have a turn
    ('scores[i - 1] < score', 'scores[i - 1] > score', 'has its turn'),
    # nothing held back: a refused light never gets in
    ('reserve_rays = MAX(reserve_rays, (long)resolution * resolution * 6);', '(void)resolution;', 'one a frame'),
    # a light too far from its depths draws from them anyway
    ('* 0.0625f;', '* 1000.0f;', 'misplaced field'),
])
def test_budget_negative_controls(before, after, expected):
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    assert code.count(before) == 1, before
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, after)),)), 'visibility')
    assert status == CHECK_FAILED and expected in output, output


@pytest.mark.parametrize('before,after,expected', [
    ('!plasma_occlusion_light_visible(rasterizer_index, field_extent(rasterizer_index)))', '0)', 'out of view traces nothing'),
    ('current_window - field->consumed > OCC_UNREAD_VIEWS)\n\t\t\t\tcontinue;', '0)\n\t\t\t\tcontinue;', 'stops being updated'),
    ('(light_trace_resolution(identity, rasterizer_index) != OCC_SIZE) != !round ||', '', 'choose their fields first'),
])
def test_prepare_negative_controls(before, after, expected):
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    assert code.count(before) == 1, before
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, after)),)), 'visibility')
    assert status == CHECK_FAILED and expected in output, output


def test_lights_come_before_the_first_draw():
    source = read('source/render/render.c')
    lights = source.index('lights_preprocess_scene();')
    sky = source.index('render_sky();')
    flares = source.index('lights_submit_queued_lens_flares();')
    assert lights < sky < flares
    assert 'queued_lens_flares[queued' not in __import__('harness').function(read('source/objects/object_lights.c'), 'lights_preprocess_scene')


@pytest.mark.parametrize('after,expected', [
    # always rebuilds: the unchanged direction's check is the first to see it
    ('< 1.1f', 'specular reuses'),
    # never rebuilds
    ('< -2.0f', 'rotating a counter'),
])
def test_sway_negative_controls(after, expected):
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    before = '< 0.9962f'
    assert code.count(before) == 1
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, after)),)), 'visibility')
    assert status == CHECK_FAILED and expected in output, output


def test_lighting_stats():
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    status, output = run(build('plasma_occlusion', (('under_test.inc', code),)), 'stats')
    assert status == 0, output


@pytest.mark.parametrize('before,after,expected', [
    ('stats.trace_moved++', 'stats.trace_scene++', 'a moved light counts as moved'),
    ('stats.trace_held++', 'stats.trace_moved++', 'and moving, it counts as held'),
    ('stats.rebuild_stationary++', 'stats.rebuild_moved++', 'a still light in a changed scene'),
    ('stats.fade_settled++', '', 'a fade counts as settled'),
    ('stats.textures++', '', 'a texture build is counted'),
    ('stats.refused++', '', 'one traced, the other refused'),
    ('stats.too_far++', '', 'one traced, the other refused'),
    ('memset(&stats, 0, sizeof(stats));', '', 'then counting starts over'),
    ('now - stats.logged < 1000', 'now - stats.logged < 1', 'an unchanged light only fades'),
])
def test_lighting_stats_negative_controls(before, after, expected):
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    assert code.count(before) == 1
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, after)),)), 'stats')
    assert status == CHECK_FAILED and expected in output, output


@pytest.mark.parametrize('before,after,expected', [
    # the toggle does nothing
    ('lighting_moving_lights_384() &&', 'FALSE &&', 'a moving light has 384 rays'),
    # a new light has it too
    ('resolution == OCC_SIZE && field->valid &&', 'resolution == OCC_SIZE &&', 'a new light has the full grid'),
    # a held light has it too
    ('resolution == OCC_SIZE && field->valid &&', 'field->valid &&', 'a held light keeps its own grid'),
    # a light that stops stays coarse: (no control: trace_scene_revision gives a coarse,
    # moving trace no revision, so a stopped light's scene check retraces it at the full
    # grid without this clause; the case still checks that it does)
])
def test_moving_grid_negative_controls(before, after, expected):
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    assert code.count(before) == 1
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, after)),)), 'stats')
    assert status == CHECK_FAILED and expected in output, output


@pytest.mark.parametrize('before,after,expected', [
    # never rests: three still lights fill the limit again, the bug this fixes
    ('field->settled = TRUE;', '', 'and it does no work'),
    # the limit counts all the slots again
    ('for (i = 0; i < OCC_LIGHTS; i++)\n\t\t\tif (!fields[i].valid || now - fields[i].used > 100)',
     'for (i = 0; i < budget->fields; i++)\n\t\t\tif (!fields[i].valid || now - fields[i].used > 100)', 'three lights at rest still leave a new one room'),
    # what wakes it
    ('field->center[0] == light->position.x && field->center[1] == light->position.y &&\n\t\tfield->center[2] == light->position.z && field->radius', 'field->radius', 'a moved light counts as moved'),
    # (not field->radius's: a new radius always changes the extent or the specular too)
    ('field->specular == specular && field->extent', 'field->extent', 'a new specular wakes it'),
    ('field->scene_revision == plasma_collision_scene_revision(&field->scene, &light->position, extent, field_vehicles(field->identity))',
     'TRUE', 'a changed scene wakes it'),
    ('field->settled && !(field->texture_mask & ~7) &&', 'field->settled &&', 'a failed upload gets its retry'),
    # a stale flag: waking must clear it
    ('\tfield->settled = FALSE;\n\tlight = &rasterizer_lights', '\tlight = &rasterizer_lights', 'a fade counts as settled'),
])
def test_rest_negative_controls(before, after, expected):
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    assert code.count(before) == 1
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, after)),)), 'stats')
    assert status == CHECK_FAILED and expected in output, output


@pytest.mark.parametrize('before,after,expected', [
    # weapon lights fill every slot again: the bug this fixes
    ('view_budget.weapon_limit = other_wanted ? MAX(budget->fields - 1, 1) : budget->fields;',
     'view_budget.weapon_limit = other_wanted ? budget->fields : budget->fields;', 'a weapon light sits out for a light that asks'),
    # they leave a slot with nothing there to take it
    ('view_budget.weapon_limit = other_wanted ? MAX(budget->fields - 1, 1) : budget->fields;',
     'view_budget.weapon_limit = other_wanted ? MAX(budget->fields - 1, 1) : MAX(budget->fields - 1, 1);', 'a weapon light has its field'),
    # a light sitting out spends rays all the same
    ('order[i]->waits_window == current_window ||', '', 'a weapon light sits out for a light that asks'),
    # the same light always waits
    ('(long)(weapons[i]->updated - weapons[latest]->updated) > 0', '(long)(weapons[i]->updated - weapons[latest]->updated) < 0', 'none waits two views running'),
    # one that woke and was turned away stops asking
    ('if (field)\n\t\t\tfield->settled = FALSE;\n', '', 'woken and turned away'),
    # a waiting light draws from texels made too far away
    ('if (!field_near(field, rasterizer_index))\n\t\treturn FALSE;\n', '', 'a waiting light too far'),
    # a new weapon light takes the slot
    ('view_budget.weapon_updates >= view_budget.weapon_limit', 'FALSE', 'a new weapon light'),
    # rays held for a light with no slot: the other half of the bug
    ('if (at_work + weapon_count < budget->fields)', 'if (at_work + weapon_count < budget->fields + 99)', 'no rays are held'),
    ('if (at_work + weapon_count < budget->fields)', 'if (at_work + weapon_count < 0)', 'but they are held'),
])
def test_weapon_slot_negative_controls(before, after, expected):
    code = re.sub(r'^#include.*$', '', read('port/linux/game/ammo_counter_falloff.c') + '\n' + read('port/linux/game/plasma_occlusion.c'), flags=re.M)
    assert code.count(before) == 1, before
    status, output = run(build('plasma_occlusion', (('under_test.inc', code.replace(before, after)),)), 'stats')
    assert status == CHECK_FAILED and expected in output, output
