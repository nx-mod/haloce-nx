/* pgbots.h: the Proving Grounds bots' own state, shared by pgbots*.c

The second kind of bot (bots_common.h): pgbots.c (perception, goals, the
fight, movement), pgbots_map.c (what the map is: areas, power positions,
holds, overlooks, cover), pgbots_director.c (each team's roles, posture,
threat map and silent callouts) and pgbots_drive.c (vehicles). The rules
are pgbots_rules.c's. Nothing here is game state: it is made again each map
and each spawn, on the host alone. Clean room: written after
research_notes/ppg-bot-personalities.md, none of Project Proving Grounds'
code. */
#ifndef HALO_PGBOTS_H
#define HALO_PGBOTS_H

#include "pgbots_rules.h"

/* the enemies a bot keeps beliefs about at once (the oldest let go) */
#define PGBOTS_BELIEFS 32
/* goals banned for no headway at once */
#define PGBOTS_BANS 4
/* a team's blackboard entries */
#define PGBOTS_BOARD 16
/* the map's areas (8 units on a side), power positions, role slots */
#define PGBOTS_AREAS 1536
#define PGBOTS_AREA_SIZE 8.0f
#define PGBOTS_POWER 8
#define PGBOTS_SLOTS 16
/* the teams with a director (red, blue; free for all has none) */
#define PGBOTS_TEAMS 2

/* where a belief came from */
enum
{
	_pgbots_source_eyes = 0,
	_pgbots_source_hit,
	_pgbots_source_radar,
	_pgbots_source_heard,
	_pgbots_source_callout,
	NUMBER_OF_PGBOTS_SOURCES,
};

/* the director's roles */
enum
{
	_pgbots_role_free = 0,
	_pgbots_role_runner,
	_pgbots_role_escort,
	_pgbots_role_defender,
	_pgbots_role_hunter,
	_pgbots_role_anchor,
	_pgbots_role_scorer,
	_pgbots_role_overlook,
	_pgbots_role_drive,
	_pgbots_role_ride,
	NUMBER_OF_PGBOTS_ROLES,
};

/* the bot's belief about one enemy (S4) */
struct pgbots_belief
{
	long player_index;
	real_point3d position;
	real_vector3d velocity;
	long tick;
	short source;
	boolean weak;
	/* seen with its eyes: last in sight (NONE never), the sighting's start,
	recognised at (NONE: not yet), out of view since */
	long seen_tick;
	long sighting_tick;
	long recognized_tick;
	long lost_tick;
	/* its magazine last scan (gunfire heard as rounds dropping) */
	short rounds;
	/* the scan's last look at it (the rest wait their turn), and its last
	ray: when, to where, clear (the fire gate reuses it) */
	long checked_tick;
	/* when it became known after 3 s or more unknown (a new contact: what
	a bot reacts to, once) */
	long contact_tick;
	long ray_tick;
	real_point3d ray_point;
	boolean ray_clear;
};

/* the hands (S7) */
struct pgbots_aim
{
	long target_player_index;
	/* the target's velocity and the lead as last sampled, the next sample */
	real_vector3d velocity;
	real lead;
	long next_sample_tick;
	real error_yaw;
	real error_pitch;
	long focus_since_tick;
	/* a flick: from, to, when, how long (ticks), 0 for none */
	real flick_from_yaw, flick_from_pitch;
	real flick_to_yaw, flick_to_pitch;
	long flick_tick;
	long flick_ticks;
	/* the head or the chest, rolled for this target, weapon and shield state */
	long head_target;
	long head_weapon;
	boolean head_shield;
	boolean head;
};

/* a ride in a vehicle (S10, pgbots_drive.c) */
struct pgbots_ride
{
	long vehicle_index;
	short seat;
	long since_tick;
	/* the throttle the last tick drove with (the stuck test's) */
	real last_throttle;
	/* boarding: the vehicle gone for, its seat, until when; none until then
	after a failure */
	long board_vehicle;
	short board_seat;
	long board_until_tick;
	long board_rest_until_tick;
	long board_press_tick;
	/* a vehicle whose boarding roll failed, not weighed again until then;
	since when it stands near the vehicle it goes for (a dud's test) */
	long declined_vehicle;
	long declined_until_tick;
	long near_since_tick;
	/* driving: the path's progress, reversing since (0 not), the stuck test
	and escalation, the orbit's side and its flip, the ram run */
	real progress;
	long reverse_tick;
	long stuck_since_tick;
	short stuck_stage;
	long stuck_stage_tick;
	long stuck_total_tick;
	real_point3d progress_point;
	long progress_tick;
	real orbit_side;
	long orbit_flip_tick;
	long ram_target;
	long ram_until_tick;
	real ram_error;
	long evade_until_tick;
	real evade_yaw;
	/* the tank's moves, the Banshee's runs (out since, 0 not) */
	long tank_move_until_tick;
	long tank_next_move_tick;
	real_point3d tank_move_point;
	long banshee_out_tick;
	long takeoff_until_tick;
	long feeler_tick;
	long feeler_block_until_tick;
	real feeler_turn;
	/* getting out: tries, and none until then */
	long exit_press_tick;
	short exit_tries;
	long exit_rest_until_tick;
	long parked_tick;
	long flipped_tick;
	/* a gunner's: spraying where the target vanished until then */
	long spray_until_tick;
	real_point3d spray_point;
};

