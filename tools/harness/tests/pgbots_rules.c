/*
PGBOTS_RULES.C (test)

The Proving Grounds bots' rules (port/linux/game/pgbots_rules.c, compiled as
they are; test_pgbots_rules.py brings them and their headers): the skill
tables, the personalities and their dealing and jitter, perception, the
hands (aim and trigger), target choice, weapon classes from tag facts (the
stock weapons and made-up Custom Edition ones), goals and their modifiers,
fighting, the director's posture and the vehicles' controller.
*/

#include "harness.h"

#include <math.h>

#include "under_test.inc"

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-4)
#define DEG (PGBOTS_PI / 180.0f)

static struct pgbots_weapon_facts facts(int flag, float charge, short pellets, float range, float blast, float danger,
	float speed, short zooms, float zoom, float error, float rate)
{
	struct pgbots_weapon_facts f;

	f.is_flag = flag;
	f.charging_time = charge;
	f.projectiles_per_shot = pellets;
	f.maximum_range = range;
	f.blast_radius = blast;
	f.danger_radius = danger;
	f.speed = speed;
	f.zoom_levels = zooms;
	f.zoom_maximum = zoom;
	f.error = error;
	f.rate_of_fire = rate;
	return f;
}

static struct pgbots_fire_input fire_base(void)
{
	struct pgbots_fire_input f;

	memset(&f, 0, sizeof(f));
	f.seen_age = 0.0f;
	f.line_clear = 1;
	f.distance = 10.0f;
	f.weapon_class = _pgbots_class_precision;
	f.band_high = 30.0f;
	return f;
}

static struct pgbots_profile profile_of(short skill, short personality)
{
	struct pgbots_profile profile;
	unsigned long random = 12345;

	pgbots_rules_profile(&profile, skill, personality, &random);
	/* (the tables' own values, no jitter: the scores' numbers checked) */
	profile.hands = *pgbots_rules_hands(skill);
	profile.craft = *pgbots_rules_craft(skill);
	memcpy(profile.traits, pgbots_rules_personality(personality)->traits, sizeof(profile.traits));
	return profile;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	short skill;

	/* the six levels' numbers, as the design's S5 sets them */
	CASE("tables")
	{
		struct pgbots_hands const *normal = pgbots_rules_hands(_bots_skill_normal);
		struct pgbots_hands const *legendary = pgbots_rules_hands(_bots_skill_legendary);
		struct pgbots_craft const *heroic = pgbots_rules_craft(_bots_skill_heroic);

		CHECK(NEAR(normal->reaction, 0.22f) && NEAR(normal->turn_degrees, 435.0f) && NEAR(normal->memory, 6.0f),
			"normal's hands are not the design's");
		CHECK(NEAR(legendary->aim_error, 0.006f) && NEAR(legendary->turn_degrees, 1000.0f) &&
			legendary->loose_follow == 0.0f && legendary->loose_early == 0.0f && NEAR(legendary->head_share, 0.97f),
			"legendary's hands are not the design's (no loose trigger at the top)");
		CHECK(NEAR(heroic->tactics, 0.75f) && NEAR(heroic->teamwork, 0.8f) && NEAR(heroic->juke, 0.6f),
			"heroic's craft is not the design's");
		CHECK(pgbots_rules_hands(_bots_skill_easy)->ram_error_degrees < 0.0f &&
			pgbots_rules_hands(_bots_skill_normal)->ram_error_degrees == 20.0f, "easy rams, or normal does not");
		CHECK(pgbots_rules_craft(_bots_skill_recruit)->tactics == 0.0f &&
			pgbots_rules_craft(_bots_skill_easy)->teamwork == 0.0f, "the novices have tactics or teamwork");
		/* each level at least as good as the one below */
		for (skill = 1; skill < NUMBER_OF_BOTS_SKILLS; skill++)
		{
			struct pgbots_hands const *low = pgbots_rules_hands(skill - 1), *high = pgbots_rules_hands(skill);

			CHECK(high->reaction <= low->reaction && high->turn_degrees >= low->turn_degrees &&
				high->aim_error <= low->aim_error && high->memory >= low->memory &&
				high->head_share >= low->head_share && high->hear_delay <= low->hear_delay,
				"level %d's hands are worse than the level below's", (int)skill);
			CHECK(pgbots_rules_craft(skill)->tactics >= pgbots_rules_craft(skill - 1)->tactics,
				"level %d has less tactics than the level below", (int)skill);
		}
		CHECK(pgbots_rules_hands(99) == pgbots_rules_hands(_bots_skill_normal), "an unknown level is not normal");
		return 0;
	}

