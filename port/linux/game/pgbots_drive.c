/*
PGBOTS_DRIVE.C

(port) The Proving Grounds bots in vehicles (S10 of
research_notes/ppg-bot-personalities.md): their own controller, on the
shared vehicle walkable copy and path searches, none of Birchwood's driving
code. The engine reads a driver's input as a player's: the facing is the
steering's aim (wheels and the Ghost turn to it, reversing flips them; the
tank's hull turns only while moving), the throttle's j the Ghost's and the
Banshee's strafe, the jump the brake.

- Boarding: for a goal further than 35 u (any trip in Race), not carrying
  nor fighting, an upright vehicle not known for a dud, scored by its worth
  (Warthog 0.7, Ghost 0.6, Banshee 0.8, Scorpion 0.95), its nearness (25 u)
  and the trip (40 u); taken, past 0.15, with the chance 0.3 + 0.6 map_iq
  (always in Race), committed 8 s, a failure resting 20 s. The seat: the
  driver's of an empty one, the gun's of a driven one, else a passenger's
  (a rocket or sniper rifle rides as passenger). Into the seat as a player
  gets in, where a player could (unit_enter_seat, as Birchwood's bots).
- Driving: pure pursuit on the path (a lookahead by speed), drift held,
  slowed for the turn ahead, reversing with hysteresis, the Ghost strafing
  against its cross-track error, the tank kept rolling while it turns, the
  Banshee holding its height with feelers ahead and below.
- Fighting: the Warthog circles at 15 u for its gunner, the Ghost strafes,
  the tank shells from range with short moves, the Banshee makes runs; rams
  from normal up (the level's heading error); evasions with the juke.
- Stuck: reverse at full lock, reverse at the point, forward at the other
  lock, the blocker learnt, out after 15 s; flipped: out. Riders out near
  their goal, without a driver, or parked.

Clean room: none of Project Proving Grounds' code.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/vehicles.h"
#include "units/vehicle_definitions.h"
#include "items/weapons.h"
#include "ai/path.h"
#include "tag_files/tag_groups.h"

#include "bots.h"
#include "bots_common.h"
#include "bots_combat.h"
#include "pgbots.h"
#include "bots_team.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
boolean vehicle_is_flipped(long vehicle_index);
boolean unit_get_seat_entrance_point(long unit_index, long target_unit_index, short seat_index,
	real_point3d *entry_position, real_point3d *exit_position, real_point3d *seat_transform);

#define DEGREES (PGBOTS_PI / 180.0f)
#define SECONDS(s) ((long)((s) * (real)TICKS_PER_SECOND + 0.5f))

/* the vehicle tag's speeds (actor_moving.c pins their places) */
struct pgbots_vehicle_speeds
{
	byte unit[0x2F8];
	real maximum_forward_speed;
	real maximum_reverse_speed;
	real speed_acceleration;
};
typedef char pgbots_vehicle_speed_offset_assert[
	offsetof(struct pgbots_vehicle_speeds, speed_acceleration) == 0x300 ? 1 : -1];

/* the vehicle types (vehicles.c's) */
enum
{
	_type_tank = 0,
	_type_jeep = 1,
	_type_scout = 4,
	_type_fighter = 5,
	_type_turret = 6,
};

/* vehicles bots stood by and never got into: skipped by every bot a while */
#define PGBOTS_DUDS 8
static struct
{
	long vehicle_index;
	long until_tick;
} pgbots_duds[PGBOTS_DUDS];

static boolean drive_dud(long vehicle_index, long now)
{
	long index;

	for (index = 0; index < PGBOTS_DUDS; index++)
	{
		if (pgbots_duds[index].vehicle_index == vehicle_index && now < pgbots_duds[index].until_tick)
			return TRUE;
	}
	return FALSE;
}

static void drive_mark_dud(long vehicle_index, long now)
{
	long index, oldest = 0;

	for (index = 0; index < PGBOTS_DUDS; index++)
	{
		if (pgbots_duds[index].until_tick < pgbots_duds[oldest].until_tick)
			oldest = index;
	}
	pgbots_duds[oldest].vehicle_index = vehicle_index;
	pgbots_duds[oldest].until_tick = now + SECONDS(120.0f);
}

