/*
PGBOTS_RULES.C

The Proving Grounds bots' rules (pgbots_rules.h): plain numbers, no engine.
Every number is the design's starting point (research_notes/ppg-bot-
personalities.md, S5-S12), to be tuned in play. Clean room: written from that
design alone, in our own words; nothing of Project Proving Grounds' code.
*/

#include "bots_combat.h"
#include "pgbots_rules.h"

#include <math.h>

#define DEGREES (PGBOTS_PI / 180.0f)

static float pgbots_clamp(float value, float low, float high)
{
	return value < low ? low : value > high ? high : value;
}

static float pgbots_clamp01(float value)
{
	return pgbots_clamp(value, 0.0f, 1.0f);
}

static float pgbots_lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

static float pgbots_inverse_lerp(float a, float b, float value)
{
	return b == a ? 0.0f : pgbots_clamp01((value - a) / (b - a));
}

/* ---------- the skill tables (S5): easy, normal, heroic and legendary
the design's four tiers; recruit below easy, veteran between heroic and
legendary */

static struct pgbots_hands const pgbots_hands_table[NUMBER_OF_BOTS_SKILLS] =
{
	/* reaction track lag flick overshoot error lead turn memory head press follow early ram callout hear */
	{ 0.35f, 0.14f, 0.13f, 1.3f, 0.45f, 0.012f, 0.6f, 200.0f, 3.0f, 0.10f, 0.0f, 0.4f, 0.45f, -1.0f, 0.2f, 0.35f },
	{ 0.275f, 0.10f, 0.09f, 1.0f, 0.30f, 0.009f, 0.8f, 270.0f, 4.0f, 0.20f, 0.0f, 0.35f, 0.4f, -1.0f, 0.40f, 0.30f },
	{ 0.22f, 0.10f, 0.09f, 1.0f, 0.30f, 0.009f, 0.8f, 435.0f, 6.0f, 0.5f, 0.0f, 0.3f, 0.35f, 20.0f, 0.6f, 0.25f },
	{ 0.187f, 0.085f, 0.058f, 0.80f, 0.30f, 0.0068f, 0.8f, 650.0f, 8.0f, 0.9f, 0.5f, 0.25f, 0.3f, 12.0f, 0.8f, 0.2f },
	{ 0.17f, 0.083f, 0.052f, 0.75f, 0.30f, 0.0064f, 0.8f, 825.0f, 9.0f, 0.93f, 0.75f, 0.12f, 0.15f, 6.0f, 0.9f, 0.17f },
	{ 0.154f, 0.08f, 0.045f, 0.70f, 0.30f, 0.006f, 0.8f, 1000.0f, 10.0f, 0.97f, 1.0f, 0.0f, 0.0f, 0.0f, 0.95f, 0.15f },
};

