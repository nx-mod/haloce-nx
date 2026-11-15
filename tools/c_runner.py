"""run_c: a small C program built for the game's 32-bit ABI and run, for the
tests that check a function lifted out of the game's sources (it was
test_hud_button_style.py's, which went with upstream's controller glyphs)"""
import shutil
import subprocess
import sys

import pytest


def run_c(tmp_path, code, link_math=False):
    compiler = shutil.which('clang')
    if not compiler:
        pytest.skip('clang required')
    source = tmp_path / 'style.c'
    binary = tmp_path / 'style.exe'
    source.write_text(code)
    target = ['--target=i686-pc-windows-msvc'] if sys.platform == 'win32' else ['-m32']
    libraries = ['-lm'] if link_math and sys.platform != 'win32' else []
    result = subprocess.run([compiler, *target, '-std=gnu89', '-O1', str(source), '-o', str(binary), *libraries],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    subprocess.run([str(binary)], check=True, timeout=10)