	/* the personalities, their dealing (mirrored) and their names */
	CASE("personalities")
	{
		struct pgbots_personality_definition const *rusher = pgbots_rules_personality(_pgbots_personality_rusher);
		struct pgbots_personality_definition const *marksman = pgbots_rules_personality(_pgbots_personality_marksman);
		long rank;

		CHECK(NEAR(rusher->traits[_pgbots_trait_aggression], 0.9f) && NEAR(rusher->traits[_pgbots_trait_caution], 0.2f) &&
			NEAR(rusher->traits[_pgbots_trait_jumpy], 0.7f), "the rusher is not the design's");
		CHECK((marksman->liked & (1UL << _pgbots_class_sniper)) && (marksman->liked & (1UL << _pgbots_class_precision)) &&
			(marksman->disliked & (1UL << _pgbots_class_close)), "the marksman does not prefer precision and sniper guns");
		CHECK(NEAR(pgbots_rules_personality(_pgbots_personality_grudger)->traits[_pgbots_trait_grudge], 0.95f),
			"the grudger's grudge is not high");
		for (rank = 0; rank < 20; rank++)
			CHECK(pgbots_rules_deal(rank, 3) == (short)((3 + rank) % NUMBER_OF_PGBOTS_PERSONALITIES),
				"dealing is not round-robin from the offset");
		CHECK(pgbots_rules_deal(0, 5) == pgbots_rules_deal(0, 5 + NUMBER_OF_PGBOTS_PERSONALITIES),
			"the offset wraps wrongly");
		CHECK(pgbots_rules_personality_named("Marksman") == _pgbots_personality_marksman &&
			pgbots_rules_personality_named("objective") == _pgbots_personality_objective &&
			pgbots_rules_personality_named("nobody") == -1 && pgbots_rules_personality_named("") == -1,
			"a personality is not found by its name");
		return 0;
	}

	/* the jitter: every value within 10%, the same seed the same profile */
	CASE("jitter")
	{
		struct pgbots_profile a, b, c;
		unsigned long seed_a = 777, seed_b = 777, seed_c = 778;
		long index;

		pgbots_rules_profile(&a, _bots_skill_heroic, _pgbots_personality_flanker, &seed_a);
		pgbots_rules_profile(&b, _bots_skill_heroic, _pgbots_personality_flanker, &seed_b);
		pgbots_rules_profile(&c, _bots_skill_heroic, _pgbots_personality_flanker, &seed_c);
		CHECK(!memcmp(&a, &b, sizeof(a)), "the same seed made two profiles");
		CHECK(memcmp(&a, &c, sizeof(a)), "two seeds made the same profile");
		for (index = 0; index < NUMBER_OF_PGBOTS_TRAITS; index++)
		{
			float base = pgbots_rules_personality(_pgbots_personality_flanker)->traits[index];

			CHECK(a.traits[index] >= base * 0.9f - 1e-5f && a.traits[index] <= base * 1.1f + 1e-5f &&
				a.traits[index] <= 1.0f, "trait %ld is jittered past 10%% or 1", index);
		}
		CHECK(a.hands.reaction >= 0.187f * 0.9f && a.hands.reaction <= 0.187f * 1.1f, "the reaction jitters past 10%%");
		CHECK(a.hands.ram_error_degrees == 12.0f, "the ram error is jittered");
		CHECK(a.personality == _pgbots_personality_flanker && a.skill == _bots_skill_heroic, "the profile's names");
		for (index = 0; index < 1000; index++)
		{
			float r = pgbots_rules_random01(&seed_a);

			CHECK(r >= 0.0f && r < 1.0f, "a random number out of 0 to 1");
		}
		return 0;
	}

	CASE("perception")
	{
		CHECK(NEAR(pgbots_rules_recognition_seconds(0.2f, 0, 0, 0.0f), 0.2f), "the recognition's median is not the reaction");
		CHECK(NEAR(pgbots_rules_recognition_seconds(0.2f, 1, 0, 0.0f), 0.32f), "the periphery is not 1.6x slower");
		CHECK(NEAR(pgbots_rules_recognition_seconds(0.2f, 0, 1, 0.0f), 0.07f), "a lost enemy is not 0.35x quicker");
		CHECK(pgbots_rules_recognition_seconds(0.2f, 0, 0, 1.0f) > 0.2f, "the spread goes the wrong way");
		CHECK(pgbots_rules_in_view(50.0f * DEG, 70.0f) && !pgbots_rules_in_view(60.0f * DEG, 10.0f) &&
			!pgbots_rules_in_view(0.0f, 81.0f) && pgbots_rules_in_view(PGBOTS_PI, 2.0f), "the view's cone and reach");
		CHECK(pgbots_rules_foveal(30.0f * DEG) && !pgbots_rules_foveal(40.0f * DEG), "the foveal cone is not 35 degrees");
		CHECK(pgbots_rules_radar_chance(0.0f, 3) == 0.0f && pgbots_rules_radar_chance(1.0f, 3) > 0.15f &&
			pgbots_rules_radar_chance(1.0f, 3) < 0.45f, "the radar's chance a scan");
		return 0;
	}

