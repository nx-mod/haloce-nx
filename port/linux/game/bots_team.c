/*
BOTS_TEAM.C

(port) What a team of bots of either kind agrees on (bots_team.h):
capture the flag's carrier, and a Warthog's crew.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "items/weapons.h"
#include "scenario/scenario.h"

#include "bots.h"
#include "bots_common.h"
#include "bots_team.h"

#include <math.h>

/* (a flag lying untouched this long goes home: game_engine_ctf.c's 17 s; the
carrier back on it well before) */
#define BOTS_TEAM_REGRAB_SECONDS 9
/* (the carrier, the flag lying, keeps its job while this near it) */
#define BOTS_TEAM_CARRIER_LEASH 15.0f

struct bots_team_ctf
{
	/* the team's carrier of the other team's flag (a player), NONE */
	long carrier;
	/* the tick the other team's flag was last held (NONE: held now, or home) */
	long lying_since_tick;
	/* the player holding it a tick ago (who let it go) */
	long last_holder;
	/* the guard of the hiding carrier, picked again each second */
	long escort;
	long escort_tick;
	/* the hiding place near home, found once a map */
	real_point3d hide;
	boolean hide_found;
	unsigned long hide_generation;
};

/* crews: the Warthogs a team's driver sits in, followed (a call each, its
seats' called bots) */
#define BOTS_TEAM_MAXIMUM_CALLS 8
#define BOTS_TEAM_CREW_SEATS 4
/* (a called bot's way to its seat; the driver's wait from the call) */
#define BOTS_TEAM_CREW_SECONDS 8
#define BOTS_TEAM_CREW_WAIT_SECONDS 6
#define BOTS_TEAM_CREW_REACH 20.0f

struct bots_team_call
{
	long vehicle_index;
	long team_index;
	long tick;
	/* each seat called: its seat, the bot (player), refused by it */
	short seat_count;
	short seats[BOTS_TEAM_CREW_SEATS];
	long crew[BOTS_TEAM_CREW_SEATS];
	boolean bot_driver;
};

