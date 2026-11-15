/*
PGBOTS.C

(port) The Proving Grounds bots: the second kind of bot (bots.kind
"provinggrounds"; bots_common.h), next to Birchwood's (bots.c). Written from
research_notes/ppg-bot-personalities.md, a design in our own words after
Project Proving Grounds' bots, in a clean room: none of that project's code
(PolyForm Noncommercial, which our AGPL cannot take) is here.

A bot knows only what it has seen, heard, been told or been hit by: a belief
about each enemy (where, how fast, when, from what), forgotten after its
level's memory. Its eyes see within 55 degrees and 80 units (or anything
within 2.5) through the engine's sight test, and recognise what they see
after a moment (its reaction, slower at the edge of its view, quicker for an
enemy it just lost). A hit tells it who shot (the unit's damage record: what
a player's damage arc shows), the motion tracker (when the game shows it)
and gunfire tell it roughly where, and its team's silent callouts
(pgbots_director.c: in the engine, never chat or sound) tell it the area.

About ten times a second it scores its goals (pgbots_rules.c: fight, react,
retreat, recharge, hunt, the objective, a hold, the director's order, items,
a vehicle, roam), its personality's traits in the scores, the current goal
favoured and a little noise; the best wins. Its hands are a person's: it
reads the target's velocity only now and then (a juke beats it), flicks with
Fitts' law timing and sometimes overshoots, follows with a lag and a tremor,
fires only on target with a clear line (the loose trigger below legendary),
and paces its trigger by its weapon's class (taken from the weapon's tag:
precision, automatic, close, explosive, sniper, charge).

Difficulty sets the hands and the craft (bots.skill: pgbots_rules.c's
tables), personality sets the preferences and never the aim: eight
personalities, hidden, dealt round-robin per team so the rosters mirror, each
trait jittered 10% from the bot's own random numbers
(HALO_PGBOT_PERSONALITY=<name> forces one). Everything a bot does follows
from the game state and its random numbers (seeded per bot and game): the
same input, the same bots. Nothing of it is game state.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/unit_definitions.h"
#include "units/vehicles.h"
#include "items/items.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "game/game_globals.h"
#include "effects/effect_definitions.h"
#include "objects/damage_effect_definitions.h"
#include "physics/collisions.h"
#include "networking/network_game_globals.h"
#include "ai/path.h"
#include "tag_files/tag_groups.h"

#include "bots.h"
#include "bots_common.h"
#include "bots_combat.h"
#include "infection.h"
#include "pgbots.h"
#include "bots_team.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

#define DEGREES (PGBOTS_PI / 180.0f)
#define DT (1.0f / (real)TICKS_PER_SECOND)
#define SECONDS(s) ((long)((s) * (real)TICKS_PER_SECOND + 0.5f))
/* a biped's run, units a second (the retreat's reach in time) */
#define PGBOTS_RUN_SPEED 2.3f
/* the scan's and the decision's interval (ticks) */
#define PGBOTS_SCAN_TICKS 3

struct pgbots_globals pgbots_globals;

/* ---------- helpers */

real pgbots_distance(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

real pgbots_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

real pgbots_angle(real a)
{
	real angle = (real)fmod(a, 2.0f * PGBOTS_PI);

	if (angle > PGBOTS_PI)
		angle -= 2.0f * PGBOTS_PI;
	else if (angle < -PGBOTS_PI)
		angle += 2.0f * PGBOTS_PI;
	return angle;
}

real pgbots_yaw_to(real_point3d const *from, real_point3d const *to)
{
	return (real)atan2(to->y - from->y, to->x - from->x);
}

static real pgbots_pitch_to(real_point3d const *from, real_point3d const *to)
{
	return (real)atan2(to->z - from->z, pgbots_distance2d(from, to));
}

real pgbots_random01(struct pgbots_bot *bot)
{
	return pgbots_rules_random01(&bot->random);
}

static real pgbots_gaussian(struct pgbots_bot *bot)
{
	real a = pgbots_random01(bot);

	return pgbots_rules_gaussian(a, pgbots_random01(bot));
}

static long pgbots_random_ticks(struct pgbots_bot *bot, real low, real high)
{
	return SECONDS(low + (high - low) * pgbots_random01(bot));
}

boolean pgbots_enemy(struct player_datum *player, struct player_datum *other)
{
	if (player == other || other->unit_index == NONE)
		return FALSE;
	if (game_engine_has_teams() && player->team_index == other->team_index)
		return FALSE;
	return TRUE;
}

long pgbots_bot_index(struct pgbots_bot const *bot)
{
	return (long)(bot - pgbots_globals.bots);
}

void pgbots_profile_part(short part, unsigned long long *started)
{
	unsigned long long now;

	if (!*started)
		return;
	now = bots_time_us();
	pgbots_globals.part_us[part] += now - *started;
	*started = now;
}

long pgbots_ground(real_point3d const *point, real_point3d *ground)
{
	real_point3d raised = *point;

	raised.z += 0.3f;
	return bots_common_surface_below(&raised, ground);
}

static boolean pgbots_unit_dead(long unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;

	return !object || TEST_FLAG(object->object.damage_flags, _object_dead_bit);
}

/* the unit's velocity (units a tick), its vehicle's when riding */
static void pgbots_unit_velocity(long unit_index, real_vector3d *velocity)
{
	struct object_datum *object = object_get(unit_index);
	long vehicle_index = bots_common_unit_vehicle(unit_index);

	*velocity = vehicle_index != NONE ? object_get(vehicle_index)->object.translational_velocity :
		object->object.translational_velocity;
}

/* ---------- the tick's sight pairs: a ray from one unit to a point, asked
again in the same tick (the scan, the fire gate, another bot), answered
from here */

#define PGBOTS_SIGHT_CACHE 64
static struct
{
	long tick;
	struct
	{
		long tick;
		long unit_index;
		long target_unit_index;
		real_point3d point;
		boolean clear;
	} entries[PGBOTS_SIGHT_CACHE];
} pgbots_sight_cache;

boolean pgbots_sight(long unit_index, real_point3d const *eye, real_point3d const *point, long target_unit_index)
{
	long now = game_time_get();
	unsigned long hash = (unsigned long)unit_index * 2654435761UL ^ (unsigned long)target_unit_index * 40503UL ^
		(unsigned long)(long)(point->x * 4.0f) * 97UL ^ (unsigned long)(long)(point->y * 4.0f) * 31UL ^
		(unsigned long)(long)(point->z * 4.0f);
	long slot = (long)(hash % PGBOTS_SIGHT_CACHE);
	boolean clear;

	if (pgbots_sight_cache.entries[slot].tick == now && pgbots_sight_cache.entries[slot].unit_index == unit_index &&
		pgbots_sight_cache.entries[slot].target_unit_index == target_unit_index &&
		pgbots_distance(&pgbots_sight_cache.entries[slot].point, point) < 0.05f)
	{
		return pgbots_sight_cache.entries[slot].clear;
	}
	clear = bots_common_line_of_sight(unit_index, eye, point, target_unit_index);
	pgbots_globals.rays++;
	pgbots_sight_cache.entries[slot].tick = now;
	pgbots_sight_cache.entries[slot].unit_index = unit_index;
	pgbots_sight_cache.entries[slot].target_unit_index = target_unit_index;
	pgbots_sight_cache.entries[slot].point = *point;
	pgbots_sight_cache.entries[slot].clear = clear;
	return clear;
}

/* ---------- weapons: their classes from tag data alone (S9), once a map */

struct pgbots_weapon_info
{
	long definition_index;
	short weapon_class;
	real low;
	real high;
	real blast;
	/* units a tick (0: at once) */
	real speed;
	real rate;
	boolean latched;
	short zoom_levels;
};

#define PGBOTS_WEAPON_INFOS 32
static struct
{
	unsigned long generation;
	long count;
	struct pgbots_weapon_info items[PGBOTS_WEAPON_INFOS];
	struct pgbots_weapon_info spare;
} pgbots_weapons;

/* the blast's reach of an effect: its parts' damage effects' cutoff */
static real pgbots_effect_blast(long effect_index)
{
	struct effect_definition *effect;
	long event_index, part_index;
	real radius = 0.0f;

	if (effect_index == NONE)
		return 0.0f;
	effect = (struct effect_definition *)tag_get(EFFECT_DEFINITION_TAG, effect_index);
	for (event_index = 0; event_index < effect->events.count && event_index < 4; event_index++)
	{
		struct effect_event_definition *event = TAG_BLOCK_GET_ELEMENT(&effect->events, event_index,
			struct effect_event_definition);

		for (part_index = 0; part_index < event->parts.count && part_index < 8; part_index++)
		{
			struct effect_part_definition *part = TAG_BLOCK_GET_ELEMENT(&event->parts, part_index,
				struct effect_part_definition);

			/* (a part's kind is its runtime base class, as effects.c reads it) */
			if ((part->runtime_base_class_tag == DAMAGE_EFFECT_DEFINITION_TAG ||
				part->reference.group_tag == DAMAGE_EFFECT_DEFINITION_TAG) && part->reference.index != NONE)
			{
				struct damage_effect_definition *damage = damage_effect_definition_get(part->reference.index);

				radius = MAX(radius, damage->cutoff_radius);
			}
		}
	}
	return radius;
}

static real pgbots_projectile_blast(struct projectile_definition *projectile)
{
	real radius = 0.0f;
	long index;

	if (projectile->projectile.attached_detonation_damage.index != NONE)
		radius = damage_effect_definition_get(projectile->projectile.attached_detonation_damage.index)->cutoff_radius;
	for (index = 0; index < projectile->projectile.material_responses.count && index < 4; index++)
	{
		struct projectile_material_response_definition *response = TAG_BLOCK_GET_ELEMENT(
			&projectile->projectile.material_responses, index, struct projectile_material_response_definition);

		if (response->default_response == _projectile_material_response_detonate)
			radius = MAX(radius, pgbots_effect_blast(response->default_effect.index));
		radius = MAX(radius, pgbots_effect_blast(response->detonation_effect.index));
	}
	return radius;
}

static struct pgbots_weapon_info const *pgbots_weapon_info(long definition_index)
{
	struct pgbots_weapon_info *info;
	struct weapon_definition *definition;
	struct pgbots_weapon_facts facts;
	long index;

	if (pgbots_weapons.generation != bots_common_map_generation())
	{
		pgbots_weapons.generation = bots_common_map_generation();
		pgbots_weapons.count = 0;
	}
	for (index = 0; index < pgbots_weapons.count; index++)
	{
		if (pgbots_weapons.items[index].definition_index == definition_index)
			return &pgbots_weapons.items[index];
	}
	info = pgbots_weapons.count < PGBOTS_WEAPON_INFOS ? &pgbots_weapons.items[pgbots_weapons.count++] :
		&pgbots_weapons.spare;
	csmemset(info, 0, sizeof(*info));
	csmemset(&facts, 0, sizeof(facts));
	info->definition_index = definition_index;
	definition = weapon_definition_get(definition_index);
	facts.is_flag = (definition->weapon.flags >> 3) & 1;
	facts.zoom_levels = definition->weapon.zoom_level_count;
	facts.zoom_maximum = definition->weapon.zoom_magnification_maximum;
	facts.projectiles_per_shot = 1;
	if (definition->weapon.triggers.count > 0)
	{
		struct weapon_trigger_definition *trigger =
			TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);

		facts.charging_time = trigger->charging_time;
		facts.projectiles_per_shot = trigger->projectiles_per_shot;
		facts.error = trigger->projectile_error_angle_lower_bound;
		facts.rate_of_fire = trigger->initial_rate_of_fire;
		info->latched = TEST_FLAG(trigger->flags, _weapon_trigger_latched_bit) || trigger->charging_time > 0.0f;
		if (trigger->projectile.index != NONE)
		{
			struct projectile_definition *projectile = projectile_definition_get(trigger->projectile.index);

			info->speed = 0.5f * (projectile->projectile.initial_velocity + projectile->projectile.final_velocity);
			facts.speed = info->speed * (real)TICKS_PER_SECOND;
			facts.maximum_range = projectile->projectile.maximum_range;
			facts.danger_radius = projectile->projectile.danger_radius;
			facts.blast_radius = pgbots_projectile_blast(projectile);
		}
	}
	info->weapon_class = pgbots_rules_weapon_class(&facts);
	pgbots_rules_class_band(info->weapon_class, facts.maximum_range, &info->low, &info->high);
	info->blast = MAX(facts.blast_radius, facts.danger_radius);
	info->rate = facts.rate_of_fire;
	info->zoom_levels = facts.zoom_levels;
	if (info != &pgbots_weapons.spare)
	{
		static char const *const names[NUMBER_OF_PGBOTS_CLASSES] =
		{
			"objective", "charge", "close", "explosive", "sniper", "precision", "automatic",
		};

		platform_log("pgbots: weapon %s: %s, band %.1f-%.1f, blast %.1f, %.0f u/s, %.1f/s", tag_get_name(definition_index),
			names[info->weapon_class], info->low, info->high, info->blast, facts.speed, info->rate);
	}
	return info;
}

short pgbots_weapon_class(long definition_index, real *blast)
{
	struct pgbots_weapon_info const *info = pgbots_weapon_info(definition_index);

	if (blast)
		*blast = info->blast;
	return info->weapon_class;
}

static struct pgbots_weapon_info const *pgbots_weapon_of(long weapon_index)
{
	return weapon_index != NONE ? pgbots_weapon_info(weapon_get(weapon_index)->definition_index) : NULL;
}

/* ---------- the kind's map and bots */

static void pgbots_allocate(void)
{
	if (pgbots_globals.allocated)
		return;
	pgbots_globals.bots = (struct pgbots_bot *)calloc(BOTS_MAXIMUM, sizeof(struct pgbots_bot));
	pgbots_globals.allocated = pgbots_globals.bots != NULL;
}

void pgbots_initialize_for_new_map(void)
{
	long index;

	pgbots_allocate();
	if (pgbots_globals.bots)
	{
		for (index = 0; index < BOTS_MAXIMUM; index++)
		{
			csmemset(&pgbots_globals.bots[index], 0, sizeof(pgbots_globals.bots[index]));
			pgbots_globals.bots[index].player_index = NONE;
			pgbots_globals.bots[index].search_slot = NONE;
		}
	}
	csmemset(pgbots_globals.counts, 0, sizeof(pgbots_globals.counts));
	csmemset(pgbots_globals.part_us, 0, sizeof(pgbots_globals.part_us));
	pgbots_globals.rays = 0;
	pgbots_weapons.count = 0;
	pgbots_map_reset();
	pgbots_director_reset();
}

void pgbots_dispose_from_old_map(void)
{
	long index;

	if (pgbots_globals.bots)
	{
		for (index = 0; index < BOTS_MAXIMUM; index++)
		{
			if (pgbots_globals.bots[index].search_slot != NONE)
				bots_common_search_drop(pgbots_globals.bots[index].search_slot);
			pgbots_globals.bots[index].search_slot = NONE;
			pgbots_globals.bots[index].player_index = NONE;
		}
	}
	pgbots_map_release();
}

void pgbots_tick_begin(void)
{
	unsigned long long started = bots_common_profiling() ? bots_time_us() : 0;

	pgbots_allocate();
	pgbots_map_step();
	pgbots_profile_part(_pgbots_part_map, &started);
	pgbots_director_update();
	pgbots_profile_part(_pgbots_part_director, &started);
}

