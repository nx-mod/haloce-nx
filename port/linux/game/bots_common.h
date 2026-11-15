/* bots_common.h: what the two kinds of bots share (port/linux/game/bots.c)

The bots come in two kinds (bots.kind): Birchwood's (bots.c and its helpers,
halo-ce-vita's bots with this port's rules) and Proving Grounds' (pgbots*.c,
a clean-room brain after research_notes/ppg-bot-personalities.md). Both are
players of machines of their own (bots.h), and both take from bots.c the
plumbing that makes a bot a player and reads the world: the settings, the
lines of sight and the ground, the vehicles' and weapons' tag facts, the
powerups and turrets found once a second, the path searches (three slots for
every bot of either kind, in the tick or on the helper thread), and the
action written as a player's (one-tick presses).

These are bots.c's own functions, unchanged, behind names of their own: a
Birchwood bot does what it always did. Each bot records its kind when it
joins (bots_network_player), so one game may hold both. */
#ifndef HALO_BOTS_COMMON_H
#define HALO_BOTS_COMMON_H

struct player_action;
struct path_result;
struct collision_bsp;

enum
{
	_bots_kind_birchwood = 0,
	_bots_kind_proving_grounds,
	NUMBER_OF_BOTS_KINDS,
};

/* bots.kind (HALO_BOT_KIND): the kind a bot added now takes */
short bots_kind(void);
/* the kind bot `bot_index` joined as (Oddball and Race: Proving Grounds,
whatever it joined as, since Birchwood's bots do not play them) */
short bots_bot_kind(long bot_index);
/* whether the game's type is one Birchwood's bots play (Slayer, CTF, King) */
boolean bots_birchwood_plays(long game_engine_index);
/* whether bots of either kind play it (those, Oddball and Race) */
boolean bots_game_type_played(long game_engine_index);
/* the kind and skill level (bots_combat.h's; NONE: bots.skill's) the next
bot to join takes, once (bots_network_player); NONE for the settings' */
void bots_set_next_join(short kind, short skill);
/* the team the next bot to join was asked for (NONE: the smaller, kept even
in a server's lobby as players come and go and the gametype changes), and
the one bot `bot_index` was asked for */
void bots_set_next_team(long team_index);
/* a server's bots' roster (sv_bot_add puts a bot on it, sv_bot_drop takes it
off): those on it join each new lobby again, as they joined */
void bots_set_on_roster(long bot_index, boolean on);
boolean bots_on_roster(long bot_index);
/* (hosting begun from the menus: no bot of the last time's) */
void bots_clear_roster(void);
long bots_bot_team_asked(long bot_index);
/* bot `bot_index`'s kind as it joined (not the gametype's), its own level
(NONE: bots.skill's), the level it plays at, and a level's name */
short bots_bot_joined_kind(long bot_index);
short bots_bot_own_skill(long bot_index);
short bots_bot_skill(long bot_index);
char const *bots_skill_name(short skill);

/* the settings and switches (bots.c's) */
boolean bots_common_peaceful(void);
boolean bots_common_vehicles_enabled(void);
boolean bots_trace_enabled(void);
unsigned long long bots_time_us(void);
/* the maps the bots were readied for, counted (a new one: what was kept is
found again) */
unsigned long bots_common_map_generation(void);
/* whether profiling is on (HALO_TICK_PROFILE) */
int bots_common_profiling(void);

/* the world */
boolean bots_common_line_of_sight(long unit_index, real_point3d const *eye, real_point3d const *point,
	long target_unit_index);
long bots_common_surface_below(real_point3d const *point, real_point3d *ground);
long bots_common_unit_surface(long unit_index, real_point3d *point);
long bots_common_unit_vehicle(long unit_index);
/* whether a surface is walkable to the bots' searches (their copy: less what
was learnt to be in the way), the structure's collision bsp and a surface's
corners (16 at most) */
boolean bots_common_surface_walkable(long surface_index);
long bots_common_surface_count(void);
struct collision_bsp const *bots_common_bsp(void);
long bots_common_surface_corners(long surface_index, real_point3d *corners);
/* the surfaces across a surface's edges (its neighbours), how many */
long bots_common_surface_neighbours(long surface_index, long *neighbours, long maximum);
/* the scenery and machines about a stuck walk learnt to be in the way (the
searches go round them from now on); how many were */
long bots_common_paths_learn(real_point3d const *position, real_point3d const *goal);
/* the ground just before a stuck vehicle (at `point`, the vehicle at
`vehicle_point`, its goal left open) closed to the vehicles' searches alone,
as far about as the vehicle is wide */
void bots_common_paths_close_for_vehicles(real_point3d const *point, real_point3d const *vehicle_point,
	real_point3d const *goal_point, real vehicle_radius);