	CASE("aim")
	{
		float slow = pgbots_rules_flick_seconds(1.0f, 0.10f, 1.0f, 270.0f * DEG);
		float quick = pgbots_rules_flick_seconds(1.0f, 0.08f, 0.7f, 1000.0f * DEG);
		float r;

		CHECK(slow > quick, "legendary flicks no quicker than easy");
		CHECK(pgbots_rules_flick_seconds(3.0f, 0.14f, 1.3f, 200.0f * DEG) >= 3.0f / (200.0f * DEG) - 1e-4f,
			"a flick quicker than the turn speed");
		CHECK(pgbots_rules_flick_seconds(2.0f, 0.1f, 1.0f, 100.0f) > pgbots_rules_flick_seconds(0.5f, 0.1f, 1.0f, 100.0f),
			"a long flick no slower than a short one");
		CHECK(pgbots_rules_is_flick(0.4f) && !pgbots_rules_is_flick(0.3f), "a flick is not a turn over 0.35 rad");
		CHECK(pgbots_rules_ease(0.0f) == 0.0f && pgbots_rules_ease(1.0f) == 1.0f && NEAR(pgbots_rules_ease(0.5f), 0.5f),
			"the easing");
		CHECK(pgbots_rules_overshoot(0.3f, 0.5f, 0.5f) == 0.0f, "an overshoot past its chance");
		r = pgbots_rules_overshoot(0.3f, 0.1f, 1.0f);
		CHECK(r >= 0.08f && r <= 0.18f + 1e-6f, "an overshoot out of 8-18%%");
		CHECK(NEAR(pgbots_rules_pursuit_step(5.0f, 1.0f / 30.0f, 0.001f), 0.3f), "the pursuit is not capped at 9 rad/s");
		CHECK(pgbots_rules_pursuit_step(0.01f, 1.0f / 30.0f, 0.09f) < 0.01f &&
			pgbots_rules_pursuit_step(0.01f, 1.0f / 30.0f, 0.045f) > pgbots_rules_pursuit_step(0.01f, 1.0f / 30.0f, 0.09f),
			"the pursuit's lag");
		CHECK(NEAR(pgbots_rules_aim_sigma(0.01f, 1.0f), 0.003f) && NEAR(pgbots_rules_aim_sigma(0.01f, 0.0f), 0.01f),
			"the focus does not take 70%% of the error");
		CHECK(NEAR(pgbots_rules_focus(0.75f), 0.5f) && pgbots_rules_focus(3.0f) == 1.0f, "the focus builds over 1.5 s");
		CHECK(NEAR(pgbots_rules_sample_seconds(0.1f, 0.0f), 0.08f) && NEAR(pgbots_rules_sample_seconds(0.1f, 1.0f), 0.12f),
			"the sampling is not track x 0.8-1.2");
		CHECK(NEAR(pgbots_rules_lead_scale(0.8f, 0.0f), 0.48f) && NEAR(pgbots_rules_lead_scale(0.8f, 1.0f), 0.96f),
			"the lead is not x 0.6-1.2");
		CHECK(!pgbots_rules_aim_head(1, 0, 0, 1.0f, 0.0f) && pgbots_rules_aim_head(1, 1, 0, 0.5f, 0.4f) &&
			pgbots_rules_aim_head(1, 0, 1, 0.2f, 0.1f) && !pgbots_rules_aim_head(0, 1, 0, 1.0f, 0.0f),
			"the head: precision, shield down (the novices regardless), the share");
		return 0;
	}

