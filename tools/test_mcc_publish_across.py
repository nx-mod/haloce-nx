"""posix_mcc_share_publish (port/linux/src/posix_mcc_share.c) across storages: Android's GAME FILES can move mcc_maps
to a memory card (game.mcc.path) while mcc_staging stays in the data folder, where a rename cannot go (EXDEV). The
package is copied beside its place, renamed in (still refusing one there), and the staged copy removed. /dev/shm
(tmpfs) and the temporary folder are two file systems here, as the phone's storage and its card are."""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]

DRIVER = r"""
#include <stdio.h>
#include "posix.h"
int main(int argc, char **argv)
{
    (void)argc;
    printf("%d\n", posix_mcc_share_publish(argv[1], argv[2]));
    return 0;
}
"""


@pytest.fixture(scope="module")
def publish(tmp_path_factory):
    clang = shutil.which("clang")
    if not clang:
        pytest.skip("clang required")
    work = tmp_path_factory.mktemp("publish")
    (work / "driver.c").write_text(DRIVER)
    exe = work / "publish"
    subprocess.run([clang, "-std=gnu99", "-w", "-I", str(ROOT / "port/linux/src"), str(work / "driver.c"),
                    str(ROOT / "port/linux/src/posix_mcc_share.c"), str(ROOT / "port/linux/src/posix_files.c"),
                    "-o", str(exe)], check=True)
    return exe


@pytest.fixture
def card():
    if not os.path.isdir("/dev/shm"):
        pytest.skip("no /dev/shm")
    folder = Path(tempfile.mkdtemp(dir="/dev/shm", prefix="card-"))
    yield folder
    shutil.rmtree(folder, ignore_errors=True)


def run(publish, source, target):
    return subprocess.run([str(publish), str(source), str(target)], capture_output=True, text=True,
                          check=True).stdout.strip() == "1"


def staged(data):
    package = data / "mcc_staging" / "steam_1" / "content"
    (package / "maps").mkdir(parents=True)
    (package / "maps" / "one.map").write_bytes(os.urandom(300000))
    (package / "ModInfo.json").write_text("{}")
    return package


def test_a_package_published_to_another_storage(publish, tmp_path, card):
    if os.stat(tmp_path).st_dev == os.stat(card).st_dev:
        pytest.skip("the temporary folder and /dev/shm are one file system here")
    package = staged(tmp_path)
    contents = (package / "maps" / "one.map").read_bytes()
    (card / "mcc_maps").mkdir()
    target = card / "mcc_maps" / "1234"
    assert run(publish, package, target)
    assert (target / "maps" / "one.map").read_bytes() == contents
    assert (target / "ModInfo.json").read_text() == "{}"
    assert not package.exists()
    assert not (card / "mcc_maps" / "1234.copying").exists()


def test_one_there_is_still_refused(publish, tmp_path, card):
    package = staged(tmp_path)
    (card / "mcc_maps" / "1234").mkdir(parents=True)
    (card / "mcc_maps" / "1234" / "old.map").write_bytes(b"old")
    assert not run(publish, package, card / "mcc_maps" / "1234")
    assert (card / "mcc_maps" / "1234" / "old.map").read_bytes() == b"old"
    assert (package / "maps" / "one.map").is_file()


def test_a_link_in_the_package_refuses_the_copy(publish, tmp_path, card):
    if os.stat(tmp_path).st_dev == os.stat(card).st_dev:
        pytest.skip("the temporary folder and /dev/shm are one file system here")
    package = staged(tmp_path)
    (package / "maps" / "elsewhere.map").symlink_to("/etc/hostname")
    (card / "mcc_maps").mkdir()
    assert not run(publish, package, card / "mcc_maps" / "1234")
    assert not (card / "mcc_maps" / "1234").exists()
    assert not (card / "mcc_maps" / "1234.copying").exists()
    assert (package / "maps" / "one.map").is_file()
