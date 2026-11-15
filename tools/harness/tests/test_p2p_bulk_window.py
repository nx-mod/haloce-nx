"""A map-sharing stream's send window over the internet play tunnel (p2p.c's stream_tune): the upload's cap over twice
the round trip, within its bounds and its part of a shared budget, halved when the round trip grows well past the least
seen or resends climb (once a round trip), and grown back slowly. KCP's congestion control is off, so this and the
uploader's pacing are all that keep a large transfer from flooding the path."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["cap-sized", "seeding-cap", "bounds", "budget", "round-trip-grows", "resends", "recovers", "floor",
         "once-a-round-trip"]
FUNCTION = "stream_tune"

# a fault in p2p.c's stream_tune and the case that must catch it
NEGATIVE_CONTROLS = {
    "no-maximum": (("if (window > BULK_WINDOW_MAXIMUM)", "if (0)"), "bounds"),
    "no-budget": (("window = ideal < budget ? ideal : budget;", "window = ideal + 0 * budget;"), "budget"),
    "no-delay-backoff": (("rtt > stream->base_rtt * 2 && rtt > stream->base_rtt + 50", "0"), "round-trip-grows"),
    "no-resend-backoff": (("else if (resent > (IUINT32)(stream->window / 8 + 2))", "else if (0)"), "resends"),
    "no-recovery": (("stream->share = stream->share + 50 < 1000 ? stream->share + 50 : 1000;", "(void)0;"), "recovers"),
    "always-tuned": (("if (!elapsed(stream->tuned_time, (unsigned long)(rtt > 50 ? rtt : 50)))", "if (0)"),
                     "once-a-round-trip"),
    "host-cap-always": (('p2p.hosting ? config_integer("network.map_share_upload") : '
                         'config_integer("network.map_seed_upload")', 'config_integer("network.map_share_upload")'),
                        "seeding-cap"),
}


def generated(fault=None):
    code = function(read("port/linux/src/p2p.c"), FUNCTION)
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("p2p_bulk_window", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("p2p_bulk_window", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