/* the bot's rank among its team's Proving Grounds bots (the dealing's) */
static long pgbots_team_rank(long bot_index, struct player_datum *player)
{
	struct data_iterator iterator;
	struct player_datum *other;
	long rank = 0;

	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long other_bot = other->network_player_data.machine_index - BOTS_FIRST_MACHINE;

		if (other->local_player_index != NONE || !bots_machine_is_bot(other->network_player_data.machine_index) ||
			other_bot >= bot_index || bots_bot_kind(other_bot) != _bots_kind_proving_grounds)
		{
			continue;
		}
		if (!game_engine_has_teams() || other->team_index == player->team_index)
			rank++;
	}
	return rank;
}

static short pgbots_personality_for(long bot_index, struct player_datum *player)
{
	static short forced = -2;

	if (forced == -2)
		forced = pgbots_rules_personality_named(getenv("HALO_PGBOT_PERSONALITY"));
	if (forced >= 0)
		return forced;
	return pgbots_rules_deal(pgbots_team_rank(bot_index, player), (unsigned long)network_game_get_random_seed() %
		NUMBER_OF_PGBOTS_PERSONALITIES);
}

/* the profile at a spawn: the level now, the personality dealt, the same
jitter for the bot all game (its own seed's) */
static void pgbots_build_profile(struct pgbots_bot *bot, short personality)
{
	unsigned long seed = (0x6C8E9CF5UL * (unsigned long)(pgbots_bot_index(bot) + 3) ^
		(unsigned long)network_game_get_random_seed()) & 0xFFFFFFFFUL;

	pgbots_rules_profile(&bot->profile, bots_bot_skill(pgbots_bot_index(bot)), personality, &seed);
}

static void pgbots_bot_reset(struct pgbots_bot *bot, long bot_index, long player_index)
{
	struct player_datum *player = player_get(player_index);
	short personality;
	long index;

	if (bot->search_slot != NONE)
		bots_common_search_drop(bot->search_slot);
	csmemset(bot, 0, sizeof(*bot));
	bot->player_index = player_index;
	bot->unit_index = NONE;
	bot->search_slot = NONE;
	bot->random = (0x2545F491UL * (unsigned long)(bot_index + 101) ^ (unsigned long)network_game_get_random_seed()) &
		0xFFFFFFFFUL;
	bot->team = game_engine_has_teams() && VALID_INDEX(player->team_index, PGBOTS_TEAMS) ? player->team_index : NONE;
	bot->team_rank = pgbots_team_rank(bot_index, player);
	personality = pgbots_personality_for(bot_index, player);
	pgbots_build_profile(bot, personality);
	bot->target_player_index = NONE;
	bot->hit_attacker = NONE;
	bot->last_killer = NONE;
	bot->goal_object = NONE;
	bot->goal_surface = NONE;
	bot->role = _pgbots_role_free;
	bot->order_surface = NONE;
	bot->roam_area = NONE;
	bot->portal_index = NONE;
	bot->portal_route_tick = -TICKS_PER_SECOND * 60;
	bot->upgrade_object = NONE;
	bot->cover_tick = -TICKS_PER_SECOND;
	bot->upgrade_tick = -TICKS_PER_SECOND;
	bot->strafe_sign = 1.0f;
	bot->aim.target_player_index = NONE;
	bot->aim.head_target = NONE;
	for (index = 0; index < PGBOTS_BELIEFS; index++)
		bot->beliefs[index].player_index = NONE;
	pgbots_drive_reset(bot);
	if (bots_trace_enabled())
		platform_log("pgbots: bot %ld joins: %s, %s (team %d, rank %ld)", bot_index + 1,
			pgbots_rules_personality(personality)->name, "hidden", (int)bot->team, bot->team_rank);
}

/* ---------- beliefs (S6) */

struct pgbots_belief *pgbots_belief(struct pgbots_bot *bot, long player_index)
{
	long index;

	for (index = 0; index < PGBOTS_BELIEFS; index++)
	{
		if (bot->beliefs[index].player_index == player_index)
			return &bot->beliefs[index];
	}
	return NULL;
}

/* a belief's slot for the player: its own, a free one, else the oldest */
static struct pgbots_belief *pgbots_belief_slot(struct pgbots_bot *bot, long player_index)
{
	struct pgbots_belief *belief = pgbots_belief(bot, player_index);
	long index, oldest = 0;

	if (belief)
		return belief;
	for (index = 0; index < PGBOTS_BELIEFS; index++)
	{
		if (bot->beliefs[index].player_index == NONE)
		{
			oldest = index;
			break;
		}
		if (MAX(bot->beliefs[index].tick, bot->beliefs[index].checked_tick) <
			MAX(bot->beliefs[oldest].tick, bot->beliefs[oldest].checked_tick))
		{
			oldest = index;
		}
	}
	belief = &bot->beliefs[oldest];
	csmemset(belief, 0, sizeof(*belief));
	belief->player_index = player_index;
	belief->tick = NONE;
	belief->seen_tick = NONE;
	belief->sighting_tick = NONE;
	belief->recognized_tick = NONE;
	belief->lost_tick = NONE;
	belief->ray_tick = NONE;
	belief->checked_tick = NONE;
	belief->contact_tick = NONE;
	belief->rounds = -1;
	return belief;
}

static boolean pgbots_belief_known(struct pgbots_belief const *belief)
{
	return belief && belief->player_index != NONE && belief->tick != NONE;
}

/* a belief from a source other than the eyes (or the eyes' recognised) */
void pgbots_believe(struct pgbots_bot *bot, long player_index, real_point3d const *point,
	real_vector3d const *velocity, short source, long now)
{
	struct pgbots_belief *belief = pgbots_belief_slot(bot, player_index);
	struct player_datum *other = player_try_and_get(player_index);

	/* (a fresher eye's belief is not overwritten by a vaguer one) */
	if (belief->tick != NONE && belief->source == _pgbots_source_eyes && source != _pgbots_source_eyes &&
		now - belief->tick < SECONDS(0.5f))
	{
		return;
	}
	if (belief->tick == NONE || now - belief->tick > SECONDS(3.0f))
		belief->contact_tick = now;
	belief->position = *point;
	if (velocity)
		belief->velocity = *velocity;
	else
		belief->velocity.i = belief->velocity.j = belief->velocity.k = 0.0f;
	belief->tick = now;
	belief->source = source;
	belief->weak = FALSE;
	if (other && other->unit_index != NONE && source == _pgbots_source_eyes)
		belief->weak = object_get(other->unit_index)->object.shield_vitality <= 0.0f;
	if (source < NUMBER_OF_PGBOTS_SOURCES)
		pgbots_globals.counts[_pgbots_count_beliefs_eyes + source]++;
}

boolean pgbots_target_visible(struct pgbots_bot *bot, long now)
{
	struct pgbots_belief *belief;

	if (bot->target_player_index == NONE)
		return FALSE;
	belief = pgbots_belief(bot, bot->target_player_index);
	return belief && belief->recognized_tick != NONE && belief->seen_tick != NONE &&
		now - belief->seen_tick <= SECONDS(0.25f) + 1;
}

/* the eyes: every third tick (bots in turn), the enemies in view nearest
first, two rays at most (the chest, then the head); the rest wait */
static void pgbots_look_eyes(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *eye, long now)
{
	struct
	{
		long player_index;
		long unit_index;
		real distance;
		real bearing;
		real order;
	} candidates[24];
	long candidate_count = 0, index, rays = 0;
	struct data_iterator iterator;
	struct player_datum *other;
	struct pgbots_hands const *hands = &bot->profile.hands;

	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *object;
		struct pgbots_belief *belief;
		real distance, bearing;

		if (!pgbots_enemy(player, other) || pgbots_unit_dead(other->unit_index))
			continue;
		object = object_get(other->unit_index);
		distance = pgbots_distance(eye, &object->object.bounding_sphere_center);
		bearing = pgbots_angle(pgbots_yaw_to(eye, &object->object.bounding_sphere_center) - bot->yaw);
		belief = pgbots_belief(bot, iterator.datum_index);
		if (!pgbots_rules_in_view(bearing, distance))
		{
			/* (out of view: lost) */
			if (belief && belief->seen_tick != NONE && belief->lost_tick < belief->seen_tick)
				belief->lost_tick = now;
			continue;
		}
		if (candidate_count >= (long)NUMBEROF(candidates))
			continue;
		candidates[candidate_count].player_index = iterator.datum_index;
		candidates[candidate_count].unit_index = other->unit_index;
		candidates[candidate_count].distance = distance;
		candidates[candidate_count].bearing = bearing;
		/* (the target first; those looked at last scan wait behind those not) */
		candidates[candidate_count].order = iterator.datum_index == bot->target_player_index ? -1.0f : distance +
			(belief && belief->checked_tick != NONE && now - belief->checked_tick <= PGBOTS_SCAN_TICKS ? 1000.0f : 0.0f);
		candidate_count++;
	}
	while (candidate_count > 0 && rays < 2)
	{
		long least = 0;
		struct object_datum *object;
		struct unit_datum *unit;
		struct pgbots_belief *belief;
		real_point3d head, chest;
		boolean seen;

		for (index = 1; index < candidate_count; index++)
		{
			if (candidates[index].order < candidates[least].order)
				least = index;
		}
		object = object_get(candidates[least].unit_index);
		unit = unit_get(candidates[least].unit_index);
		belief = pgbots_belief_slot(bot, candidates[least].player_index);
		belief->checked_tick = now;
		chest = object->object.bounding_sphere_center;
		/* (camouflage hides a target past 4 u nine scans in ten) */
		if (TEST_FLAG(unit->unit.flags, _unit_active_camouflaged_bit) && candidates[least].distance > 4.0f &&
			pgbots_random01(bot) < 0.9f)
		{
			seen = FALSE;
		}
		else
		{
			seen = pgbots_sight(player->unit_index, eye, &chest, candidates[least].unit_index);
			rays++;
			if (!seen && rays < 2)
			{
				unit_get_head_position(candidates[least].unit_index, &head);
				seen = pgbots_sight(player->unit_index, eye, &head, candidates[least].unit_index);
				rays++;
			}
			belief->ray_tick = now;
			belief->ray_point = chest;
			belief->ray_clear = seen;
		}
		if (seen)
		{
			real_vector3d velocity;
			boolean newly = belief->seen_tick == NONE || now - belief->seen_tick > SECONDS(1.2f);

			/* (a new sighting: recognised after a moment; within 1.2 s of
			the last, still recognised) */
			if (newly)
			{
				boolean relost = belief->lost_tick != NONE && now - belief->lost_tick < SECONDS(3.0f) &&
					belief->recognized_tick != NONE;

				belief->sighting_tick = now;
				belief->recognized_tick = now + SECONDS(pgbots_rules_recognition_seconds(hands->reaction,
					!pgbots_rules_foveal(candidates[least].bearing), relost, pgbots_gaussian(bot)));
			}
			belief->seen_tick = now;
			pgbots_unit_velocity(candidates[least].unit_index, &velocity);
			if (belief->recognized_tick != NONE && now >= belief->recognized_tick)
			{
				boolean first = belief->tick == NONE || belief->source != _pgbots_source_eyes ||
					now - belief->tick > SECONDS(3.0f);

				pgbots_believe(bot, candidates[least].player_index, &chest, &velocity, _pgbots_source_eyes, now);
				if (first)
					pgbots_director_post(bot, candidates[least].player_index, &chest);
				if (bot->threat_until_tick < now + SECONDS(0.5f))
				{
					bot->threat_point = chest;
					bot->threat_until_tick = now + SECONDS(0.5f + 4.5f * bot->profile.craft.aware);
				}
			}
		}
		else if (belief->seen_tick != NONE && belief->lost_tick < belief->seen_tick)
			belief->lost_tick = now;
		candidate_count--;
		for (index = least; index < candidate_count; index++)
			candidates[index] = candidates[index + 1];
	}
}

/* the motion tracker (when the game shows it) and gunfire */
static void pgbots_look_ears(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *position, long now)
{
	boolean radar = bot->profile.craft.radar > 0.0f && game_engine_hud_draw_motion_sensor(bot->player_index);
	real radar_chance = pgbots_rules_radar_chance(bot->profile.craft.radar, PGBOTS_SCAN_TICKS);
	struct data_iterator iterator;
	struct player_datum *other;

	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *object;
		struct unit_datum *unit;
		real distance, speed;
		long weapon_index;

		if (!pgbots_enemy(player, other) || pgbots_unit_dead(other->unit_index))
			continue;
		object = object_get(other->unit_index);
		unit = unit_get(other->unit_index);
		distance = pgbots_distance(position, &object->object.position);
		if (distance > 30.0f)
			continue;
		/* (the tracker: moving, not crouched, within 20 u) */
		speed = (real)sqrt(object->object.translational_velocity.i * object->object.translational_velocity.i +
			object->object.translational_velocity.j * object->object.translational_velocity.j) * (real)TICKS_PER_SECOND;
		if (radar && distance < 20.0f && speed > 0.6f && !TEST_FLAG(unit->unit.control_flags, _unit_control_crouch_modifier_bit) &&
			pgbots_random01(bot) < radar_chance)
		{
			pgbots_believe(bot, iterator.datum_index, &object->object.position, NULL, _pgbots_source_radar, now);
		}
		/* (gunfire: its magazine dropped since the last scan) */
		weapon_index = bots_common_current_weapon(other->unit_index);
		if (weapon_index != NONE && bot->profile.craft.aware > 0.0f)
		{
			struct pgbots_belief *belief = pgbots_belief(bot, iterator.datum_index);
			short rounds = weapon_get(weapon_index)->weapon.magazines[0].rounds_loaded;

			if (!belief && distance < 30.0f)
				belief = pgbots_belief_slot(bot, iterator.datum_index);
			if (belief)
			{
				if (belief->rounds >= 0 && rounds < belief->rounds && pgbots_random01(bot) < bot->profile.craft.aware)
				{
					real_point3d heard = object->object.position;

					heard.x += pgbots_random01(bot) * 2.0f - 1.0f;
					heard.y += pgbots_random01(bot) * 2.0f - 1.0f;
					pgbots_believe(bot, iterator.datum_index, &heard, NULL, _pgbots_source_heard, now);
				}
				belief->rounds = rounds;
			}
		}
	}
}