	CASE("trigger")
	{
		struct pgbots_fire_input f = fire_base();
		float burst, gap;

		CHECK(pgbots_rules_may_fire(&f), "no shot on target");
		f.line_clear = 0;
		CHECK(!pgbots_rules_may_fire(&f), "a shot through a wall");
		f = fire_base();
		f.across = 0.2f;
		CHECK(!pgbots_rules_may_fire(&f), "a shot off target");
		f.flicking = 1;
		f.flick_progress = 0.8f;
		f.loose_early = 0.3f;
		CHECK(pgbots_rules_may_fire(&f), "no loose shot at a flick's end");
		f.loose_early = 0.0f;
		CHECK(!pgbots_rules_may_fire(&f), "a loose shot without the loose trigger");
		f = fire_base();
		f.seen_age = 0.4f;
		CHECK(!pgbots_rules_may_fire(&f), "a shot at a target lost 0.4 s ago");
		f.loose_follow = 0.3f;
		f.line_clear = 0;
		CHECK(pgbots_rules_may_fire(&f), "no follow-through");
		f = fire_base();
		f.weapon_class = _pgbots_class_explosive;
		f.blast_radius = 3.0f;
		f.distance = 2.0f;
		CHECK(!pgbots_rules_may_fire(&f), "a rocket inside its blast");
		f = fire_base();
		f.teammate_in_line = 1;
		CHECK(!pgbots_rules_may_fire(&f), "a shot through a teammate");
		f = fire_base();
		f.weapon_class = _pgbots_class_close;
		f.band_high = 2.0f;
		f.distance = 3.0f;
		CHECK(!pgbots_rules_may_fire(&f), "a shotgun past its band");
		f = fire_base();
		f.distance = 70.0f;
		CHECK(!pgbots_rules_may_fire(&f), "a shot past 2.2x the band");
		f.deterrent = 1;
		CHECK(pgbots_rules_may_fire(&f), "no deterrent burst");
		f = fire_base();
		f.distance = 1.0f;
		f.across = 0.2f;
		CHECK(pgbots_rules_may_fire(&f), "no wider window point blank");
		pgbots_rules_burst(2.0f, 0.5f, 0.5f, &burst, &gap);
		CHECK(gap == 0.0f, "no spray within 4 u");
		pgbots_rules_burst(8.0f, 0.0f, 1.0f, &burst, &gap);
		CHECK(NEAR(burst, 0.22f) && NEAR(gap, 0.3f), "the bursts beyond 4 u");
		CHECK(pgbots_rules_press_interval(4.0f, 2.0f, 0.0f) < pgbots_rules_press_interval(4.0f, 7.0f, 0.0f) &&
			pgbots_rules_press_interval(4.0f, 7.0f, 0.0f) < pgbots_rules_press_interval(4.0f, 15.0f, 0.0f),
			"the presses are not spammed close and paced far");
		return 0;
	}

	CASE("threat")
	{
		struct pgbots_threat_input t;
		float base, factor_novice, factor_expert;

		memset(&t, 0, sizeof(t));
		t.in_band = 1;
		t.distance = 10.0f;
		base = pgbots_rules_threat(&t, 0.5f, 0.5f, 0.5f);
		t.last_killer = 1;
		CHECK(NEAR(pgbots_rules_threat(&t, 0.5f, 0.5f, 1.0f) - base, 15.0f), "the grudge bonus is not 15 x grudge");
		t.last_killer = 0;
		t.carrier = 1;
		CHECK(NEAR(pgbots_rules_threat(&t, 0.5f, 0.5f, 0.5f) - base, 40.0f), "the carrier bonus");
		t.carrier = 0;
		t.aiming_at_us = 1;
		CHECK(NEAR(pgbots_rules_threat(&t, 0.5f, 0.5f, 0.5f), base * 1.3f), "aiming at us is not x1.3");
		t.aiming_at_us = 0;
		t.distance = 0.5f;
		CHECK(NEAR(pgbots_rules_threat(&t, 1.0f, 0.5f, 0.5f), 70.0f * 1.9f), "the close factor");
		t.distance = 40.0f;
		CHECK(NEAR(pgbots_rules_threat(&t, 1.0f, 0.5f, 0.5f), 70.0f * 0.4f), "the far factor");
		factor_novice = pgbots_rules_switch_factor(0.0f, 0.0f, 0.5f);
		factor_expert = pgbots_rules_switch_factor(1.0f, 0.0f, 0.5f);
		CHECK(NEAR(factor_novice, 2.0f) && NEAR(factor_expert, 1.25f), "the switch margin by tactics");
		CHECK(pgbots_rules_switch_factor(1.0f, 0.5f, 0.5f) > factor_expert, "damage done does not hold the target");
		CHECK(pgbots_rules_switch_factor(1.0f, 0.0f, 1.0f) > pgbots_rules_switch_factor(1.0f, 0.0f, 0.0f),
			"stickiness does not hold the target");
		CHECK(!pgbots_rules_switch(50.0f, 200.0f, 1.25f, 0.5f) && pgbots_rules_switch(50.0f, 200.0f, 1.25f, 1.0f) &&
			!pgbots_rules_switch(50.0f, 60.0f, 1.25f, 2.0f), "the switch: past the factor, held 1 s");
		return 0;
	}