void pgbots_drive_reset(struct pgbots_bot *bot)
{
	long declined = bot->ride.declined_vehicle, declined_until = bot->ride.declined_until_tick;
	long rest = bot->ride.board_rest_until_tick;

	csmemset(&bot->ride, 0, sizeof(bot->ride));
	bot->ride.vehicle_index = NONE;
	bot->ride.board_vehicle = NONE;
	bot->ride.board_seat = NONE;
	bot->ride.ram_target = NONE;
	bot->ride.orbit_side = 1.0f;
	bot->ride.declined_vehicle = declined;
	bot->ride.declined_until_tick = declined_until;
	bot->ride.board_rest_until_tick = rest;
}

static real drive_value(short type)
{
	switch (type)
	{
	case _type_tank: return 0.95f;
	case _type_jeep: return 0.7f;
	case _type_scout: return 0.6f;
	case _type_fighter: return 0.8f;
	}
	return 0.0f;
}

/* the seat a bot takes in a vehicle: the driver's of an empty one, the gun's
of a driven one, else a passenger's (a rocket or sniper rifle rides along);
NONE for none free or claimed */
static short drive_seat(struct pgbots_bot *bot, long vehicle_index)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	struct unit_definition *definition = unit_definition_get(vehicle->definition_index);
	short seat_index, driver = NONE, gunner = NONE, passenger = NONE;
	boolean has_driver = vehicle->unit.driver_object_index != NONE;
	short weapon_class = _pgbots_class_automatic;
	long weapon_index = bots_common_current_weapon(bot->unit_index), index;

	if (weapon_index != NONE)
		weapon_class = pgbots_weapon_class(weapon_get(weapon_index)->definition_index, NULL);
	for (seat_index = 0; seat_index < definition->unit.seats.count; seat_index++)
	{
		boolean claimed = FALSE;

		if (bots_common_seat_occupant(vehicle_index, seat_index) != NONE)
			continue;
		for (index = 0; index < BOTS_MAXIMUM; index++)
		{
			struct pgbots_bot *other = &pgbots_globals.bots[index];

			if (other != bot && other->player_index != NONE && other->ride.board_vehicle == vehicle_index &&
				other->ride.board_seat == seat_index)
			{
				claimed = TRUE;
			}
		}
		/* (the driver's seat another bot is on its way to: driven, for the
		gun's and the passengers') */
		if (claimed && bots_common_seat_kind(vehicle_index, seat_index) == 0)
			has_driver = TRUE;
		if (claimed)
			continue;
		switch (bots_common_seat_kind(vehicle_index, seat_index))
		{
		case 0: if (driver == NONE) driver = seat_index; break;
		case 1: if (gunner == NONE) gunner = seat_index; break;
		default: if (passenger == NONE) passenger = seat_index; break;
		}
	}
	if (weapon_class == _pgbots_class_explosive || weapon_class == _pgbots_class_sniper)
	{
		if (has_driver && passenger != NONE)
			return passenger;
	}
	if (!has_driver && driver != NONE)
		return driver;
	if (has_driver && gunner != NONE)
		return gunner;
	if (has_driver && passenger != NONE)
		return passenger;
	return NONE;
}

real pgbots_drive_board_score(struct pgbots_bot *bot, real_point3d const *position, real trip, long *vehicle_index,
	short *seat)
{
	struct object_iterator iterator;
	long now = game_time_get();
	boolean race = game_engine_get_variant()->game_engine_index == game_engine_race;
	real best = 0.0f;

	*vehicle_index = NONE;
	*seat = NONE;
	if (now < bot->ride.board_rest_until_tick || !pgbots_globals.bots)
		return 0.0f;
	if (race)
		trip = MAX(trip, 40.0f);
	else if (trip <= 35.0f)
		return 0.0f;
	/* (the one gone for, kept while committed) */
	if (bot->ride.board_vehicle != NONE && now < bot->ride.board_until_tick &&
		object_try_and_get_and_verify_type(bot->ride.board_vehicle, _object_mask_vehicle))
	{
		*vehicle_index = bot->ride.board_vehicle;
		*seat = bot->ride.board_seat;
		return 2.0f;
	}
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		short type = bots_common_vehicle_type(iterator.index), candidate_seat;
		real distance, score;

		if (drive_value(type) <= 0.0f || object->object.up.k <= 0.5f || drive_dud(iterator.index, now) ||
			object->object.parent_object_index != NONE)
		{
			continue;
		}
		distance = pgbots_distance(position, &object->object.position);
		score = pgbots_rules_board_score(drive_value(type), distance, trip);
		if (score <= 0.15f || score <= best)
			continue;
		candidate_seat = drive_seat(bot, iterator.index);
		if (candidate_seat == NONE)
			continue;
		/* (a driver needs a vehicle it can drive: no flyer over the
		walkable surfaces but the Banshee's own way) */
		if (iterator.index == bot->ride.declined_vehicle && now < bot->ride.declined_until_tick)
			continue;
		best = score;
		*vehicle_index = iterator.index;
		*seat = candidate_seat;
	}
	if (*vehicle_index == NONE)
		return 0.0f;
	/* (taken by map knowledge's chance, always in Race; declined, not
	weighed again a while) */
	if (!race && pgbots_random01(bot) >= 0.3f + 0.6f * bot->profile.craft.map_iq)
	{
		bot->ride.declined_vehicle = *vehicle_index;
		bot->ride.declined_until_tick = now + SECONDS(8.0f);
		*vehicle_index = NONE;
		return 0.0f;
	}
	return 2.0f + best;
}