/* hurt: who did it (the unit's damage record, as a player's damage arc
shows it), noticed after the reaction; the damage rate over 2 s */
static void pgbots_feel(struct pgbots_bot *bot, struct player_datum *player, struct unit_datum *unit, long now)
{
	real vitality = unit->object.body_vitality + unit->object.shield_vitality;
	real lost = bot->last_vitality - vitality;
	long index;

	if (now - bot->damage_bucket_tick >= SECONDS(0.5f))
	{
		bot->damage_buckets[3] = bot->damage_buckets[2];
		bot->damage_buckets[2] = bot->damage_buckets[1];
		bot->damage_buckets[1] = bot->damage_buckets[0];
		bot->damage_buckets[0] = 0.0f;
		bot->damage_bucket_tick = now;
	}
	if (lost > 0.01f)
	{
		long best = NONE;
		unsigned long best_time = 0;

		bot->damage_buckets[0] += lost;
		bot->hit_tick = now;
		for (index = 0; index < MAXIMUM_ATTACKERS_PER_UNIT; index++)
		{
			struct unit_attacker *attacker = &unit->unit.attackers[index];

			if (attacker->player_index != NONE && attacker->game_time_stamp != (unsigned long)NONE &&
				(best == NONE || attacker->game_time_stamp > best_time))
			{
				best = attacker->player_index;
				best_time = attacker->game_time_stamp;
			}
		}
		if (best != NONE && best != bot->player_index)
		{
			if (bot->hit_attacker != best || !bot->hit_noticed)
			{
				bot->hit_attacker = best;
				bot->hit_noticed = FALSE;
				bot->hit_notice_tick = now + SECONDS(bot->profile.hands.reaction);
			}
		}
	}
	bot->last_vitality = vitality;
	bot->last_shield = unit->object.shield_vitality;
	if (bot->hit_attacker != NONE && !bot->hit_noticed && now >= bot->hit_notice_tick)
	{
		struct player_datum *attacker = player_try_and_get(bot->hit_attacker);

		bot->hit_noticed = TRUE;
		if (attacker && attacker->unit_index != NONE && pgbots_enemy(player, attacker))
		{
			real_vector3d velocity;

			pgbots_unit_velocity(attacker->unit_index, &velocity);
			pgbots_believe(bot, bot->hit_attacker, &object_get(attacker->unit_index)->object.bounding_sphere_center,
				&velocity, _pgbots_source_hit, now);
		}
	}
}

static real pgbots_damage_rate(struct pgbots_bot const *bot)
{
	return (bot->damage_buckets[0] + bot->damage_buckets[1] + bot->damage_buckets[2] + bot->damage_buckets[3]) / 2.0f;
}

/* memory: beliefs older than the level's memory forgotten */
static void pgbots_forget(struct pgbots_bot *bot, long now)
{
	long memory = SECONDS(bot->profile.hands.memory);
	long index;

	for (index = 0; index < PGBOTS_BELIEFS; index++)
	{
		struct pgbots_belief *belief = &bot->beliefs[index];
		struct player_datum *other;

		if (belief->player_index == NONE)
			continue;
		other = player_try_and_get(belief->player_index);
		if (!other || other->unit_index == NONE)
		{
			belief->player_index = NONE;
			continue;
		}
		if (belief->tick != NONE && now - belief->tick > memory)
			belief->tick = NONE;
	}
}

/* ---------- target choice (S9) */

static boolean pgbots_aiming_at(long unit_index, real_point3d const *point, real within)
{
	struct unit_datum *unit = unit_get(unit_index);
	real_point3d eye;
	real_vector3d to;
	real length, dot;

	unit_get_head_position(unit_index, &eye);
	to.i = point->x - eye.x;
	to.j = point->y - eye.y;
	to.k = point->z - eye.z;
	length = (real)sqrt(to.i * to.i + to.j * to.j + to.k * to.k);
	if (length < 0.01f)
		return TRUE;
	dot = (to.i * unit->unit.aiming_vector.i + to.j * unit->unit.aiming_vector.j + to.k * unit->unit.aiming_vector.k) / length;
	return dot > (real)cos(within);
}

static boolean pgbots_power_weapon(long unit_index)
{
	long weapon_index = bots_common_current_weapon(unit_index);
	struct pgbots_weapon_info const *info = pgbots_weapon_of(weapon_index);

	return info && (info->weapon_class == _pgbots_class_explosive || info->weapon_class == _pgbots_class_sniper ||
		info->weapon_class == _pgbots_class_close);
}

long pgbots_choose_target(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *eye)
{
	long now = game_time_get();
	long index, best = NONE, focus = bot->team != NONE ? pgbots_director_focus(bot->team) : NONE;
	real best_threat = 0.0f, current_threat = -1.0f;
	struct pgbots_craft const *craft = &bot->profile.craft;

	for (index = 0; index < PGBOTS_BELIEFS; index++)
	{
		struct pgbots_belief *belief = &bot->beliefs[index];
		struct player_datum *other;
		struct object_datum *object;
		struct pgbots_threat_input input;
		struct pgbots_weapon_info const *info;
		real threat, distance;

		if (belief->player_index == NONE || belief->recognized_tick == NONE || belief->seen_tick == NONE ||
			now - belief->seen_tick > SECONDS(0.25f) + 1 || now < belief->recognized_tick)
		{
			continue;
		}
		other = player_try_and_get(belief->player_index);
		if (!other || pgbots_unit_dead(other->unit_index) || !pgbots_enemy(player, other))
			continue;
		object = object_get(other->unit_index);
		distance = pgbots_distance(eye, &object->object.bounding_sphere_center);
		info = pgbots_weapon_of(bots_common_current_weapon(other->unit_index));
		csmemset(&input, 0, sizeof(input));
		input.power_weapon = pgbots_power_weapon(other->unit_index);
		input.in_band = !info || (distance >= info->low * 0.5f && distance <= info->high * 1.2f);
		input.distance = distance;
		input.shield_down = object->object.shield_vitality <= 0.0f;
		input.aiming_at_us = pgbots_aiming_at(other->unit_index, eye, 0.15f);
		input.aiming_away = !input.aiming_at_us && !pgbots_aiming_at(other->unit_index, eye, 0.7f);
		input.carrier = pgbots_director_carrier(belief->player_index);
		input.last_killer = belief->player_index == bot->last_killer;
		input.shooting_us = bot->hit_attacker == belief->player_index && bot->hit_noticed && now - bot->hit_tick < SECONDS(1.0f);
		input.team_focus = focus == belief->player_index;
		threat = pgbots_rules_threat(&input, craft->tactics, craft->teamwork, bot->profile.traits[_pgbots_trait_grudge]);
		if (belief->player_index == bot->target_player_index)
			current_threat = threat;
		if (best == NONE || threat > best_threat)
		{
			best = belief->player_index;
			best_threat = threat;
		}
	}
	if (best == NONE)
		return bot->target_player_index;
	if (bot->target_player_index != NONE && current_threat >= 0.0f && best != bot->target_player_index)
	{
		struct player_datum *target = player_try_and_get(bot->target_player_index);
		real done = 0.0f, factor;

		if (target && target->unit_index != NONE)
		{
			struct object_datum *object = object_get(target->unit_index);

			done = bot->target_vitality - (object->object.body_vitality + object->object.shield_vitality);
		}
		factor = pgbots_rules_switch_factor(craft->tactics, done, bot->profile.traits[_pgbots_trait_stickiness]);
		if (!pgbots_rules_switch(current_threat, best_threat, factor,
			(real)(now - bot->target_since_tick) / (real)TICKS_PER_SECOND))
		{
			return bot->target_player_index;
		}
		pgbots_globals.counts[_pgbots_count_switches]++;
	}
	if (best != bot->target_player_index)
	{
		struct player_datum *target = player_get(best);
		struct object_datum *object = object_get(target->unit_index);

		bot->target_player_index = best;
		bot->target_since_tick = now;
		bot->target_threat = best_threat;
		bot->target_vitality = object->object.body_vitality + object->object.shield_vitality;
		bot->aim.focus_since_tick = now;
		bot->aim.next_sample_tick = now;
	}
	return best;
}

/* ---------- the hands (S7) */

/* the point aimed at: the head, the chest or the feet, by the weapon's
class and the roll for this target, weapon and shield state */
static void pgbots_aim_point(struct pgbots_bot *bot, long target_unit_index, long weapon_index,
	struct pgbots_weapon_info const *info, real_point3d *point)
{
	struct object_datum *object = object_get(target_unit_index);
	boolean shield_down = object->object.shield_vitality <= 0.0f;
	short weapon_class = info ? info->weapon_class : _pgbots_class_automatic;

	*point = object->object.bounding_sphere_center;
	if (weapon_class == _pgbots_class_explosive)
	{
		real_point3d ground;

		if (pgbots_ground(&object->object.position, &ground) != NONE && object->object.position.z - ground.z < 1.5f)
		{
			*point = ground;
			point->z += 0.1f;
		}
		return;
	}
	if (weapon_class != _pgbots_class_precision && weapon_class != _pgbots_class_sniper)
		return;
	if (bot->aim.head_target != target_unit_index || bot->aim.head_weapon != weapon_index ||
		bot->aim.head_shield != shield_down)
	{
		bot->aim.head_target = target_unit_index;
		bot->aim.head_weapon = weapon_index;
		bot->aim.head_shield = shield_down;
		bot->aim.head = pgbots_rules_aim_head(TRUE, shield_down, pgbots_rules_novice(&bot->profile),
			bot->profile.hands.head_share, pgbots_random01(bot));
		if (bot->aim.head)
			pgbots_globals.counts[_pgbots_count_head_aims]++;
	}
	if (bot->aim.head)
		unit_get_head_position(target_unit_index, point);
}

/* the facing turned toward (yaw, pitch) as the hands do: a flick for a
big turn (Fitts' timing, eased, maybe past), else a pursuit with a lag and
a tremor; never faster than the level's turn */
static void pgbots_turn(struct pgbots_bot *bot, real yaw, real pitch, boolean flicks, long now)
{
	struct pgbots_aim *aim = &bot->aim;
	struct pgbots_hands const *hands = &bot->profile.hands;
	real turn = hands->turn_degrees * DEGREES;
	real dyaw = pgbots_angle(yaw - bot->yaw), dpitch = pitch - bot->pitch;
	real delta = (real)sqrt(dyaw * dyaw + dpitch * dpitch);
	real new_yaw, new_pitch, step, cap = turn * DT;

	if (flicks && aim->flick_ticks <= 0 && pgbots_rules_is_flick(delta))
	{
		real over = pgbots_rules_overshoot(hands->overshoot, pgbots_random01(bot), pgbots_random01(bot));
		real seconds = pgbots_rules_flick_seconds(delta, hands->track, hands->flick, turn);

		aim->flick_from_yaw = bot->yaw;
		aim->flick_from_pitch = bot->pitch;
		aim->flick_to_yaw = bot->yaw + dyaw * (1.0f + over);
		aim->flick_to_pitch = bot->pitch + dpitch * (1.0f + over);
		aim->flick_tick = now;
		aim->flick_ticks = MAX(1, SECONDS(seconds));
		pgbots_globals.counts[_pgbots_count_flicks]++;
		if (over > 0.0f)
			pgbots_globals.counts[_pgbots_count_overshoots]++;
	}
	if (aim->flick_ticks > 0)
	{
		real t = (real)(now - aim->flick_tick + 1) / (real)aim->flick_ticks;
		real eased = pgbots_rules_ease(t);

		new_yaw = aim->flick_from_yaw + (aim->flick_to_yaw - aim->flick_from_yaw) * eased;
		new_pitch = aim->flick_from_pitch + (aim->flick_to_pitch - aim->flick_from_pitch) * eased;
		if (t >= 1.0f)
			aim->flick_ticks = 0;
	}
	else
	{
		new_yaw = bot->yaw + pgbots_rules_pursuit_step(dyaw, DT, hands->lag) + 0.0025f * pgbots_gaussian(bot);
		new_pitch = bot->pitch + pgbots_rules_pursuit_step(dpitch, DT, hands->lag) + 0.0025f * pgbots_gaussian(bot);
	}
	/* (the turn cap) */
	dyaw = pgbots_angle(new_yaw - bot->yaw);
	dpitch = new_pitch - bot->pitch;
	step = (real)sqrt(dyaw * dyaw + dpitch * dpitch);
	if (step > cap && step > 0.0f)
	{
		dyaw *= cap / step;
		dpitch *= cap / step;
	}
	bot->yaw = pgbots_angle(bot->yaw + dyaw);
	bot->pitch = PIN(bot->pitch + dpitch, -1.4f, 1.4f);
}

/* a look (no target): toward a point, at a person's pace, no flick */
static void pgbots_look_at(struct pgbots_bot *bot, real_point3d const *eye, real_point3d const *point, long now)
{
	bot->aim.flick_ticks = 0;
	pgbots_turn(bot, pgbots_yaw_to(eye, point), pgbots_pitch_to(eye, point), FALSE, now);
}

/* a teammate in the line of fire (or the blast's reach of its end) */
static boolean pgbots_teammate_in_line(struct player_datum *player, real_point3d const *eye, real_point3d const *point,
	real blast)
{
	struct data_iterator iterator;
	struct player_datum *other;
	real_vector3d line;
	real length;

	if (!game_engine_has_teams())
		return FALSE;
	line.i = point->x - eye->x;
	line.j = point->y - eye->y;
	line.k = point->z - eye->z;
	length = (real)sqrt(line.i * line.i + line.j * line.j + line.k * line.k);
	if (length < 0.01f)
		return FALSE;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		real_point3d const *at;
		real along, dx, dy, dz, off;

		if (other == player || other->team_index != player->team_index || other->unit_index == NONE)
			continue;
		at = &object_get(other->unit_index)->object.bounding_sphere_center;
		if (blast > 0.0f && pgbots_distance(at, point) < blast + 0.5f)
			return TRUE;
		dx = at->x - eye->x;
		dy = at->y - eye->y;
		dz = at->z - eye->z;
		along = (dx * line.i + dy * line.j + dz * line.k) / length;
		if (along < 0.0f || along > length)
			continue;
		dx -= line.i / length * along;
		dy -= line.j / length * along;
		dz -= line.k / length * along;
		off = (real)sqrt(dx * dx + dy * dy + dz * dz);
		if (off < 0.6f)
			return TRUE;
	}
	return FALSE;
}

