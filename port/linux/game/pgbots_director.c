/*
PGBOTS_DIRECTOR.C

(port) Each team's director for the Proving Grounds bots (S11 of
research_notes/ppg-bot-personalities.md), and their silent callouts (S6).

- Callouts: a team's blackboard, written and read on the host alone - no
  chat, voice, sound or network traffic. A bot posts a newly recognised
  enemy with its level's callout chance, one post a team every half second,
  the enemy's area (8 units), not where it stands; a teammate takes it in
  after its level's hear delay (teamwork 0: never).
- Posture (0 hide and hold, 1 all in): from the score against the limit, the
  clock and the bots alive on each side.
- The threat map: per area, from the team's beliefs and its deaths, decaying.
- Focus: the enemy most of the team believes in.
- Roles: every 2 s per team (one team a tick, every 60 ticks each), the
  mode's slots (role, point, fitting trait) matched to the cheapest bot
  (distance, a change of role, the fit), each order taken with the bot's
  teamwork as its chance. Slayer (Infection, SWAT): pairs to the hot area
  when bold, anchors on the holds when careful. CTF: defenders, a runner and
  its escort, hunters after their carrier. King: a scorer on the hill (more
  when it is contested), the rest on overlooks. Oddball: escorts for our
  carrier, everyone on theirs, runners to a loose ball. Race: everyone to
  their next checkpoint.

Clean room: none of Project Proving Grounds' code.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/weapons.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "ai/path.h"
#include "tag_files/tag_groups.h"

#include "bots.h"
#include "bots_common.h"
#include "bots_combat.h"
#include "infection.h"
#include "pgbots.h"
#include "bots_team.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

#define SECONDS(s) ((long)((s) * (real)TICKS_PER_SECOND + 0.5f))
#define TEAMS (pgbots_globals.teams)

void pgbots_director_reset(void)
{
	long team;

	for (team = 0; team < PGBOTS_TEAMS; team++)
	{
		if (TEAMS[team].threat)
			free(TEAMS[team].threat);
		csmemset(&TEAMS[team], 0, sizeof(TEAMS[team]));
		TEAMS[team].posture = 0.5f;
		TEAMS[team].focus_player = NONE;
	}
}

static real *director_threat(short team)
{
	if (team < 0 || team >= PGBOTS_TEAMS || pgbots_globals.map.area_count <= 0)
		return NULL;
	/* (the game's malloc, which its free takes: not calloc, the C library's) */
	if (!TEAMS[team].threat)
	{
		TEAMS[team].threat = (real *)malloc(PGBOTS_AREAS * sizeof(real));
		if (TEAMS[team].threat)
			csmemset(TEAMS[team].threat, 0, PGBOTS_AREAS * sizeof(real));
	}
	return TEAMS[team].threat;
}

/* ---------- callouts */

void pgbots_director_post(struct pgbots_bot *bot, long player_index, real_point3d const *point)
{
	struct pgbots_team *team;
	long now = game_time_get(), index, area;
	real_point3d snapped;

	if (bot->team == NONE)
		return;
	team = &TEAMS[bot->team];
	if (now < team->next_post_tick || pgbots_random01(bot) >= bot->profile.hands.callout_chance)
		return;
	/* (the area, not the spot) */
	area = pgbots_map_area_of(point);
	if (area != NONE)
		snapped = pgbots_globals.map.areas[area].center;
	else
	{
		snapped.x = ((real)floor(point->x / PGBOTS_AREA_SIZE) + 0.5f) * PGBOTS_AREA_SIZE;
		snapped.y = ((real)floor(point->y / PGBOTS_AREA_SIZE) + 0.5f) * PGBOTS_AREA_SIZE;
		snapped.z = point->z;
	}
	team->next_post_tick = now + SECONDS(0.5f);
	pgbots_globals.counts[_pgbots_count_posts]++;
	/* (a repeat within 3 s merged) */
	for (index = 0; index < team->board_count; index++)
	{
		if (team->board[index].player_index == player_index && now - team->board[index].tick < SECONDS(3.0f))
		{
			team->board[index].point = snapped;
			team->board[index].tick = now;
			team->board[index].poster = pgbots_bot_index(bot);
			return;
		}
	}
	if (team->board_count < PGBOTS_BOARD)
		index = team->board_count++;
	else
	{
		long oldest = 0;

		for (index = 1; index < PGBOTS_BOARD; index++)
		{
			if (team->board[index].tick < team->board[oldest].tick)
				oldest = index;
		}
		index = oldest;
	}
	team->board[index].player_index = player_index;
	team->board[index].point = snapped;
	team->board[index].tick = now;
	team->board[index].poster = pgbots_bot_index(bot);
}