/* the board given up: a rest of 20 s */
static void drive_board_fail(struct pgbots_bot *bot, long now)
{
	bot->ride.board_vehicle = NONE;
	bot->ride.board_seat = NONE;
	bot->ride.board_rest_until_tick = now + SECONDS(20.0f);
	bot->ride.near_since_tick = 0;
	bot->goal = _pgbots_goal_none;
	bot->decide_tick = now;
	pgbots_path_forget(bot);
}

boolean pgbots_drive_board(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *position,
	boolean *moving, real *direction, unsigned long *flags)
{
	long now = game_time_get();
	long vehicle_index = bot->ride.board_vehicle;
	struct unit_datum *vehicle = vehicle_index != NONE ? vehicle_try_and_get(vehicle_index) : NULL;
	real_point3d entrance, seat_point;
	real distance;

	(void)moving;
	(void)flags;
	if (!vehicle || bot->ride.board_seat == NONE || now > bot->ride.board_until_tick ||
		bots_common_seat_occupant(vehicle_index, bot->ride.board_seat) != NONE)
	{
		drive_board_fail(bot, now);
		return FALSE;
	}
	if (!unit_get_seat_entrance_point(player->unit_index, vehicle_index, bot->ride.board_seat, &entrance, &seat_point, NULL))
		entrance = vehicle->object.position;
	/* (a dud: stood within 6 u of it 10 s, never in) */
	if (pgbots_distance(position, &vehicle->object.position) < 6.0f)
	{
		if (!bot->ride.near_since_tick)
			bot->ride.near_since_tick = now;
		else if (now - bot->ride.near_since_tick > SECONDS(10.0f))
		{
			drive_mark_dud(vehicle_index, now);
			drive_board_fail(bot, now);
			return FALSE;
		}
	}
	else
		bot->ride.near_since_tick = 0;
	distance = pgbots_distance(position, &entrance);
	if (distance > 0.9f)
	{
		if (!pgbots_walk(bot, player->unit_index, position, &entrance, NONE, 0.25f, NONE, vehicle_index, 0.5f, direction))
			*direction = pgbots_yaw_to(position, &entrance);
		return TRUE;
	}
	/* (at the entrance, still: in, as a player gets in) */
	if (magnitude_squared3d(&object_get(player->unit_index)->object.translational_velocity) < 0.01f &&
		now - bot->ride.board_press_tick > 10 && vehicle->object.up.k >= 0.5f &&
		!TEST_FLAG(vehicle->unit.flags, _unit_not_enterable_by_player_bit))
	{
		bot->ride.board_press_tick = now;
		if (unit_enter_seat(player->unit_index, vehicle_index, bot->ride.board_seat))
		{
			pgbots_globals.counts[_pgbots_count_boards]++;
			bot->ride.vehicle_index = vehicle_index;
			bot->ride.seat = bot->ride.board_seat;
			bot->ride.since_tick = now;
			bot->ride.board_vehicle = NONE;
			pgbots_path_forget(bot);
		}
	}
	return FALSE;
}