	/* the classes from tag facts: the stock weapons, and Custom Edition
	maps' own */
	CASE("weapon-classes")
	{
		struct pgbots_weapon_facts f;
		float low, high;

		f = facts(0, 0.0f, 1, 0.0f, 0.0f, 0.0f, 300.0f, 1, 2.0f, 0.0035f, 3.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_precision, "the pistol is not precision");
		f = facts(0, 0.0f, 1, 0.0f, 0.0f, 0.0f, 990.0f, 2, 8.0f, 0.0087f, 1.5f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_sniper, "the sniper rifle is not sniper");
		f = facts(0, 0.0f, 1, 0.0f, 0.0f, 0.0f, 300.0f, 0, 0.0f, 0.035f, 15.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_automatic, "the assault rifle is not automatic");
		f = facts(0, 0.0f, 15, 0.0f, 0.0f, 0.0f, 60.0f, 0, 0.0f, 0.1f, 1.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_close, "the shotgun is not close");
		f = facts(0, 0.0f, 1, 0.0f, 3.0f, 3.0f, 11.0f, 1, 2.0f, 0.0f, 1.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_explosive, "the rocket launcher is not explosive");
		f = facts(0, 0.0f, 1, 0.0f, 0.0f, 2.0f, 20.0f, 0, 0.0f, 0.01f, 3.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_explosive, "the fuel rod (slow, a danger radius) is not explosive");
		f = facts(0, 1.0f, 1, 0.0f, 0.0f, 0.0f, 30.0f, 0, 0.0f, 0.01f, 6.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_charge, "the plasma pistol is not charge");
		f = facts(0, 0.0f, 1, 3.5f, 0.0f, 0.0f, 20.0f, 0, 0.0f, 0.1f, 30.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_close, "the flamethrower (a short range) is not close");
		f = facts(0, 0.0f, 1, 0.0f, 0.5f, 0.0f, 40.0f, 0, 0.0f, 0.02f, 8.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_automatic, "the needler is not automatic");
		f = facts(1, 0.0f, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_objective, "the flag is not the objective");
		/* (made up: a Custom Edition battle rifle, three zooms to 10x; a
		zoomed burst rifle firing 8 a second) */
		f = facts(0, 0.0f, 1, 0.0f, 0.0f, 0.0f, 600.0f, 3, 10.0f, 0.005f, 2.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_sniper, "a 10x rifle is not sniper");
		f = facts(0, 0.0f, 1, 0.0f, 0.0f, 0.0f, 400.0f, 1, 2.0f, 0.01f, 8.0f);
		CHECK(pgbots_rules_weapon_class(&f) == _pgbots_class_automatic, "a quick zoomed rifle is precision");
		pgbots_rules_class_band(_pgbots_class_sniper, 40.0f, &low, &high);
		CHECK(low == 8.0f && high == 40.0f, "the band is not capped at the projectile's range");
		pgbots_rules_class_band(_pgbots_class_precision, 0.0f, &low, &high);
		CHECK(low == 1.5f && high == 30.0f, "the precision band");
		CHECK(pgbots_rules_band_fit(1.5f, 30.0f, 10.0f) == 1.0f && pgbots_rules_band_fit(1.5f, 30.0f, 45.0f) == 0.0f,
			"the band's fit");
		CHECK(pgbots_rules_band_fit(8.0f, 80.0f, -1.0f) < pgbots_rules_band_fit(0.0f, 6.0f, -1.0f) + 1.0f &&
			pgbots_rules_band_fit(0.0f, 2.0f, -1.0f) < pgbots_rules_band_fit(1.5f, 30.0f, -1.0f),
			"the typical ranges' weights");
		CHECK(pgbots_rules_weapon_score(_pgbots_class_sniper, 8.0f, 80.0f, 30.0f, 1UL << _pgbots_class_sniper, 0) -
			pgbots_rules_weapon_score(_pgbots_class_sniper, 8.0f, 80.0f, 30.0f, 0, 0) == 1.0f, "a liked class is not +1");
		CHECK(pgbots_rules_weapon_score(_pgbots_class_close, 0.0f, 2.0f, 1.0f, 0, 1UL << _pgbots_class_close) -
			pgbots_rules_weapon_score(_pgbots_class_close, 0.0f, 2.0f, 1.0f, 0, 0) == -0.5f, "a disliked class is not -0.5");
		CHECK(pgbots_rules_weapon_score(_pgbots_class_objective, 0.0f, 0.0f, 1.0f, ~0UL, 0) < 0.0f, "the flag is a weapon");
		CHECK(!pgbots_rules_weapon_switch(3.0f, 4.0f) && pgbots_rules_weapon_switch(3.0f, 4.1f), "the switch's gain");
		CHECK(pgbots_rules_zoom(_pgbots_class_sniper, 2, 25.0f) == 2 && pgbots_rules_zoom(_pgbots_class_sniper, 2, 10.0f) == 1 &&
			pgbots_rules_zoom(_pgbots_class_sniper, 2, 4.0f) == 0 && pgbots_rules_zoom(_pgbots_class_precision, 1, 9.0f) == 1 &&
			pgbots_rules_zoom(_pgbots_class_precision, 1, 7.0f) == 0, "the zoom by range");
		return 0;
	}

