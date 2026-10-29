#!/usr/bin/env python3
"""How large a recorded replay is, and what takes the room (port/linux/REPLAY.md, phase 0).

    python3 tools/replay_stats.py replays/20261027-201500_bloodgulch.ocreplay [...]

For each file: its header, how long the game ran, its size raw and through
zlib (levels 1, 6 and 9, the whole file as one stream, as phase 3 would
compress it) and per minute, and its bytes by record kind and by
distributed message type (port/linux/game/network_distributed.h, whose enum
names them; read when this runs, so a new type is named without changing
this file). The game's own messages (a player joining or leaving, the
game's end) are bit-packed by network_messages.c and counted only as a
whole.

The file (port/linux/game/network_replay.c): a header of REPLAY_HEADER_SIZE
bytes (216 in format 0, 280 from format 1), then records (from format 3 all
of them one zlib stream, which is inflated first: as much of it as there
is, of a file a crash cut short), each a kind byte, a zero byte, its size (16 bits) and
the host's tick (32 bits), little-endian, then that many bytes.
"""

import argparse
import datetime
import re
import struct
import sys
import zlib
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

MAGIC = b"OCREPLAY"
RECORD_HEADER = struct.Struct("<BBHi")
# network_replay.h's record kinds
KINDS = {1: "datagram", 2: "reliable", 3: "reliable to the recorder", 4: "game message", 5: "the game",
         6: "own input (a client's)"}
DATAGRAM, RELIABLE, RELIABLE_TO_MACHINE, GAME_MESSAGE, GAME = 1, 2, 3, 4, 5
# a distributed message's header: the message header (16 bits), its type,
# its count, its tick; a batch's message: its length (16 bits), then the
# header past the message header and its entries
MESSAGE_HEADER_SIZE = 8
BATCH_TYPE = "batch"

HEADER_FILE = Path(__file__).resolve().parent.parent / "port/linux/game/network_distributed.h"


def message_names(header_file: Path = HEADER_FILE) -> Dict[int, str]:
    """The distributed message types' names, by number, from the enum that
    _distributed_message_player_prediction begins."""
    names: Dict[int, str] = {}
    try:
        text = header_file.read_text()
    except OSError:
        return names
    start = text.find("_distributed_message_player_prediction")
    end = text.find("NUMBER_OF_DISTRIBUTED_MESSAGES", start)
    if start < 0 or end < 0:
        return names
    value = -1
    for match in re.finditer(r"\b_distributed_message_(\w+)\s*(?:=\s*(\d+))?\s*,", text[start:end]):
        value = int(match.group(2)) if match.group(2) else value + 1
        names[value] = match.group(1)
    return names


@dataclass
class Header:
    format: int
    network_version: int
    build: int
    ticks_per_second: int
    start_tick: int
    header_size: int
    unix_time: int
    map: str
    variant: str
    version: str
    # (format 4) the game's length in ticks, whether a client recorded it,
    # the game's (server's) name and the recording player's
    duration_ticks: int = 0
    client: bool = False
    server: str = ""
    player: str = ""


@dataclass
class Replay:
    header: Header
    # (the header and the records, inflated: format 3's records are a zlib
    # stream in the file)
    data: bytes
    records: List[Tuple[int, int, bytes]] = field(default_factory=list)
    truncated: bool = False
    file_size: int = 0