/* port: the first ladder or teleporter (Birchwood's, found once a map) on
the way from a surface to a goal on another piece of the ground (bots_nav.h),
its index, NONE for none: where it is entered, whether a ladder (climbed
facing the direction, up to the height), where it comes out; and a ladder's
climb or failure told (three failures and no climb: left out) */
long bots_common_portal_route(long start_surface_index, real_point3d const *start, long goal_surface_index,
	real_point3d const *goal, real_point3d *entrance, long *entrance_surface_index, boolean *ladder,
	real *climb_direction, real *top_z, real_point3d *exit);
void bots_common_ladder_result(long index, boolean climbed);

/* vehicles */
real bots_common_vehicle_path_radius(long vehicle_index);
/* the vehicle tag's type (vehicles.c's vehicle_type: 0 tank, 1 jeep, 4
scout, 6 turret, others fly) */
short bots_common_vehicle_type(long vehicle_index);
short bots_common_turret_seat(long vehicle_index);
long bots_common_seat_occupant(long vehicle_index, short seat_index);
/* 0 driver, 1 gunner, 2 passenger */
short bots_common_seat_kind(long vehicle_index, short seat_index);

/* weapons */
long bots_common_current_weapon(long unit_index);
boolean bots_common_weapon_empty(long weapon_index);
real bots_common_weapon_loaded(long weapon_index);
/* the distance Birchwood's bots keep from an enemy with the weapon (NONE:
none held) */
real bots_common_weapon_range(long weapon_index);
/* the weapon's projectile speed, world units a tick (0: hits at once) */
real bots_common_projectile_speed(long weapon_index);

/* the powerups lying on the map (health 1, overshield 2, camouflage 3) and
the turrets, found once a second */
long bots_common_powerup_count(void);
boolean bots_common_powerup(long index, long *object_index, short *kind, real_point3d *position);
long bots_common_turret_count(void);
long bots_common_turret(long index);

/* path searches: asked for a requester (any number the kind keeps: the
answer is the requester's own), from a start to a goal, as wide as the
radius, through the object a search ignores (the unit, or the vehicle it
drives), up to a target object (a vehicle to get into: NONE for none). The
slot asked (NONE: none free, ask again later) */
long bots_common_search_ask(long requester, long unit_index, real radius, long target_object_index,
	real_point3d const *start, long start_surface_index, real_point3d const *goal, long goal_surface_index);
/* the answer: 0 not yet, 1 a path (*reached: to the goal itself, not as near
as it came; *waypoint: to a waypoint of a long drive's on the way to the
goal, the next search from its end), -1 none; the slot is free again once
answered */
long bots_common_search_take(long slot, long requester, struct path_result *path, boolean *reached, boolean *waypoint);
void bots_common_search_drop(long slot);

/* the action, as a player's: the facing, the throttle from where it walks
(the stick turned so the walk keeps its direction), and the buttons, a press
one tick held and the next released (*previous_control_flags) */
void bots_common_write_action(struct player_action *action, real yaw, real pitch, boolean moving, real move_direction,
	real throttle_scale, unsigned long flags, unsigned long *previous_control_flags);

/* the Proving Grounds kind (pgbots.c) */
void pgbots_initialize_for_new_map(void);
void pgbots_dispose_from_old_map(void);
/* the tick's start, once a tick with Proving Grounds bots: the map's
analysis a bounded step, a team's director */
void pgbots_tick_begin(void);
void pgbots_bot_update(long bot_index, long player_index, struct player_action *action);
/* halo.log's minute lines, and the microseconds its bots took */
void pgbots_report(long now, unsigned long long us, long bot_ticks);

#endif