/* the point a lookahead along the path ahead (its steps, then the goal),
and the largest turn within a reach */
static void drive_lookahead(struct pgbots_bot *bot, real_point3d const *position, real_point3d const *goal,
	real lookahead, real reach, real_point3d *point, real *turn_ahead)
{
	real_point3d previous = *position;
	real travelled = 0.0f, last_yaw = 0.0f;
	long index;
	boolean found = FALSE, first = TRUE;

	*point = *goal;
	*turn_ahead = 0.0f;
	if (!bot->path.valid)
		return;
	for (index = bot->path.step_index; index <= bot->path.step_count; index++)
	{
		real_point3d const *step = index < bot->path.step_count ? &bot->path.steps[index].point : goal;
		real length = pgbots_distance2d(&previous, step);
		real yaw = pgbots_yaw_to(&previous, step);

		if (!first && travelled < reach)
			*turn_ahead = MAX(*turn_ahead, (real)fabs(pgbots_angle(yaw - last_yaw)));
		first = FALSE;
		last_yaw = yaw;
		if (!found && travelled + length >= lookahead)
		{
			real t = length > 0.01f ? (lookahead - travelled) / length : 0.0f;

			point->x = previous.x + (step->x - previous.x) * t;
			point->y = previous.y + (step->y - previous.y) * t;
			point->z = previous.z + (step->z - previous.z) * t;
			found = TRUE;
		}
		travelled += length;
		previous = *step;
		if (found && travelled > reach)
			break;
	}
}

/* the nearest visible enemy's unit from the bot's beliefs (the target, if
any) and its distance */
static long drive_enemy(struct pgbots_bot *bot, real_point3d const *position, real *distance)
{
	struct pgbots_belief *belief;
	struct player_datum *target;

	*distance = 1000.0f;
	if (!pgbots_target_visible(bot, game_time_get()))
		return NONE;
	belief = pgbots_belief(bot, bot->target_player_index);
	target = player_try_and_get(bot->target_player_index);
	if (!belief || !target || target->unit_index == NONE)
		return NONE;
	*distance = pgbots_distance(position, &belief->position);
	return target->unit_index;
}

/* the stuck test and its escalation: the throttle on and under a tenth of
top speed for 1 s, or under 0.5 u in 3 s; reset after 3 s of headway */
static short drive_stuck(struct pgbots_bot *bot, long vehicle_index, real_point3d const *position, real speed, real top,
	real throttle, long now)
{
	struct pgbots_ride *ride = &bot->ride;
	boolean slow = fabs(throttle) > 0.1f && fabs(speed) < 0.1f * top;

	if (now - ride->progress_tick >= SECONDS(3.0f))
	{
		if (pgbots_distance(position, &ride->progress_point) < 0.5f && fabs(throttle) > 0.1f)
			slow = TRUE;
		else if (pgbots_distance(position, &ride->progress_point) > 3.0f)
		{
			ride->stuck_stage = 0;
			ride->stuck_total_tick = 0;
		}
		ride->progress_point = *position;
		ride->progress_tick = now;
	}
	if (slow)
	{
		if (!ride->stuck_since_tick)
			ride->stuck_since_tick = now;
	}
	else
		ride->stuck_since_tick = 0;
	if (ride->stuck_stage > 0 && now < ride->stuck_stage_tick)
		return ride->stuck_stage;
	if (ride->stuck_since_tick && now - ride->stuck_since_tick > SECONDS(1.0f))
	{
		if (!ride->stuck_total_tick)
			ride->stuck_total_tick = now;
		ride->stuck_stage = (short)(ride->stuck_stage % 4 + 1);
		ride->stuck_since_tick = 0;
		ride->stuck_stage_tick = now + SECONDS(ride->stuck_stage == 2 ? 1.5f : 1.0f);
		if (ride->stuck_stage == 4)
		{
			real_point3d ahead = *position;
			real yaw = pgbots_yaw_to(position, &bot->goal_point);

			ahead.x += (real)cos(yaw) * 3.0f;
			ahead.y += (real)sin(yaw) * 3.0f;
			bots_common_paths_learn(position, &ahead);
			/* (and the ground it drives into closed to the vehicles' searches:
			a bank, a ledge's foot, a doorway, as Birchwood's are) */
			bots_common_paths_close_for_vehicles(&ahead, position, &bot->goal_point,
				bots_common_vehicle_path_radius(vehicle_index));
			pgbots_path_forget(bot);
			ride->stuck_stage_tick = now;
		}
		return ride->stuck_stage;
	}
	return 0;
}