struct pgbots_bot
{
	long player_index;
	long unit_index;
	unsigned long random;
	struct pgbots_profile profile;
	/* its team (NONE: free for all) and rank in it (the dealing's) */
	short team;
	long team_rank;
	long spawn_tick;

	real yaw;
	real pitch;

	struct pgbots_belief beliefs[PGBOTS_BELIEFS];
	struct pgbots_aim aim;

	/* its target: who, since when, its threat then; the damage done to it
	since (its vitality when taken) */
	long target_player_index;
	long target_since_tick;
	real target_threat;
	real target_vitality;

	/* hurt: its vitality and shield last tick, the last hit, by whom (NONE
	not known), whether it knew the attacker; the damage over 2 s, a bucket a
	half second */
	real last_vitality;
	real last_shield;
	long hit_tick;
	long hit_attacker;
	boolean hit_noticed;
	long hit_notice_tick;
	real damage_buckets[4];
	long damage_bucket_tick;
	long last_damage_dealt_tick;
	long last_killer;
	long deaths_seen;

	/* the goal: what, where (its surface), the object it is for, since
	when, the next decision; headway: the distance to it at the last check,
	when; the goals banned and until when */
	short goal;
	real_point3d goal_point;
	long goal_surface;
	long goal_object;
	long goal_tick;
	long decide_tick;
	real headway_distance;
	long headway_tick;
	short bans[PGBOTS_BANS];
	long ban_until_ticks[PGBOTS_BANS];
	boolean recharging;
	/* the weapon upgrade and the cover found last (looked for again once a
	second at most) */
	long upgrade_tick;
	long upgrade_object;
	real upgrade_taste;
	long cover_tick;
	boolean cover_found;
	/* items it found no way to, left be until then */
	long unreachable_objects[4];
	long unreachable_until_ticks[4];
	real_point3d cover_point;
	long cover_surface;
	long hold_until_tick;
	long roam_area;

	/* the director's order: the role, its point, accepted, when given */
	short role;
	real_point3d order_point;
	long order_surface;
	boolean order_accepted;
	long order_tick;

	/* the fight: started, last in one, the opener and press rolls, the
	strafe, juke and pause, the trigger's rhythm, grenades, melee, the
	deterrent burst, the charge */
	long fight_start_tick;
	long fight_tick;
	boolean opener_rolled;
	boolean press;
	real strafe_sign;
	long strafe_flip_tick;
	long pause_until_tick;
	long crouch_until_tick;
	long burst_until_tick;
	long gap_until_tick;
	long next_press_tick;
	long next_grenade_tick;
	long grenade_aim_until_tick;
	real_point3d grenade_point;
	long melee_tick;
	long deterrent_until_tick;
	long next_deterrent_tick;
	long charge_tick;
	long flush_seen_tick;

	/* weapons: the next switch allowed; an overcharge let go, the precision
	weapon taken at once until this tick */
	long weapon_switch_tick;
	long combo_until_tick;
	long pickup_press_tick;

	/* the path: its search's slot (NONE), the path and when found, its
	goal, whether it reached the goal, failures in a row; a detour (rolled
	once a goal) and its point */
	long search_slot;
	struct path_result path;
	long path_tick;
	real_point3d path_goal;
	boolean path_reached;
	/* (its end a waypoint of a long drive's, the goal further on) */
	boolean path_waypoint;
	short path_failures;
	boolean detour_rolled;
	boolean detouring;
	real_point3d detour_point;
	long detour_surface;
	/* a goal on another piece of the ground (bots_nav.h): the ladder or
	teleporter on the way (Birchwood's index, NONE), for which goal, when
	looked for, where it is entered and comes out; a ladder's climb begun,
	its jump this tick; near the entrance last tick */
	long portal_index;
	real_point3d portal_goal;
	long portal_route_tick;
	real_point3d portal_entrance;
	long portal_surface;
	real_point3d portal_exit;
	boolean portal_ladder;
	real portal_climb_direction;
	real portal_top_z;
	long portal_climb_tick;
	long portal_top_tick;
	long portal_jump_tick;
	/* (the last tick its walk on foot looked at it) */
	long portal_walk_tick;
	boolean portal_near;