void pgbots_director_hear(struct pgbots_bot *bot)
{
	struct pgbots_team *team;
	long now = game_time_get(), index;
	long delay;

	if (bot->team == NONE || bot->profile.craft.teamwork <= 0.0f)
		return;
	team = &TEAMS[bot->team];
	delay = SECONDS(bot->profile.hands.hear_delay);
	for (index = 0; index < team->board_count; index++)
	{
		struct pgbots_board_entry *entry = &team->board[index];
		struct pgbots_belief *belief;

		if (entry->poster == pgbots_bot_index(bot) || now - entry->tick < delay ||
			now - entry->tick > SECONDS(bot->profile.hands.memory))
		{
			continue;
		}
		belief = pgbots_belief(bot, entry->player_index);
		if (belief && belief->tick != NONE && belief->tick >= entry->tick)
			continue;
		pgbots_believe(bot, entry->player_index, &entry->point, NULL, _pgbots_source_callout, entry->tick + delay);
	}
}

void pgbots_director_death(short team, real_point3d const *point)
{
	real *threat = director_threat(team);
	long area = pgbots_map_area_of(point);

	if (threat && area != NONE)
		threat[area] += 2.0f;
}

/* ---------- posture and focus */

real pgbots_director_posture(struct pgbots_bot const *bot)
{
	if (bot->team != NONE)
		return TEAMS[bot->team].posture;
	/* (free for all: against the leader, by kills) */
	{
		struct player_datum *player = player_try_and_get(bot->player_index);
		struct data_iterator iterator;
		struct player_datum *other;
		long leader = 0, ours = player ? player->statistics.kills[0] : 0;

		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (other != player && other->statistics.kills[0] > leader)
				leader = other->statistics.kills[0];
		}
		return pgbots_rules_posture(ours, leader, game_engine_get_variant()->universal_variant.score_to_win,
			(real)game_time_get() / (real)TICKS_PER_SECOND, (real)game_variant_options_get()->time_limit * 60.0f, 1, 1, 1);
	}
}

long pgbots_director_focus(short team)
{
	return team >= 0 && team < PGBOTS_TEAMS ? TEAMS[team].focus_player : NONE;
}

/* ---------- objectives */

static short director_mode(void)
{
	return (short)game_engine_get_variant()->game_engine_index;
}

boolean pgbots_director_carrier(long player_index)
{
	long team, index;

	if (player_index == NONE)
		return FALSE;
	if (director_mode() == game_engine_ctf)
	{
		for (team = 0; team < 2; team++)
		{
			real_point3d position, home;
			long carrier;
			boolean at_home;

			if (game_engine_ctf_flag_state(team, &position, &carrier, &at_home, &home) && carrier == player_index)
				return TRUE;
		}
	}
	if (director_mode() == game_engine_oddball)
	{
		for (index = 0; index < MAXIMUM_ODDBALLS; index++)
		{
			if (game_engine_oddball_carrier((short)index) == player_index)
				return TRUE;
		}
	}
	return FALSE;
}

/* the nearest race flag the player may touch next */
static boolean director_race_point(long player_index, real_point3d const *position, real_point3d *point)
{
	struct scenario *scenario = global_scenario_get();
	long index;
	real best = 1e30f;
	boolean found = FALSE;

	if (!scenario)
		return FALSE;
	for (index = 0; index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index,
			struct scenario_netgame_flag);
		real distance;

		if (flag->type != _netgame_flag_race_track || !game_engine_race_can_touch(player_index, flag->team_index))
			continue;
		distance = pgbots_distance(position, &flag->position);
		if (distance < best)
		{
			best = distance;
			*point = flag->position;
			found = TRUE;
		}
	}
	return found;
}

