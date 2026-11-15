"""The bots' ground mended and mapped (port/linux/game/bots_nav.c, compiled as it is): the surfaces a biped walks that
the walkable surfaces leave out opened (ramps, but not slopes past the limit, ceilings, glass or ground joined to none),
the vehicles' flatter share, the ground's pieces, and the ladders' and teleporters' routes between pieces. Negative
controls: a fault each case must catch."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["open", "components", "route", "coarse"]
NAV = "port/linux/game/bots_nav.c"

NEGATIVE_CONTROLS = {
    "slope-ignored": ("if (k < minimum_k)\n", "if (k < -2.0f)\n", "open"),
    "ceilings-up": ("\tif (surface->plane_designator < 0)\n\t\tk = -k;\n", "", "open"),
    "glass-opened": ("|| TEST_FLAG(surface->flags, BOTS_NAV_BREAKABLE_BIT)", "", "open"),
    "pieces-not-flooded": ("component[other] = component[surface_index];",
                           "component[other] = (short)(pieces + 1000);", "components"),
    "unusable-taken": ("if (portals[index].usable &&\n", "if (1 &&\n", "route"),
    "one-hop-only": ("first[next] = first[current];", "first[next] = next;", "route"),
    "coarse-unlinked": ("if (other_cell != NONE && other_cell != cell)", "if (0)", "coarse"),
    "coarse-straight-line": ("while (coarse->parent[cell] != NONE && coarse->cost[cell] > ahead)",
                             "while (coarse->parent[cell] != NONE && bots_nav_distance(&coarse->center[cell], "
                             "&coarse->center[start]) > ahead)", "coarse"),
}


def generated(fault=None):
    nav = read(NAV)
    if fault:
        nav = mutated(nav, fault[0], fault[1])
    nav = re.sub(r'^#include .*$', "", nav, flags=re.M)
    header = re.sub(r'^#include .*$', "", read("port/linux/game/bots_nav.h"), flags=re.M)
    return (("under_test.inc", header + nav),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_nav", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    before, after, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_nav", generated((before, after))), case)
    assert status == CHECK_FAILED, f"bots_nav.c with a fault ({control}) passed '{case}': the test cannot see it"
