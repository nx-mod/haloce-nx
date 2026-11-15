"""The map lists' IMPORT (port/android/host/host_mcc_import.c, the host's), built natively and run over a data root whose import inbox
holds .map and .zip files: Custom Edition maps into custom_maps, MCC packages, resource sets and maps alone into
mcc_maps, a package on disk asked about (replace or keep), refusals, and an import cut short recovered."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[1]
ZLIB = ROOT / "port/third_party/zlib"


@pytest.fixture(scope="module")
def importer(tmp_path_factory):
    clang = shutil.which("clang")
    if not clang:
        pytest.skip("clang required")
    work = tmp_path_factory.mktemp("mcc-import")
    objects = []
    # (the platform layer's posix files are the host's: no game headers)
    for source in ["posix_files.c", "posix_mcc_share.c"]:
        objects.append(work / (source + ".o"))
        subprocess.run([clang, "-std=gnu99", "-w", "-c", str(ROOT / "port/linux/src" / source), "-o", str(objects[-1])],
                       check=True)
    for source in ["adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c"]:
        objects.append(work / (source + ".o"))
        subprocess.run([clang, "-std=gnu99", "-w", "-DZ_PREFIX", "-c", str(ZLIB / source), "-o", str(objects[-1])],
                       check=True)
    for source in [ROOT / "port/android/host/host_mcc_import.c", ROOT / "port/linux/src/server_json.c",
                   ROOT / "tools/tests/mcc_import_driver.c"]:
        objects.append(work / (source.name + ".o"))
        # (as strict as the PS5's host, which builds it with -Wall -Wextra -Werror)
        warnings = ["-Wall", "-Wextra", "-Werror"] if source.name == "host_mcc_import.c" else ["-w"]
        result = subprocess.run([clang, "-std=c11", "-D_GNU_SOURCE", *warnings, "-I", str(ROOT / "port/android/host"), "-I", str(ROOT / "port/android/include"),
                                 "-I", str(ROOT / "port/linux/src"), "-I", str(ZLIB),
                                 "-c", str(source), "-o", str(objects[-1])], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
    exe = work / "importer"
    subprocess.run([clang, *map(str, objects), "-lpthread", "-o", str(exe)], check=True)
    return exe


def mcc_map(kind=0):
    data = bytearray(2088)
    struct.pack_into("<IIII", data, 0, 0x68656164, 13, len(data), 0)
    struct.pack_into("<II", data, 16, 2048, 40)
    struct.pack_into("<H", data, 0x60, kind)
    struct.pack_into("<I", data, 0x7fc, 0x666f6f74)
    return bytes(data)


def ce_map(version=609):
    data = bytearray(4096)
    struct.pack_into("<II", data, 0, 0x68656164, version)
    struct.pack_into("<I", data, 0x7fc, 0x666f6f74)
    return bytes(data)


def zip_file(path, files, zip64=False, method=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, "w", method) as archive:
        for name, data in files.items():
            with archive.open(zipfile.ZipInfo(name), "w", force_zip64=zip64) as entry:
                entry.write(data)


def modinfo(title, workshop_id=None, campaign=False):
    info = {"Engine": "Halo1", "Title": {"Neutral": title}, "GameModContents": {"HasCampaign": campaign}}
    if workshop_id is not None:
        info["ModIdentifier"] = {"HostedModIds": {"SteamWorkshopId": workshop_id}}
    return json.dumps(info).encode()


def run(importer, root, answers=""):
    result = subprocess.run([str(importer), answers], capture_output=True, text=True, timeout=60,
                            env={**os.environ, "IMPORT_ROOT": str(root)})
    assert result.returncode == 0, result.stderr
    lines = result.stdout.splitlines()
    report = next(line[len("REPORT "):] for line in lines if line.startswith("REPORT "))
    asked = [line[len("ASK "):] for line in lines if line.startswith("ASK ")]
    return report, asked


def files(root):
    return sorted(p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file())


@pytest.fixture
def root(tmp_path):
    (tmp_path / "import").mkdir()
    return tmp_path


def campaign_package(kind='"Default"'):
    second = '{"ScenarioFile":"two"' + (f',"CampaignMapKind":{kind}' if kind is not None else "") + "}"
    return {"pkg/ModInfo.json": modinfo("Test Campaign", 1234567890, campaign=True),
            "pkg/CampaignInfo.json": json.dumps({"CampaignMaps": ["m/one.json", "m/two.json"]}).encode(),
            "pkg/m/one.json": b'{"ScenarioFile":"one","CampaignMapKind":"Default"}',
            "pkg/m/two.json": second.encode(),
            "pkg/halo1/maps/one.map": mcc_map(), "pkg/halo1/maps/two.map": mcc_map(),
            "pkg/images/large.png": b"\x89PNG"}


def test_everything_sorted_into_place(importer, root):
    inbox = root / "import"
    (inbox / "Lone Map.map").write_bytes(ce_map())
    zip_file(inbox / "Two Maps.zip", {"x/alpha.map": ce_map(), "x/beta.map": ce_map(), "x/alpha.txt": b"Alpha"})
    zip_file(inbox / "campaign.zip", campaign_package())
    zip_file(inbox / "mcc resources.zip", {
        "Halo The Master Chief Collection/halo1/maps/custom_edition/bitmaps.map": b"\x01\0\0\0" + bytes(60),
        "Halo The Master Chief Collection/halo1/maps/custom_edition/sounds.map": b"\x02\0\0\0" + bytes(60)},
        zip64=True)
    zip_file(inbox / "Loose MCC.zip", {"arena.map": mcc_map(1), "arena.txt": b"an arena"}, method=zipfile.ZIP_STORED)
    (inbox / "stray.map").write_bytes(mcc_map(1))
    report, asked = run(importer, root)
    assert asked == []
    assert files(root) == [
        "custom_maps/Lone Map.map", "custom_maps/Two Maps/alpha.map", "custom_maps/Two Maps/alpha.txt",
        "custom_maps/Two Maps/beta.map",
        "mcc_maps/1234567890/campaigninfo.json", "mcc_maps/1234567890/images/large.png",
        "mcc_maps/1234567890/m/one.json", "mcc_maps/1234567890/m/two.json", "mcc_maps/1234567890/modinfo.json",
        "mcc_maps/1234567890/one.map", "mcc_maps/1234567890/two.map", "mcc_maps/1234567890/workshop_id.txt",
        "mcc_maps/Loose MCC/arena.map", "mcc_maps/Loose MCC/arena.txt",
        "mcc_maps/_shared/multiplayer/bitmaps.map", "mcc_maps/_shared/multiplayer/sounds.map"]
    assert (root / "mcc_maps/1234567890/workshop_id.txt").read_text() == "1234567890\nTest Campaign\n"
    assert "stray: it is an MCC map: import its package's .zip" in report
    assert "Installed " in report and "Imported Lone Map, Two Maps/alpha, Two Maps/beta." in report
    assert report.endswith("CAMPAIGN MISSIONS in the MCC Packages settings.")


def test_packages_on_disk_are_asked_about(importer, root):
    inbox = root / "import"
    zip_file(inbox / "campaign.zip", campaign_package())
    zip_file(inbox / "Loose MCC.zip", {"arena.map": mcc_map(1), "arena.txt": b"an arena"})
    run(importer, root)
    zip_file(inbox / "campaign v2.zip", {"ModInfo.json": modinfo("Test Campaign v2", "1234567890"),
                                         "maps/one.map": mcc_map(1), "maps/new.map": mcc_map(1)})
    zip_file(inbox / "Loose MCC.zip", {"arena.map": mcc_map(1)})
    report, asked = run(importer, root, "rk")
    # (in the order of the files' names: campaign v2.zip's package replaced, Loose MCC.zip's kept)
    assert asked == ["Test Campaign v2|1234567890", "Loose MCC|Loose MCC"]
    assert "Installed Test Campaign v2 (mcc_maps/1234567890)." in report
    assert "Kept the version on disk of Loose MCC." in report
    names = files(root)
    assert "mcc_maps/1234567890/new.map" in names and "mcc_maps/1234567890/two.map" not in names
    assert "mcc_maps/Loose MCC/arena.txt" in names
    assert not any(name.startswith("mcc_staging/") for name in names)
    assert files(inbox) == []


@pytest.mark.parametrize("kind,accepted", [('"Default"', True), ('"dEfAuLt"', True), (None, True), ('"Hub"', False),
                                           ('"Flashback"', False), ('""', False), ("0", False), ("null", False)])
def test_campaign_mission_kinds(importer, root, kind, accepted):
    zip_file(root / "import/campaign.zip", campaign_package(kind))
    report, _ = run(importer, root)
    assert (root / "mcc_maps/1234567890/two.map").exists() == accepted, report
    if not accepted:
        assert "campaign.zip: " in report


@pytest.mark.parametrize("files_in_zip,why", [
    ({"ModInfo.json": modinfo("Evil", 7), "maps/a.map": mcc_map(1), "../evil.txt": b"x"}, "unsafe path"),
    ({"ModInfo.json": json.dumps({"Engine": "Halo2"}).encode(), "maps/a.map": mcc_map(1)}, "only Halo 1 MCC packages"),
    ({"a.map": mcc_map(1), "b.map": ce_map()}, "a Custom Edition map, in a .zip of MCC maps"),
    ({"Halo/halo1/maps/custom_edition/bitmaps.map": bytes(64)}, "need both bitmaps.map and sounds.map"),
    ({"ModInfo.json": modinfo("No Maps", 7), "readme.txt": b"hi"}, "no MCC maps"),
    ({"ModInfo.json": modinfo("Bad Name", 7), "maps/Bad Name!.map": mcc_map(1)}, "cannot be launched"),
])
def test_refused(importer, root, files_in_zip, why):
    zip_file(root / "import/package.zip", files_in_zip)
    report, _ = run(importer, root)
    assert why in report, report
    assert not (root / "mcc_maps").exists() or files(root / "mcc_maps") == []
    assert not any(name.startswith("mcc_staging/") for name in files(root))


def test_custom_edition_refusals(importer, root):
    inbox = root / "import"
    (inbox / "xbox.map").write_bytes(ce_map(5))
    (inbox / "not a map.map").write_bytes(b"hello")
    (inbox / "bitmaps.map").write_bytes(b"\x02\0\0\0" + bytes(60))
    zip_file(inbox / "empty.zip", {"readme.txt": b"nothing"})
    (inbox / "broken.zip").write_bytes(b"PK not really")
    (inbox / "still copying.map.part").write_bytes(ce_map())
    report, _ = run(importer, root)
    assert "xbox: it is an Xbox map, not a Custom Edition one" in report
    assert "not a map: it is not a map file" in report
    assert "bitmaps: it is not Custom Edition's resource map" in report
    assert "empty.zip: it has no .map file" in report
    assert "broken.zip: it is not a .zip file" in report
    # (a file still being copied is left for the next import)
    assert files(inbox) == ["still copying.map.part"]


def test_a_path_out_of_the_folder_goes_nowhere(importer, root):
    # (a Custom Edition .zip's map is named for its file alone: "../" cannot place it outside custom_maps)
    zip_file(root / "import/maps.zip", {"../../escape.map": ce_map()})
    report, _ = run(importer, root)
    assert files(root) == ["custom_maps/escape.map"], report


def test_a_map_there_already_is_kept(importer, root):
    (root / "custom_maps").mkdir()
    (root / "custom_maps/arena.map").write_bytes(b"mine")
    (root / "import/arena.map").write_bytes(ce_map())
    report, _ = run(importer, root)
    assert report.startswith("arena was already there.")
    assert (root / "custom_maps/arena.map").read_bytes() == b"mine"


def test_an_import_cut_short_is_recovered(importer, root):
    # (a replacement that had moved the old package aside, and stopped before the new one was moved in)
    staging = root / "mcc_staging/import-000000000001"
    (staging / "previous").mkdir(parents=True)
    (staging / "previous/old.map").write_bytes(b"old")
    (staging / "owner").write_bytes(b"mcc-import-v1")
    (staging / "destination").write_bytes(b"Arena")
    (root / "mcc_maps").mkdir()
    run(importer, root)
    assert (root / "mcc_maps/Arena/old.map").read_bytes() == b"old"
    assert not staging.exists()