/* a known enemy near a point (the team's beliefs), in the last few seconds */
static long director_enemies_near(struct pgbots_bot *bot, real_point3d const *point, real radius, real seconds)
{
	long index, count = 0, now = game_time_get();

	for (index = 0; index < PGBOTS_BELIEFS; index++)
	{
		struct pgbots_belief *belief = &bot->beliefs[index];

		if (belief->player_index != NONE && belief->tick != NONE && now - belief->tick < SECONDS(seconds) &&
			pgbots_distance(&belief->position, point) < radius)
		{
			count++;
		}
	}
	return count;
}

boolean pgbots_director_objective(struct pgbots_bot *bot, real_point3d const *position, real_point3d *point,
	boolean *carrying, boolean *grab_near, boolean *stand_unguarded)
{
	struct player_datum *player = player_get(bot->player_index);
	short mode = director_mode();

	*carrying = FALSE;
	*grab_near = FALSE;
	*stand_unguarded = FALSE;
	switch (mode)
	{
	case game_engine_ctf:
	{
		real_point3d ours, theirs, our_home, their_home;
		long our_carrier = NONE, their_carrier = NONE;
		boolean our_home_now = TRUE, their_home_now = TRUE;
		short team = bot->team;

		if (team == NONE || !game_engine_ctf_flag_state(team, &ours, &our_carrier, &our_home_now, &our_home) ||
			!game_engine_ctf_flag_state(1 - team, &theirs, &their_carrier, &their_home_now, &their_home))
		{
			return FALSE;
		}
		/* (the team's carrier, bots_team.h: home to score, hiding near home
		while ours is away and captures need it, back to theirs where it let
		it go - the flag lying is its alone; the hiding carrier's guard) */
		switch (bots_team_ctf_carry(bot->player_index, point))
		{
		case _bots_carry_home:
		case _bots_carry_hide:
			*carrying = TRUE;
			return TRUE;
		case _bots_carry_regrab:
			*grab_near = TRUE;
			return TRUE;
		}
		if (bots_team_ctf_escort(bot->player_index, point))
			return TRUE;
		if (their_carrier == bot->player_index)
		{
			*carrying = TRUE;
			*point = our_home;
			return TRUE;
		}
		if (bots_team_ctf_flag_left(bot->player_index) && our_home_now)
		{
			struct player_datum *carrier = player_try_and_get(bots_team_ctf_carrier(team));

			if (carrier && carrier->unit_index != NONE)
			{
				*point = object_get(carrier->unit_index)->object.position;
				return TRUE;
			}
			return FALSE;
		}
		/* (ours away: back to it, or after its carrier) */
		if (!our_home_now)
		{
			*point = ours;
			*grab_near = our_carrier == NONE && pgbots_distance(position, &ours) < 8.0f;
			return TRUE;
		}
		if (their_carrier != NONE)
		{
			struct player_datum *carrier = player_try_and_get(their_carrier);

			/* (a teammate carries theirs: along with it) */
			if (carrier && carrier->unit_index != NONE)
			{
				*point = object_get(carrier->unit_index)->object.position;
				return TRUE;
			}
		}
		*point = theirs;
		*grab_near = pgbots_distance(position, &theirs) < 8.0f;
		*stand_unguarded = their_home_now && director_enemies_near(bot, &theirs, 10.0f, 5.0f) == 0 &&
			bot->profile.craft.map_iq > 0.0f;
		return TRUE;
	}
	case game_engine_king:
	{
		real_point3d points[12], center;
		long count, hill_id;
		real bottom, top;

		if (!game_engine_king_hill(points, 12, &count, &bottom, &top, &center, &hill_id))
			return FALSE;
		*point = center;
		return TRUE;
	}
	case game_engine_oddball:
	{
		long index;

		for (index = 0; index < MAXIMUM_ODDBALLS; index++)
		{
			long ball = game_engine_oddball_ball((short)index);
			long carrier = game_engine_oddball_carrier((short)index);
			struct player_datum *holder = carrier != NONE ? player_try_and_get(carrier) : NULL;

			if (ball == NONE)
				continue;
			if (carrier == bot->player_index)
			{
				long hold, best = NONE;
				real best_distance = -1.0f;

				*carrying = TRUE;
				/* (hide and hold: frozen on its spot; else the hold or mid
				furthest from the enemies seen) */
				if (pgbots_director_posture(bot) <= 0.3f && bot->goal == _pgbots_goal_objective)
				{
					*point = bot->goal_point;
					return TRUE;
				}
				*point = pgbots_globals.map.mid;
				for (hold = 0; hold < 4; hold++)
				{
					real_point3d candidate;
					long surface, enemy;
					real nearest = 1e30f;

					if (hold < 3 ? !pgbots_map_hold(bot->team != NONE ? bot->team : 0, hold, &candidate, &surface) :
						(candidate = pgbots_globals.map.mid, FALSE))
					{
						continue;
					}
					for (enemy = 0; enemy < PGBOTS_BELIEFS; enemy++)
					{
						if (bot->beliefs[enemy].player_index != NONE && bot->beliefs[enemy].tick != NONE)
							nearest = MIN(nearest, pgbots_distance(&bot->beliefs[enemy].position, &candidate));
					}
					if (nearest > best_distance)
					{
						best_distance = nearest;
						best = hold;
						*point = candidate;
					}
				}
				(void)best;
				return TRUE;
			}
			if (holder && holder->unit_index != NONE)
			{
				/* (theirs: after him; ours: not the objective, the escorts' role) */
				if (!pgbots_enemy(player, holder))
					continue;
				*point = object_get(holder->unit_index)->object.position;
				return TRUE;
			}
			item_get_position_even_if_in_inventory(ball, point);
			*grab_near = pgbots_distance(position, point) < 8.0f;
			return TRUE;
		}
		return FALSE;
	}
	case game_engine_race:
		return director_race_point(bot->player_index, position, point);
	}
	return FALSE;
}