	/* stuck: where it was and when, the tries, the move to unstick */
	real_point3d stuck_position;
	long stuck_tick;
	short stuck_count;
	long unstick_until_tick;
	real unstick_direction;
	/* the last solid ground (the edge guard), and since when in the air */
	real_point3d last_ground;
	long airborne_tick;

	/* the look: the next glance, its side, until when; where the freshest
	threat was (kept in mind until then) */
	long glance_tick;
	real glance_offset;
	long glance_until_tick;
	real_point3d threat_point;
	long threat_until_tick;

	unsigned long previous_control_flags;
	struct pgbots_ride ride;
};

struct pgbots_board_entry
{
	long player_index;
	real_point3d point;
	long tick;
	long poster;
};

struct pgbots_slot
{
	short role;
	real_point3d point;
	long surface;
	/* the trait the slot fits (NONE: teamwork, an escort's) */
	short fit;
	long bot_index;
	/* an object it is about (the carrier escorted, the vehicle crewed) */
	long object_index;
};

struct pgbots_team
{
	real posture;
	struct pgbots_board_entry board[PGBOTS_BOARD];
	long board_count;
	long next_post_tick;
	long focus_player;
	long director_tick;
	struct pgbots_slot slots[PGBOTS_SLOTS];
	long slot_count;
	long anchor_until_tick;
	short anchor_posts[2];
	long hill_contest_until_tick;
	long hill_send;
	/* its last death's area, and the threat per area (the map's) */
	real *threat;
};

struct pgbots_area
{
	real_point3d center;
	long surface;
	long visited_tick;
};

struct pgbots_power
{
	real_point3d point;
	long surface;
	real score;
};

struct pgbots_map
{
	boolean begun;
	boolean ready;
	unsigned long generation;
	long area_count;
	struct pgbots_area *areas;
	real_point3d mid;
	long mid_surface;
	boolean has_bases;
	real_point3d bases[2];
	long base_surfaces[2];
	long power_count;
	struct pgbots_power power[PGBOTS_POWER];
	/* each team's holds (power positions near its base) */
	long hold_count[PGBOTS_TEAMS];
	short holds[PGBOTS_TEAMS][3];
	/* the analysis' progress (pgbots_map.c's own) */
	long phase;
	long cursor;
	unsigned long long spent_us;
	long ticks;
};

/* the kind's globals (pgbots.c) */
struct pgbots_globals
{
	boolean allocated;
	struct pgbots_bot *bots;
	struct pgbots_team teams[PGBOTS_TEAMS];
	struct pgbots_map map;
	unsigned long deal_offset;
	long tick_director_team;
	/* the counts and costs of halo.log's lines */
	long counts[24];
	unsigned long long part_us[8];
	long rays;
};

enum
{
	_pgbots_count_beliefs_eyes = 0,
	_pgbots_count_beliefs_hit,
	_pgbots_count_beliefs_radar,
	_pgbots_count_beliefs_heard,
	_pgbots_count_beliefs_callout,
	_pgbots_count_posts,
	_pgbots_count_grenades,
	_pgbots_count_flicks,
	_pgbots_count_overshoots,
	_pgbots_count_detours,
	_pgbots_count_retreats,
	_pgbots_count_bans,
	_pgbots_count_boards,
	_pgbots_count_rams,
	_pgbots_count_head_aims,
	_pgbots_count_switches,
	_pgbots_count_unreachable,
	_pgbots_count_stuck,
	_pgbots_count_portals,
	_pgbots_count_crew,
};

/* (HALO_TICK_PROFILE) buckets */
enum
{
	_pgbots_part_look = 0,
	_pgbots_part_decide,
	_pgbots_part_fight,
	_pgbots_part_drive,
	_pgbots_part_director,
	_pgbots_part_map,
	NUMBER_OF_PGBOTS_PARTS,
};

extern struct pgbots_globals pgbots_globals;

/* pgbots.c's helpers */
real pgbots_distance(real_point3d const *a, real_point3d const *b);
real pgbots_distance2d(real_point3d const *a, real_point3d const *b);
real pgbots_angle(real a);
real pgbots_yaw_to(real_point3d const *from, real_point3d const *to);
real pgbots_random01(struct pgbots_bot *bot);
boolean pgbots_enemy(struct player_datum *player, struct player_datum *other);
/* a sight ray through the tick's pair cache (counted against the budget) */
boolean pgbots_sight(long unit_index, real_point3d const *eye, real_point3d const *point, long target_unit_index);
/* a goal's ground (its surface; NONE for none) */
long pgbots_ground(real_point3d const *point, real_point3d *ground);
/* the bot's index of a pgbots_bot */
long pgbots_bot_index(struct pgbots_bot const *bot);
void pgbots_profile_part(short part, unsigned long long *started);
/* a weapon definition's class from its tag (S9), and its blast's reach */
short pgbots_weapon_class(long definition_index, real *blast);