static struct
{
	long tick;
	boolean ctf;
	/* each player's state (by absolute index), the tick it was told */
	unsigned long state[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long state_tick[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* a player who refused a call, until when */
	long refused_until_tick[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	struct bots_team_call calls[BOTS_TEAM_MAXIMUM_CALLS];
	long call_tick;
	boolean home_to_score;
	/* each flag's state (by its team) */
	boolean present[2];
	real_point3d position[2];
	real_point3d home[2];
	long holder[2];
	boolean at_home[2];
	struct bots_team_ctf teams[2];
} bots_team_globals;

static real bots_team_distance(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

/* a player alive on the team: its unit's position */
static boolean bots_team_player_position(long player_index, long team_index, real_point3d *position)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct object_datum *object;

	if (!player || player->team_index != team_index || player->unit_index == NONE)
		return FALSE;
	object = object_try_and_get(player->unit_index);
	if (!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return FALSE;
	*position = object->object.position;
	return TRUE;
}

static boolean bots_team_player_is_bot(long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	return player && player->local_player_index == NONE &&
		bots_machine_is_bot(player->network_player_data.machine_index);
}

/* the team's bot alive nearest a point (but one), NONE */
static long bots_team_nearest_bot(long team_index, real_point3d const *point, long except, real *nearest)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long best = NONE;
	real best_distance = 1e30f;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		real_point3d position;
		real distance;

		if (iterator.datum_index == except || !bots_team_player_is_bot(iterator.datum_index) ||
			!bots_team_player_position(iterator.datum_index, team_index, &position))
		{
			continue;
		}
		distance = bots_team_distance(&position, point);
		if (distance < best_distance)
		{
			best_distance = distance;
			best = iterator.datum_index;
		}
	}
	if (nearest)
		*nearest = best_distance;
	return best;
}

/* the hiding place near the team's stand: ground 4-10 units off it, about
level with it, reached from it in a straight line, out of sight of the way
the enemy comes (a point a third of the way to their stand) and far from
it; the stand itself if none */
static void bots_team_find_hide(long team_index)
{
	struct bots_team_ctf *team = &bots_team_globals.teams[team_index];
	real_point3d const *stand = &bots_team_globals.home[team_index];
	real_point3d const *theirs = &bots_team_globals.home[1 - team_index];
	real_point3d approach, stand_eye;
	real best_score = -1e30f;
	static real const radii[] = { 4.0f, 6.0f, 8.0f, 10.0f };
	long angle_index, radius_index;

	team->hide = *stand;
	team->hide_found = TRUE;
	team->hide_generation = bots_common_map_generation();
	approach.x = stand->x + (theirs->x - stand->x) / 3.0f;
	approach.y = stand->y + (theirs->y - stand->y) / 3.0f;
	approach.z = stand->z + (theirs->z - stand->z) / 3.0f + 0.7f;
	stand_eye = *stand;
	stand_eye.z += 0.6f;
	for (angle_index = 0; angle_index < 16; angle_index++)
	{
		real angle = (real)angle_index * (2.0f * 3.14159265f / 16.0f);

		for (radius_index = 0; radius_index < (long)(sizeof(radii) / sizeof(radii[0])); radius_index++)
		{
			real_point3d probe, ground, eye;
			long surface;
			real score;

			probe.x = stand->x + (real)cos(angle) * radii[radius_index];
			probe.y = stand->y + (real)sin(angle) * radii[radius_index];
			probe.z = stand->z + 1.5f;
			surface = bots_common_surface_below(&probe, &ground);
			if (surface == NONE || !bots_common_surface_walkable(surface) || (real)fabs(ground.z - stand->z) > 1.5f)
				continue;
			eye = ground;
			eye.z += 0.6f;
			if (!bots_common_line_of_sight(NONE, &stand_eye, &eye, NONE))
				continue;
			score = bots_team_distance(&ground, theirs) * 0.2f;
			if (!bots_common_line_of_sight(NONE, &approach, &eye, NONE))
				score += 6.0f;
			/* (not on the stand's doorstep either) */
			score -= radii[radius_index] < 5.0f ? 1.0f : 0.0f;
			if (score > best_score)
			{
				best_score = score;
				team->hide = ground;
			}
		}
	}
}

static void bots_team_ctf_update(void)
{
	long now = game_time_get();
	long team_index;

	for (team_index = 0; team_index < 2; team_index++)
	{
		bots_team_globals.holder[team_index] = NONE;
		bots_team_globals.at_home[team_index] = TRUE;
		bots_team_globals.present[team_index] = game_engine_ctf_flag_state(team_index,
			&bots_team_globals.position[team_index], &bots_team_globals.holder[team_index],
			&bots_team_globals.at_home[team_index], &bots_team_globals.home[team_index]);
	}
	for (team_index = 0; team_index < 2; team_index++)
	{
		struct bots_team_ctf *team = &bots_team_globals.teams[team_index];
		long flag = 1 - team_index;
		long holder = bots_team_globals.holder[flag];
		struct player_datum *holder_player = holder != NONE ? player_try_and_get(holder) : NULL;
		real_point3d position;

		if (!bots_team_globals.present[flag] || !bots_team_globals.present[team_index])
		{
			team->carrier = NONE;
			team->lying_since_tick = NONE;
			team->last_holder = NONE;
			continue;
		}
		if (holder_player && holder_player->team_index == team_index)
		{
			team->carrier = holder;
			team->last_holder = holder;
			team->lying_since_tick = NONE;
		}
		else if (holder != NONE || bots_team_globals.at_home[flag])
		{
			/* (theirs back home, or an enemy holds it: no carrier) */
			team->carrier = NONE;
			team->last_holder = NONE;
			team->lying_since_tick = NONE;
		}
		else
		{
			/* lying: who let it go, while alive and near it, else the nearest bot */
			if (team->lying_since_tick == NONE)
				team->lying_since_tick = now;
			if (team->carrier == NONE)
				team->carrier = team->last_holder;
			if (team->carrier != NONE &&
				(!bots_team_player_position(team->carrier, team_index, &position) ||
					bots_team_distance(&position, &bots_team_globals.position[flag]) > BOTS_TEAM_CARRIER_LEASH))
			{
				team->carrier = NONE;
			}
			if (team->carrier == NONE)
				team->carrier = bots_team_nearest_bot(team_index, &bots_team_globals.position[flag], NONE, NULL);
			team->last_holder = team->carrier;
		}
		/* the hiding carrier's guard: the team's bot nearest the hiding place */
		if (team->carrier != NONE && bots_team_globals.home_to_score && !bots_team_globals.at_home[team_index])
		{
			if (!team->hide_found || team->hide_generation != bots_common_map_generation())
				bots_team_find_hide(team_index);
			if (now - team->escort_tick >= TICKS_PER_SECOND || team->escort == team->carrier)
			{
				team->escort_tick = now;
				team->escort = bots_team_nearest_bot(team_index, &team->hide, team->carrier, NULL);
			}
		}
		else
			team->escort = NONE;
	}
}

void bots_team_reset(void)
{
	long team_index;

	csmemset(&bots_team_globals, 0, sizeof(bots_team_globals));
	bots_team_globals.tick = NONE;
	for (team_index = 0; team_index < BOTS_TEAM_MAXIMUM_CALLS; team_index++)
		bots_team_globals.calls[team_index].vehicle_index = NONE;
	for (team_index = 0; team_index < 2; team_index++)
	{
		bots_team_globals.teams[team_index].carrier = NONE;
		bots_team_globals.teams[team_index].lying_since_tick = NONE;
		bots_team_globals.teams[team_index].last_holder = NONE;
		bots_team_globals.teams[team_index].escort = NONE;
	}
}

static void bots_team_crew_update(void);

void bots_team_update(void)
{
	long now = game_time_get();

	if (bots_team_globals.tick == now)
		return;
	bots_team_globals.tick = now;
	/* (the crews' calls, four times a second) */
	if (now - bots_team_globals.call_tick >= TICKS_PER_SECOND / 4 || now < bots_team_globals.call_tick)
	{
		bots_team_globals.call_tick = now;
		bots_team_crew_update();
	}
	bots_team_globals.ctf = game_engine_running() &&
		game_engine_get_variant()->game_engine_index == game_engine_ctf;
	if (bots_team_globals.ctf)
	{
		bots_team_globals.home_to_score = game_engine_ctf_home_to_score();
		bots_team_ctf_update();
	}
}

/* the player's team, 0 or 1, NONE */
static long bots_team_of(long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	if (!player || (player->team_index != 0 && player->team_index != 1))
		return NONE;
	return player->team_index;
}

long bots_team_ctf_carrier(long team_index)
{
	if (!bots_team_globals.ctf || (team_index != 0 && team_index != 1))
		return NONE;
	return bots_team_globals.teams[team_index].carrier;
}

short bots_team_ctf_carry(long player_index, real_point3d *point)
{
	long team_index = bots_team_of(player_index);
	struct bots_team_ctf *team;

	if (!bots_team_globals.ctf || team_index == NONE)
		return _bots_carry_none;
	team = &bots_team_globals.teams[team_index];
	if (team->carrier != player_index)
		return _bots_carry_none;
	if (bots_team_globals.holder[1 - team_index] != player_index)
	{
		*point = bots_team_globals.position[1 - team_index];
		return _bots_carry_regrab;
	}
	if (!bots_team_globals.home_to_score || bots_team_globals.at_home[team_index])
	{
		*point = bots_team_globals.home[team_index];
		return _bots_carry_home;
	}
	if (!team->hide_found || team->hide_generation != bots_common_map_generation())
		bots_team_find_hide(team_index);
	*point = team->hide;
	return _bots_carry_hide;
}

boolean bots_team_ctf_drop(long player_index, boolean enemy_visible, real enemy_distance)
{
	long team_index = bots_team_of(player_index);
	real_point3d point, position;

	if (!enemy_visible || enemy_distance > 20.0f || team_index == NONE)
		return FALSE;
	if (bots_team_ctf_carry(player_index, &point) != _bots_carry_hide)
		return FALSE;
	/* (settled in its hiding place, or nearly) */
	return bots_team_player_position(player_index, team_index, &position) &&
		bots_team_distance(&position, &point) < 4.0f;
}

boolean bots_team_ctf_regrab_now(long player_index, boolean fighting)
{
	long team_index = bots_team_of(player_index);
	struct bots_team_ctf *team;
	real_point3d point;

	if (team_index == NONE || bots_team_ctf_carry(player_index, &point) != _bots_carry_regrab)
		return FALSE;
	team = &bots_team_globals.teams[team_index];
	if (!fighting)
		return TRUE;
	return team->lying_since_tick != NONE &&
		game_time_get() - team->lying_since_tick >= BOTS_TEAM_REGRAB_SECONDS * TICKS_PER_SECOND;
}

boolean bots_team_ctf_flag_left(long player_index)
{
	long team_index = bots_team_of(player_index);
	struct bots_team_ctf *team;

	if (!bots_team_globals.ctf || team_index == NONE)
		return FALSE;
	team = &bots_team_globals.teams[team_index];
	return team->carrier != NONE && team->carrier != player_index &&
		bots_team_globals.holder[1 - team_index] == NONE && !bots_team_globals.at_home[1 - team_index];
}

boolean bots_team_ctf_escort(long player_index, real_point3d *point)
{
	long team_index = bots_team_of(player_index);
	struct bots_team_ctf *team;
	real_point3d position;

	if (!bots_team_globals.ctf || team_index == NONE)
		return FALSE;
	team = &bots_team_globals.teams[team_index];
	if (team->escort != player_index || team->carrier == NONE)
		return FALSE;
	/* (by the carrier where it is, else its hiding place) */
	if (bots_team_player_position(team->carrier, team_index, &position) &&
		bots_team_distance(&position, &team->hide) < 6.0f)
	{
		*point = position;
	}
	else
		*point = team->hide;
	return TRUE;
}

/* ---------- crews */

static long bots_team_absolute(long player_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

	return absolute >= 0 && absolute < HALO_PORT_MAXIMUM_NETWORK_PLAYERS ? absolute : NONE;
}

void bots_team_bot_state(long player_index, unsigned long state)
{
	long absolute = player_index != NONE ? bots_team_absolute(player_index) : NONE;

	if (absolute == NONE)
		return;
	bots_team_globals.state[absolute] = state;
	bots_team_globals.state_tick[absolute] = game_time_get();
}

/* the unit's vehicle and seat, NONE */
static long bots_team_unit_vehicle(long unit_index, short *seat_index)
{
	struct object_datum *object = object_try_and_get(unit_index);

	if (!object || object->object.parent_object_index == NONE ||
		!object_try_and_get_and_verify_type(object->object.parent_object_index, _object_mask_vehicle))
	{
		return NONE;
	}
	*seat_index = unit_get(unit_index)->unit.parent_seat_index;
	return object->object.parent_object_index;
}

/* a bot free for a call to that vehicle: on foot, alive, its kind's word
that it neither fights nor is exempt (told this second), not refusing, near
the vehicle and seeing it */
static boolean bots_team_crew_free(long player_index, long team_index, long vehicle_index, real *distance)
{
	long absolute = bots_team_absolute(player_index);
	struct player_datum *player = player_try_and_get(player_index);
	struct object_datum *vehicle = object_get(vehicle_index);
	real_point3d position, eye, center;
	short seat;
	long now = game_time_get();

	if (absolute == NONE || !player || !bots_team_player_is_bot(player_index) ||
		!bots_team_player_position(player_index, team_index, &position) ||
		bots_team_unit_vehicle(player->unit_index, &seat) != NONE ||
		now - bots_team_globals.state_tick[absolute] > TICKS_PER_SECOND ||
		bots_team_globals.state[absolute] != 0 || now < bots_team_globals.refused_until_tick[absolute])
	{
		return FALSE;
	}
	*distance = bots_team_distance(&position, &vehicle->object.position);
	if (*distance > BOTS_TEAM_CREW_REACH)
		return FALSE;
	unit_get_head_position(player->unit_index, &eye);
	center = vehicle->object.bounding_sphere_center;
	return bots_common_line_of_sight(player->unit_index, &eye, &center, vehicle_index) ||
		*distance < 4.0f;
}

static boolean bots_team_called(long player_index)
{
	long call, seat;

	for (call = 0; call < BOTS_TEAM_MAXIMUM_CALLS; call++)
	{
		for (seat = 0; bots_team_globals.calls[call].vehicle_index != NONE &&
			seat < bots_team_globals.calls[call].seat_count; seat++)
		{
			if (bots_team_globals.calls[call].crew[seat] == player_index)
				return TRUE;
		}
	}
	return FALSE;
}

static void bots_team_crew_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long now = game_time_get();
	long call;

	/* the calls kept: the driver still in its seat; a called bot that sat
	down, got busy, refused, died or was too long on its way let go */
	for (call = 0; call < BOTS_TEAM_MAXIMUM_CALLS; call++)
	{
		struct bots_team_call *crew_call = &bots_team_globals.calls[call];
		struct unit_datum *vehicle;
		long seat;

		if (crew_call->vehicle_index == NONE)
			continue;
		vehicle = (struct unit_datum *)object_try_and_get_and_verify_type(crew_call->vehicle_index, _object_mask_vehicle);
		if (!vehicle || vehicle->unit.driver_object_index == NONE ||
			TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
		{
			crew_call->vehicle_index = NONE;
			continue;
		}
		for (seat = 0; seat < crew_call->seat_count; seat++)
		{
			long member = crew_call->crew[seat];
			long absolute = member != NONE ? bots_team_absolute(member) : NONE;
			struct player_datum *crew = member != NONE ? player_try_and_get(member) : NULL;
			short sat = NONE;

			if (member == NONE)
				continue;
			if (!crew || absolute == NONE || crew->unit_index == NONE ||
				bots_team_unit_vehicle(crew->unit_index, &sat) == crew_call->vehicle_index ||
				now - crew_call->tick > BOTS_TEAM_CREW_SECONDS * TICKS_PER_SECOND ||
				(bots_team_globals.state[absolute] & _bots_team_state_fighting) ||
				now < bots_team_globals.refused_until_tick[absolute] ||
				bots_common_seat_occupant(crew_call->vehicle_index, crew_call->seats[seat]) != NONE)
			{
				crew_call->crew[seat] = NONE;
			}
		}
	}
	/* new calls: a team's driver in a Warthog (vehicle type jeep) with an
	empty gunner's or passenger's seat */
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct bots_team_call *crew_call = NULL;
		struct unit_definition *definition;
		long vehicle_index, free_call = NONE;
		short seat, seat_index;

		if (player->unit_index == NONE || player->team_index == NONE ||
			(vehicle_index = bots_team_unit_vehicle(player->unit_index, &seat)) == NONE ||
			bots_common_seat_kind(vehicle_index, seat) != 0 || bots_common_vehicle_type(vehicle_index) != 1)
		{
			continue;
		}
		for (call = 0; call < BOTS_TEAM_MAXIMUM_CALLS; call++)
		{
			if (bots_team_globals.calls[call].vehicle_index == vehicle_index)
				crew_call = &bots_team_globals.calls[call];
			else if (bots_team_globals.calls[call].vehicle_index == NONE && free_call == NONE)
				free_call = call;
		}
		if (!crew_call)
		{
			if (free_call == NONE)
				continue;
			crew_call = &bots_team_globals.calls[free_call];
			csmemset(crew_call, 0, sizeof(*crew_call));
			crew_call->vehicle_index = vehicle_index;
			crew_call->team_index = player->team_index;
			crew_call->tick = now;
			crew_call->bot_driver = bots_team_player_is_bot(iterator.datum_index);
			/* (the gunner's seats first, then the passengers') */
			definition = unit_definition_get(object_get(vehicle_index)->definition_index);
			for (seat_index = 0; seat_index < definition->unit.seats.count &&
				crew_call->seat_count < BOTS_TEAM_CREW_SEATS; seat_index++)
			{
				if (bots_common_seat_kind(vehicle_index, seat_index) == 1)
				{
					crew_call->seats[crew_call->seat_count] = seat_index;
					crew_call->crew[crew_call->seat_count++] = NONE;
				}
			}
			for (seat_index = 0; seat_index < definition->unit.seats.count &&
				crew_call->seat_count < BOTS_TEAM_CREW_SEATS; seat_index++)
			{
				if (bots_common_seat_kind(vehicle_index, seat_index) == 2)
				{
					crew_call->seats[crew_call->seat_count] = seat_index;
					crew_call->crew[crew_call->seat_count++] = NONE;
				}
			}
		}
		/* (one or two called in all, the gunner first: the nearest free bot
		to each empty seat not yet called for, while the call is young) */
		if (now - crew_call->tick > BOTS_TEAM_CREW_SECONDS * TICKS_PER_SECOND)
			continue;
		{
			long called = 0, index;

			for (index = 0; index < crew_call->seat_count; index++)
			{
				if (crew_call->crew[index] != NONE ||
					bots_common_seat_occupant(vehicle_index, crew_call->seats[index]) != NONE)
				{
					called++;
				}
			}
			for (index = 0; index < crew_call->seat_count && called < 2; index++)
			{
				struct data_iterator candidates;
				struct player_datum *candidate;
				long best = NONE;
				real best_distance = 1e30f;

				if (crew_call->crew[index] != NONE ||
					bots_common_seat_occupant(vehicle_index, crew_call->seats[index]) != NONE)
				{
					continue;
				}
				data_iterator_new(&candidates, player_data);
				while ((candidate = (struct player_datum *)data_iterator_next(&candidates)) != NULL)
				{
					real distance;

					if (candidate->team_index != crew_call->team_index || bots_team_called(candidates.datum_index) ||
						!bots_team_crew_free(candidates.datum_index, crew_call->team_index, vehicle_index, &distance))
					{
						continue;
					}
					if (distance < best_distance)
					{
						best_distance = distance;
						best = candidates.datum_index;
					}
				}
				if (best == NONE)
					break;
				crew_call->crew[index] = best;
				called++;
			}
		}
	}
}

long bots_team_crew_call(long player_index, short *seat_index)
{
	long call, seat;

	for (call = 0; call < BOTS_TEAM_MAXIMUM_CALLS; call++)
	{
		struct bots_team_call *crew_call = &bots_team_globals.calls[call];

		for (seat = 0; crew_call->vehicle_index != NONE && seat < crew_call->seat_count; seat++)
		{
			if (crew_call->crew[seat] == player_index)
			{
				/* (a vehicle gone since the calls were last looked at: no call) */
				if (!object_try_and_get_and_verify_type(crew_call->vehicle_index, _object_mask_vehicle))
				{
					crew_call->vehicle_index = NONE;
					return NONE;
				}
				*seat_index = crew_call->seats[seat];
				return crew_call->vehicle_index;
			}
		}
	}
	return NONE;
}

void bots_team_crew_refuse(long player_index)
{
	long absolute = bots_team_absolute(player_index);
	long call, seat;

	if (absolute == NONE)
		return;
	bots_team_globals.refused_until_tick[absolute] = game_time_get() + TICKS_PER_SECOND * 5;
	for (call = 0; call < BOTS_TEAM_MAXIMUM_CALLS; call++)
	{
		for (seat = 0; bots_team_globals.calls[call].vehicle_index != NONE &&
			seat < bots_team_globals.calls[call].seat_count; seat++)
		{
			if (bots_team_globals.calls[call].crew[seat] == player_index)
				bots_team_globals.calls[call].crew[seat] = NONE;
		}
	}
}

boolean bots_team_crew_wait(long vehicle_index)
{
	long call, seat;

	for (call = 0; call < BOTS_TEAM_MAXIMUM_CALLS; call++)
	{
		struct bots_team_call *crew_call = &bots_team_globals.calls[call];

		if (crew_call->vehicle_index != vehicle_index || !crew_call->bot_driver ||
			game_time_get() - crew_call->tick > BOTS_TEAM_CREW_WAIT_SECONDS * TICKS_PER_SECOND)
		{
			continue;
		}
		for (seat = 0; seat < crew_call->seat_count; seat++)
		{
			if (crew_call->crew[seat] != NONE)
				return TRUE;
		}
	}
	return FALSE;
}
