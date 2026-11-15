"""The Proving Grounds bots' rules (port/linux/game/pgbots_rules.c, compiled as they are): the skill tables, the
personalities (dealing, jitter), perception, aim and trigger, target choice, weapon classes from tag facts, goals and
their modifiers, fighting, posture and the vehicles' controller. Negative controls: a fault in the rules each case must
catch. A wiring check: every rule pgbots_rules.h declares is asked by the bots' code (pgbots*.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, build, mutated, read, run  # noqa: E402

CASES = ["tables", "personalities", "jitter", "perception", "aim", "trigger", "threat", "weapon-classes", "goals",
         "fighting", "posture", "drive"]

RULES = "port/linux/game/pgbots_rules.c"

# a fault in the rules, and the case that must catch it
NEGATIVE_CONTROLS = {
    "legendary-loose": ("0.154f, 0.08f, 0.045f, 0.70f, 0.30f, 0.006f, 0.8f, 1000.0f, 10.0f, 0.97f, 1.0f, 0.0f, 0.0f",
                        "0.154f, 0.08f, 0.045f, 0.70f, 0.30f, 0.006f, 0.8f, 1000.0f, 10.0f, 0.97f, 1.0f, 0.1f, 0.0f",
                        "tables"),
    "veteran-slower": ("{ 0.17f, 0.083f", "{ 0.30f, 0.083f", "tables"),
    "rusher-timid": ('{ "rusher", { 0.9f, 0.2f', '{ "rusher", { 0.9f, 0.8f', "personalities"),
    "deal-not-mirrored": ("(offset + (unsigned long)team_rank)", "(offset + 2 * (unsigned long)team_rank)",
                          "personalities"),
    "jitter-wide": ("value * (0.9f + 0.2f * random)", "value * (0.8f + 0.4f * random)", "jitter"),
    "periphery-quick": ("seconds *= 1.6f;", "seconds *= 1.0f;", "perception"),
    "flick-beats-turn": ("return fitts > slowest ? fitts : slowest;", "return fitts;", "aim"),
    "pursuit-uncapped": ("return pgbots_clamp(step, -cap, cap);", "return step;", "aim"),
    "fires-through-walls": ("return seen ? input->line_clear : 1;", "return 1;", "trigger"),
    "rockets-point-blank": ("input->distance < input->blast_radius)\n", "input->distance < 0.0f)\n", "trigger"),
    "grudge-ignored": ("threat += 15.0f * grudge;", "threat += 0.0f * grudge;", "threat"),
    "novices-switch-freely": ("(1.25f + 0.75f * (1.0f - tactics))", "(1.25f + 0.0f * (1.0f - tactics))", "threat"),
    "needler-explosive": ("facts->blast_radius >= 1.0f ||", "facts->blast_radius >= 0.1f ||", "weapon-classes"),
    "taste-ignored": ("\t\tscore += 1.0f;\n\tif (disliked", "\t\tscore += 0.0f;\n\tif (disliked", "weapon-classes"),
    "close-enemy-ignored": ("scores[_pgbots_goal_fight] = 10.0f;", "scores[_pgbots_goal_fight] += 0.0f;", "goals"),
    "no-commitment": ("\t\t\tscore += 0.2f;\n", "\t\t\tscore += 0.0f;\n", "goals"),
    "camouflage-ignored": ("score = 1.05f - input->powerup_distance / 80.0f;", "score += 0.0f;", "goals"),
    "posture-backwards": ("0.5f - 1.5f * lead", "0.5f + 1.5f * lead", "posture"),
    "reverse-no-hysteresis": ("magnitude < 60.0f * DEGREES || input->reverse_seconds",
                              "magnitude < 100.0f * DEGREES || input->reverse_seconds", "drive"),
    "tank-stalls": ("if (magnitude > 10.0f * DEGREES && output->throttle < 0.3f)",
                    "if (0 && output->throttle < 0.3f)", "drive"),
}


def generated(fault=None):
    rules = read(RULES)
    if fault:
        rules = mutated(rules, fault[0], fault[1])
    return (("bots_combat.h", read("port/linux/game/bots_combat.h")),
            ("pgbots_rules.h", read("port/linux/game/pgbots_rules.h")), ("under_test.inc", rules))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("pgbots_rules", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    before, after, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("pgbots_rules", generated((before, after))), case)
    assert status == CHECK_FAILED, f"the rules with a fault ({control}) passed '{case}': the test cannot see it"


RULE = re.compile(r"^[\w ]+?\*?\b(pgbots_rules_\w+)\(", re.M)


def unused(header, users, rules=None):
    """the rules pgbots_rules.h declares that nothing asks: neither a pgbots*.c file nor another rule (a call in
    pgbots_rules.c beside the rule's own definition)"""
    rules = read(RULES) if rules is None else rules
    return [name for name in sorted(set(RULE.findall(header)))
            if not any(re.search(r"\b" + name + r"\(", text) for text in users)
            and len(re.findall(r"\b" + name + r"\(", rules)) < 2]


def users():
    return [path.read_text(encoding="latin-1") for path in sorted((ROOT / "port/linux/game").glob("pgbots*.c"))
            if path.name != "pgbots_rules.c"]


def test_every_rule_is_asked():
    assert unused(read("port/linux/game/pgbots_rules.h"), users()) == []


def test_wiring_check_sees_a_rule_left_unasked():
    header = read("port/linux/game/pgbots_rules.h") + "\nfloat pgbots_rules_nobody_asks(float value);\n"
    assert "pgbots_rules_nobody_asks" in unused(header, users())
