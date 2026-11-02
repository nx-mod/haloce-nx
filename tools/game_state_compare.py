#!/usr/bin/env python3
"""Compare two images of the game state (cores, or savegame.bin files) taken
with the Xbox memory window at different addresses, to find which of its
words are addresses.

A core (config.toml's debug.core_dump_tick: d:\\core\\game_state_<address>.bin)
or a saved game is the game state's memory as it was. Two of the same moment
of the same level, from runs whose windows lay at different places, differ
where a word holds an address into the window: by exactly the distance
between the windows. Each aligned word is put in one of these:

  pointer      in each run's window, at the same offset from its base
  image        in each run's game image (code and globals), at the same
               offset from its base: equal values when the image did not
               move between the runs (debug.image_base moves it)
  changed      in each run's window, at different offsets (the runs differ
               there: what it points at moved, or the game did not play
               the same)
  false        the same value in both, and in one run's window: a scan of
               that run for addresses would have taken it for one
  differs      different, and not in the windows (the runs differ there)
  same         the same, and in neither window

and is said with the allocation it lies in: a data array (its header and
its elements), a memory pool, an lruv cache (found by their signatures), or
none of those (the game state's plain allocations).

    tools/game_state_compare.py A.bin 0xc0000000 B.bin 0xa0000000 \
        --first-image 0x3c000000 --second-image 0x50000000

The addresses are each run's window base (its game state lies at
0x1a00000 into it: halo_port_capacity.h's HALO_PORT_GAME_STATE_BASE_ADDRESS)
and, if the image moved, each run's image base (the log's "debug.image_base:
the guest image at ..."; 0x3c000000 where it was linked).

The script threads' stacks (hs thread) pack their frames at any byte, so
their pointers need not be aligned: --unaligned also looks at the three
other offsets of each word, for window and image pointers only.
"""

import argparse
import struct
import sys
from collections import Counter, defaultdict

GAME_STATE_OFFSET = 0x01A00000
WINDOW_SIZE = 0x20000000
IMAGE_BASE = 0x3C000000
IMAGE_SIZE = 0x04000000  # halo_android_abi.h's HALO_GUEST_IMAGE_RESERVE

DATA_SIGNATURE = 0x64407440  # 'd@t@'
POOL_SIGNATURE = 0x706F6F6C  # 'pool'
LRUV_SIGNATURE = 0x77656565  # 'weee'


def cstring(data, offset, size):
    raw = data[offset:offset + size]
    return raw.split(b"\0", 1)[0].decode("latin-1")


def regions(image, game_state_base):
    """[start, end, name] of the allocations the image's signatures give,
    as offsets into the image"""
    found = []
    words = len(image) // 4
    for index in range(words):
        value = struct.unpack_from("<I", image, index * 4)[0]
        offset = index * 4
        if value == DATA_SIGNATURE and offset >= 0x28:
            start = offset - 0x28
            name = cstring(image, start, 32)
            maximum_count, size = struct.unpack_from("<hh", image, start + 0x20)
            elements = struct.unpack_from("<I", image, start + 0x34)[0]
            if elements - game_state_base != start + 0x38 or maximum_count <= 0 or size <= 0:
                continue
            found.append([start, start + 0x38, "data " + name + " (header)"])
            found.append([start + 0x38, start + 0x38 + maximum_count * size,
                "data " + name + " (%d of %d bytes)" % (maximum_count, size)])
        elif value == POOL_SIGNATURE:
            name = cstring(image, offset + 4, 32)
            base, size = struct.unpack_from("<Ii", image, offset + 0x24)
            if base - game_state_base != offset + 0x38 or size <= 0:
                continue
            found.append([offset, offset + 0x38, "pool " + name + " (header)"])
            found.append([offset + 0x38, offset + 0x38 + size, "pool " + name])
        elif value == LRUV_SIGNATURE and offset >= 0x40:
            start = offset - 0x40
            name = cstring(image, start, 32)
            blocks = struct.unpack_from("<I", image, start + 0x3C)[0]
            if blocks - game_state_base != start + 0x44:
                continue
            found.append([start, start + 0x44, "lruv " + name + " (header)"])
    found.sort()
    return found