/* a gunner's (or a passenger's) fight: the target with the seat's gun, the
window wider (2.5x, 4x once firing); a Warthog's gunner sprays 2 s where it
vanished; idle, it sweeps */
static void drive_gun(struct pgbots_bot *bot, struct player_datum *player, long weapon_index, unsigned long *flags,
	real_point3d const *eye, long now)
{
	real window = TEST_FLAG(bot->previous_control_flags, _unit_control_weapon_primary_trigger_bit) ? 4.0f : 2.5f;
	real speed = weapon_index != NONE ? bots_common_projectile_speed(weapon_index) : 0.0f;
	struct pgbots_belief *belief = pgbots_belief(bot, bot->target_player_index);

	if (pgbots_aim_and_fire(bot, player, eye, weapon_index, speed, window, flags))
	{
		if (belief)
			bot->ride.spray_point = belief->position;
		bot->ride.spray_until_tick = now + SECONDS(2.0f);
		return;
	}
	if (now < bot->ride.spray_until_tick && bot->ride.seat == 1)
	{
		real yaw = pgbots_yaw_to(eye, &bot->ride.spray_point);

		bot->yaw += PIN(pgbots_angle(yaw - bot->yaw), -0.1f, 0.1f);
		*flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		return;
	}
	if (!pgbots_target_visible(bot, now))
		bot->yaw = pgbots_angle(bot->yaw + 1.6f / (real)TICKS_PER_SECOND * (((now / 90) & 1) ? 1.0f : -1.0f));
}

void pgbots_drive_update(struct pgbots_bot *bot, struct player_datum *player, long vehicle_index,
	struct player_action *action, unsigned long *flags, boolean *moving, real *direction, real *throttle)
{
	struct pgbots_ride *ride = &bot->ride;
	struct unit_datum *vehicle = unit_get(vehicle_index);
	struct object_datum *object = object_get(vehicle_index);
	struct pgbots_vehicle_speeds *speeds = (struct pgbots_vehicle_speeds *)tag_get(VEHICLE_DEFINITION_TAG,
		object->definition_index);
	short type = bots_common_vehicle_type(vehicle_index);
	short seat = unit_get(player->unit_index)->unit.parent_seat_index;
	short seat_kind = bots_common_seat_kind(vehicle_index, seat);
	boolean driver = seat_kind == 0;
	long now = game_time_get();
	real_point3d position = object->object.position, eye, goal;
	real chassis = (real)atan2(object->object.forward.j, object->object.forward.i);
	real speed, lateral, top, acceleration, enemy_distance;
	long enemy, weapon_index, goal_surface = NONE;
	boolean exit = FALSE, fighting;
	struct pgbots_drive_output output;

