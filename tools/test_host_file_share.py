"""The file connection between two SlipstreamCE machines (port/android/host/host_file_share.c,
docs/file_share_tcp_plan.md): its ciphers and HMAC against Python's cryptography, and a host and a
joiner in one process over loopback, with real files (built natively, the host's own sockets)."""
import hashlib
import hmac
import os
import shutil
import subprocess
from pathlib import Path

import pytest

cryptography = pytest.importorskip("cryptography")
from cryptography.hazmat.primitives import serialization  # noqa: E402
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey  # noqa: E402
from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "port/android/host"
SRC = ROOT / "port/linux/src"
MONOCYPHER = ROOT / "port/third_party/monocypher"
COMMON = [HOST / "host_file_share_crypto.c", HOST / "host_mcc_hash.c", MONOCYPHER / "monocypher.c"]


def build(work, name, sources, *defines):
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("no C compiler")
    exe = work / name
    result = subprocess.run([compiler, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-D_GNU_SOURCE",
                             "-DHOST_FILE_SHARE_TEST", "-DHOST_MCC_HASH_TEST", *defines,
                             f"-I{HOST}", f"-I{SRC}", f"-I{MONOCYPHER}", *map(str, sources), "-lpthread",
                             "-o", str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return exe


@pytest.fixture(scope="module")
def crypto(tmp_path_factory):
    work = tmp_path_factory.mktemp("fs-crypto")
    random = work / "random.c"
    random.write_text('#include <stdio.h>\n#include <stdlib.h>\n'
                      'void posix_random_bytes(void *b, unsigned int n) { FILE *f = fopen("/dev/urandom", "rb");'
                      ' if (!f || fread(b, 1, n, f) != n) abort(); fclose(f); }\n')
    return build(work, "crypto", [ROOT / "tools/tests/host_file_share_crypto_driver.c", *COMMON, random])


def driver(tmp_path_factory, name, port, *defines):
    work = tmp_path_factory.mktemp(name)
    return build(work, name, [ROOT / "tools/tests/host_file_share_driver.c", HOST / "host_file_share.c", *COMMON,
                              SRC / "posix_net.c"], f"-DHOST_FILE_SHARE_PORT={port}", *defines)


# (each driver its own port: the tests may run at once)
@pytest.fixture(scope="module")
def transfer(tmp_path_factory):
    return driver(tmp_path_factory, "transfer", 25154 + os.getpid() % 1000)


@pytest.fixture(scope="module")
def transfer_chacha(tmp_path_factory):
    return driver(tmp_path_factory, "transfer-chacha", 26154 + os.getpid() % 1000, "-DHOST_FILE_SHARE_NO_AES")


def aead(exe, suite, key, nonce, additional, plaintext):
    out = subprocess.run([str(exe), "aead", str(suite), key.hex(), nonce.hex(), additional.hex(), plaintext.hex()],
                         capture_output=True, text=True, check=True).stdout.split()
    if out == ["unavailable"]:
        pytest.skip("this CPU has no AES instructions")
    sealed = bytes.fromhex(out[0]) + (bytes.fromhex(out[1]) if plaintext else b"")
    return sealed, out[-2:]


@pytest.mark.parametrize("suite,cipher", [(1, AESGCM), (2, ChaCha20Poly1305)], ids=["aes-256-gcm", "chacha20-poly1305"])
@pytest.mark.parametrize("size", [0, 1, 15, 16, 17, 63, 64, 65, 127, 1000, 40000])
def test_aead_matches(crypto, suite, cipher, size):
    for additional_size in (0, 4, 17, 100):
        key, nonce = os.urandom(32), os.urandom(12)
        additional, plaintext = os.urandom(additional_size), os.urandom(size)
        sealed, checks = aead(crypto, suite, key, nonce, additional, plaintext)
        assert sealed == cipher(key).encrypt(nonce, plaintext, additional)
        # (opens; and with a bit changed, does not)
        assert checks == ["1", "0"]


def test_hmac_matches(crypto):
    for key_size in (0, 16, 32, 64, 65, 100):
        for data_size in (0, 10, 64, 200):
            key, data = os.urandom(key_size), os.urandom(data_size)
            got = subprocess.run([str(crypto), "hmac", key.hex(), data.hex()], capture_output=True, text=True,
                                 check=True).stdout.strip()
            assert got == hmac.new(key, data, hashlib.sha256).hexdigest()


def test_x25519_matches(crypto):
    a, b = X25519PrivateKey.generate(), X25519PrivateKey.generate()
    secret = a.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
                             serialization.NoEncryption())
    public = b.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    got = subprocess.run([str(crypto), "x25519", secret.hex(), public.hex()], capture_output=True, text=True,
                         check=True).stdout.split()
    assert got == ["1", a.exchange(b.public_key()).hex()]
    # (a public key of low order gives no secret)
    got = subprocess.run([str(crypto), "x25519", secret.hex(), "00" * 32], capture_output=True, text=True,
                         check=True).stdout.split()
    assert got[0] == "0"


def run(exe, tmp_path, mode, *extra):
    work = tmp_path / mode
    work.mkdir()
    result = subprocess.run([str(exe), str(work), mode, *map(str, extra)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    lines = result.stdout.splitlines()
    fields = dict(part.split("=", 1) for part in lines[0].split(" ") if "=" in part) if lines else {}
    return fields, result.stdout, result.stderr, work


@pytest.mark.parametrize("which", ["aes", "chacha"])
def test_transfer(transfer, transfer_chacha, tmp_path, which):
    """every file whole and in order (an empty one, one byte, records' edges), each checked against its
    SHA-256, and the host's count of what it sent"""
    fields, out, err, _ = run(transfer if which == "aes" else transfer_chacha, tmp_path, "ok")
    assert fields["state"] == "3" and fields["files"] == "7", out + err
    assert "DIFFERENT" not in out and out.count(" same") == 7
    assert "sent=3346803 files_sent=7 flags=10000" in out
    assert ("ChaCha20-Poly1305" in err) == (which == "chacha")


def test_rate_limit(transfer, tmp_path):
    """paced at the guest's cap"""
    fields, out, err, _ = run(transfer, tmp_path, "ok", 2 * 1024 * 1024)
    assert fields["state"] == "3"
    rate = float(err.split("received 3.2 MB in ")[1].split("(")[1].split(" MB/s")[0])
    assert 1.2 < rate < 2.6, err


def test_wrong_token_fails_before_writing(transfer, tmp_path):
    """keys made with another token open nothing: the joiner fails early (the host's chunks then)
    with no file touched"""
    fields, out, err, work = run(transfer, tmp_path, "wrong-token")
    assert fields["state"] == "4", out + err
    assert not list(work.glob("target*"))


def test_unknown_session_refused(transfer, tmp_path):
    fields, out, err, work = run(transfer, tmp_path, "unknown-session")
    assert fields["state"] == "4" and "the host refused the connection" in out
    assert not list(work.glob("target*"))


def test_bad_hash_fails_late(transfer, tmp_path):
    """a file that does not match its SHA-256 fails the download (after files were written)"""
    fields, out, err, _ = run(transfer, tmp_path, "bad-hash")
    assert fields["state"] == "5" and fields["files"] == "3" and "SHA-256" in out


def test_stop_joins(transfer, tmp_path):
    """stopping waits for the joiner's thread, which lets its file go"""
    fields, out, err, _ = run(transfer, tmp_path, "stop", 200000)
    assert "stopped state=0" in out


def test_pieces(transfer, tmp_path):
    """a Custom Edition map's pieces, as map_share.c's swarm asks them of the host: every piece of a
    3 MB file, each checked against its BLAKE2b hash on the joiner's host thread, the one given a
    wrong hash reported as not matching"""
    fields, out, err, _ = run(transfer, tmp_path, "pieces")
    assert "pieces taken=12 matched=11 wrong=0 state=2" in out, out + err


def test_many_pieces_one_connection(transfer, tmp_path):
    """a swarm's source is asked piece after piece all download long: far more than the host's wants
    hold at once (it closed the connection after 336 before)"""
    fields, out, err, _ = run(transfer, tmp_path, "many-pieces")
    assert "pieces taken=480 matched=480 wrong=0 state=2" in out, out + err


def rate(exe, script):
    out = subprocess.run([str(exe), "rate"], input=script, capture_output=True, text=True, timeout=30, check=True).stdout
    return [dict(part.split("=") for part in line.split()) for line in out.splitlines()]


MB = 1048576


def test_rate_outside_a_game_is_the_cap_and_learns_the_line(transfer):
    steps = rate(transfer, "f 0 0 0 10485760\nt 100\ns 10485760 300\nt 500\ns 10485760 300\nt 500\n")
    assert all(float(step["budget"]) == 10485760 for step in steps)
    assert float(steps[-1]["capacity"]) == 20 * MB and steps[-1]["measured"] == "1"


def test_rate_in_a_game_leaves_the_game_and_five_percent(transfer):
    steps = rate(transfer, "f 0 0 0 262144000\ns 10485760 300\nt 500\nf 1 1048576 40 262144000\nt 100\n"
                           "s 9000000 0\nt 500\n")
    assert float(steps[-1]["budget"]) == 0.95 * 20 * MB - MB


def test_rate_backs_off_when_the_players_pings_rise(transfer):
    steps = rate(transfer, "f 0 0 0 262144000\ns 10485760 300\nt 500\nf 1 1048576 40 262144000\nt 100\n"
                           "s 9000000 0\nt 500\nf 1 1048576 80 262144000\ns 9000000 0\nt 500\n"
                           "f 1 1048576 80 262144000\ns 9000000 0\nt 500\n")
    # (one half second over the threshold is not enough; the second running cuts, from the most of the last two s)
    assert float(steps[-2]["capacity"]) >= float(steps[-3]["capacity"])
    going = (10485760 * 1000 / 500 + MB) / 0.95
    assert abs(float(steps[-1]["capacity"]) - min(0.9 * going, float(steps[-2]["capacity"]))) < 2
    assert float(steps[-1]["capacity"]) < float(steps[-2]["capacity"])


def test_rate_in_a_game_with_the_line_unknown_starts_at_the_limit(transfer):
    """optimistic: as fast as the limit lets until the players' pings say otherwise"""
    steps = rate(transfer, "f 1 0 30 10485760\nt 100\ns 600000 0\nt 500\n")
    assert float(steps[0]["budget"]) == 10485760 and float(steps[1]["budget"]) == 10485760


def test_rate_backs_off_below_what_was_going_out(transfer):
    """the pings rising with C far above what goes out: C a tenth below what went out, at once"""
    steps = rate(transfer, "f 1 0 40 262144000\nt 100\ns 1000000 0\nt 500\nf 1 0 90 262144000\n"
                           "s 1000000 0\nt 500\nf 1 0 90 262144000\ns 1000000 0\nt 500\n")
    going = 1000000 * 1000 / 500 / 0.95
    assert abs(float(steps[-1]["capacity"]) - 0.9 * going) < 2


def test_rate_trickles_when_the_game_takes_the_line(transfer):
    steps = rate(transfer, "f 0 0 0 262144000\ns 1048576 300\nt 500\nf 1 5000000 30 262144000\nt 600\n")
    assert float(steps[-1]["budget"]) == 16384


def test_direct_falls_back(transfer, tmp_path):
    """the host's forwarded internet address answering nothing: the usual way, at once"""
    fields, out, err, _ = run(transfer, tmp_path, "direct-closed")
    assert fields["state"] == "3" and "no direct connection to the host" in err


def test_direct_first(transfer, tmp_path):
    """the host's forwarded internet address tried first (the usual one here a closed port)"""
    fields, out, err, _ = run(transfer, tmp_path, "direct-works")
    assert fields["state"] == "3" and "connected directly" in err and out.count(" same") == 7


def test_rate_a_full_socket_never_lowers_the_line_in_a_game(transfer):
    """a joiner stalling (or TCP ramping up over a long path) backs the socket up too: in a game a full
    socket raises C, the players' pings alone lower it (an internet joiner's download was held to 0.3
    MB/s of a 2.5 MB/s line)"""
    steps = rate(transfer, "f 0 0 0 262144000\ns 5000000 300\nt 500\nf 1 0 30 262144000\nt 100\n"
                           "s 100000 400\nt 500\n")
    assert float(steps[-1]["capacity"]) >= float(steps[0]["capacity"])


def test_rate_ignores_ping_jitter(transfer):
    """internet pings jitter: one reading 30 ms over the least, or a half second's average over it once, is not
    a queue (a 254 ms ping read 285 once and cut a download from 10.7 MB/s to 2.3); a long ping's margin is 15%"""
    steps = rate(transfer, "f 1 0 254 262144000\nt 100\ns 3000000 0\nt 500\n"
                           "f 1 0 300 262144000\nf 1 0 260 262144000\ns 3000000 0\nt 500\n"
                           "f 1 0 290 262144000\ns 3000000 0\nt 500\nf 1 0 260 262144000\ns 3000000 0\nt 500\n")
    capacities = [float(step["capacity"]) for step in steps]
    assert all(later >= earlier for earlier, later in zip(capacities, capacities[1:]))