/* ---------- roles */

static long director_slot(struct pgbots_team *team, short role, real_point3d const *point, short fit, long object_index)
{
	struct pgbots_slot *slot;

	if (team->slot_count >= PGBOTS_SLOTS)
		return NONE;
	slot = &team->slots[team->slot_count];
	slot->role = role;
	slot->point = *point;
	slot->surface = pgbots_ground(point, NULL);
	slot->fit = fit;
	slot->bot_index = NONE;
	slot->object_index = object_index;
	return team->slot_count++;
}

/* the hottest area of the team's threat map (mid for none) */
static void director_hot(short team, real_point3d *point)
{
	real *threat = director_threat(team);
	long index, best = NONE;

	*point = pgbots_globals.map.mid;
	if (!threat)
		return;
	for (index = 0; index < pgbots_globals.map.area_count; index++)
	{
		if (threat[index] > 0.5f && (best == NONE || threat[index] > threat[best]))
			best = index;
	}
	if (best != NONE)
		*point = pgbots_globals.map.areas[best].center;
}

static void director_slots(short team_index, long bot_count)
{
	struct pgbots_team *team = &TEAMS[team_index];
	short mode = director_mode();
	long now = game_time_get(), index;
	real_point3d hot, point;
	long surface;

	team->slot_count = 0;
	director_hot(team_index, &hot);
	switch (mode)
	{
	case game_engine_slayer:
		if (infection_enabled())
		{
			/* (the humans hold a power position together; the zombies hunt,
			their own rule) */
			if (pgbots_map_hold(team_index, 0, &point, &surface))
			{
				for (index = 0; index < bot_count && index < 4; index++)
					director_slot(team, _pgbots_role_anchor, &point, _pgbots_trait_camping, NONE);
			}
			break;
		}
		if (team->posture >= 0.5f)
		{
			for (index = 0; index < 4 && index < bot_count; index++)
				director_slot(team, _pgbots_role_hunter, &hot, _pgbots_trait_aggression, NONE);
		}
		else
		{
			/* (anchors on the holds, their posts picked again every 25-45 s) */
			if (now >= team->anchor_until_tick)
			{
				team->anchor_posts[0] = (short)((team->anchor_posts[0] + 1) % 3);
				team->anchor_posts[1] = (short)((team->anchor_posts[0] + 1) % 3);
				team->anchor_until_tick = now + SECONDS(25.0f) + (long)((unsigned long)now % (unsigned long)SECONDS(20.0f));
			}
			for (index = 0; index < 2 && index < bot_count; index++)
			{
				if (pgbots_map_hold(team_index, team->anchor_posts[index], &point, &surface))
					director_slot(team, _pgbots_role_anchor, &point, _pgbots_trait_camping, NONE);
			}
		}
		break;
	case game_engine_ctf:
	{
		real_point3d ours, theirs, our_home, their_home;
		long our_carrier, their_carrier;
		boolean our_at_home, their_at_home;

		if (!game_engine_ctf_flag_state(team_index, &ours, &our_carrier, &our_at_home, &our_home) ||
			!game_engine_ctf_flag_state(1 - team_index, &theirs, &their_carrier, &their_at_home, &their_home))
		{
			break;
		}
		/* (defenders on the posts that see our stand) */
		for (index = 0; index < 2 && index < bot_count - 1; index++)
		{
			if (pgbots_map_hold(team_index, index, &point, &surface) && pgbots_distance(&point, &our_home) < 35.0f)
				director_slot(team, _pgbots_role_defender, &point, _pgbots_trait_camping, NONE);
			else
				director_slot(team, _pgbots_role_defender, &our_home, _pgbots_trait_camping, NONE);
		}
		/* (a runner and its escort: theirs taken, ours away; our carrier's,
		bots_team.h, with it - a flag it let go is its alone to take again) */
		{
			struct player_datum *carrier = player_try_and_get(bots_team_ctf_carrier(team_index));
			real_point3d with = theirs;

			if (carrier && carrier->unit_index != NONE)
				with = object_get(carrier->unit_index)->object.position;
			director_slot(team, _pgbots_role_runner, their_carrier != NONE ? &our_home : &with, _pgbots_trait_objective,
				NONE);
			director_slot(team, _pgbots_role_escort, &with, NONE, NONE);
		}
		/* (hunters after their carrier, ahead of him toward his home, or to
		ours lying away) */
		if (our_carrier != NONE || !our_at_home)
		{
			for (index = 0; index < 2; index++)
				director_slot(team, _pgbots_role_hunter, &ours, _pgbots_trait_aggression, NONE);
		}
		else
			director_slot(team, _pgbots_role_hunter, &hot, _pgbots_trait_aggression, NONE);
		break;
	}
	case game_engine_king:
	{
		real_point3d points[12], center;
		long count, hill_id, scorers = 1, seen = 0;
		real bottom, top;

		if (!game_engine_king_hill(points, 12, &count, &bottom, &top, &center, &hill_id))
			break;
		/* (contested in the last 3 s: those seen there and one more, at least
		two, held 4 s) */
		for (index = 0; index < BOTS_MAXIMUM && pgbots_globals.bots; index++)
		{
			struct pgbots_bot *bot = &pgbots_globals.bots[index];

			if (bot->player_index != NONE && bot->team == team_index)
				seen = MAX(seen, director_enemies_near(bot, &center, 4.0f, 3.0f));
		}
		if (seen > 0)
		{
			team->hill_send = MAX(2, seen + 1);
			team->hill_contest_until_tick = now + SECONDS(4.0f);
		}
		if (now < team->hill_contest_until_tick)
			scorers = team->hill_send;
		for (index = 0; index < scorers && index < bot_count; index++)
			director_slot(team, _pgbots_role_scorer, &center, _pgbots_trait_objective, NONE);
		for (index = 0; index < bot_count - scorers && index < 6; index++)
		{
			if (pgbots_map_overlook(&center, index, &point, &surface))
				director_slot(team, _pgbots_role_overlook, &point, _pgbots_trait_camping, NONE);
		}
		break;
	}
	case game_engine_oddball:
	{
		long ball, carrier;
		struct player_datum *holder;

		ball = game_engine_oddball_ball(0);
		carrier = game_engine_oddball_carrier(0);
		holder = carrier != NONE ? player_try_and_get(carrier) : NULL;
		if (ball == NONE)
			break;
		item_get_position_even_if_in_inventory(ball, &point);
		if (holder && holder->team_index == team_index && game_engine_has_teams())
		{
			/* (ours: two escorts trailing, the rest on overlooks) */
			for (index = 0; index < 2; index++)
				director_slot(team, _pgbots_role_escort, &point, NONE, holder->unit_index);
			for (index = 0; index < bot_count - 3 && index < 4; index++)
			{
				real_point3d overlook;

				if (pgbots_map_overlook(&point, index, &overlook, &surface))
					director_slot(team, _pgbots_role_overlook, &overlook, _pgbots_trait_camping, NONE);
			}
		}
		else if (!holder)
		{
			/* (loose: two runners to it, the rest hunt) */
			for (index = 0; index < 2; index++)
				director_slot(team, _pgbots_role_runner, &point, _pgbots_trait_objective, NONE);
		}
		break;
	}
	default:
		break;
	}
}

