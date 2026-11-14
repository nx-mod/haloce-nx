"""Runs the C tests of the capabilities' handshake
(port/linux/game/network_extensions.c, docs/network_extensions.md) with the
build machine's C compiler, against stub headers for the game's own
(port/linux/tests/network_extensions_stubs)."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    """Finds a C compiler on the build machine: clang, cc or gcc, or None."""
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_network_extensions(tmp_path):
    """Builds the unit with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "network_extensions_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         f"-I{ROOT / 'port' / 'linux' / 'tests' / 'network_extensions_stubs'}",
         str(ROOT / "port" / "linux" / "tests" / "network_extensions_test.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