boolean pgbots_aim_and_fire(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *eye,
	long weapon_index, real projectile_speed, real window_scale, unsigned long *flags)
{
	long now = game_time_get();
	struct pgbots_aim *aim = &bot->aim;
	struct pgbots_hands const *hands = &bot->profile.hands;
	struct pgbots_belief *belief = pgbots_belief(bot, bot->target_player_index);
	struct player_datum *target = bot->target_player_index != NONE ? player_try_and_get(bot->target_player_index) : NULL;
	struct pgbots_weapon_info const *info = pgbots_weapon_of(weapon_index);
	struct pgbots_fire_input fire;
	real_point3d point, aimed;
	real distance, flight, yaw, pitch, sigma, seen_age, dyaw, dpitch;
	boolean fires;

	if (!target || !belief || pgbots_unit_dead(target->unit_index))
		return FALSE;
	seen_age = belief->seen_tick != NONE ? (real)(now - belief->seen_tick) / (real)TICKS_PER_SECOND : 99.0f;
	if (seen_age > 0.25f + hands->loose_follow + 0.05f)
		return FALSE;
	/* (the target changed: its velocity sampled at once) */
	if (aim->target_player_index != bot->target_player_index)
	{
		aim->target_player_index = bot->target_player_index;
		aim->next_sample_tick = now;
		aim->focus_since_tick = now;
	}
	if (now >= aim->next_sample_tick)
	{
		sigma = pgbots_rules_aim_sigma(hands->aim_error,
			pgbots_rules_focus((real)(now - aim->focus_since_tick) / (real)TICKS_PER_SECOND));
		pgbots_unit_velocity(target->unit_index, &aim->velocity);
		aim->lead = pgbots_rules_lead_scale(hands->lead, pgbots_random01(bot));
		aim->error_yaw = sigma * pgbots_gaussian(bot);
		aim->error_pitch = 0.6f * sigma * pgbots_gaussian(bot);
		aim->next_sample_tick = now + MAX(1, SECONDS(pgbots_rules_sample_seconds(hands->track, pgbots_random01(bot))));
	}
	/* (in sight: where it is; lost a moment ago: where it went) */
	if (seen_age <= 0.25f)
		pgbots_aim_point(bot, target->unit_index, weapon_index, info, &point);
	else
		point = belief->position;
	distance = pgbots_distance(eye, &point);
	flight = projectile_speed > 0.01f ? MIN(distance / projectile_speed, 45.0f) : 0.0f;
	aimed = point;
	aimed.x += aim->velocity.i * (flight + 1.5f) * aim->lead;
	aimed.y += aim->velocity.j * (flight + 1.5f) * aim->lead;
	aimed.z += aim->velocity.k * (flight + 1.5f) * aim->lead;
	/* (a grenade in the hand: the throw's aim, up for its arc) */
	if (now < bot->grenade_aim_until_tick)
	{
		aimed = bot->grenade_point;
		aimed.z += 0.12f * pgbots_distance2d(eye, &aimed);
	}
	yaw = pgbots_yaw_to(eye, &aimed) + aim->error_yaw;
	pitch = pgbots_pitch_to(eye, &aimed) + aim->error_pitch;
	pgbots_turn(bot, yaw, pitch, TRUE, now);
	if (now < bot->grenade_aim_until_tick || !info)
		return FALSE;
	/* the trigger: on target, the line clear (the scan's ray while fresh) */
	csmemset(&fire, 0, sizeof(fire));
	/* (against where the hand meant to aim, its error with it: the error
	makes misses, not waits) */
	dyaw = pgbots_angle(yaw - bot->yaw);
	dpitch = pitch - bot->pitch;
	fire.seen_age = seen_age;
	fire.loose_follow = hands->loose_follow;
	fire.across = (real)fabs(dyaw) * distance / MAX(window_scale, 0.1f);
	fire.vertical = (real)fabs(dpitch) * distance / MAX(window_scale, 0.1f);
	fire.flicking = aim->flick_ticks > 0;
	fire.flick_progress = aim->flick_ticks > 0 ? (real)(now - aim->flick_tick) / (real)aim->flick_ticks : 1.0f;
	fire.loose_early = hands->loose_early;
	fire.distance = distance;
	fire.weapon_class = info->weapon_class;
	/* (fired at whatever distance it sees the target, as Birchwood's bots
	fire: the class's band chooses the weapon, not whether it fires) */
	fire.band_high = 1000.0f;
	fire.blast_radius = info->blast;
	if (now >= bot->next_deterrent_tick && now - bot->hit_tick < SECONDS(0.5f))
	{
		bot->deterrent_until_tick = now + pgbots_random_ticks(bot, 0.2f, 0.35f);
		bot->next_deterrent_tick = now + pgbots_random_ticks(bot, 2.5f, 4.0f);
	}
	fire.deterrent = now < bot->deterrent_until_tick;
	if (seen_age <= 0.25f)
	{
		if (belief->ray_tick != NONE && now - belief->ray_tick < PGBOTS_SCAN_TICKS &&
			pgbots_distance(&belief->ray_point, &point) < 0.5f)
		{
			fire.line_clear = belief->ray_clear;
		}
		else
		{
			fire.line_clear = pgbots_sight(player->unit_index, eye, &point, target->unit_index);
			belief->ray_tick = now;
			belief->ray_point = point;
			belief->ray_clear = fire.line_clear;
		}
	}
	fire.teammate_in_line = pgbots_teammate_in_line(player, eye, &point,
		info->weapon_class == _pgbots_class_explosive ? info->blast : 0.0f);
	fires = pgbots_rules_may_fire(&fire);
	/* the rhythm by class */
	if (!fires)
	{
		/* (a charge held for the overcharge is let go only on target) */
		if (info->weapon_class == _pgbots_class_charge && bot->charge_tick)
		{
			/* (the target lost a while: let go) */
			if (now - bot->charge_tick < SECONDS(2.0f))
				*flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
			else
				bot->charge_tick = 0;
		}
		return FALSE;
	}
	switch (info->weapon_class)
	{
	case _pgbots_class_automatic:
		if (now < bot->burst_until_tick)
			*flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		else if (now >= bot->gap_until_tick)
		{
			real burst, gap;

			pgbots_rules_burst(distance, pgbots_random01(bot), pgbots_random01(bot), &burst, &gap);
			bot->burst_until_tick = now + MAX(1, SECONDS(burst));
			bot->gap_until_tick = bot->burst_until_tick + SECONDS(gap);
			*flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		}
		break;
	case _pgbots_class_charge:
	{
		struct object_datum *object = object_get(target->unit_index);

		/* (a shielded target: charged 0.9 s and let go, then the
		precision weapon: the overcharge combo) */
		if (object->object.shield_vitality > 0.2f)
		{
			if (!bot->charge_tick)
				bot->charge_tick = now;
			if (now - bot->charge_tick < SECONDS(0.9f))
				*flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
			else
			{
				bot->charge_tick = 0;
				bot->combo_until_tick = now + SECONDS(1.2f);
			}
			break;
		}
		bot->charge_tick = 0;
	}
	/* (unshielded: tapped, as a semi-automatic) */
	case _pgbots_class_precision:
	case _pgbots_class_sniper:
	case _pgbots_class_explosive:
	case _pgbots_class_close:
		if (!info->latched && info->weapon_class == _pgbots_class_close)
			*flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		else if (now >= bot->next_press_tick)
		{
			*flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
			bot->next_press_tick = now + MAX(2, SECONDS(pgbots_rules_press_interval(info->rate, distance,
				pgbots_random01(bot))));
			/* (a jump shot) */
			if (pgbots_random01(bot) < pgbots_rules_shot_jump(bot->profile.craft.jump, bot->profile.traits[_pgbots_trait_jumpy]))
				*flags |= FLAG(_unit_control_jump_bit);
		}
		break;
	default:
		break;
	}
	if (TEST_FLAG(*flags, _unit_control_weapon_primary_trigger_bit))
		bot->last_damage_dealt_tick = now;
	return TRUE;
}

/* ---------- walking: a path asked of the shared searches, taken and
followed (S2's replan rules: 1/3 s at least, 4 s at most, at its end, when
stuck) */

static void pgbots_ban(struct pgbots_bot *bot, short goal, long now);

/* a goal the paths do not reach (a dead end, searches that found none): a
place goal let go at once and banned a while (its next decision another);
a fight's or a hunt's, only stood at */
static void pgbots_unreachable(struct pgbots_bot *bot, long now)
{
	if (bot->goal == _pgbots_goal_fight || bot->goal == _pgbots_goal_react || bot->goal == _pgbots_goal_attacker ||
		bot->goal == _pgbots_goal_none)
	{
		return;
	}
	if (bots_trace_enabled())
		platform_log("pgbots: trace bot %ld: goal %d at (%.1f %.1f %.1f) out of reach", pgbots_bot_index(bot) + 1,
			(int)bot->goal, bot->goal_point.x, bot->goal_point.y, bot->goal_point.z);
	/* (an item: left be a minute, the goal itself banned only a little) */
	if (bot->goal_object != NONE)
	{
		long slot, oldest = 0;

		for (slot = 1; slot < 4; slot++)
		{
			if (bot->unreachable_until_ticks[slot] < bot->unreachable_until_ticks[oldest])
				oldest = slot;
		}
		bot->unreachable_objects[oldest] = bot->goal_object;
		bot->unreachable_until_ticks[oldest] = now + SECONDS(60.0f);
		bot->upgrade_tick = -TICKS_PER_SECOND;
		bot->upgrade_object = NONE;
	}
	else
		pgbots_ban(bot, bot->goal, now);
	if (bot->goal == _pgbots_goal_roam)
		bot->roam_area = NONE;
	bot->goal = _pgbots_goal_none;
	bot->decide_tick = now;
	pgbots_globals.counts[_pgbots_count_unreachable]++;
}

void pgbots_path_forget(struct pgbots_bot *bot)
{
	if (bot->search_slot != NONE)
		bots_common_search_drop(bot->search_slot);
	bot->search_slot = NONE;
	bot->path.valid = FALSE;
	bot->path_tick = game_time_get() - TICKS_PER_SECOND;
}

/* on foot, a goal on another piece of the ground (an upper floor a ladder
or a teleporter alone leads to: bots_nav.h): the first of them on the way
gone to, looked for every 2 s and for a new goal; at a ladder's foot,
climbed facing it with a jump now and then up to its top, then onto its
landing; onto a teleporter's entrance. TRUE when this steers the walk now
(*direction); else the walk goes to the entrance (bot->portal_index set) or
the goal */
static boolean pgbots_portal_walk(struct pgbots_bot *bot, long unit_index, real_point3d const *position,
	real_point3d const *goal, long goal_surface, real *direction)
{
	long now = game_time_get();
	boolean near;

	/* (the ladder's top reached: onto its landing a moment) */
	if (bot->portal_top_tick)
	{
		if (now - bot->portal_top_tick < SECONDS(1.5f) && pgbots_distance2d(position, &bot->portal_exit) > 0.4f)
		{
			*direction = pgbots_yaw_to(position, &bot->portal_exit);
			return TRUE;
		}
		bot->portal_top_tick = 0;
		bot->portal_index = NONE;
		bot->portal_route_tick = now - SECONDS(2.0f);
	}
	if (bot->portal_climb_tick)
	{
		if (position->z >= bot->portal_top_z - 0.25f)
		{
			bots_common_ladder_result(bot->portal_index, TRUE);
			bot->portal_climb_tick = 0;
			bot->portal_top_tick = now;
			*direction = pgbots_yaw_to(position, &bot->portal_exit);
			return TRUE;
		}
		if (now - bot->portal_climb_tick > SECONDS(8.0f))
		{
			bots_common_ladder_result(bot->portal_index, FALSE);
			bot->portal_climb_tick = 0;
			bot->portal_index = NONE;
			bot->portal_route_tick = now;
			return FALSE;
		}
		*direction = bot->portal_climb_direction;
		if (((now - bot->portal_climb_tick) % 15) == 0)
			bot->portal_jump_tick = now;
		return TRUE;
	}
	if (pgbots_distance(&bot->portal_goal, goal) > 1.5f || now - bot->portal_route_tick >= SECONDS(2.0f))
	{
		real_point3d start;
		long start_surface = bots_common_unit_surface(unit_index, &start);
		long surface = goal_surface != NONE ? goal_surface : pgbots_ground(goal, NULL);

		bot->portal_goal = *goal;
		bot->portal_route_tick = now;
		bot->portal_near = FALSE;
		bot->portal_index = start_surface != NONE && surface != NONE ? bots_common_portal_route(start_surface, &start,
			surface, goal, &bot->portal_entrance, &bot->portal_surface, &bot->portal_ladder, &bot->portal_climb_direction,
			&bot->portal_top_z, &bot->portal_exit) : NONE;
		if (bot->portal_index != NONE)
			pgbots_globals.counts[_pgbots_count_portals]++;
	}
	if (bot->portal_index == NONE)
		return FALSE;
	near = pgbots_distance2d(position, &bot->portal_entrance) < 0.7f && fabs(position->z - bot->portal_entrance.z) < 1.5f;
	/* (through the teleporter: somewhere else now, the way looked for again) */
	if (!near && bot->portal_near && !bot->portal_ladder && pgbots_distance(position, &bot->portal_entrance) > 4.0f)
	{
		bot->portal_index = NONE;
		bot->portal_near = FALSE;
		bot->portal_route_tick = now - SECONDS(2.0f);
		return FALSE;
	}
	bot->portal_near = near;
	if (!near)
		return FALSE;
	if (bot->portal_ladder)
	{
		bot->portal_climb_tick = now;
		bot->portal_jump_tick = now;
		*direction = bot->portal_climb_direction;
		return TRUE;
	}
	*direction = pgbots_yaw_to(position, &bot->portal_entrance);
	return TRUE;
}

/* the way through a ladder or teleporter let go (dead, in a vehicle, a
tick with no walk on foot): no climb goes on, none counted failed */
static void pgbots_portal_forget(struct pgbots_bot *bot)
{
	bot->portal_index = NONE;
	bot->portal_climb_tick = 0;
	bot->portal_top_tick = 0;
	bot->portal_jump_tick = 0;
	bot->portal_near = FALSE;
	bot->portal_route_tick = game_time_get() - SECONDS(2.0f);
}

boolean pgbots_walk(struct pgbots_bot *bot, long unit_index, real_point3d const *position, real_point3d const *goal,
	long goal_surface, real radius, long search_unit, long target_object, real reached, real *direction)
{
	long now = game_time_get();
	long bot_index = pgbots_bot_index(bot);

	if (pgbots_distance2d(position, goal) < reached && fabs(position->z - goal->z) < 1.5f)
		return FALSE;
	/* (on foot: a ladder or teleporter on the way to other ground) */
	if (search_unit == NONE && radius <= 0.5f)
	{
		/* (a climb its walk left a tick or more ago - a vehicle, another
		goal: let go, not gone on with) */
		if ((bot->portal_climb_tick || bot->portal_top_tick) && now - bot->portal_walk_tick > 1)
			pgbots_portal_forget(bot);
		bot->portal_walk_tick = now;
		if (pgbots_portal_walk(bot, unit_index, position, goal, goal_surface, direction))
			return TRUE;
		if (bot->portal_index != NONE)
		{
			goal = &bot->portal_entrance;
			goal_surface = bot->portal_surface;
			reached = 0.3f;
		}
	}
	/* (another goal: the path let go) */
	if (pgbots_distance(&bot->path_goal, goal) > 1.5f)
	{
		pgbots_path_forget(bot);
		bot->path_goal = *goal;
		bot->path_failures = 0;
		bot->path_tick = now - TICKS_PER_SECOND;
	}
	if (bot->search_slot != NONE)
	{
		struct path_result path;
		boolean found_reached = FALSE, found_waypoint = FALSE;
		long answer = bots_common_search_take(bot->search_slot, bot_index, &path, &found_reached, &found_waypoint);

		if (answer != 0)
		{
			bot->search_slot = NONE;
			bot->path_tick = now;
			if (answer > 0)
			{
				bot->path = path;
				bot->path.step_index = 0;
				bot->path_reached = found_reached;
				bot->path_waypoint = found_waypoint;
				bot->path_failures = 0;
			}
			else
			{
				bot->path.valid = FALSE;
				bot->path_failures++;
			}
		}
	}
	if (bot->search_slot == NONE && now - bot->path_tick >= TICKS_PER_SECOND / 3 &&
		(!bot->path.valid || now - bot->path_tick >= TICKS_PER_SECOND * 4 || bot->path.step_index >= bot->path.step_count))
	{
		real_point3d start;
		long start_surface = search_unit != unit_index && search_unit != NONE ?
			vehicle_find_pathfinding_surface_index(search_unit, &start) : bots_common_unit_surface(unit_index, &start);
		long surface = goal_surface != NONE ? goal_surface : pgbots_ground(goal, NULL);

		if (start_surface != NONE && surface != NONE)
		{
			bot->search_slot = bots_common_search_ask(bot_index, search_unit != NONE ? search_unit : unit_index, radius,
				target_object, &start, start_surface, goal, surface);
		}
		if (bot->search_slot == NONE)
			bot->path_tick = now - TICKS_PER_SECOND / 6;
	}
	/* the path's steps, each reached in turn; past its last, on to the goal */
	if (bot->path.valid)
	{
		real step_reached = radius > 0.5f ? 2.5f : 0.35f;

		while (bot->path.step_index < bot->path.step_count &&
			pgbots_distance2d(position, &bot->path.steps[(long)bot->path.step_index].point) < step_reached)
		{
			bot->path.step_index++;
		}
		if (bot->path.step_index < bot->path.step_count)
		{
			*direction = pgbots_yaw_to(position, &bot->path.steps[(long)bot->path.step_index].point);
			return TRUE;
		}
		/* (its end a waypoint on a long drive: on about it for the next
		search's answer, asked above, not straight on to the goal - the far
		side of a canyon's wall) */
		if (bot->path_waypoint && bot->path.step_count > 0)
		{
			*direction = pgbots_yaw_to(position, &bot->path.steps[bot->path.step_count - 1].point);
			return TRUE;
		}
		/* (its end as near as the paths come, the goal beyond: a dead end,
		not straight on into what is in the way) */
		if (!bot->path_reached && bot->path.steps_finish_path)
		{
			pgbots_unreachable(bot, now);
			return FALSE;
		}
	}
	/* (no way found, twice: out of reach) */
	else if (bot->path_failures >= 2)
	{
		pgbots_unreachable(bot, now);
		return FALSE;
	}
	/* (no path yet, its first search to come, or past its end: straight on;
	a search that failed, a moment's wait for the next) */
	if (bot->path_failures > 0 && !bot->path.valid)
		return FALSE;
	*direction = pgbots_yaw_to(position, goal);
	return TRUE;
}