/* the team's bots matched to its slots: the cheapest bot for each, the order
taken by teamwork */
static void director_assign(short team_index)
{
	struct pgbots_team *team = &TEAMS[team_index];
	long now = game_time_get(), slot_index, index;
	boolean taken[BOTS_MAXIMUM];

	csmemset(taken, 0, sizeof(taken));
	for (slot_index = 0; slot_index < team->slot_count; slot_index++)
	{
		struct pgbots_slot *slot = &team->slots[slot_index];
		long best = NONE;
		real best_cost = 1e30f;

		for (index = 0; index < BOTS_MAXIMUM; index++)
		{
			struct pgbots_bot *bot = &pgbots_globals.bots[index];
			struct player_datum *player;
			real fit, cost;

			if (taken[index] || bot->player_index == NONE || bot->team != team_index || bot->unit_index == NONE)
				continue;
			player = player_try_and_get(bot->player_index);
			if (!player || player->unit_index == NONE)
				continue;
			fit = slot->fit >= 0 ? bot->profile.traits[slot->fit] : bot->profile.craft.teamwork;
			cost = pgbots_rules_slot_cost(pgbots_distance(&object_get(player->unit_index)->object.position, &slot->point),
				bot->role != slot->role, fit);
			if (cost < best_cost)
			{
				best_cost = cost;
				best = index;
			}
		}
		if (best == NONE)
			continue;
		taken[best] = TRUE;
		slot->bot_index = best;
		{
			struct pgbots_bot *bot = &pgbots_globals.bots[best];
			boolean changed = bot->role != slot->role || pgbots_distance(&bot->order_point, &slot->point) > 4.0f;

			if (changed)
			{
				bot->order_accepted = pgbots_random01(bot) < bot->profile.craft.teamwork;
				bot->order_tick = now;
			}
			bot->role = slot->role;
			bot->order_point = slot->point;
			bot->order_surface = slot->surface;
			/* (an escort trails 3 u behind its carrier) */
			if (slot->role == _pgbots_role_escort && slot->object_index != NONE)
			{
				struct object_datum *carrier = object_try_and_get(slot->object_index);

				if (carrier)
				{
					real back = (real)atan2(carrier->object.forward.j, carrier->object.forward.i) + PGBOTS_PI;

					bot->order_point = carrier->object.position;
					bot->order_point.x += (real)cos(back) * 3.0f;
					bot->order_point.y += (real)sin(back) * 3.0f;
					bot->order_surface = NONE;
				}
			}
		}
	}
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct pgbots_bot *bot = &pgbots_globals.bots[index];

		if (!taken[index] && bot->player_index != NONE && bot->team == team_index)
		{
			bot->role = _pgbots_role_free;
			bot->order_accepted = FALSE;
		}
	}
}