	CASE("goals")
	{
		struct pgbots_goal_input in;
		struct pgbots_profile rusher = profile_of(_bots_skill_heroic, _pgbots_personality_rusher);
		struct pgbots_profile anchor = profile_of(_bots_skill_heroic, _pgbots_personality_anchor);
		float scores[NUMBER_OF_PGBOTS_GOALS], zero[NUMBER_OF_PGBOTS_GOALS];

		memset(zero, 0, sizeof(zero));
		memset(&in, 0, sizeof(in));
		in.posture = 0.5f;
		in.health_distance = -1.0f;
		in.powerup_distance = -1.0f;
		in.target_visible = 1;
		in.target_distance = 15.0f;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(NEAR(scores[_pgbots_goal_fight], 1.5f + 0.4f * 0.9f), "the fight's score");
		in.runner = 1;
		in.objective = 1;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(NEAR(scores[_pgbots_goal_fight], 1.5f + 0.4f * 0.9f - 1.2f * 0.4f), "a runner skips far fights less");
		in.target_distance = 5.0f;
		in.grab_near = 1;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(pgbots_rules_goal_pick(scores, _pgbots_goal_objective, zero) == _pgbots_goal_fight,
			"an enemy within 8 u is not fought (even a grab near)");
		in.grab_near = 0;
		/* (a carrier cannot fire: the flag first, let go to fight only
		hiding - bots_team.h - and banned or not) */
		in.carrying = 1;
		in.banned = 1UL << _pgbots_goal_objective;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(pgbots_rules_goal_pick(scores, _pgbots_goal_fight, zero) == _pgbots_goal_objective,
			"a carrier turns to fight an enemy within 8 u, or its flag goal is banned");
		in.carrying = 0;
		in.regrab_urgent = 1;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(pgbots_rules_goal_pick(scores, _pgbots_goal_fight, zero) == _pgbots_goal_objective,
			"a carrier does not go back for its flag before its return");
		memset(&in, 0, sizeof(in));
		in.posture = 0.5f;
		in.health_distance = -1.0f;
		in.powerup_distance = -1.0f;
		in.hold = 1;
		in.hunt_belief = 1;
		in.hunt_distance = 30.0f;
		pgbots_rules_goal_scores(&in, &anchor, _pgbots_personality_anchor, scores);
		CHECK(NEAR(scores[_pgbots_goal_hold], 0.2f + 0.6f * 0.9f), "the hold's score");
		CHECK(pgbots_rules_goal_pick(scores, _pgbots_goal_none, zero) == _pgbots_goal_hold, "the anchor does not hold");
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(pgbots_rules_goal_pick(scores, _pgbots_goal_none, zero) == _pgbots_goal_hunt, "the rusher does not hunt");
		in.carrying = 1;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(scores[_pgbots_goal_objective] == 3.5f, "carrying is not 3.5");
		in.carrying = 0;
		in.objective = 1;
		in.banned = 1UL << _pgbots_goal_objective;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(scores[_pgbots_goal_objective] < 0.0f, "a banned goal is scored");
		/* the commitment bonus keeps the current goal; the noise is at most 0.15 */
		memset(scores, 0, sizeof(scores));
		{
			short goal;

			for (goal = 0; goal < NUMBER_OF_PGBOTS_GOALS; goal++)
				scores[goal] = -1.0f;
		}
		scores[_pgbots_goal_roam] = 0.3f;
		scores[_pgbots_goal_hunt] = 0.45f;
		CHECK(pgbots_rules_goal_pick(scores, _pgbots_goal_roam, zero) == _pgbots_goal_roam, "no commitment bonus");
		scores[_pgbots_goal_hold] = 0.45f;
		scores[_pgbots_goal_hunt] = -1.0f;
		CHECK(pgbots_rules_goal_pick(scores, _pgbots_goal_hold, zero) == _pgbots_goal_hold, "the hold lost to roam");
		scores[_pgbots_goal_hold] = 0.35f;
		CHECK(pgbots_rules_goal_pick(scores, _pgbots_goal_hold, zero) == _pgbots_goal_hold &&
			pgbots_rules_goal_pick(scores, _pgbots_goal_roam, zero) == _pgbots_goal_roam,
			"a hold has a commitment bonus");
		/* the modifiers */
		memset(&in, 0, sizeof(in));
		in.health_distance = -1.0f;
		in.powerup_distance = -1.0f;
		in.target_visible = 1;
		in.target_distance = 20.0f;
		in.posture = 0.2f;
		in.outnumbered = 1;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(scores[_pgbots_goal_fight] == 0.3f && scores[_pgbots_goal_retreat] > 2.5f, "no avoid mode");
		in.posture = 0.5f;
		in.outnumbered = 0;
		in.stalemate = 1;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(NEAR(scores[_pgbots_goal_fight], (1.5f + 0.4f * 0.9f) * 0.5f), "no stalemate breaker");
		in.stalemate = 0;
		in.race = 1;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(NEAR(scores[_pgbots_goal_fight], (1.5f + 0.4f * 0.9f) * 0.3f), "a race does not lower the fight");
		memset(&in, 0, sizeof(in));
		in.health_distance = -1.0f;
		in.powerup_distance = -1.0f;
		in.hunt_belief = 1;
		in.hunt_distance = 10.0f;
		in.posture = 0.5f;
		{
			struct pgbots_profile marksman = profile_of(_bots_skill_heroic, _pgbots_personality_marksman);

			pgbots_rules_goal_scores(&in, &marksman, _pgbots_personality_marksman, scores);
			CHECK(NEAR(scores[_pgbots_goal_hunt], (0.3f + 0.6f * 0.3f) * (0.4f + 0.6f) * 0.4f),
				"the marksman hunts close as eagerly");
		}
		in.hurt = 0.5f;
		in.health_distance = 16.0f;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(NEAR(scores[_pgbots_goal_health], 0.4f + 0.8f - 0.2f), "the health pack's score");
		in.hurt = 0.2f;
		pgbots_rules_goal_scores(&in, &rusher, _pgbots_personality_rusher, scores);
		CHECK(scores[_pgbots_goal_health] < 0.0f, "a health pack for a scratch");
		/* (camouflage near: before a hunt, at the lowest level too) */
		{
			struct pgbots_profile recruit = profile_of(_bots_skill_easy, _pgbots_personality_rusher);

			memset(&in, 0, sizeof(in));
			in.posture = 0.5f;
			in.health_distance = -1.0f;
			in.hunt_belief = 1;
			in.hunt_distance = 10.0f;
			in.powerup_distance = 8.0f;
			in.powerup_value = 0.7f;
			in.powerup_camouflage = 1;
			pgbots_rules_goal_scores(&in, &recruit, _pgbots_personality_rusher, scores);
			CHECK(NEAR(scores[_pgbots_goal_powerup], 1.05f - 8.0f / 80.0f), "camouflage near is not worth a detour");
			CHECK(pgbots_rules_goal_pick(scores, _pgbots_goal_roam, zero) == _pgbots_goal_powerup,
				"a fresh hunt before camouflage 8 units away");
		}
		return 0;
	}