/* ---------- goals (S8) */

static boolean pgbots_banned(struct pgbots_bot const *bot, short goal, long now)
{
	long index;

	for (index = 0; index < PGBOTS_BANS; index++)
	{
		if (bot->bans[index] == goal && now < bot->ban_until_ticks[index])
			return TRUE;
	}
	return FALSE;
}

static void pgbots_ban(struct pgbots_bot *bot, short goal, long now)
{
	long index, oldest = 0;

	for (index = 0; index < PGBOTS_BANS; index++)
	{
		if (bot->ban_until_ticks[index] < bot->ban_until_ticks[oldest])
			oldest = index;
	}
	bot->bans[oldest] = goal;
	bot->ban_until_ticks[oldest] = now + SECONDS(30.0f);
	pgbots_globals.counts[_pgbots_count_bans]++;
}

/* the freshest unseen belief to hunt (the current one kept unless another
is 4 s fresher; the grudger's last killer first) */
static struct pgbots_belief *pgbots_hunt_belief(struct pgbots_bot *bot, long now)
{
	struct pgbots_belief *best = NULL, *current = NULL;
	long index;

	for (index = 0; index < PGBOTS_BELIEFS; index++)
	{
		struct pgbots_belief *belief = &bot->beliefs[index];

		if (!pgbots_belief_known(belief) || (belief->seen_tick != NONE && now - belief->seen_tick <= SECONDS(0.25f)))
			continue;
		if (bot->goal == _pgbots_goal_hunt && pgbots_distance(&belief->position, &bot->goal_point) < 3.0f)
			current = belief;
		if (bot->profile.traits[_pgbots_trait_grudge] > 0.7f && belief->player_index == bot->last_killer)
			return belief;
		if (!best || belief->tick > best->tick)
			best = belief;
	}
	if (current && best && best->tick - current->tick < SECONDS(4.0f))
		return current;
	return best;
}

/* whether the bot found no way to the item lately */
static boolean pgbots_item_unreachable(struct pgbots_bot const *bot, long object_index)
{
	long slot, now = game_time_get();

	for (slot = 0; slot < 4; slot++)
	{
		if (bot->unreachable_objects[slot] == object_index && now < bot->unreachable_until_ticks[slot])
			return TRUE;
	}
	return FALSE;
}

/* the nearest health pack and best powerup (the reach by greed) */
static void pgbots_items(struct pgbots_bot *bot, real_point3d const *position, long *health, real *health_distance,
	long *powerup, real *powerup_distance, real *powerup_value, short *powerup_kind)
{
	real reach = (pgbots_rules_novice(&bot->profile) ? 8.0f : 30.0f) * (0.5f + bot->profile.traits[_pgbots_trait_greed]);
	long count = bots_common_powerup_count(), index;
	real best_score = -1.0f;

	*health = NONE;
	*powerup = NONE;
	*health_distance = -1.0f;
	*powerup_distance = -1.0f;
	*powerup_value = 0.0f;
	*powerup_kind = 0;
	for (index = 0; index < count; index++)
	{
		long object_index;
		short kind;
		real_point3d point;
		real distance;

		/* (the list is a second old at most: an item taken since is gone) */
		if (!bots_common_powerup(index, &object_index, &kind, &point) || pgbots_item_unreachable(bot, object_index) ||
			!object_try_and_get(object_index) || object_get(object_index)->object.parent_object_index != NONE)
		{
			continue;
		}
		distance = pgbots_distance(position, &point);
		if (kind == 1)
		{
			if (distance < 45.0f && (*health == NONE || distance < *health_distance))
			{
				*health = object_index;
				*health_distance = distance;
			}
			continue;
		}
		/* (camouflage and the overshield looked for within 20 u at least) */
		if (distance < (kind == 2 || kind == 3 ? MAX(reach, 20.0f) : reach))
		{
			real value = pgbots_map_item_power(object_index);
			real score = value - distance / 120.0f;

			if (score > best_score)
			{
				best_score = score;
				*powerup = object_index;
				*powerup_distance = distance;
				*powerup_value = value;
				*powerup_kind = kind;
			}
		}
	}
}

/* a weapon on the ground worth more to the bot than what it holds (S9):
its score's gain, the taste bias; NONE for none */
static long pgbots_weapon_upgrade(struct pgbots_bot *bot, struct unit_datum *unit, real_point3d const *position,
	real *taste)
{
	struct object_iterator iterator;
	real reach = pgbots_rules_novice(&bot->profile) ? 2.0f : 25.0f * (0.5f + bot->profile.traits[_pgbots_trait_greed]);
	real held = -10.0f, best_gain = 1.0f;
	long best = NONE, slot;

	*taste = 0.0f;
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long weapon_index = unit->unit.weapon_object_indices[slot];
		struct pgbots_weapon_info const *info;

		if (weapon_index == NONE || bots_common_weapon_empty(weapon_index))
			continue;
		info = pgbots_weapon_of(weapon_index);
		held = MAX(held, pgbots_rules_weapon_score(info->weapon_class, info->low, info->high, -1.0f, bot->profile.liked,
			bot->profile.disliked));
	}
	object_iterator_new(&iterator, _object_mask_weapon, 0);
	while (object_iterator_next(&iterator))
	{
		struct item_datum *item = item_get(iterator.index);
		struct pgbots_weapon_info const *info;
		real score, distance;
		boolean owned = FALSE;

		if (item->object.parent_object_index != NONE || !TEST_FLAG(item->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(item->item.flags, _item_attached_to_unit_bit) || pgbots_item_unreachable(bot, iterator.index))
		{
			continue;
		}
		distance = pgbots_distance(position, &item->object.position);
		if (distance > reach)
			continue;
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long weapon_index = unit->unit.weapon_object_indices[slot];

			if (weapon_index != NONE && weapon_get(weapon_index)->definition_index == item->definition_index)
				owned = TRUE;
		}
		if (owned)
			continue;
		info = pgbots_weapon_info(item->definition_index);
		if (info->weapon_class == _pgbots_class_objective)
			continue;
		score = pgbots_rules_weapon_score(info->weapon_class, info->low, info->high, -1.0f, bot->profile.liked,
			bot->profile.disliked) - distance / 40.0f;
		if (score - held > best_gain)
		{
			best_gain = score - held;
			best = iterator.index;
			*taste = (bot->profile.liked >> info->weapon_class) & 1 ? 0.3f :
				(bot->profile.disliked >> info->weapon_class) & 1 ? -0.3f : 0.0f;
		}
	}
	return best;
}

static boolean pgbots_teammate_near(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *position)
{
	struct data_iterator iterator;
	struct player_datum *other;

	if (bot->team == NONE)
		return FALSE;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (other != player && other->team_index == player->team_index && other->unit_index != NONE &&
			pgbots_distance(position, &object_get(other->unit_index)->object.position) < 12.0f)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* enemies known near against teammates near: outnumbered */
static boolean pgbots_outnumbered(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *position,
	long now)
{
	long enemies = 0, friends = 1, index;
	struct data_iterator iterator;
	struct player_datum *other;

	for (index = 0; index < PGBOTS_BELIEFS; index++)
	{
		if (pgbots_belief_known(&bot->beliefs[index]) && now - bot->beliefs[index].tick < SECONDS(3.0f) &&
			pgbots_distance(&bot->beliefs[index].position, position) < 25.0f)
		{
			enemies++;
		}
	}
	if (bot->team != NONE)
	{
		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (other != player && other->team_index == player->team_index && other->unit_index != NONE &&
				pgbots_distance(position, &object_get(other->unit_index)->object.position) < 15.0f)
			{
				friends++;
			}
		}
	}
	return enemies > friends;
}