def pool_blocks(image, game_state_base):
    """{offset of a block's data: its size} for each memory pool's blocks,
    walked from the pool's first block"""
    blocks = {}
    for index in range(len(image) // 4):
        offset = index * 4
        if struct.unpack_from("<I", image, offset)[0] != POOL_SIGNATURE:
            continue
        base, size, free, first = struct.unpack_from("<IiiI", image, offset + 0x24)
        if base - game_state_base != offset + 0x38:
            continue
        block = first
        while block and len(blocks) < 100000:
            at = block - game_state_base
            if not 0 <= at < len(image) - 0x18:
                break
            block_size = struct.unpack_from("<i", image, at + 4)[0]
            blocks[at + 0x18] = block_size - 0x18
            block = struct.unpack_from("<I", image, at + 0xC)[0]
    return blocks


def field(offset, allocations, blocks):
    """where a word lies: its allocation, and the field (the offset into its
    element, or into its pool block's data)"""
    for start, end, label in allocations:
        if start <= offset < end and label.startswith("data ") and not label.endswith("(header)"):
            size = int(label.rsplit(" of ", 1)[1].split()[0])
            return "%s +%x" % (label.split(" (")[0], (offset - start) % size)
    for data, size in blocks.items():
        if data - 0x18 <= offset < data + size:
            return "pool block +%x" % (offset - data) if offset >= data else "pool block header +%x" % (offset - data + 0x18)
    for start, end, label in allocations:
        if start <= offset < end:
            return "%s +%x" % (label, offset - start)
    return "(no signature) +%07x" % offset


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("first")
    parser.add_argument("first_window", type=lambda text: int(text, 0))
    parser.add_argument("second")
    parser.add_argument("second_window", type=lambda text: int(text, 0))
    parser.add_argument("--first-image", type=lambda text: int(text, 0), default=IMAGE_BASE,
        help="the first run's image base (default: where it was linked)")
    parser.add_argument("--second-image", type=lambda text: int(text, 0), default=IMAGE_BASE,
        help="the second run's image base")
    parser.add_argument("--unaligned", action="store_true",
        help="also the pointers at the other three offsets of each word")
    parser.add_argument("--list", type=int, default=40, help="false positives and changed words to list")
    parser.add_argument("--fields", action="store_true", help="the pointers counted by the field they are in")
    arguments = parser.parse_args()

    first = open(arguments.first, "rb").read()
    second = open(arguments.second, "rb").read()
    if len(first) != len(second):
        sys.exit("the images are of different sizes")
    first_window, second_window = arguments.first_window, arguments.second_window
    first_state, second_state = first_window + GAME_STATE_OFFSET, second_window + GAME_STATE_OFFSET

    allocations = regions(first, first_state)
    print("%d allocations found by signature" % len(allocations))

    def allocation(offset):
        name = "(no signature)"
        for start, end, label in allocations:
            if start <= offset < end:
                name = label
            if start > offset:
                break
        return name

    def in_window(value, base):
        return base <= value < base + WINDOW_SIZE

    first_image, second_image = arguments.first_image, arguments.second_image

    def in_image(value, base):
        return base <= value < base + IMAGE_SIZE

    count = len(first) // 4
    firsts = struct.unpack("<%dI" % count, first)
    seconds = struct.unpack("<%dI" % count, second)
    totals = Counter()
    by_allocation = defaultdict(Counter)
    targets = Counter()
    false_positives = []
    changed = []
    pointers = []
    image_pointers = []
    for index in range(count):
        a, b = firsts[index], seconds[index]
        a_in, b_in = in_window(a, first_window), in_window(b, second_window)
        if a_in and b_in and a - first_window == b - second_window:
            kind = "pointer"
            pointers.append(index * 4)
            target = a - first_window
            if GAME_STATE_OFFSET <= target < GAME_STATE_OFFSET + len(first):
                targets["the game state"] += 1
            else:
                targets["window +%x MB" % (target >> 20)] += 1
        elif a_in and b_in:
            kind = "changed"
            changed.append(index * 4)
        elif a == b and (in_window(a, first_window) or in_window(a, second_window)):
            kind = "false"
            false_positives.append(index * 4)
        elif in_image(a, first_image) and in_image(b, second_image) and a - first_image == b - second_image:
            kind = "image"
            image_pointers.append(index * 4)
        elif a != b:
            kind = "differs"
        else:
            kind = "same"
        totals[kind] += 1
        if kind != "same":
            by_allocation[allocation(index * 4)][kind] += 1

    print("words: " + ", ".join("%s %d" % (kind, totals[kind]) for kind in
        ("pointer", "changed", "false", "image", "differs", "same")))
    print("pointers point into: " + ", ".join("%s %d" % item for item in targets.most_common()))
    print()
    print("%-60s %8s %8s %6s %6s %8s" % ("allocation", "pointer", "changed", "false", "image", "differs"))
    for name, kinds in sorted(by_allocation.items(), key=lambda item: -sum(item[1].values())):
        print("%-60s %8d %8d %6d %6d %8d" % (name[:60], kinds["pointer"], kinds["changed"], kinds["false"],
            kinds["image"], kinds["differs"]))
    unaligned = []
    if arguments.unaligned:
        for offset in range(len(first) - 3):
            if offset & 3 == 0:
                continue
            a = struct.unpack_from("<I", first, offset)[0]
            b = struct.unpack_from("<I", second, offset)[0]
            if a == b:
                continue
            if in_window(a, first_window) and in_window(b, second_window) and a - first_window == b - second_window:
                unaligned.append((offset, "pointer"))
            elif in_image(a, first_image) and in_image(b, second_image) and a - first_image == b - second_image:
                unaligned.append((offset, "image"))
        print()
        print("unaligned: %d window pointers, %d image pointers" % (
            sum(1 for _, kind in unaligned if kind == "pointer"), sum(1 for _, kind in unaligned if kind == "image")))
    if arguments.fields:
        blocks = pool_blocks(first, first_state)
        for title, offsets in (("pointers", pointers), ("image pointers", image_pointers),
                ("unaligned pointers", [offset for offset, _ in unaligned])):
            if not offsets or (title == "image pointers" and first_image == second_image):
                continue
            fields = Counter(field(offset, allocations, blocks) for offset in offsets)
            print()
            print("%s by field (%d pool blocks):" % (title, len(blocks)))
            for name, number in sorted(fields.items()):
                print("  %-60s %6d" % (name, number))
    for title, offsets in (("false positives", false_positives), ("changed", changed)):
        print()
        print("%s (the first %d of %d):" % (title, min(arguments.list, len(offsets)), len(offsets)))
        for offset in offsets[:arguments.list]:
            a = struct.unpack_from("<I", first, offset)[0]
            b = struct.unpack_from("<I", second, offset)[0]
            print("  +%07x %08x %08x  %s" % (offset, a, b, allocation(offset)))


if __name__ == "__main__":
    main()