	CASE("fighting")
	{
		float near, far;

		CHECK(NEAR(pgbots_rules_cracked(0.5f), 0.25f) && NEAR(pgbots_rules_cracked(0.0f), 0.45f) &&
			NEAR(pgbots_rules_cracked(1.0f), 0.05f), "the cracked shield by posture");
		CHECK(NEAR(pgbots_rules_retreat_range(0.0f), 8.0f) && NEAR(pgbots_rules_retreat_range(1.0f), 14.0f), "the retreat's reach");
		CHECK(NEAR(pgbots_rules_grenade_opener(0.5f, 0.0f), 0.4f) && pgbots_rules_grenade_opener(0.95f, 1.0f) == 1.0f,
			"the opener's chance");
		pgbots_rules_keep_range(10.0f, 0.5f, 0.5f, &near, &far);
		CHECK(NEAR(near, 6.0f) && NEAR(far, 10.0f), "the range kept");
		pgbots_rules_keep_range(10.0f, 1.0f, 0.0f, &near, &far);
		CHECK(NEAR(far, 7.5f), "aggression does not shorten the range");
		CHECK(pgbots_rules_flip_jump(1.0f, 0.1f) < pgbots_rules_flip_jump(1.0f, 0.7f) &&
			pgbots_rules_travel_hops(0.0f, 1.0f) == 0.0f && pgbots_rules_shot_jump(1.0f, 0.0f) == 0.0f,
			"the jumps by skill and jumpiness");
		CHECK(NEAR(pgbots_rules_detour_chance(1.0f, 0.5f), 0.8f) &&
			pgbots_rules_detour_chance(1.0f, 0.9f) > pgbots_rules_detour_chance(1.0f, 0.1f), "the detour's chance");
		return 0;
	}