	*moving = FALSE;
	unit_get_head_position(player->unit_index, &eye);
	if (ride->vehicle_index != vehicle_index)
	{
		pgbots_drive_reset(bot);
		ride->vehicle_index = vehicle_index;
		ride->since_tick = now;
		ride->progress_point = position;
		ride->progress_tick = now;
	}
	ride->seat = seat_kind;
	speed = (object->object.translational_velocity.i * object->object.forward.i +
		object->object.translational_velocity.j * object->object.forward.j) * (real)TICKS_PER_SECOND;
	lateral = (-object->object.translational_velocity.i * object->object.forward.j +
		object->object.translational_velocity.j * object->object.forward.i) * (real)TICKS_PER_SECOND;
	top = MAX(speeds->maximum_forward_speed * (real)TICKS_PER_SECOND, 1.0f);
	acceleration = MAX(speeds->speed_acceleration * (real)(TICKS_PER_SECOND * TICKS_PER_SECOND), 1.0f);
	enemy = drive_enemy(bot, &position, &enemy_distance);
	fighting = enemy != NONE;
	/* the seat's gun: the vehicle's for its gunner (and a Ghost's, a
	Scorpion's, a Banshee's driver), its own for a passenger */
	weapon_index = seat_kind == 2 || (driver && type == _type_jeep) ? bots_common_current_weapon(player->unit_index) :
		bots_common_current_weapon(vehicle_index);
	/* the goal: decided as on foot, every third tick */
	if (now >= bot->decide_tick)
	{
		real_point3d objective;
		boolean carrying, grab_near, unguarded;

		bot->decide_tick = now + 3;
		if (pgbots_director_objective(bot, &position, &objective, &carrying, &grab_near, &unguarded))
			bot->goal_point = objective;
		else if (bot->order_accepted && bot->role != _pgbots_role_free)
			bot->goal_point = bot->order_point;
		else if (pgbots_distance(&position, &bot->goal_point) < 8.0f || bot->goal_point.x == 0.0f)
		{
			long area = pgbots_map_roam(bot, &position);

			bot->goal_point = area != NONE ? pgbots_globals.map.areas[area].center : pgbots_globals.map.mid;
		}
	}
	goal = bot->goal_point;
	/* getting out: flipped, stuck 15 s, a rider without a driver or parked,
	near a goal on foot */
	if (vehicle_is_flipped(vehicle_index) || (object->object.up.k < 0.3f && fabs(speed) < 0.5f))
	{
		if (!ride->flipped_tick)
			ride->flipped_tick = now;
		exit = vehicle_is_flipped(vehicle_index) || now - ride->flipped_tick > SECONDS(1.5f);
	}
	else
		ride->flipped_tick = 0;
	if (driver && ride->stuck_total_tick && now - ride->stuck_total_tick > SECONDS(15.0f))
		exit = TRUE;
	if (!driver && vehicle->unit.driver_object_index == NONE && type != _type_turret && now - ride->since_tick > SECONDS(4.0f))
		exit = TRUE;
	if (!driver && fabs(speed) < 0.5f && now - ride->since_tick > SECONDS(4.0f) && !bots_team_crew_wait(vehicle_index))
	{
		if (!ride->parked_tick)
			ride->parked_tick = now;
		else if (now - ride->parked_tick > SECONDS(5.0f))
			exit = TRUE;
	}
	else
		ride->parked_tick = 0;
	if (game_engine_get_variant()->game_engine_index != game_engine_race &&
		pgbots_distance(&position, &goal) < 10.0f && !fighting && type != _type_fighter)
	{
		exit = TRUE;
	}
	/* (the Banshee: out only near the floor) */
	if (exit && type == _type_fighter)
	{
		real_point3d ground;

		if (pgbots_ground(&position, &ground) == NONE || position.z - ground.z > 2.5f)
			exit = FALSE;
	}
	if (exit && now >= ride->exit_rest_until_tick)
	{
		if (now - ride->exit_press_tick > TICKS_PER_SECOND && !unit_is_busy(player->unit_index))
		{
			*flags |= FLAG(_unit_control_action_bit);
			ride->exit_press_tick = now;
			if (++ride->exit_tries > 3)
			{
				ride->exit_rest_until_tick = now + SECONDS(30.0f);
				ride->exit_tries = 0;
			}
		}
		return;
	}
	if (!driver)
	{
		drive_gun(bot, player, weapon_index, flags, &eye, now);
		return;
	}
	/* (its crew called and on its way: waiting, a few seconds at most -
	bots_team.h) */
	if (!fighting && bots_team_crew_wait(vehicle_index))
	{
		action->throttle.i = 0.0f;
		action->throttle.j = 0.0f;
		return;
	}