def _text(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", "replace")


def parse(data: bytes) -> Replay:
    if len(data) < 40 or data[:8] != MAGIC:
        raise ValueError("not a replay (no OCREPLAY header)")
    fmt, network_version, build, ticks, start, header_size, low, high = struct.unpack_from("<8I", data, 8)
    if header_size < 216 or header_size > len(data):
        raise ValueError(f"a header of {header_size} bytes")
    # (the version text: format 0's 32 bytes, 1 to 3's 96, to the header's end
    # before format 4's fields)
    version_end = 280 if fmt >= 4 and header_size >= 384 else header_size
    header = Header(fmt, network_version, build, ticks or 30, struct.unpack("<i", struct.pack("<I", start))[0],
                    header_size, low | (high << 32), _text(data[40:168]), _text(data[168:184]),
                    _text(data[184:version_end]))
    if fmt >= 4 and header_size >= 384:
        header.duration_ticks, flags = struct.unpack_from("<II", data, 280)
        header.client = bool(flags & 1)
        header.server = data[288:352].split(b"\0", 1)[0].decode("utf-8", "replace")
        header.player = data[352:384].split(b"\0", 1)[0].decode("utf-8", "replace")
    file_size = len(data)
    stream_cut = False
    if fmt >= 3:
        inflater = zlib.decompressobj()
        try:
            body = inflater.decompress(data[header_size:])
        except zlib.error as error:
            raise ValueError(f"its records cannot be inflated ({error})")
        stream_cut = not inflater.eof
        data = data[:header_size] + body
    replay = Replay(header, data, file_size=file_size, truncated=stream_cut)
    offset = header_size
    while offset < len(data):
        if offset + RECORD_HEADER.size > len(data):
            replay.truncated = True
            break
        kind, _, size, tick = RECORD_HEADER.unpack_from(data, offset)
        offset += RECORD_HEADER.size
        if offset + size > len(data):
            replay.truncated = True
            break
        replay.records.append((kind, tick, data[offset:offset + size]))
        offset += size
    return replay


def batch_messages(datagram: bytes) -> List[Tuple[int, int, int]]:
    """A batch's messages: (type, count, bytes taken in the datagram, its
    length word included)."""
    messages = []
    offset = MESSAGE_HEADER_SIZE
    while offset + 2 + 6 <= len(datagram):
        (length,) = struct.unpack_from("<H", datagram, offset)
        if length < 6 or offset + 2 + length > len(datagram):
            break
        messages.append((datagram[offset + 2], datagram[offset + 3], 2 + length))
        offset += 2 + length
    return messages


@dataclass
class Stats:
    seconds: float
    raw: int
    zlib: Dict[int, int]
    kinds: Dict[str, Tuple[int, int]]
    types: Dict[str, List[int]]


def measure(replay: Replay, names: Optional[Dict[int, str]] = None) -> Stats:
    names = names if names is not None else message_names()
    ticks = [tick for _, tick, _ in replay.records]
    seconds = (max(ticks) - replay.header.start_tick + 1) / replay.header.ticks_per_second if ticks else 0.0
    kinds: Dict[str, List[int]] = defaultdict(lambda: [0, 0])
    # type: [messages, entries, bytes]
    types: Dict[str, List[int]] = defaultdict(lambda: [0, 0, 0])
    for kind, _, payload in replay.records:
        entry = kinds[KINDS.get(kind, f"kind {kind}")]
        entry[0] += 1
        entry[1] += RECORD_HEADER.size + len(payload)
        if kind == DATAGRAM:
            inner = batch_messages(payload)
            # (the batch's own header and the record's)
            overhead = types["(batch and record headers)"]
            overhead[0] += 1
            overhead[2] += RECORD_HEADER.size + len(payload) - sum(size for _, _, size in inner)
            for message_type, count, size in inner:
                row = types[names.get(message_type, f"type {message_type}")]
                row[0] += 1
                row[1] += count
                row[2] += size
        elif kind in (RELIABLE, RELIABLE_TO_MACHINE) and len(payload) >= MESSAGE_HEADER_SIZE:
            row = types[names.get(payload[2], f"type {payload[2]}") + " (reliable)"]
            row[0] += 1
            row[1] += payload[3]
            row[2] += RECORD_HEADER.size + len(payload)
        else:
            row = types["(game messages)" if kind == GAME_MESSAGE else "(the game: struct network_game)"
                        if kind == GAME else "(own input: a client's)" if kind == 6 else f"(kind {kind})"]
            row[0] += 1
            row[2] += RECORD_HEADER.size + len(payload)
    return Stats(seconds, len(replay.data), {level: len(zlib.compress(replay.data, level)) for level in (1, 6, 9)},
                 {name: (value[0], value[1]) for name, value in kinds.items()}, dict(types))


def _size(size: float) -> str:
    return f"{size / 1024:,.0f} KB" if size < 1024 * 1024 else f"{size / (1024 * 1024):,.2f} MB"


def report(path: str, replay: Replay, stats: Stats) -> str:
    h = replay.header
    minutes = stats.seconds / 60 if stats.seconds else 0
    lines = [
        f"{path}",
        f"  the file: {_size(replay.file_size)}" + (" (its records zlibbed in it, format 3)" if h.format >= 3 else ""),
        f"  {h.map} ({h.variant or 'no variant'}), network version {h.network_version}, build {h.build}"
        f" {h.version}, replay format {h.format}" + (" (cut short: the game did not finish writing it)"
                                                     if replay.truncated else ""),
        f"  {stats.seconds:,.0f} seconds, {len(replay.records):,} records",
        *([f"  recorded {datetime.datetime.fromtimestamp(h.unix_time):%Y-%m-%d %H:%M}"
           f", {h.duration_ticks / h.ticks_per_second:,.0f} seconds long (the header's)"
           f", server {h.server or '?'}, {('played by ' if h.client else 'hosted by ') + h.player if h.player else 'by the server'}"]
          if h.format >= 4 else []),
        "  size:" + "".join(f"\n    {label:<10}{_size(size):>12}" + (f"  {_size(size / minutes)}/minute,"
                                                                      f" {_size(size / minutes * 10)} for 10 minutes"
                                                                      if minutes else "")
                             for label, size in [("raw", stats.raw)] +
                             [(f"zlib -{level}", size) for level, size in stats.zlib.items()]),
        "  by record kind:",
    ]
    for name, (count, size) in sorted(stats.kinds.items(), key=lambda item: -item[1][1]):
        lines.append(f"    {name:<28}{count:>9,} records {_size(size):>12} {100 * size / stats.raw:5.1f}%")
    lines.append("  by message type (messages, entries, bytes):")
    for name, (count, entries, size) in sorted(stats.types.items(), key=lambda item: -item[1][2]):
        lines.append(f"    {name:<40}{count:>9,} {entries:>10,} {_size(size):>12} {100 * size / stats.raw:5.1f}%")
    return "\n".join(lines)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("files", nargs="+", help=".ocreplay files")
    args = parser.parse_args(argv)
    names = message_names()
    failed = 0
    for path in args.files:
        try:
            replay = parse(Path(path).read_bytes())
        except (OSError, ValueError) as error:
            print(f"{path}: {error}", file=sys.stderr)
            failed = 1
            continue
        print(report(path, replay, measure(replay, names)))
    return failed


if __name__ == "__main__":
    sys.exit(main())