/* a team's director: posture, the threat map, focus, the roles */
static void director_team(short team_index)
{
	struct pgbots_team *team = &TEAMS[team_index];
	real *threat = director_threat(team_index);
	struct data_iterator iterator;
	struct player_datum *player;
	long alive[2] = { 0, 0 }, size = 0, index, bot_count = 0, now = game_time_get();
	long focus_counts[PGBOTS_BELIEFS], focus_players[PGBOTS_BELIEFS], focus_count = 0;
	long their = 1 - team_index;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (!VALID_INDEX(player->team_index, 2))
			continue;
		if (player->team_index == team_index)
			size++;
		if (player->unit_index != NONE)
			alive[player->team_index == team_index ? 0 : 1]++;
	}
	team->posture = pgbots_rules_posture(game_engine_get_team_score(team_index), game_engine_get_team_score(their),
		game_engine_get_variant()->universal_variant.score_to_win, (real)now / (real)TICKS_PER_SECOND,
		(real)game_variant_options_get()->time_limit * 60.0f, alive[0], alive[1], size);
	/* (the threat map decays 10% a second; the team's beliefs add to it) */
	if (threat)
	{
		for (index = 0; index < pgbots_globals.map.area_count; index++)
			threat[index] *= 0.81f;
	}
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct pgbots_bot *bot = &pgbots_globals.bots[index];
		long belief_index, slot;

		if (bot->player_index == NONE || bot->team != team_index)
			continue;
		bot_count++;
		for (belief_index = 0; belief_index < PGBOTS_BELIEFS; belief_index++)
		{
			struct pgbots_belief *belief = &bot->beliefs[belief_index];
			long area;

			if (belief->player_index == NONE || belief->tick == NONE || now - belief->tick > SECONDS(3.0f))
				continue;
			area = threat ? pgbots_map_area_of(&belief->position) : NONE;
			if (area != NONE)
				threat[area] += 1.0f;
			for (slot = 0; slot < focus_count && focus_players[slot] != belief->player_index; slot++)
				;
			if (slot == focus_count && focus_count < PGBOTS_BELIEFS)
			{
				focus_players[focus_count] = belief->player_index;
				focus_counts[focus_count++] = 0;
			}
			if (slot < focus_count)
				focus_counts[slot]++;
		}
	}
	team->focus_player = NONE;
	{
		long best = 1;

		for (index = 0; index < focus_count; index++)
		{
			if (focus_counts[index] > best)
			{
				best = focus_counts[index];
				team->focus_player = focus_players[index];
			}
		}
	}
	/* (the blackboard's stale entries dropped) */
	for (index = 0; index < team->board_count;)
	{
		if (now - team->board[index].tick > SECONDS(10.0f))
			team->board[index] = team->board[--team->board_count];
		else
			index++;
	}
	director_slots(team_index, bot_count);
	director_assign(team_index);
}

void pgbots_director_update(void)
{
	long now = game_time_get();
	short team;

	if (!pgbots_globals.bots || !game_engine_has_teams())
		return;
	/* (every 60 ticks a team, one team a tick) */
	for (team = 0; team < PGBOTS_TEAMS; team++)
	{
		if (now - TEAMS[team].director_tick >= 60 && (now % 30) == team * 15)
		{
			TEAMS[team].director_tick = now;
			director_team(team);
			break;
		}
	}
}

void pgbots_director_report(long now)
{
	if (!game_engine_has_teams())
		return;
	platform_log("pgbots: director at tick %ld: red posture %.2f, %ld slots, %ld callouts; blue posture %.2f, %ld slots, "
		"%ld callouts", now, TEAMS[0].posture, TEAMS[0].slot_count, TEAMS[0].board_count, TEAMS[1].posture,
		TEAMS[1].slot_count, TEAMS[1].board_count);
}