/* pgbots_map.c */
void pgbots_map_reset(void);
void pgbots_map_release(void);
/* the analysis a bounded step (25 us at most a tick) */
void pgbots_map_step(void);
long pgbots_map_area_of(real_point3d const *point);
/* a roaming area, toward those not visited lately */
long pgbots_map_roam(struct pgbots_bot *bot, real_point3d const *position);
/* a team's hold (0 to 2): its point; FALSE for none */
boolean pgbots_map_hold(short team, long index, real_point3d *point, long *surface);
/* cover from threats within reach of the position: on demand, at most 8
candidates (their rays counted); FALSE for none (a point straight away) */
boolean pgbots_map_cover(struct pgbots_bot *bot, real_point3d const *position, real_point3d const *threat,
	real reach, real_point3d *cover, long *surface);
/* overlooks of a point (a hill, a ball): power positions 4 to 16 u away that
see it, else rings of points 9, 6, 4 u out */
boolean pgbots_map_overlook(real_point3d const *point, long which, real_point3d *overlook, long *surface);
/* the power value of an item (rockets 1.0, sniper 0.8, overshield 0.9,
camouflage 0.7, fuel rod 0.6; 0 for others) */
real pgbots_map_item_power(long object_index);

/* pgbots_director.c */
void pgbots_director_reset(void);
void pgbots_director_update(void);
/* a newly recognised enemy posted to the bot's team (the skill's chance, one
post a team every 0.5 s) */
void pgbots_director_post(struct pgbots_bot *bot, long player_index, real_point3d const *point);
/* the blackboard heard: the team's entries become callout beliefs after the
skill's hear delay */
void pgbots_director_hear(struct pgbots_bot *bot);
/* a death of the team's, in its area (the threat map) */
void pgbots_director_death(short team, real_point3d const *point);
real pgbots_director_posture(struct pgbots_bot const *bot);
long pgbots_director_focus(short team);
/* the objective's point for the bot's mode (flag, hill, ball, checkpoint),
whether it carries it; FALSE for no objective */
boolean pgbots_director_objective(struct pgbots_bot *bot, real_point3d const *position, real_point3d *point,
	boolean *carrying, boolean *grab_near, boolean *stand_unguarded);
boolean pgbots_director_carrier(long player_index);
void pgbots_director_report(long now);

/* pgbots_drive.c */
void pgbots_drive_reset(struct pgbots_bot *bot);
/* a vehicle worth boarding for this trip: its score (0 for none), the
vehicle and seat */
real pgbots_drive_board_score(struct pgbots_bot *bot, real_point3d const *position, real trip, long *vehicle_index,
	short *seat);
/* on foot, going for a vehicle's seat: the walk there and in (TRUE while
it goes) */
boolean pgbots_drive_board(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *position,
	boolean *moving, real *direction, unsigned long *flags);
/* riding: driving, gunning or along, and out */
void pgbots_drive_update(struct pgbots_bot *bot, struct player_datum *player, long vehicle_index,
	struct player_action *action, unsigned long *flags, boolean *moving, real *direction, real *throttle);

/* pgbots.c's fight, for the drivers and gunners too: the target chosen,
the hands' aim at it (from an eye, with a projectile speed) and the trigger */
long pgbots_choose_target(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *eye);
boolean pgbots_aim_and_fire(struct pgbots_bot *bot, struct player_datum *player, real_point3d const *eye,
	long weapon_index, real projectile_speed, real window_scale, unsigned long *flags);
struct pgbots_belief *pgbots_belief(struct pgbots_bot *bot, long player_index);
/* a belief from a source (its time `now`: a callout's is when it was posted) */
void pgbots_believe(struct pgbots_bot *bot, long player_index, real_point3d const *point,
	real_vector3d const *velocity, short source, long now);
boolean pgbots_target_visible(struct pgbots_bot *bot, long now);
/* a path to a goal (asked, taken, followed): the direction to walk; FALSE
with none yet (or there) */
boolean pgbots_walk(struct pgbots_bot *bot, long unit_index, real_point3d const *position, real_point3d const *goal,
	long goal_surface, real radius, long search_unit, long target_object, real reached, real *direction);
void pgbots_path_forget(struct pgbots_bot *bot);

#endif
