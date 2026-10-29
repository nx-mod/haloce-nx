"""tools/replay_stats.py against replays made here, as port/linux/game/network_replay.c writes them."""

import struct

from tools import replay_stats

NAMES = {2: "unit_states", 6: "object_states", 5: "object_changes"}


def header(start_tick=100, fmt=1):
    size = 280 if fmt else 216
    data = bytearray(size)
    data[:8] = b"OCREPLAY"
    struct.pack_into("<8I", data, 8, fmt, 39, 125, 30, start_tick, size, 1761600000, 0)
    data[40:40 + 10] = b"bloodgulch"
    data[168:168 + 6] = b"Slayer"
    version = b"OpenCE Android | local build (0123456789ab)" if fmt else b"1.125"
    data[184:184 + len(version)] = version
    return bytes(data)


def record(kind, tick, payload):
    return struct.pack("<BBHi", kind, 0, len(payload), tick) + payload


def batch(*messages):
    """A datagram of the batched messages: (type, count, entries)."""
    data = bytearray(struct.pack("<HBBi", 0, 16, 0, 0))
    for message_type, count, entries in messages:
        data += struct.pack("<HBBi", 6 + len(entries), message_type, count, 0) + entries
    return bytes(data)


def replay_bytes():
    datagram = batch((2, 3, b"\x01" * 105), (6, 2, b"\x02" * 40))
    reliable = struct.pack("<HBBi", 0, 5, 1, 0) + b"\x03" * 12
    return (header() + record(1, 100, datagram) + record(2, 101, reliable) + record(4, 129, b"\x04" * 20),
            datagram, reliable)


def test_header_and_records():
    data, datagram, reliable = replay_bytes()
    replay = replay_stats.parse(data)
    assert replay.header.map == "bloodgulch"
    assert replay.header.variant == "Slayer"
    assert replay.header.network_version == 39 and replay.header.build == 125
    assert replay.header.start_tick == 100
    assert [(kind, tick) for kind, tick, _ in replay.records] == [(1, 100), (2, 101), (4, 129)]
    assert replay.records[0][2] == datagram and replay.records[1][2] == reliable
    assert not replay.truncated


def test_batch_messages():
    _, datagram, _ = replay_bytes()
    assert replay_stats.batch_messages(datagram) == [(2, 3, 2 + 6 + 105), (6, 2, 2 + 6 + 40)]


def test_measure():
    data, datagram, reliable = replay_bytes()
    stats = replay_stats.measure(replay_stats.parse(data), NAMES)
    # (ticks 100 to 129: a second)
    assert stats.seconds == 1.0
    assert stats.raw == len(data)
    assert stats.kinds["datagram"] == (1, 8 + len(datagram))
    assert stats.kinds["game message"] == (1, 8 + 20)
    assert stats.types["unit_states"] == [1, 3, 113]
    assert stats.types["object_states"] == [1, 2, 48]
    assert stats.types["object_changes (reliable)"] == [1, 1, 8 + len(reliable)]
    assert stats.types["(batch and record headers)"] == [1, 0, 8 + 8]
    # every byte counted once
    assert sum(row[2] for row in stats.types.values()) == stats.raw - 280
    assert set(stats.zlib) == {1, 6, 9}


def test_format_0():
    data = header(fmt=0) + record(1, 100, batch((2, 1, b"\x01" * 35)))
    replay = replay_stats.parse(data)
    assert replay.header.format == 0 and replay.header.version == "1.125"
    assert len(replay.records) == 1


def test_version_whole():
    data, _, _ = replay_bytes()
    assert replay_stats.parse(data).header.version == "OpenCE Android | local build (0123456789ab)"


def test_cut_short():
    data, _, _ = replay_bytes()
    replay = replay_stats.parse(data[:-5])
    assert replay.truncated
    assert len(replay.records) == 2


def test_not_a_replay():
    try:
        replay_stats.parse(b"not a replay at all" * 4)
    except ValueError:
        return
    raise AssertionError("parsed")


def test_names_from_the_header():
    names = replay_stats.message_names()
    assert names[2] == "unit_states" and names[16] == "batch" and names[64] == "actor_states"


def test_report_runs():
    data, _, _ = replay_bytes()
    replay = replay_stats.parse(data)
    text = replay_stats.report("x.ocreplay", replay, replay_stats.measure(replay, NAMES))
    assert "bloodgulch (Slayer)" in text and "unit_states" in text


def test_format_3_inflated():
    import zlib
    data, datagram, reliable = replay_bytes()
    head = bytearray(data[:280])
    struct.pack_into("<I", head, 8, 3)
    compressed = bytes(head) + zlib.compress(data[280:], 1)
    replay = replay_stats.parse(compressed)
    assert replay.header.format == 3 and not replay.truncated
    assert [record[2] for record in replay.records[:2]] == [datagram, reliable]
    assert replay.file_size == len(compressed)


def test_format_3_cut_short():
    import zlib
    data, _, _ = replay_bytes()
    head = bytearray(data[:280])
    struct.pack_into("<I", head, 8, 3)
    compressor = zlib.compressobj(1)
    body = compressor.compress(data[280:]) + compressor.flush(zlib.Z_SYNC_FLUSH)
    replay = replay_stats.parse(bytes(head) + body)
    assert replay.truncated and len(replay.records) == 3


def test_format_4_metadata():
    import zlib
    data, datagram, _ = replay_bytes()
    head = bytearray(384)
    head[:280] = data[:280]
    struct.pack_into("<I", head, 8, 4)
    struct.pack_into("<I", head, 28, 384)
    struct.pack_into("<II", head, 280, 900, 1)
    head[288:288 + 6] = "Lobby\u00e9".encode("utf-8")[:6]
    head[352:352 + 5] = b"Carlo"
    replay = replay_stats.parse(bytes(head) + zlib.compress(data[280:], 1))
    h = replay.header
    assert h.format == 4 and h.duration_ticks == 900 and h.client and h.player == "Carlo"
    assert h.version == "OpenCE Android | local build (0123456789ab)"
    assert replay.records[0][2] == datagram
    text = replay_stats.report("x", replay, replay_stats.measure(replay, NAMES))
    assert "30 seconds long" in text and "played by Carlo" in text