/* the bot's goal decided (every third tick, bots in turn) */
static void pgbots_decide(struct pgbots_bot *bot, struct player_datum *player, struct unit_datum *unit,
	real_point3d const *position, long now)
{
	struct pgbots_goal_input input;
	struct pgbots_belief *hunt, *target_belief = pgbots_belief(bot, bot->target_player_index);
	struct pgbots_craft const *craft = &bot->profile.craft;
	real scores[NUMBER_OF_PGBOTS_GOALS], randoms[NUMBER_OF_PGBOTS_GOALS];
	real_point3d objective_point, hold_point, cover;
	long hold_surface = NONE, cover_surface = NONE, health, powerup, upgrade, vehicle = NONE;
	real health_distance, powerup_distance, powerup_value, taste, vehicle_score = 0.0f;
	short powerup_kind, seat = NONE, goal, previous = bot->goal;
	boolean carrying = FALSE, grab_near = FALSE, unguarded = FALSE, objective;
	real shield = unit->object.shield_vitality, body = unit->object.body_vitality;
	long index;

	csmemset(&input, 0, sizeof(input));
	input.posture = pgbots_director_posture(bot);
	input.target_visible = pgbots_target_visible(bot, now);
	if (target_belief && pgbots_belief_known(target_belief))
		input.target_distance = pgbots_distance(position, &target_belief->position);
	else
		input.target_distance = 1000.0f;
	/* (committed: the target unseen under 6 s, 10 s with its shield down) */
	input.committed = !input.target_visible && target_belief && target_belief->seen_tick != NONE &&
		target_belief->recognized_tick != NONE &&
		now - target_belief->seen_tick < SECONDS(target_belief->weak ? 10.0f : 6.0f) && pgbots_belief_known(target_belief);
	/* (react: a new contact on the tracker, or a hit from close by, in the
	last second - not every tracker blip of an enemy known already, which
	held the bots still, turning, most of a game - and not with a target
	in sight) */
	input.react = FALSE;
	for (index = 0; index < PGBOTS_BELIEFS && !input.target_visible; index++)
	{
		struct pgbots_belief *belief = &bot->beliefs[index];

		if (pgbots_belief_known(belief) && belief->contact_tick != NONE && now - belief->contact_tick < TICKS_PER_SECOND &&
			(belief->source == _pgbots_source_radar ||
				(belief->source == _pgbots_source_hit && pgbots_distance(&belief->position, position) < 3.5f)))
		{
			input.react = TRUE;
		}
	}
	if (now - bot->hit_tick < SECONDS(1.0f) && !input.target_visible)
	{
		if (bot->hit_noticed && bot->hit_attacker != NONE)
			input.attacker_unseen = TRUE;
		else
			input.hit_unknown = TRUE;
	}
	/* (the retreat: cracked, losing, and cover reached before death) */
	if (input.target_visible && !pgbots_rules_novice(&bot->profile) && body < 0.8f &&
		shield < pgbots_rules_cracked(input.posture))
	{
		struct player_datum *target = player_try_and_get(bot->target_player_index);
		real theirs = target && target->unit_index != NONE ? object_get(target->unit_index)->object.body_vitality +
			object_get(target->unit_index)->object.shield_vitality : 0.0f;

		if (body + shield < theirs && now - bot->cover_tick >= TICKS_PER_SECOND)
		{
			bot->cover_tick = now;
			bot->cover_found = pgbots_map_cover(bot, position, &target_belief->position,
				pgbots_rules_retreat_range(craft->tactics), &bot->cover_point, &bot->cover_surface);
		}
		cover = bot->cover_point;
		cover_surface = bot->cover_surface;
		if (body + shield < theirs && bot->cover_found)
		{
			real rate = pgbots_damage_rate(bot);
			real time_to_die = rate > 0.01f ? (body + shield) / rate : 99.0f;

			if (pgbots_distance(position, &cover) / PGBOTS_RUN_SPEED < time_to_die)
				input.retreat_ok = TRUE;
		}
	}
	input.recharging = bot->recharging && shield < 0.99f;
	input.recharge = !input.target_visible && shield < 0.6f && now - bot->fight_tick < SECONDS(5.0f) &&
		!pgbots_rules_novice(&bot->profile);
	hunt = pgbots_hunt_belief(bot, now);
	if (hunt)
	{
		input.hunt_belief = TRUE;
		input.hunt_weak = hunt->weak;
		input.hunt_distance = pgbots_distance(position, &hunt->position);
	}
	input.alone = bot->team == NONE || !pgbots_teammate_near(bot, player, position);
	objective = pgbots_director_objective(bot, position, &objective_point, &carrying, &grab_near, &unguarded);
	input.objective = objective;
	input.carrying = carrying;
	input.grab_near = grab_near;
	input.regrab_urgent = bots_team_ctf_regrab_now(bot->player_index, input.target_visible);
	input.stand_unguarded = unguarded;
	input.runner = bot->role == _pgbots_role_runner || bot->role == _pgbots_role_scorer;
	input.hold = bot->team != NONE && pgbots_map_hold(bot->team, bot->team_rank % 3, &hold_point, &hold_surface);
	input.order = bot->order_accepted && bot->role != _pgbots_role_free;
	input.order_distance = input.order ? pgbots_distance(position, &bot->order_point) : 0.0f;
	pgbots_items(bot, position, &health, &health_distance, &powerup, &powerup_distance, &powerup_value, &powerup_kind);
	input.hurt = 1.0f - body;
	input.health_distance = health != NONE ? health_distance : -1.0f;
	input.powerup_distance = powerup != NONE ? powerup_distance : -1.0f;
	input.powerup_value = powerup_value;
	input.powerup_camouflage = powerup_kind == 3;
	input.powerup_overshield = powerup_kind == 2;
	if (now - bot->upgrade_tick >= TICKS_PER_SECOND || (bot->upgrade_object != NONE &&
		(!object_try_and_get(bot->upgrade_object) || object_get(bot->upgrade_object)->object.parent_object_index != NONE)))
	{
		bot->upgrade_tick = now;
		bot->upgrade_object = pgbots_weapon_upgrade(bot, unit, position, &bot->upgrade_taste);
	}
	upgrade = bot->upgrade_object;
	taste = bot->upgrade_taste;
	{
		long weapon_index = bots_common_current_weapon(player->unit_index);
		long slot;

		input.dry = TRUE;
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long held = unit->unit.weapon_object_indices[slot];

			if (held != NONE && !bots_common_weapon_empty(held))
				input.dry = FALSE;
		}
		input.dry = input.dry && upgrade != NONE;
		(void)weapon_index;
	}
	input.upgrade = upgrade != NONE;
	input.weapon_taste = taste;
	/* (a vehicle for a long trip: the objective's, or the order's) */
	if (bots_common_vehicles_enabled() && !carrying && !input.target_visible)
	{
		real_point3d const *trip_to = objective ? &objective_point : input.order ? &bot->order_point : NULL;

		if (trip_to)
			vehicle_score = pgbots_drive_board_score(bot, position, pgbots_distance(position, trip_to), &vehicle, &seat);
	}
	input.vehicle = vehicle_score;
	input.outnumbered = pgbots_outnumbered(bot, player, position, now);
	input.just_hit = now - bot->hit_tick < SECONDS(1.0f);
	input.stalemate = input.target_visible && now - bot->fight_start_tick > SECONDS(10.0f) &&
		now - bot->hit_tick > SECONDS(10.0f) && now - bot->last_damage_dealt_tick > SECONDS(10.0f);
	input.race = game_engine_get_variant()->game_engine_index == game_engine_race;
	for (goal = 0; goal < NUMBER_OF_PGBOTS_GOALS; goal++)
	{
		if (pgbots_banned(bot, goal, now))
			input.banned |= 1UL << goal;
		randoms[goal] = pgbots_random01(bot);
	}
	pgbots_rules_goal_scores(&input, &bot->profile, bot->profile.personality, scores);
	goal = pgbots_rules_goal_pick(scores, bot->goal, randoms);
	/* (called to a Warthog's crew - bots_team.h: a teammate in its driver's
	seat, the bot near, seeing it and free - whatever it would do: to the
	seat; a passenger's only on a long way of its own) */
	if (bots_common_vehicles_enabled() && !input.target_visible && !carrying)
	{
		short called_seat = NONE;
		long called = bots_team_crew_call(bot->player_index, &called_seat);

		if (called != NONE && bots_common_seat_kind(called, called_seat) == 2 &&
			(!objective || pgbots_distance(position, &objective_point) < 25.0f))
		{
			bots_team_crew_refuse(bot->player_index);
			called = NONE;
		}
		if (called != NONE && bots_common_seat_occupant(called, called_seat) == NONE)
		{
			vehicle = called;
			seat = called_seat;
			goal = _pgbots_goal_vehicle;
			bot->ride.board_vehicle = NONE;
			pgbots_globals.counts[_pgbots_count_crew]++;
		}
	}
	bot->recharging = goal == _pgbots_goal_recharge;
	bot->goal = goal;
	bot->goal_object = NONE;
	bot->goal_surface = NONE;
	switch (goal)
	{
	case _pgbots_goal_fight:
	case _pgbots_goal_react:
	case _pgbots_goal_attacker:
		bot->goal_point = target_belief && pgbots_belief_known(target_belief) ? target_belief->position :
			bot->hit_attacker != NONE && pgbots_belief(bot, bot->hit_attacker) ?
			pgbots_belief(bot, bot->hit_attacker)->position : *position;
		break;
	case _pgbots_goal_retreat:
		bot->goal_point = cover;
		bot->goal_surface = cover_surface;
		if (previous != goal)
			pgbots_globals.counts[_pgbots_count_retreats]++;
		break;
	case _pgbots_goal_recharge:
		if (previous != goal)
		{
			real_point3d threat = bot->threat_point;

			if (!pgbots_map_cover(bot, position, &threat, 10.0f, &bot->goal_point, &bot->goal_surface))
				bot->goal_point = *position;
		}
		break;
	case _pgbots_goal_hunt:
		bot->goal_point = hunt ? hunt->position : *position;
		break;
	case _pgbots_goal_objective:
		bot->goal_point = objective_point;
		break;
	case _pgbots_goal_hold:
		bot->goal_point = hold_point;
		bot->goal_surface = hold_surface;
		break;
	case _pgbots_goal_order:
		bot->goal_point = bot->order_point;
		bot->goal_surface = bot->order_surface;
		break;
	case _pgbots_goal_health:
		bot->goal_object = health;
		bot->goal_point = object_try_and_get(health) ? object_get(health)->object.position : *position;
		break;
	case _pgbots_goal_powerup:
		bot->goal_object = powerup;
		bot->goal_point = object_try_and_get(powerup) ? object_get(powerup)->object.position : *position;
		break;
	case _pgbots_goal_weapon:
		bot->goal_object = upgrade;
		bot->goal_point = object_try_and_get(upgrade) ? object_get(upgrade)->object.position : *position;
		break;
	case _pgbots_goal_vehicle:
		if (bot->ride.board_vehicle != vehicle)
		{
			bot->ride.board_vehicle = vehicle;
			bot->ride.board_seat = seat;
			bot->ride.board_until_tick = now + SECONDS(8.0f);
		}
		bot->goal_object = vehicle;
		bot->goal_point = object_try_and_get(vehicle) ? object_get(vehicle)->object.position : *position;
		break;
	default:
	{
		long area = bot->roam_area;

		if (previous != _pgbots_goal_roam || area == NONE ||
			(pgbots_globals.map.areas && area < pgbots_globals.map.area_count &&
				pgbots_distance2d(position, &pgbots_globals.map.areas[area].center) < 3.0f))
		{
			area = pgbots_map_roam(bot, position);
		}
		bot->roam_area = area;
		if (area != NONE && pgbots_globals.map.areas)
		{
			bot->goal_point = pgbots_globals.map.areas[area].center;
			bot->goal_surface = pgbots_globals.map.areas[area].surface;
		}
		else
			bot->goal_point = pgbots_globals.map.mid;
		break;
	}
	}
	if (goal != previous)
	{
		bot->goal_tick = now;
		bot->headway_tick = now;
		bot->headway_distance = pgbots_distance(position, &bot->goal_point);
		bot->detour_rolled = FALSE;
		bot->detouring = FALSE;
		if (bots_trace_enabled())
			platform_log("pgbots: trace bot %ld goal %d (%.2f) at (%.1f %.1f %.1f)", pgbots_bot_index(bot) + 1, (int)goal,
				scores[goal], bot->goal_point.x, bot->goal_point.y, bot->goal_point.z);
	}
}

/* headway: a walk to a goal that gets no nearer in 6 s bans it 30 s */
static void pgbots_headway(struct pgbots_bot *bot, real_point3d const *position, long now)
{
	real distance;

	if (bot->goal == _pgbots_goal_fight || bot->goal == _pgbots_goal_react || bot->goal == _pgbots_goal_attacker ||
		bot->goal == _pgbots_goal_roam)
	{
		bot->headway_tick = now;
		return;
	}
	distance = pgbots_distance(position, &bot->goal_point);
	if (distance < 2.0f || distance < bot->headway_distance - 1.0f)
	{
		bot->headway_distance = distance;
		bot->headway_tick = now;
		return;
	}
	if (now - bot->headway_tick > SECONDS(6.0f))
	{
		pgbots_ban(bot, bot->goal, now);
		if (bots_trace_enabled())
			platform_log("pgbots: trace bot %ld bans goal %d (no headway)", pgbots_bot_index(bot) + 1, (int)bot->goal);
		bot->goal = _pgbots_goal_none;
		bot->decide_tick = now;
		pgbots_path_forget(bot);
	}
}

/* ---------- fighting on foot (S9) */

/* the weapons: the best held for the range, switched only for a gain over
1.0 and at most every 1.2-2 s (at once when dry); reloads out of a fight;
zoom by range */
static void pgbots_choose_weapon(struct pgbots_bot *bot, struct unit_datum *unit, real distance, boolean fighting,
	struct player_action *action, unsigned long *flags, long now)
{
	long current_slot = unit->unit.current_weapon_index, slot, best_slot = NONE;
	long current = bots_common_current_weapon(bot->unit_index);
	real current_score = -10.0f, best_score = -10.0f;
	boolean dry = current == NONE || bots_common_weapon_empty(current);
	boolean best_precision = FALSE;

	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long weapon_index = unit->unit.weapon_object_indices[slot];
		struct pgbots_weapon_info const *info;
		real score;

		if (weapon_index == NONE || bots_common_weapon_empty(weapon_index))
			continue;
		info = pgbots_weapon_of(weapon_index);
		score = pgbots_rules_weapon_score(info->weapon_class, info->low, info->high, fighting ? distance : -1.0f,
			bot->profile.liked, bot->profile.disliked);
		/* (after an overcharge, the precision weapon at once) */
		if (bot->combo_until_tick > now && info->weapon_class == _pgbots_class_precision && fighting)
			score += 3.0f;
		if (slot == current_slot)
			current_score = score;
		if (score > best_score)
		{
			best_score = score;
			best_slot = slot;
			best_precision = info->weapon_class == _pgbots_class_precision;
		}
	}
	if (best_slot != NONE && best_slot != current_slot &&
		(dry || (bot->combo_until_tick > now && fighting && best_precision) ||
			(now >= bot->weapon_switch_tick && pgbots_rules_weapon_switch(current_score, best_score))))
	{
		action->desired_weapon_index = (short)best_slot;
		bot->combo_until_tick = 0;
		bot->weapon_switch_tick = now + pgbots_random_ticks(bot, 1.2f, 2.0f);
	}
	if (current != NONE && !fighting && bots_common_weapon_loaded(current) < 0.4f && (now & 31) == 0)
		*flags |= FLAG(_unit_control_weapon_reload_bit);
	if (current != NONE && fighting)
	{
		struct pgbots_weapon_info const *info = pgbots_weapon_of(current);
		short zoom = pgbots_rules_zoom(info->weapon_class, info->zoom_levels, distance);

		action->desired_zoom_level = zoom > 0 ? (short)(zoom - 1) : NONE;
	}
}

/* a grenade: the opener (once a fight, 4-25 u), a flush where the target
broke sight 0.5-3 s ago (4-16 u), and steady pressure; 3-7 s apart, never
near teammates; plasma close or at vehicles, frag otherwise */
static void pgbots_grenade(struct pgbots_bot *bot, struct player_datum *player, struct unit_datum *unit,
	real_point3d const *eye, boolean visible, struct player_action *action, unsigned long *flags, long now)
{
	struct pgbots_belief *belief = pgbots_belief(bot, bot->target_player_index);
	struct player_datum *target = bot->target_player_index != NONE ? player_try_and_get(bot->target_player_index) : NULL;
	real grenades = bot->profile.traits[_pgbots_trait_grenades];
	boolean has = unit->unit.grenade_counts[0] > 0 || unit->unit.grenade_counts[1] > 0;
	boolean throw_now = FALSE;
	real_point3d point;
	real distance;
	long kind;

	if (!has || !belief || !target || target->unit_index == NONE || now < bot->next_grenade_tick ||
		bot->profile.skill == _bots_skill_recruit)
	{
		return;
	}
	point = belief->position;
	distance = pgbots_distance(eye, &point);
	if (visible && !bot->opener_rolled && distance >= 4.0f && distance <= 25.0f)
	{
		bot->opener_rolled = TRUE;
		throw_now = pgbots_random01(bot) < pgbots_rules_grenade_opener(grenades, bot->profile.craft.tactics);
	}
	if (!throw_now && !visible && belief->seen_tick != NONE && bot->flush_seen_tick != belief->seen_tick &&
		now - belief->seen_tick >= SECONDS(0.5f) && now - belief->seen_tick <= SECONDS(3.0f) && distance >= 4.0f &&
		distance <= 16.0f)
	{
		bot->flush_seen_tick = belief->seen_tick;
		throw_now = pgbots_random01(bot) < grenades;
	}
	if (!throw_now && visible && distance >= 4.0f && distance <= 25.0f)
		throw_now = pgbots_random01(bot) < grenades * DT;
	if (!throw_now || pgbots_teammate_in_line(player, eye, &point, 4.0f))
		return;
	kind = (distance < 8.0f || bots_common_unit_vehicle(target->unit_index) != NONE) ? 1 : 0;
	if (unit->unit.grenade_counts[kind] <= 0)
		kind = !kind;
	action->desired_grenade_index = (short)kind;
	*flags |= FLAG(_unit_control_throw_grenade_bit);
	bot->grenade_point = point;
	bot->grenade_aim_until_tick = now + SECONDS(0.5f);
	bot->next_grenade_tick = now + pgbots_random_ticks(bot, 3.0f, 7.0f);
	pgbots_globals.counts[_pgbots_count_grenades]++;
}

/* the fight's moves: the strafe (normal and up always; the novices by
their strafe), the juke when hit or (heroic and up) aimed at, jumps on the
flips, the range kept, the push and the press */
static void pgbots_fight_moves(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *position,
	real_point3d const *target_point, long target_unit_index, boolean *moving, real *direction, real *throttle,
	unsigned long *flags, long now)
{
	struct pgbots_craft const *craft = &bot->profile.craft;
	struct pgbots_weapon_info const *info = pgbots_weapon_of(bots_common_current_weapon(bot->unit_index));
	real distance = pgbots_distance(position, target_point);
	real to_target = pgbots_yaw_to(position, target_point);
	real near, far, forward = 0.0f, side = 0.0f;
	boolean juking, novice = pgbots_rules_novice(&bot->profile);
	boolean strafes = !novice || craft->strafe > 0.0f;
	struct object_datum *target = object_get(target_unit_index);
	struct object_datum *self = object_get(bot->unit_index);

	/* (a new fight: the press rolled, the strafe's chance for the novices) */
	if (now - bot->fight_tick > SECONDS(2.0f))
	{
		bot->fight_start_tick = now;
		bot->opener_rolled = FALSE;
		bot->press = pgbots_random01(bot) < bot->profile.hands.press_share;
		if (novice)
			bot->strafe_sign = pgbots_random01(bot) < craft->strafe ? 1.0f : 0.0f;
		else if (bot->strafe_sign == 0.0f)
			bot->strafe_sign = 1.0f;
	}
	bot->fight_tick = now;
	(void)strafes;
	juking = now - bot->hit_tick < SECONDS(0.8f) ||
		(bot->profile.skill >= _bots_skill_heroic && pgbots_aiming_at(target_unit_index,
			&self->object.bounding_sphere_center, 0.15f));
	juking = juking && craft->juke > 0.0f && pgbots_random01(bot) < craft->juke;
	if (now >= bot->strafe_flip_tick && bot->strafe_sign != 0.0f)
	{
		real flip = juking ? 0.85f : 0.8f;

		if (pgbots_random01(bot) < flip)
		{
			bot->strafe_sign = -bot->strafe_sign;
			if (pgbots_random01(bot) < pgbots_rules_flip_jump(craft->jump, bot->profile.traits[_pgbots_trait_jumpy]))
				*flags |= FLAG(_unit_control_jump_bit);
		}
		bot->strafe_flip_tick = now + (juking ? pgbots_random_ticks(bot, 0.3f, 0.55f) : pgbots_random_ticks(bot, 0.45f, 1.0f));
		if (juking && pgbots_random01(bot) < 0.15f * craft->juke)
			bot->crouch_until_tick = now + SECONDS(0.25f);
		if (juking && pgbots_random01(bot) < 0.25f)
			bot->pause_until_tick = now + pgbots_random_ticks(bot, 0.2f, 0.4f);
	}
	side = now < bot->pause_until_tick ? 0.0f : bot->strafe_sign;
	/* the range: Birchwood's distance for the weapon, kept between 0.8 and
	1.3 of it (aggression nearer, caution further) */
	pgbots_rules_keep_range(1.3f * bots_common_weapon_range(bots_common_current_weapon(bot->unit_index)),
		bot->profile.traits[_pgbots_trait_aggression], bot->profile.traits[_pgbots_trait_caution], &near, &far);
	if (distance > far)
		forward = 1.0f;
	else if (distance < near && (!info || info->weapon_class != _pgbots_class_close))
		forward = -0.7f;
	/* the push (normal and up): close in with a close-range gun, or on a
	shieldless target within 8 u; the press: walk in while the trade is even */
	if (!novice && ((info && info->weapon_class == _pgbots_class_close) ||
		(target->object.shield_vitality <= 0.0f && distance < 8.0f)))
	{
		forward = 1.0f;
	}
	if (bot->press && self->object.body_vitality + self->object.shield_vitality >=
		target->object.body_vitality + target->object.shield_vitality && distance > 2.0f)
	{
		forward = MAX(forward, 0.7f);
	}
	if (forward == 0.0f && side == 0.0f)
		return;
	*moving = TRUE;
	*direction = (real)atan2((real)sin(to_target) * forward + (real)sin(to_target + PGBOTS_PI / 2.0f) * side,
		(real)cos(to_target) * forward + (real)cos(to_target + PGBOTS_PI / 2.0f) * side);
	*throttle = 1.0f;
	(void)player;
}