	CASE("posture")
	{
		CHECK(NEAR(pgbots_rules_posture(0, 0, 50, 0.0f, 0.0f, 4, 4, 4), 0.5f), "an even game's posture");
		CHECK(pgbots_rules_posture(40, 10, 50, 0.0f, 0.0f, 4, 4, 4) < 0.3f, "ahead does not hide and hold");
		CHECK(pgbots_rules_posture(10, 40, 50, 0.0f, 0.0f, 4, 4, 4) > 0.7f, "behind does not go all in");
		CHECK(pgbots_rules_posture(30, 20, 50, 9.0f, 10.0f, 4, 4, 4) < pgbots_rules_posture(30, 20, 50, 1.0f, 10.0f, 4, 4, 4),
			"late in a lead is not more careful");
		CHECK(pgbots_rules_posture(0, 0, 50, 0.0f, 0.0f, 4, 1, 4) > 0.5f, "more alive is not bolder");
		CHECK(NEAR(pgbots_rules_slot_cost(30.0f, 1, 1.0f), 1.0f + 0.5f - 0.8f), "the slot's cost");
		return 0;
	}

	CASE("drive")
	{
		struct pgbots_drive_input in;
		struct pgbots_drive_output out;

		memset(&in, 0, sizeof(in));
		in.top_speed = 10.0f;
		in.acceleration = 5.0f;
		in.lookahead = 5.0f;
		in.wheelbase = 0.8f;
		in.point_distance = 5.0f;
		in.arrival = -1.0f;
		pgbots_rules_drive(&in, &out);
		CHECK(out.throttle == 1.0f && !out.reversing && NEAR(out.steer, 0.0f), "straight ahead is not full throttle");
		in.forward_speed = 10.0f;
		in.turn_ahead = 90.0f * DEG;
		pgbots_rules_drive(&in, &out);
		CHECK(out.throttle < 0.0f && out.brake, "no braking for a sharp turn ahead");
		CHECK(NEAR(pgbots_rules_speed_target(10.0f, 90.0f * DEG), 3.5f) && NEAR(pgbots_rules_speed_target(10.0f, 0.0f), 10.0f),
			"the corner speed");
		in.forward_speed = 0.0f;
		in.turn_ahead = 0.0f;
		in.alpha = 2.5f;
		in.point_distance = 3.0f;
		pgbots_rules_drive(&in, &out);
		CHECK(out.reversing && out.throttle == -1.0f && NEAR(out.steer, 2.5f), "a point behind and near: no reverse");
		in.reversing = 1;
		in.alpha = 1.5f;
		in.reverse_seconds = 0.5f;
		pgbots_rules_drive(&in, &out);
		CHECK(out.reversing, "the reverse left too early (hysteresis)");
		in.alpha = 0.8f;
		pgbots_rules_drive(&in, &out);
		CHECK(!out.reversing, "the reverse kept with the point ahead");
		in.reversing = 1;
		in.alpha = 1.5f;
		in.reverse_seconds = 1.6f;
		pgbots_rules_drive(&in, &out);
		CHECK(!out.reversing, "the reverse kept past 1.5 s");
		in.reversing = 0;
		in.alpha = 1.2f;
		in.point_distance = 10.0f;
		pgbots_rules_drive(&in, &out);
		CHECK(!out.reversing && out.throttle <= 0.4f, "no throttle cap while turning hard");
		in.ghost = 1;
		in.cross_track = 0.4f;
		pgbots_rules_drive(&in, &out);
		CHECK(out.throttle <= 0.3f && NEAR(out.strafe, 0.6f), "the Ghost's strafe or turn");
		in.ghost = 0;
		in.tank = 1;
		in.alpha = 0.5f;
		in.forward_speed = 10.0f;
		in.turn_ahead = 90.0f * DEG;
		pgbots_rules_drive(&in, &out);
		CHECK(out.throttle >= 0.3f, "the tank's hull stops turning");
		in.alpha = 2.0f;
		in.point_distance = 3.0f;
		pgbots_rules_drive(&in, &out);
		CHECK(!out.reversing, "the tank reverses under 120 degrees");
		CHECK(pgbots_rules_lookahead(0.5f, 0.0f) == 1.5f && pgbots_rules_lookahead(2.0f, 20.0f) == 8.0f, "the lookahead");
		CHECK(pgbots_rules_banshee_pitch(100.0f, 0.0f, 1.0f) <= 35.0f * DEG + 1e-5f &&
			pgbots_rules_banshee_pitch(-5.0f, 0.0f, 10.0f) < 0.0f, "the Banshee's pitch");
		CHECK(NEAR(pgbots_rules_board_score(0.8f, 5.0f, 80.0f), 0.64f) && pgbots_rules_board_score(0.8f, 30.0f, 80.0f) == 0.0f,
			"the boarding score");
		return 0;
	}

	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
