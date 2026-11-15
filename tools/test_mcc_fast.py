"""The MCC fast path between two SlipstreamCE machines (port/linux/game/mcc_fast.c), built natively over POSIX
sockets (tools/tests/mcc_fast_shim): a host and a joiner over loopback on port 5154 (the package's bytes in order,
the host's progress), a joiner with no fast host to reach (OpenCE's way at once), and a hello for another
session (refused)."""
from pathlib import Path
import shutil
import socket
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "port/linux/game"


@pytest.fixture(scope="module")
def driver(tmp_path_factory):
    clang = shutil.which("clang")
    if not clang:
        pytest.skip("clang required")
    work = tmp_path_factory.mktemp("mcc-fast")
    exe = work / "driver"
    result = subprocess.run([clang, "-std=gnu99", "-D_GNU_SOURCE", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-pointer-sign",
                             "-DMCC_FAST_IDLE_TIMEOUT=1000", "-I", str(ROOT / "tools/tests/mcc_fast_shim"), "-I", str(GAME),
                             str(GAME / "mcc_fast.c"), str(ROOT / "tools/tests/mcc_fast_driver.c"),
                             "-lpthread", "-o", str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return exe


def port_free():
    with socket.socket() as probe:
        # (as the listener does: a closed connection's TIME_WAIT does not hold the port)
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            probe.bind(("127.0.0.1", 5154))
            return True
        except OSError:
            return False


@pytest.mark.parametrize("case", ["transfer", "slow", "refused", "wrong_session"])
def test_case(driver, tmp_path, case):
    if not port_free():
        pytest.skip("port 5154 in use")
    result = subprocess.run([str(driver), case, str(tmp_path)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr


def test_negative_control_idle(tmp_path):
    """With a served connection closed for the joiner's silence, a file longer than the idle timeout is cut at its end."""
    clang = shutil.which("clang")
    if not clang:
        pytest.skip("clang required")
    if not port_free():
        pytest.skip("port 5154 in use")
    source = (GAME / "mcc_fast.c").read_text()
    assert source.count("!connection->served && elapsed(connection->heard_at, now, IDLE_TIMEOUT)") == 1
    faulty = source.replace("!connection->served && elapsed(connection->heard_at, now, IDLE_TIMEOUT)",
                            "!connection->remaining && connection->block_sent == connection->block_size && "
                            "elapsed(connection->heard_at, now, IDLE_TIMEOUT)")
    (tmp_path / "mcc_fast.c").write_text(faulty)
    exe = tmp_path / "driver"
    result = subprocess.run([clang, "-std=gnu99", "-D_GNU_SOURCE", "-Wall", "-Werror", "-Wno-unused-function",
                             "-Wno-pointer-sign", "-DMCC_FAST_IDLE_TIMEOUT=1000",
                             "-I", str(ROOT / "tools/tests/mcc_fast_shim"), "-I", str(GAME),
                             str(tmp_path / "mcc_fast.c"), str(ROOT / "tools/tests/mcc_fast_driver.c"),
                             "-lpthread", "-o", str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    work = tmp_path / "work"
    work.mkdir()
    result = subprocess.run([str(exe), "slow", str(work)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 1, "the faulty code passed 'slow': the test cannot see it"
    assert "mcc fast: lost the host" in result.stderr, result.stderr