static struct pgbots_craft const pgbots_craft_table[NUMBER_OF_BOTS_SKILLS] =
{
	/* jump strafe juke routes map_iq tactics aware radar teamwork */
	{ 0.0f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
	{ 0.5f, 0.7f, 0.1f, 0.3f, 0.3f, 0.4f, 0.35f, 0.5f, 0.55f },
	{ 0.7f, 0.85f, 0.6f, 0.7f, 0.7f, 0.75f, 0.75f, 0.8f, 0.8f },
	{ 0.85f, 0.92f, 0.8f, 0.85f, 0.85f, 0.88f, 0.88f, 0.9f, 0.88f },
	{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.95f },
};

#define CLASS_BIT(c) (1UL << (c))

/* ---------- the personalities (S12), in the order they are dealt */

static struct pgbots_personality_definition const pgbots_personalities[NUMBER_OF_PGBOTS_PERSONALITIES] =
{
	/* aggression caution camping objective grenades jumpy crouchy grudge stickiness greed route_style */
	{ "balanced", { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f }, 0, 0 },
	{ "rusher", { 0.9f, 0.2f, 0.0f, 0.4f, 0.5f, 0.7f, 0.2f, 0.5f, 0.6f, 0.4f, 0.3f },
		CLASS_BIT(_pgbots_class_close) | CLASS_BIT(_pgbots_class_automatic), CLASS_BIT(_pgbots_class_sniper) },
	{ "anchor", { 0.3f, 0.7f, 0.9f, 0.6f, 0.3f, 0.1f, 0.7f, 0.3f, 0.5f, 0.3f, 0.1f },
		CLASS_BIT(_pgbots_class_precision), 0 },
	{ "marksman", { 0.3f, 0.8f, 0.7f, 0.3f, 0.2f, 0.1f, 0.8f, 0.3f, 0.7f, 0.5f, 0.2f },
		CLASS_BIT(_pgbots_class_sniper) | CLASS_BIT(_pgbots_class_precision), CLASS_BIT(_pgbots_class_close) },
	{ "flanker", { 0.6f, 0.5f, 0.1f, 0.5f, 0.4f, 0.4f, 0.5f, 0.4f, 0.5f, 0.4f, 0.9f },
		CLASS_BIT(_pgbots_class_automatic) | CLASS_BIT(_pgbots_class_close), 0 },
	{ "grenadier", { 0.7f, 0.3f, 0.2f, 0.4f, 0.95f, 0.5f, 0.2f, 0.4f, 0.5f, 0.5f, 0.3f },
		CLASS_BIT(_pgbots_class_explosive), 0 },
	{ "objective", { 0.5f, 0.4f, 0.1f, 0.95f, 0.4f, 0.3f, 0.3f, 0.2f, 0.4f, 0.3f, 0.4f }, 0, 0 },
	{ "grudger", { 0.7f, 0.3f, 0.2f, 0.3f, 0.5f, 0.4f, 0.4f, 0.95f, 0.9f, 0.4f, 0.3f }, 0, 0 },
};

static short pgbots_skill_index(short skill)
{
	return skill >= 0 && skill < NUMBER_OF_BOTS_SKILLS ? skill : _bots_skill_normal;
}

struct pgbots_hands const *pgbots_rules_hands(short skill)
{
	return &pgbots_hands_table[pgbots_skill_index(skill)];
}

struct pgbots_craft const *pgbots_rules_craft(short skill)
{
	return &pgbots_craft_table[pgbots_skill_index(skill)];
}

struct pgbots_personality_definition const *pgbots_rules_personality(short personality)
{
	return &pgbots_personalities[personality >= 0 && personality < NUMBER_OF_PGBOTS_PERSONALITIES ? personality :
		_pgbots_personality_balanced];
}

short pgbots_rules_personality_named(char const *name)
{
	short index;

	if (!name || !name[0])
		return -1;
	for (index = 0; index < NUMBER_OF_PGBOTS_PERSONALITIES; index++)
	{
		char const *a = pgbots_personalities[index].name, *b = name;

		while (*a && (*b | 0x20) == *a)
		{
			a++;
			b++;
		}
		if (!*a && !*b)
			return index;
	}
	return -1;
}

short pgbots_rules_deal(long team_rank, unsigned long offset)
{
	if (team_rank < 0)
		team_rank = 0;
	return (short)((offset + (unsigned long)team_rank) % NUMBER_OF_PGBOTS_PERSONALITIES);
}

float pgbots_rules_jitter(float value, float random)
{
	return value * (0.9f + 0.2f * random);
}

void pgbots_rules_profile(struct pgbots_profile *profile, short skill, short personality, unsigned long *random)
{
	struct pgbots_personality_definition const *definition = pgbots_rules_personality(personality);
	struct pgbots_hands *hands = &profile->hands;
	struct pgbots_craft *craft = &profile->craft;
	long index;

	profile->skill = pgbots_skill_index(skill);
	profile->personality = (short)(definition - pgbots_personalities);
	*hands = *pgbots_rules_hands(skill);
	*craft = *pgbots_rules_craft(skill);
	/* (each value its own jitter, in this order: the same bot, the same
	profile) */
	hands->reaction = pgbots_rules_jitter(hands->reaction, pgbots_rules_random01(random));
	hands->track = pgbots_rules_jitter(hands->track, pgbots_rules_random01(random));
	hands->lag = pgbots_rules_jitter(hands->lag, pgbots_rules_random01(random));
	hands->flick = pgbots_rules_jitter(hands->flick, pgbots_rules_random01(random));
	hands->overshoot = pgbots_clamp01(pgbots_rules_jitter(hands->overshoot, pgbots_rules_random01(random)));
	hands->aim_error = pgbots_rules_jitter(hands->aim_error, pgbots_rules_random01(random));
	hands->lead = pgbots_rules_jitter(hands->lead, pgbots_rules_random01(random));
	hands->turn_degrees = pgbots_rules_jitter(hands->turn_degrees, pgbots_rules_random01(random));
	hands->memory = pgbots_rules_jitter(hands->memory, pgbots_rules_random01(random));
	hands->head_share = pgbots_clamp01(pgbots_rules_jitter(hands->head_share, pgbots_rules_random01(random)));
	hands->press_share = pgbots_clamp01(pgbots_rules_jitter(hands->press_share, pgbots_rules_random01(random)));
	hands->loose_follow = pgbots_rules_jitter(hands->loose_follow, pgbots_rules_random01(random));
	hands->loose_early = pgbots_clamp01(pgbots_rules_jitter(hands->loose_early, pgbots_rules_random01(random)));
	hands->callout_chance = pgbots_clamp01(pgbots_rules_jitter(hands->callout_chance, pgbots_rules_random01(random)));
	hands->hear_delay = pgbots_rules_jitter(hands->hear_delay, pgbots_rules_random01(random));
	craft->jump = pgbots_clamp01(pgbots_rules_jitter(craft->jump, pgbots_rules_random01(random)));
	craft->strafe = pgbots_clamp01(pgbots_rules_jitter(craft->strafe, pgbots_rules_random01(random)));
	craft->juke = pgbots_clamp01(pgbots_rules_jitter(craft->juke, pgbots_rules_random01(random)));
	craft->routes = pgbots_clamp01(pgbots_rules_jitter(craft->routes, pgbots_rules_random01(random)));
	craft->map_iq = pgbots_clamp01(pgbots_rules_jitter(craft->map_iq, pgbots_rules_random01(random)));
	craft->tactics = pgbots_clamp01(pgbots_rules_jitter(craft->tactics, pgbots_rules_random01(random)));
	craft->aware = pgbots_clamp01(pgbots_rules_jitter(craft->aware, pgbots_rules_random01(random)));
	craft->radar = pgbots_clamp01(pgbots_rules_jitter(craft->radar, pgbots_rules_random01(random)));
	craft->teamwork = pgbots_clamp01(pgbots_rules_jitter(craft->teamwork, pgbots_rules_random01(random)));
	for (index = 0; index < NUMBER_OF_PGBOTS_TRAITS; index++)
		profile->traits[index] = pgbots_clamp01(pgbots_rules_jitter(definition->traits[index], pgbots_rules_random01(random)));
	profile->liked = definition->liked;
	profile->disliked = definition->disliked;
}

int pgbots_rules_novice(struct pgbots_profile const *profile)
{
	return profile->skill <= _bots_skill_easy;
}

/* ---------- random numbers */

unsigned long pgbots_rules_random(unsigned long *state)
{
	unsigned long x = *state ? *state : 0x9E3779B9UL;

	x ^= x << 13;
	x &= 0xFFFFFFFFUL;
	x ^= x >> 17;
	x ^= x << 5;
	x &= 0xFFFFFFFFUL;
	*state = x;
	return x;
}

float pgbots_rules_random01(unsigned long *state)
{
	return (float)(pgbots_rules_random(state) & 0xFFFFFF) / (float)0x1000000;
}

float pgbots_rules_gaussian(float random_a, float random_b)
{
	float a = random_a < 1e-6f ? 1e-6f : random_a;

	return (float)(sqrt(-2.0 * log(a)) * cos(2.0 * PGBOTS_PI * random_b));
}

/* ---------- perception */

float pgbots_rules_recognition_seconds(float reaction, int peripheral, int relost, float gaussian)
{
	float seconds = reaction * (float)exp(0.25 * pgbots_clamp(gaussian, -3.0f, 3.0f));

	if (peripheral)
		seconds *= 1.6f;
	if (relost)
		seconds *= 0.35f;
	return seconds;
}

int pgbots_rules_in_view(float bearing, float distance)
{
	return distance <= 2.5f || (fabs(bearing) <= 55.0f * DEGREES && distance <= 80.0f);
}

int pgbots_rules_foveal(float bearing)
{
	return fabs(bearing) <= 35.0f * DEGREES;
}

float pgbots_rules_radar_chance(float radar, long scan_ticks)
{
	float tick = pgbots_clamp01(radar * 0.15f);

	return 1.0f - (float)pow(1.0f - tick, (double)(scan_ticks > 0 ? scan_ticks : 1));
}

/* ---------- aim */

float pgbots_rules_sample_seconds(float track, float random)
{
	return track * (0.8f + 0.4f * random);
}

float pgbots_rules_lead_scale(float lead, float random)
{
	return lead * (0.6f + 0.6f * random);
}

float pgbots_rules_focus(float seconds_on_target)
{
	return pgbots_clamp01(seconds_on_target / 1.5f);
}

float pgbots_rules_aim_sigma(float aim_error, float focus)
{
	return aim_error * (1.0f - 0.7f * pgbots_clamp01(focus));
}

float pgbots_rules_flick_seconds(float delta, float track, float flick, float turn_radians_per_second)
{
	float magnitude = (float)fabs(delta);
	float fitts = (0.06f + 0.07f * (float)log2(1.0 + magnitude / 0.05)) * (float)pow(track / 0.18f, 0.3) * flick;
	float slowest = turn_radians_per_second > 0.0f ? magnitude / turn_radians_per_second : 0.0f;

	return fitts > slowest ? fitts : slowest;
}

int pgbots_rules_is_flick(float delta)
{
	return fabs(delta) > 0.35f;
}

float pgbots_rules_ease(float t)
{
	t = pgbots_clamp01(t);
	return t * t * (3.0f - 2.0f * t);
}

float pgbots_rules_overshoot(float chance, float random_roll, float random_size)
{
	return random_roll < chance ? 0.08f + 0.10f * random_size : 0.0f;
}

float pgbots_rules_pursuit_step(float error, float dt, float lag)
{
	float gain = lag > 0.0f ? 1.0f - (float)exp(-dt / lag) : 1.0f;
	float step = error * gain, cap = 9.0f * dt;

	return pgbots_clamp(step, -cap, cap);
}

int pgbots_rules_aim_head(int precision_class, int shield_down, int novice, float head_share, float random)
{
	return precision_class && (shield_down || novice) && random < head_share;
}

/* ---------- the trigger */

int pgbots_rules_may_fire(struct pgbots_fire_input const *input)
{
	float across = 0.12f, vertical = 0.22f;
	int seen = input->seen_age <= 0.25f;
	int on;

	if (input->teammate_in_line)
		return 0;
	if (input->weapon_class == _pgbots_class_objective)
		return 0;
	if (input->weapon_class == _pgbots_class_explosive && input->distance < input->blast_radius)
		return 0;
	if (input->weapon_class == _pgbots_class_close && input->distance > input->band_high)
		return 0;
	if (input->distance > 2.2f * input->band_high && !input->deterrent)
		return 0;
	if (!seen && input->seen_age > 0.25f + input->loose_follow)
		return 0;
	/* (point blank: a body fills the view, the window twice as wide) */
	if (input->distance < 1.6f)
	{
		across *= 2.0f;
		vertical *= 2.0f;
	}
	on = fabs(input->across) <= across && fabs(input->vertical) <= vertical;
	if (!on && input->flicking && input->loose_early > 0.0f && input->flick_progress >= 1.0f - input->loose_early)
		on = 1;
	if (!on)
		return 0;
	/* (the loose follow-through fires on where the target went; in sight,
	the line must be clear) */
	return seen ? input->line_clear : 1;
}

float pgbots_rules_press_interval(float rate_of_fire, float distance, float random)
{
	float interval = rate_of_fire > 0.0f ? 1.0f / rate_of_fire : 0.33f;

	if (distance < 4.0f)
		return interval * 0.85f;
	if (distance <= 10.0f)
		return interval * 1.05f;
	return interval * (1.1f + 0.25f * random);
}

void pgbots_rules_burst(float distance, float random_a, float random_b, float *burst, float *gap)
{
	if (distance < 4.0f)
	{
		*burst = 0.5f;
		*gap = 0.0f;
		return;
	}
	*burst = 0.22f + 0.18f * random_a;
	*gap = 0.15f + 0.15f * random_b;
}

/* ---------- target choice */

float pgbots_rules_threat(struct pgbots_threat_input const *input, float tactics, float teamwork, float grudge)
{
	float threat = input->power_weapon && input->in_band ? 100.0f : input->in_band ? 70.0f : 30.0f;
	float near = 1.5f + 0.4f * tactics;

	if (input->distance <= 0.8f)
		threat *= near;
	else if (input->distance >= 30.0f)
		threat *= 0.4f;
	else
		threat *= pgbots_lerp(near, 0.4f, (input->distance - 0.8f) / (30.0f - 0.8f));
	if (input->shield_down)
		threat *= 1.15f;
	if (input->aiming_at_us)
		threat *= 1.3f;
	else if (input->aiming_away)
		threat *= 0.5f + 0.6f * tactics;
	if (input->shooting_teammate)
		threat *= 1.0f + 0.6f * tactics;
	if (input->carrier)
		threat += 40.0f;
	if (input->teammate_on_him || input->team_focus)
		threat += 60.0f * teamwork;
	if (input->last_killer)
		threat += 15.0f * grudge;
	if (input->shooting_us)
		threat += 60.0f * tactics;
	return threat;
}

float pgbots_rules_switch_factor(float tactics, float damage_done, float stickiness)
{
	return (1.25f + 0.75f * (1.0f - tactics)) * (1.0f + 2.0f * pgbots_clamp01(damage_done)) *
		(0.75f + 0.5f * pgbots_clamp01(stickiness));
}

int pgbots_rules_switch(float current, float candidate, float factor, float held_seconds)
{
	return held_seconds >= 1.0f && candidate > current * factor;
}

/* ---------- weapons */

short pgbots_rules_weapon_class(struct pgbots_weapon_facts const *facts)
{
	if (facts->is_flag)
		return _pgbots_class_objective;
	if (facts->charging_time > 0.0f)
		return _pgbots_class_charge;
	if (facts->projectiles_per_shot >= 5 || (facts->maximum_range > 0.0f && facts->maximum_range < 5.0f))
		return _pgbots_class_close;
	if (facts->blast_radius >= 1.0f || (facts->danger_radius > 0.0f && facts->speed > 0.0f && facts->speed < 30.0f))
		return _pgbots_class_explosive;
	if (facts->zoom_levels >= 2 && facts->zoom_maximum >= 6.0f)
		return _pgbots_class_sniper;
	if (facts->zoom_levels >= 1 && facts->projectiles_per_shot == 1 && facts->error <= 0.0175f &&
		facts->rate_of_fire <= 6.0f)
	{
		return _pgbots_class_precision;
	}
	return _pgbots_class_automatic;
}

void pgbots_rules_class_band(short weapon_class, float maximum_range, float *low, float *high)
{
	static float const bands[NUMBER_OF_PGBOTS_CLASSES][2] =
	{
		{ 0.0f, 0.0f }, { 0.0f, 10.0f }, { 0.0f, 2.0f }, { 2.5f, 35.0f }, { 8.0f, 80.0f }, { 1.5f, 30.0f },
		{ 0.0f, 6.0f },
	};
	short index = weapon_class >= 0 && weapon_class < NUMBER_OF_PGBOTS_CLASSES ? weapon_class : _pgbots_class_automatic;

	*low = bands[index][0];
	*high = bands[index][1];
	if (maximum_range > 0.0f && maximum_range < *high)
		*high = maximum_range;
	if (*low > *high)
		*low = *high;
}

float pgbots_rules_class_rank(short weapon_class)
{
	static float const ranks[NUMBER_OF_PGBOTS_CLASSES] = { -1.0f, 1.5f, 3.0f, 5.0f, 4.0f, 3.2f, 2.0f };

	return weapon_class >= 0 && weapon_class < NUMBER_OF_PGBOTS_CLASSES ? ranks[weapon_class] : 2.0f;
}

static float pgbots_band_fit_at(float low, float high, float distance)
{
	if (distance < low)
		return low > 0.0f ? pgbots_clamp01((distance - 0.5f * low) / (0.5f * low)) : 1.0f;
	if (distance > high)
		return high > 0.0f ? pgbots_clamp01((1.5f * high - distance) / (0.5f * high)) : 0.0f;
	return 1.0f;
}

float pgbots_rules_band_fit(float low, float high, float distance)
{
	static float const middles[4] = { 2.0f, 7.0f, 15.0f, 30.0f };
	static float const weights[4] = { 0.3f, 0.35f, 0.25f, 0.1f };
	float fit = 0.0f;
	long index;

	if (distance >= 0.0f)
		return pgbots_band_fit_at(low, high, distance);
	for (index = 0; index < 4; index++)
		fit += weights[index] * pgbots_band_fit_at(low, high, middles[index]);
	return fit;
}

float pgbots_rules_weapon_score(short weapon_class, float low, float high, float distance, unsigned long liked,
	unsigned long disliked)
{
	float score;

	if (weapon_class == _pgbots_class_objective)
		return -1.0f;
	score = pgbots_rules_class_rank(weapon_class) + 2.0f * pgbots_rules_band_fit(low, high, distance);
	if (liked & CLASS_BIT(weapon_class))
		score += 1.0f;
	if (disliked & CLASS_BIT(weapon_class))
		score -= 0.5f;
	return score;
}

int pgbots_rules_weapon_switch(float current, float candidate)
{
	return candidate > current + 1.0f;
}

short pgbots_rules_zoom(short weapon_class, short zoom_levels, float distance)
{
	if (zoom_levels <= 0)
		return 0;
	if (weapon_class == _pgbots_class_sniper)
		return distance >= 20.0f && zoom_levels >= 2 ? 2 : distance >= 6.0f ? 1 : 0;
	if (weapon_class == _pgbots_class_precision)
		return distance > 8.0f ? 1 : 0;
	return 0;
}

/* ---------- goals */

void pgbots_rules_goal_scores(struct pgbots_goal_input const *input, struct pgbots_profile const *profile,
	short personality, float *scores)
{
	float aggression = profile->traits[_pgbots_trait_aggression];
	float caution = profile->traits[_pgbots_trait_caution];
	float objective = profile->traits[_pgbots_trait_objective];
	float tactics = profile->craft.tactics;
	float teamwork = profile->craft.teamwork;
	float posture = pgbots_clamp01(input->posture);
	long goal;

	for (goal = 0; goal < NUMBER_OF_PGBOTS_GOALS; goal++)
		scores[goal] = -1.0f;
	scores[_pgbots_goal_roam] = 0.3f;
	if (input->target_visible)
	{
		scores[_pgbots_goal_fight] = 1.5f + 0.4f * aggression;
		if (input->runner && input->target_distance > 12.0f)
			scores[_pgbots_goal_fight] -= 1.2f * objective;
	}
	if (input->committed && scores[_pgbots_goal_fight] < 2.6f + 0.3f * aggression)
		scores[_pgbots_goal_fight] = 2.6f + 0.3f * aggression;
	if (input->react)
		scores[_pgbots_goal_react] = 2.7f + 0.5f * profile->craft.aware;
	if (input->attacker_unseen)
		scores[_pgbots_goal_attacker] = 2.7f + 0.5f * tactics;
	else if (input->hit_unknown)
		scores[_pgbots_goal_attacker] = 1.4f + 0.4f * aggression;
	if (input->retreat_ok)
		scores[_pgbots_goal_retreat] = 2.5f + 0.5f * tactics + 0.3f * caution;
	if (input->recharge || input->recharging)
	{
		scores[_pgbots_goal_recharge] = 1.6f + 0.6f * tactics;
		if (input->recharging && scores[_pgbots_goal_recharge] < 2.3f)
			scores[_pgbots_goal_recharge] = 2.3f;
	}
	if (input->hunt_belief)
	{
		float hunt = (0.3f + 0.6f * aggression + (input->hunt_weak ? 0.35f : 0.0f)) * (0.4f + 1.2f * posture);

		if (input->alone)
			hunt *= 1.0f - 0.6f * tactics;
		if (personality == _pgbots_personality_marksman && input->hunt_distance < 25.0f)
			hunt *= 0.4f;
		scores[_pgbots_goal_hunt] = hunt;
	}
	if (input->objective || input->carrying)
	{
		float score = 0.5f + 0.7f * objective;

		if (input->stand_unguarded)
			score += 1.0f;
		if (input->grab_near && score < 2.4f)
			score = 2.4f;
		if (input->carrying)
			score = 3.0f;
		scores[_pgbots_goal_objective] = score;
	}
	if (input->hold)
		scores[_pgbots_goal_hold] = 0.2f + 0.6f * profile->traits[_pgbots_trait_camping];
	/* (the role's leash: its order's pull grows with teamwork, more when
	far from its point) */
	if (input->order)
		scores[_pgbots_goal_order] = 0.4f + 0.8f * teamwork + (input->order_distance > 20.0f ? 0.4f * teamwork : 0.0f);
	if (input->hurt >= 0.35f && input->health_distance >= 0.0f)
		scores[_pgbots_goal_health] = 0.4f + 1.6f * input->hurt - input->health_distance / 80.0f;
	if (input->powerup_distance >= 0.0f)
	{
		float score = 0.3f + 0.9f * profile->craft.map_iq * input->powerup_value - input->powerup_distance / 120.0f +
			0.3f * (profile->traits[_pgbots_trait_greed] - 0.5f);

		if (input->powerup_camouflage &&
			(personality == _pgbots_personality_flanker || personality == _pgbots_personality_marksman))
		{
			score += 0.25f;
		}
		if (input->powerup_overshield)
			score += 0.3f * (posture - 0.5f);
		/* (camouflage and the overshield near: worth a detour at any level,
		before a hunt or a loose order) */
		if ((input->powerup_camouflage || input->powerup_overshield) && input->powerup_distance < 20.0f &&
			score < 1.05f - input->powerup_distance / 80.0f)
		{
			score = 1.05f - input->powerup_distance / 80.0f;
		}
		scores[_pgbots_goal_powerup] = score;
	}
	if (input->dry)
		scores[_pgbots_goal_weapon] = 3.0f;
	else if (input->fetch)
		scores[_pgbots_goal_weapon] = 1.85f;
	else if (input->upgrade)
		scores[_pgbots_goal_weapon] = 1.2f + 0.4f * profile->craft.map_iq + input->weapon_taste;
	if (input->vehicle > 0.0f)
		scores[_pgbots_goal_vehicle] = input->vehicle;

	/* the modifiers, in order: avoid, (the leash, above), stalemate, a close
	enemy, a race, no headway */
	if (posture <= 0.3f && input->outnumbered && !input->just_hit && input->target_distance > 8.0f &&
		scores[_pgbots_goal_fight] > 0.0f)
	{
		float retreat = 2.5f + 0.5f * tactics + 0.3f * caution;

		scores[_pgbots_goal_fight] = 0.3f;
		if (scores[_pgbots_goal_retreat] < retreat)
			scores[_pgbots_goal_retreat] = retreat;
	}
	if (input->stalemate && input->target_distance > 6.0f && scores[_pgbots_goal_fight] > 0.0f)
		scores[_pgbots_goal_fight] *= 0.5f;
	if (input->target_visible && input->target_distance < 8.0f)
		scores[_pgbots_goal_fight] = 10.0f;
	if (input->race)
	{
		static short const lowered[] = { _pgbots_goal_fight, _pgbots_goal_hunt, _pgbots_goal_health,
			_pgbots_goal_powerup, _pgbots_goal_weapon, _pgbots_goal_hold };
		long index;

		for (index = 0; index < (long)(sizeof(lowered) / sizeof(lowered[0])); index++)
		{
			if (scores[lowered[index]] > 0.0f)
				scores[lowered[index]] *= 0.3f;
		}
	}
	/* (the carrier: the flag before any fight, which it cannot fire in -
	it lets the flag go to fight, hiding: bots_team.h - and before turning
	to a hit or backing off; going back for the flag, its return near, the
	same) */
	if (input->carrying || input->regrab_urgent)
	{
		static short const yielding[] = { _pgbots_goal_fight, _pgbots_goal_react, _pgbots_goal_attacker,
			_pgbots_goal_retreat, _pgbots_goal_recharge, _pgbots_goal_hunt, _pgbots_goal_health,
			_pgbots_goal_powerup, _pgbots_goal_weapon, _pgbots_goal_vehicle };
		long index;

		for (index = 0; index < (long)(sizeof(yielding) / sizeof(yielding[0])); index++)
		{
			if (scores[yielding[index]] > 1.0f)
				scores[yielding[index]] = 1.0f;
		}
		scores[_pgbots_goal_objective] = 3.5f;
	}
	for (goal = 0; goal < NUMBER_OF_PGBOTS_GOALS; goal++)
	{
		if ((input->banned & (1UL << goal)) &&
			!(goal == _pgbots_goal_objective && (input->carrying || input->regrab_urgent)))
		{
			scores[goal] = -1.0f;
		}
	}
	scores[_pgbots_goal_none] = -1.0f;
}

short pgbots_rules_goal_pick(float const *scores, short current, float const *randoms)
{
	short goal, best = _pgbots_goal_roam;
	float best_score = -1e30f;

	for (goal = 1; goal < NUMBER_OF_PGBOTS_GOALS; goal++)
	{
		float score;

		if (scores[goal] < 0.0f)
			continue;
		score = scores[goal] + 0.15f * randoms[goal];
		if (goal == current && goal != _pgbots_goal_hold)
			score += 0.2f;
		if (score > best_score)
		{
			best_score = score;
			best = goal;
		}
	}
	return best;
}

/* ---------- fighting */

float pgbots_rules_cracked(float posture)
{
	return pgbots_clamp(0.25f + 0.5f * (0.5f - posture), 0.05f, 0.45f);
}

float pgbots_rules_retreat_range(float tactics)
{
	return 8.0f + 6.0f * tactics;
}

float pgbots_rules_grenade_opener(float grenades, float tactics)
{
	float chance = 1.6f * grenades * (0.5f + 0.5f * tactics);

	return chance < 1.0f ? chance : 1.0f;
}

void pgbots_rules_keep_range(float band_high, float aggression, float caution, float *near, float *far)
{
	float scale = 1.0f + 0.25f * pgbots_clamp(caution - aggression, -1.0f, 1.0f);

	*near = 0.6f * band_high * scale;
	*far = 1.0f * band_high * scale;
}

float pgbots_rules_flip_jump(float jump, float jumpy)
{
	return 0.187f * jump * (0.5f + jumpy);
}

float pgbots_rules_travel_hops(float jump, float jumpy)
{
	return 0.306f * jump * (0.5f + jumpy);
}

float pgbots_rules_idle_hops(float jump, float jumpy)
{
	return 0.0255f * jump * (0.3f + jumpy);
}

float pgbots_rules_shot_jump(float jump, float jumpy)
{
	return 0.2125f * jumpy * jump;
}

float pgbots_rules_detour_chance(float routes, float route_style)
{
	return pgbots_clamp01((0.3f + 0.5f * routes) * (0.5f + route_style));
}

/* ---------- the director */

float pgbots_rules_posture(long our_score, long their_score, long score_limit, float elapsed, float time_limit,
	long alive_us, long alive_them, long team_size)
{
	float lead = score_limit > 0 ? (float)(our_score - their_score) / (float)score_limit : 0.0f;
	float late = time_limit > 0.0f ? pgbots_clamp01(elapsed / time_limit) : 0.0f;
	float numbers = team_size > 0 ? 0.2f * (float)(alive_us - alive_them) / (float)team_size : 0.0f;

	return pgbots_clamp01(0.5f - 1.5f * lead * (0.5f + late) + numbers);
}

float pgbots_rules_slot_cost(float distance, int role_changes, float fit)
{
	return distance / 30.0f + (role_changes ? 0.5f : 0.0f) - 0.8f * fit;
}

/* ---------- vehicles */

float pgbots_rules_lookahead(float path_radius, float speed)
{
	return pgbots_clamp(2.0f * path_radius + 0.6f * (float)fabs(speed), 1.5f, 8.0f);
}

float pgbots_rules_speed_target(float top_speed, float turn_ahead)
{
	return pgbots_lerp(top_speed, 0.35f * top_speed, pgbots_inverse_lerp(15.0f * DEGREES, 90.0f * DEGREES,
		(float)fabs(turn_ahead)));
}

void pgbots_rules_drive(struct pgbots_drive_input const *input, struct pgbots_drive_output *output)
{
	float top = input->top_speed > 0.1f ? input->top_speed : 0.1f;
	float alpha = input->alpha, magnitude = (float)fabs(alpha);
	float target, error;
	float reverse_above = input->tank ? 120.0f * DEGREES : 110.0f * DEGREES;
	int reversing = input->reversing;

	output->strafe = 0.0f;
	output->brake = 0;
	/* (reverse: entered for a point behind and near, or to get unstuck; left
	once it is ahead enough, or after 1.5 s) */
	if (!reversing && ((magnitude > reverse_above && input->point_distance < 4.0f) || input->stuck_reverse))
		reversing = 1;
	else if (reversing && !input->stuck_reverse && (magnitude < 60.0f * DEGREES || input->reverse_seconds > 1.5f))
		reversing = 0;
	output->reversing = reversing;
	if (reversing)
	{
		output->steer = alpha;
		output->throttle = -1.0f;
		return;
	}
	/* (pure pursuit, against drift) */
	output->steer = input->lookahead > 0.1f ?
		(float)atan(2.0f * input->wheelbase * (float)sin(alpha) / input->lookahead) : alpha;
	if (fabs(input->lateral_speed) > 0.15f * top)
	{
		float forward = (float)fabs(input->forward_speed) > 1.0f ? (float)fabs(input->forward_speed) : 1.0f;

		output->steer -= 0.5f * (float)atan(input->lateral_speed / forward);
	}
	target = pgbots_rules_speed_target(top, input->turn_ahead);
	if (input->arrival >= 0.0f && input->acceleration > 0.0f)
	{
		float stopping = (float)sqrt(2.0f * input->acceleration * input->arrival);

		if (stopping < target)
			target = stopping;
	}
	error = target - input->forward_speed;
	output->throttle = pgbots_clamp(error * (error > 0.0f ? 3.0f : 8.0f) / top, -1.0f, 1.0f);
	if (input->forward_speed > target + 0.15f * top)
		output->brake = 1;
	if (input->tank)
	{
		if (magnitude > 10.0f * DEGREES && output->throttle < 0.3f)
			output->throttle = 0.3f;
	}
	else if (magnitude > 60.0f * DEGREES)
	{
		float cap = input->ghost ? 0.3f : 0.4f;

		if (output->throttle > cap)
			output->throttle = cap;
	}
	if (input->ghost)
		output->strafe = pgbots_clamp(1.5f * input->cross_track, -1.0f, 1.0f);
}

float pgbots_rules_banshee_pitch(float height_error, float vertical_speed, float horizontal_distance)
{
	float pitch = (float)atan2(height_error - 0.5f * vertical_speed, horizontal_distance > 0.1f ? horizontal_distance : 0.1f);

	return pgbots_clamp(pitch, -35.0f * DEGREES, 35.0f * DEGREES);
}

float pgbots_rules_board_score(float value, float distance, float trip)
{
	float near = 1.0f - distance / 25.0f;
	float far = trip / 40.0f;

	return value * (near > 0.0f ? near : 0.0f) * (far < 1.0f ? far : 1.0f);
}