/* a blow: within 1.25 u on the same floor, 0.6-0.9 s apart, never with a
teammate in the swing */
static void pgbots_melee(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *position,
	real_point3d const *target_point, unsigned long *flags, long now)
{
	struct data_iterator iterator;
	struct player_datum *other;

	if (now < bot->melee_tick || pgbots_distance2d(position, target_point) > 1.25f ||
		fabs(position->z - target_point->z) > 0.6f)
	{
		return;
	}
	if (game_engine_has_teams())
	{
		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			real_point3d const *at;

			if (other == player || other->team_index != player->team_index || other->unit_index == NONE)
				continue;
			at = &object_get(other->unit_index)->object.position;
			if (pgbots_distance(at, position) < 1.5f &&
				fabs(pgbots_angle(pgbots_yaw_to(position, at) - bot->yaw)) < 0.8f)
			{
				return;
			}
		}
	}
	*flags |= FLAG(_unit_control_use_equipment_bit);
	bot->melee_tick = now + pgbots_random_ticks(bot, 0.6f, 0.9f);
}

/* ---------- guards: the edge, stuck */

/* the edge guard: a fatal drop (6 u or more) about a unit ahead (2 for a
jump) stops the walk and the jump; in the air, back over the last ground */
static void pgbots_edge_guard(struct pgbots_bot *bot, struct unit_datum *unit, real_point3d const *position,
	boolean *moving, real *direction, unsigned long *flags, long now)
{
	struct biped_datum *biped = biped_try_and_get(bot->unit_index);
	real reach = TEST_FLAG(*flags, _unit_control_jump_bit) ? 2.0f : 1.0f;
	real_point3d ahead, ground;

	(void)unit;
	if (!biped)
		return;
	if (biped->biped.support_surface_index != NONE)
	{
		bot->last_ground = *position;
		bot->airborne_tick = 0;
	}
	else
	{
		if (!bot->airborne_tick)
			bot->airborne_tick = now;
		if (now - bot->airborne_tick > SECONDS(0.3f) && pgbots_distance2d(position, &bot->last_ground) > 0.5f)
		{
			*moving = TRUE;
			*direction = pgbots_yaw_to(position, &bot->last_ground);
		}
		return;
	}
	/* (looked at every third tick, with the decision) */
	if (!*moving || (now + pgbots_bot_index(bot)) % PGBOTS_SCAN_TICKS)
		return;
	ahead = *position;
	ahead.x += (real)cos(*direction) * reach;
	ahead.y += (real)sin(*direction) * reach;
	ahead.z += 0.5f;
	if (bots_common_surface_below(&ahead, &ground) == NONE)
	{
		*moving = FALSE;
		*flags &= ~FLAG(_unit_control_jump_bit);
	}
}

/* stuck: a walk that goes nowhere for a second wiggles and jumps; three
times, the scenery about learnt to be in the way and a new path; five, the
goal banned */
static void pgbots_unstick(struct pgbots_bot *bot, real_point3d const *position, boolean *moving, real *direction,
	unsigned long *flags, long now)
{
	if (now < bot->unstick_until_tick)
	{
		*moving = TRUE;
		*direction = bot->unstick_direction;
		if ((now & 7) == 0)
			*flags |= FLAG(_unit_control_jump_bit);
		return;
	}
	if (now - bot->stuck_tick < TICKS_PER_SECOND)
		return;
	if (*moving && pgbots_distance(position, &bot->stuck_position) < 0.5f)
	{
		bot->stuck_count++;
		pgbots_globals.counts[_pgbots_count_stuck]++;
		bot->unstick_direction = *direction + (pgbots_random01(bot) < 0.5f ? 1.0f : -1.0f) * (0.8f + 0.8f * pgbots_random01(bot));
		bot->unstick_until_tick = now + SECONDS(0.6f);
		*flags |= FLAG(_unit_control_jump_bit);
		if (bot->stuck_count == 3)
		{
			bots_common_paths_learn(position, &bot->goal_point);
			pgbots_path_forget(bot);
		}
		else if (bot->stuck_count >= 5)
		{
			pgbots_ban(bot, bot->goal, now);
			bot->goal = _pgbots_goal_none;
			bot->decide_tick = now;
			bot->stuck_count = 0;
			pgbots_path_forget(bot);
		}
	}
	else if (!*moving || pgbots_distance(position, &bot->stuck_position) > 1.0f)
		bot->stuck_count = 0;
	bot->stuck_position = *position;
	bot->stuck_tick = now;
}

/* the slope of the path a couple of units ahead (up stairs, down ramps), as
a rise a unit across; 0 on the flat or without a path */
static real pgbots_path_slope(struct pgbots_bot const *bot, real_point3d const *eye)
{
	long step;

	if (!bot->path.valid)
		return 0.0f;
	for (step = bot->path.step_index; step < bot->path.step_count; step++)
	{
		real_point3d const *point = &bot->path.steps[step].point;
		real across = pgbots_distance2d(eye, point);

		/* (the eyes about 0.6 over the feet the path's points are at) */
		if (across >= 2.0f || step == bot->path.step_count - 1)
			return across > 0.5f ? PIN((point->z + 0.6f - eye->z) / across, -0.85f, 0.85f) : 0.0f;
	}
	return 0.0f;
}

/* ---------- the look when not fighting: the freshest threat (kept in mind
by awareness), glances to the flanks, else the way ahead */
static void pgbots_look_about(struct pgbots_bot *bot, real_point3d const *eye, boolean moving, real direction,
	boolean carrying, long now)
{
	real_point3d point = *eye;
	real aware = bot->profile.craft.aware;

	if (!carrying && now < bot->threat_until_tick)
	{
		pgbots_look_at(bot, eye, &bot->threat_point, now);
		return;
	}
	if (aware > 0.0f && now >= bot->glance_tick)
	{
		bot->glance_offset = (pgbots_random01(bot) < 0.5f ? 1.0f : -1.0f) * 60.0f * DEGREES;
		bot->glance_until_tick = now + SECONDS(0.5f);
		bot->glance_tick = now + pgbots_random_ticks(bot, 2.0f / aware, 4.0f / aware);
	}
	if (!moving)
	{
		if (now < bot->glance_until_tick)
			direction = bot->yaw + bot->glance_offset * 0.1f;
		else
			return;
	}
	else if (!carrying && now < bot->glance_until_tick)
		direction += bot->glance_offset;
	point.x += (real)cos(direction) * 10.0f;
	point.y += (real)sin(direction) * 10.0f;
	/* (up the stairs it climbs, down those it goes down) */
	if (moving)
		point.z += 10.0f * pgbots_path_slope(bot, eye);
	pgbots_look_at(bot, eye, &point, now);
}

/* ---------- a bot's tick */

