"""D3DDevice_DrawVerticesImmediate (the decals' packed vertices) draws what Begin, SetVertexData and End draw, on
deko3d (the Switch) and Vulkan (Android and the PS5)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, build, function, mutated, read, run

DK = 'port/switch/guest/d3d8_dk.c'
VK = 'port/android/guest/d3d8_vk.c'
FUNCTIONS = {
    DK: ['D3DDevice_Begin', 'immediate_emit', 'D3DDevice_End', 'set_attribute', 'D3DDevice_SetVertexData4f',
         'D3DDevice_DrawVerticesImmediate'],
    VK: ['D3DDevice_Begin', 'immediate_room', 'immediate_column_add', 'immediate_emit', 'D3DDevice_End',
         'set_attribute', 'D3DDevice_SetVertexData4f', 'D3DDevice_DrawVerticesImmediate'],
}
FLAGS = {DK: ('-I', str(ROOT / 'port/switch/guest')), VK: ('-DVULKAN',)}


def generated(backend, fault=None):
    source = read(backend)
    code = '\n'.join(function(source, name) for name in FUNCTIONS[backend])
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('backend', [DK, VK])
def test_same_draw(backend):
    status, output = run(build('immediate_vertices', generated(backend), FLAGS[backend]), 'all')
    assert status == 0, output


@pytest.mark.parametrize('backend,before,after', [
    # a vertex short
    (DK, 'memcpy(&command->streams[1], data, count * stride);', 'memcpy(&command->streams[1], data, count * stride - 16);'),
    (VK, 'device.immediate_count = count;', 'device.immediate_count = count - 1;'),
    # the registers not left as the last vertex left them
    (DK, 'memcpy(device.attributes[reg], last + index++ * 4, 4 * sizeof(float));', '(void)last, index++;'),
    (VK, 'memcpy(device.attributes[reg], last + index++ * 4, 4 * sizeof(float));', '(void)last, index++;'),
])
def test_negative_control(backend, before, after):
    status, output = run(build('immediate_vertices', generated(backend, (before, after)), FLAGS[backend]), 'all')
    assert status == CHECK_FAILED, output