	/* driving */
	{
		struct pgbots_drive_input input;
		real radius = bots_common_vehicle_path_radius(vehicle_index);
		real lookahead = type == _type_fighter ? 3.0f + 0.8f * fabs(speed) : pgbots_rules_lookahead(radius, speed);
		real reach = speed * speed / (2.0f * acceleration) + 2.0f;
		real_point3d point, target_point = goal;
		real walk_direction;
		short stage;

		/* the fight's own driving */
		if (fighting)
		{
			struct object_datum *target = object_get(enemy);
			real to_enemy = pgbots_yaw_to(&position, &target->object.position);

			target_point = target->object.position;
			if (type == _type_jeep && vehicle->unit.gunner_object_index != NONE && enemy_distance >= 8.0f &&
				enemy_distance <= 40.0f)
			{
				/* (the orbit: 15 u out at 35 degrees off the line, the side
				flipped every 3-6 s) */
				if (now >= ride->orbit_flip_tick)
				{
					ride->orbit_side = -ride->orbit_side;
					ride->orbit_flip_tick = now + SECONDS(3.0f + 3.0f * pgbots_random01(bot));
				}
				target_point.x += (real)cos(to_enemy + PGBOTS_PI - ride->orbit_side * 35.0f * DEGREES) * 15.0f;
				target_point.y += (real)sin(to_enemy + PGBOTS_PI - ride->orbit_side * 35.0f * DEGREES) * 15.0f;
				goal_surface = NONE;
			}
			else if (type == _type_tank && enemy_distance >= 15.0f && enemy_distance <= 45.0f)
			{
				/* (artillery: still, shelling, a short move now and then) */
				if (now >= ride->tank_next_move_tick || now - bot->hit_tick < 2)
				{
					ride->tank_move_until_tick = now + SECONDS(1.0f + pgbots_random01(bot));
					ride->tank_next_move_tick = now + SECONDS(3.0f + 2.0f * pgbots_random01(bot));
					ride->tank_move_point = position;
					ride->tank_move_point.x += (real)cos(to_enemy + PGBOTS_PI / 2.0f) * 4.0f * (pgbots_random01(bot) - 0.5f);
					ride->tank_move_point.y += (real)sin(to_enemy + PGBOTS_PI / 2.0f) * 4.0f * (pgbots_random01(bot) - 0.5f);
				}
				drive_gun(bot, player, weapon_index, flags, &eye, now);
				if (now < ride->tank_move_until_tick)
				{
					action->throttle.i = 0.5f;
					return;
				}
				action->throttle.i = 0.0f;
				return;
			}
			else if (type == _type_scout && enemy_distance < 40.0f)
			{
				/* (the Ghost: strafing at its range) */
				drive_gun(bot, player, weapon_index, flags, &eye, now);
				if (now >= ride->orbit_flip_tick)
				{
					ride->orbit_side = -ride->orbit_side;
					ride->orbit_flip_tick = now + SECONDS(0.5f + 0.9f * pgbots_random01(bot));
				}
				action->throttle.i = enemy_distance > 22.0f ? 1.0f : enemy_distance > 8.0f ? 0.5f : -0.4f;
				action->throttle.j = 0.8f * ride->orbit_side * MAX(0.5f, bot->profile.craft.strafe);
				return;
			}
			else if (type == _type_fighter)
			{
				/* (the Banshee's run: nose on from 15 u, out to 22 u and up 12
				past 6 u or once behind it) */
				drive_gun(bot, player, weapon_index, flags, &eye, now);
				if (ride->banshee_out_tick && now - ride->banshee_out_tick < SECONDS(6.0f) && enemy_distance < 22.0f)
				{
					target_point.x = position.x + (real)cos(to_enemy + PGBOTS_PI) * 22.0f;
					target_point.y = position.y + (real)sin(to_enemy + PGBOTS_PI) * 22.0f;
					target_point.z = target->object.position.z + 12.0f;
				}
				else
				{
					ride->banshee_out_tick = 0;
					if (pgbots_distance2d(&position, &target->object.position) < 6.0f)
						ride->banshee_out_tick = now;
				}
				action->throttle.i = 1.0f;
				goal_surface = NONE;
			}
			/* (a ram: from normal up, not in Race, at infantry without an
			explosive or charge weapon within 18 u, fast and nose on) */
			if (bot->profile.hands.ram_error_degrees >= 0.0f && game_engine_get_variant()->game_engine_index != game_engine_race &&
				bots_common_unit_vehicle(enemy) == NONE && enemy_distance < 18.0f && speed > 0.5f * top &&
				type != _type_fighter)
			{
				long target_weapon = bots_common_current_weapon(enemy);
				short target_class = target_weapon != NONE ?
					pgbots_weapon_class(weapon_get(target_weapon)->definition_index, NULL) : _pgbots_class_automatic;
				real nose = (real)fabs(pgbots_angle(to_enemy - chassis));

				if (target_class != _pgbots_class_explosive && target_class != _pgbots_class_charge &&
					nose < (type == _type_scout ? 12.0f : 15.0f) * DEGREES && ride->ram_target != enemy)
				{
					ride->ram_target = enemy;
					ride->ram_until_tick = now + SECONDS(6.0f);
					ride->ram_error = (pgbots_random01(bot) * 2.0f - 1.0f) * bot->profile.hands.ram_error_degrees * DEGREES;
					pgbots_globals.counts[_pgbots_count_rams]++;
				}
			}
		}
		if (ride->ram_target != NONE && (now > ride->ram_until_tick || !object_try_and_get(ride->ram_target)))
			ride->ram_target = NONE;
		/* the path to the point (the vehicle's own search) */
		if (!pgbots_walk(bot, player->unit_index, &position, &target_point, goal_surface, radius, vehicle_index, NONE, 3.0f,
			&walk_direction))
		{
			walk_direction = pgbots_yaw_to(&position, &target_point);
		}
		csmemset(&input, 0, sizeof(input));
		drive_lookahead(bot, &position, &target_point, lookahead, reach, &point, &input.turn_ahead);
		if (ride->ram_target != NONE)
		{
			struct object_datum *target = object_get(ride->ram_target);
			real lead = MIN(1.0f, pgbots_distance(&position, &target->object.position) / 10.0f);

			point = target->object.position;
			point.x += target->object.translational_velocity.i * (real)TICKS_PER_SECOND * lead;
			point.y += target->object.translational_velocity.j * (real)TICKS_PER_SECOND * lead;
		}
		input.alpha = pgbots_angle(pgbots_yaw_to(&position, &point) - chassis +
			(ride->ram_target != NONE ? ride->ram_error : 0.0f));
		input.lookahead = MAX(pgbots_distance2d(&position, &point), 0.5f);
		input.wheelbase = type == _type_tank ? 1.2f : 0.8f;
		input.forward_speed = speed;
		input.lateral_speed = lateral;
		input.top_speed = top;
		input.acceleration = acceleration;
		input.reversing = ride->reverse_tick != 0;
		input.reverse_seconds = ride->reverse_tick ? (real)(now - ride->reverse_tick) / (real)TICKS_PER_SECOND : 0.0f;
		input.point_distance = pgbots_distance2d(&position, &point);
		input.ghost = type == _type_scout;
		input.tank = type == _type_tank;
		input.arrival = fighting ? -1.0f : pgbots_distance2d(&position, &goal);
		input.cross_track = 0.0f;
		if (input.ghost)
			input.cross_track = (real)sin(pgbots_angle(walk_direction - chassis)) * MIN(input.point_distance, 2.0f);
		stage = drive_stuck(bot, vehicle_index, &position, speed, top, ride->last_throttle, now);
		input.stuck_reverse = stage == 1 || stage == 2;
		pgbots_rules_drive(&input, &output);
		ride->last_throttle = output.throttle;
		if (output.reversing && !ride->reverse_tick)
			ride->reverse_tick = now;
		else if (!output.reversing)
			ride->reverse_tick = 0;
		/* (the stuck escalation's moves: full lock one way back, back at the
		point, full lock the other way ahead) */
		if (stage == 1)
			output.steer = -PIN(input.alpha * 4.0f, -1.2f, 1.2f);
		else if (stage == 3)
		{
			output.steer = PIN(-input.alpha * 4.0f, -1.2f, 1.2f);
			output.throttle = 1.0f;
		}
		/* (the Banshee: its height held, feelers ahead and below) */
		if (type == _type_fighter)
		{
			real cruise = pgbots_distance2d(&position, &goal) > 60.0f ? 14.8f : 5.0f;
			real_point3d ground;
			real height = pgbots_ground(&point, &ground) != NONE ? ground.z + cruise : target_point.z + cruise;
			real pitch;

			if (fighting)
				height = target_point.z + 1.0f;
			pitch = pgbots_rules_banshee_pitch(height - position.z, object->object.translational_velocity.k *
				(real)TICKS_PER_SECOND, pgbots_distance2d(&position, &point));
			if (now - ride->since_tick < SECONDS(2.0f))
				pitch = 0.5f;
			if (now >= ride->feeler_tick)
			{
				real_point3d ahead = position;
				real reach_ahead = MAX(5.0f, fabs(speed));
				boolean below = (now / 6) & 1;

				ride->feeler_tick = now + 6;
				ahead.x += (real)cos(chassis) * reach_ahead * (below ? 0.86f : 1.0f);
				ahead.y += (real)sin(chassis) * reach_ahead * (below ? 0.86f : 1.0f);
				ahead.z -= below ? reach_ahead * 0.5f : 0.0f;
				pgbots_globals.rays++;
				if (!bots_common_line_of_sight(vehicle_index, &position, &ahead, NONE))
				{
					ride->feeler_block_until_tick = now + SECONDS(1.0f);
					ride->feeler_turn = pgbots_random01(bot) < 0.5f ? 0.8f : -0.8f;
				}
			}
			if (now < ride->feeler_block_until_tick)
			{
				pitch = 30.0f * DEGREES;
				output.steer += ride->feeler_turn;
			}
			if (!fighting)
			{
				bot->pitch = pitch;
				action->throttle.i = pgbots_distance2d(&position, &goal) < 6.0f ? 0.5f : 1.0f;
			}
			bot->yaw = pgbots_angle(chassis + output.steer);
			return;
		}
		bot->yaw = pgbots_angle(chassis + output.steer);
		bot->pitch = 0.0f;
		action->throttle.i = output.throttle;
		action->throttle.j = output.strafe;
		if (output.brake)
			*flags |= FLAG(_unit_control_jump_bit);
		/* (a driver with its own gun fights as it drives: the Scorpion's
		gun, a Ghost's, aimed as the facing allows) */
		if (fighting && type != _type_jeep)
			drive_gun(bot, player, weapon_index, flags, &eye, now);
	}
	(void)direction;
	(void)throttle;
	(void)moving;
}