void pgbots_bot_update(long bot_index, long player_index, struct player_action *action)
{
	struct pgbots_bot *bot;
	struct player_datum *player = player_get(player_index);
	struct unit_datum *unit;
	struct object_datum *object;
	real_point3d position, eye;
	long now = game_time_get();
	long vehicle_index;
	unsigned long flags = 0;
	boolean moving = FALSE, visible, carrying = FALSE, scan;
	real direction = 0.0f, throttle = 1.0f;
	unsigned long long started = bots_common_profiling() ? bots_time_us() : 0;

	csmemset(action, 0, sizeof(*action));
	action->desired_weapon_index = NONE;
	action->desired_grenade_index = NONE;
	action->desired_zoom_level = NONE;
	if (!pgbots_globals.bots)
		return;
	bot = &pgbots_globals.bots[bot_index];
	if (bot->player_index != player_index)
		pgbots_bot_reset(bot, bot_index, player_index);
	action->desired_facing.yaw = bot->yaw;
	action->desired_facing.pitch = bot->pitch;
	if (player->unit_index == NONE)
	{
		/* (dead: who killed it, its beliefs and path let go) */
		if (bot->unit_index != NONE)
		{
			struct unit_datum *corpse = (struct unit_datum *)object_try_and_get_and_verify_type(bot->unit_index,
				_object_mask_unit);
			long index;
			unsigned long best_time = 0;

			if (corpse)
			{
				for (index = 0; index < MAXIMUM_ATTACKERS_PER_UNIT; index++)
				{
					if (corpse->unit.attackers[index].player_index != NONE &&
						corpse->unit.attackers[index].player_index != player_index &&
						corpse->unit.attackers[index].game_time_stamp != (unsigned long)NONE &&
						corpse->unit.attackers[index].game_time_stamp >= best_time)
					{
						best_time = corpse->unit.attackers[index].game_time_stamp;
						bot->last_killer = corpse->unit.attackers[index].player_index;
					}
				}
				if (bot->team != NONE)
					pgbots_director_death(bot->team, &corpse->object.position);
			}
			for (index = 0; index < PGBOTS_BELIEFS; index++)
				bot->beliefs[index].player_index = NONE;
			pgbots_path_forget(bot);
			pgbots_portal_forget(bot);
			bot->unit_index = NONE;
			bot->target_player_index = NONE;
			bot->goal = _pgbots_goal_none;
			bot->previous_control_flags = 0;
			pgbots_drive_reset(bot);
		}
		return;
	}
	unit = unit_get(player->unit_index);
	object = object_get(player->unit_index);
	if (bot->unit_index != player->unit_index)
	{
		/* (spawned: the profile at the level now; it looks the way it faces) */
		bot->unit_index = player->unit_index;
		pgbots_build_profile(bot, bot->profile.personality);
		bot->yaw = (real)atan2(object->object.forward.j, object->object.forward.i);
		bot->pitch = 0.0f;
		bot->last_vitality = object->object.body_vitality + object->object.shield_vitality;
		bot->last_shield = object->object.shield_vitality;
		bot->spawn_tick = now;
		bot->stuck_position = object->object.position;
		bot->stuck_tick = now;
		bot->stuck_count = 0;
		bot->goal = _pgbots_goal_none;
		bot->decide_tick = now + (bot_index % PGBOTS_SCAN_TICKS);
		bot->hit_attacker = NONE;
		bot->hit_tick = -TICKS_PER_SECOND * 60;
		bot->fight_tick = -TICKS_PER_SECOND * 60;
		bot->last_damage_dealt_tick = now;
		bot->last_ground = object->object.position;
		bot->recharging = FALSE;
		bot->aim.flick_ticks = 0;
		pgbots_path_forget(bot);
		pgbots_drive_reset(bot);
	}
	vehicle_index = bots_common_unit_vehicle(player->unit_index);
	position = vehicle_index != NONE ? object_get(vehicle_index)->object.position : object->object.position;
	unit_get_head_position(player->unit_index, &eye);

	/* perceive: hurt each tick; the eyes, the tracker, gunfire and the
	team's callouts every third tick, bots in turn */
	pgbots_feel(bot, player, unit, now);
	scan = (now + bot_index) % PGBOTS_SCAN_TICKS == 0;
	if (scan)
	{
		pgbots_forget(bot, now);
		pgbots_look_eyes(bot, player, &eye, now);
		pgbots_look_ears(bot, player, &position, now);
		pgbots_director_hear(bot);
	}
	pgbots_choose_target(bot, player, &eye);
	visible = pgbots_target_visible(bot, now);
	pgbots_profile_part(_pgbots_part_look, &started);

	/* riding: pgbots_drive.c's */
	if (vehicle_index != NONE)
	{
		pgbots_drive_update(bot, player, vehicle_index, action, &flags, &moving, &direction, &throttle);
		pgbots_profile_part(_pgbots_part_drive, &started);
	}
	else
	{
		struct pgbots_belief *belief = pgbots_belief(bot, bot->target_player_index);
		long weapon_index = bots_common_current_weapon(player->unit_index);
		struct pgbots_weapon_info const *info = pgbots_weapon_of(weapon_index);
		real target_distance = belief && pgbots_belief_known(belief) ? pgbots_distance(&eye, &belief->position) : 1000.0f;
		boolean fired = FALSE;
		boolean zombie = infection_enabled() && infection_unit_is_zombie(player->unit_index);

		/* decide */
		if (now >= bot->decide_tick)
		{
			bot->decide_tick = now + PGBOTS_SCAN_TICKS;
			pgbots_decide(bot, player, unit, &position, now);
			pgbots_headway(bot, &position, now);
		}
		carrying = info && info->weapon_class == _pgbots_class_objective;
		/* (what the team's crews' calls know of it: bots_team.h) */
		bots_team_bot_state(bot->player_index, (visible && target_distance < 25.0f ? _bots_team_state_fighting : 0) |
			(carrying || bot->role == _pgbots_role_defender || bot->goal == _pgbots_goal_powerup ||
				bot->goal == _pgbots_goal_health || bot->goal == _pgbots_goal_weapon ? _bots_team_state_exempt : 0));
		/* (hiding with the flag, an enemy in sight: the flag let go to fire,
		a press of the trigger dropping it - players.c - taken again once the
		fight is done or before it goes home: bots_team.h) */
		if (carrying && visible && game_engine_get_variant()->game_engine_index == game_engine_ctf &&
			bots_team_ctf_drop(bot->player_index, TRUE, target_distance))
		{
			flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
			carrying = FALSE;
			bot->decide_tick = now;
		}
		pgbots_profile_part(_pgbots_part_decide, &started);

		/* the goal's walk */
		switch (bot->goal)
		{
		case _pgbots_goal_fight:
		case _pgbots_goal_react:
		case _pgbots_goal_attacker:
			if (!visible && bot->goal == _pgbots_goal_fight)
				moving = pgbots_walk(bot, player->unit_index, &position, &bot->goal_point, NONE, 0.25f, NONE, NONE, 1.5f,
					&direction);
			/* (turning to a contact or an unseen shooter: sidestepping as it
			turns, not standing still for a shot) */
			else if (!visible)
			{
				if (now >= bot->strafe_flip_tick)
				{
					bot->strafe_sign = bot->strafe_sign < 0.0f ? 1.0f : -1.0f;
					bot->strafe_flip_tick = now + pgbots_random_ticks(bot, 0.4f, 0.8f);
				}
				moving = TRUE;
				direction = bot->yaw + bot->strafe_sign * PGBOTS_PI / 2.0f;
			}
			break;
		case _pgbots_goal_vehicle:
			moving = pgbots_drive_board(bot, player, &position, &moving, &direction, &flags);
			break;
		case _pgbots_goal_none:
			break;
		default:
		{
			real_point3d const *goal_point = &bot->goal_point;
			long goal_surface = bot->goal_surface;
			real reached = bot->goal_object != NONE ? 0.3f : bot->goal == _pgbots_goal_hold ||
				bot->goal == _pgbots_goal_order ? 1.5f : 1.0f;

			/* (a detour, once a goal, for a trip over 15 u: via an area
			25-75 degrees off the line) */
			if (!bot->detour_rolled && pgbots_distance(&position, goal_point) > 15.0f && pgbots_globals.map.area_count > 0)
			{
				bot->detour_rolled = TRUE;
				if (pgbots_random01(bot) < pgbots_rules_detour_chance(bot->profile.craft.routes,
					bot->profile.traits[_pgbots_trait_route_style]) * (bot->profile.craft.routes > 0.0f ? 1.0f : 0.0f))
				{
					real line = pgbots_yaw_to(&position, goal_point), half = 0.5f * pgbots_distance(&position, goal_point);
					real off = (25.0f + 50.0f * pgbots_random01(bot)) * DEGREES * (pgbots_random01(bot) < 0.5f ? 1.0f : -1.0f);
					real_point3d via = position;
					long area;

					via.x += (real)cos(line + off) * half;
					via.y += (real)sin(line + off) * half;
					area = pgbots_map_area_of(&via);
					if (area != NONE)
					{
						bot->detouring = TRUE;
						bot->detour_point = pgbots_globals.map.areas[area].center;
						bot->detour_surface = pgbots_globals.map.areas[area].surface;
						pgbots_globals.counts[_pgbots_count_detours]++;
					}
				}
			}
			if (bot->detouring && pgbots_distance2d(&position, &bot->detour_point) < 2.5f)
				bot->detouring = FALSE;
			if (bot->detouring)
			{
				goal_point = &bot->detour_point;
				goal_surface = bot->detour_surface;
				reached = 2.5f;
			}
			moving = pgbots_walk(bot, player->unit_index, &position, goal_point, goal_surface, 0.25f, NONE, NONE, reached,
				&direction);
			/* (at a powerup or weapon: onto it; a weapon swapped with the
			action held, as a player does) */
			if (!moving && bot->goal_object != NONE)
			{
				struct object_datum *item = object_try_and_get(bot->goal_object);

				if (item && pgbots_distance2d(&item->object.position, &position) > 0.15f)
				{
					moving = TRUE;
					direction = pgbots_yaw_to(&position, &item->object.position);
				}
				if (bot->goal == _pgbots_goal_weapon && item && (now - bot->pickup_press_tick) % TICKS_PER_SECOND <
					TICKS_PER_SECOND / 2)
				{
					flags |= FLAG(_unit_control_swap_weapons_bit);
				}
			}
			/* (holding a post: crouched if crouchy; at the order's point,
			looking about) */
			if (!moving && (bot->goal == _pgbots_goal_hold || bot->goal == _pgbots_goal_order) &&
				bot->profile.traits[_pgbots_trait_crouchy] > 0.5f)
			{
				flags |= FLAG(_unit_control_crouch_modifier_bit);
			}
			/* (the flag home: straight onto the stand past where the walk
			stops - a capture needs it within a unit, height and all - a jump
			if the stand stands higher) */
			if (bot->goal == _pgbots_goal_objective && carrying &&
				game_engine_get_variant()->game_engine_index == game_engine_ctf)
			{
				real_point3d stand;

				if (bots_team_ctf_carry(bot->player_index, &stand) == _bots_carry_home &&
					pgbots_distance2d(&position, &stand) < 4.0f && (real)fabs(stand.z - position.z) < 2.5f)
				{
					moving = TRUE;
					direction = pgbots_yaw_to(&position, &stand);
					throttle = 1.0f;
					if (pgbots_distance2d(&position, &stand) < 1.5f && stand.z > position.z + 0.4f && (now & 15) == 0)
						flags |= FLAG(_unit_control_jump_bit);
				}
			}
			/* (the ball, the flag: picked up with the action button) */
			if (bot->goal == _pgbots_goal_objective && !carrying && pgbots_distance2d(&position, &bot->goal_point) < 1.2f)
				flags |= FLAG(_unit_control_action_bit);
			break;
		}
		}
		/* travel hops; idle hops */
		if (moving && !visible && pgbots_random01(bot) <
			pgbots_rules_travel_hops(bot->profile.craft.jump, bot->profile.traits[_pgbots_trait_jumpy]) * DT)
		{
			flags |= FLAG(_unit_control_jump_bit);
		}
		else if (!moving && !visible && pgbots_random01(bot) <
			pgbots_rules_idle_hops(bot->profile.craft.jump, bot->profile.traits[_pgbots_trait_jumpy]) * DT)
		{
			flags |= FLAG(_unit_control_jump_bit);
		}

		/* the fight: aimed and fired while it sees its target (or lost it a
		moment ago), whatever the goal; the fight's moves when fighting */
		if (bot->target_player_index != NONE && belief && pgbots_belief_known(belief))
		{
			struct player_datum *target = player_try_and_get(bot->target_player_index);

			if (target && target->unit_index != NONE && !pgbots_unit_dead(target->unit_index))
			{
				/* (an Infection zombie runs straight in and strikes; the oddball
				carrier only strikes) */
				if (zombie)
				{
					pgbots_turn(bot, pgbots_yaw_to(&eye, &belief->position), pgbots_pitch_to(&eye, &belief->position),
						TRUE, now);
					if (pgbots_distance(&position, &belief->position) > 1.0f)
					{
						moving = pgbots_walk(bot, player->unit_index, &position, &belief->position, NONE, 0.25f, NONE, NONE,
							1.0f, &direction);
					}
					if (visible && pgbots_distance(&position, &belief->position) < 1.6f && now >= bot->melee_tick)
					{
						flags |= FLAG(_unit_control_use_equipment_bit);
						bot->melee_tick = now + SECONDS(0.5f);
					}
				}
				else if (carrying && game_engine_get_variant()->game_engine_index == game_engine_oddball)
				{
					if (visible)
						pgbots_turn(bot, pgbots_yaw_to(&eye, &belief->position), pgbots_pitch_to(&eye, &belief->position),
							TRUE, now);
				}
				else
					fired = pgbots_aim_and_fire(bot, player, &eye, weapon_index, weapon_index != NONE ?
						bots_common_projectile_speed(weapon_index) : 0.0f, 1.0f, &flags);
				if (visible && !zombie)
				{
					pgbots_melee(bot, player, &position, &belief->position, &flags, now);
					if (bot->goal == _pgbots_goal_fight)
						pgbots_fight_moves(bot, player, &position, &belief->position, target->unit_index, &moving,
							&direction, &throttle, &flags, now);
					else
						bot->fight_tick = now;
				}
				if (!carrying && !zombie)
					pgbots_grenade(bot, player, unit, &eye, visible, action, &flags, now);
			}
			else if (target && pgbots_unit_dead(target->unit_index) && bot->target_player_index != NONE)
			{
				/* (the kill: a crouch, by crouchiness) */
				if (pgbots_random01(bot) < 0.5f * bot->profile.traits[_pgbots_trait_crouchy])
					bot->crouch_until_tick = now + SECONDS(0.6f);
				bot->target_player_index = NONE;
			}
		}
		if (!fired && (!visible || bot->target_player_index == NONE) &&
			!(bot->target_player_index != NONE && belief && belief->seen_tick != NONE &&
				now - belief->seen_tick <= SECONDS(0.25f + bot->profile.hands.loose_follow)))
		{
			real_point3d look;

			/* (reacting: toward the hit or the contact) */
			if ((bot->goal == _pgbots_goal_react || bot->goal == _pgbots_goal_attacker || bot->goal == _pgbots_goal_hunt) &&
				pgbots_distance(&bot->goal_point, &position) > 0.5f)
			{
				look = bot->goal_point;
				look.z += 0.5f;
				if (bot->goal != _pgbots_goal_hunt || !moving)
					pgbots_look_at(bot, &eye, &look, now);
				else
					pgbots_look_about(bot, &eye, moving, direction, carrying, now);
			}
			else
				pgbots_look_about(bot, &eye, moving, direction, carrying, now);
		}
		if (now < bot->crouch_until_tick)
			flags |= FLAG(_unit_control_crouch_modifier_bit);
		pgbots_choose_weapon(bot, unit, target_distance, visible, action, &flags, now);
		pgbots_profile_part(_pgbots_part_fight, &started);
		/* (a climb its walk left this tick - another goal, a fight that
		stands: let go) */
		if ((bot->portal_climb_tick || bot->portal_top_tick) && bot->portal_walk_tick != now)
			pgbots_portal_forget(bot);
		/* (climbing a ladder: facing it, a jump now and then) */
		if (bot->portal_climb_tick)
		{
			if (!visible)
				pgbots_turn(bot, bot->portal_climb_direction, 0.0f, FALSE, now);
			if (bot->portal_jump_tick == now)
				flags |= FLAG(_unit_control_jump_bit);
		}
		pgbots_edge_guard(bot, unit, &position, &moving, &direction, &flags, now);
		if (!bot->portal_climb_tick)
			pgbots_unstick(bot, &position, &moving, &direction, &flags, now);
	}
	if (bots_common_peaceful())
		flags &= ~(FLAG(_unit_control_weapon_primary_trigger_bit) | FLAG(_unit_control_throw_grenade_bit) |
			FLAG(_unit_control_use_equipment_bit));
	bots_common_write_action(action, bot->yaw, bot->pitch, moving, direction, throttle, flags,
		&bot->previous_control_flags);
	if (bots_trace_enabled() && (now % (TICKS_PER_SECOND * 2)) == (bot_index % (TICKS_PER_SECOND * 2)))
	{
		platform_log("pgbots: trace bot %ld %s at (%.1f %.1f %.1f) goal %d role %d target %ld%s hp %.2f/%.2f",
			bot_index + 1, pgbots_rules_personality(bot->profile.personality)->name, position.x, position.y, position.z,
			(int)bot->goal, (int)bot->role, bot->target_player_index, visible ? " (seen)" : "",
			object->object.body_vitality, object->object.shield_vitality);
	}
	pgbots_profile_part(_pgbots_part_fight, &started);
}

/* ---------- halo.log */

void pgbots_report(long now, unsigned long long us, long bot_ticks)
{
	long const *counts = pgbots_globals.counts;
	long personalities[NUMBER_OF_PGBOTS_PERSONALITIES];
	long index, bots = 0;
	char line[256];
	int length = 0;

	csmemset(personalities, 0, sizeof(personalities));
	if (pgbots_globals.bots)
	{
		for (index = 0; index < BOTS_MAXIMUM; index++)
		{
			if (pgbots_globals.bots[index].player_index != NONE)
			{
				personalities[pgbots_globals.bots[index].profile.personality]++;
				bots++;
			}
		}
	}
	for (index = 0; index < NUMBER_OF_PGBOTS_PERSONALITIES && length < (int)sizeof(line) - 32; index++)
	{
		if (personalities[index])
			length += snprintf(line + length, sizeof(line) - (size_t)length, " %s %ld",
				pgbots_rules_personality((short)index)->name, personalities[index]);
	}
	line[length] = 0;
	/* (each bot's kills and deaths, as Birchwood's line has them) */
	{
		char scores[400];
		int written = 0;

		scores[0] = 0;
		for (index = 0; pgbots_globals.bots && index < BOTS_MAXIMUM && written < (int)sizeof(scores) - 40; index++)
		{
			struct player_datum *player = pgbots_globals.bots[index].player_index != NONE ?
				player_try_and_get(pgbots_globals.bots[index].player_index) : NULL;

			if (player)
				written += snprintf(scores + written, sizeof(scores) - (size_t)written, " %ld:%d/%d", index + 1,
					(int)player->statistics.kills[0], (int)player->statistics.deaths);
		}
		platform_log("pgbots: tick %ld kills/deaths%s; stuck %ld", now, scores, counts[_pgbots_count_stuck]);
	}
	platform_log("pgbots: tick %ld, %ld bots, %.1f us a bot; beliefs eyes %ld hit %ld radar %ld heard %ld callout %ld, "
		"posts %ld; grenades %ld, flicks %ld (overshot %ld), head aims %ld, switches %ld, detours %ld, retreats %ld, "
		"bans %ld (%ld out of reach), boardings %ld, rams %ld, portals %ld, crew calls %ld; rays %.2f a bot tick", now, bots,
		bot_ticks ? (double)us / (double)bot_ticks : 0.0, counts[_pgbots_count_beliefs_eyes],
		counts[_pgbots_count_beliefs_hit], counts[_pgbots_count_beliefs_radar], counts[_pgbots_count_beliefs_heard],
		counts[_pgbots_count_beliefs_callout], counts[_pgbots_count_posts], counts[_pgbots_count_grenades],
		counts[_pgbots_count_flicks], counts[_pgbots_count_overshoots], counts[_pgbots_count_head_aims],
		counts[_pgbots_count_switches], counts[_pgbots_count_detours], counts[_pgbots_count_retreats],
		counts[_pgbots_count_bans], counts[_pgbots_count_unreachable], counts[_pgbots_count_boards],
		counts[_pgbots_count_rams], counts[_pgbots_count_portals], counts[_pgbots_count_crew],
		bot_ticks ? (double)pgbots_globals.rays / (double)bot_ticks : 0.0);
	/* (the personalities dealt: the debug trace only, they are hidden) */
	if (bots_trace_enabled())
		platform_log("pgbots: personalities:%s", line);
	if (bots_common_profiling() && bot_ticks)
	{
		double ticks = (double)bot_ticks;

		platform_log("pgbots: split (us a bot tick): pg_look %.2f pg_decide %.2f pg_fight %.2f pg_drive %.2f "
			"pg_director %.2f pg_map %.2f", pgbots_globals.part_us[_pgbots_part_look] / ticks,
			pgbots_globals.part_us[_pgbots_part_decide] / ticks, pgbots_globals.part_us[_pgbots_part_fight] / ticks,
			pgbots_globals.part_us[_pgbots_part_drive] / ticks, pgbots_globals.part_us[_pgbots_part_director] / ticks,
			pgbots_globals.part_us[_pgbots_part_map] / ticks);
	}
	pgbots_director_report(now);
}
