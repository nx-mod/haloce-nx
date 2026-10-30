/*
BOTS.C

Computer players (bots) for multiplayer games (bots.count): the lobby of a
game hosted here takes as many as the setting asks for
(network_server_manager.c adds them), and each plays as a player does -
spawns, respawns, scores, is in the scores and the game's rules - with its
controls from here instead of a controller: the game tick takes every bot's
action from bots_update_actions (players_update_before_game), where a
player's comes from its pad. A bot has no view: nothing is drawn for it.

The paths beyond the engine's own (teleporters, ladders, shortcuts), the
vantage points, routes and roles (bots_vantage.c, bots_routes.c,
bots_roles.c through bots_tactics.h), the vehicles, turrets and powerups are
halo-ce-vita's (BirchWoodGod's PS Vita port of this decompilation, GPL-3.0),
this file the Vita's with this port's own bots' rules (Quake III's capture
the flag and king of the hill, Infection's and SWAT's, their aim) and their
network play kept as they were.

What a bot does each tick, in a player's terms:

- It looks for enemies (every few ticks, bots in turn): the nearest player
  of the other team (anyone in a game without teams) within its sight's
  reach and field of view whom a line of sight reaches (the AI's own sight
  test: the level, vehicles, scenery and machines block it), nearer for one
  in active camouflage; and it notices who shot it. A flag's carrier, and
  whoever stands on the hill, it fights before anyone.
- It reacts after a moment (its skill's reaction time), then turns towards
  the target at a limited speed, with an aim error that settles while it
  keeps the target in sight (and leads a moving target by its weapon's
  projectile speed), and fires in bursts while its aim is on the target
  (released between bursts, so semi-automatic weapons fire too); it throws
  a grenade now and then at middle range and strikes with its weapon close
  up. It strafes while fighting, jumps now and then, and keeps its distance.
  By its skill it aims at the head with a weapon whose headshots count (the
  pistol and the sniper rifle at an unshielded target, always from normal;
  every weapon in SWAT), and at the feet with one whose blast hurts
  (rockets, the fuel rod). How it reacts, aims, fires, strikes, throws and
  moves in a fight is its level's (below: "Fighting by level").
- Where it goes, in turn: a flag it carries, home; a vehicle it goes for; a
  health pack when hurt, an overshield or camouflage near; capture the
  flag's urgent goals (its own flag carried off or dropped); a vantage point
  to hold; in Slayer, where an enemy was last seen; a better weapon near;
  the game's own goals by Quake III Arena's bots' rules (bots_ctf_choose_goal,
  bots_king_choose_goal) and Infection's (bots_infection_choose_goal); else
  about the map (the player starting locations, the weapon and item
  spawns), most often towards an enemy.
- Powerups (port): hurt - its body below its skill's mark, lower in a fight
  (where an easy bot never leaves it) - it goes for a health pack lying on
  the map (45 units at most, 15 in a fight, backing to it as it fights),
  before the game's goals but for the flag or the ball it carries; an
  overshield or camouflage near (12 units in a game with an objective) the
  same. A pack is an object only while it has spawned, so a bot never goes
  for one the game has yet to bring back; one another bot nearer goes for
  is that bot's; one it finds no way to, or stood at and did not get, it
  leaves be a while (a few remembered at once). The map's powerups are
  found once a second for every bot.
- It walks the paths the campaign's AI finds: the engine's own pathfinding
  over the level's walkable surfaces (source/ai/path.c), which every stock
  multiplayer map carries (tool builds it with the structure BSP: Blood
  Gulch has 2807 walkable surfaces of 4916; only the AI's placements,
  encounters and firing positions, are missing). A bot that stops moving
  jumps and sidesteps, then finds another path. The multiplayer maps were
  never made for the AI, and the bots' searches read their own copy of the
  walkable surfaces: those with no room above them (a floor going on under
  a wall: Longest's bases) closed a few a tick from the game's start, and
  those under scenery or a machine the path's obstacle avoidance once found
  no way past (Sidewinder's blast door between the bases) closed from then
  on. A goal on ground the walkable surfaces do not reach is gone to
  through a teleporter (Chiron TL34's rooms), else as near as they go and
  on with jumps; an item on a ledge is left be. Bots in each other's way
  give way by their order.
- Vehicles (bots.vehicles 0, HALO_BOT_VEHICLES=0, leaves them on foot):
  a bot whose goal is a long way off (35 units: Blood Gulch, Sidewinder)
  goes for an empty Warthog, Ghost or Scorpion on its way there, as driver,
  or for a Warthog's gunner's (else passenger's) seat when a teammate, bot
  or player, drives it (the seats as the campaign's AI weighs them,
  action_vehicle.c); a bot driver waits while a teammate it saw coming gets
  in. Port: a vehicle is worth what its kind is, as players weigh them - a
  Scorpion a walk of 50 units, back to it too when it is near (Blood Gulch's
  stand behind the bases, which bots leaving for the other flag never went
  back for), a Ghost 40, a Warthog 30 and only on the way - and a bot fresh
  from its base goes back for its base's vehicles; a bot much nearer to a
  seat a teammate bot set off for takes it over.
  It gets in where and when a player could (still at the seat's
  entrance: players.c's player_examine_nearby_vehicle), into the seat it
  chose, as the campaign's AI does (unit_enter_seat); a vehicle on its side
  it rights first, with the action button. A driver drives the same path
  searches over the walkable surfaces, as wide as the vehicle's own
  pathfinding radius (actor_moving.c's), steering as a player does, by
  where it looks: on to the game's objective while it fights, a driver that
  is also the gunner (Ghost, Scorpion) fighting from it otherwise, a
  Warthog's driver going round the enemy for its gunner (or at a lone enemy,
  to run it down); gunners and passengers shoot. One bot to a seat (a seat a
  bot goes for is its own), so no two go for the same one. Stuck, a driver
  backs off and turns, then goes on; scenery or a machine it drives into is
  learnt to be in the way (Sidewinder's blast door), and the level it drives
  into closed to the vehicles' searches alone (the banks a biped walks up;
  port: what is before it looked for past its front and low at its treads,
  and closed as far about as it is wide: a Scorpion's 2.2 units).
  A bot gets out when its vehicle lies on its side, sticks fast, has no
  driver, or near its goal when that wants it on foot (a flag, a hill, the
  ball), and leaves a vehicle that failed it alone a while. halo.log has
  the game's vehicles (port), each entry and exit, and every ten seconds'
  census of them (bots riding, driving, gunning, the distance driven,
  getting in and out and why, kills riding; port: the entries by kind, the
  first's tick, the seconds riding, the seats taken over and the vehicles
  given up on the way).
  Turrets (port): a vehicle that does not move - the vehicle type turret,
  or one with a gunner's seat and no driver's (Custom Edition maps' gun
  towers and Shades; no stock map has one) - is manned by a bot on foot
  within 12 units of it with an enemy seen in the last five seconds no
  further than 60 from it, the turret's seat gone for as any other; it
  fights from it (a gun whose trigger charges held until charged) and gets
  off when no enemy has been before it six seconds, when the gun does not
  turn to the one it fights (out of its arc) for two, or when it is being
  killed on it, and leaves that turret be a while.
- Ladders (port): Halo has climbable surfaces (collision_bsp's flag; a
  biped that jumps onto one it faces goes up it: bipeds.c) - Boarding
  Action has sixteen ladders, Wizard four, Longest two - and the walkable
  surfaces never
  go up one, so a ladder (its foot and the landing at its top found once a
  map) is to the searches as a teleporter: gone to when the goal is beyond
  the ground the search walks, climbed facing it with a jump now and then,
  then on to the landing. One no bot gets up after three tries (Damnation's
  climbable pipes) is left be. There is no other climbing: a stuck bot
  jumps and sidesteps as before (halo.log's moves line counted what stood
  before stuck bots: low walls a jump clears were a few in a hundred).
- Teleporters as shortcuts (port; bots_shortcut_weigh): a bot on foot whose
  goal the walkable surfaces do reach walked there before, through no
  teleporter however much shorter (Blood Gulch's, from beside each base's
  flag out to the middle of the canyon; Battle Creek's between the bases).
  Now, as a search to its goal answers, it weighs the map's teleporters
  (each source flag with the first target of its channel, the one the game
  sends a player to): the walk to an entrance and on from its exit against
  the walk the search found; the best, 80% of the walk at most and 15 units
  shorter, is weighed again with the walk from its exit measured by a search
  of its own, and taken: the bot goes onto the entrance and on from where
  it comes out. Never driving, never a defender, nor a bot on a side lane;
  to the objective, a roaming place or an enemy last seen alone (items,
  powerups and vehicles come first - a bot fresh from its base with a
  vehicle near weighs none for its first ten seconds - a fight stops its
  clock); two bots to a teleporter at most at once. One that fails a bot (not sent from its
  entrance, or not come to in time) is left be thirty seconds, for the map
  after three failures and none sent.
- Roles, routes and vantage points (bots_roles.c, bots_routes.c and
  bots_vantage.c, steering the bot through bots_tactics.h's hooks here: its
  goal, its searches' goal, its look and its fight). A team's defenders are
  Quake III's (capture the flag's, the hill's guards: bots_quake_defender);
  Slayer's bots roam; the rest attack. Bot routes (bots.routes): Direct, the
  default, everyone the shortest way; Lanes spreads each team's attackers
  over the direct route and the map's two side lanes in turn, Mixed every
  third of them on a side lane. Vantage points: a bot holding a precision
  weapon (bots_weapon_precision, from the weapon's tag: a sniper rifle; a
  pistol, a roamer's, one near; a rocket launcher) holds one a while (a third
  of a team at once, half with sniper rifles; a capture the flag defender's
  sniper rifle over its own stand), looking from one place it sees to
  another, and leaves it when an enemy comes near, when out of ammunition,
  hurt, when the weapon is dropped or swapped, when its flag comes first, or
  after a while; none in king of the hill or Infection, whose bots keep to
  their own rules. The lanes and vantage points are found as each game's bots
  first tick, a bounded step a tick, and logged.

Skill (bots.skill): recruit, easy, normal, heroic, veteran, legendary - how
soon it reacts, how far and how wide it sees, how large its aim error is and
how fast that settles, how fast it turns, how tight its shots must be before
it fires, its bursts and its grenades, how often it goes for the head and
the feet, how hurt it is before it goes for a health pack. easy, normal,
heroic and legendary are the levels the bots always had; recruit (for new
players) is below easy, and veteran is halfway from heroic to legendary.

Fighting by level: a bot fights as a player of its level does, by
bots_combat.c's rules (OpenCE's, from Halo CE: Arena Evolved's bots) - a
reaction and a look that are never the same twice, an aim that trails the
target at the lowest levels and keeps an error at range, headshots once the
shields are down, the pistol paced at range and the sniper rifle zoomed (again,
after a hit), blows and grenades by level (a throw that leads the target's
path, one at the spot it was last seen), strafes and halts of the level's
rhythm (short jiggles at the top), jumps and crouches, a step in for a blow,
a run in to finish a won fight, a retreat along a path when losing (as its
shields pop, at the top), no sight of players far above or below where it
looks, and high ground when it roams. A zombie keeps Infection's own fight,
and a bot on the hill, carrying the flag or the ball, or holding a vantage
point neither retreats nor runs in. halo.log's minute line is followed by
the fight's counts.

The simulation: a bot decides from the game state at the tick and its own
random numbers (seeded per bot and game), and its path searches run in the
tick (HALO_BOT_THREAD=1 runs them on a helper thread instead, answered a tick
or more later), so the bots do the same in two runs of the same input.
Nothing of a bot is in the game state: multiplayer games are never saved.
Bots are in local, system link and private internet games
(bots_game_allowed), not public ones: on the host alone, whose tick writes
each bot's action into the update it ran (update_server_note_bot_action),
from which the distributed netcode relays it to the clients as any player's
input; they drive the bot with it, and take its unit's state (and the seat
it sits in) from the host as any player's. No message of the network
protocol changes. halo.log has each minute of play's bots lines (searches,
moves, vehicles, tactics, capture the flag's and king of the hill's state);
debug.bot_trace logs each bot's state.
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
#include "units/vehicle_definitions.h"
#include "items/items.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "items/equipment.h"
#include "items/equipment_definitions.h"
#include "physics/collisions.h"
#include "physics/breakable_surfaces.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "networking/network_game_globals.h"
#include "ai/path.h"
#include "devices/devices.h"
#include "devices/device_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"

#include "bots.h"
#include "bots_combat.h"
#include "bots_tactics.h"
#include "game_modes.h"
#include "infection.h"
#include "game/player_queues_new.h"

/* p2p.c's, p2p_lobby.c's */
int p2p_hosting_internet(void);
int p2p_hosting_listed(void);

/* capture the flag's and king of the hill's (below) */
struct bots_bot;
static boolean bots_ctf_active(void);
static void bots_ctf_update(void);
static boolean bots_ctf_carrying(struct bots_bot *bot);
static boolean bots_king_active(void);
static boolean bots_king_on_hill(long unit_index);
static boolean bots_zombie(struct player_datum *player);
static boolean bots_king_guard(struct bots_bot *bot, struct player_datum *player);

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's */
const char *config_string(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);
/* the maps the bots were readied for, counted (bots_initialize_for_new_map):
what is kept a map (the weapons' kinds, the powerups, the ground memo) is
found again on the next */
static unsigned long bots_map_generation;

/* whether each bot's state is logged (debug.bot_trace, HALO_BOT_TRACE; bots_tactics.h) */
boolean bots_trace_enabled(void)
{
	static int trace = -1;

	if (trace < 0)
		trace = config_integer("debug.bot_trace") > 0;
	return trace != 0;
}

/* now, in microseconds, as the game's clock has it (to the millisecond:
halo.log's timings alone; bots_tactics.h) */
unsigned long system_milliseconds(void);
unsigned long long bots_time_us(void)
{
	return (unsigned long long)system_milliseconds() * 1000ULL;
}
/* path.c's, which path.h declares for the flee routines alone: the path goes
up to the object (a vehicle to get into), not round it */
void path_input_set_target_object(struct path_input *input, long target_object_index);
/* units.c's: where a unit stands to get into a seat (the seat's entrance),
the seat, and the entrance's hint */
boolean unit_get_seat_entrance_point(long unit_index, long target_unit_index, short seat_index,
	real_point3d *entry_position, real_point3d *exit_position, real_point3d *seat_transform);

/* ---------- constants */

#define BOTS_PI 3.14159265f
#define BOTS_DEGREES (BOTS_PI / 180.0f)
/* the most places a bot roams to (player starting locations, item spawns) */
#define BOTS_MAXIMUM_ROAM_POINTS 96
/* the path searches asked for at once, whose answers wait for their bots
(a search's state is 80 KB: the engine's 1024 nodes, heap and table) */
#define BOTS_SEARCH_SLOTS 3
/* the bots' size as the path searches know it (a biped's radius) */
#define BOTS_PATH_RADIUS 0.25f
/* the most teleporters a map's bots use (Chiron TL34 has 30), and ladders
(port: Boarding Action has 12) */
#define BOTS_MAXIMUM_TELEPORTERS 64
/* how near a path's step a bot is when it has reached it (across) */
#define BOTS_STEP_REACHED 0.35f
/* a pathfinding surface's flag: walkable (path.c's) */
#define BOTS_PATHFINDING_WALKABLE_BIT 6
/* a collision surface's flag: climbable (collision_bsp.c's) */
#define BOTS_COLLISION_SURFACE_CLIMBABLE_BIT 2
/* vehicles: a bot goes for one when its goal is further than this, the
vehicle no further than its type's reach (bots_vehicle_worths: a Warthog's
30, a Ghost's 40, a Scorpion's 50) */
#define BOTS_VEHICLE_GOAL_DISTANCE 35.0f
/* port: the seconds after it spawns that a bot is fresh from its base: a
vehicle there is gone for further out of its way (bots_vehicle_consider) */
#define BOTS_VEHICLE_FRESH_SECONDS 10
/* port: how much nearer a bot is to a seat another bot goes for before it
takes the seat over (the other let go of it: bots_vehicle_consider) */
#define BOTS_VEHICLE_TAKE_OVER 15.0f
/* how near a driven path's step a vehicle is when it has reached it */
#define BOTS_DRIVE_STEP_REACHED 2.5f
/* how near its goal a bot riding gets out, when the goal wants it on foot */
#define BOTS_VEHICLE_GOAL_EXIT 10.0f
/* port: a biped's knees and chest above its feet (a unit is three metres:
the Master Chief stands 0.7), the heights a stuck bot looks for a wall at;
a wall below the chest is one a jump clears */
#define BOTS_KNEE_HEIGHT 0.15f
#define BOTS_CHEST_HEIGHT 0.4f
/* port: the items a bot remembers it found no way to, at once (one before:
a weapon set aside put back the powerup set aside before it, gone for again
every second - Sidewinder's overshield and camouflage under the bridge) */
#define BOTS_UNREACHABLE_ITEMS 8
/* port: turrets: a bot on foot mans one no further than this, the enemy it
saw no further from the turret than this; and the most turrets kept */
#define BOTS_TURRET_REACH 12.0f
#define BOTS_TURRET_RANGE 60.0f
#define BOTS_MAXIMUM_TURRETS 16
/* port: teleporters as shortcuts (bots_shortcut_weigh): taken when the way
through (the walk to the entrance, and from the exit to the goal) is no more
than this share of the walk, and this many units shorter at least; a bot
weighs them again no sooner than this after (and not for the same goal);
at most this many bots go for one teleporter at once */
#define BOTS_SHORTCUT_SHARE 0.8f
#define BOTS_SHORTCUT_GAIN 15.0f
#define BOTS_SHORTCUT_WEIGH_SECONDS 5
#define BOTS_SHORTCUT_BOTS 2
/* port: a teleporter that failed a bot (it stood on the entrance and was
not sent, or did not come to it in time) left be this long; one that
failed three times and never sent a bot, left be for the map */
#define BOTS_SHORTCUT_REST_SECONDS 30

/* the fight's counts in halo.log's minute line */
enum
{
	_bots_combat_count_grenades = 0,
	_bots_combat_count_blows,
	_bots_combat_count_blows_held,
	_bots_combat_count_head_aims,
	_bots_combat_count_retreats,
	_bots_combat_count_charges,
	_bots_combat_count_jumps,
	_bots_combat_count_halts,
	_bots_combat_count_paced,
	_bots_combat_count_rescopes,
	_bots_combat_count_prenades,
	_bots_combat_count_high_ground,
	_bots_combat_count_out_of_view,
	NUMBER_OF_BOTS_COMBAT_COUNTS,
};

/* where a bot is going */
enum
{
	_bots_goal_none = 0,
	_bots_goal_objective,
	_bots_goal_enemy,
	_bots_goal_roam,
	/* a better weapon, or a powerup, to pick up */
	_bots_goal_item,
	/* a vehicle's seat to get into (or a vehicle on its side to right) */
	_bots_goal_vehicle,
};

/* port: what stands before a stuck bot (bots_stuck_obstacle) */
enum
{
	_bots_obstacle_none = 0,
	_bots_obstacle_low,
	_bots_obstacle_tall,
};

/* port: the powerups a bot goes for (a goal _bots_goal_item's), and those
it took (halo.log's moves line) */
enum
{
	_bots_powerup_none = 0,
	_bots_powerup_health,
	_bots_powerup_overshield,
	_bots_powerup_camouflage,
	NUMBER_OF_BOTS_POWERUPS,
};

/* a vehicle's seat, as a bot takes it */
enum
{
	_bots_seat_driver = 0,
	_bots_seat_gunner,
	_bots_seat_passenger,
	NUMBER_OF_BOTS_SEATS,
};

/* why a bot got out of a vehicle (halo.log's census) */
enum
{
	_bots_exit_none = 0,
	_bots_exit_flipped,
	_bots_exit_stuck,
	_bots_exit_goal,
	_bots_exit_driverless,
	/* thrown out, or the vehicle gone */
	_bots_exit_other,
	/* port: a turret's: no enemy before it a while, or none its gun turns
	to; and its gunner hurt (being killed on it) */
	_bots_exit_no_enemy,
	_bots_exit_hurt,
	NUMBER_OF_BOTS_EXITS,
};

/* the vehicle types (vehicles.c's vehicle_type) a bot drives: on the ground */
enum
{
	_bots_vehicle_type_human_tank = 0,
	_bots_vehicle_type_human_jeep = 1,
	_bots_vehicle_type_alien_scout = 4,
	/* port: a gun on a mount, which does not move */
	_bots_vehicle_type_turret = 6,
};

/* a path search's state (the helper thread's hand-off) */
enum
{
	_bots_search_idle = 0,
	_bots_search_waiting,
	_bots_search_running,
	_bots_search_done,
};

/* port: what a path search is for: the bot's path, the walk from a
teleporter's exit to the bot's goal (measured, not walked: bots_shortcut_
weigh), or the bot's path to the teleporter's entrance it takes */
enum
{
	_bots_search_for_path = 0,
	_bots_search_for_exit,
	_bots_search_for_entrance,
};

/* port: a bot's teleporter shortcut (bots_shortcut_weigh): none, the walk
from its exit being measured, or on the way to its entrance */
enum
{
	_bots_shortcut_none = 0,
	_bots_shortcut_measuring,
	_bots_shortcut_going,
};

/* port: how a bot came to go through a teleporter (halo.log's census) */
enum
{
	_bots_through_shortcut = 0,
	_bots_through_last_way,
	_bots_through_by_chance,
	NUMBER_OF_BOTS_THROUGH,
};

/* ---------- structures */

struct bots_skill_definition
{
	char const *name;
	/* seconds from first seeing an enemy to firing */
	real reaction_seconds;
	/* how far it sees an enemy (world units), and how wide (degrees) */
	real sight_distance;
	real field_of_view_degrees;
	/* the aim error on taking a target (degrees), and the seconds it takes
	to settle to a third */
	real aim_error_degrees;
	real aim_settle_seconds;
	/* the most it turns a tick (degrees) */
	real turn_degrees_per_tick;
	/* fires while the aim is within this many times the target's size */
	real fire_cone;
	/* how much of a moving target's lead it takes (0 none, 1 all) */
	real lead;
	/* the chance a second of throwing a grenade at middle range (normal's;
	bots_combat_grenade's rules start from it) */
	real grenade_chance;
	/* the ticks of a burst, and of a pause between bursts */
	short burst_ticks;
	short pause_ticks;
	/* the chance it aims at the head with a weapon whose headshots count (the
	pistol and the sniper rifle at an unshielded target; SWAT's every
	weapon), and at the feet with one whose blast hurts (rockets, the fuel
	rod: Quake III's aim at the ground beside a target for splash damage),
	taken once a target and again every couple of seconds */
	real headshot_chance;
	real feet_chance;
	/* the body's vitality (0 to 1) below which it goes for a health pack,
	out of a fight and in one (0: never); and how far it goes for an
	overshield or camouflage */
	real heal_below;
	real heal_below_fighting;
	real powerup_reach;
};

/* the levels (bots_combat.h's order): easy, normal, heroic and legendary
are the bots' first levels as they were (their jumps in a fight are now
bots_combat_jump_delay's); recruit is below easy, and veteran halfway from
heroic to legendary in every number */
static struct bots_skill_definition const bots_skills[NUMBER_OF_BOTS_SKILLS] =
{
	{ "recruit", 1.4f, 16.0f, 80.0f, 22.0f, 2.5f, 5.0f, 2.6f, 0.0f, 0.0f, 4, 24, 0.05f, 0.0f, 0.25f, 0.00f, 10.0f },
	{ "easy", 0.75f, 22.0f, 100.0f, 12.0f, 1.4f, 8.0f, 2.2f, 0.3f, 0.04f, 6, 14, 0.15f, 0.0f, 0.35f, 0.00f, 15.0f },
	{ "normal", 0.45f, 32.0f, 120.0f, 7.0f, 0.9f, 13.0f, 1.8f, 0.7f, 0.10f, 9, 9, 0.4f, 0.4f, 0.50f, 0.25f, 25.0f },
	{ "heroic", 0.30f, 42.0f, 140.0f, 4.0f, 0.6f, 20.0f, 1.5f, 0.8f, 0.16f, 12, 6, 0.7f, 0.8f, 0.65f, 0.35f, 30.0f },
	{ "veteran", 0.24f, 48.0f, 150.0f, 3.1f, 0.48f, 25.0f, 1.4f, 0.9f, 0.19f, 14, 5, 0.85f, 0.9f, 0.725f, 0.40f, 35.0f },
	{ "legendary", 0.18f, 55.0f, 160.0f, 2.2f, 0.35f, 30.0f, 1.25f, 1.0f, 0.22f, 16, 4, 1.0f, 1.0f, 0.80f, 0.45f, 40.0f },
};

/* the ticks of its target's places a bot keeps (the aim that trails it) */
#define BOTS_TRACK_TICKS 16

/* a bot's fight by level (bots_combat.c's rules), started over at each
spawn */
struct bots_bot_combat
{
	struct bots_combat_retreat retreat;
	struct bots_combat_crouch crouch;
	/* the end of the strafe it last saw (a new strafe: a halt, maybe), and
	the halt's end */
	long last_strafe_until_tick;
	long halt_until_tick;
	/* the fight's last tick (a new fight after 2 s without one), and its
	next jump */
	long last_fight_tick;
	long next_jump_tick;
	/* the tick it strikes, waiting within reach (NONE: not waiting) */
	long melee_ready_tick;
	/* it runs in to finish its target */
	boolean charging;
	/* it aims at its target's head */
	boolean head_aim;
	/* a grenade in its hand until then: the aim leads the target's path by
	this much */
	long throw_until_tick;
	real throw_lead;
	/* a hit knocked its zoom out: no zoom (and past 10 units no shot) until
	then */
	long rescope_until_tick;
	/* the tick it began holding a shot for its weapon's error to settle
	(NONE: not holding) */
	long pace_since_tick;
	/* the throw at the spot its target was last seen: the sighting it was
	weighed for, and the aim held on the spot until then */
	long prenade_seen_tick;
	long prenade_until_tick;
	real_point3d prenade_point;
	/* its target's places, a tick each */
	struct
	{
		long tick;
		long player_index;
		real_point3d point;
	} track[BOTS_TRACK_TICKS];
};

/* the vehicle tag's fields a bot drives by (vehicle_definitions.h keeps the
tag opaque; the places are those actor_moving.c's fragment pins for the
campaign's drivers) */
struct bots_vehicle_definition
{
	byte unit[0x2F4];
	short vehicle_type;
	short pad2f6;
	byte unknown2f8[0x94];
	real ai_pathfinding_radius;
};
typedef char bots_vehicle_type_offset_assert[
	offsetof(struct bots_vehicle_definition, vehicle_type) == 0x2F4 ? 1 : -1];
typedef char bots_vehicle_pathfinding_radius_offset_assert[
	offsetof(struct bots_vehicle_definition, ai_pathfinding_radius) == 0x38C ? 1 : -1];

/* a path search, for the helper thread (or the tick) */
struct bots_search
{
	volatile long state;
	/* the asking: from where, to where, by whom (the unit, or the vehicle
	it drives: what the search ignores in the way), how wide, and the object
	at the goal the path goes up to (a vehicle to get into), NONE for none */
	long bot_index;
	long unit_index;
	real radius;
	long target_object_index;
	real_point3d start_point;
	long start_surface_index;
	real_point3d goal_point;
	long goal_surface_index;
	/* asked at this tick, for this goal (the bot's goal serial); port: and
what for (_bots_search_for_path ...) */
	long asked_tick;
	long goal_serial;
	short purpose;
	/* the answer */
	boolean found;
	struct path_state state_memory;
};

struct bots_bot
{
	long player_index;
	long unit_index;
	unsigned long random;

	/* where it looks (radians) */
	real yaw;
	real pitch;

	/* its target */
	long target_player_index;
	long target_seen_tick;
	long target_first_seen_tick;
	real_point3d target_last_position;
	long target_last_surface_index;
	real aim_error_yaw;
	real aim_error_pitch;
	long next_look_tick;
	/* the last time it was hurt, and its vitality then */
	real last_vitality;
	long hurt_tick;

	/* where it is going */
	short goal_kind;
	long goal_serial;
	real_point3d goal_point;
	long goal_surface_index;
	long goal_tick;
	/* the goal must be stood on (a flag stand, a flag to return): the last
	bit walked straight */
	boolean goal_exact;
	short roam_index;
	/* the roaming place is towards an enemy (looked at again sooner) */
	boolean hunting;
	/* the item it fetches (_bots_goal_item), and when it last pressed the
	action button for it */
	long item_index;
	long item_press_tick;
	/* when it first pressed it for this item */
	long item_first_press_tick;
	/* port: the item a powerup (_bots_powerup_none for a weapon), given up
	on at this tick; when the bot first stood at it, and how far it was from
	it last tick (a powerup gone while it stood there: taken); the powerup it
	went for last (NONE once done with); when it next
	looks for one; its body's vitality last tick (a health pack's: up) */
	short item_powerup;
	long powerup_index;
	long item_deadline_tick;
	long item_arrived_tick;
	real item_last_distance;
	long powerup_check_tick;
	real last_body_vitality;
	/* the path there (steps of the engine's path), and when it was found */
	struct path_result path;
	long path_tick;
	long path_failures;
	/* off a surface closed to the searches (beside a door): away from what
	closed it, until then */
	long escape_until_tick;
	real escape_direction;
	/* its path goes to a teleporter (its goal is beyond, on ground the
	search does not walk to): onto it at the path's end, since then */
	boolean via_teleporter;
	real_point3d via_point;
	long via_tick;
	/* port: the teleporter (or ladder) it goes to; on a ladder: since when
	it climbs, 0 before, and since when it is over the top */
	long via_index;
	long via_climb_tick;
	long via_top_tick;
	boolean ladder_jump;
	/* port: a teleporter as a shortcut to a goal it could walk to
	(bots_shortcut_weigh): how far along (_bots_shortcut_none ...), which
	teleporter, for which goal (its serial, and where it was), the walk it
	was weighed against (as little as it is, for a search that stopped
	short), the way to its entrance as weighed (and whether a search to it
	has said how far it is: taken once it has) and the walk from its exit,
	measured; when it gives up going there, since when it stands on the
	entrance (0 off it); and when it next weighs the teleporters, the goal it
	last weighed them for */
	short shortcut_phase;
	long shortcut_index;
	long shortcut_serial;
	real_point3d shortcut_goal;
	real shortcut_walk;
	boolean shortcut_walk_short;
	real shortcut_entrance;
	boolean shortcut_entrance_known;
	real shortcut_beyond;
	long shortcut_tick;
	long shortcut_deadline_tick;
	long shortcut_on_tick;
	long shortcut_weigh_tick;
	long shortcut_weighed_serial;
	/* its path ends short of the goal (the search found no way there: a
	platform the walkable surfaces do not reach, Wizard's flags'); at its
	end, straight on with jumps until then, and the game's goals left a
	while after */
	boolean path_short;
	long climb_until_tick;
	long climb_start_tick;
	real climb_best_distance;
	long objective_rest_until_tick;
	/* where it was last tick (a teleporter's jump) */
	real_point3d last_position;
	/* an item it found no way to (on a ledge), not fetched again until then */
	long unreachable_item_indices[BOTS_UNREACHABLE_ITEMS];
	long unreachable_item_until_ticks[BOTS_UNREACHABLE_ITEMS];
	long unreachable_item_next;
	/* whether it moves, checked once a second */
	real_point3d stuck_position;
	long stuck_tick;
	short stuck_count;
	long unstick_until_tick;
	real unstick_direction;

	/* fighting: the side it strafes to and until when; bursts */
	real strafe_sign;
	long strafe_until_tick;
	long burst_until_tick;
	long pause_until_tick;
	long grenade_tick;
	long melee_tick;
	long item_check_tick;

	/* the buttons held last tick (a press is a tick held, then released) */
	unsigned long previous_control_flags;

	/* its path search (a slot of bots_globals.searches), NONE for none */
	long search_slot;

	/* the game's object it held last tick (halo.log's objective count): 0
	none, 1 a flag, 2 a ball */
	short held_objective;
	/* CTF: at the other team's stand last tick */
	boolean at_enemy_stand;

	/* vehicles: the vehicle and seat it goes for (NONE for none: a seat a
	bot goes for is its own), to right it instead (on its side), since when,
	where its seat's entrance is (and where the vehicle was then), when it
	last pressed the action button there and how often; when it next looks
	for one, none until then (just out of one), and one that failed it, not
	again until then */
	long vehicle_index;
	short vehicle_seat_index;
	boolean vehicle_flip;
	long vehicle_tick;
	real_point3d vehicle_entrance;
	real_point3d vehicle_entrance_from;
	long vehicle_press_tick;
	short vehicle_presses;
	long vehicle_check_tick;
	long vehicle_rest_until_tick;
	/* port: when it gives up on the vehicle it goes for (the walk there and
	a while: bots_vehicle_approach); since when its unit lives (spawned) */
	long vehicle_deadline_tick;
	long spawn_tick;
	long vehicle_avoid_index;
	long vehicle_avoid_until_tick;
	/* port: the vehicle it goes for is a turret (its gunner's seat, with no
	driver); when it next looks for one */
	boolean vehicle_turret;
	long turret_check_tick;
	/* riding: the vehicle (NONE on foot), its seat, since when, where it was
	last tick (the census's distance); whether it moves while driven
	(checked once a second), backing off until then; since when it has
	had no driver, or lain on its side; why the bot is getting out */
	long ride_vehicle_index;
	short ride_seat;
	long ride_tick;
	real_point3d ride_last_position;
	real_point3d ride_stuck_position;
	long ride_stuck_tick;
	short ride_stuck_count;
	boolean ride_driving;
	boolean ride_reversing;
	real ride_direction;
	long ride_reverse_until_tick;
	real ride_reverse_yaw;
	long ride_driverless_tick;
	long ride_flipped_tick;
	short ride_exit;
	/* port: it rides a turret; since when its gun has not turned to the
	enemy (out of its arc), 0 while it has */
	boolean ride_turret;
	long ride_turret_off_tick;
	/* since when it holds a turret's charging trigger, 0 while it does not */
	long ride_charge_tick;
	/* its kills counted so far (the census's kills riding) */
	long kills_seen;
	/* (debug: HALO_BOT_TRACE) its path searches */
	long searches;

	/* capture the flag's defender (Quake III's rules: bots_ctf_choose_goal):
	where about its flag it stands, until when; king of the hill's guard of
	the hill's approaches the same */
	real_point3d guard_point;
	long guard_surface_index;
	long guard_until_tick;
	/* where it aims at its target (the head, the feet, the middle), taken by
	its skill's chances once a target and again until then (bots_bot_fight) */
	long aim_choice_target;
	long aim_choice_until_tick;
	boolean aim_head;
	boolean aim_feet;
	/* king of the hill: the place on the hill it goes to, and until when; the
	hill it was for (bots_king_choose_goal) */
	real_point3d hill_point;
	long hill_until_tick;
	long hill_id;
	/* its fight by level (bots_combat.c's rules) */
	struct bots_bot_combat combat;
};

/* ---------- globals */

static struct
{
	boolean map_ready;
	long roam_point_count;
	real_point3d roam_points[BOTS_MAXIMUM_ROAM_POINTS];
	long roam_surfaces[BOTS_MAXIMUM_ROAM_POINTS];
	/* the teleporters: where each is entered (its ground and surface) and
	where it comes out; port: and the ladders (bots_prepare_ladders), entered
	at their foot and come out of at their top's landing, climbed facing
	them (the climb's direction) up to the top's height */
	long teleporter_count;
	struct
	{
		real_point3d entrance;
		long entrance_surface_index;
		real_point3d exit;
		boolean ladder;
		real climb_direction;
		real top_z;
		/* the times bots climbed it, and failed to (a climbable surface no
		biped goes up, Damnation's: left be after three, none up it); port: a
		teleporter's, the times bots went for it as a shortcut and it failed
		them (bots_shortcut_fail) */
		long climbs;
		long failures;
		/* port: a teleporter's channel (its source flag's team index), the
		exit's ground and surface (where a search measures the walk on from:
		bots_shortcut_weigh), when it is gone for again after a failure, and
		the times it sent a bot, by how the bot came to it (halo.log) */
		short channel;
		real_point3d exit_ground;
		long exit_surface_index;
		long rest_until_tick;
		long through[NUMBER_OF_BOTS_THROUGH];
	} teleporters[BOTS_MAXIMUM_TELEPORTERS];
	struct bots_bot bots[BOTS_MAXIMUM];
	/* the skill setting, read once a tick (bots_update_actions) for every
	bot (a lookup of the environment and the settings, twice a bot a tick
	before) */
	short skill;
	/* the fight's counts for halo.log */
	long combat_counts[NUMBER_OF_BOTS_COMBAT_COUNTS];
	/* the path searches (allocated once a game has bots) */
	struct bots_search *search_slots;

	/* the statistics of halo.log's line */
	unsigned long long tick_us;
	unsigned long long search_us;
	long ticks;
	long bot_ticks;
	long searches;
	long search_failures;
	/* searches that stopped short of the goal (a path to as near as they
	came), and those that ran out of nodes */
	long search_partials;
	long search_overflows;
	/* paths taken without the obstacle avoidance, which found no way */
	long search_unavoided;
	/* the paths whose smoothing went nowhere, taken unsmoothed */
	long search_unsmoothed;
	long flag_grabs;
	long ball_grabs;
	/* CTF: the times a bot stood at the other team's flag stand (with the
	flag there or not) */
	long stand_visits;
	long last_report_tick;
	/* vehicles: the bots' getting in, out (by why), the vehicles they righted,
	the distance their vehicles were driven, and the last census */
	long vehicle_entries;
	long vehicle_entries_by_seat[NUMBER_OF_BOTS_SEATS];
	long vehicle_exits[NUMBER_OF_BOTS_EXITS];
	long vehicle_flips;
	real driven_distance;
	/* the kills of bots riding (driving, and in the other seats), and the
	ticks a gunner or passenger bot held the trigger */
	long riding_kills[2];
	long riding_fire_ticks;
	/* the path searches bots driving asked for */
	long driving_searches;
	/* port: the bots' getting in by the vehicle's type (bots_vehicle_kind:
	tanks, jeeps, scouts - the Scorpion, the Warthog, the Ghost), the tick
	of the game's first (0 for none yet), and the ticks bots rode, all of
	them together */
	long vehicle_entries_by_kind[3];
	long first_entry_tick;
	long riding_ticks;
	/* port: the seats a bot took over from a teammate further from them,
	and the vehicles given up on the way (too long, or no way in) */
	long vehicle_take_overs;
	long vehicle_giveups;
	long last_census_tick;
	/* port: the game's vehicles logged (its first census) */
	boolean vehicles_listed;
	/* port: the bots' moves (halo.log's moves line, each minute): the times
	a bot stood stuck while it walked, by what was before it - a low wall
	(the level, scenery or a machine at its knees, nothing at its chest: a
	jump's height), a tall one, or nothing (another bot, a slope it slid
	on) - and the goals given up after three */
	long stuck_events;
	long stuck_low;
	long stuck_tall;
	long stuck_clear;
	long stuck_giveups;
	/* the level's climbable surfaces (a ladder: collision_bsp's climbable
	flag, which a biped walks up as it faces it: bipeds.c) */
	long climbable_surfaces;
	/* port: the ladders made of them, and the times a bot climbed one */
	long ladder_count;
	long ladder_tries;
	long ladder_climbs;
	/* port: the teleporters as shortcuts (bots_shortcut_weigh): the map's
	teleporters (not the ladders), the walks from an exit measured, the
	shortcuts taken, the bots sent through by how they came (a shortcut, the
	last way to a goal the walkable surfaces do not reach, by chance), those
	that failed (bots_shortcut_fail), and those dropped on the way (another
	goal, a vehicle, the entrance further than it was weighed) */
	long teleporter_shortcuts;
	long shortcut_measures;
	long shortcut_takes;
	long shortcut_through[NUMBER_OF_BOTS_THROUGH];
	long shortcut_failures;
	long shortcut_drops;
	/* port: the powerups bots went for and took (by kind), and the times a
	bot's body was restored (a health pack, gone for or walked over) */
	long powerups_sought[NUMBER_OF_BOTS_POWERUPS];
	long powerups_taken[NUMBER_OF_BOTS_POWERUPS];
	long bodies_restored;
	/* port: turrets: the bots' getting on one, their kills from one, and the
	ticks they held a turret's trigger */
	long turret_entries;
	long turret_kills;
	long turret_fire_ticks;
	/* the turrets on the map, found with the powerups (bots_powerups_find:
	once a second, a walk of the vehicles), in the objects' order */
	long turret_count;
	long turret_indices[BOTS_MAXIMUM_TURRETS];

	/* capture the flag: each team's flag as the tick began
	(bots_ctf_update), red's and blue's */
	struct
	{
		long tick;
		boolean present[2];
		real_point3d position[2];
		long carrier_player_index[2];
		boolean at_home[2];
		real_point3d home[2];
		boolean touch_returns;
		boolean home_to_score;
	} ctf;
	/* king of the hill: the hill as the tick began (bots_king_update) */
	struct
	{
		long tick;
		boolean present;
		real_point3d points[12];
		long point_count;
		real bottom, top;
		real_point3d center;
		long hill_id;
	} king;
} bots_globals;

/* port: the powerups lying on the map - health packs, overshields, active
camouflage - found once a second for every bot (a walk of the equipment; a
bot walked every item each second for one before), in the objects' order;
an item is there only while it has spawned (taken, it is gone until the
game spawns it again), so a bot never goes for one that is not there */
#define BOTS_MAXIMUM_POWERUPS 32
static struct
{
	unsigned long map_generation;
	long found_tick;
	long count;
	struct
	{
		long object_index;
		short kind;
		real_point3d position;
	} items[BOTS_MAXIMUM_POWERUPS];
} bots_powerups;

/* the walkable surfaces the bots' searches read: a copy of the structure's
pathfinding surfaces, less those learnt to be in the way (scenery and
machines) and those with no room above them */
static struct
{
	boolean active;
	struct structure_bsp const *source;
	struct structure_bsp structure;
	byte *walkable;
	/* port: the vehicles' copy, their searches': the same, less what a
	vehicle stuck at (a bank a biped walks up, a ledge's foot) */
	struct structure_bsp vehicle_structure;
	byte *vehicle_walkable;
	long vehicle_closed_count;
	/* the objects whose surfaces are closed */
	long learned[32];
	long learned_count;
	long closed_count;
	/* the surfaces looked at for room above them, and those closed */
	long scan_index;
	boolean scan_done;
	long roofed_count;
	unsigned long long scan_us;
	long scan_ticks;
} bots_paths;

/* the helper thread */
static struct
{
	boolean started;
	boolean synchronous;
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t wake;
	volatile long pending;
	volatile long generation;
	volatile unsigned long long helper_us;
} bots_helper = { FALSE, FALSE, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0 };

/* (HALO_TICK_PROFILE) the bots' tick by part, and the rays they cast, for
halo.log's bots line */
enum
{
	_bots_part_look = 0,
	_bots_part_fight,
	_bots_part_goal,
	_bots_part_take,
	_bots_part_walk,
	_bots_part_rest,
	_bots_part_scan,
	NUMBER_OF_BOTS_PARTS,
};

static struct
{
	int enabled;
	unsigned long long part_us[NUMBER_OF_BOTS_PARTS];
	/* rays down to the ground and lines of sight: cast, and their time */
	long below_rays;
	long sight_rays;
	unsigned long long below_us;
	unsigned long long sight_us;
} bots_profile = { -1 };

static int bots_profile_enabled(void)
{
	if (bots_profile.enabled < 0)
	{
		char const *setting = getenv("HALO_TICK_PROFILE");

		bots_profile.enabled = setting && atoi(setting) ? 1 : 0;
	}
	return bots_profile.enabled;
}

/* a part's time to here from *started, which moves on to now */
static void bots_profile_part(int part, unsigned long long *started)
{
	unsigned long long now;

	if (!bots_profile.enabled)
		return;
	now = bots_time_us();
	bots_profile.part_us[part] += now - *started;
	*started = now;
}

/* ---------- settings */

static long bots_setting_integer(char const *variable, char const *name)
{
	char const *value = getenv(variable);

	return value && value[0] ? atol(value) : config_integer(name);
}

static char const *bots_setting_string(char const *variable, char const *name)
{
	char const *value = getenv(variable);

	return value && value[0] ? value : config_string(name);
}

static long bots_count_override = -1;

void bots_set_count_override(long count)
{
	bots_count_override = count;
}

long bots_wanted_count(void)
{
	long count = bots_count_override >= 0 ? bots_count_override : bots_setting_integer("HALO_BOTS", "bots.count");

	return count < 0 ? 0 : count > BOTS_MAXIMUM ? BOTS_MAXIMUM : count;
}

short bots_skill(void)
{
	char const *name = bots_setting_string("HALO_BOT_SKILL", "bots.skill");
	short skill;

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		if (!strcmp(name, bots_skills[skill].name))
			return skill;
	}
	/* (a number, 0 to 3: the four levels the bots first had, easy to
	legendary) */
	if (name[0] >= '0' && name[0] <= '3' && !name[1])
	{
		static short const numbered[] = { _bots_skill_easy, _bots_skill_normal, _bots_skill_heroic, _bots_skill_legendary };

		return numbered[name[0] - '0'];
	}
	return _bots_skill_normal;
}

short bots_teams(void)
{
	return strcmp(bots_setting_string("HALO_BOT_TEAMS", "bots.teams"), "against") ? _bots_teams_even :
		_bots_teams_against;
}

/* whether this machine hosts a game bots may play in: a local (split screen)
one, one for the local network (system link), or one for internet play that
the server browser does not list (a private game, joined by its invite), whose
clients drive the bots with the input the host relays (network_distributed.c);
not a public one */
int bots_game_allowed(void)
{
	if (!global_network_game_server_get())
		return FALSE;
	return network_game_is_splitscreen_local() || !p2p_hosting_internet() || !p2p_hosting_listed();
}

int bots_machine_is_bot(long machine_index)
{
	return machine_index >= BOTS_FIRST_MACHINE && machine_index < BOTS_FIRST_MACHINE + BOTS_MAXIMUM;
}

static int bots_player_is_bot(long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	return player && player->local_player_index == NONE &&
		bots_machine_is_bot(player->network_player_data.machine_index);
}

void bots_network_player(long bot_index, struct network_player *player)
{
	/* (the Marines of Halo's campaign; eleven characters at most: past the fifteenth, each again with a number,
	"Johnson 2") */
	static char const *const names[] =
	{
		"Johnson", "Jenkins", "Mendoza", "Bisenti", "Stacker", "Polaski", "Chips", "Kojo",
		"Rawley", "Riley", "Wellsley", "Hall", "Fitzgerald", "Locklear", "Mitchell",
	};
	char name[16];
	long index;

	if (bot_index < (long)NUMBEROF(names))
		snprintf(name, sizeof(name), "%s", names[bot_index]);
	else
		snprintf(name, sizeof(name), "%.8s %ld", names[bot_index % NUMBEROF(names)], bot_index / NUMBEROF(names) + 1);

	csmemset(player, 0, sizeof(*player));
	for (index = 0; index < (long)NUMBEROF(player->name) - 1 && name[index]; index++)
		player->name[index] = (wchar_t)name[index];
	player->name[index] = 0;
	player->primary_color_index = NONE;
	player->icon_index = 0;
	player->machine_index = (char)(BOTS_FIRST_MACHINE + bot_index);
	player->controller_index = 0;
	player->team_index = 0;
	player->player_list_index = NONE;
}

/* ---------- small helpers */

static boolean bots_peaceful(void)
{
	static int peaceful = -1;

	if (peaceful < 0)
	{
		char const *setting = getenv("HALO_BOT_PEACEFUL");

		peaceful = setting && atoi(setting) ? 1 : 0;
	}
	return peaceful != 0;
}

/* whether bots take vehicles (bots.vehicles, the Vita's settings panel's
Bot vehicles; HALO_BOT_VEHICLES=0: never, the bots on foot as before; read
each time, as the panel changes it at any time) */
static boolean bots_vehicles_enabled(void)
{
	return bots_setting_integer("HALO_BOT_VEHICLES", "bots.vehicles") != 0;
}

static unsigned long bots_random(struct bots_bot *bot)
{
	/* (xorshift32) */
	unsigned long x = bot->random ? bot->random : 0x9E3779B9UL;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	x &= 0xFFFFFFFFUL;
	bot->random = x;
	return x;
}

/* 0 to 1 */
static real bots_random_real(struct bots_bot *bot)
{
	return (real)(bots_random(bot) & 0xFFFFFF) / (real)0x1000000;
}

/* -1 to 1 */
static real bots_random_signed(struct bots_bot *bot)
{
	return bots_random_real(bot) * 2.0f - 1.0f;
}

static real bots_angle_difference(real a, real b)
{
	real difference = (real)fmod(a - b, 2.0f * BOTS_PI);

	if (difference > BOTS_PI)
		difference -= 2.0f * BOTS_PI;
	else if (difference < -BOTS_PI)
		difference += 2.0f * BOTS_PI;
	return difference;
}

static real bots_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

static real bots_distance3d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

/* the answers of bots_surface_below's rays kept by the point they were cast
from, while the bots' tick runs (the stands, the nav points and the flags
lying still that the bots ask for every tick): a structure test's answer is a
function of the bsp, its flags, the breakable surfaces' state and the ray
alone (collisions.c's obstruction memo is kept so), and this one tests no
objects nor water, so a point asked again (the same bits) is answered as it
was while the bsp ((unsigned long)global_structure_bsp_index_get()), the map and the breakable
surfaces' flags (compared as each bots' tick starts; nothing in it breaks
one) are as they were */
#define BOTS_GROUND_MEMO_ENTRIES 64
static struct
{
	boolean active;
	unsigned long map_generation;
	unsigned long structure_bsp_generation;
	byte breakable_surface_flags[MAXIMUM_BREAKABLE_SURFACES_PER_MAP / 8];
	struct
	{
		boolean used;
		unsigned long point[3];
		long surface_index;
		real_point3d ground;
	} entries[BOTS_GROUND_MEMO_ENTRIES];
} bots_ground_memo;

/* at a bots' tick's start: the answers kept dropped if the level changed */
static void bots_ground_memo_begin(void)
{
	byte const *flags = breakable_surface_flags_get();
	long index;

	if (bots_ground_memo.map_generation != bots_map_generation ||
		bots_ground_memo.structure_bsp_generation != (unsigned long)global_structure_bsp_index_get() ||
		memcmp(bots_ground_memo.breakable_surface_flags, flags, sizeof(bots_ground_memo.breakable_surface_flags)))
	{
		for (index = 0; index < BOTS_GROUND_MEMO_ENTRIES; index++)
			bots_ground_memo.entries[index].used = FALSE;
		bots_ground_memo.map_generation = bots_map_generation;
		bots_ground_memo.structure_bsp_generation = (unsigned long)global_structure_bsp_index_get();
		memcpy(bots_ground_memo.breakable_surface_flags, flags, sizeof(bots_ground_memo.breakable_surface_flags));
	}
	bots_ground_memo.active = TRUE;
}

/* the surface of the level under a point (a step up included), and the
ground point there; NONE over nothing */
static long bots_surface_below(real_point3d const *point, real_point3d *ground)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -6.0f };
	unsigned long long started;
	unsigned long bits[3];
	long entry_index = NONE;
	boolean hit;

	if (bots_ground_memo.active)
	{
		unsigned long hash = 2166136261UL;
		long index;

		memcpy(bits, point, sizeof(bits));
		for (index = 0; index < 3; index++)
			hash = (hash ^ bits[index]) * 16777619UL;
		entry_index = (long)((hash >> 8) % BOTS_GROUND_MEMO_ENTRIES);
		if (bots_ground_memo.entries[entry_index].used && !memcmp(bots_ground_memo.entries[entry_index].point, bits, sizeof(bits)))
		{
			if (bots_ground_memo.entries[entry_index].surface_index != NONE && ground)
				*ground = bots_ground_memo.entries[entry_index].ground;
			return bots_ground_memo.entries[entry_index].surface_index;
		}
	}
	started = bots_profile.enabled > 0 ? bots_time_us() : 0;
	start.z += 0.4f;
	hit = collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&start, &down, NONE, &collision) && collision.type == _collision_result_structure;
	if (started)
	{
		bots_profile.below_rays++;
		bots_profile.below_us += bots_time_us() - started;
	}
	if (entry_index != NONE)
	{
		bots_ground_memo.entries[entry_index].used = TRUE;
		memcpy(bots_ground_memo.entries[entry_index].point, bits, sizeof(bits));
		bots_ground_memo.entries[entry_index].surface_index = hit ? collision.surface_index : NONE;
		bots_ground_memo.entries[entry_index].ground = collision.point;
	}
	if (hit)
	{
		if (ground)
			*ground = collision.point;
		return collision.surface_index;
	}
	return NONE;
}

/* the surface a unit stands on (a biped's support surface), and its point */
static long bots_unit_surface(long unit_index, real_point3d *point)
{
	struct object_datum *object = object_get(unit_index);
	struct biped_datum *biped = biped_try_and_get(unit_index);

	*point = object->object.position;
	if (object->object.parent_object_index != NONE)
		return NONE;
	if (biped && biped->biped.support_surface_index != NONE)
		return biped->biped.support_surface_index;
	return bots_surface_below(&object->object.position, point);
}

/* the vehicle the unit rides in, NONE for none */
static long bots_unit_vehicle(long unit_index)
{
	struct object_datum *object = object_try_and_get(unit_index);

	if (!object || object->object.parent_object_index == NONE ||
		!object_try_and_get_and_verify_type(object->object.parent_object_index, _object_mask_vehicle))
	{
		return NONE;
	}
	return object->object.parent_object_index;
}

static struct bots_vehicle_definition *bots_vehicle_definition(long vehicle_index)
{
	return (struct bots_vehicle_definition *)tag_get(VEHICLE_DEFINITION_TAG, object_get(vehicle_index)->definition_index);
}

/* how wide a vehicle is to the path searches (its definition's AI
pathfinding radius, as the campaign's drivers search: actor_path_input_new) */
static real bots_vehicle_path_radius(long vehicle_index)
{
	real radius = bots_vehicle_definition(vehicle_index)->ai_pathfinding_radius;

	return radius > 0.1f ? MIN(radius, 2.0f) : 1.0f;
}

/* a vehicle a bot drives: on the ground (a Warthog, a Scorpion, a Ghost;
not a turret, nor one that flies, which the walkable surfaces' paths are
not for) */
static boolean bots_vehicle_drivable(long vehicle_index)
{
	short type = bots_vehicle_definition(vehicle_index)->vehicle_type;

	return type == _bots_vehicle_type_human_tank || type == _bots_vehicle_type_human_jeep ||
		type == _bots_vehicle_type_alien_scout;
}

/* port: the kind of a vehicle a bot drives (bots_vehicle_worths' index):
0 a tank (the Scorpion), 1 a jeep (the Warthog, the rocket Warthog), 2 a
scout (the Ghost) */
static short bots_vehicle_kind(long vehicle_index)
{
	short type = bots_vehicle_definition(vehicle_index)->vehicle_type;

	return type == _bots_vehicle_type_human_tank ? 0 : type == _bots_vehicle_type_alien_scout ? 2 : 1;
}

/* port: what an empty vehicle's driver's seat is worth to a bot on foot, by
its kind - as players weigh them, a Scorpion worth a walk back to it, a
Ghost some way, a Warthog only on the way (the campaign's AI weighs none:
its encounters are handed theirs): how far the bot walks to it (reach), and
no further than this share of the way to its goal (but for one close); one
this close is gone for in any direction (fresh from its base, nearer
BOTS_VEHICLE_FRESH_SECONDS, ten units further, fifteen at least); else the
vehicle no further from the goal than the bot is and this slack; and how
much nearer it counts than it is (the choice's score). A Warthog's are the
rules bots had before (a reach of 30, 0.6 of the way, 5 of slack) */
static struct
{
	real reach;
	real share;
	real close;
	real slack;
	real bonus;
} const bots_vehicle_worths[3] =
{
	{ 50.0f, 1.0f, 20.0f, 20.0f, 12.0f },
	{ 30.0f, 0.6f, 0.0f, 5.0f, 0.0f },
	{ 40.0f, 0.8f, 10.0f, 10.0f, 5.0f },
};

/* port: the seat of a turret a bot mans: a vehicle that does not move, its
gun its gunner's - the vehicle type turret, or one with a gunner's seat and
no driver's (a Shade, a Custom Edition map's gun tower) - its first gunner's
seat (a turret's single seat may be its driver's too), else its first
driver's on a turret; NONE for a vehicle that is no turret. Its definition's
alone (a tag's seats: a few flags read) */
static short bots_turret_seat(long vehicle_index)
{
	struct unit_definition *definition = unit_definition_get(object_get(vehicle_index)->definition_index);
	boolean turret = bots_vehicle_definition(vehicle_index)->vehicle_type == _bots_vehicle_type_turret;
	short seat_index, gunner = NONE, driver = NONE;

	for (seat_index = 0; seat_index < definition->unit.seats.count; seat_index++)
	{
		if (unit_seat_is_gunner(vehicle_index, seat_index) && gunner == NONE)
			gunner = seat_index;
		if (unit_seat_is_driver(vehicle_index, seat_index) && driver == NONE)
			driver = seat_index;
	}
	if (!turret && (gunner == NONE || driver != NONE))
		return NONE;
	return gunner != NONE ? gunner : driver;
}

/* the unit in a vehicle's seat, NONE for none (unit_can_enter_seat's look
at the vehicle's riders) */
static long bots_seat_occupant(long vehicle_index, short seat_index)
{
	long child_index = object_get(vehicle_index)->object.first_child_object_index;

	while (child_index != NONE)
	{
		struct object_datum *child = object_get(child_index);

		if (TEST_FLAG(_object_mask_unit, child->object.type) && unit_get(child_index)->unit.parent_seat_index == seat_index)
			return child_index;
		child_index = child->object.next_object_index;
	}
	return NONE;
}

/* whether anyone rides in the vehicle */
static boolean bots_vehicle_has_riders(long vehicle_index)
{
	long child_index = object_get(vehicle_index)->object.first_child_object_index;

	while (child_index != NONE && !TEST_FLAG(_object_mask_unit, object_get(child_index)->object.type))
		child_index = object_get(child_index)->object.next_object_index;
	return child_index != NONE;
}

static short bots_seat_kind(long vehicle_index, short seat_index)
{
	if (unit_seat_is_driver(vehicle_index, seat_index))
		return _bots_seat_driver;
	if (unit_seat_is_gunner(vehicle_index, seat_index))
		return _bots_seat_gunner;
	return _bots_seat_passenger;
}

/* the unit's current weapon, NONE without one */
static long bots_current_weapon(struct unit_datum *unit)
{
	if (unit->unit.current_weapon_index < 0 || unit->unit.current_weapon_index >= MAXIMUM_WEAPONS_PER_UNIT)
		return NONE;
	return unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
}

/* whether a weapon has no rounds left (one with magazines) */
static boolean bots_weapon_empty(long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);
	struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);

	if (definition->weapon.magazines.count <= 0)
		return FALSE;
	return weapon->weapon.magazines[0].rounds_loaded + weapon->weapon.magazines[0].rounds_total <= 0;
}

/* how full the weapon's magazine is, 0 to 1 (1 for one without) */
static real bots_weapon_loaded(long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);
	struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);
	struct weapon_magazine_definition *magazine;

	if (definition->weapon.magazines.count <= 0)
		return 1.0f;
	magazine = TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines, 0, struct weapon_magazine_definition);
	if (magazine->rounds_loaded_maximum <= 0)
		return 1.0f;
	return (real)weapon->weapon.magazines[0].rounds_loaded / (real)magazine->rounds_loaded_maximum;
}

/* the speed of the weapon's projectile (world units a tick; 0 for one
that hits at once or none) */
static real bots_projectile_speed_compute(long definition_index)
{
	struct weapon_definition *definition = weapon_definition_get(definition_index);

	if (definition->weapon.triggers.count > 0)
	{
		struct weapon_trigger_definition *trigger =
			TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);

		if (trigger->projectile.index != NONE)
		{
			struct projectile_definition *projectile = projectile_definition_get(trigger->projectile.index);

			return 0.5f * (projectile->projectile.initial_velocity + projectile->projectile.final_velocity);
		}
	}
	return 0.0f;
}

/* how much a bot wants a weapon (the stock weapons, by their tags' names),
-1 for the game's own (a flag, a ball) */
static short bots_weapon_value_compute(long definition_index)
{
	static struct
	{
		char const *name;
		short value;
	} const values[] =
	{
		{ "rocket", 9 }, { "sniper", 8 }, { "shotgun", 8 }, { "fuel rod", 7 }, { "plasma_cannon", 7 },
		{ "plasma pistol", 3 }, { "pistol", 7 }, { "assault rifle", 6 }, { "needler", 5 }, { "plasma rifle", 5 },
		{ "flamethrower", 4 }, { "flag", -1 }, { "ball", -1 }, { "skull", -1 },
	};
	char const *name = tag_get_name(definition_index);
	long index;

	if (!name)
		return 4;
	for (index = 0; index < (long)NUMBEROF(values); index++)
	{
		if (strstr(name, values[index].name))
			return values[index].value;
	}
	return 4;
}

/* how a bot fires the weapon: the ticks between presses of a weapon fired a
press at a time (a latched trigger: the pistols, the sniper rifle, the
rocket launcher; or one that charges while held: the plasma pistol), 0 for
one held down in bursts */
static short bots_weapon_tap_ticks_compute(long definition_index)
{
	struct weapon_definition *definition = weapon_definition_get(definition_index);
	struct weapon_trigger_definition *trigger;
	real rate;

	if (definition->weapon.triggers.count <= 0)
		return 0;
	trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);
	if (!TEST_FLAG(trigger->flags, _weapon_trigger_latched_bit) && trigger->charging_time <= 0.0f)
		return 0;
	rate = trigger->initial_rate_of_fire > 0.0f ? trigger->initial_rate_of_fire : 3.0f;
	return (short)PIN((long)((real)TICKS_PER_SECOND / rate + 0.5f), 2, TICKS_PER_SECOND * 2);
}

/* the distance a bot keeps from its enemy with the weapon */
static real bots_weapon_range_compute(long definition_index)
{
	char const *name = tag_get_name(definition_index);

	if (!name)
		return 6.0f;
	if (strstr(name, "shotgun") || strstr(name, "flamethrower"))
		return 2.0f;
	if (strstr(name, "sniper"))
		return 18.0f;
	if (strstr(name, "rocket") || strstr(name, "fuel rod") || strstr(name, "plasma_cannon"))
		return 12.0f;
	if (strstr(name, "plasma pistol") || strstr(name, "needler"))
		return 5.0f;
	if (strstr(name, "pistol"))
		return 9.0f;
	return 7.0f;
}

/* port: what kind of a precision weapon it is, from its tag alone (not its
name: a Custom Edition map's own rifles too), bots_tactics.h's
_bots_precision_...: it zooms, its first trigger fires one projectile a
shot (no spread of pellets), and
- its projectile is a bullet, BOTS_PRECISION_SPEED or faster (world units
  a tick: the stock pistol's 10, the sniper rifle's 33; the plasma bolts'
  0.7 to 1.3), aimed true, its least error BOTS_PRECISION_ERROR at most (a
  degree: the pistol's 0.2, the sniper rifle's 0.5; the assault rifle's 2):
  a scope of two zoom levels or more, or BOTS_PRECISION_SCOPE times and up,
  makes a long one (the sniper rifle: two levels, 2x and 8x), a zoom alone
  a middle one (the pistol: 2x);
- or it is slower than BOTS_HEAVY_SPEED (the rocket's 0.37): a heavy one
  (the rocket launcher), which the bots holding it took to the vantage
  points before too.
The plasma pistol, the needler, the plasma and assault rifles and the
flamethrower (none zooms) and the shotgun (fifteen pellets) are none.
(Blood Gulch's, HALO_BOT_TRACE: each weapon's numbers in halo.log.) */
#define BOTS_PRECISION_SPEED 6.0f
#define BOTS_PRECISION_ERROR 0.0175f
#define BOTS_PRECISION_SCOPE 4.0f
#define BOTS_HEAVY_SPEED 1.0f
static short bots_weapon_precision_compute(long definition_index)
{
	struct weapon_definition *definition = weapon_definition_get(definition_index);
	struct weapon_trigger_definition *trigger;
	struct projectile_definition *projectile;
	real speed;

	if (definition->weapon.triggers.count <= 0)
		return _bots_precision_none;
	trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);
	if (trigger->projectile.index == NONE)
		return _bots_precision_none;
	projectile = projectile_definition_get(trigger->projectile.index);
	speed = 0.5f * (projectile->projectile.initial_velocity + projectile->projectile.final_velocity);
	if (bots_trace_enabled())
		platform_log("bots: trace weapon %s: zoom %d x%.1f-%.1f, %d a shot, projectile %s speed %.2f, error %.3f-%.3f",
			tag_get_name(definition_index), (int)definition->weapon.zoom_level_count,
			definition->weapon.zoom_magnification_minimum, definition->weapon.zoom_magnification_maximum,
			(int)trigger->projectiles_per_shot, tag_get_name(trigger->projectile.index), speed,
			trigger->projectile_error_angle_lower_bound, trigger->projectile_error_angle_upper_bound);
	if (definition->weapon.zoom_level_count < 1 || trigger->projectiles_per_shot > 1)
		return _bots_precision_none;
	if (speed >= BOTS_PRECISION_SPEED && trigger->projectile_error_angle_lower_bound <= BOTS_PRECISION_ERROR)
	{
		return definition->weapon.zoom_level_count >= 2 ||
			definition->weapon.zoom_magnification_maximum >= BOTS_PRECISION_SCOPE ? _bots_precision_long : _bots_precision_mid;
	}
	return speed > 0.0f && speed < BOTS_HEAVY_SPEED ? _bots_precision_heavy : _bots_precision_none;
}

/* what the bots know of a weapon definition (its value, the distance kept
with it, how it fires, its projectile's speed), made once a map: the
functions above read only its tag, its name and its trigger, which a map
never changes, and each answers the same for it every time (a bot asked them
of its weapon every tick: a dozen name searches and a few tag lookups) */
#define BOTS_WEAPON_KINDS 32
/* where a weapon is best aimed (bots_weapon_aim_compute) */
enum
{
	_bots_aim_body = 0,
	/* its headshots count (the pistol, the sniper rifle) */
	_bots_aim_precision,
	/* its blast hurts (rockets, the fuel rod) */
	_bots_aim_explosive,
};

struct bots_weapon_kind
{
	long definition_index;
	short value;
	short aim;
	short tap_ticks;
	real range;
	real projectile_speed;
	/* port: the ticks its trigger charges (a turret's gun that fires only
	charged, Extinction's gun tower's), 0 for none */
	short charge_ticks;
	/* port: what kind of a precision weapon (bots_tactics.h) */
	short precision;
	/* (the fight by level) its shots are true only zoomed (the sniper rifle:
	its trigger's error when unzoomed), and its error grows as it fires (the
	pistol) */
	boolean sniper;
	boolean blooms;
};

static struct
{
	unsigned long map_generation;
	long count;
	struct bots_weapon_kind kinds[BOTS_WEAPON_KINDS];
	/* (the one made when the table is full) */
	struct bots_weapon_kind spare;
} bots_weapon_kinds;

/* where the stock weapons are best aimed, by their tags' names */
static short bots_weapon_aim_compute(long definition_index)
{
	char const *name = tag_get_name(definition_index);

	if (!name)
		return _bots_aim_body;
	if (strstr(name, "sniper") || (strstr(name, "pistol") && !strstr(name, "plasma")))
		return _bots_aim_precision;
	if (strstr(name, "rocket") || strstr(name, "fuel rod") || strstr(name, "plasma_cannon"))
		return _bots_aim_explosive;
	return _bots_aim_body;
}

static void bots_weapon_kinds_flush(void)
{
	bots_weapon_kinds.count = 0;
	bots_weapon_kinds.map_generation = bots_map_generation;
}

static struct bots_weapon_kind const *bots_weapon_kind(long definition_index)
{
	struct bots_weapon_kind *kind;
	long index;

	if (bots_weapon_kinds.map_generation != bots_map_generation)
		bots_weapon_kinds_flush();
	for (index = 0; index < bots_weapon_kinds.count; index++)
	{
		if (bots_weapon_kinds.kinds[index].definition_index == definition_index)
			return &bots_weapon_kinds.kinds[index];
	}
	kind = bots_weapon_kinds.count < BOTS_WEAPON_KINDS ? &bots_weapon_kinds.kinds[bots_weapon_kinds.count++] :
		&bots_weapon_kinds.spare;
	kind->definition_index = definition_index;
	kind->value = bots_weapon_value_compute(definition_index);
	kind->tap_ticks = bots_weapon_tap_ticks_compute(definition_index);
	kind->range = bots_weapon_range_compute(definition_index);
	kind->projectile_speed = bots_projectile_speed_compute(definition_index);
	kind->precision = bots_weapon_precision_compute(definition_index);
	kind->aim = bots_weapon_aim_compute(definition_index);
	if (kind->precision != _bots_precision_none && kind != &bots_weapon_kinds.spare)
		platform_log("bots: weapon %s: %s", tag_get_name(definition_index), kind->precision == _bots_precision_long ?
			"precision, long (a vantage point)" : kind->precision == _bots_precision_mid ? "precision, middle range" :
			"heavy (a vantage point)");
	kind->charge_ticks = 0;
	kind->sniper = FALSE;
	kind->blooms = FALSE;
	{
		struct weapon_definition *definition = weapon_definition_get(definition_index);

		if (definition->weapon.triggers.count > 0)
		{
			struct weapon_trigger_definition *trigger =
				TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);

			kind->charge_ticks = (short)PIN((long)(trigger->charging_time * TICKS_PER_SECOND + 0.5f), 0, TICKS_PER_SECOND * 5);
			kind->sniper = TEST_FLAG(trigger->flags, _weapon_trigger_use_error_when_unzoomed_bit) &&
				definition->weapon.zoom_level_count > 0;
			kind->blooms = trigger->projectile_error_angle_upper_bound > trigger->projectile_error_angle_lower_bound + 0.001f;
		}
	}
	return kind;
}

static short bots_weapon_value(long definition_index)
{
	return bots_weapon_kind(definition_index)->value;
}

static real bots_projectile_speed(long weapon_index)
{
	return bots_weapon_kind(weapon_get(weapon_index)->definition_index)->projectile_speed;
}

static short bots_weapon_tap_ticks(long weapon_index)
{
	return bots_weapon_kind(weapon_get(weapon_index)->definition_index)->tap_ticks;
}

static real bots_weapon_range(long weapon_index)
{
	return weapon_index != NONE ? bots_weapon_kind(weapon_get(weapon_index)->definition_index)->range : 6.0f;
}

/* port: (bots_tactics.h) */
short bots_weapon_precision(long weapon_index)
{
	return weapon_index != NONE ? bots_weapon_kind(weapon_get(weapon_index)->definition_index)->precision :
		_bots_precision_none;
}

/* port: whether the bot found no way to the item a while ago (left be
thirty seconds), and the item so remembered (the oldest of those kept
forgotten) */
static boolean bots_item_unreachable(struct bots_bot const *bot, long item_index)
{
	long index;

	for (index = 0; index < BOTS_UNREACHABLE_ITEMS; index++)
	{
		if (bot->unreachable_item_indices[index] == item_index && game_time_get() < bot->unreachable_item_until_ticks[index])
			return TRUE;
	}
	return FALSE;
}

static void bots_item_set_unreachable(struct bots_bot *bot, long item_index)
{
	long slot;

	/* (one remembered already: kept longer) */
	for (slot = 0; slot < BOTS_UNREACHABLE_ITEMS; slot++)
	{
		if (bot->unreachable_item_indices[slot] == item_index && game_time_get() < bot->unreachable_item_until_ticks[slot])
		{
			bot->unreachable_item_until_ticks[slot] = game_time_get() + TICKS_PER_SECOND * 30;
			return;
		}
	}
	slot = bot->unreachable_item_next;

	bot->unreachable_item_indices[slot] = item_index;
	bot->unreachable_item_until_ticks[slot] = game_time_get() + TICKS_PER_SECOND * 30;
	bot->unreachable_item_next = (slot + 1) % BOTS_UNREACHABLE_ITEMS;	if (bots_trace_enabled())
	{
		struct object_datum *item = object_try_and_get(item_index);

		platform_log("bots: trace bot %ld leaves %s be", (long)(bot - bots_globals.bots) + 1,
			item ? tag_get_name(item->definition_index) : "?");
	}
}

/* an item on the ground worth fetching near the bot: a weapon better than
the one it holds; NONE for none (port: the powerups are bots_powerup_goal's) */
static long bots_find_item(struct bots_bot *bot, struct unit_datum *unit, real_point3d const *position,
	real maximum_distance)
{
	struct object_iterator iterator;
	long best_index = NONE;
	real best_score = REAL_MAX;
	long current = bots_current_weapon(unit);
	short current_value = current != NONE ? bots_weapon_value(weapon_get(current)->definition_index) : 0;

	object_iterator_new(&iterator, _object_mask_weapon, 0);
	while (object_iterator_next(&iterator))
	{
		struct item_datum *item = item_get(iterator.index);
		real distance, score;
		short value;
		short slot;
		boolean held = FALSE;

		if (item->object.parent_object_index != NONE ||
			bots_item_unreachable(bot, iterator.index) ||
			!TEST_FLAG(item->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
		{
			continue;
		}
		distance = bots_distance3d(&item->object.position, position);
		if (distance > maximum_distance)
			continue;
		value = bots_weapon_value(item->definition_index);
		/* (one it holds already: only its ammunition, which walking over
		takes) */
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long other = unit->unit.weapon_object_indices[slot];

			if (other != NONE && weapon_get(other)->definition_index == item->definition_index)
				held = TRUE;
		}
		if (held || value <= current_value)
			continue;
		score = distance - (real)(value - current_value) * 3.0f;
		if (score < best_score)
		{
			best_score = score;
			best_index = iterator.index;
		}
	}
	return best_index;
}

/* port: the bots' kind of an equipment item's powerup, _bots_powerup_none
for one they do not go for (a grenade, double speed) */
static short bots_powerup_kind(long definition_index)
{
	switch (equipment_definition_get(definition_index)->equipment.powerup_type)
	{
	case _equipment_powerup_health: return _bots_powerup_health;
	case _equipment_powerup_overshield: return _bots_powerup_overshield;
	case _equipment_powerup_active_camouflage: return _bots_powerup_camouflage;
	}
	return _bots_powerup_none;
}

/* port: the powerups lying on the map found again, and the turrets, once a
second (at a bots' tick's start, from the game state alone: the same in two
runs) */
static void bots_powerups_find(void)
{
	struct object_iterator iterator;
	long now = game_time_get();

	if (bots_powerups.map_generation == bots_map_generation && bots_powerups.found_tick != NONE &&
		now >= bots_powerups.found_tick && now - bots_powerups.found_tick < TICKS_PER_SECOND)
	{
		return;
	}
	bots_powerups.map_generation = bots_map_generation;
	bots_powerups.found_tick = now;
	bots_powerups.count = 0;
	object_iterator_new(&iterator, _object_mask_equipment, 0);
	while (object_iterator_next(&iterator) && bots_powerups.count < BOTS_MAXIMUM_POWERUPS)
	{
		struct item_datum *item = item_get(iterator.index);
		short kind;

		if (item->object.parent_object_index != NONE || !TEST_FLAG(item->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
		{
			continue;
		}
		kind = bots_powerup_kind(item->definition_index);
		if (kind == _bots_powerup_none)
			continue;
		bots_powerups.items[bots_powerups.count].object_index = iterator.index;
		bots_powerups.items[bots_powerups.count].kind = kind;
		bots_powerups.items[bots_powerups.count].position = item->object.position;
		bots_powerups.count++;
	}
	/* (and the turrets: none on a stock map, whose bots never look again) */
	{
		long previous_count = bots_globals.turret_count;

		bots_globals.turret_count = 0;
		object_iterator_new(&iterator, _object_mask_vehicle, 0);
		while (object_iterator_next(&iterator) && bots_globals.turret_count < BOTS_MAXIMUM_TURRETS)
		{
			if (bots_turret_seat(iterator.index) == NONE)
				continue;
			/* (halo.log: each turret, as the count changes) */
			if (bots_globals.turret_count >= previous_count)
			{
				struct object_datum *turret = object_get(iterator.index);

				platform_log("bots: a turret, %s at (%.1f %.1f %.1f), seat %d", tag_get_name(turret->definition_index),
					turret->object.position.x, turret->object.position.y, turret->object.position.z,
					(int)bots_turret_seat(iterator.index));
			}
			bots_globals.turret_indices[bots_globals.turret_count++] = iterator.index;
		}
	}
}

/* ---------- the map */

static void bots_add_roam_point(real_point3d const *point)
{
	real_point3d ground;
	long surface_index;

	if (bots_globals.roam_point_count >= BOTS_MAXIMUM_ROAM_POINTS)
		return;
	surface_index = bots_surface_below(point, &ground);
	if (surface_index == NONE)
		return;
	bots_globals.roam_points[bots_globals.roam_point_count] = ground;
	bots_globals.roam_surfaces[bots_globals.roam_point_count] = surface_index;
	bots_globals.roam_point_count++;
}

static void bots_search_drop(struct bots_search *search);

/* the bots' walkable surfaces let go (no search reading them: those asked
are dropped, a running one waited for) */
static void bots_paths_release(void)
{
	long index;

	for (index = 0; bots_globals.search_slots && index < BOTS_SEARCH_SLOTS; index++)
		bots_search_drop(&bots_globals.search_slots[index]);
	for (index = 0; index < BOTS_MAXIMUM; index++)
		bots_globals.bots[index].search_slot = NONE;
	pthread_mutex_lock(&bots_helper.lock);
	bots_paths.active = FALSE;
	bots_paths.source = NULL;
	pthread_mutex_unlock(&bots_helper.lock);
	bots_paths.learned_count = 0;
	bots_paths.closed_count = 0;
	bots_paths.scan_index = 0;
	bots_paths.scan_done = FALSE;
	bots_paths.roofed_count = 0;
	bots_paths.scan_us = 0;
	bots_paths.scan_ticks = 0;
	if (bots_paths.walkable)
		free(bots_paths.walkable);
	bots_paths.walkable = NULL;
	if (bots_paths.vehicle_walkable)
		free(bots_paths.vehicle_walkable);
	bots_paths.vehicle_walkable = NULL;
	bots_paths.vehicle_closed_count = 0;
}

/* the surface's corners (at most BOTS_SURFACE_CORNERS), from its ring of
edges; 0 for one not the bsp's */
#define BOTS_SURFACE_CORNERS 16
static long bots_surface_corners(struct collision_bsp const *bsp, long surface_index, real_point3d *corners)
{
	struct collision_surface const *surface;
	long edge_index, count = 0;

	if (surface_index < 0 || surface_index >= bsp->surfaces.count)
		return 0;
	surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface);
	edge_index = surface->first_edge_index;
	do
	{
		struct collision_edge const *edge;
		boolean right;

		if (edge_index < 0 || edge_index >= bsp->edges.count)
			return 0;
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		right = edge->surface_indices[1] == surface_index;
		if (!VALID_INDEX(edge->vertex_indices[right], bsp->vertices.count))
			return 0;
		corners[count++] = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[right], struct collision_vertex)->point;
		edge_index = edge->edge_indices[right];
	} while (edge_index != surface->first_edge_index && count < BOTS_SURFACE_CORNERS);
	return count;
}

/* whether the object stands across the surface: a line over it at a biped's
knees or chest, from its middle to a little past an edge's, meets it, or
its middle is under it */
static boolean bots_surface_blocked(long object_index, real_point3d const *corners, long corner_count,
	real_point3d const *middle)
{
	static real const heights[] = { 0.35f, 1.0f };
	unsigned long flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit);
	struct collision_result collision;
	real_point3d start = *middle;
	real_vector3d up = { 0.0f, 0.0f, 1.6f };
	long height, corner;

	start.z += 0.05f;
	if (collision_test_vector(flags, &start, &up, NONE, &collision) && collision.type == _collision_result_object &&
		collision.object_index == object_index)
	{
		return TRUE;
	}
	for (height = 0; height < (long)NUMBEROF(heights); height++)
	{
		start.z = middle->z + heights[height];
		for (corner = 0; corner < corner_count; corner++)
		{
			real_point3d const *a = &corners[corner], *b = &corners[(corner + 1) % corner_count];
			real_vector3d vector;
			real length, past;

			vector.i = 0.5f * (a->x + b->x) - start.x;
			vector.j = 0.5f * (a->y + b->y) - start.y;
			vector.k = 0.5f * (a->z + b->z) + heights[height] - start.z;
			/* (on a little past the edge: a door may stand on it) */
			length = (real)sqrt(vector.i * vector.i + vector.j * vector.j + vector.k * vector.k);
			if (length < 0.01f)
				continue;
			past = (length + 0.4f) / length;
			vector.i *= past;
			vector.j *= past;
			vector.k *= past;
			if (collision_test_vector(flags, &start, &vector, NONE, &collision) &&
				collision.type == _collision_result_object && collision.object_index == object_index)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* the bots' copy of the structure's walkable surfaces, made (the searches
on the helper thread take it from then on); FALSE without one */
static boolean bots_paths_copy(void)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	long count;

	if (!structure || structure->pathfinding_surfaces.count <= 0 || structure->collision_bsp.count <= 0 ||
		(bots_paths.source && bots_paths.source != structure))
	{
		return FALSE;
	}
	if (bots_paths.walkable)
		return TRUE;
	count = structure->pathfinding_surfaces.count;
	bots_paths.walkable = (byte *)malloc((size_t)count);
	bots_paths.vehicle_walkable = (byte *)malloc((size_t)count);
	if (!bots_paths.walkable || !bots_paths.vehicle_walkable)
	{
		free(bots_paths.walkable);
		free(bots_paths.vehicle_walkable);
		bots_paths.walkable = bots_paths.vehicle_walkable = NULL;
		return FALSE;
	}
	csmemcpy(bots_paths.walkable, structure->pathfinding_surfaces.address, (size_t)count);
	csmemcpy(bots_paths.vehicle_walkable, structure->pathfinding_surfaces.address, (size_t)count);
	bots_paths.structure = *structure;
	bots_paths.structure.pathfinding_surfaces.address = bots_paths.walkable;
	/* (path.c: the helper's cache of surfaces' edges made again) */
	bots_paths.vehicle_structure = *structure;
	bots_paths.vehicle_structure.pathfinding_surfaces.address = bots_paths.vehicle_walkable;
	pthread_mutex_lock(&bots_helper.lock);
	bots_paths.source = structure;
	bots_paths.active = TRUE;
	pthread_mutex_unlock(&bots_helper.lock);
	return TRUE;
}

/* a walkable surface of the bots' copy: its corners, its middle and how far
its corners are from it; FALSE for one not walkable (or not the bsp's) */
static boolean bots_paths_walkable_surface(struct collision_bsp const *bsp, long surface_index,
	real_point3d *corners, long *corner_count, real_point3d *middle, real *reach)
{
	long corner;

	/* (a byte the helper may be reading: walkable or not, either is an
	answer) */
	if (surface_index >= bots_paths.structure.pathfinding_surfaces.count ||
		!TEST_FLAG(bots_paths.walkable[surface_index], BOTS_PATHFINDING_WALKABLE_BIT))
	{
		return FALSE;
	}
	*corner_count = bots_surface_corners(bsp, surface_index, corners);
	if (*corner_count < 3)
		return FALSE;
	middle->x = middle->y = middle->z = 0.0f;
	for (corner = 0; corner < *corner_count; corner++)
	{
		middle->x += corners[corner].x / (real)*corner_count;
		middle->y += corners[corner].y / (real)*corner_count;
		middle->z += corners[corner].z / (real)*corner_count;
	}
	*reach = 0.0f;
	for (corner = 0; corner < *corner_count; corner++)
		*reach = MAX(*reach, bots_distance3d(middle, &corners[corner]));
	return TRUE;
}

/* the walkable surfaces an object stands across closed to the bots' searches */
static void bots_paths_close_object(long object_index, char const *why)
{
	struct object_datum *object = object_get(object_index);
	struct collision_bsp const *bsp;
	unsigned long long started = bots_time_us();
	long surface_index, closed = 0;

	if (bots_paths.learned_count >= (long)NUMBEROF(bots_paths.learned) || !bots_paths_copy())
		return;
	bots_paths.learned[bots_paths.learned_count++] = object_index;
	bsp = TAG_BLOCK_GET_ELEMENT(&bots_paths.structure.collision_bsp, 0, struct collision_bsp);
	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		real_point3d corners[BOTS_SURFACE_CORNERS], middle;
		real reach;
		long corner_count;

		if (bots_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach) &&
			bots_distance3d(&middle, &object->object.bounding_sphere_center) <
				object->object.bounding_sphere_radius + reach + 0.5f &&
			bots_surface_blocked(object_index, corners, corner_count, &middle))
		{
			bots_paths.walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			bots_paths.vehicle_walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			closed++;
		}
	}
	bots_paths.closed_count += closed;
	if (closed)
	platform_log("bots: %s %s at (%.1f %.1f %.1f): %ld walkable surfaces closed (%.1f ms)", why,
		tag_get_name(object->definition_index), object->object.bounding_sphere_center.x,
		object->object.bounding_sphere_center.y, object->object.bounding_sphere_center.z, closed,
		(double)(bots_time_us() - started) / 1000.0);
}

/* port: a vehicle stuck with the level before it (a bank a biped walks up,
the foot of a ledge, which the walkable surfaces join): the walkable surfaces
just before it closed to the vehicles' searches alone, as far about as the
vehicle is wide (1.5 units at least: a Scorpion's 2.2, which drove at a
base's back door strip by strip), those under it and its goal's left open */
static void bots_paths_close_for_vehicles(real_point3d const *point, real_point3d const *vehicle_point,
	real_point3d const *goal_point, real vehicle_radius)
{
	struct collision_bsp const *bsp;
	long surface_index, closed = 0;
	real about = MAX(1.5f, vehicle_radius);

	if (!bots_paths_copy())
		return;
	bsp = TAG_BLOCK_GET_ELEMENT(&bots_paths.vehicle_structure.collision_bsp, 0, struct collision_bsp);
	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		real_point3d corners[BOTS_SURFACE_CORNERS], middle;
		real reach;
		long corner_count;
		struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface);
		struct collision_edge const *edge;

		if (surface_index >= bots_paths.vehicle_structure.pathfinding_surfaces.count ||
			!TEST_FLAG(bots_paths.vehicle_walkable[surface_index], BOTS_PATHFINDING_WALKABLE_BIT) ||
			!VALID_INDEX(surface->first_edge_index, bsp->edges.count))
		{
			continue;
		}
		/* (a corner far off: the surface is, the corners of the near ones
		alone worked out) */
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, surface->first_edge_index, struct collision_edge);
		if (!VALID_INDEX(edge->vertex_indices[0], bsp->vertices.count) ||
			bots_distance2d(&TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[0], struct collision_vertex)->point, point) >
				8.0f ||
			!bots_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach))
		{
			continue;
		}
		if (bots_distance2d(&middle, point) < about && fabs(middle.z - point->z) < 2.5f &&
			bots_distance2d(&middle, vehicle_point) > 1.2f && bots_distance2d(&middle, goal_point) > 3.0f)
		{
			bots_paths.vehicle_walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			closed++;
		}
	}
	bots_paths.vehicle_closed_count += closed;
	platform_log("bots: a vehicle stuck at (%.1f %.1f %.1f): %ld walkable surfaces before it closed to vehicles (%ld in all)",
		vehicle_point->x, vehicle_point->y, vehicle_point->z, closed, bots_paths.vehicle_closed_count);
}

/* the walkable surfaces with no room above them closed, a few a tick from
the game's start: a floor that goes on under a wall (Longest's bases: the
floor's strips under the walls joined the rooms either side, and a team's
bots walked into the wall a whole game). A surface is closed whose middle,
or most of the points halfway from it to its corners, have the level less
than half a biped's height above them */
#define BOTS_PATHS_SCAN_A_TICK 32
static void bots_paths_scan(void)
{
	struct collision_bsp const *bsp;
	long scanned;
	unsigned long long started;

	if (bots_paths.scan_done || !bots_paths_copy())
		return;
	started = bots_time_us();
	bots_paths.scan_ticks++;
	bsp = TAG_BLOCK_GET_ELEMENT(&bots_paths.structure.collision_bsp, 0, struct collision_bsp);
	for (scanned = 0; scanned < BOTS_PATHS_SCAN_A_TICK; scanned++)
	{
		long surface_index = bots_paths.scan_index++;
		real_point3d corners[BOTS_SURFACE_CORNERS], middle;
		real reach;
		long corner_count, sample, covered = 0;

		if (surface_index >= bsp->surfaces.count || surface_index >= bots_paths.structure.pathfinding_surfaces.count)
		{
			bots_paths.scan_done = TRUE;
			bots_paths.scan_us += bots_time_us() - started;
			platform_log("bots: %ld walkable surfaces with no room above closed (%ld ticks, %.1f ms, %.0f us a tick at most "
				"%d surfaces)", bots_paths.roofed_count, bots_paths.scan_ticks, (double)bots_paths.scan_us / 1000.0,
				(double)bots_paths.scan_us / (double)bots_paths.scan_ticks, BOTS_PATHS_SCAN_A_TICK);
			return;
		}
		if (!bots_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach))
			continue;
		/* (the middle first: covered, the surface is; else most of the
		points between it and the corners) */
		for (sample = -1; sample < corner_count; sample++)
		{
			unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_back_facing_surfaces_bit);
			real_vector3d up = { 0.0f, 0.0f, 0.35f };
			struct collision_result collision;
			real_point3d point = middle;

			if (sample >= 0)
			{
				point.x = 0.5f * (middle.x + corners[sample].x);
				point.y = 0.5f * (middle.y + corners[sample].y);
				point.z = 0.5f * (middle.z + corners[sample].z);
			}
			point.z += 0.05f;
			if (collision_test_vector(flags, &point, &up, NONE, &collision))
			{
				if (sample < 0)
				{
					covered = corner_count;
					break;
				}
				covered++;
			}
		}
		if (covered * 2 > corner_count)
		{
			bots_paths.walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			bots_paths.vehicle_walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			bots_paths.roofed_count++;
		}
	}
	bots_paths.scan_us += bots_time_us() - started;
}

static boolean bots_paths_learned(long object_index)
{
	long index;

	for (index = 0; index < bots_paths.learned_count; index++)
	{
		if (bots_paths.learned[index] == object_index)
			return TRUE;
	}
	return FALSE;
}

/* a path's obstacle avoidance found no way past where the bot is: the
scenery and machines about it (not the bipeds and vehicles, which move) are
in the way, their surfaces closed to the searches from now on (Sidewinder's
blast door between the bases is scenery: the search, over the structure
alone, goes through it, the shortest way, and the avoidance then finds no
way past; more than half of the searches across that map failed so). A
rock or a tree the avoidance goes around is never in the way */
/* whether the point is on or in the object (by its bounding sphere) */
static boolean bots_object_holds(struct object_datum *object, real_point3d const *point)
{
	return bots_distance2d(&object->object.bounding_sphere_center, point) < object->object.bounding_sphere_radius + 0.4f &&
		fabs(object->object.bounding_sphere_center.z - point->z) < object->object.bounding_sphere_radius + 1.0f;
}

/* the scenery or machine (with a collision model) the point is on or in,
the nearest; NONE for none */
static long bots_object_at(real_point3d const *point)
{
	struct object_iterator iterator;
	long best_index = NONE;
	real best_distance = REAL_MAX;

	object_iterator_new(&iterator, _object_mask_scenery | _object_mask_machine, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		real distance;

		if (object_definition_get(object->definition_index)->object.collision_model.index == NONE ||
			!bots_object_holds(object, point))
		{
			continue;
		}
		distance = bots_distance3d(&object->object.bounding_sphere_center, point);
		if (distance < best_distance)
		{
			best_distance = distance;
			best_index = iterator.index;
		}
	}
	return best_index;
}

/* whether one of the map's game places (a flag's stand, a teleporter, a
hill's corner, a ball's spawn) is on or in the object: never in the way */
static boolean bots_object_marks_place(struct object_datum *object)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	for (index = 0; scenario && index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

		if (bots_object_holds(object, &flag->position))
			return TRUE;
	}
	return FALSE;
}

/* port: whether the point stands on the object (the level or another object
under it first) */
static boolean bots_object_supports(long object_index, real_point3d const *point)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -1.2f };

	start.z += 0.4f;
	return collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit), &start, &down, NONE, &collision) &&
		collision.type == _collision_result_object && collision.object_index == object_index;
}

static long bots_paths_learn(real_point3d const *position, real_point3d const *goal)
{
	struct object_iterator iterator;
	long learned = 0;

	object_iterator_new(&iterator, _object_mask_scenery | _object_mask_machine, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		struct object_definition *definition = object_definition_get(object->definition_index);

		/* (not what the bot or its goal stands on or in: a flag's stand, a
		teleporter's base; port: what the bot stands beside, within its
		bounding sphere but on the level, is learnt - a bot pressed against
		Sidewinder's blast door before it was learnt never learnt it, and the
		flag carriers and their escorts stood there all game) */
		if (definition->object.collision_model.index == NONE ||
			bots_distance2d(&object->object.bounding_sphere_center, position) > object->object.bounding_sphere_radius + 4.5f ||
			fabs(object->object.bounding_sphere_center.z - position->z) > object->object.bounding_sphere_radius + 2.0f ||
			(bots_object_holds(object, position) && bots_object_supports(iterator.index, position)) ||
			bots_object_holds(object, goal) ||
			bots_paths_learned(iterator.index) || bots_object_marks_place(object))
		{
			continue;
		}
		bots_paths_close_object(iterator.index, "in the way:");
		learned++;
	}
	return learned;
}

/* the way off a closed surface: away from the nearest of what closed it */
static boolean bots_paths_escape_direction(real_point3d const *position, real *direction)
{
	long index, nearest = NONE;
	real nearest_distance = REAL_MAX;

	for (index = 0; index < bots_paths.learned_count; index++)
	{
		struct object_datum *object = object_try_and_get(bots_paths.learned[index]);
		real distance;

		if (!object)
			continue;
		distance = bots_distance2d(&object->object.bounding_sphere_center, position);
		if (distance < nearest_distance && distance < object->object.bounding_sphere_radius + 4.0f)
		{
			nearest_distance = distance;
			nearest = index;
		}
	}
	if (nearest == NONE)
		return FALSE;
	{
		struct object_datum *object = object_get(bots_paths.learned[nearest]);

		*direction = (real)atan2(position->y - object->object.bounding_sphere_center.y,
			position->x - object->object.bounding_sphere_center.x);
	}
	return TRUE;
}

/* the bots' searches' walkable surfaces, the structure's to begin with,
less those of a machine its definition calls a pathfinding obstacle (not an
elevator, nor a door open now that is none open) */
static void bots_prepare_paths(void)
{
	struct object_iterator iterator;

	bots_paths_release();
	object_iterator_new(&iterator, _object_mask_machine, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		struct machine_definition *definition = machine_definition_get(object->definition_index);

		if (!TEST_FLAG(definition->machine.flags, _machine_is_pathfinding_obstacle_bit) ||
			TEST_FLAG(definition->machine.flags, _machine_is_elevator_bit) ||
			(device_get_position(iterator.index) > 0.5f &&
				TEST_FLAG(definition->machine.flags, _machine_is_not_pathfinding_obstacle_when_open_bit)))
		{
			continue;
		}
		bots_paths_close_object(iterator.index, "a machine in the way:");
	}
}

/* port: the level's ladders, among the teleporters (bots_search_teleporter
goes up one as through a teleporter): its climbable surfaces (collision_bsp's
climbable flag) that stand upright, those one over another (a ladder's
pieces) and the two faces of one taken together; its foot the ground at its
bottom on the side a biped stands on (the level's surface there a walkable
one, as near the bottom as a step), its top's landing the ground beyond its
top (or before it) up at its top's height. One whose foot or landing is not
found is none. Made once a map */
#define BOTS_MAXIMUM_LADDER_PIECES 128
static void bots_prepare_ladders(struct collision_bsp const *bsp)
{
	struct
	{
		real_point3d middle;
		real_vector3d normal;
		real low, high;
	} pieces[BOTS_MAXIMUM_LADDER_PIECES];
	long piece_count = 0, surface_index, index;

	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		real_point3d corners[BOTS_SURFACE_CORNERS], middle = { 0.0f, 0.0f, 0.0f };
		real_vector3d a, b, normal;
		real low = REAL_MAX, high = -REAL_MAX, length;
		long corner, count;

		if (!TEST_FLAG(TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface)->flags,
			BOTS_COLLISION_SURFACE_CLIMBABLE_BIT))
		{
			continue;
		}
		count = bots_surface_corners(bsp, surface_index, corners);
		if (count < 3)
			continue;
		for (corner = 0; corner < count; corner++)
		{
			middle.x += corners[corner].x / (real)count;
			middle.y += corners[corner].y / (real)count;
			middle.z += corners[corner].z / (real)count;
			low = MIN(low, corners[corner].z);
			high = MAX(high, corners[corner].z);
		}
		vector_from_points3d(&corners[0], &corners[1], &a);
		vector_from_points3d(&corners[0], &corners[2], &b);
		cross_product3d(&a, &b, &normal);
		normal.k = 0.0f;
		length = magnitude3d(&normal);
		/* (upright, and a metre high at least) */
		if (length < 0.0001f || high - low < 0.3f)
			continue;
		normal.i /= length;
		normal.j /= length;
		/* (a piece over or under one, or its other face: the one ladder) */
		for (index = 0; index < piece_count; index++)
		{
			if (bots_distance2d(&pieces[index].middle, &middle) < 0.5f &&
				fabs(pieces[index].normal.i * normal.i + pieces[index].normal.j * normal.j) > 0.9f &&
				low <= pieces[index].high + 0.1f && high >= pieces[index].low - 0.1f)
			{
				pieces[index].low = MIN(pieces[index].low, low);
				pieces[index].high = MAX(pieces[index].high, high);
				break;
			}
		}
		if (index < piece_count || piece_count >= BOTS_MAXIMUM_LADDER_PIECES)
			continue;
		pieces[piece_count].middle = middle;
		pieces[piece_count].normal = normal;
		pieces[piece_count].low = low;
		pieces[piece_count].high = high;
		piece_count++;
	}
	bots_globals.ladder_count = 0;
	for (index = 0; index < piece_count && bots_globals.teleporter_count < BOTS_MAXIMUM_TELEPORTERS; index++)
	{
		long teleporter = bots_globals.teleporter_count;
		real_point3d foot = { 0.0f, 0.0f, 0.0f }, landing = { 0.0f, 0.0f, 0.0f }, ground;
		long foot_surface = NONE, landing_surface = NONE;
		real side, foot_side = 0.0f;

		for (side = -1.0f; side <= 1.0f; side += 2.0f)
		{
			real_point3d point;
			long surface;

			point.x = pieces[index].middle.x + pieces[index].normal.i * side * 0.4f;
			point.y = pieces[index].middle.y + pieces[index].normal.j * side * 0.4f;
			point.z = pieces[index].low + 0.3f;
			surface = bots_surface_below(&point, &ground);
			if (foot_surface == NONE && surface != NONE && fabs(ground.z - pieces[index].low) < 0.35f)
			{
				foot_surface = surface;
				foot = ground;
				foot_side = side;
			}
		}
		/* (the landing beyond the top first, as a ladder up a wall's face
		goes over it; else on the same side, a ladder up to a ledge) */
		for (side = -1.0f; side <= 1.0f && foot_surface != NONE; side += 2.0f)
		{
			real_point3d point;
			long surface;
			real way = side * -foot_side;

			point.x = pieces[index].middle.x + pieces[index].normal.i * way * 0.6f;
			point.y = pieces[index].middle.y + pieces[index].normal.j * way * 0.6f;
			point.z = pieces[index].high + 0.5f;
			surface = bots_surface_below(&point, &ground);
			if (landing_surface == NONE && surface != NONE && fabs(ground.z - pieces[index].high) < 0.5f)
			{
				landing_surface = surface;
				landing = ground;
			}
		}
		if (bots_trace_enabled())
		{
			platform_log("bots: trace ladder at (%.1f %.1f) z %.1f to %.1f normal (%.2f %.2f): foot %s (%.1f %.1f %.1f), "
				"landing %s (%.1f %.1f %.1f)", pieces[index].middle.x, pieces[index].middle.y, pieces[index].low,
				pieces[index].high, pieces[index].normal.i, pieces[index].normal.j, foot_surface != NONE ? "yes" : "no",
				foot.x, foot.y, foot.z, landing_surface != NONE ? "yes" : "no", landing.x, landing.y, landing.z);
		}
		if (foot_surface == NONE || landing_surface == NONE)
			continue;
		/* (no shortcut: bots_shortcut_weigh takes teleporters alone) */
		csmemset(&bots_globals.teleporters[teleporter], 0, sizeof(bots_globals.teleporters[teleporter]));
		bots_globals.teleporters[teleporter].exit_surface_index = NONE;
		bots_globals.teleporters[teleporter].entrance = foot;
		bots_globals.teleporters[teleporter].entrance_surface_index = foot_surface;
		bots_globals.teleporters[teleporter].exit = landing;
		bots_globals.teleporters[teleporter].ladder = TRUE;
		/* (facing it from its foot) */
		bots_globals.teleporters[teleporter].climb_direction =
			(real)atan2(-pieces[index].normal.j * foot_side, -pieces[index].normal.i * foot_side);
		bots_globals.teleporters[teleporter].top_z = pieces[index].high;
		bots_globals.teleporters[teleporter].climbs = 0;
		bots_globals.teleporters[teleporter].failures = 0;
		bots_globals.teleporter_count++;
		bots_globals.ladder_count++;
	}
}

/* the places a bot roams to: the player starting locations, the weapon and
item spawns, the hills (once the level's BSP is in) */
static void bots_prepare_map(void)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	bots_globals.map_ready = TRUE;
	bots_globals.roam_point_count = 0;
	bots_prepare_paths();
	/* port: the map's weapons known at once (halo.log names its precision
	weapons: bots_weapon_precision), half the table at most (a vehicle's gun
	is a weapon too) */
	{
		struct tag_iterator tags;
		long tag_index;

		tag_iterator_new(&tags, WEAPON_DEFINITION_TAG);
		while (bots_weapon_kinds.count < BOTS_WEAPON_KINDS / 2 && (tag_index = tag_iterator_next(&tags)) != NONE)
			bots_weapon_kind(tag_index);
	}
	if (!scenario)
		return;
	for (index = 0; index < scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment *equipment =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, index, struct scenario_netgame_equipment);

		bots_add_roam_point(&equipment->position);
	}
	/* (the teleporters: a source flag's channel is its team index, and the
	target flag of that channel is where it comes out; port: the first of
	the channel's targets, in the flags' order, as the game sends a player
	to it alone - game_engine.c's game_engine_update_teleporter takes the
	first find_netgame_flags finds - so a channel with more exits than one
	sends every player to its first, and so the bots know it. A two-way
	teleporter is two sources, each with the other's channel; a one-way one
	a source alone, its target no entrance. Each logged, a shortcut's
	exit's ground with it) */
	bots_globals.teleporter_count = 0;
	bots_globals.teleporter_shortcuts = 0;
	for (index = 0; index < scenario->netgame_flags.count && bots_globals.teleporter_count < BOTS_MAXIMUM_TELEPORTERS; index++)
	{
		struct scenario_netgame_flag *source =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);
		long target_index, exits = 0;
		long teleporter = bots_globals.teleporter_count;
		boolean paired = FALSE;

		if (source->type != _netgame_flag_teleporter_source)
			continue;
		for (target_index = 0; target_index < scenario->netgame_flags.count; target_index++)
		{
			struct scenario_netgame_flag *target =
				TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, target_index, struct scenario_netgame_flag);

			if (target->type != _netgame_flag_teleporter_target || target->team_index != source->team_index)
				continue;
			exits++;
			if (paired)
				continue;
			paired = TRUE;
			csmemset(&bots_globals.teleporters[teleporter], 0, sizeof(bots_globals.teleporters[teleporter]));
			bots_globals.teleporters[teleporter].entrance_surface_index =
				bots_surface_below(&source->position, &bots_globals.teleporters[teleporter].entrance);
			bots_globals.teleporters[teleporter].exit = target->position;
			bots_globals.teleporters[teleporter].ladder = FALSE;
			bots_globals.teleporters[teleporter].channel = source->team_index;
			bots_globals.teleporters[teleporter].exit_surface_index =
				bots_surface_below(&target->position, &bots_globals.teleporters[teleporter].exit_ground);
		}
		if (!paired || bots_globals.teleporters[teleporter].entrance_surface_index == NONE)
		{
			platform_log("bots: teleporter at (%.1f %.1f %.1f) channel %d: %s", source->position.x, source->position.y,
				source->position.z, (int)source->team_index, paired ? "on no ground (left out)" : "no exit (left out)");
			continue;
		}
		bots_globals.teleporter_count++;
		if (bots_globals.teleporters[teleporter].exit_surface_index != NONE)
			bots_globals.teleporter_shortcuts++;
		platform_log("bots: teleporter %ld at (%.1f %.1f %.1f) to (%.1f %.1f %.1f) channel %d, %.0f units%s%s",
			teleporter + 1, source->position.x, source->position.y, source->position.z,
			bots_globals.teleporters[teleporter].exit.x, bots_globals.teleporters[teleporter].exit.y,
			bots_globals.teleporters[teleporter].exit.z, (int)source->team_index,
			(double)bots_distance3d(&source->position, &bots_globals.teleporters[teleporter].exit),
			exits > 1 ? " (the first of the channel's exits, as the game sends a player)" : "",
			bots_globals.teleporters[teleporter].exit_surface_index == NONE ? " (its exit on no ground: no shortcut)" : "");
	}
	for (index = 0; index < scenario->players.count; index += 2)
	{
		struct player_starting_location *location =
			TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);

		bots_add_roam_point(&location->position);
	}
	/* (debug: HALO_BOT_TRACE, the map's starting locations and netgame
	flags) */
	if (bots_trace_enabled())
	{
		for (index = 0; index < scenario->players.count; index++)
		{
			struct player_starting_location *location =
				TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);

			platform_log("bots: trace starting location %ld at (%.1f %.1f %.1f) team %d games %d %d %d %d", index,
				location->position.x, location->position.y, location->position.z, (int)location->team_index,
				(int)location->game_types[0], (int)location->game_types[1], (int)location->game_types[2],
				(int)location->game_types[3]);
		}
		for (index = 0; index < scenario->netgame_flags.count; index++)
		{
			struct scenario_netgame_flag *flag =
				TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

			platform_log("bots: trace netgame flag %ld at (%.1f %.1f %.1f) type %d team %d", index,
				flag->position.x, flag->position.y, flag->position.z, (int)flag->type, (int)flag->team_index);
		}
	}
	{
		real_point3d low = { REAL_MAX, REAL_MAX, REAL_MAX }, high = { -REAL_MAX, -REAL_MAX, -REAL_MAX };

		for (index = 0; index < bots_globals.roam_point_count; index++)
		{
			low.x = MIN(low.x, bots_globals.roam_points[index].x);
			low.y = MIN(low.y, bots_globals.roam_points[index].y);
			high.x = MAX(high.x, bots_globals.roam_points[index].x);
			high.y = MAX(high.y, bots_globals.roam_points[index].y);
		}
		platform_log("bots: %ld places to roam on this map, over %.0f units; %ld teleporters", bots_globals.roam_point_count,
			bots_globals.roam_point_count > 0 ? (double)bots_distance2d(&low, &high) : 0.0, bots_globals.teleporter_count);
	}
	/* port: (the level's ladders: climbable surfaces - Boarding Action's
	sides, Prisoner's, Sidewinder's - which the walkable surfaces never go up;
	the bots climb them as they go through teleporters) */
	{
		struct structure_bsp *structure = global_structure_bsp_get();

		bots_globals.climbable_surfaces = 0;
		if (structure && structure->collision_bsp.count > 0)
		{
			struct collision_bsp const *bsp = TAG_BLOCK_GET_ELEMENT(&structure->collision_bsp, 0, struct collision_bsp);

			for (index = 0; index < bsp->surfaces.count; index++)
			{
				if (TEST_FLAG(TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, index, struct collision_surface)->flags,
					BOTS_COLLISION_SURFACE_CLIMBABLE_BIT))
				{
					bots_globals.climbable_surfaces++;
					/* (debug: HALO_BOT_TRACE, each one's middle, size and slope) */
					if (bots_trace_enabled())
					{
						real_point3d corners[BOTS_SURFACE_CORNERS], middle = { 0.0f, 0.0f, 0.0f };
						long corner, count = bots_surface_corners(bsp, index, corners);
						real low = REAL_MAX, high = -REAL_MAX;
						real_vector3d a, b, normal;

						for (corner = 0; corner < count; corner++)
						{
							middle.x += corners[corner].x / (real)count;
							middle.y += corners[corner].y / (real)count;
							middle.z += corners[corner].z / (real)count;
							low = MIN(low, corners[corner].z);
							high = MAX(high, corners[corner].z);
						}
						normal.i = normal.j = normal.k = 0.0f;
						if (count >= 3)
						{
							vector_from_points3d(&corners[0], &corners[1], &a);
							vector_from_points3d(&corners[0], &corners[2], &b);
							cross_product3d(&a, &b, &normal);
							normalize3d(&normal);
						}
						platform_log("bots: trace climbable surface %ld at (%.1f %.1f %.1f) z %.1f to %.1f normal (%.2f %.2f %.2f)",
							index, middle.x, middle.y, middle.z, low, high, normal.i, normal.j, normal.k);
					}
				}
			}
			bots_prepare_ladders(bsp);
			platform_log("bots: %ld climbable surfaces of %ld, %ld ladders", bots_globals.climbable_surfaces,
				(long)bsp->surfaces.count, bots_globals.ladder_count);
		}
	}
	/* (CTF: the teams' stands, the scenario's netgame flags, for the tests:
	run_bots_test.sh checks that each team spawns at its own) */
	if (game_engine_get_variant()->game_engine_index == game_engine_ctf)
	{
		real_point3d stands[2] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

		for (index = 0; index < scenario->netgame_flags.count; index++)
		{
			struct scenario_netgame_flag *flag =
				TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

			if (flag->type == _netgame_flag_ctf_flag && (flag->team_index == 0 || flag->team_index == 1))
				stands[flag->team_index] = flag->position;
		}
		platform_log("bots: CTF%s, the stands: team 0 (%.1f %.1f %.1f) team 1 (%.1f %.1f %.1f)",
			game_engine_get_variant()->game_engine_variant.ctf.assault ? " (assault: each team's flag at the other's)" : "",
			stands[0].x, stands[0].y, stands[0].z, stands[1].x, stands[1].y, stands[1].z);
	}
}

void bots_initialize_for_new_map(void)
{
	long index;

	/* (what was kept for the last map is found again) */
	bots_map_generation++;
	csmemset(bots_globals.bots, 0, sizeof(bots_globals.bots));
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		bots_globals.bots[index].player_index = NONE;
		bots_globals.bots[index].search_slot = NONE;
	}
	bots_globals.map_ready = FALSE;
	bots_globals.roam_point_count = 0;
	bots_globals.tick_us = 0;
	bots_globals.search_us = 0;
	bots_globals.ticks = 0;
	bots_globals.bot_ticks = 0;
	bots_globals.searches = 0;
	bots_globals.search_failures = 0;
	bots_globals.search_partials = 0;
	bots_globals.search_overflows = 0;
	bots_globals.search_unavoided = 0;
	bots_globals.search_unsmoothed = 0;
	bots_globals.flag_grabs = 0;
	bots_globals.ball_grabs = 0;
	bots_globals.stand_visits = 0;
	csmemset(bots_profile.part_us, 0, sizeof(bots_profile.part_us));
	bots_profile.below_rays = 0;
	bots_profile.sight_rays = 0;
	bots_profile.below_us = 0;
	bots_profile.sight_us = 0;
	/* (the first line a minute into the game) */
	bots_globals.last_report_tick = 0;
	bots_globals.vehicle_entries = 0;
	csmemset(bots_globals.vehicle_entries_by_seat, 0, sizeof(bots_globals.vehicle_entries_by_seat));
	csmemset(bots_globals.vehicle_exits, 0, sizeof(bots_globals.vehicle_exits));
	bots_globals.vehicle_flips = 0;
	bots_globals.driven_distance = 0.0f;
	bots_globals.riding_kills[0] = bots_globals.riding_kills[1] = 0;
	bots_globals.riding_fire_ticks = 0;
	bots_globals.driving_searches = 0;
	csmemset(bots_globals.vehicle_entries_by_kind, 0, sizeof(bots_globals.vehicle_entries_by_kind));
	bots_globals.first_entry_tick = 0;
	bots_globals.vehicle_take_overs = 0;
	bots_globals.vehicle_giveups = 0;
	bots_globals.riding_ticks = 0;
	bots_globals.last_census_tick = 0;
	bots_globals.vehicles_listed = FALSE;
	bots_globals.stuck_events = 0;
	bots_globals.stuck_low = 0;
	bots_globals.stuck_tall = 0;
	bots_globals.stuck_clear = 0;
	bots_globals.stuck_giveups = 0;
	csmemset(bots_globals.combat_counts, 0, sizeof(bots_globals.combat_counts));
	bots_globals.climbable_surfaces = 0;
	bots_globals.ladder_count = 0;
	bots_globals.ladder_climbs = 0;
	bots_globals.ladder_tries = 0;
	bots_globals.teleporter_shortcuts = 0;
	bots_globals.shortcut_measures = 0;
	bots_globals.shortcut_takes = 0;
	csmemset(bots_globals.shortcut_through, 0, sizeof(bots_globals.shortcut_through));
	bots_globals.shortcut_failures = 0;
	bots_globals.shortcut_drops = 0;
	csmemset(bots_globals.powerups_sought, 0, sizeof(bots_globals.powerups_sought));
	csmemset(bots_globals.powerups_taken, 0, sizeof(bots_globals.powerups_taken));
	bots_globals.bodies_restored = 0;
	bots_globals.turret_entries = 0;
	bots_globals.turret_kills = 0;
	bots_globals.turret_fire_ticks = 0;
	bots_globals.turret_count = 0;
	bots_powerups.count = 0;
	bots_powerups.found_tick = NONE;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		bots_globals.bots[index].vehicle_index = NONE;
		bots_globals.bots[index].vehicle_avoid_index = NONE;
		bots_globals.bots[index].ride_vehicle_index = NONE;
	}
	bots_helper.helper_us = 0;
	/* port: (the routes, vantage points and roles: bots_tactics.h) */
	bots_tactics_initialize_for_new_map();
}

void bots_dispose_from_old_map(void)
{
	/* (a search still running finishes on the old level's data, which is
	still loaded; the answers are dropped) */
	bots_paths_release();
	bots_globals.map_ready = FALSE;
	bots_tactics_dispose_from_old_map();
}

/* ---------- path searches

A search's state changes under the helper's lock (the game's structures are
compiled for any alignment, -fmax-type-align=1 on the Vita, where an atomic
operation on them would be a library call); the search itself runs outside
it. The tick holds the lock only to look at or change a state. */

static long bots_search_state(struct bots_search *search)
{
	long state;

	pthread_mutex_lock(&bots_helper.lock);
	state = search->state;
	pthread_mutex_unlock(&bots_helper.lock);
	return state;
}

static void bots_search_set_state(struct bots_search *search, long state)
{
	pthread_mutex_lock(&bots_helper.lock);
	search->state = state;
	pthread_mutex_unlock(&bots_helper.lock);
}

static void bots_search_run(struct bots_search *search)
{
	struct path_input input;
	unsigned long long started = bots_time_us();

	path_input_new(&input, search->radius, FALSE, search->unit_index);
	/* (a vehicle to get into: the path goes up to it, not round it) */
	if (search->target_object_index != NONE)
		path_input_set_target_object(&input, search->target_object_index);
	path_input_set_start(&input, &search->start_point, search->start_surface_index);
	path_state_new(&input, &search->state_memory, NULL);
	/* (the walkable surfaces less those closed by what is in the way) */
	pthread_mutex_lock(&bots_helper.lock);
	if (bots_paths.active && search->state_memory.structure == bots_paths.source)
	{
		search->state_memory.structure = search->radius > BOTS_PATH_RADIUS ? &bots_paths.vehicle_structure :
			&bots_paths.structure;
	}
	pthread_mutex_unlock(&bots_helper.lock);
	/* (a path to as near the goal as the search reaches: a long way across a
	large level is more surfaces than a search holds, and the bot searches
	again from there) */
	path_state_destination(&search->state_memory, &search->goal_point, search->goal_surface_index, 1000.0f);
	path_state_find(&search->state_memory);
	search->found = TRUE;
	pthread_mutex_lock(&bots_helper.lock);
	bots_helper.helper_us += bots_time_us() - started;
	pthread_mutex_unlock(&bots_helper.lock);
}

static void *bots_helper_main(void *argument)
{
	(void)argument;
	/* (path.c: this thread's searches are not the tick's) */
	for (;;)
	{
		long index;

		pthread_mutex_lock(&bots_helper.lock);
		while (!bots_helper.pending)
			pthread_cond_wait(&bots_helper.wake, &bots_helper.lock);
		bots_helper.pending = 0;
		pthread_mutex_unlock(&bots_helper.lock);
		for (index = 0; index < BOTS_SEARCH_SLOTS; index++)
		{
			struct bots_search *search = &bots_globals.search_slots[index];
			boolean mine = FALSE;

			pthread_mutex_lock(&bots_helper.lock);
			if (search->state == _bots_search_waiting)
			{
				search->state = _bots_search_running;
				mine = TRUE;
			}
			pthread_mutex_unlock(&bots_helper.lock);
			if (mine)
			{
				bots_search_run(search);
				bots_search_set_state(search, _bots_search_done);
			}
		}
	}
	return NULL;
}

static void bots_helper_start(void)
{
	char const *fixed = getenv("HALO_FIXED_TICK");
	char const *threaded = getenv("HALO_BOT_THREAD");

	bots_helper.started = TRUE;
	bots_globals.search_slots = (struct bots_search *)calloc(BOTS_SEARCH_SLOTS, sizeof(struct bots_search));
	/* (port: in the tick unless HALO_BOT_THREAD=1 asks for the helper thread,
	so that the bots do the same in two runs of the same input, the benchmark's
	(benchmark.c); HALO_FIXED_TICK in the tick always) */
	bots_helper.synchronous = (fixed && atoi(fixed)) || !(threaded && atoi(threaded));
	if (!bots_helper.synchronous)
	{
		pthread_attr_t attributes;

		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, 256 * 1024);
		if (pthread_create(&bots_helper.thread, &attributes, bots_helper_main, NULL))
			bots_helper.synchronous = TRUE;
		pthread_attr_destroy(&attributes);
	}
	platform_log("bots: path searches %s", bots_helper.synchronous ? "in the tick" : "on a helper thread");
}

/* port: (teleporters as shortcuts, below) */
static boolean bots_shortcut_same_goal(struct bots_bot *bot, real_point3d const *position);
static void bots_shortcut_drop(struct bots_bot *bot, char const *why);

/* asks for a path from where the bot's unit is to its goal */
static void bots_search_ask(struct bots_bot *bot, long unit_index)
{
	struct bots_search *search = NULL;
	real_point3d start;
	long start_surface_index;
	long slot;
	/* port: a bot driving searches from where its vehicle is, as wide as
	the vehicle, the vehicle the search's own object */
	long vehicle_index = bots_unit_vehicle(unit_index);

	/* (one at a time a bot; a free slot, else again next tick) */
	if (bot->search_slot != NONE || !bots_globals.search_slots)
		return;
	for (slot = 0; slot < BOTS_SEARCH_SLOTS && !search; slot++)
	{
		if (bots_search_state(&bots_globals.search_slots[slot]) == _bots_search_idle)
			search = &bots_globals.search_slots[slot];
	}
	if (!search)
		return;
	start_surface_index = vehicle_index != NONE ? vehicle_find_pathfinding_surface_index(vehicle_index, &start) :
		bots_unit_surface(unit_index, &start);
	if (start_surface_index == NONE || bot->goal_surface_index == NONE)
		return;
	/* (on a surface closed to the searches, whose way out may be closed
	too: away from what closed it first, a second) */
	if (vehicle_index == NONE && bots_paths.walkable && start_surface_index < bots_paths.structure.pathfinding_surfaces.count &&
		!TEST_FLAG(bots_paths.walkable[start_surface_index], BOTS_PATHFINDING_WALKABLE_BIT) &&
		bots_paths_escape_direction(&start, &bot->escape_direction))
	{
		bot->escape_until_tick = game_time_get() + TICKS_PER_SECOND;
		bot->path.valid = FALSE;
		bot->path_tick = game_time_get();
		return;
	}
	bot->search_slot = (long)(search - bots_globals.search_slots);
	search->bot_index = (long)(bot - bots_globals.bots);
	search->unit_index = vehicle_index != NONE ? vehicle_index : unit_index;
	search->radius = vehicle_index != NONE ? bots_vehicle_path_radius(vehicle_index) : BOTS_PATH_RADIUS;
	search->target_object_index = bot->goal_kind == _bots_goal_vehicle ? bot->vehicle_index : NONE;
	search->start_point = start;
	search->start_surface_index = start_surface_index;
	search->goal_point = bot->goal_point;
	search->goal_surface_index = bot->goal_surface_index;
	search->purpose = _bots_search_for_path;
	/* port: a teleporter as a shortcut (bots_shortcut_weigh), for this goal
	on foot alone: the walk from its exit to the goal measured (the bot
	walks its path meanwhile), then the bot's path to its entrance */
	if (bot->shortcut_phase != _bots_shortcut_none &&
		(vehicle_index != NONE || !bots_shortcut_same_goal(bot, &start)))
	{
		bots_shortcut_drop(bot, vehicle_index != NONE ? "riding" : "another goal");
	}
	if (bot->shortcut_phase == _bots_shortcut_measuring)
	{
		search->purpose = _bots_search_for_exit;
		search->start_point = bots_globals.teleporters[bot->shortcut_index].exit_ground;
		search->start_surface_index = bots_globals.teleporters[bot->shortcut_index].exit_surface_index;
	}
	else if (bot->shortcut_phase == _bots_shortcut_going)
	{
		search->purpose = _bots_search_for_entrance;
		search->goal_point = bots_globals.teleporters[bot->shortcut_index].entrance;
		search->goal_surface_index = bots_globals.teleporters[bot->shortcut_index].entrance_surface_index;
	}
	/* port: Bot routes Lanes or Mixed (bots_routes.c): on foot, to the
	waypoint of the bot's lane on its way */
	else if (vehicle_index == NONE && (bot->goal_kind == _bots_goal_objective || bot->goal_kind == _bots_goal_roam ||
		bot->goal_kind == _bots_goal_enemy))
	{
		bots_routes_search_goal(search->bot_index, &start, &search->goal_point, &search->goal_surface_index);
	}
	search->asked_tick = game_time_get();
	search->goal_serial = bot->goal_serial;
	search->found = FALSE;
	bots_globals.searches++;
	if (vehicle_index != NONE)
		bots_globals.driving_searches++;
	bot->searches++;
	if (bots_helper.synchronous)
	{
		unsigned long long started = bots_time_us();

		bots_search_run(search);
		bots_globals.search_us += bots_time_us() - started;
		search->state = _bots_search_done;
		return;
	}
	pthread_mutex_lock(&bots_helper.lock);
	search->state = _bots_search_waiting;
	bots_helper.pending = 1;
	pthread_cond_signal(&bots_helper.wake);
	pthread_mutex_unlock(&bots_helper.lock);
}

/* (debug) HALO_BOT_PATH_LOG=1: each failed search's ask and answer, and for
one that reached its goal, the obstacles its path's avoidance met */
static void bots_search_log_failure(struct bots_bot *bot, struct bots_search *search, boolean reached)
{
	struct path_state *state = &search->state_memory;
	short reached_node = reached ? path_node_from_hash_table(state, search->goal_surface_index) : NONE;
	static struct path_debug_storage *debug;
	struct path_result again;
	long step;

	platform_log("bots: path failed bot %ld goal %d from (%.1f %.1f %.1f) s%ld to (%.1f %.1f %.1f) s%ld dist %.1f: "
		"nodes %d heap %d closest %.1f at (%.1f %.1f %.1f) reached %d",
		(long)(bot - bots_globals.bots) + 1, (int)bot->goal_kind,
		search->start_point.x, search->start_point.y, search->start_point.z, search->start_surface_index,
		search->goal_point.x, search->goal_point.y, search->goal_point.z, search->goal_surface_index,
		bots_distance3d(&search->start_point, &search->goal_point), (int)state->node_count, (int)state->heap_count,
		state->closest_distance, state->closest_point.x, state->closest_point.y, state->closest_point.z,
		(int)reached_node);
	if (reached_node == NONE)
		return;
	if (!debug)
		debug = (struct path_debug_storage *)calloc(1, sizeof(*debug));
	if (!debug)
		return;
	csmemset(debug, 0, sizeof(*debug));
	state->debug = debug;
	path_state_build_path(state, &again);
	state->debug = NULL;
	platform_log("bots:   depth %d build %d raw %d smoothed %d avoided %d", (int)state->node_list[reached_node].depth,
		(int)debug->path_build_result, (int)debug->raw_step_count, (int)debug->smoothed_step_count,
		(int)debug->avoided_step_count);
	for (step = 0; step < debug->avoidance_path_count && step < 4; step++)
	{
		struct obstacles *obstacles = &debug->avoidance_obstacles[step];
		long disc;

		platform_log("bots:   step %ld to (%.1f %.1f): %d obstacles", step, debug->smoothed_steps[step].point.x,
			debug->smoothed_steps[step].point.y, (int)obstacles->disc_count);
		for (disc = 0; disc < obstacles->disc_count && disc < 8; disc++)
		{
			struct object_datum *object = object_try_and_get(obstacles->discs[disc].object_index);

			platform_log("bots:     (%.1f %.1f) radius %.2f %s", obstacles->discs[disc].center.x,
				obstacles->discs[disc].center.y, obstacles->discs[disc].radius,
				object ? tag_get_name(object->definition_index) : "?");
		}
	}
}

/* the goal is on ground the search does not walk to from where the bot is
(Chiron TL34's rooms, joined by teleporters alone): a teleporter the search
reached whose exit is nearer the goal than the search came (two units at
least, so that a bot never goes round in teleporters), the one whose way
there and exit's distance to the goal are least; the path to it built.
TRUE when there is one */
static boolean bots_search_teleporter(struct bots_bot *bot, struct bots_search *search, struct path_result *path)
{
	struct path_state *state = &search->state_memory;
	long index, best = NONE;
	real best_cost = REAL_MAX;

	for (index = 0; index < bots_globals.teleporter_count; index++)
	{
		short node_index = path_node_from_hash_table(state, bots_globals.teleporters[index].entrance_surface_index);
		real beyond = bots_distance3d(&bots_globals.teleporters[index].exit, &search->goal_point);
		real cost;

		/* port: (a ladder no bot got up, tried three times: none; and a
		teleporter that failed bots three times as a shortcut and never sent
		one: bots_shortcut_fail) */
		if (bots_globals.teleporters[index].ladder && bots_globals.teleporters[index].failures >= 3 &&
			!bots_globals.teleporters[index].climbs)
		{
			continue;
		}
		if (!bots_globals.teleporters[index].ladder && bots_globals.teleporters[index].failures >= 3 &&
			!bots_globals.teleporters[index].through[_bots_through_shortcut] &&
			!bots_globals.teleporters[index].through[_bots_through_last_way] &&
			!bots_globals.teleporters[index].through[_bots_through_by_chance])
		{
			continue;
		}
		if (node_index == NONE || beyond > state->closest_distance - 2.0f)
			continue;
		cost = state->node_list[node_index].path_distance_from_origin + beyond;
		if (cost < best_cost)
		{
			best_cost = cost;
			best = index;
		}
	}
	if (best == NONE)
		return FALSE;
	state->destination.point = bots_globals.teleporters[best].entrance;
	state->destination.surface_index = bots_globals.teleporters[best].entrance_surface_index;
	if (!path_state_build_path(state, path) || path->step_count <= 0)
	{
		/* (the bot on the teleporter already, as it comes out: onto it) */
		if (bots_distance2d(&search->start_point, &bots_globals.teleporters[best].entrance) > 1.0f)
			return FALSE;
		csmemset(path, 0, sizeof(*path));
	}
	bot->via_teleporter = TRUE;
	bot->via_point = bots_globals.teleporters[best].entrance;
	bot->via_tick = game_time_get();
	bot->via_index = best;
	bot->via_climb_tick = 0;
	bot->via_top_tick = 0;
	if (bots_globals.teleporters[best].ladder)
	{
		bots_globals.ladder_tries++;
		if (bots_trace_enabled())
			platform_log("bots: trace bot %ld to the ladder at (%.1f %.1f %.1f), for (%.1f %.1f %.1f)", (long)(bot - bots_globals.bots) + 1,
				bot->via_point.x, bot->via_point.y, bot->via_point.z, search->goal_point.x, search->goal_point.y, search->goal_point.z);
	}
	return TRUE;
}

/* ---------- port: teleporters as shortcuts

A goal the walkable surfaces reach was walked to the whole way, through no
teleporter however much shorter it was (Blood Gulch's, from beside each
base's flag out to the middle of the canyon, half the way to the other base;
Battle Creek's between the bases). Now a bot on foot weighs the map's
teleporters as a search to its goal answers: the walk (the search's own, the
surfaces' entry points apart; as near as it came and on in a straight line,
for a search that stopped short) against the way through one - the walk to
its entrance (the same search's, when it came to it; else a straight line,
as short as it can be) and from its exit on to the goal (a straight line
first). The one whose way through is least, 80% of the walk at most and 15
units shorter at least, is weighed again with the walk from its exit
measured: a search from the exit's ground to the goal (the bot walks its
path meanwhile; nothing of it is walked). Still as short, the bot takes it
(an entrance weighed by a straight line, once its first search there says
how far it is, and it is still short enough): its searches go to the
entrance, it walks onto it as onto the last way's (bots_search_teleporter:
the path's end, then onto the flag), and goes on from where it comes out
with a new search (no teleporter weighed again for that goal). Not for every
bot, nor every goal: on foot alone (a vehicle does not go through: a driver
drives on, and getting out is the vehicle's to weigh); to the game's
objective, a roaming place or an enemy last seen (an item or a vehicle is
near, and comes first; a fight goes on first, its clock stopped; a bot fresh
from its base with a vehicle near weighs them once its ten fresh seconds are
over, its base's vehicles weighed first); never a defender (bots_roles.c: it
keeps about its flag, or its hill), nor a bot on a side lane (bots_routes.c:
the lanes spread a team out, the teleporter would gather it again); two bots
at most go for one teleporter at once, and a bot weighs them once a goal,
five seconds apart at least. A teleporter that fails a bot - it stood on the
entrance two seconds and was not sent (another player on the exit blocks it:
game_engine.c), or did not come to it in time (the walk there at a slow
pace, and eight seconds), or its search finds no way to the entrance
(Damnation's upper one, from below) - is left be thirty seconds by every
bot, and one that failed three times and never sent a bot, for the map (the
last way too, as a ladder no bot got up). halo.log has the map's
teleporters, each shortcut taken, each bot sent and each failure, and each
minute's census in the moves line and the teleporters line (each
teleporter's bots sent, by how they came to it: a shortcut, the last way, or
by chance) */

/* whether bots take teleporters as shortcuts (HALO_BOT_SHORTCUTS=0: never,
the bots as before, for a comparison) */
static boolean bots_shortcuts_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		char const *setting = getenv("HALO_BOT_SHORTCUTS");

		enabled = setting && setting[0] && !atoi(setting) ? 0 : 1;
	}
	return enabled != 0;
}

/* the goals a shortcut is taken to: the game's objective (a vantage point
too), a roaming place, an enemy last seen */
static boolean bots_shortcut_goal_kind(struct bots_bot const *bot)
{
	return bot->goal_kind == _bots_goal_objective || bot->goal_kind == _bots_goal_roam ||
		bot->goal_kind == _bots_goal_enemy;
}

/* whether the way through (to the entrance and from the exit) is short
enough against the walk */
static boolean bots_shortcut_pays(real walk, real through)
{
	return through <= walk * BOTS_SHORTCUT_SHARE && walk - through >= BOTS_SHORTCUT_GAIN;
}

/* whether the bot's goal is still the one its shortcut was taken for: the
same, or one of the kinds a shortcut is taken to that moved a little (an
enemy last seen further on, the ball's carrier, another roaming place
beside it), or moved further and the teleporter still clearly short of the
way there in straight lines (the walk there as short as it can be: the
enemy chased on through it); the shortcut then for it */
static boolean bots_shortcut_same_goal(struct bots_bot *bot, real_point3d const *position)
{
	real_point3d const *entrance = &bots_globals.teleporters[bot->shortcut_index].entrance;
	real_point3d const *exit = &bots_globals.teleporters[bot->shortcut_index].exit_ground;

	if (!bots_shortcut_goal_kind(bot))
		return FALSE;
	if (bot->goal_serial == bot->shortcut_serial)
		return TRUE;
	if (bots_distance3d(&bot->goal_point, &bot->shortcut_goal) > 8.0f &&
		!bots_shortcut_pays(bots_distance3d(position, &bot->goal_point),
			bots_distance3d(position, entrance) + bots_distance3d(exit, &bot->goal_point)))
	{
		return FALSE;
	}
	bot->shortcut_serial = bot->goal_serial;
	bot->shortcut_goal = bot->goal_point;
	return TRUE;
}

/* (debug: HALO_BOT_SHORTCUT_LOG) a goal's kind */
static char const *bots_goal_kind_name(short kind)
{
	return kind == _bots_goal_objective ? " (objective)" : kind == _bots_goal_roam ? " (roaming)" :
		kind == _bots_goal_enemy ? " (an enemy)" : "";
}

/* the times a teleporter sent a bot, however it came to it */
static long bots_teleporter_sent(long index)
{
	long kind, sent = 0;

	for (kind = 0; kind < NUMBER_OF_BOTS_THROUGH; kind++)
		sent += bots_globals.teleporters[index].through[kind];
	return sent;
}

/* whether a teleporter is one to go for as a shortcut now: a teleporter
(not a ladder) whose exit is on the level's ground, not left be */
static boolean bots_shortcut_usable(long index, long now)
{
	return !bots_globals.teleporters[index].ladder && bots_globals.teleporters[index].exit_surface_index != NONE &&
		now >= bots_globals.teleporters[index].rest_until_tick &&
		!(bots_globals.teleporters[index].failures >= 3 && !bots_teleporter_sent(index));
}

/* the bots going for a teleporter as a shortcut (or weighing it), but one */
static long bots_shortcut_claimers(long index, struct bots_bot const *except)
{
	long bot_index, count = 0;

	for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
	{
		struct bots_bot const *other = &bots_globals.bots[bot_index];

		if (other != except && other->player_index != NONE && other->shortcut_phase != _bots_shortcut_none &&
			other->shortcut_index == index)
		{
			count++;
		}
	}
	return count;
}

/* how far a search walked to a surface it came to, and on to the point on
it (the surfaces' entry points apart: the raw way, a little longer than the
smoothed path walked, as long for every way weighed); a negative for one it
did not come to */
static real bots_search_walk(struct path_state const *state, long surface_index, real_point3d const *point)
{
	short node_index = surface_index != NONE ? path_node_from_hash_table((struct path_state *)state, surface_index) : NONE;

	if (node_index == NONE || node_index < 0 || node_index >= state->node_count)
		return -1.0f;
	return state->node_list[node_index].path_distance_from_origin +
		bots_distance3d(&state->node_list[node_index].entry_point, point);
}

/* the shortcut let go (another goal, riding, another teleporter, its
searches to the entrance unanswered): the bot's path to its goal again, the
teleporters not weighed again for this goal */
static void bots_shortcut_drop(struct bots_bot *bot, char const *why)
{
	if (bot->shortcut_phase == _bots_shortcut_none)
		return;
	if (bot->shortcut_phase == _bots_shortcut_going && bot->shortcut_entrance_known)
	{
		bots_globals.shortcut_drops++;
		platform_log("bots: bot %ld off teleporter %ld (%s) after %.0f s", (long)(bot - bots_globals.bots) + 1,
			bot->shortcut_index + 1, why, (double)(game_time_get() - bot->shortcut_tick) / TICKS_PER_SECOND);
	}
	if (bot->shortcut_phase == _bots_shortcut_going)
	{
		if (bot->via_teleporter && bot->via_index == bot->shortcut_index)
			bot->via_teleporter = FALSE;
		bot->path.valid = FALSE;
	}
	bot->shortcut_phase = _bots_shortcut_none;
	bot->shortcut_weighed_serial = bot->goal_serial;
}

/* the teleporter failed the bot (not sent from its entrance, or not come to
in time): left be a while by every bot (for the map after three failures,
none sent), the bot on to its goal on foot */
static void bots_shortcut_fail(struct bots_bot *bot, char const *why, long now)
{
	long index = bot->shortcut_index;

	bots_globals.teleporters[index].failures++;
	bots_globals.teleporters[index].rest_until_tick = now + TICKS_PER_SECOND * BOTS_SHORTCUT_REST_SECONDS;
	bots_globals.shortcut_failures++;
	platform_log("bots: teleporter %ld failed bot %ld (%s) after %.0f s: left be %s", index + 1,
		(long)(bot - bots_globals.bots) + 1, why, (double)(now - bot->shortcut_tick) / TICKS_PER_SECOND,
		bots_shortcut_usable(index, bots_globals.teleporters[index].rest_until_tick) ? "a while" : "for the map");
	if (bot->via_teleporter && bot->via_index == index)
		bot->via_teleporter = FALSE;
	bot->shortcut_phase = _bots_shortcut_none;
	bot->shortcut_weighed_serial = bot->goal_serial;
	bot->path.valid = FALSE;
	bot->path_tick = now - TICKS_PER_SECOND;
}

/* whether bots take vehicles and one a bot may drive is near (25 units:
its base's), not destroyed nor closed to players */
static boolean bots_shortcut_vehicle_near(real_point3d const *position)
{
	struct object_iterator iterator;

	if (!bots_vehicles_enabled())
		return FALSE;
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct unit_datum *vehicle = unit_get(iterator.index);

		if (!TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) &&
			!TEST_FLAG(vehicle->unit.flags, _unit_not_enterable_by_player_bit) && bots_vehicle_drivable(iterator.index) &&
			bots_distance2d(&vehicle->object.position, position) < 25.0f)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* a search for the bot's path answered: the teleporters weighed against the
walk it found (above), the best one's exit measured next. Its path stands
meanwhile */
static void bots_shortcut_weigh(struct bots_bot *bot, struct bots_search *search, boolean reached)
{
	struct path_state const *state = &search->state_memory;
	long bot_index = (long)(bot - bots_globals.bots);
	struct bots_tactics_bot const *tactics = &bots_tactics_bots[bot_index];
	long now = game_time_get(), index, best = NONE;
	real walk, best_through = REAL_MAX, best_entrance = 0.0f;
	boolean best_known = FALSE;

	if (!bots_globals.teleporter_shortcuts || !bots_shortcuts_enabled() || bot->shortcut_phase != _bots_shortcut_none ||
		search->purpose != _bots_search_for_path || search->radius > BOTS_PATH_RADIUS || !bots_shortcut_goal_kind(bot) ||
		bot->goal_serial == bot->shortcut_weighed_serial || now < bot->shortcut_weigh_tick)
	{
		return;
	}
	/* (the search went to the goal itself, not a lane's waypoint; never a
	defender, nor a bot on a side lane; a bot fresh from its base with a
	vehicle about weighs them once the vehicles are weighed, its base's
	first: bots_vehicle_consider, a teleporter beside its flag taken at once
	would leave them all behind - Blood Gulch's) */
	if (bots_distance3d(&search->goal_point, &bot->goal_point) > 0.5f || tactics->role == _bots_role_defender ||
		(tactics->lane != 0 && !tactics->lane_given_up) ||
		(now - bot->spawn_tick < TICKS_PER_SECOND * BOTS_VEHICLE_FRESH_SECONDS && bots_shortcut_vehicle_near(&search->start_point)))
	{
		return;
	}
	bot->shortcut_weigh_tick = now + TICKS_PER_SECOND * BOTS_SHORTCUT_WEIGH_SECONDS;
	bot->shortcut_weighed_serial = bot->goal_serial;
	/* (the walk: to the goal, else as near as the search came and on in a
	straight line - one stopped short, or out of nodes: Blood Gulch's from
	base to base takes more surfaces than a search holds; a search spent,
	the goal on ground it does not walk to, is the last way's, bots_search_
	teleporter) */
	if (reached)
		walk = bots_search_walk(state, search->goal_surface_index, &search->goal_point);
	else if ((state->heap_count > 1 || state->node_count >= PATH_NODE_LIST_SIZE) && state->closest_node_index >= 0 &&
		state->closest_node_index < state->node_count)
		walk = state->node_list[state->closest_node_index].path_distance_from_origin + state->closest_distance;
	else
		return;
	if (walk < BOTS_SHORTCUT_GAIN * 2.0f)
		return;
	for (index = 0; index < bots_globals.teleporter_count; index++)
	{
		real entrance, through;
		boolean known;

		if (!bots_shortcut_usable(index, now) || bots_shortcut_claimers(index, bot) >= BOTS_SHORTCUT_BOTS)
			continue;
		/* (an exit where the bot is: no way on) */
		if (bots_distance3d(&bots_globals.teleporters[index].exit_ground, &search->start_point) < 8.0f)
			continue;
		entrance = bots_search_walk(state, bots_globals.teleporters[index].entrance_surface_index,
			&bots_globals.teleporters[index].entrance);
		known = entrance >= 0.0f;
		if (!known)
			entrance = bots_distance3d(&search->start_point, &bots_globals.teleporters[index].entrance);
		through = entrance + bots_distance3d(&bots_globals.teleporters[index].exit_ground, &search->goal_point);
		/* (debug) HALO_BOT_SHORTCUT_LOG=1: each teleporter weighed */
		if (getenv("HALO_BOT_SHORTCUT_LOG"))
		{
			platform_log("bots: shortcut bot %ld from (%.1f %.1f %.1f) to (%.1f %.1f %.1f)%s: the walk %.0f%s, teleporter %ld "
				"%.0f to it%s and %.0f on: %s", bot_index + 1, search->start_point.x, search->start_point.y,
				search->start_point.z, search->goal_point.x, search->goal_point.y, search->goal_point.z,
				bots_goal_kind_name(bot->goal_kind), (double)walk, reached ? "" : " (short)", index + 1, (double)entrance,
				known ? "" : " (straight)", (double)(through - entrance), bots_shortcut_pays(walk, through) ? "pays" : "no");
		}
		if (bots_shortcut_pays(walk, through) && through < best_through)
		{
			best = index;
			best_through = through;
			best_entrance = entrance;
			best_known = known;
		}
	}
	if (best == NONE)
		return;
	bot->shortcut_phase = _bots_shortcut_measuring;
	bot->shortcut_index = best;
	bot->shortcut_serial = bot->goal_serial;
	bot->shortcut_goal = bot->goal_point;
	bot->shortcut_walk = walk;
	bot->shortcut_walk_short = !reached;
	bot->shortcut_entrance = best_entrance;
	bot->shortcut_entrance_known = best_known;
	bot->shortcut_tick = now;
	/* (the measuring search asked next tick; given up if not answered in
	ten seconds) */
	bot->shortcut_deadline_tick = now + TICKS_PER_SECOND * 10;
	bot->path_tick = now - TICKS_PER_SECOND * 4 - 1;
}

/* the shortcut taken: halo.log's line, and the time it has to come to the
entrance (the walk there at a slow pace, a unit and a half a second, and
eight seconds) */
static void bots_shortcut_take(struct bots_bot *bot, long now)
{
	bot->shortcut_tick = now;
	bot->shortcut_deadline_tick = now + (long)(TICKS_PER_SECOND * (8.0f + bot->shortcut_entrance / 1.5f));
	bots_globals.shortcut_takes++;
	platform_log("bots: bot %ld to teleporter %ld for (%.1f %.1f %.1f): %.0f units to it and %.0f from its exit, the walk %.0f%s",
		(long)(bot - bots_globals.bots) + 1, bot->shortcut_index + 1, bot->goal_point.x, bot->goal_point.y,
		bot->goal_point.z, (double)bot->shortcut_entrance, (double)bot->shortcut_beyond, (double)bot->shortcut_walk,
		bot->shortcut_walk_short ? " at least" : "");
}

/* the walk from the teleporter's exit to the goal measured: the shortcut
taken when the way through is still short enough */
static void bots_shortcut_measured(struct bots_bot *bot, struct bots_search *search)
{
	struct path_state const *state = &search->state_memory;
	long now = game_time_get();
	real beyond;

	if (bot->shortcut_phase != _bots_shortcut_measuring || search->goal_serial != bot->shortcut_serial)
		return;
	bots_globals.shortcut_measures++;
	/* (to the goal; a search out of nodes, as near as it came and on in a
	straight line, as the walk it is weighed against was) */
	beyond = state->node_count > 0 ? bots_search_walk(state, search->goal_surface_index, &search->goal_point) : -1.0f;
	if (beyond < 0.0f && state->node_count >= PATH_NODE_LIST_SIZE && state->closest_node_index >= 0 &&
		state->closest_node_index < state->node_count)
	{
		beyond = state->node_list[state->closest_node_index].path_distance_from_origin + state->closest_distance;
	}
	/* (debug) HALO_BOT_SHORTCUT_LOG=1: the walk from the exit */
	if (getenv("HALO_BOT_SHORTCUT_LOG"))
	{
		platform_log("bots: shortcut bot %ld from teleporter %ld's exit (%.1f %.1f %.1f) s%ld: %.0f on (nodes %d heap %d closest %.1f)",
			(long)(bot - bots_globals.bots) + 1, bot->shortcut_index + 1, search->start_point.x, search->start_point.y,
			search->start_point.z, search->start_surface_index, (double)beyond, (int)state->node_count,
			(int)state->heap_count, (double)state->closest_distance);
	}
	if (beyond < 0.0f || !bots_shortcut_pays(bot->shortcut_walk, bot->shortcut_entrance + beyond))
	{
		bot->shortcut_phase = _bots_shortcut_none;
		return;
	}
	/* (its searches to the entrance next: the shortcut taken now, or once
	the first says how far the entrance is, when weighed by a straight line) */
	bot->shortcut_phase = _bots_shortcut_going;
	bot->shortcut_beyond = beyond;
	bot->shortcut_on_tick = 0;
	bot->path_tick = now - TICKS_PER_SECOND * 4 - 1;
	if (bot->shortcut_entrance_known)
		bots_shortcut_take(bot, now);
	else
		bot->shortcut_deadline_tick = now + TICKS_PER_SECOND * 10;
}

/* a search to the teleporter's entrance answered: the walk there, weighed
by a straight line, checked (no longer short enough, the shortcut not taken
after all); TRUE when the path to it (or towards it) stands */
static boolean bots_shortcut_entrance_answer(struct bots_bot *bot, struct bots_search *search, boolean reached)
{
	if (bot->shortcut_phase != _bots_shortcut_going || search->goal_serial != bot->shortcut_serial)
		return FALSE;
	/* (the search spent, the entrance not among the surfaces the bot walks
	to: a failure, Damnation's upper one from below; a search that stopped
	short is a way towards it, the next from nearer) */
	if (!reached && search->state_memory.heap_count <= 1 && search->state_memory.node_count < PATH_NODE_LIST_SIZE)
	{
		bots_shortcut_fail(bot, "no way to its entrance", game_time_get());
		return FALSE;
	}
	if (!reached)
		return TRUE;
	if (!bot->shortcut_entrance_known)
	{
		real entrance = bots_search_walk(&search->state_memory, search->goal_surface_index, &search->goal_point);

		/* (further than the straight line said, and no longer short enough:
		not taken after all) */
		if (!bots_shortcut_pays(bot->shortcut_walk, entrance + bot->shortcut_beyond))
		{
			bot->shortcut_phase = _bots_shortcut_none;
			return FALSE;
		}
		bot->shortcut_entrance_known = TRUE;
		bot->shortcut_entrance = entrance;
		bots_shortcut_take(bot, game_time_get());
	}
	return TRUE;
}

/* the bot moved further than a step at once: sent by a teleporter when it
was at one's entrance and is at its exit (halo.log's census, by how it came
to it); its shortcut done, or dropped for another teleporter */
static void bots_teleporter_landed(struct bots_bot *bot, real_point3d const *from, real_point3d const *to, long now)
{
	long index, kind;

	for (index = 0; index < bots_globals.teleporter_count; index++)
	{
		if (bots_globals.teleporters[index].ladder || bots_distance3d(&bots_globals.teleporters[index].exit, to) > 2.0f ||
			bots_distance2d(&bots_globals.teleporters[index].entrance, from) > 1.5f ||
			fabs(bots_globals.teleporters[index].entrance.z - from->z) > 2.0f)
		{
			continue;
		}
		break;
	}
	if (index >= bots_globals.teleporter_count)
		return;
	if (bot->shortcut_phase == _bots_shortcut_going && bot->shortcut_index == index)
		kind = _bots_through_shortcut;
	else if (bot->via_teleporter && bot->via_index == index)
		kind = _bots_through_last_way;
	else
		kind = _bots_through_by_chance;
	bots_globals.teleporters[index].through[kind]++;
	bots_globals.shortcut_through[kind]++;
	if (kind == _bots_through_shortcut)
	{
		platform_log("bots: bot %ld through teleporter %ld after %.0f s, to (%.1f %.1f %.1f)", (long)(bot - bots_globals.bots) + 1,
			index + 1, (double)(now - bot->shortcut_tick) / TICKS_PER_SECOND, bot->goal_point.x, bot->goal_point.y,
			bot->goal_point.z);
		bot->shortcut_phase = _bots_shortcut_none;
		bot->shortcut_weighed_serial = bot->goal_serial;
	}
	else
		bots_shortcut_drop(bot, "another teleporter");
}

/* the search's way to its destination (or as near as it came): each
surface's entry point (path_state_build_path's raw steps, at most 64), the
endpoint last. TRUE with steps */
static boolean bots_path_raw_steps(struct path_state *state, struct path_step *raw_steps, short *raw_step_count,
	boolean *steps_finish_path, real_point3d *endpoint)
{
	short node_index, child_index = NONE;

	*steps_finish_path = TRUE;
	*raw_step_count = 0;
	if (!state->destination_valid || state->node_count <= 0)
		return FALSE;
	node_index = path_node_from_hash_table(state, state->destination.surface_index);
	*endpoint = state->destination.point;
	if (node_index == NONE)
	{
		node_index = state->closest_node_index;
		*endpoint = state->closest_point;
	}
	if (node_index < 0 || node_index >= state->node_count)
		return FALSE;
	*raw_step_count = (short)MIN(state->node_list[node_index].depth + 1, 64);
	while (node_index != NONE)
	{
		struct path_node *node;

		if (node_index < 0 || node_index >= state->node_count)
			return FALSE;
		node = &state->node_list[node_index];
		if (node->depth >= 64)
			*steps_finish_path = FALSE;
		else if (node->depth >= 0 && node->depth < *raw_step_count)
		{
			raw_steps[node->depth].surface_index = node->surface_index;
			raw_steps[node->depth].point = child_index == NONE ? *endpoint : state->node_list[child_index].entry_point;
		}
		else
			return FALSE;
		child_index = node_index;
		node_index = node->parent_node_index;
	}
	return *raw_step_count > 0;
}

/* the path as path_state_build_path makes it, the steps smoothed, but not
taken round the objects about (the obstacle avoidance found no way round:
the other bots crowding a narrow way out of a base, Longest's, all waiting
on each other); the bots push past each other, and one that stops moving
sidesteps and jumps. TRUE with a path */
static boolean bots_build_path_unavoided(struct path_state *state, struct path_result *path)
{
	struct path_step raw_steps[64];
	struct path_step smoothed_steps[MAXIMUM_SMOOTHED_PATH_STEPS];
	short raw_step_count, smoothed_step_count = 0;
	boolean steps_finish_path;
	real_point3d endpoint;

	csmemset(path, 0, sizeof(*path));
	if (!bots_path_raw_steps(state, raw_steps, &raw_step_count, &steps_finish_path, &endpoint))
		return FALSE;
	path_smooth(state, raw_step_count, raw_steps, &smoothed_step_count, smoothed_steps, &steps_finish_path);
	if (smoothed_step_count <= 0)
		return FALSE;
	smoothed_step_count = MIN(smoothed_step_count, MAXIMUM_SMOOTHED_PATH_STEPS);
	path->valid = TRUE;
	path->endpoint.point = endpoint;
	path->endpoint.surface_index = smoothed_steps[smoothed_step_count - 1].surface_index;
	path->steps_finish_path = steps_finish_path;
	path->step_count = (char)smoothed_step_count;
	path->step_index = 0;
	csmemcpy(path->steps, smoothed_steps, smoothed_step_count * sizeof(struct path_step));
	return TRUE;
}

/* a path that goes nowhere: short of its end, every step where the bot
stands. The smoothing (path_smooth) gave one at the foot of a steep bank:
all four of its steps the same point a few centimetres from the bot, which
it took as reached at once and searched again, the same, while it slid
about (Sidewinder's banks up to the side ledges, under Team Race's flags,
where the bots went round in circles: issue #40) */
static boolean bots_path_stalls(struct path_result const *path, real_point3d const *start)
{
	return path->valid && !path->steps_finish_path && path->step_count > 0 &&
		bots_distance2d(&path->steps[path->step_count - 1].point, start) <= BOTS_STEP_REACHED;
}

/* a stalling path's steps instead the search's own, unsmoothed: the next
surfaces' entry points beyond where the bot stands (up the bank), four at
most. TRUE when made */
static boolean bots_path_unsmoothed(struct path_state *state, real_point3d const *start, struct path_result *path)
{
	struct path_step raw_steps[64];
	short raw_step_count, step, count = 0;
	boolean steps_finish_path;
	real_point3d endpoint;
	struct path_result result;

	if (!bots_path_raw_steps(state, raw_steps, &raw_step_count, &steps_finish_path, &endpoint))
		return FALSE;
	csmemset(&result, 0, sizeof(result));
	for (step = 0; step < raw_step_count && count < (short)NUMBEROF(result.steps); step++)
	{
		if (!count && bots_distance2d(&raw_steps[step].point, start) <= BOTS_STEP_REACHED)
			continue;
		result.steps[count++] = raw_steps[step];
	}
	if (!count)
		return FALSE;
	result.valid = TRUE;
	result.steps_finish_path = steps_finish_path && step >= raw_step_count;
	result.step_count = (char)count;
	result.endpoint.point = endpoint;
	result.endpoint.surface_index = result.steps[count - 1].surface_index;
	*path = result;
	return TRUE;
}

/* a search's answer made a path: the way to the goal (or as near as the
search came), else through a teleporter when the goal is on ground the
search does not walk to; when the path's obstacle avoidance finds no way,
again with what the goal or the bot stands on (a flag's stand, a
teleporter's base) no obstacle, and failing that, the scenery and machines
about the bot are learnt to be in the way (the next searches go round them)
and this once the path is taken without the avoidance. TRUE with a path */
static boolean bots_search_build(struct bots_bot *bot, struct bots_search *search, boolean reached,
	struct path_result *path)
{
	struct path_state *state = &search->state_memory;
	long object_index;

	if (!search->found || state->node_count <= 0)
		return FALSE;
	/* (the search spent: every surface the bot walks to tried, the goal
	not among them) */
	if (!reached && state->heap_count <= 1 && bots_search_teleporter(bot, search, path))
		return TRUE;
	if (path_state_build_path(state, path) && path->step_count > 0)
		return TRUE;
	object_index = bots_object_at(&search->goal_point);
	if (object_index == NONE)
		object_index = bots_object_at(&search->start_point);
	if (object_index != NONE)
	{
		state->input.ignore_target_object_index = object_index;
		csmemset(path, 0, sizeof(*path));
		if (path_state_build_path(state, path) && path->step_count > 0)
			return TRUE;
	}
	/* (something new learnt: a new search, round it; port: not from a
	vehicle's search, whose width, not the scenery, finds no way where a
	biped walks) */
	if (search->radius <= BOTS_PATH_RADIUS && bots_paths_learn(&search->start_point, &search->goal_point) > 0)
		return FALSE;
	if (!bots_build_path_unavoided(state, path))
		return FALSE;
	bots_globals.search_unavoided++;
	return TRUE;
}

/* a search's answer taken (in the tick: the path's smoothing and the
objects it goes around are the tick's), into the bot's path */
static void bots_search_take(struct bots_bot *bot, boolean alive)
{
	struct bots_search *search;

	if (bot->search_slot == NONE)
		return;
	search = &bots_globals.search_slots[bot->search_slot];
	if (bots_search_state(search) != _bots_search_done)
		return;
	/* port: the walk from a teleporter's exit measured (no path of the
	bot's: bots_shortcut_measured) */
	if (alive && search->goal_serial == bot->goal_serial && search->purpose == _bots_search_for_exit)
		bots_shortcut_measured(bot, search);
	/* port: a path to a teleporter's entrance, the shortcut since dropped:
	none, a new search soon */
	else if (alive && search->goal_serial == bot->goal_serial && search->purpose == _bots_search_for_entrance &&
		bot->shortcut_phase != _bots_shortcut_going)
	{
		bot->path.valid = FALSE;
		bot->path_tick = game_time_get() - TICKS_PER_SECOND;
	}
	else if (alive && search->goal_serial == bot->goal_serial)
	{
		struct path_state *state = &search->state_memory;
		struct path_result path;
		unsigned long long started = bots_time_us();
		boolean reached = state->node_count > 0 &&
			path_node_from_hash_table(state, search->goal_surface_index) != NONE;
		boolean entrance = search->purpose == _bots_search_for_entrance;

		/* port: (to a teleporter's entrance, the shortcut: the walk there
		checked, or the shortcut dropped and a new search soon) */
		if (entrance && !bots_shortcut_entrance_answer(bot, search, reached))
		{
			bot->path.valid = FALSE;
			bot->path_tick = game_time_get() - TICKS_PER_SECOND;
			bot->search_slot = NONE;
			bots_search_set_state(search, _bots_search_idle);
			return;
		}
		if (state->node_count >= PATH_NODE_LIST_SIZE)
			bots_globals.search_overflows++;
		if (!reached)
		{
			bots_globals.search_partials++;
			if (getenv("HALO_BOT_PATH_LOG") && atoi(getenv("HALO_BOT_PATH_LOG")) >= 2)
				platform_log("bots: path short bot %ld goal %d from (%.1f %.1f %.1f) to (%.1f %.1f %.1f) s%ld: nodes %d "
					"heap %d closest %.1f at (%.1f %.1f %.1f)", (long)(bot - bots_globals.bots) + 1, (int)bot->goal_kind,
					search->start_point.x, search->start_point.y, search->start_point.z, search->goal_point.x,
					search->goal_point.y, search->goal_point.z, search->goal_surface_index, (int)state->node_count,
					(int)state->heap_count, state->closest_distance, state->closest_point.x, state->closest_point.y,
					state->closest_point.z);
		}
		csmemset(&path, 0, sizeof(path));
		bot->via_teleporter = FALSE;
		if (bots_search_build(bot, search, reached, &path) ||
			(entrance && bots_distance2d(&search->start_point, &search->goal_point) < 1.0f))
		{
			if (bots_path_stalls(&path, &search->start_point) &&
				bots_path_unsmoothed(&search->state_memory, &search->start_point, &path))
			{
				bots_globals.search_unsmoothed++;
			}
			/* port: (to a teleporter's entrance, the shortcut: onto it at
			the path's end, as onto the last way's; on it already, onto it) */
			if (entrance && reached)
			{
				bot->via_teleporter = TRUE;
				bot->via_point = search->goal_point;
				bot->via_tick = game_time_get();
				bot->via_index = bot->shortcut_index;
				bot->via_climb_tick = 0;
				bot->via_top_tick = 0;
			}
			bot->path = path;
			bot->path.step_index = 0;
			bot->path_failures = 0;
			/* (port: towards a teleporter's entrance it has yet to come to,
			no climbing on to the goal: the next search from nearer) */
			bot->path_short = !reached && !bot->via_teleporter && !entrance;
			if (!bot->path_short)
				bot->climb_until_tick = 0;
		}
		else
		{
			bot->path.valid = FALSE;
			bot->path_failures++;
			bots_globals.search_failures++;
			if (getenv("HALO_BOT_PATH_LOG"))
				bots_search_log_failure(bot, search, reached);
		}
		/* port: (a lane's waypoint or a vantage point it finds no way to:
		given up, a new search) */
		if (bots_tactics_search_answer(search->bot_index, &search->goal_point, reached, state->heap_count <= 1))
			bot->path.valid = FALSE;
		/* (an item it finds no way to: another, and not this one a while) */
		if (bot->goal_kind == _bots_goal_item && !reached && !bot->via_teleporter)
		{
			bots_item_set_unreachable(bot, bot->item_index);
			bot->goal_kind = _bots_goal_none;
			bot->path.valid = FALSE;
		}
		bot->path_tick = game_time_get();
		/* port: the teleporters weighed against the walk found (a path's
		search alone) */
		if (bot->path.valid)
			bots_shortcut_weigh(bot, search, reached);
		bots_globals.search_us += bots_time_us() - started;
	}
	bot->search_slot = NONE;
	bots_search_set_state(search, _bots_search_idle);
}

/* a search let go, its answer dropped (waiting for it if the helper is on
it: a search takes well under a millisecond) */
static void bots_search_drop(struct bots_search *search)
{
	for (;;)
	{
		pthread_mutex_lock(&bots_helper.lock);
		if (search->state != _bots_search_running)
		{
			search->state = _bots_search_idle;
			pthread_mutex_unlock(&bots_helper.lock);
			return;
		}
		pthread_mutex_unlock(&bots_helper.lock);
		sched_yield();
	}
}

/* ---------- perception */

/* whether the bot sees the point from its eyes (the AI's sight test) */
static boolean bots_line_of_sight(long unit_index, real_point3d const *eye, real_point3d const *point, long target_unit_index)
{
	struct collision_result collision;
	real_vector3d vector;
	unsigned long long started = bots_profile.enabled > 0 ? bots_time_us() : 0;
	boolean hit;
	long vehicle_index = bots_unit_vehicle(unit_index);

	vector.i = point->x - eye->x;
	vector.j = point->y - eye->y;
	vector.k = point->z - eye->z;
	/* port: riding, its own vehicle (a Warthog's gun before its gunner's
	eyes) and the riders in it are no wall: the ray ignores the vehicle, and
	with it everything riding it (object_test_vector) */
	hit = collision_test_vector(_collision_test_for_line_of_sight_flags, eye, &vector,
		vehicle_index != NONE ? vehicle_index : unit_index, &collision);
	if (started)
	{
		bots_profile.sight_rays++;
		bots_profile.sight_us += bots_time_us() - started;
	}
	if (!hit)
		return TRUE;
	/* (the target's vehicle: seen) */
	if (collision.type == _collision_result_object && target_unit_index != NONE &&
		(collision.object_index == target_unit_index ||
			collision.object_index == object_get(target_unit_index)->object.parent_object_index))
	{
		return TRUE;
	}
	return FALSE;
}

static boolean bots_is_enemy(struct player_datum *player, struct player_datum *other)
{
	if (player == other || other->unit_index == NONE)
		return FALSE;
	if (game_engine_has_teams() && player->team_index == other->team_index)
		return FALSE;
	return TRUE;
}

/* the enemy the bot sees best now, NONE for none: of the enemies in its
sight's reach and view, the one of the least score (nearest, most ahead, the
one it fights favoured) a line of sight reaches, the first in the players'
order of those of that score. The enemies are scored first and their lines
of sight cast from the least score up, until one reaches its enemy, which is
the answer: cast in the players' order, as before, every enemy scoring less
than the best seen so far had its line cast, and the best was the same one
(the lines of sight change nothing in the game but the collision tests'
marker stamps). A score that is not a number (none is: the players'
positions are) is compared as before, in the players' order */
struct bots_target_candidate
{
	long player_index;
	long unit_index;
	real score;
	real_point3d point;
};

/* an enemy's score as a target (its point the bounding sphere's centre);
FALSE for one out of reach or view, or not an enemy */
static boolean bots_target_score(struct bots_bot *bot, struct player_datum *player, struct player_datum *other,
	long player_index, real_point3d const *eye, struct bots_skill_definition const *skill, boolean hurt,
	real sight_scale, struct bots_target_candidate *candidate)
{
	struct object_datum *object;
	struct unit_datum *other_unit;
	real distance, reach, bearing;
	real half_view = 0.5f * skill->field_of_view_degrees * BOTS_DEGREES;

	if (!bots_is_enemy(player, other))
		return FALSE;
	object = object_get(other->unit_index);
	other_unit = unit_get(other->unit_index);
	if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return FALSE;
	candidate->point = object->object.bounding_sphere_center;
	distance = bots_distance3d(eye, &candidate->point);
	/* (port: further through a scope at a vantage point: bots_tactics.h) */
	reach = skill->sight_distance * sight_scale;
	/* (active camouflage: seen only close) */
	if (TEST_FLAG(other_unit->unit.flags, _unit_active_camouflaged_bit))
		reach *= 0.3f;
	/* (the one it fights: a little further) */
	if (player_index == bot->target_player_index)
		reach *= 1.25f;
	if (distance > reach)
		return FALSE;
	bearing = (real)fabs(bots_angle_difference((real)atan2(candidate->point.y - eye->y, candidate->point.x - eye->x),
		bot->yaw));
	/* (out of view: unless close by, it shot the bot, or it is the one
	the bot fights) */
	if (bearing > half_view && distance > 3.0f && !hurt && player_index != bot->target_player_index)
		return FALSE;
	/* (nor far above or below where it looks, on the same terms: a player
	running by looks ahead, not up) */
	if (distance > 3.0f && !hurt && player_index != bot->target_player_index)
	{
		real dx = candidate->point.x - eye->x, dy = candidate->point.y - eye->y;
		real elevation = (real)atan2(candidate->point.z - eye->z, (real)sqrt(dx * dx + dy * dy));

		if (!bots_combat_in_vertical_view(bots_globals.skill, elevation / BOTS_DEGREES, bot->pitch / BOTS_DEGREES))
		{
			bots_globals.combat_counts[_bots_combat_count_out_of_view]++;
			return FALSE;
		}
	}
	candidate->player_index = player_index;
	candidate->unit_index = other->unit_index;
	candidate->score = distance + bearing * 4.0f - (player_index == bot->target_player_index ? 6.0f : 0.0f);
	/* (capture the flag: who carries its team's flag, before anyone) */
	if (bots_ctf_active() && (player->team_index == 0 || player->team_index == 1))
	{
		bots_ctf_update();
		if (bots_globals.ctf.present[player->team_index] &&
			bots_globals.ctf.carrier_player_index[player->team_index] == player_index)
		{
			candidate->score -= 30.0f;
		}
	}
	/* (king of the hill: who stands on the hill, before anyone) */
	if (bots_king_active() && game_engine_king_point_on_hill(&object->object.bounding_sphere_center))
		candidate->score -= 20.0f;
	return TRUE;
}

static long bots_find_target(struct bots_bot *bot, struct player_datum *player, struct unit_datum *unit,
	real_point3d const *eye)
{
	struct bots_skill_definition const *skill = &bots_skills[bots_globals.skill];
	struct data_iterator iterator;
	struct player_datum *other;
	boolean hurt = game_time_get() - bot->hurt_tick < TICKS_PER_SECOND * 2;
	real sight_scale = bots_tactics_sight_scale((long)(bot - bots_globals.bots));
	struct bots_target_candidate candidates[BOTS_MAXIMUM + MAXIMUM_LOCAL_PLAYERS];
	long candidate_count = 0, index;
	boolean ordered = TRUE;

	(void)unit;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL && ordered)
	{
		if (candidate_count >= (long)NUMBEROF(candidates))
			ordered = FALSE;
		else if (bots_target_score(bot, player, other, iterator.datum_index, eye, skill, hurt, sight_scale, &candidates[candidate_count]))
		{
			real score = candidates[candidate_count].score;

			if (score != score)
				ordered = FALSE;
			else if (score < REAL_MAX)
				candidate_count++;
		}
	}
	if (!ordered)
	{
		/* (more enemies than kept, or a score not a number: the players'
		order, each scoring less than the best so far) */
		struct bots_target_candidate candidate;
		long best_index = NONE;
		real best_score = REAL_MAX;

		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (!bots_target_score(bot, player, other, iterator.datum_index, eye, skill, hurt, sight_scale, &candidate) ||
				candidate.score >= best_score ||
				!bots_line_of_sight(player->unit_index, eye, &candidate.point, candidate.unit_index))
			{
				continue;
			}
			best_score = candidate.score;
			best_index = candidate.player_index;
		}
		return best_index;
	}
	while (candidate_count > 0)
	{
		long least = 0;

		/* (the least score, the first of it in the players' order) */
		for (index = 1; index < candidate_count; index++)
		{
			if (candidates[index].score < candidates[least].score)
				least = index;
		}
		if (bots_line_of_sight(player->unit_index, eye, &candidates[least].point, candidates[least].unit_index))
			return candidates[least].player_index;
		/* (out, the others kept in their order) */
		candidate_count--;
		for (index = least; index < candidate_count; index++)
			candidates[index] = candidates[index + 1];
	}
	return NONE;
}

/* ---------- moving */

static void bots_set_goal(struct bots_bot *bot, short kind, real_point3d const *point, long surface_index)
{
	/* (the same goal, near enough: the path stands) */
	if (bot->goal_kind == kind && bot->goal_surface_index == surface_index &&
		bots_distance3d(&bot->goal_point, point) < 1.5f)
	{
		return;
	}
	bot->goal_kind = kind;
	bot->goal_point = *point;
	bot->goal_surface_index = surface_index;
	bot->goal_tick = game_time_get();
	bot->item_powerup = _bots_powerup_none;
	bot->goal_serial++;
	bot->path.valid = FALSE;
	bot->path_failures = 0;
	bot->climb_until_tick = 0;
}

/* a roaming place picked at random (index), or a higher one (VETERAN and
up): two more places at random, not within 6 units, and the one of the most
credit for its height above the bot */
static short bots_roam_higher(struct bots_bot *bot, real_point3d const *position, short index)
{
	short best = index, tries;
	real best_credit = bots_combat_high_ground(bots_globals.skill, bots_globals.roam_points[index].z - position->z);

	if (bots_globals.skill < _bots_skill_veteran)
		return index;
	for (tries = 0; tries < 2; tries++)
	{
		short other = (short)(bots_random(bot) % (unsigned long)bots_globals.roam_point_count);
		real credit;

		if (bots_distance2d(&bots_globals.roam_points[other], position) < 6.0f)
			continue;
		credit = bots_combat_high_ground(bots_globals.skill, bots_globals.roam_points[other].z - position->z);
		if (credit > best_credit)
		{
			best_credit = credit;
			best = other;
		}
	}
	if (best != index)
		bots_globals.combat_counts[_bots_combat_count_high_ground]++;
	return best;
}

/* the next place to roam to: most often towards an enemy (the roaming place
nearest a living enemy, as a player knows where the other team's base is
and reads the motion tracker; by level: bots_combat_hunt_chance), else
anywhere (a higher place from veteran) */
static void bots_pick_roam_goal(struct bots_bot *bot, real_point3d const *position)
{
	long tries;

	if (bots_globals.roam_point_count <= 0)
		return;
	bot->hunting = FALSE;
	if (bots_random_real(bot) < bots_combat_hunt_chance(bots_globals.skill))
	{
		struct player_datum *player = player_get(bot->player_index);
		struct data_iterator iterator;
		struct player_datum *other;
		long enemies[BOTS_MAXIMUM + MAXIMUM_LOCAL_PLAYERS];
		long enemy_count = 0;

		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
			enemy_count < (long)NUMBEROF(enemies))
		{
			if (bots_is_enemy(player, other))
				enemies[enemy_count++] = other->unit_index;
		}
		if (enemy_count > 0)
		{
			struct object_datum *enemy = object_get(enemies[bots_random(bot) % (unsigned long)enemy_count]);
			long index, nearest = NONE;
			real nearest_distance = REAL_MAX;

			for (index = 0; index < bots_globals.roam_point_count; index++)
			{
				real distance = bots_distance2d(&bots_globals.roam_points[index], &enemy->object.position) +
					bots_random_real(bot) * 6.0f;

				/* (a place above the enemy counts as nearer, from veteran) */
				distance -= bots_combat_high_ground(bots_globals.skill,
					bots_globals.roam_points[index].z - enemy->object.position.z);

				if (distance < nearest_distance && bots_distance2d(&bots_globals.roam_points[index], position) > 4.0f)
				{
					nearest_distance = distance;
					nearest = index;
				}
			}
			if (nearest != NONE)
			{
				bot->roam_index = (short)nearest;
				bot->hunting = TRUE;
				bots_set_goal(bot, _bots_goal_roam, &bots_globals.roam_points[nearest], bots_globals.roam_surfaces[nearest]);
				return;
			}
		}
	}
	for (tries = 0; tries < 6; tries++)
	{
		short index = (short)(bots_random(bot) % (unsigned long)bots_globals.roam_point_count);

		/* (not where it is) */
		if (bots_distance2d(&bots_globals.roam_points[index], position) < 6.0f && tries < 5)
			continue;
		index = bots_roam_higher(bot, position, index);
		bot->roam_index = index;
		bots_set_goal(bot, _bots_goal_roam, &bots_globals.roam_points[index], bots_globals.roam_surfaces[index]);
		return;
	}
}

/* CTF: where a team's flag is at home, which is also where that team
captures the other's flag: its own stand (the scenario's netgame flag of its
team), but in an assault game (Invasion: ctf.assault) the other team's
stand - the flags change bases (ctf_engine_initialize_for_new_map's flag
slots), so a team takes the flag lying at its own base and runs it to the
other's (the bots took it and stood on the stand they took it from, issue
#40). FALSE for none */
static boolean bots_ctf_stand(long team_index, real_point3d *position)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	if (game_engine_get_variant()->game_engine_index == game_engine_ctf &&
		game_engine_get_variant()->game_engine_variant.ctf.assault)
	{
		team_index = team_index ? 0 : 1;
	}
	for (index = 0; scenario && index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

		if (flag->type == _netgame_flag_ctf_flag && flag->team_index == team_index)
		{
			*position = flag->position;
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- capture the flag

Quake III Arena's bots' (ioquake3's code/game/ai_dmq3.c, BotCTFSeekGoals;
ai_team.c, the team leader's orders), taken as their rules: each team's
bots are its defenders (the first, about two in five of the team, at least
one of a team of two or more) and its attackers, and where each goes is by
the two flags' state, looked at as the tick begins:

- carrying the other team's flag: home, to its own flag's place, fighting
  only what stands in the way (bots_bot_update);
- its own flag carried: the defenders, and any bot near, after the carrier
  (whom they fight before anyone: bots_target_score);
- its own flag dropped: the defenders, and any bot near, to it, which
  touched goes home (unless the gametype's flag must reset: then they
  guard it);
- the other team's flag: an attacker to it where it is (at home, or
  dropped), or, a team mate carrying it, with the carrier;
- a defender otherwise about its own flag, a place near it at a time.

One flag's assault: the team with no flag of its own all attack, the other
all defend. */

static boolean bots_ctf_active(void)
{
	return game_engine_running() && game_engine_get_variant()->game_engine_index == game_engine_ctf;
}

/* the flags' state, once a tick */
static void bots_ctf_update(void)
{
	long now = game_time_get();
	long team;

	if (bots_globals.ctf.tick == now)
		return;
	bots_globals.ctf.tick = now;
	bots_globals.ctf.touch_returns = game_engine_ctf_touch_returns();
	bots_globals.ctf.home_to_score = game_engine_ctf_home_to_score();
	for (team = 0; team < 2; team++)
	{
		bots_globals.ctf.present[team] = game_engine_ctf_flag_state(team, &bots_globals.ctf.position[team],
			&bots_globals.ctf.carrier_player_index[team], &bots_globals.ctf.at_home[team], &bots_globals.ctf.home[team]);
	}
}

/* the flag the bot carries (its team's other), or FALSE */
static boolean bots_ctf_carrying(struct bots_bot *bot)
{
	long team;

	if (!bots_ctf_active())
		return FALSE;
	bots_ctf_update();
	for (team = 0; team < 2; team++)
	{
		if (bots_globals.ctf.present[team] && bots_globals.ctf.carrier_player_index[team] == bot->player_index)
			return TRUE;
	}
	return FALSE;
}

/* whether the bot defends: its team's first bots, as many as its share */
static boolean bots_ctf_defender(struct bots_bot *bot, struct player_datum *player)
{
	long team = player->team_index;
	long own_bot_index = (long)(bot - bots_globals.bots);
	struct data_iterator iterator;
	struct player_datum *other;
	long team_size = 0;
	long rank = 0;
	long defenders;

	/* (one flag's assault: the team without one attacks it, the other defends) */
	if (!bots_globals.ctf.present[team])
		return FALSE;
	if (!bots_globals.ctf.present[1 - team])
		return TRUE;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (other->team_index != team)
			continue;
		team_size++;
		if (bots_machine_is_bot(other->network_player_data.machine_index) &&
			other->network_player_data.machine_index - BOTS_FIRST_MACHINE < own_bot_index)
		{
			rank++;
		}
	}
	defenders = team_size >= 2 ? MAX(1, team_size * 2 / 5) : 0;
	return rank < defenders;
}

/* a goal of the game's: the ground under point (a carrier in the air, the
ground below it), a moving one taken again only once it has moved */
static boolean bots_ctf_set_goal(struct bots_bot *bot, real_point3d const *point, real moved)
{
	real_point3d ground;
	long surface_index;

	if (bot->goal_kind == _bots_goal_objective && bots_distance3d(&bot->goal_point, point) < moved)
		return TRUE;
	surface_index = bots_surface_below(point, &ground);
	if (surface_index == NONE)
		return FALSE;
	bots_set_goal(bot, _bots_goal_objective, &ground, surface_index);
	return TRUE;
}

/* the flags' goals that come before anything else (carrying theirs, ours
carried off or dropped: Quake III's urgent ones), a vantage point too
(bots_choose_goal); FALSE for none */
static boolean bots_ctf_urgent_goal(struct bots_bot *bot, struct player_datum *player, real_point3d const *position)
{
	long team = player->team_index;
	long other = 1 - team;
	boolean defender;

	if (team != 0 && team != 1)
		return FALSE;
	bots_ctf_update();
	defender = bots_ctf_defender(bot, player);
	/* carrying theirs: home */
	if (bots_globals.ctf.present[other] && bots_globals.ctf.carrier_player_index[other] == bot->player_index)
		return bots_ctf_set_goal(bot, &bots_globals.ctf.home[team], 1.0f);
	/* (a gametype whose captures need the team's own flag at home, ours
	carried off while a team mate carries theirs: everyone after our flag,
	or the carrier waits at home for ever, Quake III's flag standoff) */
	if (bots_globals.ctf.home_to_score && bots_globals.ctf.present[team] && bots_globals.ctf.present[other] &&
		bots_globals.ctf.carrier_player_index[team] != NONE && bots_globals.ctf.carrier_player_index[other] != NONE)
	{
		return bots_ctf_set_goal(bot, &bots_globals.ctf.position[team], 4.0f);
	}
	if (bots_globals.ctf.present[team] && !bots_globals.ctf.at_home[team])
	{
		real distance = bots_distance3d(position, &bots_globals.ctf.position[team]);

		/* its own carried off: after the carrier */
		if (bots_globals.ctf.carrier_player_index[team] != NONE && (defender || distance < 25.0f))
			return bots_ctf_set_goal(bot, &bots_globals.ctf.position[team], 4.0f);
		/* dropped: to it (to touch it home, or to guard it) */
		if (bots_globals.ctf.carrier_player_index[team] == NONE && (defender || distance < 30.0f))
			return bots_ctf_set_goal(bot, &bots_globals.ctf.position[team], 1.5f);
	}
	return FALSE;
}

/* where the bot goes for its team in capture the flag; FALSE to let it
roam (bots_choose_goal) */
static boolean bots_ctf_choose_goal(struct bots_bot *bot, struct player_datum *player, real_point3d const *position)
{
	long team = player->team_index;
	long other = 1 - team;
	long now = game_time_get();
	boolean defender;

	if (team != 0 && team != 1)
		return FALSE;
	if (bots_ctf_urgent_goal(bot, player, position))
		return TRUE;
	defender = bots_ctf_defender(bot, player);
	if (!defender && bots_globals.ctf.present[other])
	{
		long carrier = bots_globals.ctf.carrier_player_index[other];

		/* a team mate has it: with the carrier */
		if (carrier != NONE)
			return bots_ctf_set_goal(bot, &bots_globals.ctf.position[other], 4.0f);
		/* at home or dropped: to it */
		return bots_ctf_set_goal(bot, &bots_globals.ctf.position[other], 1.5f);
	}
	if (defender && bots_globals.ctf.present[team])
	{
		/* about its flag: a roaming place near it at a time, or the flag's place */
		if (now >= bot->guard_until_tick || bots_distance2d(&bot->guard_point, &bots_globals.ctf.home[team]) > 16.0f)
		{
			long index, tries;

			bot->guard_point = bots_globals.ctf.home[team];
			bot->guard_surface_index = NONE;
			for (tries = 0; tries < 8 && bots_globals.roam_point_count > 0; tries++)
			{
				index = (long)(bots_random(bot) % (unsigned long)bots_globals.roam_point_count);
				if (bots_distance2d(&bots_globals.roam_points[index], &bots_globals.ctf.home[team]) < 14.0f)
				{
					bot->guard_point = bots_globals.roam_points[index];
					break;
				}
			}
			bot->guard_until_tick = now + TICKS_PER_SECOND * (6 + (long)(bots_random(bot) % 6));
		}
		return bots_ctf_set_goal(bot, &bot->guard_point, 1.5f);
	}
	return FALSE;
}

/* whether the bot defends by Quake III's rules: capture the flag's defender
of its flag (bots_ctf_defender), king of the hill's guard of the hill's
approaches (bots_king_guard); bots_roles.c's defenders are these */
boolean bots_quake_defender(long bot_index)
{
	struct bots_bot *bot;
	struct player_datum *player;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM)
		return FALSE;
	bot = &bots_globals.bots[bot_index];
	if (bot->player_index == NONE || (player = player_try_and_get(bot->player_index)) == NULL)
		return FALSE;
	if (bots_ctf_active())
	{
		if (player->team_index != 0 && player->team_index != 1)
			return FALSE;
		bots_ctf_update();
		return bots_ctf_defender(bot, player);
	}
	if (bots_king_active())
		return bots_king_guard(bot, player);
	return FALSE;
}

/* ---------- king of the hill

Quake III Arena's bots have no king of the hill; its rules here are their
key area's defence (LTG_DEFENDKEYAREA, ai_dmq3.c) on the hill the game
draws (game_engine_king_hill), which scores each team (each player, free
for all) with anyone on it every tick, contested or not:

- a bot goes to a place on the hill (a random one inside its outline, so
  that they spread over it), and on it, from place to place inside it every
  few seconds, fighting from there: its strafing turns back at the hill's
  edge (bots_bot_fight), and it chases no one off it;
- whoever stands on the hill is fought before anyone (bots_target_score);
- the hill moved: to the new one at once;
- in a team game of five or more, the team's first bot (and its second of
  eight or more) guards the hill's approaches instead: a roaming place 8 to
  16 units from the hill at a time;
- a better weapon is fetched on the way, not while on the hill. */

static boolean bots_king_active(void)
{
	return game_engine_running() && game_engine_get_variant()->game_engine_index == game_engine_king;
}

/* the hill, once a tick */
static void bots_king_update(void)
{
	long now = game_time_get();

	if (bots_globals.king.tick == now)
		return;
	bots_globals.king.tick = now;
	bots_globals.king.present = game_engine_king_hill(bots_globals.king.points,
		(long)NUMBEROF(bots_globals.king.points), &bots_globals.king.point_count, &bots_globals.king.bottom,
		&bots_globals.king.top, &bots_globals.king.center, &bots_globals.king.hill_id);
}

/* whether the bot's unit (its middle, as the game counts it) is on the hill */
static boolean bots_king_on_hill(long unit_index)
{
	bots_king_update();
	return bots_globals.king.present && unit_index != NONE &&
		game_engine_king_point_on_hill(&object_get(unit_index)->object.bounding_sphere_center);
}

/* whether the bot guards the hill's approaches: a team's first bot of five
or more, and its second of eight or more */
static boolean bots_king_guard(struct bots_bot *bot, struct player_datum *player)
{
	long own_bot_index = (long)(bot - bots_globals.bots);
	struct data_iterator iterator;
	struct player_datum *other;
	long team_size = 0;
	long rank = 0;

	if (!game_engine_has_teams())
		return FALSE;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (other->team_index != player->team_index)
			continue;
		team_size++;
		if (bots_machine_is_bot(other->network_player_data.machine_index) &&
			other->network_player_data.machine_index - BOTS_FIRST_MACHINE < own_bot_index)
		{
			rank++;
		}
	}
	return rank < (team_size >= 8 ? 2 : team_size >= 5 ? 1 : 0);
}

/* a walkable place inside the hill: between its middle and one of its
points, the ground under it */
static boolean bots_king_place(struct bots_bot *bot, real_point3d *place)
{
	long tries;

	for (tries = 0; tries < 6; tries++)
	{
		real_point3d const *corner = &bots_globals.king.points[bots_random(bot) %
			(unsigned long)bots_globals.king.point_count];
		real along = bots_random_real(bot) * 0.75f;
		real_point3d probe;

		probe.x = bots_globals.king.center.x + (corner->x - bots_globals.king.center.x) * along;
		probe.y = bots_globals.king.center.y + (corner->y - bots_globals.king.center.y) * along;
		probe.z = bots_globals.king.center.z + (corner->z - bots_globals.king.center.z) * along + 0.5f;
		if (bots_surface_below(&probe, place) != NONE)
		{
			/* (a place the game counts: a player's middle half a unit up) */
			real_point3d middle = *place;

			middle.z += 0.5f;
			if (game_engine_king_point_on_hill(&middle))
				return TRUE;
		}
	}
	return FALSE;
}

/* where the bot goes in king of the hill; FALSE to let it roam */
static boolean bots_king_choose_goal(struct bots_bot *bot, struct player_datum *player, real_point3d const *position)
{
	long now = game_time_get();
	real_point3d ground;
	long surface_index;

	bots_king_update();
	if (!bots_globals.king.present || bots_globals.king.point_count <= 0)
		return FALSE;
	if (bots_king_guard(bot, player))
	{
		if (now >= bot->guard_until_tick || bots_distance2d(&bot->guard_point, &bots_globals.king.center) > 18.0f)
		{
			long index, tries;

			bot->guard_point = bots_globals.king.center;
			for (tries = 0; tries < 10 && bots_globals.roam_point_count > 0; tries++)
			{
				real distance;

				index = (long)(bots_random(bot) % (unsigned long)bots_globals.roam_point_count);
				distance = bots_distance2d(&bots_globals.roam_points[index], &bots_globals.king.center);
				if (distance >= 8.0f && distance <= 16.0f)
				{
					bot->guard_point = bots_globals.roam_points[index];
					break;
				}
			}
			bot->guard_until_tick = now + TICKS_PER_SECOND * (8 + (long)(bots_random(bot) % 7));
		}
		surface_index = bots_surface_below(&bot->guard_point, &ground);
		if (surface_index == NONE)
			return FALSE;
		bots_set_goal(bot, _bots_goal_objective, &ground, surface_index);
		return TRUE;
	}
	/* (a new place: the hill moved, this one reached, or its time up: a few
	seconds on the hill, longer on the way) */
	if (bot->hill_id != bots_globals.king.hill_id || now >= bot->hill_until_tick ||
		bot->goal_kind != _bots_goal_objective || bots_distance2d(position, &bot->hill_point) < 1.0f)
	{
		boolean on_hill = bots_king_on_hill(player->unit_index);

		if (!bots_king_place(bot, &bot->hill_point))
			bot->hill_point = bots_globals.king.center;
		bot->hill_id = bots_globals.king.hill_id;
		bot->hill_until_tick = now + TICKS_PER_SECOND * (on_hill ? 2 + (long)(bots_random(bot) % 3) : 12);
	}
	surface_index = bots_surface_below(&bot->hill_point, &ground);
	if (surface_index == NONE)
		return FALSE;
	bots_set_goal(bot, _bots_goal_objective, &ground, surface_index);
	return TRUE;
}

/* ---------- OpenCE's modes of Slayer

Infection (infection.c): the zombies, blue, carry the skull and fight with
it close up; the humans, red, keep their weapons. A zombie bot goes after
the nearest human where they are (the zombies hunt), runs straight at them
and strikes, and fetches nothing; a human bot stays with the other humans
(the roaming place nearest the living humans' middle, or, alone, the one
furthest from the zombies), backs away from a zombie close by, and goes
after no one it last saw. SWAT (game_modes.c): a shot to the head kills, so
the bots aim at the head (bots_bot_fight). */

static boolean bots_zombie(struct player_datum *player)
{
	return infection_enabled() && player->unit_index != NONE && infection_unit_is_zombie(player->unit_index);
}

/* where an Infection bot goes; FALSE to let it roam */
static boolean bots_infection_choose_goal(struct bots_bot *bot, struct player_datum *player, real_point3d const *position)
{
	struct data_iterator iterator;
	struct player_datum *other;
	long now = game_time_get();
	real_point3d humans = { 0.0f, 0.0f, 0.0f };
	long human_count = 0;
	long nearest_human = NONE;
	real nearest_distance = REAL_MAX;
	boolean zombie = bots_zombie(player);

	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		real_point3d const *where;
		real distance;

		if (other->unit_index == NONE || other == player || infection_unit_is_zombie(other->unit_index) ||
			TEST_FLAG(object_get(other->unit_index)->object.damage_flags, _object_dead_bit))
		{
			continue;
		}
		where = &object_get(other->unit_index)->object.position;
		humans.x += where->x;
		humans.y += where->y;
		humans.z += where->z;
		human_count++;
		distance = bots_distance3d(position, where);
		if (distance < nearest_distance)
		{
			nearest_distance = distance;
			nearest_human = other->unit_index;
		}
	}
	if (zombie)
	{
		real_point3d ground;
		long surface_index;
		real_point3d const *where;

		if (nearest_human == NONE)
			return FALSE;
		where = &object_get(nearest_human)->object.position;
		if (bot->goal_kind == _bots_goal_objective && bots_distance3d(&bot->goal_point, where) < 3.0f)
			return TRUE;
		surface_index = bots_surface_below(where, &ground);
		if (surface_index == NONE)
			return bot->goal_kind == _bots_goal_objective;
		bots_set_goal(bot, _bots_goal_objective, &ground, surface_index);
		return TRUE;
	}
	/* a human: with the others (their middle), or alone, away from the zombies */
	if (bot->goal_kind == _bots_goal_objective && now < bot->guard_until_tick)
		return TRUE;
	{
		long index, best = NONE;
		real best_score = REAL_MAX;

		if (human_count > 0)
		{
			humans.x /= (real)human_count;
			humans.y /= (real)human_count;
			humans.z /= (real)human_count;
		}
		for (index = 0; index < bots_globals.roam_point_count; index++)
		{
			real score;

			if (human_count > 0)
				score = bots_distance3d(&bots_globals.roam_points[index], &humans);
			else
			{
				real zombie_distance = REAL_MAX;

				data_iterator_new(&iterator, player_data);
				while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
				{
					if (other->unit_index != NONE && infection_unit_is_zombie(other->unit_index))
					{
						zombie_distance = MIN(zombie_distance,
							bots_distance3d(&bots_globals.roam_points[index], &object_get(other->unit_index)->object.position));
					}
				}
				score = -zombie_distance;
			}
			score += bots_random_real(bot) * 4.0f;
			if (score < best_score)
			{
				best_score = score;
				best = index;
			}
		}
		if (best == NONE)
			return FALSE;
		bots_set_goal(bot, _bots_goal_objective, &bots_globals.roam_points[best], bots_globals.roam_surfaces[best]);
		bot->guard_until_tick = now + TICKS_PER_SECOND * (8 + (long)(bots_random(bot) % 6));
		return TRUE;
	}
}

/* port: vehicles' seats and the bots going for them */

/* the bot (other than `except`) going for the vehicle's seat (NONE: to right
it), NULL for none */
static struct bots_bot *bots_seat_claimer(long vehicle_index, short seat_index, struct bots_bot const *except)
{
	long index;

	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *other = &bots_globals.bots[index];

		if (other != except && other->player_index != NONE && other->unit_index != NONE &&
			other->vehicle_index == vehicle_index && other->vehicle_seat_index == seat_index)
		{
			return other;
		}
	}
	return NULL;
}

/* a teammate of the player drives the vehicle, or a teammate bot goes for
its driver's seat (*bot_driver: a bot, which waits for those getting in) */
static boolean bots_vehicle_friendly_driver(long vehicle_index, struct player_datum *player, boolean *bot_driver)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	long player_index = NONE;

	*bot_driver = FALSE;
	if (!game_engine_has_teams())
		return FALSE;
	if (vehicle->unit.driver_object_index != NONE)
		player_index = unit_get(vehicle->unit.driver_object_index)->unit.player_index;
	else
	{
		short seat_index;
		struct unit_definition *definition = unit_definition_get(vehicle->definition_index);

		for (seat_index = 0; seat_index < definition->unit.seats.count && player_index == NONE; seat_index++)
		{
			struct bots_bot *claimer;

			if (bots_seat_kind(vehicle_index, seat_index) == _bots_seat_driver &&
				(claimer = bots_seat_claimer(vehicle_index, seat_index, NULL)) != NULL)
			{
				player_index = claimer->player_index;
			}
		}
	}
	if (player_index == NONE || !player_try_and_get(player_index) || player_get(player_index) == player ||
		player_get(player_index)->team_index != player->team_index)
	{
		return FALSE;
	}
	*bot_driver = bots_player_is_bot(player_index);
	return TRUE;
}

/* port: a vehicle the bot went for let go (its seat free for another bot) */
static void bots_vehicle_forget(struct bots_bot *bot)
{
	bot->vehicle_index = NONE;
	bot->vehicle_seat_index = NONE;
	bot->vehicle_flip = FALSE;
	bot->vehicle_turret = FALSE;
	bot->vehicle_presses = 0;
	if (bot->goal_kind == _bots_goal_vehicle)
		bot->goal_kind = _bots_goal_none;
}

/* port: a vehicle the bot gave up on, not gone for again a while */
static void bots_vehicle_avoid(struct bots_bot *bot, long vehicle_index, long ticks)
{
	bot->vehicle_avoid_index = vehicle_index;
	bot->vehicle_avoid_until_tick = game_time_get() + ticks;
}

/* port: going for a vehicle: the goal its seat's entrance (worked out again
once the vehicle has moved), or the vehicle itself to right it; FALSE when
the vehicle is gone, dead or righted (the bot goes for it no more) */
static boolean bots_vehicle_goal(struct bots_bot *bot, long unit_index)
{
	struct object_datum *vehicle = object_try_and_get_and_verify_type(bot->vehicle_index, _object_mask_vehicle);
	real_point3d point, ground, seat, hint;
	long surface_index;
	boolean driver_is_bot;

	if (!vehicle || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
	{
		bots_vehicle_forget(bot);
		return FALSE;
	}
	if (bot->vehicle_flip && vehicle->object.up.k > 0.5f)
	{
		/* (righted: a seat of it looked for at once) */
		bots_globals.vehicle_flips++;
		bots_vehicle_forget(bot);
		bot->vehicle_check_tick = 0;
		return FALSE;
	}
	/* (its seat taken meanwhile, by a player say, or a Warthog's driver gone
	from beside the seat it went for: another looked for at once) */
	if (!bot->vehicle_flip && (bots_seat_occupant(bot->vehicle_index, bot->vehicle_seat_index) != NONE ||
		(bots_seat_kind(bot->vehicle_index, bot->vehicle_seat_index) != _bots_seat_driver && !bot->vehicle_turret &&
			!bots_vehicle_friendly_driver(bot->vehicle_index, player_get(bot->player_index), &driver_is_bot))))
	{
		bots_vehicle_forget(bot);
		bot->vehicle_check_tick = 0;
		return FALSE;
	}
	if (bot->goal_kind == _bots_goal_vehicle && bots_distance3d(&vehicle->object.position, &bot->vehicle_entrance_from) < 0.5f)
		return TRUE;
	if (bot->vehicle_flip)
		bot->vehicle_entrance = vehicle->object.position;
	else if (!unit_get_seat_entrance_point(unit_index, bot->vehicle_index, bot->vehicle_seat_index, &bot->vehicle_entrance,
		&seat, &hint))
	{
		bots_vehicle_forget(bot);
		return FALSE;
	}
	bot->vehicle_entrance_from = vehicle->object.position;
	point = bot->vehicle_entrance;
	surface_index = bots_surface_below(&point, &ground);
	if (surface_index == NONE)
	{
		bots_vehicle_avoid(bot, bot->vehicle_index, TICKS_PER_SECOND * 20);
		bots_vehicle_forget(bot);
		return FALSE;
	}
	bots_set_goal(bot, _bots_goal_vehicle, &ground, surface_index);
	return TRUE;
}

/* port: a powerup to go for - a health pack when the bot's body is hurt
(below its skill's mark: lower in a fight, where only the less hurt of the
skilled leave it for one near; never for an easy bot in a fight), the most
hurt going furthest; an overshield or camouflage near (nearer in a fight,
and in a game with an objective) - the best of those lying on the map
(bots_powerups), on ground below it, that no other bot nearer to it goes
for, nor one it found no way to. The one it goes for kept while it lies
there, the bot still wants it (a body not whole for a health pack) and its
time is not up (the walk there and a little); one it stood at two seconds
and did not get (its body whole by then, a pack another took first) left
be a while, as one it finds no way to (bots_search_take). TRUE while it
goes for one */
static boolean bots_powerup_goal(struct bots_bot *bot, struct unit_datum *unit, real_point3d const *position, boolean fighting)
{
	struct bots_skill_definition const *skill = &bots_skills[bots_globals.skill];
	long now = game_time_get();
	real body = unit->object.body_vitality;
	real heal_below, best_score = REAL_MAX;
	boolean objective_game, holding;
	long index, best = NONE;
	real_point3d ground;
	long surface_index;

	/* (the goal changed under it - a path's end short of it, three times
	stuck, a teleporter's way that went nowhere - the powerup left be a
	while, as one it found no way to: it was gone for every second before) */
	if (bot->powerup_index != NONE &&
		(bot->goal_kind != _bots_goal_item || bot->item_index != bot->powerup_index || bot->item_powerup == _bots_powerup_none))
	{
		struct object_datum *item = object_try_and_get_and_verify_type(bot->powerup_index, _object_mask_equipment);

		if (item && item->object.parent_object_index == NONE && bots_distance3d(&item->object.position, position) > 1.5f)
			bots_item_set_unreachable(bot, bot->powerup_index);
		bot->powerup_index = NONE;
	}
	if (bot->goal_kind == _bots_goal_item && bot->item_powerup != _bots_powerup_none)
	{
		struct item_datum *item = (struct item_datum *)object_try_and_get_and_verify_type(bot->item_index, _object_mask_equipment);
		real distance;

		if (!item || item->object.parent_object_index != NONE)
		{
			/* (gone as the bot stood at it: taken) */
			if (bot->item_last_distance < 1.5f)
				bots_globals.powerups_taken[bot->item_powerup]++;
			if (bots_trace_enabled())
				platform_log("bots: trace bot %ld powerup %d gone, %.1f away, body %.2f", (long)(bot - bots_globals.bots) + 1,
					(int)bot->item_powerup, bot->item_last_distance, body);
			bot->goal_kind = _bots_goal_none;
			bot->item_powerup = _bots_powerup_none;
			return FALSE;
		}
		distance = bots_distance3d(&item->object.position, position);
		bot->item_last_distance = distance;
		if (distance < 0.8f && !bot->item_arrived_tick)
			bot->item_arrived_tick = now;
		if (now < bot->item_deadline_tick && (!bot->item_arrived_tick || now - bot->item_arrived_tick < TICKS_PER_SECOND * 2) &&
			(bot->item_powerup != _bots_powerup_health || body < 1.0f))
		{
			return TRUE;
		}
		/* (a health pack's body whole as it stood at it: taken, the pack
		gone the tick after) */
		if (bot->item_powerup == _bots_powerup_health && body >= 1.0f && distance < 1.5f)
		{
			bots_globals.powerups_taken[_bots_powerup_health]++;
			bot->goal_kind = _bots_goal_none;
			bot->item_powerup = _bots_powerup_none;
			return FALSE;
		}
		if (bots_trace_enabled())
			platform_log("bots: trace bot %ld powerup %d given up, %.1f away, body %.2f, %s", (long)(bot - bots_globals.bots) + 1,
				(int)bot->item_powerup, distance, body, now >= bot->item_deadline_tick ? "late" :
				bot->item_arrived_tick ? "stood at it" : "whole");
		bots_item_set_unreachable(bot, bot->item_index);
		bot->goal_kind = _bots_goal_none;
		bot->item_powerup = _bots_powerup_none;
		return FALSE;
	}
	if (now < bot->powerup_check_tick || bots_powerups.count <= 0)
		return FALSE;
	bot->powerup_check_tick = now + TICKS_PER_SECOND;
	heal_below = fighting ? skill->heal_below_fighting : skill->heal_below;
	objective_game = game_engine_get_variant()->game_engine_index != game_engine_slayer;
	/* port: (holding a vantage point, an overshield or camouflage only
	beside it: bots_tactics.h) */
	holding = bots_tactics_holding((long)(bot - bots_globals.bots));
	for (index = 0; index < bots_powerups.count; index++)
	{
		long object_index = bots_powerups.items[index].object_index;
		short kind = bots_powerups.items[index].kind;
		real distance = bots_distance3d(&bots_powerups.items[index].position, position);
		real reach, score;
		long other_index;

		if (bots_item_unreachable(bot, object_index))
			continue;
		/* (taken since the list was made) */
		if (!object_try_and_get_and_verify_type(object_index, _object_mask_equipment) ||
			object_get(object_index)->object.parent_object_index != NONE)
		{
			continue;
		}
		if (kind == _bots_powerup_health)
		{
			if (body >= heal_below)
				continue;
			/* (the more hurt, the further: 15 units in a fight, 45 out of
			one, scored nearer by how hurt it is) */
			reach = fighting ? 15.0f : 45.0f;
			score = distance - (heal_below - body) * 40.0f;
		}
		else
		{
			reach = fighting ? 10.0f : objective_game ? MIN(skill->powerup_reach, 12.0f) : skill->powerup_reach;
			if (holding)
				reach = MIN(reach, 6.0f);
			score = distance - 6.0f;
		}
		if (distance > reach || score >= best_score)
			continue;
		/* (another bot going for it from nearer: its) */
		for (other_index = 0; other_index < BOTS_MAXIMUM; other_index++)
		{
			struct bots_bot const *other = &bots_globals.bots[other_index];

			if (other != bot && other->player_index != NONE && other->goal_kind == _bots_goal_item &&
				other->item_index == object_index && other->item_last_distance < distance)
			{
				break;
			}
		}
		if (other_index < BOTS_MAXIMUM)
			continue;
		best_score = score;
		best = index;
	}
	if (best == NONE)
		return FALSE;
	surface_index = bots_surface_below(&bots_powerups.items[best].position, &ground);
	if (surface_index == NONE)
		return FALSE;
	bots_set_goal(bot, _bots_goal_item, &ground, surface_index);
	bot->item_index = bots_powerups.items[best].object_index;
	bot->item_powerup = bots_powerups.items[best].kind;
	bot->powerup_index = bot->item_index;
	bot->item_last_distance = bots_distance3d(&bots_powerups.items[best].position, position);
	bot->item_arrived_tick = 0;
	bots_globals.powerups_sought[bot->item_powerup]++;
	if (bots_trace_enabled())
		platform_log("bots: trace bot %ld goes for powerup %d at (%.1f %.1f %.1f), %.1f away, body %.2f%s",
			(long)(bot - bots_globals.bots) + 1, (int)bot->item_powerup, bots_powerups.items[best].position.x,
			bots_powerups.items[best].position.y, bots_powerups.items[best].position.z, bot->item_last_distance, body,
			fighting ? ", fighting" : "");
	/* (the walk there - two units a second, the way half as long again -
	and ten seconds more) */
	bot->item_deadline_tick = now + TICKS_PER_SECOND * 10 + (long)(bot->item_last_distance * 0.75f * TICKS_PER_SECOND);
	return TRUE;
}

/* where the bot goes: a flag it carries home, a vehicle it goes for, a
health pack or powerup, the flags' urgent goals, a vantage point to hold
(bots_tactics.h), the enemy last seen (Slayer), a better weapon near, the
game's own goals by Quake III's rules (capture the flag, king of the hill;
Infection's), or about the map */
static void bots_choose_goal(struct bots_bot *bot, long player_index, real_point3d const *position, boolean fighting)
{
	long now = game_time_get();
	long bot_index = (long)(bot - bots_globals.bots);
	struct player_datum *player = player_get(player_index);
	boolean ctf = bots_ctf_active();
	boolean king = bots_king_active();
	boolean on_hill = king && bots_king_on_hill(player->unit_index);
	boolean infection = infection_enabled();
	boolean zombie = infection && bots_zombie(player);
	/* (a while from the game's goals, which it found no way to) */
	boolean objectives = now >= bot->objective_rest_until_tick;

	bot->goal_exact = FALSE;
	/* (off its vantage point when it is time, whatever it goes for) */
	bots_tactics_check(bot_index, player_index);
	/* (carrying a flag: home, nothing else) */
	if (ctf && bots_ctf_carrying(bot) && bots_ctf_choose_goal(bot, player, position))
	{
		bots_tactics_objective(bot_index);
		return;
	}
	if (bot->vehicle_index != NONE && bots_vehicle_goal(bot, player->unit_index))
		return;
	/* a health pack when hurt, an overshield or camouflage near: before the
	game's goals, but for the ball it carries, nor a zombie (which takes
	nothing), nor on the hill it holds */
	if (!bot->held_objective && !zombie && !on_hill && bots_powerup_goal(bot, unit_get(player->unit_index), position, fighting))
		return;
	if (objectives && ctf && bots_ctf_urgent_goal(bot, player, position))
	{
		bots_tactics_objective(bot_index);
		return;
	}
	/* a vantage point to hold: a precision weapon's (a sniper rifle's, a
	rocket launcher's; a pistol's roaming), a defender's with a sniper rifle
	over its own stand (bots_roles.c); not on king of the hill's hill nor in
	Infection, whose bots keep to their own rules */
	if (objectives && !bot->held_objective && !king && !infection)
	{
		real_point3d post;
		long post_surface_index;

		if (bots_tactics_goal(bot_index, player_index, position, &post, &post_surface_index))
		{
			bots_set_goal(bot, _bots_goal_objective, &post, post_surface_index);
			return;
		}
	}
	/* (capture the flag's and king of the hill's bots go for the flags and
	the hill, not after whom they last saw) */
	if (!ctf && !king && !infection && !fighting && bot->target_player_index != NONE && bot->target_last_surface_index != NONE &&
		now - bot->target_seen_tick < TICKS_PER_SECOND * 8)
	{
		bots_set_goal(bot, _bots_goal_enemy, &bot->target_last_position, bot->target_last_surface_index);
		return;
	}
	/* a better weapon or a powerup near (looked for once a second; given up
	after a while) */
	if (bot->goal_kind == _bots_goal_item)
	{
		struct item_datum *item = (struct item_datum *)object_try_and_get_and_verify_type(bot->item_index,
			_object_mask_weapon | _object_mask_equipment);

		if (item && item->object.parent_object_index == NONE && now - bot->goal_tick < TICKS_PER_SECOND * 15 &&
			(bot->item_first_press_tick < bot->goal_tick || now - bot->item_first_press_tick < TICKS_PER_SECOND * 3))
		{
			return;
		}
		/* (one it did not get, standing on it or not: left be a while) */
		if (item && item->object.parent_object_index == NONE)
			bots_item_set_unreachable(bot, bot->item_index);
		bot->goal_kind = _bots_goal_none;
		bot->item_check_tick = now + TICKS_PER_SECOND * 2;
	}
	/* (not while holding the hill, nor a zombie, which carries its skull
	alone, nor at a vantage point) */
	if (now >= bot->item_check_tick && !on_hill && !zombie && !bots_tactics_holding(bot_index))
	{
		long item_index = bots_find_item(bot, unit_get(player->unit_index), position, fighting ? 8.0f : 20.0f);

		bot->item_check_tick = now + TICKS_PER_SECOND;
		if (item_index != NONE)
		{
			real_point3d ground;
			long surface_index = bots_surface_below(&object_get(item_index)->object.position, &ground);

			if (surface_index != NONE)
			{
				bots_set_goal(bot, _bots_goal_item, &ground, surface_index);
				bot->item_index = item_index;
				return;
			}
		}
	}
	if (objectives)
	{
		if (ctf && bots_ctf_choose_goal(bot, player, position))
			return;
		if (king && bots_king_choose_goal(bot, player, position))
			return;
		if (infection && bots_infection_choose_goal(bot, player, position))
			return;
	}
	if (bot->goal_kind != _bots_goal_roam || bots_distance2d(&bot->goal_point, position) < 1.5f ||
		now - bot->goal_tick > TICKS_PER_SECOND * (bot->hunting ? 12 : 40))
	{
		bots_pick_roam_goal(bot, position);
	}
}

/* the direction the bot walks (radians), following its path; FALSE when
it has nowhere to go */
static boolean bots_follow_path(struct bots_bot *bot, long unit_index, real_point3d const *position, real *direction)
{
	long now = game_time_get();

	if (bot->goal_kind == _bots_goal_none)
		return FALSE;
	/* port: at its lane's waypoint (bots_routes.c): a new path, on to the
	goal (the search asked for the waypoint dropped) */
	if (bots_routes_waypoint_reached((long)(bot - bots_globals.bots), position))
	{
		bot->goal_serial++;
		bot->path.valid = FALSE;
		bot->path_tick = now - TICKS_PER_SECOND;
	}
	if (now < bot->escape_until_tick)
	{
		*direction = bot->escape_direction;
		return TRUE;
	}
	/* (at the end of a path short of its goal: straight on, jumping, while
	it comes nearer (the walkable surfaces miss ramps a biped walks up:
	Prisoner's, Wizard's); then the game's goals left a while) */
	if (bot->climb_until_tick)
	{
		real distance = bots_distance3d(&bot->goal_point, position);

		if (distance < bot->climb_best_distance - 0.5f && now - bot->climb_start_tick < TICKS_PER_SECOND * 20)
		{
			bot->climb_best_distance = distance;
			bot->climb_until_tick = now + TICKS_PER_SECOND * 3;
		}
		if (now < bot->climb_until_tick && distance > 1.0f)
		{
			*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
			return TRUE;
		}
		bot->climb_until_tick = 0;
		if (distance > 1.0f)
		{
			if (bot->goal_kind == _bots_goal_objective)
				bot->objective_rest_until_tick = now + TICKS_PER_SECOND * 10;
			bot->goal_kind = _bots_goal_none;
			return FALSE;
		}
	}
	/* port: (at a ladder's foot: up it, facing it, until it is up to the
	top's height - a biped walks up a climbable surface it faces, bipeds.c
	- then on to the landing beyond, five and two seconds at most; a new
	path from there. Once at its foot, no new search: one's answer started
	the climb again from the foot) */
	if (bot->via_teleporter && bot->via_index >= 0 && bot->via_index < bots_globals.teleporter_count &&
		bots_globals.teleporters[bot->via_index].ladder && (bot->via_climb_tick || !bot->path.valid))
	{
		long ladder = bot->via_index;

		if (!bot->via_climb_tick && now - bot->via_tick < TICKS_PER_SECOND * 6 &&
			bots_distance2d(&bot->via_point, position) > 0.3f)
		{
			*direction = (real)atan2(bot->via_point.y - position->y, bot->via_point.x - position->x);
			return TRUE;
		}
		if (!bot->via_climb_tick)
			bot->via_climb_tick = now;
		if (!bot->via_top_tick && position->z < bots_globals.teleporters[ladder].top_z - 0.25f &&
			now - bot->via_climb_tick < TICKS_PER_SECOND * 8)
		{
			*direction = bots_globals.teleporters[ladder].climb_direction;
			/* (a jump onto it, now and then: walked into, it holds a
			biped at its foot) */
			if (!((now - bot->via_climb_tick) % 15))
				bot->ladder_jump = TRUE;
			return TRUE;
		}
		if (!bot->via_top_tick)
		{
			bot->via_top_tick = now;
			if (position->z >= bots_globals.teleporters[ladder].top_z - 0.25f)
			{
				bots_globals.ladder_climbs++;
				bots_globals.teleporters[ladder].climbs++;
			}
			else
				bots_globals.teleporters[ladder].failures++;
			if (bots_trace_enabled())
				platform_log("bots: trace bot %ld off the ladder at (%.1f %.1f %.1f), top %.1f, after %.1f s", (long)(bot - bots_globals.bots) + 1,
					position->x, position->y, position->z, bots_globals.teleporters[ladder].top_z,
					(real)(now - bot->via_climb_tick) / TICKS_PER_SECOND);
		}
		if (now - bot->via_top_tick < TICKS_PER_SECOND * 2 &&
			bots_distance2d(&bots_globals.teleporters[ladder].exit, position) > 0.4f &&
			position->z >= bots_globals.teleporters[ladder].top_z - 0.5f)
		{
			*direction = (real)atan2(bots_globals.teleporters[ladder].exit.y - position->y,
				bots_globals.teleporters[ladder].exit.x - position->x);
			return TRUE;
		}
		bot->via_teleporter = FALSE;
		bot->path.valid = FALSE;
		bot->path_tick = now - TICKS_PER_SECOND;
		bots_search_ask(bot, unit_index);
	}
	/* (a new path now and then: the objects about move, and the goal) */
	if ((!bot->path.valid || now - bot->path_tick > TICKS_PER_SECOND * 4) &&
		now - bot->path_tick > TICKS_PER_SECOND / 3)
	{
		bots_search_ask(bot, unit_index);
	}
	if (bot->path.valid)
	{
		while (bot->path.step_index < bot->path.step_count)
		{
			struct path_step *step = &bot->path.steps[(long)bot->path.step_index];

			if (bots_distance2d(&step->point, position) > BOTS_STEP_REACHED)
				break;
			bot->path.step_index++;
		}
		if (bot->path.step_index >= bot->path.step_count)
		{
			bot->path.valid = FALSE;
			/* (as near as the search came, the goal on above or beyond:
			on towards it) */
			if (bot->path.steps_finish_path && bot->path_short && bots_distance3d(&bot->goal_point, position) > 1.5f &&
				!bot->climb_until_tick)
			{
				bot->climb_start_tick = now;
				bot->climb_until_tick = now + TICKS_PER_SECOND * 3;
				bot->climb_best_distance = bots_distance3d(&bot->goal_point, position);
				*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
				return TRUE;
			}
			/* (the whole way walked: there, or at the teleporter) */
			if (bot->path.steps_finish_path && !bot->via_teleporter)
				return FALSE;
			if (!bot->via_teleporter)
				bots_search_ask(bot, unit_index);
		}
		else
		{
			struct path_step *step = &bot->path.steps[(long)bot->path.step_index];

			*direction = (real)atan2(step->point.y - position->y, step->point.x - position->x);
			return TRUE;
		}
	}
	/* (at the teleporter: onto it, a few seconds at most; port: a ladder's
	foot, up it next tick, above) */
	if (bot->via_teleporter && !bots_globals.teleporters[bot->via_index].ladder)
	{
		if (now - bot->via_tick < TICKS_PER_SECOND * 4 && bots_distance2d(&bot->via_point, position) > 0.1f)
		{
			*direction = (real)atan2(bot->via_point.y - position->y, bot->via_point.x - position->x);
			return TRUE;
		}
		bot->via_teleporter = FALSE;
		bots_search_ask(bot, unit_index);
	}
	/* (no path yet, or none found: straight there) */
	if (bots_distance2d(&bot->goal_point, position) < 1.0f)
		return FALSE;
	*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
	return TRUE;
}

/* ---------- a bot's tick */

static void bots_bot_reset(struct bots_bot *bot, long player_index, long bot_index)
{
	long serial = bot->goal_serial;

	if (bot->search_slot != NONE && bots_globals.search_slots)
		bots_search_drop(&bots_globals.search_slots[bot->search_slot]);
	csmemset(bot, 0, sizeof(*bot));
	bot->search_slot = NONE;
	bot->player_index = player_index;
	bot->unit_index = NONE;
	bot->target_player_index = NONE;
	bot->target_last_surface_index = NONE;
	bot->goal_surface_index = NONE;
	bot->goal_serial = serial + 1;
	bot->random = 0x2545F491UL * (unsigned long)(bot_index + 1) ^ (unsigned long)network_game_get_random_seed();
	bot->strafe_sign = 1.0f;
	bot->vehicle_index = NONE;
	bot->vehicle_seat_index = NONE;
	bot->vehicle_avoid_index = NONE;
	bot->ride_vehicle_index = NONE;
	bot->powerup_index = NONE;
	bots_tactics_bot_reset(bot_index, player_index);
}

/* a bot's tick: what it knows of itself, and the controls it builds */
struct bots_tick
{
	struct bots_skill_definition const *skill;
	struct player_datum *player;
	struct unit_datum *unit;
	struct object_datum *object;
	long now;
	/* the unit's eyes (its head marker), found when first needed: the look
	(every third tick) and the fight (bots_tick_eye) */
	boolean eye_found;
	real_point3d eye;
	real_point3d position;
	long weapon_index;
	/* the controls: buttons, where it walks, how hard */
	unsigned long flags;
	boolean moving;
	real move_direction;
	real throttle_scale;
	/* it has a target in sight, this far away */
	boolean fighting;
	real target_distance;
	struct player_action *action;
};

/* the unit's eyes: its head marker, as unit_get_head_position finds it
(the marker's matrix from the object's node matrices, which nothing in the
bot's tick changes, so it is the same found now or at the tick's start) */
static real_point3d const *bots_tick_eye(struct bots_tick *tick)
{
	if (!tick->eye_found)
	{
		unit_get_head_position(tick->player->unit_index, &tick->eye);
		tick->eye_found = TRUE;
	}
	return &tick->eye;
}

/* ---------- fighting by level (bots_combat.c's rules) */

/* a bot's fight started over (a spawn) */
static void bots_level_reset(struct bots_bot_combat *combat)
{
	long index;

	csmemset(combat, 0, sizeof(*combat));
	bots_combat_retreat_reset(&combat->retreat);
	combat->last_fight_tick = -TICKS_PER_SECOND * 10;
	combat->melee_ready_tick = NONE;
	combat->pace_since_tick = NONE;
	combat->prenade_seen_tick = NONE;
	for (index = 0; index < BOTS_TRACK_TICKS; index++)
		combat->track[index].tick = NONE;
}

/* the aim point (its target's middle now) where the target was the level's
delay ago (RECRUIT and EASY), so that a target that turns back is missed a
moment, as a person misses it; the places kept a tick each */
static void bots_level_trail(struct bots_bot *bot, long now, real_point3d *aim_point)
{
	struct bots_bot_combat *combat = &bot->combat;
	long delay = bots_combat_track_delay_ticks(bots_globals.skill);
	long slot;

	if (now < 0)
		return;
	slot = now % BOTS_TRACK_TICKS;
	combat->track[slot].tick = now;
	combat->track[slot].player_index = bot->target_player_index;
	combat->track[slot].point = *aim_point;
	if (delay <= 0 || delay >= BOTS_TRACK_TICKS || now < delay)
		return;
	slot = (now - delay) % BOTS_TRACK_TICKS;
	if (combat->track[slot].tick == now - delay && combat->track[slot].player_index == bot->target_player_index)
		*aim_point = combat->track[slot].point;
}

/* whether it aims at its target's head: always with a weapon whose
headshots count at a target whose shields are down (NORMAL and up), else by
its skill's chance taken now and then (the lower levels; any weapon in SWAT,
where a shot to the head kills) */
static boolean bots_level_aim_head(struct bots_bot *bot, struct object_datum *target, short aim, boolean swat)
{
	struct bots_bot_combat *combat = &bot->combat;
	boolean precision = aim == _bots_aim_precision;
	boolean head = bots_combat_aim_head(bots_globals.skill, target->object.shield_vitality, precision) ||
		(bot->aim_head && (swat || (precision && target->object.shield_vitality <= 0.0f)));

	if (head && !combat->head_aim)
		bots_globals.combat_counts[_bots_combat_count_head_aims]++;
	combat->head_aim = head;
	return head;
}

/* with a grenade in the hand, the aim ahead of the target by its flight
(HEROIC and up), at most 3.5 units */
static void bots_level_aim_point(struct bots_bot *bot, struct bots_tick *tick, struct object_datum *target,
	real_point3d *aim_point)
{
	struct bots_bot_combat *combat = &bot->combat;

	if (tick->now < combat->throw_until_tick && combat->throw_lead > 0.0f)
	{
		struct unit_definition *definition = unit_definition_get(tick->object->definition_index);
		real speed = definition->unit.grenade_velocity / (real)TICKS_PER_SECOND;
		real ticks, dx, dy, lead;

		if (speed < 0.05f)
			speed = 0.6f;
		ticks = MIN(8.0f + bots_distance2d(bots_tick_eye(tick), aim_point) / speed, 60.0f) * combat->throw_lead;
		dx = target->object.translational_velocity.i * ticks;
		dy = target->object.translational_velocity.j * ticks;
		lead = (real)sqrt(dx * dx + dy * dy);
		if (lead > 3.5f)
		{
			dx *= 3.5f / lead;
			dy *= 3.5f / lead;
		}
		aim_point->x += dx;
		aim_point->y += dy;
	}
}

/* the aim error once settled: RECRUIT's wander that never settles; and,
for a far target, at least the level's floor (pushed out to 0 to 30% past it,
on its side), so that a shot from far off misses about half the time */
static void bots_level_aim_error(struct bots_bot *bot, struct bots_tick *tick)
{
	real *errors[2];
	real const scale[2] = { 1.0f, 0.6f };
	real floor = bots_combat_range_error_floor(bots_globals.skill, tick->target_distance) * BOTS_DEGREES;
	short index;

	errors[0] = &bot->aim_error_yaw;
	errors[1] = &bot->aim_error_pitch;
	for (index = 0; index < 2; index++)
	{
		real random = bots_random_signed(bot);

		*errors[index] = bots_combat_aim_wander(bots_globals.skill, *errors[index] / BOTS_DEGREES, scale[index], random) *
			BOTS_DEGREES;
		if (floor > 0.0f && (real)fabs(*errors[index]) < floor * scale[index])
		{
			real sign = *errors[index] > 0.0f ? 1.0f : *errors[index] < 0.0f ? -1.0f : random < 0.0f ? -1.0f : 1.0f;

			*errors[index] = sign * floor * scale[index] * (1.0f + 0.3f * (real)fabs(random));
		}
	}
}

/* whether the bot may fire now (NORMAL and up): the sniper rifle zoomed at
a target past 6 units, zoomed again a moment after a hit took the zoom (and
not fired past 10 units meanwhile); the pistol paced at range, its shot held
while its error is over the level's limit, a few ticks at most */
static boolean bots_level_may_fire(struct bots_bot *bot, struct bots_tick *tick)
{
	struct bots_bot_combat *combat = &bot->combat;
	struct bots_weapon_kind const *kind;
	short skill = bots_globals.skill;

	/* (a vehicle's gun: as ever) */
	if (tick->weapon_index == NONE || tick->object->object.parent_object_index != NONE)
		return TRUE;
	kind = bots_weapon_kind(weapon_get(tick->weapon_index)->definition_index);
	if (kind->sniper)
	{
		boolean zooms = bots_combat_zooms(skill, tick->target_distance);

		/* (hurt while zoomed: the game took the zoom) */
		if (bot->hurt_tick == tick->now && tick->unit->unit.current_zoom_level == NONE &&
			tick->now >= combat->rescope_until_tick && zooms)
		{
			combat->rescope_until_tick = tick->now + bots_combat_rescope_ticks(skill, bots_random_real(bot));
			bots_globals.combat_counts[_bots_combat_count_rescopes]++;
		}
		if (tick->now < combat->rescope_until_tick)
			return (boolean)!bots_combat_unzoomed_holds_fire(tick->target_distance);
		if (zooms)
			tick->action->desired_zoom_level = 0;
		return TRUE;
	}
	if (kind->aim == _bots_aim_precision && kind->blooms &&
		bots_combat_pace_holds(skill, tick->target_distance, weapon_get(tick->weapon_index)->weapon.triggers[0].error,
			combat->pace_since_tick == NONE ? 0 : tick->now - combat->pace_since_tick))
	{
		if (combat->pace_since_tick == NONE)
		{
			combat->pace_since_tick = tick->now;
			bots_globals.combat_counts[_bots_combat_count_paced]++;
		}
		return FALSE;
	}
	combat->pace_since_tick = NONE;
	return TRUE;
}

/* whether a unit has its back to a point (it faces more than 114.6 degrees
away from it) */
static boolean bots_unit_back_turned(long unit_index, real_point3d const *point)
{
	struct object_datum *object = (struct object_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	real dx, dy, length, forward_length;

	if (!object)
		return FALSE;
	dx = point->x - object->object.position.x;
	dy = point->y - object->object.position.y;
	length = (real)sqrt(dx * dx + dy * dy);
	forward_length = (real)sqrt(object->object.forward.i * object->object.forward.i +
		object->object.forward.j * object->object.forward.j);
	if (length < 0.01f || forward_length < 0.01f)
		return FALSE;
	return (dx * object->object.forward.i + dy * object->object.forward.j) / (length * forward_length) < -0.416f;
}

/* whether the bot strikes now: within the level's reach it waits the
level's moment first; a blow it chose not to strike is asked again a second
later */
static boolean bots_level_melee(struct bots_bot *bot, struct bots_tick *tick, struct object_datum *target,
	long target_unit_index)
{
	struct bots_bot_combat *combat = &bot->combat;
	int choice = bots_combat_melee(bots_globals.skill, tick->target_distance, tick->now >= bot->melee_tick,
		bots_unit_back_turned(target_unit_index, &tick->position), target->object.shield_vitality,
		combat->charging, bots_random_real(bot));

	if (choice == 0)
	{
		combat->melee_ready_tick = NONE;
		return FALSE;
	}
	if (combat->melee_ready_tick == NONE)
		combat->melee_ready_tick = tick->now + bots_combat_melee_delay(bots_globals.skill, bots_random_real(bot));
	if (tick->now < combat->melee_ready_tick)
		return FALSE;
	combat->melee_ready_tick = NONE;
	if (choice == 2)
	{
		bot->melee_tick = tick->now + TICKS_PER_SECOND;
		bots_globals.combat_counts[_bots_combat_count_blows_held]++;
		return FALSE;
	}
	bots_globals.combat_counts[_bots_combat_count_blows]++;
	return TRUE;
}

/* a grenade by the level's rules (the throw's aim leads the target's path
while it is in the hand, from HEROIC) */
static void bots_level_grenade(struct bots_bot *bot, struct bots_tick *tick, long reaction_ticks)
{
	struct bots_bot_combat *combat = &bot->combat;
	short skill = bots_globals.skill;
	long now = tick->now;
	boolean has = tick->unit->unit.grenade_counts[0] > 0 || tick->unit->unit.grenade_counts[1] > 0;

	if (now < bot->grenade_tick || !has)
		return;
	if (!bots_combat_grenade(skill, tick->target_distance, has, now - bot->target_first_seen_tick >= reaction_ticks,
		now - bot->target_first_seen_tick < TICKS_PER_SECOND * 3 / 2,
		bots_combat_about_to_die(tick->object->object.shield_vitality, tick->object->object.body_vitality),
		bots_skills[skill].grenade_chance, bots_random_real(bot)))
	{
		return;
	}
	tick->flags |= FLAG(_unit_control_throw_grenade_bit);
	if (tick->unit->unit.grenade_counts[(long)tick->unit->unit.current_grenade_index] <= 0)
		tick->action->desired_grenade_index = tick->unit->unit.grenade_counts[0] > 0 ? 0 : 1;
	bot->grenade_tick = now + bots_combat_grenade_cooldown(skill, bots_random_real(bot));
	combat->throw_until_tick = now + TICKS_PER_SECOND / 2;
	combat->throw_lead = bots_combat_grenade_lead(skill);
	bots_globals.combat_counts[_bots_combat_count_grenades]++;
}

/* a place to back off to: a roaming place 5 to 35 units off, not towards the
target, the furthest from the target for the least way; FALSE for none */
static boolean bots_level_retreat_goal(struct bots_bot *bot, struct bots_tick *tick, real_point3d const *target)
{
	long index, best = NONE;
	real best_score = -REAL_MAX;
	real tx = target->x - tick->position.x, ty = target->y - tick->position.y;
	real target_distance = (real)sqrt(tx * tx + ty * ty);

	for (index = 0; index < bots_globals.roam_point_count; index++)
	{
		real_point3d const *point = &bots_globals.roam_points[index];
		real px = point->x - tick->position.x, py = point->y - tick->position.y;
		real distance = (real)sqrt(px * px + py * py);
		real score;

		if (distance < 5.0f || distance > 35.0f || bots_globals.roam_surfaces[index] == NONE)
			continue;
		if (target_distance > 0.01f && (px * tx + py * ty) > 0.3f * distance * target_distance)
			continue;
		score = bots_distance2d(point, target) - 0.6f * distance;
		if (score > best_score)
		{
			best_score = score;
			best = index;
		}
	}
	if (best == NONE)
		return FALSE;
	bot->roam_index = (short)best;
	bot->hunting = FALSE;
	bots_set_goal(bot, _bots_goal_roam, &bots_globals.roam_points[best], bots_globals.roam_surfaces[best]);
	return TRUE;
}

/* the fight's moves by level, after the strafe: halts (it stands, keeping
its weapon's distance, at a fight's start and now and then at a new strafe),
a retreat along a path when losing (still shooting; not back to where the
enemy was), a run in to finish a won fight, a step in for a blow, jumps at
the level's rhythm. A bot that holds its place (on the hill, carrying the
flag or the ball, at a vantage point) neither halts, retreats, runs in nor
steps in; nor does one backing from a zombie (Infection) close in */
static void bots_level_move(struct bots_bot *bot, struct bots_tick *tick, struct object_datum *target, boolean holding,
	boolean keep_off)
{
	struct bots_bot_combat *combat = &bot->combat;
	short skill = bots_globals.skill;
	long now = tick->now;
	real_point3d const *target_point = &target->object.bounding_sphere_center;
	real to_target = (real)atan2(target_point->y - tick->position.y, target_point->x - tick->position.x);
	real own_shield = tick->object->object.shield_vitality;
	real target_shield = target->object.shield_vitality;
	real confidence = bots_combat_confidence(own_shield, tick->object->object.body_vitality, target_shield,
		target->object.body_vitality, tick->target_distance, bots_weapon_range(tick->weapon_index),
		tick->weapon_index != NONE ? bots_weapon_loaded(tick->weapon_index) : 1.0f,
		tick->object->object.bounding_sphere_center.z - target_point->z);
	boolean new_fight = now - combat->last_fight_tick > TICKS_PER_SECOND * 2 || now < combat->last_fight_tick;
	int started = FALSE, ended = FALSE;
	boolean retreating = FALSE;

	if (!holding)
	{
		retreating = (boolean)bots_combat_retreat_step(&combat->retreat, skill, confidence, own_shield, now,
			bots_combat_hash01((unsigned long)bot->player_index, (unsigned long)combat->retreat.next_allowed), &started,
			&ended);
	}
	else if (combat->retreat.active)
	{
		/* (its place comes first: a retreat is given up) */
		bots_combat_retreat_reset(&combat->retreat);
		ended = TRUE;
	}
	if (new_fight)
	{
		combat->halt_until_tick = bot->target_first_seen_tick + bots_combat_initial_halt_ticks(skill,
			bots_combat_hash01((unsigned long)bot->player_index ^ 0x27D4EB2DUL, (unsigned long)bot->target_first_seen_tick));
		combat->next_jump_tick = now + bots_combat_jump_delay(skill, bots_random_real(bot));
		combat->last_strafe_until_tick = bot->strafe_until_tick;
	}
	else if (bot->strafe_until_tick != combat->last_strafe_until_tick)
	{
		long halt = bots_combat_halt_ticks(skill, bots_random_real(bot), bots_random_real(bot));

		combat->last_strafe_until_tick = bot->strafe_until_tick;
		if (halt > 0)
		{
			combat->halt_until_tick = now + halt;
			bots_globals.combat_counts[_bots_combat_count_halts]++;
		}
	}
	combat->last_fight_tick = now;
	if (now < combat->halt_until_tick && !holding && !keep_off)
	{
		real range = bots_weapon_range(tick->weapon_index);

		if (tick->target_distance > range * 1.3f)
			tick->move_direction = to_target;
		else if (tick->target_distance < range * 0.6f)
			tick->move_direction = to_target + BOTS_PI;
		else
			tick->moving = FALSE;
	}
	if (started)
	{
		bots_level_retreat_goal(bot, tick, target_point);
		bots_globals.combat_counts[_bots_combat_count_retreats]++;
	}
	if (ended && bot->goal_kind == _bots_goal_roam)
		bot->goal_kind = _bots_goal_none;
	if (retreating)
	{
		real direction;

		bot->target_last_surface_index = NONE;
		bot->item_check_tick = MAX(bot->item_check_tick, now + TICKS_PER_SECOND);
		if (bot->goal_kind == _bots_goal_roam &&
			bots_follow_path(bot, tick->player->unit_index, &tick->position, &direction))
		{
			tick->move_direction = direction;
		}
		else
			tick->move_direction = to_target + BOTS_PI + bot->strafe_sign * 0.5f;
		tick->moving = TRUE;
		tick->throttle_scale = 1.0f;
		combat->charging = FALSE;
	}
	else if (!holding && !keep_off)
	{
		boolean charge = (boolean)bots_combat_charge(skill, confidence, target_shield, target->object.body_vitality,
			tick->target_distance);

		if (charge)
		{
			tick->move_direction = to_target + bot->strafe_sign * 0.25f;
			tick->moving = TRUE;
			tick->throttle_scale = 1.0f;
			if (!combat->charging)
				bots_globals.combat_counts[_bots_combat_count_charges]++;
		}
		else if (bots_combat_step_in(skill, tick->target_distance, target_shield))
		{
			tick->move_direction = to_target + bot->strafe_sign * 0.3f;
			tick->moving = TRUE;
			tick->throttle_scale = 1.0f;
		}
		combat->charging = charge;
	}
	else
		combat->charging = FALSE;
	if (combat->next_jump_tick > 0 && now >= combat->next_jump_tick)
	{
		long delay = bots_combat_jump_delay(skill, bots_random_real(bot));

		combat->next_jump_tick = delay > 0 ? now + delay : 0;
		if (delay > 0)
		{
			tick->flags |= FLAG(_unit_control_jump_bit);
			bots_globals.combat_counts[_bots_combat_count_jumps]++;
		}
	}
}

/* out of a fight (VETERAN and up): once a lost contact, a grenade at the
spot its target was last seen, 0.6 to 3 s after, the spot 5 to 20 units off
and in its line of sight; the aim held on the spot for the throw (half a
second) */
static void bots_level_prenade(struct bots_bot *bot, struct bots_tick *tick)
{
	struct bots_bot_combat *combat = &bot->combat;
	short skill = bots_globals.skill;
	boolean has = tick->unit->unit.grenade_counts[0] > 0 || tick->unit->unit.grenade_counts[1] > 0;
	struct player_datum *target;
	real_point3d point;
	real seconds, distance;

	if (tick->now < combat->prenade_until_tick)
	{
		real_point3d const *eye = bots_tick_eye(tick);
		real dx = combat->prenade_point.x - eye->x, dy = combat->prenade_point.y - eye->y;
		real turn = bots_skills[skill].turn_degrees_per_tick * BOTS_DEGREES;
		real yaw_off = bots_angle_difference((real)atan2(dy, dx), bot->yaw);
		real pitch_off = (real)atan2(combat->prenade_point.z - eye->z, (real)sqrt(dx * dx + dy * dy)) - bot->pitch;

		bot->yaw += PIN(yaw_off, -turn, turn);
		bot->pitch += PIN(pitch_off, -turn, turn);
		/* (the throw once the aim is on the spot) */
		if (combat->prenade_until_tick - tick->now > 1 && (real)fabs(yaw_off) < 0.1f && (real)fabs(pitch_off) < 0.1f)
		{
			tick->flags |= FLAG(_unit_control_throw_grenade_bit);
			if (tick->unit->unit.grenade_counts[(long)tick->unit->unit.current_grenade_index] <= 0)
				tick->action->desired_grenade_index = tick->unit->unit.grenade_counts[0] > 0 ? 0 : 1;
			combat->prenade_until_tick = tick->now + 1;
		}
		return;
	}
	if (skill < _bots_skill_veteran || bot->target_player_index == NONE || bot->target_last_surface_index == NONE ||
		combat->prenade_seen_tick == bot->target_seen_tick || !has || tick->now < bot->grenade_tick ||
		tick->object->object.parent_object_index != NONE || bots_zombie(tick->player))
	{
		return;
	}
	seconds = (real)(tick->now - bot->target_seen_tick) / TICKS_PER_SECOND;
	if (seconds < 0.6f)
		return;
	combat->prenade_seen_tick = bot->target_seen_tick;
	/* (about the target's middle where it stood) */
	point = bot->target_last_position;
	point.z += 0.3f;
	distance = bots_distance3d(bots_tick_eye(tick), &point);
	target = player_try_and_get(bot->target_player_index);
	if (!bots_combat_prenade(skill, seconds, distance, has, bots_random_real(bot)) ||
		!bots_line_of_sight(tick->player->unit_index, bots_tick_eye(tick), &point,
			target && target->unit_index != NONE ? target->unit_index : NONE))
	{
		return;
	}
	combat->prenade_point = point;
	combat->prenade_until_tick = tick->now + TICKS_PER_SECOND / 2;
	bot->grenade_tick = tick->now + bots_combat_grenade_cooldown(skill, bots_random_real(bot));
	bots_globals.combat_counts[_bots_combat_count_prenades]++;
}

/* looking for enemies (every third tick, the bots in turn; (debug)
HALO_BOT_PEACEFUL=1: never, for the moving alone), and forgetting the one
gone a while */
static void bots_bot_look(struct bots_bot *bot, struct bots_tick *tick)
{
	if (tick->now >= bot->next_look_tick && !bots_peaceful())
	{
		long target = bots_find_target(bot, tick->player, tick->unit, bots_tick_eye(tick));

		bot->next_look_tick = tick->now + 3;
		/* (3 to 5 ticks apart: never a metronome) */
		bot->next_look_tick += bots_combat_look_jitter(bots_combat_hash01((unsigned long)bot->player_index ^ 0x5BD1E995UL,
			(unsigned long)tick->now));
		if (target != NONE)
		{
			struct player_datum *target_player = player_get(target);
			struct object_datum *target_object = object_get(target_player->unit_index);
			real_point3d ground;
			long surface_index;

			/* (a new target: the aim's error as it first turns to it) */
			if (target != bot->target_player_index || tick->now - bot->target_seen_tick > TICKS_PER_SECOND * 2)
			{
				real error = tick->skill->aim_error_degrees * BOTS_DEGREES;

				bot->target_first_seen_tick = tick->now;
				bot->aim_error_yaw = bots_random_signed(bot) * error;
				bot->aim_error_pitch = bots_random_signed(bot) * error * 0.6f;
			}
			bot->target_player_index = target;
			bot->target_seen_tick = tick->now;
			bot->target_last_position = target_object->object.position;
			surface_index = bots_unit_surface(target_player->unit_index, &ground);
			if (surface_index != NONE)
				bot->target_last_surface_index = surface_index;
		}
	}
	if (bot->target_player_index != NONE)
	{
		struct player_datum *target_player = player_try_and_get(bot->target_player_index);

		if (!target_player || target_player->unit_index == NONE ||
			tick->now - bot->target_seen_tick > TICKS_PER_SECOND * 10)
		{
			bot->target_player_index = NONE;
		}
	}
}

/* fighting the target seen in the last half second: aim (turning at its
speed, leading, an error that settles), fire, a grenade, a blow, strafing;
each by its level (bots_combat.c's rules) */
static void bots_bot_fight(struct bots_bot *bot, struct bots_tick *tick)
{
	struct bots_skill_definition const *skill = tick->skill;
	struct player_datum *target_player = player_get(bot->target_player_index);
	struct object_datum *target_object = object_get(target_player->unit_index);
	real_point3d aim_point = target_object->object.bounding_sphere_center;
	boolean zombie = bots_zombie(tick->player);
	long reaction_ticks = (long)(skill->reaction_seconds * TICKS_PER_SECOND);
	long now = tick->now;
	real desired_yaw, desired_pitch, dx, dy, dz, horizontal, turn, yaw_off, pitch_off, cone;
	real settle = (real)exp(-1.0f / (skill->aim_settle_seconds * TICKS_PER_SECOND) * 1.1f);

	tick->fighting = TRUE;
	/* (a reaction of 0.85 to 1.35 of the level's, another each sighting; the
	aim trailing the target at the lowest levels) */
	reaction_ticks = bots_combat_reaction_ticks(bots_globals.skill, reaction_ticks,
		bots_combat_hash01((unsigned long)bot->player_index, (unsigned long)bot->target_first_seen_tick));
	bots_level_trail(bot, now, &aim_point);
	bots_tick_eye(tick);
	/* where on the target: the head with a weapon whose headshots count (the
	pistol and the sniper rifle once its shields are down: always from
	normal, by the skill's chance below it; any weapon in SWAT, where a shot
	to the head kills, by the chance), the ground at its feet with one whose
	blast hurts (when it stands no higher than the bot's eyes), by the
	skill's chance; the chances taken once a target and again every couple
	of seconds */
	{
		short aim = tick->weapon_index != NONE ?
			bots_weapon_kind(weapon_get(tick->weapon_index)->definition_index)->aim : _bots_aim_body;
		boolean swat = game_modes_mode() == _game_mode_swat;

		if (bot->aim_choice_target != bot->target_player_index || now >= bot->aim_choice_until_tick)
		{
			bot->aim_choice_target = bot->target_player_index;
			bot->aim_choice_until_tick = now + TICKS_PER_SECOND * 2 + (long)(bots_random(bot) % TICKS_PER_SECOND);
			bot->aim_head = bots_random_real(bot) < skill->headshot_chance;
			bot->aim_feet = bots_random_real(bot) < skill->feet_chance;
		}
		if (bots_level_aim_head(bot, target_object, aim, swat))
		{
			unit_get_head_position(target_player->unit_index, &aim_point);
		}
		else if (bot->aim_feet && aim == _bots_aim_explosive && aim_point.z <= tick->eye.z + 0.3f)
		{
			real_point3d ground;

			if (bots_surface_below(&target_object->object.position, &ground) != NONE &&
				target_object->object.position.z - ground.z < 1.5f)
			{
				aim_point = ground;
				aim_point.z += 0.1f;
			}
		}
	}
	/* (ahead of a throw, from heroic) */
	bots_level_aim_point(bot, tick, target_object, &aim_point);
	tick->target_distance = bots_distance3d(&tick->eye, &aim_point);
	/* (leading a moving target by the projectile's flight) */
	if (tick->weapon_index != NONE && skill->lead > 0.0f)
	{
		real speed = bots_projectile_speed(tick->weapon_index);

		if (speed > 0.01f)
		{
			real ticks = MIN(tick->target_distance / speed, 30.0f);

			aim_point.x += target_object->object.translational_velocity.i * ticks * skill->lead;
			aim_point.y += target_object->object.translational_velocity.j * ticks * skill->lead;
			aim_point.z += target_object->object.translational_velocity.k * ticks * skill->lead;
		}
	}
	dx = aim_point.x - tick->eye.x;
	dy = aim_point.y - tick->eye.y;
	dz = aim_point.z - tick->eye.z;
	horizontal = (real)sqrt(dx * dx + dy * dy);
	/* the aim error settles while the target stays in sight, with a tremble */
	bot->aim_error_yaw = bot->aim_error_yaw * settle +
		bots_random_signed(bot) * skill->aim_error_degrees * 0.04f * BOTS_DEGREES;
	bot->aim_error_pitch = bot->aim_error_pitch * settle +
		bots_random_signed(bot) * skill->aim_error_degrees * 0.03f * BOTS_DEGREES;
	/* (recruit's wander, the error kept at range) */
	bots_level_aim_error(bot, tick);
	desired_yaw = (real)atan2(dy, dx) + bot->aim_error_yaw;
	desired_pitch = (real)atan2(dz, horizontal) + bot->aim_error_pitch;
	turn = skill->turn_degrees_per_tick * BOTS_DEGREES;
	yaw_off = bots_angle_difference(desired_yaw, bot->yaw);
	pitch_off = desired_pitch - bot->pitch;
	bot->yaw += PIN(yaw_off, -turn, turn);
	bot->pitch += PIN(pitch_off, -turn, turn);

	/* fire: once it has reacted, while its aim is on the target (its size
	at that distance) */
	yaw_off = (real)fabs(bots_angle_difference((real)atan2(dy, dx), bot->yaw));
	pitch_off = (real)fabs((real)atan2(dz, horizontal) - bot->pitch);
	cone = (real)atan(0.35f / MAX(tick->target_distance, 0.5f)) * skill->fire_cone + 0.01f;
	/* (the sniper rifle zoomed, the pistol paced) */
	if (!zombie && !bots_level_may_fire(bot, tick))
		cone = -1.0f;
	if (!zombie && now - bot->target_first_seen_tick >= reaction_ticks && now - bot->target_seen_tick <= 3 &&
		yaw_off < cone && pitch_off < cone * 1.5f && tick->weapon_index != NONE)
	{
		short tap_ticks = bots_weapon_tap_ticks(tick->weapon_index);

		if (tap_ticks > 0)
		{
			/* (a press a shot, released between: as fast as the weapon fires,
			a little slower for the less skilled) */
			if (now >= bot->burst_until_tick)
			{
				tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
				bot->burst_until_tick = now + tap_ticks + (long)(bots_random(bot) % (1 + skill->pause_ticks / 3));
			}
		}
		else if (now < bot->burst_until_tick)
			tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		else if (now >= bot->pause_until_tick)
		{
			/* (bursts, with pauses between) */
			bot->burst_until_tick = now + skill->burst_ticks + (long)(bots_random(bot) % 4);
			bot->pause_until_tick = bot->burst_until_tick + skill->pause_ticks + (long)(bots_random(bot) % 4);
			tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		}
	}
	/* a blow close up: a zombie's, its skull's, a little further and more
	often (its only one); else the level's reach, its moment and its wait */
	if (zombie)
	{
		if (tick->target_distance < 1.6f && now >= bot->melee_tick)
		{
			tick->flags |= FLAG(_unit_control_use_equipment_bit);
			bot->melee_tick = now + TICKS_PER_SECOND / 2;
		}
	}
	else if (bots_level_melee(bot, tick, target_object, target_player->unit_index))
	{
		tick->flags |= FLAG(_unit_control_use_equipment_bit);
		bot->melee_tick = now + bots_combat_melee_cooldown(bots_globals.skill, bots_random_real(bot));
	}
	/* a grenade at middle range, by the level's rules */
	if (!zombie)
		bots_level_grenade(bot, tick, reaction_ticks);
	/* strafing, to a side for a while (the level's strafes, short jiggles at
	the top); nearer or further to keep its weapon's distance */
	if (now >= bot->strafe_until_tick)
	{
		bot->strafe_sign = bots_random_real(bot) < 0.5f ? -1.0f : 1.0f;
		bot->strafe_until_tick = now + bots_combat_strafe_ticks(bots_globals.skill, bots_random_real(bot));
	}
	{
		real to_target = (real)atan2(dy, dx);
		real range = bots_weapon_range(tick->weapon_index);
		real forward = tick->target_distance > range * 1.3f ? 0.8f : tick->target_distance < range * 0.6f ? -0.5f : 0.0f;
		real side = bot->strafe_sign * bots_combat_strafe_strength(bots_globals.skill);

		/* (Infection: a zombie straight at them, little to the side; a human
		backs away from a zombie close by) */
		if (zombie)
		{
			forward = 1.0f;
			side *= 0.2f;
		}
		else if (infection_enabled() && infection_unit_is_zombie(target_player->unit_index) &&
			tick->target_distance < 7.0f)
		{
			forward = -1.0f;
		}

		tick->move_direction = (real)atan2((real)sin(to_target) * forward + (real)sin(to_target + BOTS_PI * 0.5f) * side,
			(real)cos(to_target) * forward + (real)cos(to_target + BOTS_PI * 0.5f) * side);
		tick->moving = TRUE;
		tick->throttle_scale = 1.0f;
		/* (king of the hill: a bot on the hill strafes on it, the other way at
		its edge, and back towards its middle if both ways leave it) */
		if (bots_king_active() && bots_king_on_hill(tick->player->unit_index) &&
			!bots_king_guard(bot, tick->player))
		{
			real_point3d ahead = tick->object->object.bounding_sphere_center;

			ahead.x += (real)cos(tick->move_direction) * 1.2f;
			ahead.y += (real)sin(tick->move_direction) * 1.2f;
			if (!game_engine_king_point_on_hill(&ahead))
			{
				bot->strafe_sign = -bot->strafe_sign;
				tick->move_direction = (real)atan2(bots_globals.king.center.y - tick->position.y,
					bots_globals.king.center.x - tick->position.x);
			}
		}
	}
	/* crouches, halts, a retreat, a charge, a step in, jumps, by level (on
	foot: a rider's vehicle moves it; not a zombie's: Infection's own fight) */
	if (!zombie && tick->object->object.parent_object_index == NONE)
	{
		boolean on_hill = bots_king_active() && bots_king_on_hill(tick->player->unit_index);
		boolean holding = on_hill || bot->held_objective || bots_tactics_holding((long)(bot - bots_globals.bots));
		boolean keep_off = infection_enabled() && infection_unit_is_zombie(target_player->unit_index);

		if (bots_combat_crouch_chance(bots_globals.skill) > 0.0f &&
			bots_combat_crouch_step(&bot->combat.crouch, bots_globals.skill, now, bots_random_real(bot)))
		{
			tick->flags |= FLAG(_unit_control_crouch_modifier_bit);
		}
		bots_level_move(bot, tick, target_object, holding, keep_off);
	}
}

/* walking to the goal, looking where it goes; there, a look about (and the
last bit straight onto a spot or an item); a reload when no one is about */
static void bots_bot_walk(struct bots_bot *bot, struct bots_tick *tick)
{
	real direction;

	if (bots_follow_path(bot, tick->player->unit_index, &tick->position, &direction))
	{
		real turn = 12.0f * BOTS_DEGREES;
		real off = bots_angle_difference(direction, bot->yaw);

		tick->move_direction = direction;
		tick->moving = TRUE;
		bot->yaw += PIN(off, -turn, turn);
		bot->pitch += PIN(-bot->pitch, -0.05f, 0.05f);
		if (bot->climb_until_tick && tick->now < bot->climb_until_tick && (bot->climb_until_tick - tick->now) % 20 == 0)
			tick->flags |= FLAG(_unit_control_jump_bit);
		if (bot->ladder_jump)
		{
			tick->flags |= FLAG(_unit_control_jump_bit);
			bot->ladder_jump = FALSE;
		}
	}
	else
	{
		bot->yaw += 2.0f * BOTS_DEGREES;
		if (bot->goal_kind == _bots_goal_roam || bot->goal_kind == _bots_goal_enemy)
			bot->goal_kind = _bots_goal_none;
		/* (on to a spot that must be stood on) */
		if (bot->goal_kind == _bots_goal_objective && bot->goal_exact &&
			bots_distance2d(&bot->goal_point, &tick->position) > 0.15f)
		{
			tick->move_direction = (real)atan2(bot->goal_point.y - tick->position.y, bot->goal_point.x - tick->position.x);
			tick->moving = TRUE;
			tick->throttle_scale = 0.5f;
		}
		/* (at an item: on to it) */
		if (bot->goal_kind == _bots_goal_item)
		{
			struct object_datum *item = object_try_and_get_and_verify_type(bot->item_index, _object_mask_item);

			if (item && bots_distance2d(&item->object.position, &tick->position) > 0.3f)
			{
				tick->move_direction = (real)atan2(item->object.position.y - tick->position.y,
					item->object.position.x - tick->position.x);
				tick->moving = TRUE;
				tick->throttle_scale = 0.6f;
			}
		}
	}
	if (tick->weapon_index != NONE && bots_weapon_loaded(tick->weapon_index) < 0.4f &&
		tick->now - bot->target_seen_tick > TICKS_PER_SECOND * 2)
	{
		tick->flags |= FLAG(_unit_control_weapon_reload_bit);
	}
	/* port: standing at a vantage point or a defender's post: looking from
	one place it watches to another (bots_tactics.h) */
	if (!tick->moving)
		bots_tactics_look((long)(bot - bots_globals.bots), &tick->position, tick->now, &bot->yaw, &bot->pitch);
}

/* the bots in each other's way (a team's bots leaving their base by the same
door, Longest's, jammed in it a whole game, each going round the others):
the one with the higher index steps back from the other, while the other
moves, so that the first goes through */
static void bots_bot_yield(struct bots_bot *bot, long bot_index, struct bots_tick *tick)
{
	long index;

	if (!tick->moving || tick->fighting || tick->object->object.parent_object_index != NONE)
		return;
	for (index = 0; index < bot_index; index++)
	{
		struct bots_bot *other = &bots_globals.bots[index];
		struct object_datum *object;
		real dx, dy, distance, velocity;

		if (other->player_index == NONE || other->unit_index == NONE)
			continue;
		object = (struct object_datum *)object_try_and_get_and_verify_type(other->unit_index, _object_mask_unit);
		if (!object || object->object.parent_object_index != NONE)
			continue;
		dx = object->object.position.x - tick->position.x;
		dy = object->object.position.y - tick->position.y;
		distance = (real)sqrt(dx * dx + dy * dy);
		velocity = (real)sqrt(object->object.translational_velocity.i * object->object.translational_velocity.i +
			object->object.translational_velocity.j * object->object.translational_velocity.j);
		if (distance > 1.1f || distance < 0.01f || fabs(object->object.position.z - tick->position.z) > 1.5f ||
			velocity < 0.02f ||
			(dx * (real)cos(tick->move_direction) + dy * (real)sin(tick->move_direction)) < 0.3f * distance)
		{
			continue;
		}
		tick->move_direction = (real)atan2(-dy, -dx);
		tick->throttle_scale = 0.6f;
		return;
	}
}

/* weapons: the other one when this one is empty; at the weapon it fetches,
the action button (X) held, as a player holds it to swap a weapon (the swap:
players_update_before_game's player_handle_weapon_swap), let go now and
then, so that a second swap can follow a first */
static void bots_bot_weapons(struct bots_bot *bot, struct bots_tick *tick)
{
	if (tick->weapon_index != NONE && bots_weapon_empty(tick->weapon_index))
	{
		short slot;

		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long other = tick->unit->unit.weapon_object_indices[slot];

			if (slot != tick->unit->unit.current_weapon_index && other != NONE && !bots_weapon_empty(other))
			{
				tick->action->desired_weapon_index = slot;
				break;
			}
		}
	}
	if (bot->goal_kind == _bots_goal_item)
	{
		struct object_datum *item = object_try_and_get_and_verify_type(bot->item_index, _object_mask_weapon);

		if (item && item->object.parent_object_index == NONE &&
			bots_distance2d(&item->object.position, &tick->position) < 0.8f &&
			fabs(item->object.position.z - tick->position.z) < 1.2f)
		{
			if (tick->now - bot->item_press_tick > TICKS_PER_SECOND)
				bot->item_press_tick = tick->now;
			if (bot->item_first_press_tick < bot->goal_tick)
				bot->item_first_press_tick = tick->now;
			if (tick->now - bot->item_press_tick < TICKS_PER_SECOND / 2)
				tick->flags |= FLAG(_unit_control_swap_weapons_bit);
		}
	}
}

/* port: what stands before a stuck bot, the way it walks (halo.log's
moves line): a ray at its knees and one at its chest, half a unit on, against
the level, scenery and machines (not the other bots); a low wall when the
first meets one and the second none */
static short bots_stuck_obstacle(struct bots_bot *bot, struct bots_tick *tick)
{
	real_vector3d vector = { (real)cos(tick->move_direction) * 0.6f, (real)sin(tick->move_direction) * 0.6f, 0.0f };
	unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit);
	struct collision_result collision;
	real_point3d knee = tick->position, chest = tick->position;
	boolean low, high;

	(void)bot;
	knee.z += BOTS_KNEE_HEIGHT;
	chest.z += BOTS_CHEST_HEIGHT;
	low = collision_test_vector(flags, &knee, &vector, tick->player->unit_index, &collision);
	high = collision_test_vector(flags, &chest, &vector, tick->player->unit_index, &collision);
	return high ? _bots_obstacle_tall : low ? _bots_obstacle_low : _bots_obstacle_none;
}

/* stuck: not moving for a second while it walks - a jump and a sidestep,
then another path; somewhere else after three */
static void bots_bot_unstick(struct bots_bot *bot, struct bots_tick *tick)
{
	long now = tick->now;

	if (now - bot->stuck_tick >= TICKS_PER_SECOND)
	{
		/* (port: not up a ladder, where it moves up alone) */
		if (tick->moving && bots_distance2d(&tick->position, &bot->stuck_position) < 0.4f &&
			tick->object->object.parent_object_index == NONE &&
			!(bot->via_teleporter && bot->via_climb_tick && !bot->via_top_tick))
		{
			short obstacle = bots_stuck_obstacle(bot, tick);

			bots_globals.stuck_events++;
			if (obstacle == _bots_obstacle_low)
				bots_globals.stuck_low++;
			else if (obstacle == _bots_obstacle_tall)
				bots_globals.stuck_tall++;
			else
				bots_globals.stuck_clear++;
			bot->stuck_count++;
			bot->unstick_until_tick = now + TICKS_PER_SECOND / 2;
			bot->unstick_direction = tick->move_direction + (bots_random_real(bot) < 0.5f ? -1.0f : 1.0f) * BOTS_PI * 0.5f;
			bot->path.valid = FALSE;
			if (bot->stuck_count >= 3)
			{
				bots_globals.stuck_giveups++;
				bot->goal_kind = _bots_goal_none;
				bot->stuck_count = 0;
				if (bots_globals.roam_point_count > 0)
					bots_pick_roam_goal(bot, &tick->position);
			}
		}
		else
			bot->stuck_count = 0;
		bot->stuck_position = tick->position;
		bot->stuck_tick = now;
	}
	if (now < bot->unstick_until_tick)
	{
		tick->move_direction = bot->unstick_direction;
		tick->moving = TRUE;
		if (now == bot->unstick_until_tick - TICKS_PER_SECOND / 2)
			tick->flags |= FLAG(_unit_control_jump_bit);
	}
}

/* ---------- vehicles (port)

The bots' own: the campaign's AI gets in, drives and guns through its actors
(action_vehicle.c, actor_moving.c), which a bot - a player, with no actor -
has not; a bot gets in and drives with a player's controls, as its seats and
ways are weighed there. */

/* port: a teammate bot going for a seat the bot is much nearer to
(BOTS_VEHICLE_TAKE_OVER), which it takes over: a Scorpion a bot set off for
from 45 units away goes to the bot that spawns beside it. Teammates alone (a
free-for-all's bots keep what they go for); the other bot, on foot as any
going for a seat is, lets it go (bots_vehicle_consider) and looks again in a
while; it can never take it back, being further from it */
static boolean bots_seat_take_over(struct bots_bot const *bot, struct bots_bot const *other, long vehicle_index,
	real distance)
{
	struct object_datum *unit = object_try_and_get(other->unit_index);

	return unit && game_engine_has_teams() &&
		player_get(other->player_index)->team_index == player_get(bot->player_index)->team_index &&
		bots_distance2d(&unit->object.position, &object_get(vehicle_index)->object.position) >
			distance + BOTS_VEHICLE_TAKE_OVER;
}

/* on foot: a vehicle to go for, looked for once a second (port: at once as
it spawns) while the bot's goal is a long way off
(BOTS_VEHICLE_GOAL_DISTANCE) and no fight is close, nor does it carry the
flag or the ball: an empty Warthog, Ghost or Scorpion standing still on its
way, its driver's seat, as its kind is worth (bots_vehicle_worths: a
Scorpion 50 units off, or 20 in any direction, 30 fresh from its base; a
Warthog 30 off, most of the way to the goal and not further from it than
the bot is); a Warthog a teammate drives (a bot, which waits; a player,
stopped), its gunner's seat, else a passenger's; or an empty vehicle on its
side close by, to right. A seat it can enter as a player does
(unit_can_enter_seat: no enemy in the vehicle; an animation for the seat),
none another bot goes for (port: but a teammate's much further from it,
bots_seat_take_over); the nearest, a Scorpion's before a Ghost's before a
Warthog's driver's seat by their worth, a gunner's seat before a
passenger's, as the campaign's AI weighs them
(action_vehicle_evaluate_seat). The walk there given up on after 25 s, or
longer for one far (bots_vehicle_approach) */
static void bots_vehicle_consider(struct bots_bot *bot, struct bots_tick *tick)
{
	struct object_iterator iterator;
	long now = tick->now;
	long unit_index = tick->player->unit_index;
	real goal_distance, best_score = REAL_MAX, best_distance = 0.0f;
	long best_index = NONE;
	short best_seat = NONE;
	struct bots_bot *best_claimer = NULL;
	boolean fresh;

	if (bot->vehicle_index != NONE || now < bot->vehicle_check_tick || now < bot->vehicle_rest_until_tick)
		return;
	bot->vehicle_check_tick = now + TICKS_PER_SECOND;
	/* (the setting looked at once a second: a configuration lookup) */
	if (!bots_vehicles_enabled())
		return;
	if (bot->goal_kind == _bots_goal_none || bot->goal_kind == _bots_goal_item || bot->held_objective ||
		(tick->fighting && tick->target_distance < 15.0f))
	{
		return;
	}
	goal_distance = bots_distance2d(&bot->goal_point, &tick->position);
	if (goal_distance < BOTS_VEHICLE_GOAL_DISTANCE)
		return;
	fresh = now - bot->spawn_tick < TICKS_PER_SECOND * BOTS_VEHICLE_FRESH_SECONDS;
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct unit_datum *vehicle = unit_get(iterator.index);
		struct unit_definition *definition;
		real distance, speed, close, reach;
		short seat_index, kind;
		boolean friendly_driver, bot_driver;

		if (TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ||
			TEST_FLAG(vehicle->unit.flags, _unit_not_enterable_by_player_bit) ||
			(iterator.index == bot->vehicle_avoid_index && now < bot->vehicle_avoid_until_tick) ||
			!bots_vehicle_drivable(iterator.index))
		{
			continue;
		}
		kind = bots_vehicle_kind(iterator.index);
		close = fresh ? MAX(bots_vehicle_worths[kind].close + 10.0f, 15.0f) : bots_vehicle_worths[kind].close;
		reach = MIN(bots_vehicle_worths[kind].reach, MAX(close, goal_distance * bots_vehicle_worths[kind].share));
		distance = bots_distance2d(&vehicle->object.position, &tick->position);
		if (distance > reach || (distance > close &&
			bots_distance2d(&vehicle->object.position, &bot->goal_point) > goal_distance + bots_vehicle_worths[kind].slack))
		{
			continue;
		}
		/* (on its side, no one in it: righted first, as a player does) */
		if (vehicle->object.up.k < 0.5f)
		{
			if (!bots_vehicle_has_riders(iterator.index) && distance < 15.0f && distance + 10.0f < best_score &&
				!bots_seat_claimer(iterator.index, NONE, bot))
			{
				best_score = distance + 10.0f;
				best_index = iterator.index;
				best_seat = NONE;
				best_claimer = NULL;
				best_distance = distance;
			}
			continue;
		}
		speed = magnitude3d(&vehicle->object.translational_velocity);
		friendly_driver = bots_vehicle_friendly_driver(iterator.index, tick->player, &bot_driver);
		definition = unit_definition_get(vehicle->definition_index);
		for (seat_index = 0; seat_index < definition->unit.seats.count; seat_index++)
		{
			short seat_kind = bots_seat_kind(iterator.index, seat_index);
			real score = distance;
			struct bots_bot *claimer;

			if (seat_kind == _bots_seat_driver)
			{
				if (vehicle->unit.driver_object_index != NONE || speed > 0.03f)
					continue;
				score -= bots_vehicle_worths[kind].bonus;
			}
			else
			{
				if (!friendly_driver || (!bot_driver && speed > 0.03f) ||
					bots_vehicle_definition(iterator.index)->vehicle_type != _bots_vehicle_type_human_jeep)
				{
					continue;
				}
				score += seat_kind == _bots_seat_gunner ? -3.5f : 3.0f;
			}
			if (score >= best_score || bots_seat_occupant(iterator.index, seat_index) != NONE)
				continue;
			claimer = bots_seat_claimer(iterator.index, seat_index, bot);
			if ((claimer && !bots_seat_take_over(bot, claimer, iterator.index, distance)) ||
				!unit_has_animation_to_enter_seat(unit_index, iterator.index, seat_index) ||
				!unit_can_enter_seat(unit_index, iterator.index, seat_index, NULL))
			{
				continue;
			}
			best_score = score;
			best_index = iterator.index;
			best_seat = seat_index;
			best_claimer = claimer;
			best_distance = distance;
		}
	}
	if (best_index == NONE)
		return;
	if (best_claimer)
	{
		if (best_claimer->goal_kind == _bots_goal_vehicle)
		{
			best_claimer->goal_serial++;
			best_claimer->path.valid = FALSE;
		}
		bots_vehicle_forget(best_claimer);
		best_claimer->vehicle_check_tick = now + TICKS_PER_SECOND * 3;
		bots_globals.vehicle_take_overs++;
	}
	bot->vehicle_index = best_index;
	bot->vehicle_seat_index = best_seat;
	bot->vehicle_flip = best_seat == NONE;
	bot->vehicle_tick = now;
	/* (the walk there: some 2 units a second, and a while; 25 s at least) */
	bot->vehicle_deadline_tick = now + TICKS_PER_SECOND * (long)MAX(25.0f, best_distance / 1.6f + 8.0f);
	bot->vehicle_presses = 0;
	bot->vehicle_press_tick = 0;
	bots_vehicle_goal(bot, unit_index);
}

/* port: on foot, a turret to man (bots_turret_seat), looked for once a
second while an enemy it saw in the last five seconds is about, nor does it
carry the flag or the ball, go for a powerup or a vehicle: the nearest
turret (of the map's: bots_powerups_find) no further than BOTS_TURRET_REACH,
the enemy no further from it than BOTS_TURRET_RANGE, upright, alive and
enterable, its seat empty, no other bot's, and one it can enter as a player
does (unit_can_enter_seat: no enemy on it; an animation for the seat); not
one it got off a while ago. Then it goes for the seat as for any vehicle's
(bots_vehicle_approach) */
static void bots_turret_consider(struct bots_bot *bot, struct bots_tick *tick)
{
	long now = tick->now;
	long unit_index = tick->player->unit_index;
	long index, best_index = NONE;
	short best_seat = NONE;
	real best_distance = BOTS_TURRET_REACH;

	if (bots_globals.turret_count <= 0 || bot->vehicle_index != NONE || now < bot->turret_check_tick ||
		now < bot->vehicle_rest_until_tick || bot->held_objective || bot->target_player_index == NONE ||
		now - bot->target_seen_tick > TICKS_PER_SECOND * 5 ||
		(bot->goal_kind == _bots_goal_item && bot->item_powerup != _bots_powerup_none))
	{
		return;
	}
	bot->turret_check_tick = now + TICKS_PER_SECOND;
	if (!bots_vehicles_enabled())
		return;
	for (index = 0; index < bots_globals.turret_count; index++)
	{
		long vehicle_index = bots_globals.turret_indices[index];
		struct unit_datum *vehicle = (struct unit_datum *)object_try_and_get_and_verify_type(vehicle_index, _object_mask_vehicle);
		real distance;
		short seat_index;

		if (!vehicle || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ||
			TEST_FLAG(vehicle->unit.flags, _unit_not_enterable_by_player_bit) || vehicle->object.up.k < 0.5f ||
			(vehicle_index == bot->vehicle_avoid_index && now < bot->vehicle_avoid_until_tick))
		{
			continue;
		}
		distance = bots_distance3d(&vehicle->object.position, &tick->position);
		if (distance >= best_distance ||
			bots_distance3d(&vehicle->object.position, &bot->target_last_position) > BOTS_TURRET_RANGE)
		{
			continue;
		}
		seat_index = bots_turret_seat(vehicle_index);
		if (seat_index == NONE || bots_seat_occupant(vehicle_index, seat_index) != NONE ||
			bots_seat_claimer(vehicle_index, seat_index, bot) ||
			!unit_has_animation_to_enter_seat(unit_index, vehicle_index, seat_index) ||
			!unit_can_enter_seat(unit_index, vehicle_index, seat_index, NULL))
		{
			continue;
		}
		best_distance = distance;
		best_index = vehicle_index;
		best_seat = seat_index;
	}
	if (best_index == NONE)
		return;
	bot->vehicle_index = best_index;
	bot->vehicle_seat_index = best_seat;
	bot->vehicle_flip = FALSE;
	bot->vehicle_turret = TRUE;
	bot->vehicle_tick = now;
	bot->vehicle_deadline_tick = now + TICKS_PER_SECOND * 25;
	bot->vehicle_presses = 0;
	bot->vehicle_press_tick = 0;
	bots_vehicle_goal(bot, unit_index);
}

/* on foot, near the vehicle it goes for: the last bit straight to the
seat's entrance (the path brings it near), round the vehicle when its middle
is in the way (action_vehicle_find_destination's way: to beside it), then
still, and in (below); a vehicle on its side, gone up to and righted with
the action button, as a player rights one (a press, not held). Given up on
after a while on the way, or trying to no end */
static void bots_vehicle_approach(struct bots_bot *bot, struct bots_tick *tick)
{
	struct object_datum *vehicle;
	real_point3d const *target = &bot->vehicle_entrance;
	real_point3d const *position = &tick->position;
	real distance, reach;
	long now = tick->now;

	if (bot->vehicle_index == NONE || bot->goal_kind != _bots_goal_vehicle)
		return;
	vehicle = object_try_and_get_and_verify_type(bot->vehicle_index, _object_mask_vehicle);
	if (!vehicle)
	{
		bots_vehicle_forget(bot);
		return;
	}
	if (now > bot->vehicle_deadline_tick || bot->vehicle_presses > 10)
	{
		/* (port: halo.log's; every bot leaving it be a while too was
		tried, Oct 2026: on Sidewinder, whose Scorpions stick where some
		bots get in and others do not, the bots rode them less) */
		bots_globals.vehicle_giveups++;
		platform_log("bots: bot %ld gave up on %s (%s), %.1f away", (long)(bot - bots_globals.bots) + 1,
			tag_get_name(vehicle->definition_index), bot->vehicle_presses > 10 ? "no way in" : "too long on the way",
			bots_distance2d(&bot->vehicle_entrance, &tick->position));
		bots_vehicle_avoid(bot, bot->vehicle_index, TICKS_PER_SECOND * 30);
		bots_vehicle_forget(bot);
		return;
	}
	distance = bots_distance2d(target, position);
	/* (port: a turret's entrance may lie against its mount, never quite
	reached: nearer than a seat's reach, unit_find_nearby_seat's metre) */
	reach = bot->vehicle_flip ? vehicle->object.bounding_sphere_radius * 0.8f : bot->vehicle_turret ? 0.85f : 0.45f;
	if (distance > 4.0f || fabs(target->z - position->z) > 2.5f)
		return;
	if (distance > reach)
	{
		real_point3d aim = *target;
		real to_target_i = target->x - position->x, to_target_j = target->y - position->y;
		real to_vehicle_i = vehicle->object.position.x - position->x, to_vehicle_j = vehicle->object.position.y - position->y;
		real length_squared = to_target_i * to_target_i + to_target_j * to_target_j;
		real t = (to_target_i * to_vehicle_i + to_target_j * to_vehicle_j) / MAX(length_squared, 0.0001f);
		/* (how wide the vehicle is this way: its middle to the entrance) */
		real radius = bots_distance2d(&vehicle->object.position, target);

		if (!bot->vehicle_flip && distance > 1.2f && t > 0.0f && t < 1.2f &&
			fabs(to_target_i * to_vehicle_j - to_target_j * to_vehicle_i) / (real)sqrt(MAX(length_squared, 0.0001f)) < radius * 0.9f)
		{
			real side_i = -to_target_j, side_j = to_target_i;
			real side_length = (real)sqrt(side_i * side_i + side_j * side_j);

			if (side_i * to_vehicle_i + side_j * to_vehicle_j > 0.0f)
			{
				side_i = -side_i;
				side_j = -side_j;
			}
			aim.x = vehicle->object.position.x + side_i / MAX(side_length, 0.0001f) * radius * 1.1f;
			aim.y = vehicle->object.position.y + side_j / MAX(side_length, 0.0001f) * radius * 1.1f;
		}
		tick->move_direction = (real)atan2(aim.y - position->y, aim.x - position->x);
		tick->moving = TRUE;
		tick->throttle_scale = distance < 1.5f ? 0.45f : 1.0f;
		bot->yaw += PIN(bots_angle_difference(tick->move_direction, bot->yaw), -12.0f * BOTS_DEGREES, 12.0f * BOTS_DEGREES);
		return;
	}
	tick->moving = FALSE;
	bot->yaw += PIN(bots_angle_difference((real)atan2(vehicle->object.position.y - position->y,
		vehicle->object.position.x - position->x), bot->yaw), -12.0f * BOTS_DEGREES, 12.0f * BOTS_DEGREES);
	/* (still, and the vehicle not turning: player_examine_nearby_vehicle's
	rule) */
	if (magnitude_squared3d(&tick->object->object.translational_velocity) >= 0.01f ||
		magnitude_squared3d(&vehicle->object.angular_velocity) >= 0.01f || now - bot->vehicle_press_tick <= 10)
	{
		return;
	}
	bot->vehicle_press_tick = now;
	bot->vehicle_presses++;
	if (bot->vehicle_flip)
	{
		tick->flags |= FLAG(_unit_control_action_bit);
		return;
	}
	/* its own seat: the action button takes the seat nearest the player
	(unit_find_nearby_seat), and a Warthog's three are close together - a bot
	going for the driver's seat got the passenger's, and sat waiting for a
	driver - so the bot gets into the seat it chose, as the campaign's AI
	does (action_vehicle_perform: unit_enter_seat), where a player could:
	within a metre of the seat's entrance or the seat (unit_find_nearby_seat's
	reach), the vehicle upright and enterable, a seat that wants a driver
	only with one, and none in it nor an enemy in the vehicle
	(unit_can_enter_seat, in unit_enter_seat) */
	{
		long unit_index = tick->player->unit_index;
		struct unit_datum *vehicle_unit = unit_get(bot->vehicle_index);
		struct unit_definition *definition = unit_definition_get(vehicle_unit->definition_index);
		struct unit_seat *seat;
		real_point3d entrance, seat_point;

		if (bot->vehicle_seat_index < 0 || bot->vehicle_seat_index >= definition->unit.seats.count)
			return;
		seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, bot->vehicle_seat_index, struct unit_seat);
		if (vehicle->object.up.k < 0.5f || TEST_FLAG(vehicle_unit->unit.flags, _unit_not_enterable_by_player_bit) ||
			(TEST_FLAG(seat->flags, 9) && vehicle_unit->unit.driver_object_index == NONE) ||
			!unit_get_seat_entrance_point(unit_index, bot->vehicle_index, bot->vehicle_seat_index, &entrance, &seat_point, NULL) ||
			MIN(bots_distance3d(&entrance, &tick->object->object.bounding_sphere_center),
				bots_distance3d(&seat_point, &tick->object->object.bounding_sphere_center)) >= 1.0f)
		{
			return;
		}
		unit_enter_seat(unit_index, bot->vehicle_index, bot->vehicle_seat_index);
	}
}

/* a teammate bot on its way to a seat of the vehicle, near (the driver
waits for it, a while) */
static boolean bots_vehicle_boarding(struct bots_bot *bot, long vehicle_index)
{
	struct object_datum *vehicle = object_get(vehicle_index);
	long index;

	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *other = &bots_globals.bots[index];
		struct object_datum *unit;

		if (other == bot || other->player_index == NONE || other->unit_index == NONE ||
			other->vehicle_index != vehicle_index || other->vehicle_flip ||
			game_time_get() - other->vehicle_tick > TICKS_PER_SECOND * 12)
		{
			continue;
		}
		unit = object_try_and_get(other->unit_index);
		if (unit && bots_distance2d(&unit->object.position, &vehicle->object.position) < 25.0f)
			return TRUE;
	}
	return FALSE;
}

/* driving: the direction to its goal along its path (the searches as on
foot, from where the vehicle is, as wide as it is: bots_search_ask), a step
passed once the vehicle is near it; FALSE with nowhere to go (there) */
static boolean bots_drive_path(struct bots_bot *bot, struct bots_tick *tick, real *direction)
{
	long now = tick->now;
	real_point3d const *position = &tick->position;

	if (bot->goal_kind == _bots_goal_none)
		return FALSE;
	if ((!bot->path.valid || now - bot->path_tick > TICKS_PER_SECOND * 4) && now - bot->path_tick > TICKS_PER_SECOND / 3)
		bots_search_ask(bot, tick->player->unit_index);
	if (bot->path.valid)
	{
		while (bot->path.step_index < bot->path.step_count &&
			bots_distance2d(&bot->path.steps[(long)bot->path.step_index].point, position) < BOTS_DRIVE_STEP_REACHED)
		{
			bot->path.step_index++;
		}
		if (bot->path.step_index < bot->path.step_count)
		{
			struct path_step *step = &bot->path.steps[(long)bot->path.step_index];

			*direction = (real)atan2(step->point.y - position->y, step->point.x - position->x);
			return TRUE;
		}
		bot->path.valid = FALSE;
		/* (the whole way driven: there; a roaming place, another) */
		if (bot->path.steps_finish_path && !bot->path_short && !bot->via_teleporter)
		{
			if (bot->goal_kind != _bots_goal_objective)
				bot->goal_kind = _bots_goal_none;
			return FALSE;
		}
		bots_search_ask(bot, tick->player->unit_index);
	}
	/* (no path yet, or none found: straight there) */
	if (bots_distance2d(&bot->goal_point, position) < BOTS_DRIVE_STEP_REACHED * 1.5f)
	{
		if (bot->goal_kind != _bots_goal_objective)
			bot->goal_kind = _bots_goal_none;
		return FALSE;
	}
	*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
	return TRUE;
}

/* the driver's seat: steering as a player steers, by where it looks (the
vehicle turns to its driver's facing: vehicles.c), the throttle on (eased in
a sharp turn); along its path to its goal, or with an enemy in sight: a
driver that is also the gunner (a Ghost's, a Scorpion's) fights as on foot,
the vehicle on at an enemy far off (a Ghost strafes one near, a Scorpion
stands), a Warthog's driver goes round the enemy for its gunner, or with no
gunner at a lone one (to run it down). Stuck (not moving for a second while
it drives), it backs off a moment every other second, then goes on; it
waits as it gets in, and while a teammate bot comes to a seat */
static void bots_bot_drive(struct bots_bot *bot, struct bots_tick *tick, long vehicle_index, boolean gunner)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	long now = tick->now;
	real direction = bot->yaw;
	boolean fighting = bot->target_player_index != NONE && now - bot->target_seen_tick <= TICKS_PER_SECOND / 2;

	if (now - bot->ride_stuck_tick >= TICKS_PER_SECOND)
	{
		real moved = bots_distance2d(&vehicle->object.position, &bot->ride_stuck_position);

		if (bot->ride_driving && moved < 0.6f)
		{
			/* (what it drives into: scenery or a machine - Sidewinder's
			blast door, which the walkable surfaces go through - is learnt to
			be in the way, its surfaces closed to every bot's searches, as a
			bot on foot learns it: bots_paths_learn) */
			struct collision_result collision;
			unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
				FLAG(_collision_test_objects_machines_bit);
			/* (port: past its front: a Scorpion's is 2.2 units from its
			middle, whose 2.5 found nothing before it) */
			real length = MAX(2.5f, vehicle->object.bounding_sphere_radius + 0.6f);
			real scale = bot->ride_reversing ? -length : length;
			real_vector3d vector = { vehicle->object.forward.i * scale, vehicle->object.forward.j * scale,
				vehicle->object.forward.k * scale };
			boolean hit = collision_test_vector(flags, &vehicle->object.bounding_sphere_center, &vector, vehicle_index, &collision);

			/* (port: and low, at its wheels or treads: a ledge below its
			middle) */
			if (!hit)
			{
				real_point3d low = vehicle->object.position;

				low.z += 0.25f;
				hit = collision_test_vector(flags, &low, &vector, vehicle_index, &collision);
			}

			if (hit && collision.type == _collision_result_object &&
				object_definition_get(object_get(collision.object_index)->definition_index)->object.collision_model.index != NONE &&
				!bots_paths_learned(collision.object_index) && !bots_object_marks_place(object_get(collision.object_index)))
			{
				bots_paths_close_object(collision.object_index, "in a vehicle's way:");
			}
			/* (the level before it, a second time: the way closed to vehicles) */
			else if (hit && collision.type == _collision_result_structure && !bot->ride_reversing && bot->ride_stuck_count >= 1)
			{
				real_point3d before = collision.point;

				before.x += vehicle->object.forward.i * 0.5f;
				before.y += vehicle->object.forward.j * 0.5f;
				bots_paths_close_for_vehicles(&before, &vehicle->object.position, &bot->goal_point,
					vehicle->object.bounding_sphere_radius);
			}
			/* (stuck a second time: the scenery and machines about it learnt
			too, as a bot on foot learns them when its path finds no way) */
			if (bot->ride_stuck_count >= 1)
				bots_paths_learn(&vehicle->object.position, &bot->goal_point);
			if (bots_trace_enabled())
			{
				platform_log("bots: trace bot %ld stuck %d driving at (%.1f %.1f %.1f) heading %.0f yaw %.0f: %s %s %s",
					(long)(bot - bots_globals.bots) + 1, (int)bot->ride_stuck_count + 1, vehicle->object.position.x,
					vehicle->object.position.y, vehicle->object.position.z,
					atan2(vehicle->object.forward.j, vehicle->object.forward.i) / BOTS_DEGREES, bot->yaw / BOTS_DEGREES,
					bot->ride_reversing ? "behind" : "ahead", !hit ? "clear" : collision.type == _collision_result_structure ? "the level" : "",
					hit && collision.type == _collision_result_object ? tag_get_name(object_get(collision.object_index)->definition_index) : "");
			}
			bot->ride_stuck_count++;
			if (!(bot->ride_stuck_count % 2))
			{
				/* (backing off, the wheels turned the other way: the nose
				comes round to where it was going, a three-point turn; the
				driver steers by its facing, vehicles.c) */
				real heading = (real)atan2(vehicle->object.forward.j, vehicle->object.forward.i);
				real off = bots_angle_difference(bot->ride_direction, heading);

				bot->ride_reverse_until_tick = now + TICKS_PER_SECOND * 3 / 2;
				bot->ride_reverse_yaw = heading - PIN(off, -BOTS_PI / 3.0f, BOTS_PI / 3.0f);
				bot->path.valid = FALSE;
			}
		}
		else if (moved > 2.0f)
			bot->ride_stuck_count = 0;
		bot->ride_stuck_position = vehicle->object.position;
		bot->ride_stuck_tick = now;
	}
	bot->ride_driving = FALSE;
	bot->ride_reversing = FALSE;
	if (now - bot->ride_tick < TICKS_PER_SECOND || bots_vehicle_boarding(bot, vehicle_index))
	{
		if (fighting && gunner)
		{
			bots_bot_fight(bot, tick);
			tick->moving = FALSE;
		}
		return;
	}
	if (now < bot->ride_reverse_until_tick)
	{
		bot->yaw += PIN(bots_angle_difference(bot->ride_reverse_yaw, bot->yaw), -15.0f * BOTS_DEGREES, 15.0f * BOTS_DEGREES);
		tick->moving = TRUE;
		tick->move_direction = bot->yaw + BOTS_PI;
		bot->ride_driving = TRUE;
		bot->ride_reversing = TRUE;
		return;
	}
	/* (the game's objective first, as on foot: a Ghost goes on to it as it
	fights, strafing; a Scorpion and a Warthog's driver drive on to it, the
	Scorpion firing at what comes before its gun, below) */
	if (fighting && gunner && bot->goal_kind == _bots_goal_objective)
	{
		short type = bots_vehicle_definition(vehicle_index)->vehicle_type;

		if (bots_drive_path(bot, tick, &direction))
		{
			if (type == _bots_vehicle_type_alien_scout)
			{
				bots_bot_fight(bot, tick);
				tick->moving = TRUE;
				tick->move_direction = direction;
				tick->throttle_scale = 1.0f;
				bot->ride_driving = TRUE;
				return;
			}
			fighting = FALSE;
		}
	}
	else if (fighting && bot->goal_kind == _bots_goal_objective)
		fighting = FALSE;
	if (fighting)
	{
		struct object_datum *target = object_get(player_get(bot->target_player_index)->unit_index);
		real to_target = (real)atan2(target->object.position.y - tick->position.y, target->object.position.x - tick->position.x);
		real distance = bots_distance2d(&target->object.position, &tick->position);

		if (gunner)
		{
			bots_bot_fight(bot, tick);
			if (bots_vehicle_definition(vehicle_index)->vehicle_type == _bots_vehicle_type_human_tank)
			{
				tick->moving = distance > 20.0f;
				tick->move_direction = bot->yaw;
			}
			else if (distance > 12.0f)
			{
				tick->moving = TRUE;
				tick->move_direction = bot->yaw;
			}
			bot->ride_driving = tick->moving;
			return;
		}
		if (now >= bot->strafe_until_tick)
		{
			bot->strafe_sign = bots_random_real(bot) < 0.5f ? -1.0f : 1.0f;
			bot->strafe_until_tick = now + TICKS_PER_SECOND * 3 + (long)(bots_random(bot) % (TICKS_PER_SECOND * 3));
		}
		direction = vehicle->unit.gunner_object_index != NONE && distance < 18.0f ?
			to_target + bot->strafe_sign * BOTS_PI * (distance < 10.0f ? 0.6f : 0.4f) : to_target;
	}
	else if (!bots_drive_path(bot, tick, &direction))
	{
		tick->moving = FALSE;
		return;
	}
	{
		real heading = (real)atan2(vehicle->object.forward.j, vehicle->object.forward.i);

		bot->ride_direction = direction;
		bot->yaw += PIN(bots_angle_difference(direction, bot->yaw), -15.0f * BOTS_DEGREES, 15.0f * BOTS_DEGREES);
		bot->pitch += PIN(-bot->pitch, -0.05f, 0.05f);
		tick->moving = TRUE;
		tick->move_direction = bot->yaw;
		tick->throttle_scale = fabs(bots_angle_difference(direction, heading)) > BOTS_PI * 0.5f ? 0.6f : 1.0f;
		bot->ride_driving = TRUE;
	}
	/* (the gun the driver's, an enemy in sight before it as it drives on:
	fired) */
	if (gunner && bot->target_player_index != NONE && now - bot->target_seen_tick <= TICKS_PER_SECOND / 2 &&
		now - bot->target_first_seen_tick >= (long)(tick->skill->reaction_seconds * TICKS_PER_SECOND))
	{
		struct object_datum *target = object_get(player_get(bot->target_player_index)->unit_index);

		if (fabs(bots_angle_difference((real)atan2(target->object.position.y - tick->position.y,
			target->object.position.x - tick->position.x), bot->yaw)) < 8.0f * BOTS_DEGREES)
		{
			tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		}
	}
}

/* whether the goal wants the bot on foot: the game's own (a flag to take or
bring home, a hill, the ball), but a race's checkpoints, driven through */
static boolean bots_goal_on_foot(struct bots_bot *bot)
{
	return bot->goal_kind == _bots_goal_objective && game_engine_get_variant()->game_engine_index != game_engine_race;
}

/* port: on a turret: fighting from it the enemy it sees (the turret turns
to where its gunner looks: unit aiming), looking about round the way it
faces otherwise (towards the enemy last seen); off (the reason returned,
_bots_exit_none to stay) when no enemy has been before it for six seconds
(since it got on, at the least), when the gun has not turned to
the one it fights for two (out of its arc: the turret's aim more than 25
degrees off it), or when it is being killed on it (hurt in the last second,
its shields down and its body below its skill's mark for a health pack in a
fight, a little above) */
static short bots_bot_turret(struct bots_bot *bot, struct bots_tick *tick, long vehicle_index)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	long now = tick->now;

	if (bot->target_player_index != NONE && now - bot->target_seen_tick <= TICKS_PER_SECOND / 2)
	{
		struct object_datum *target = object_get(player_get(bot->target_player_index)->unit_index);
		real_vector3d to_target;
		real length, aim;

		bots_bot_fight(bot, tick);
		tick->moving = FALSE;
		/* (a gun whose trigger charges, fired as it says: held until charged,
		then let go - a press a shot fired nothing from Extinction's gun
		tower) */
		if (tick->weapon_index != NONE && bots_weapon_kind(weapon_get(tick->weapon_index)->definition_index)->charge_ticks > 0)
		{
			short charge_ticks = bots_weapon_kind(weapon_get(tick->weapon_index)->definition_index)->charge_ticks;

			if (!bot->ride_charge_tick && TEST_FLAG(tick->flags, _unit_control_weapon_primary_trigger_bit))
				bot->ride_charge_tick = now;
			if (bot->ride_charge_tick && now - bot->ride_charge_tick <= charge_ticks + 3)
				tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
			else
			{
				tick->flags &= ~FLAG(_unit_control_weapon_primary_trigger_bit);
				bot->ride_charge_tick = 0;
			}
		}
		if (TEST_FLAG(tick->flags, _unit_control_weapon_primary_trigger_bit))
			bots_globals.turret_fire_ticks++;
		vector_from_points3d(&vehicle->object.bounding_sphere_center, &target->object.bounding_sphere_center, &to_target);
		length = magnitude3d(&to_target);
		aim = length > 0.01f ? dot_product3d(&to_target, &vehicle->unit.aiming_vector) / length : 1.0f;
		if (aim < (real)cos(25.0f * BOTS_DEGREES))
		{
			if (!bot->ride_turret_off_tick)
				bot->ride_turret_off_tick = now;
			else if (now - bot->ride_turret_off_tick > TICKS_PER_SECOND * 2)
				return _bots_exit_no_enemy;
		}
		else
			bot->ride_turret_off_tick = 0;
	}
	else
	{
		/* (towards where the enemy was last seen, else about) */
		real heading = (real)atan2(vehicle->object.forward.j, vehicle->object.forward.i);
		real look = bot->target_player_index != NONE ?
			(real)atan2(bot->target_last_position.y - tick->position.y, bot->target_last_position.x - tick->position.x) :
			heading + (real)sin((real)(now + (long)(bot - bots_globals.bots) * 37) / 40.0f) * 1.0f;

		bot->yaw += PIN(bots_angle_difference(look, bot->yaw), -6.0f * BOTS_DEGREES, 6.0f * BOTS_DEGREES);
		bot->pitch += PIN(-bot->pitch, -0.05f, 0.05f);
		bot->ride_turret_off_tick = 0;
	}
	if (now - MAX(bot->ride_tick, bot->target_seen_tick) > TICKS_PER_SECOND * 6)
		return _bots_exit_no_enemy;
	if (now - bot->hurt_tick < TICKS_PER_SECOND && tick->object->object.shield_vitality <= 0.05f &&
		tick->object->object.body_vitality < tick->skill->heal_below_fighting + 0.1f)
	{
		return _bots_exit_hurt;
	}
	return _bots_exit_none;
}

/* riding: just in, its seat counted; then out (the action button pressed,
as a player gets out: bipeds.c) when the vehicle lies on its side, sticks
fast, is left without a driver a while (but by a gunner still fighting), or
near a goal that wants it on foot; else the driver drives (bots_bot_drive),
and a gunner (with the vehicle's gun) or a passenger (with its own) fights
what it sees, looking about otherwise */
static void bots_bot_ride(struct bots_bot *bot, struct bots_tick *tick, long vehicle_index)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	long unit_index = tick->player->unit_index;
	long now = tick->now;
	boolean driver = vehicle->unit.driver_object_index == unit_index;
	boolean gunner = vehicle->unit.gunner_object_index == unit_index;
	short exit = _bots_exit_none;

	if (bot->ride_vehicle_index != vehicle_index)
	{
		bot->ride_vehicle_index = vehicle_index;
		bot->ride_seat = bots_seat_kind(vehicle_index, tick->unit->unit.parent_seat_index);
		bot->ride_tick = now;
		bot->ride_last_position = vehicle->object.position;
		bot->ride_stuck_position = vehicle->object.position;
		bot->ride_stuck_tick = now;
		bot->ride_stuck_count = 0;
		bot->ride_driving = FALSE;
		bot->ride_reverse_until_tick = 0;
		bot->ride_driverless_tick = 0;
		bot->ride_flipped_tick = 0;
		bot->ride_exit = _bots_exit_none;
		bot->ride_turret = bots_turret_seat(vehicle_index) == tick->unit->unit.parent_seat_index;
		bot->ride_turret_off_tick = 0;
		bot->ride_charge_tick = 0;
		if (bot->ride_turret)
			bots_globals.turret_entries++;
		/* (debug: HALO_BOT_TRACE, a turret's gun) */
		if (bot->ride_turret && bots_trace_enabled() && bots_current_weapon(vehicle) != NONE)
		{
			long gun_index = bots_current_weapon(vehicle);
			struct weapon_definition *gun = weapon_definition_get(weapon_get(gun_index)->definition_index);
			struct weapon_trigger_definition *trigger = gun->weapon.triggers.count > 0 ?
				TAG_BLOCK_GET_ELEMENT(&gun->weapon.triggers, 0, struct weapon_trigger_definition) : NULL;

			platform_log("bots: trace turret gun %s: magazines %d triggers %d flags %lx rate %.2f charging %.2f gunner %s",
				tag_get_name(weapon_get(gun_index)->definition_index), (int)gun->weapon.magazines.count,
				(int)gun->weapon.triggers.count, trigger ? trigger->flags : 0, trigger ? trigger->initial_rate_of_fire : 0.0f,
				trigger ? trigger->charging_time : 0.0f, vehicle->unit.gunner_object_index == unit_index ? "it" : "not it");
		}
		platform_log("bots: bot %ld in %s seat %d (%s%s), went for seat %d%s, at tick %ld", (long)(bot - bots_globals.bots) + 1,
			tag_get_name(vehicle->definition_index), (int)tick->unit->unit.parent_seat_index,
			bot->ride_seat == _bots_seat_driver ? "driver" : bot->ride_seat == _bots_seat_gunner ? "gunner" : "passenger",
			bot->ride_turret ? ", a turret" : "",
			bot->vehicle_index == vehicle_index ? (int)bot->vehicle_seat_index : NONE,
			vehicle->unit.driver_object_index != NONE && vehicle->unit.driver_object_index != unit_index ?
				(bots_player_is_bot(unit_get(vehicle->unit.driver_object_index)->unit.player_index) ? ", a bot driving" :
					", a player driving") : "", now);
		if (bots_vehicle_drivable(vehicle_index))
			bots_globals.vehicle_entries_by_kind[bots_vehicle_kind(vehicle_index)]++;
		if (!bots_globals.first_entry_tick)
			bots_globals.first_entry_tick = MAX(now, 1);
		bots_vehicle_forget(bot);
		bot->goal_kind = _bots_goal_none;
		bot->goal_serial++;
		bot->path.valid = FALSE;
		bots_globals.vehicle_entries++;
		bots_globals.vehicle_entries_by_seat[bot->ride_seat]++;
	}
	if (driver)
	{
		real moved = bots_distance2d(&vehicle->object.position, &bot->ride_last_position);

		if (moved < 3.0f)
			bots_globals.driven_distance += moved;
	}
	bot->ride_last_position = vehicle->object.position;
	/* (no items while riding) */
	bot->item_check_tick = now + TICKS_PER_SECOND;
	if (tick->unit->unit.animation.state == _unit_state_entering_seat)
		return;
	if (gunner)
	{
		long weapon_index = bots_current_weapon(vehicle);

		if (weapon_index != NONE)
			tick->weapon_index = weapon_index;
	}

	if (TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
		exit = _bots_exit_other;
	if (vehicle->object.up.k < 0.3f)
	{
		if (!bot->ride_flipped_tick)
			bot->ride_flipped_tick = now;
		else if (now - bot->ride_flipped_tick > TICKS_PER_SECOND)
			exit = _bots_exit_flipped;
	}
	else
		bot->ride_flipped_tick = 0;
	if (!driver && !bot->ride_turret && vehicle->unit.driver_object_index == NONE)
	{
		if (!bot->ride_driverless_tick)
			bot->ride_driverless_tick = now;
		else if (now - bot->ride_driverless_tick > TICKS_PER_SECOND * 3 && now - bot->target_seen_tick > TICKS_PER_SECOND * 2)
			exit = _bots_exit_driverless;
	}
	else
		bot->ride_driverless_tick = 0;
	/* (the goal looked at again every fourth tick, the bots in turn) */
	if (!((now + (long)(bot - bots_globals.bots)) % 4) || bot->goal_kind == _bots_goal_none)
		bots_choose_goal(bot, bot->player_index, &tick->position, FALSE);
	bots_search_take(bot, TRUE);
	/* (stuck fast; or stuck twice near a goal that wants it on foot: a
	base's doors, which no vehicle goes through) */
	if (driver && (bot->ride_stuck_count >= 5 || (bot->ride_stuck_count >= 2 && bots_goal_on_foot(bot) &&
		bots_distance2d(&bot->goal_point, &tick->position) < 2.5f * BOTS_VEHICLE_GOAL_EXIT)))
	{
		exit = _bots_exit_stuck;
	}
	if (exit == _bots_exit_none && bots_goal_on_foot(bot) && !bot->ride_turret &&
		bots_distance2d(&bot->goal_point, &tick->position) < BOTS_VEHICLE_GOAL_EXIT)
	{
		exit = _bots_exit_goal;
	}
	/* port: a turret's gunner: fighting from it, and off as it says */
	if (exit == _bots_exit_none && bot->ride_turret)
		exit = bots_bot_turret(bot, tick, vehicle_index);
	if (exit != _bots_exit_none)
	{
		if (bot->ride_exit == _bots_exit_none)
			bot->ride_exit = exit;
		if (now - bot->vehicle_press_tick > TICKS_PER_SECOND / 2 && !unit_is_busy(unit_index))
		{
			tick->flags |= FLAG(_unit_control_action_bit);
			bot->vehicle_press_tick = now;
		}
		return;
	}
	bot->ride_exit = _bots_exit_none;
	if (bot->ride_turret)
		;
	else if (driver)
		bots_bot_drive(bot, tick, vehicle_index, gunner);
	else if (bot->target_player_index != NONE && now - bot->target_seen_tick <= TICKS_PER_SECOND / 2)
	{
		bots_bot_fight(bot, tick);
		tick->moving = FALSE;
		if (TEST_FLAG(tick->flags, _unit_control_weapon_primary_trigger_bit))
			bots_globals.riding_fire_ticks++;
	}
	else
	{
		/* (looking about, round the way the vehicle goes) */
		real heading = (real)atan2(vehicle->object.forward.j, vehicle->object.forward.i);
		real look = heading + (real)sin((real)(now + (long)(bot - bots_globals.bots) * 37) / 45.0f) * 1.3f;

		bot->yaw += PIN(bots_angle_difference(look, bot->yaw), -6.0f * BOTS_DEGREES, 6.0f * BOTS_DEGREES);
		bot->pitch += PIN(-bot->pitch, -0.05f, 0.05f);
	}
	/* (in a seat, no grenade, blow or jump) */
	tick->flags &= ~(FLAG(_unit_control_throw_grenade_bit) | FLAG(_unit_control_use_equipment_bit) |
		FLAG(_unit_control_jump_bit));
}

/* on foot again after riding: why it got out counted; no vehicle a while
(longer at its goal), and the one that failed it (on its side, stuck) not
again for longer; a new goal and path from where it stands */
static void bots_vehicle_got_out(struct bots_bot *bot, struct bots_tick *tick)
{
	short exit = bot->ride_exit != _bots_exit_none ? bot->ride_exit : _bots_exit_other;

	bots_globals.vehicle_exits[exit]++;
	{
		struct object_datum *vehicle = object_try_and_get(bot->ride_vehicle_index);

		platform_log("bots: bot %ld out of %s (%s) after %.1f s%s", (long)(bot - bots_globals.bots) + 1,
			vehicle ? tag_get_name(vehicle->definition_index) : "?",
			exit == _bots_exit_flipped ? "on its side" : exit == _bots_exit_stuck ? "stuck" : exit == _bots_exit_goal ?
				"at its goal" : exit == _bots_exit_driverless ? "no driver" : exit == _bots_exit_no_enemy ? "no enemy before it" :
				exit == _bots_exit_hurt ? "hurt" : "other",
			(double)(tick->now - bot->ride_tick) / TICKS_PER_SECOND,
			!vehicle ? ", the vehicle gone" : TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ? ", the vehicle destroyed" :
				vehicle->object.up.k < 0.3f ? ", thrown out (the vehicle over)" : "");
	}
	bot->vehicle_rest_until_tick = tick->now + TICKS_PER_SECOND * (exit == _bots_exit_goal ? 20 : 6);
	if (exit == _bots_exit_flipped || exit == _bots_exit_stuck)
		bots_vehicle_avoid(bot, bot->ride_vehicle_index, TICKS_PER_SECOND * 20);
	/* port: (a turret got off: not again a while, and none at once) */
	if (bot->ride_turret)
	{
		bots_vehicle_avoid(bot, bot->ride_vehicle_index, TICKS_PER_SECOND * 15);
		bot->ride_turret = FALSE;
	}
	bot->ride_vehicle_index = NONE;
	bot->ride_exit = _bots_exit_none;
	bot->goal_kind = _bots_goal_none;
	bot->goal_serial++;
	bot->path.valid = FALSE;
	bot->path_tick = tick->now - TICKS_PER_SECOND;
	bot->last_position = tick->position;
	bot->stuck_position = tick->position;
	bot->stuck_tick = tick->now;
	bot->stuck_count = 0;
}

/* port: halo.log's line of the game's vehicles, at its first census: how
many of each (by tag; the gametype's vehicle set decides which the map
places: game_engine.c), and with HALO_BOT_TRACE where each stands */
static void bots_vehicle_list(void)
{
	struct object_iterator iterator;
	long definitions[8], counts[8], kinds = 0, total = 0, index;
	char line[400];
	int length = 0;

	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *vehicle = object_get(iterator.index);

		total++;
		for (index = 0; index < kinds && definitions[index] != vehicle->definition_index; index++)
			;
		if (index == kinds && kinds < (long)NUMBEROF(definitions))
		{
			definitions[kinds] = vehicle->definition_index;
			counts[kinds++] = 0;
		}
		if (index < kinds)
			counts[index]++;
		if (bots_trace_enabled())
			platform_log("bots: trace vehicle %s at (%.1f %.1f %.1f), bounding radius %.2f, pathfinding radius %.2f",
				tag_get_name(vehicle->definition_index), vehicle->object.position.x, vehicle->object.position.y,
				vehicle->object.position.z, vehicle->object.bounding_sphere_radius,
				bots_vehicle_definition(iterator.index)->ai_pathfinding_radius);
	}
	line[0] = 0;
	for (index = 0; index < kinds && length < (int)sizeof(line) - 80; index++)
		length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s %ld", index ? ", " : " (",
			tag_get_name(definitions[index]), counts[index]);
	platform_log("bots: the game's vehicles: %ld%s%s", total, line, kinds ? ")" : "");
}

/* halo.log's census of the bots' vehicles, every ten seconds: the bots
riding and in which seats, those going for one, the vehicles and those with
riders, the distance the bots drove them, the bots' getting in (by seat) and
out (by why), those killed riding and the vehicles righted */
static void bots_vehicle_census(void)
{
	long now = game_time_get();
	long riding[NUMBER_OF_BOTS_SEATS] = { 0, 0, 0 };
	long alive = 0, going = 0, vehicles = 0, occupied = 0, exits = 0, index;
	struct object_iterator iterator;

	if (bots_globals.last_census_tick && now - bots_globals.last_census_tick < TICKS_PER_SECOND * 10)
		return;
	if (!bots_globals.vehicles_listed)
		bots_vehicle_list();
	bots_globals.vehicles_listed = TRUE;
	bots_globals.last_census_tick = now;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *bot = &bots_globals.bots[index];
		struct player_datum *player = bot->player_index != NONE ? player_try_and_get(bot->player_index) : NULL;

		if (!player || player->unit_index == NONE)
			continue;
		alive++;
		if (bots_unit_vehicle(player->unit_index) != NONE && bot->ride_vehicle_index != NONE)
			riding[bot->ride_seat]++;
		else if (bot->vehicle_index != NONE)
			going++;
	}
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		vehicles++;
		if (bots_vehicle_has_riders(iterator.index))
			occupied++;
	}
	for (index = 0; index < NUMBER_OF_BOTS_EXITS; index++)
		exits += bots_globals.vehicle_exits[index];
	if (bots_trace_enabled())
	{
		char line[200];
		int length = 0;

		for (index = 0; index < BOTS_MAXIMUM && length < (int)sizeof(line) - 16; index++)
		{
			if (bots_globals.bots[index].player_index != NONE)
				length += snprintf(line + length, sizeof(line) - (size_t)length, " %ld:%ld", index + 1, bots_globals.bots[index].searches);
		}
		line[length] = 0;
		platform_log("bots: trace searches by bot%s", line);
	}
	platform_log("bots: vehicles at tick %ld: %ld of %ld bots riding (%ld driving, %ld gunning, %ld passengers), %ld going to one; "
		"vehicles %ld, %ld with riders; driven %.0f units; in %ld (driver %ld, gunner %ld, passenger %ld), out %ld (flipped %ld, "
		"stuck %ld, at the goal %ld, no driver %ld, other %ld, no enemy %ld, hurt %ld), righted %ld; kills riding %ld (driving %ld), "
		"%.1f s of gunners' and passengers' fire; %ld path searches driving; turrets %ld, manned %ld times, %.1f s of their fire, %ld kills from one; "
		"in by kind: tank %ld, jeep %ld, scout %ld; first in at tick %ld; %.0f s riding; %ld seats taken over, %ld given up on the way%s", now,
		riding[_bots_seat_driver] + riding[_bots_seat_gunner] + riding[_bots_seat_passenger], alive,
		riding[_bots_seat_driver], riding[_bots_seat_gunner], riding[_bots_seat_passenger], going, vehicles, occupied,
		(double)bots_globals.driven_distance, bots_globals.vehicle_entries,
		bots_globals.vehicle_entries_by_seat[_bots_seat_driver], bots_globals.vehicle_entries_by_seat[_bots_seat_gunner],
		bots_globals.vehicle_entries_by_seat[_bots_seat_passenger], exits, bots_globals.vehicle_exits[_bots_exit_flipped],
		bots_globals.vehicle_exits[_bots_exit_stuck], bots_globals.vehicle_exits[_bots_exit_goal],
		bots_globals.vehicle_exits[_bots_exit_driverless], bots_globals.vehicle_exits[_bots_exit_other],
		bots_globals.vehicle_exits[_bots_exit_no_enemy], bots_globals.vehicle_exits[_bots_exit_hurt],
		bots_globals.vehicle_flips, bots_globals.riding_kills[0] + bots_globals.riding_kills[1], bots_globals.riding_kills[0],
		(double)bots_globals.riding_fire_ticks / TICKS_PER_SECOND, bots_globals.driving_searches,
		bots_globals.turret_count, bots_globals.turret_entries, (double)bots_globals.turret_fire_ticks / TICKS_PER_SECOND,
		bots_globals.turret_kills, bots_globals.vehicle_entries_by_kind[0], bots_globals.vehicle_entries_by_kind[1],
		bots_globals.vehicle_entries_by_kind[2], bots_globals.first_entry_tick, (double)bots_globals.riding_ticks / TICKS_PER_SECOND,
		bots_globals.vehicle_take_overs, bots_globals.vehicle_giveups, bots_vehicles_enabled() ? "" : " (bots take no vehicles: HALO_BOT_VEHICLES=0)");
}

/* (debug) HALO_BOT_TRACE=<seconds>: each bot's state that often */
static void bots_bot_trace(struct bots_bot *bot, long bot_index, struct bots_tick *tick)
{
	static long trace = -1;
	long now = tick->now;

	if (trace < 0)
		trace = MAX(config_integer("debug.bot_trace"), 0) * TICKS_PER_SECOND;
	if (trace <= 0 || now % trace != bot_index)
		return;
	platform_log("bots: trace %ld bot %ld at (%.1f %.1f %.1f) goal %d (%.1f %.1f %.1f) s%ld path %s %d/%d%s "
		"target %ld seen %ld ago%s stuck %d yaw %.0f thr %.2f/%.2f flags %lx search %ld fail %ld",
		now, bot_index + 1, tick->position.x, tick->position.y, tick->position.z, (int)bot->goal_kind,
		bot->goal_point.x, bot->goal_point.y, bot->goal_point.z, bot->goal_surface_index,
		bot->path.valid ? "yes" : "no", (int)bot->path.step_index, (int)bot->path.step_count,
		bot->path.steps_finish_path ? " (all)" : "",
		bot->target_player_index == NONE ? -1L : (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(bot->target_player_index),
		now - bot->target_seen_tick, tick->fighting ? " fighting" : "", (int)bot->stuck_count,
		bot->yaw / BOTS_DEGREES, tick->action->throttle.i, tick->action->throttle.j, tick->action->control_flags,
		bot->search_slot, bot->path_failures);
	/* port: the vehicle it rides, or goes for */
	if (bot->ride_vehicle_index != NONE && object_try_and_get(bot->ride_vehicle_index))
	{
		platform_log("bots: trace riding %s seat %d for %ld ticks, stuck %d up %.2f exit %d",
			tag_get_name(object_get(bot->ride_vehicle_index)->definition_index), (int)bot->ride_seat, now - bot->ride_tick,
			(int)bot->ride_stuck_count, object_get(bot->ride_vehicle_index)->object.up.k, (int)bot->ride_exit);
	}
	else if (bot->vehicle_index != NONE && object_try_and_get(bot->vehicle_index))
	{
		platform_log("bots: trace going for %s seat %d%s, entrance (%.1f %.1f %.1f) %.1f away, %d presses",
			tag_get_name(object_get(bot->vehicle_index)->definition_index), (int)bot->vehicle_seat_index,
			bot->vehicle_flip ? " (to right it)" : "", bot->vehicle_entrance.x, bot->vehicle_entrance.y,
			bot->vehicle_entrance.z, bots_distance2d(&bot->vehicle_entrance, &tick->position), (int)bot->vehicle_presses);
	}
	if (bot->path.valid)
	{
		char steps[200];
		int length = 0;
		long step;

		for (step = 0; step < bot->path.step_count && length < (int)sizeof(steps) - 40; step++)
			length += snprintf(steps + length, sizeof(steps) - (size_t)length, " (%.1f %.1f %.1f)",
				bot->path.steps[step].point.x, bot->path.steps[step].point.y, bot->path.steps[step].point.z);
		platform_log("bots: trace path%s%s", steps, bot->via_teleporter ? " to a teleporter" : "");
	}
	if (bot->goal_kind == _bots_goal_item && object_try_and_get(bot->item_index))
	{
		struct object_datum *item = object_get(bot->item_index);

		platform_log("bots: trace item %s at (%.2f %.2f %.2f)", tag_get_name(item->definition_index),
			item->object.position.x, item->object.position.y, item->object.position.z);
	}
	if (tick->weapon_index != NONE)
	{
		struct weapon_datum *weapon = weapon_get(tick->weapon_index);
		short slot;

		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long other = tick->unit->unit.weapon_object_indices[slot];

			if (slot != tick->unit->unit.current_weapon_index && other != NONE)
				platform_log("bots: trace other weapon %s", tag_get_name(object_get(other)->definition_index));
		}
		platform_log("bots: trace weapon %s slot %d loaded %d total %d fired %ld ago, aim off %.1f/%.1f deg dist %.1f",
			tag_get_name(weapon->definition_index), (int)tick->unit->unit.current_weapon_index,
			(int)weapon->weapon.magazines[0].rounds_loaded, (int)weapon->weapon.magazines[0].rounds_total,
			now - weapon->weapon.game_time_last_fired, bot->aim_error_yaw / BOTS_DEGREES,
			bot->aim_error_pitch / BOTS_DEGREES, tick->target_distance);
	}
}

/* port: (teleporters as shortcuts) each tick on foot: a shortcut given up
on in time (the measure never answered, the entrance not come to), and the
teleporter failed when the bot stands on its entrance and is not sent; a
fight stops the clock */
static void bots_shortcut_update(struct bots_bot *bot, struct bots_tick *tick)
{
	real_point3d const *entrance;

	if (bot->shortcut_phase == _bots_shortcut_none)
		return;
	if (tick->fighting)
		bot->shortcut_deadline_tick++;
	if (bot->shortcut_phase == _bots_shortcut_measuring)
	{
		if (tick->now > bot->shortcut_deadline_tick)
			bot->shortcut_phase = _bots_shortcut_none;
		return;
	}
	entrance = &bots_globals.teleporters[bot->shortcut_index].entrance;
	if (bots_distance2d(entrance, &tick->position) < 0.6f && fabs(entrance->z - tick->position.z) < 1.5f)
	{
		if (!bot->shortcut_on_tick)
			bot->shortcut_on_tick = tick->now;
		else if (tick->now - bot->shortcut_on_tick > TICKS_PER_SECOND * 2)
		{
			bots_shortcut_fail(bot, "stood on it, not sent", tick->now);
			return;
		}
	}
	else
		bot->shortcut_on_tick = 0;
	/* (not taken yet, its searches to the entrance yet to say how far it
	is: let go, no failure of the teleporter's) */
	if (tick->now > bot->shortcut_deadline_tick && !bot->shortcut_entrance_known)
		bots_shortcut_drop(bot, "no answer");
	else if (tick->now > bot->shortcut_deadline_tick)
		bots_shortcut_fail(bot, "not come to in time", tick->now);
}

static void bots_bot_update(struct bots_bot *bot, long bot_index, struct player_action *action)
{
	struct bots_tick tick;
	real vitality;
	unsigned long long started = bots_profile.enabled > 0 ? bots_time_us() : 0;
	long vehicle_index;

	csmemset(&tick, 0, sizeof(tick));
	tick.skill = &bots_skills[bots_globals.skill];
	tick.player = player_get(bot->player_index);
	tick.now = game_time_get();
	tick.throttle_scale = 1.0f;
	tick.action = action;
	csmemset(action, 0, sizeof(*action));
	action->desired_weapon_index = NONE;
	action->desired_grenade_index = NONE;
	action->desired_zoom_level = NONE;
	action->desired_facing.yaw = bot->yaw;
	action->desired_facing.pitch = bot->pitch;

	if (tick.player->unit_index == NONE)
	{
		/* (dead: it forgets its fight, its path and its search; the game
		respawns it) */
		bots_search_take(bot, FALSE);
		bot->unit_index = NONE;
		bot->target_player_index = NONE;
		bot->path.valid = FALSE;
		bot->goal_kind = _bots_goal_none;
		bot->previous_control_flags = 0;
		/* port: (and the vehicle it went for, or rode; its vantage point
		and its lane's waypoints) */
		bots_vehicle_forget(bot);
		bot->ride_vehicle_index = NONE;
		bots_tactics_bot_dead(bot_index);
		/* port: (and its teleporter shortcut) */
		bot->shortcut_phase = _bots_shortcut_none;
		return;
	}
	tick.unit = unit_get(tick.player->unit_index);
	tick.object = object_get(tick.player->unit_index);
	if (bot->unit_index != tick.player->unit_index)
	{
		/* (spawned: it looks the way its unit faces) */
		bot->unit_index = tick.player->unit_index;
		bot->yaw = (real)atan2(tick.object->object.forward.j, tick.object->object.forward.i);
		bot->pitch = 0.0f;
		bot->last_vitality = tick.object->object.body_vitality + tick.object->object.shield_vitality;
		bot->stuck_position = tick.object->object.position;
		bot->stuck_tick = tick.now;
		bot->stuck_count = 0;
		bot->goal_kind = _bots_goal_none;
		bot->next_look_tick = tick.now + (bot_index % 3);
		bot->climb_until_tick = 0;
		bot->path_short = FALSE;
		bot->via_teleporter = FALSE;
		bot->shortcut_phase = _bots_shortcut_none;
		bot->escape_until_tick = 0;
		bot->last_position = tick.object->object.position;
		bots_vehicle_forget(bot);
		bot->ride_vehicle_index = NONE;
		/* port: (its base's vehicles looked at as it spawns) */
		bot->spawn_tick = tick.now;
		bot->vehicle_check_tick = 0;
		bot->last_body_vitality = tick.object->object.body_vitality;
		bot->item_powerup = _bots_powerup_none;
		bot->powerup_index = NONE;
		/* (its fight by level, started over) */
		bots_level_reset(&bot->combat);
	}
	/* port: riding, it is where its vehicle is (a rider's own position is
	its seat's) */
	vehicle_index = bots_unit_vehicle(tick.player->unit_index);
	tick.position = vehicle_index != NONE ? object_get(vehicle_index)->object.position : tick.object->object.position;
	if (vehicle_index == NONE && bot->ride_vehicle_index != NONE)
		bots_vehicle_got_out(bot, &tick);
	/* (the time riding, and kills riding: the census's) */
	if (vehicle_index != NONE && bot->ride_vehicle_index == vehicle_index)
		bots_globals.riding_ticks++;
	if (vehicle_index != NONE && bot->ride_vehicle_index == vehicle_index && tick.player->statistics.kills[0] > bot->kills_seen)
	{
		bots_globals.riding_kills[bot->ride_seat != _bots_seat_driver] += tick.player->statistics.kills[0] - bot->kills_seen;
		if (bot->ride_turret)
			bots_globals.turret_kills += tick.player->statistics.kills[0] - bot->kills_seen;
	}
	bot->kills_seen = tick.player->statistics.kills[0];
	/* (through a teleporter: a new path from where it came out; port: which
	one and how it came to it, halo.log's census) */
	if (bots_distance3d(&tick.position, &bot->last_position) > 3.0f && tick.object->object.parent_object_index == NONE)
		bots_teleporter_landed(bot, &bot->last_position, &tick.position, tick.now);
	if (bots_distance3d(&tick.position, &bot->last_position) > 3.0f && bot->goal_kind != _bots_goal_none &&
		tick.object->object.parent_object_index == NONE)
	{
		bot->via_teleporter = FALSE;
		bot->path.valid = FALSE;
		bot->path_tick = tick.now - TICKS_PER_SECOND;
		bot->stuck_position = tick.position;
	}
	bot->last_position = tick.position;

	/* hurt: it turns to look for who did it */
	vitality = tick.object->object.body_vitality + tick.object->object.shield_vitality;
	if (vitality < bot->last_vitality - 0.01f)
		bot->hurt_tick = tick.now;
	bot->last_vitality = vitality;
	/* port: its body restored (a body heals no other way: a health pack) */
	if (tick.object->object.body_vitality > bot->last_body_vitality + 0.2f)
		bots_globals.bodies_restored++;
	bot->last_body_vitality = tick.object->object.body_vitality;

	bots_profile_part(_bots_part_rest, &started);
	bots_bot_look(bot, &tick);
	bots_profile_part(_bots_part_look, &started);
	tick.weapon_index = bots_current_weapon(tick.unit);
	/* (the flag or the ball taken: halo.log's count) */
	{
		short held = 0;

		/* (the ball is a weapon_is_flag too) */
		if (tick.weapon_index != NONE &&
			(weapon_is_flag(tick.weapon_index) || bots_weapon_value(weapon_get(tick.weapon_index)->definition_index) < 0))
		{
			held = game_engine_get_variant()->game_engine_index == game_engine_ctf ? 1 : 2;
		}
		if (held && held != bot->held_objective)
		{
			if (held == 1)
				bots_globals.flag_grabs++;
			else
				bots_globals.ball_grabs++;
		}
		bot->held_objective = held;
	}
	/* (CTF: at the other team's stand, once a visit; halo.log's count) */
	if (game_engine_get_variant()->game_engine_index == game_engine_ctf && (tick.now % 10) == (bot_index % 10))
	{
		real_point3d stand;
		boolean at = bots_ctf_stand(tick.player->team_index ? 0 : 1, &stand) &&
			bots_distance3d(&stand, &tick.position) < 1.5f;

		if (at && !bot->at_enemy_stand)
			bots_globals.stand_visits++;
		bot->at_enemy_stand = at;
	}
	bots_profile_part(_bots_part_rest, &started);
	/* port: riding a vehicle: driving, gunning or riding along, and out
	(the lines profile's walk) */
	if (vehicle_index != NONE)
	{
		/* port: (a teleporter shortcut is on foot alone) */
		bots_shortcut_drop(bot, "riding");
		bots_bot_ride(bot, &tick, vehicle_index);
	}
	else
	{
		/* (carrying a flag: home, fighting only what stands in its way) */
		boolean carrying = bots_ctf_carrying(bot);

		if (bot->target_player_index != NONE && tick.now - bot->target_seen_tick <= TICKS_PER_SECOND / 2 &&
			(!carrying || bots_distance3d(&tick.position, &bot->target_last_position) < 2.0f))
		{
			bots_bot_fight(bot, &tick);
		}
		bots_profile_part(_bots_part_fight, &started);

		/* the goal, and the path to it (while fighting, only a weapon close by
		or a vehicle it goes for: towards it, still facing the enemy; carrying
		a flag, home whatever it fights; the game's other goals by Quake III's
		rules, which fight where they are) */
		if (!tick.fighting || bot->goal_kind == _bots_goal_item || bot->goal_kind == _bots_goal_vehicle || carrying)
			bots_choose_goal(bot, bot->player_index, &tick.position, tick.fighting && !carrying);
		/* port: hurt in a fight: a health pack near, backed towards as it
		fights (not a zombie, which takes nothing, nor on the hill it holds) */
		else if (!bot->held_objective && !bots_zombie(tick.player) &&
			!(bots_king_active() && bots_king_on_hill(tick.player->unit_index)))
		{
			bots_powerup_goal(bot, tick.unit, &tick.position, TRUE);
		}
		/* (Infection's zombies have no vehicles: infection.c takes them out) */
		if (!bots_zombie(tick.player))
		{
			bots_vehicle_consider(bot, &tick);
			bots_turret_consider(bot, &tick);
		}
		bots_profile_part(_bots_part_goal, &started);
		bots_search_take(bot, TRUE);
		/* port: (its teleporter shortcut: in time, or failed) */
		bots_shortcut_update(bot, &tick);
		bots_profile_part(_bots_part_take, &started);
		if (!tick.fighting || carrying)
		{
			bots_bot_walk(bot, &tick);
			bots_bot_yield(bot, bot_index, &tick);
			/* (a grenade where the target was last seen, from veteran) */
			if (!tick.fighting)
				bots_level_prenade(bot, &tick);
		}
		else if (bot->goal_kind == _bots_goal_item || bot->goal_kind == _bots_goal_vehicle)
		{
			real direction;
			struct object_datum *item;

			if (bots_follow_path(bot, tick.player->unit_index, &tick.position, &direction))
				tick.move_direction = direction;
			/* port: (at a powerup: on to it, still fighting) */
			else if (bot->goal_kind == _bots_goal_item &&
				(item = object_try_and_get_and_verify_type(bot->item_index, _object_mask_item)) != NULL &&
				bots_distance2d(&item->object.position, &tick.position) > 0.3f)
			{
				tick.move_direction = (real)atan2(item->object.position.y - tick.position.y,
					item->object.position.x - tick.position.x);
				tick.moving = TRUE;
			}
		}
		/* (carrying a flag near home: straight onto its place, which a
		capture needs within a unit of, past where its path ends; a jump if it
		stands still there) */
		if (carrying && (tick.player->team_index == 0 || tick.player->team_index == 1) &&
			bots_globals.ctf.present[tick.player->team_index])
		{
			real_point3d const *home = &bots_globals.ctf.home[tick.player->team_index];

			if (bots_distance2d(&tick.position, home) < 4.0f && (real)fabs(home->z - tick.position.z) < 2.5f)
			{
				tick.move_direction = (real)atan2(home->y - tick.position.y, home->x - tick.position.x);
				tick.moving = TRUE;
				tick.throttle_scale = 1.0f;
				if (bots_distance2d(&tick.position, home) < 1.5f && home->z > tick.position.z + 0.4f &&
					(tick.now & 15) == 0)
				{
					tick.flags |= FLAG(_unit_control_jump_bit);
				}
			}
		}
		/* port: fighting at its vantage point: there still, or off it with
		the enemy near (bots_tactics.h) */
		if (tick.fighting)
		{
			bots_tactics_fight(bot_index, &tick.position, tick.target_distance, &tick.moving, &tick.move_direction,
				&tick.throttle_scale);
		}
		bots_vehicle_approach(bot, &tick);
		bots_profile_part(_bots_part_walk, &started);
		bots_bot_weapons(bot, &tick);
		bots_bot_unstick(bot, &tick);
	}
	bots_profile_part(_bots_part_walk, &started);

	if (tick.moving)
	{
		real relative = bots_angle_difference(tick.move_direction, bot->yaw);

		action->throttle.i = (real)cos(relative) * tick.throttle_scale;
		action->throttle.j = (real)sin(relative) * tick.throttle_scale;
	}
	bot->pitch = PIN(bot->pitch, -1.4f, 1.4f);
	bot->yaw = bots_angle_difference(bot->yaw, 0.0f);
	action->desired_facing.yaw = bot->yaw;
	action->desired_facing.pitch = bot->pitch;

	/* a press is one tick held and the next released (the jump, the grenade,
	the blow, the reload, the action button: port, into and out of a
	vehicle), as a player's buttons are; the trigger and the
	swap button may be held */
	{
		unsigned long pressed = FLAG(_unit_control_jump_bit) | FLAG(_unit_control_throw_grenade_bit) |
			FLAG(_unit_control_use_equipment_bit) | FLAG(_unit_control_weapon_reload_bit) |
			FLAG(_unit_control_action_bit);

		tick.flags &= ~(bot->previous_control_flags & pressed);
		bot->previous_control_flags = tick.flags;
	}
	action->control_flags = tick.flags;
	action->primary_trigger = TEST_FLAG(tick.flags, _unit_control_weapon_primary_trigger_bit) ? 1.0f : 0.0f;
	bots_bot_trace(bot, bot_index, &tick);
	bots_profile_part(_bots_part_rest, &started);
}

static void bots_report(void)
{
	long now = game_time_get();
	long count = 0, index;
	long fired = 0, hit = 0;
	char line[600];
	int length = 0;

	if (bots_globals.last_report_tick != NONE && now - bots_globals.last_report_tick < TICKS_PER_SECOND * 60)
		return;
	bots_globals.last_report_tick = now;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *bot = &bots_globals.bots[index];
		struct player_datum *player = bot->player_index != NONE ? player_try_and_get(bot->player_index) : NULL;

		if (!player)
			continue;
		count++;
		fired += player->statistics.shots_fired;
		hit += player->statistics.shots_hit;
		if (length < (int)sizeof(line) - 40)
			length += snprintf(line + length, sizeof(line) - (size_t)length, " %ld:%d/%d", index + 1,
				(int)player->statistics.kills[0], (int)player->statistics.deaths);
	}
	line[length] = 0;
	if (!count || !bots_globals.ticks)
		return;
	platform_log("bots: tick %ld, %ld bots (%s): %.0f us/tick (%.1f a bot), searches %ld (%ld failed, %ld short, %ld full, %ld unavoided, %ld unsmoothed, tick %.0f us, "
		"helper %.0f us), shots %ld hit %ld, flag taken %ld (enemy stand reached %ld), ball taken %ld, kills/deaths%s", now, count,
		bots_skills[bots_skill()].name,
		(double)bots_globals.tick_us / (double)bots_globals.ticks,
		bots_globals.bot_ticks ? (double)bots_globals.tick_us / (double)bots_globals.bot_ticks : 0.0,
		bots_globals.searches, bots_globals.search_failures, bots_globals.search_partials, bots_globals.search_overflows,
		bots_globals.search_unavoided, bots_globals.search_unsmoothed,
		(double)bots_globals.search_us / (double)bots_globals.ticks,
		(double)bots_helper.helper_us / (double)bots_globals.ticks, fired, hit, bots_globals.flag_grabs,
		bots_globals.stand_visits, bots_globals.ball_grabs, line);
	{
		long const *counts = bots_globals.combat_counts;

		platform_log("bots: fights since the map's start: grenades %ld (%ld at a last-seen spot), blows %ld "
			"(%ld held back), head aims %ld, retreats %ld, charges %ld, jumps %ld, halts %ld, shots paced %ld, "
			"zoomed again %ld, high ground picks %ld, enemies out of vertical view %ld",
			counts[_bots_combat_count_grenades] + counts[_bots_combat_count_prenades], counts[_bots_combat_count_prenades],
			counts[_bots_combat_count_blows], counts[_bots_combat_count_blows_held], counts[_bots_combat_count_head_aims],
			counts[_bots_combat_count_retreats], counts[_bots_combat_count_charges], counts[_bots_combat_count_jumps],
			counts[_bots_combat_count_halts], counts[_bots_combat_count_paced], counts[_bots_combat_count_rescopes],
			counts[_bots_combat_count_high_ground], counts[_bots_combat_count_out_of_view]);
	}
	platform_log("bots: moves at tick %ld: stuck %ld (a low wall %ld, a tall one %ld, nothing %ld), goals given up %ld; "
		"powerups gone for/taken: health %ld/%ld, overshield %ld/%ld, camouflage %ld/%ld; bodies restored %ld; "
		"ladders %ld, gone to %ld, climbed %ld; teleporters %ld (shortcuts: weighed %ld, taken %ld, through %ld, "
		"failed %ld, dropped %ld; through as the last way %ld, by chance %ld)",
		now, bots_globals.stuck_events, bots_globals.stuck_low, bots_globals.stuck_tall, bots_globals.stuck_clear,
		bots_globals.stuck_giveups, bots_globals.powerups_sought[_bots_powerup_health],
		bots_globals.powerups_taken[_bots_powerup_health], bots_globals.powerups_sought[_bots_powerup_overshield],
		bots_globals.powerups_taken[_bots_powerup_overshield], bots_globals.powerups_sought[_bots_powerup_camouflage],
		bots_globals.powerups_taken[_bots_powerup_camouflage], bots_globals.bodies_restored, bots_globals.ladder_count,
		bots_globals.ladder_tries, bots_globals.ladder_climbs, bots_globals.teleporter_count - bots_globals.ladder_count,
		bots_globals.shortcut_measures, bots_globals.shortcut_takes, bots_globals.shortcut_through[_bots_through_shortcut],
		bots_globals.shortcut_failures, bots_globals.shortcut_drops, bots_globals.shortcut_through[_bots_through_last_way],
		bots_globals.shortcut_through[_bots_through_by_chance]);
	/* port: (each teleporter's bots sent, by how they came to it, and its
	failures as a shortcut: the map's teleporters' census; a line more for
	every ten or so - Chiron TL34's thirty) */
	if (bots_globals.teleporter_count > bots_globals.ladder_count)
	{
		length = 0;
		for (index = 0; index < bots_globals.teleporter_count; index++)
		{
			if (!bots_globals.teleporters[index].ladder)
			{
				length += snprintf(line + length, sizeof(line) - (size_t)length,
					"%s %ld sent %ld (shortcut %ld, last way %ld, chance %ld) failed %ld", length ? ";" : "", index + 1,
					bots_teleporter_sent(index), bots_globals.teleporters[index].through[_bots_through_shortcut],
					bots_globals.teleporters[index].through[_bots_through_last_way],
					bots_globals.teleporters[index].through[_bots_through_by_chance], bots_globals.teleporters[index].failures);
			}
			if (length > 0 && (length > (int)sizeof(line) - 100 || index + 1 >= bots_globals.teleporter_count))
			{
				line[MIN(length, (int)sizeof(line) - 1)] = 0;
				platform_log("bots: teleporters at tick %ld:%s", now, line);
				length = 0;
			}
		}
	}
	/* port: (the lanes, vantage points and posts: bots_tactics.h) */
	bots_tactics_report(now);
	if (bots_profile.enabled > 0)
	{
		double ticks = (double)bots_globals.ticks;

		platform_log("bots: tick %ld split (us/tick): look %.1f fight %.1f goal %.1f take %.1f walk %.1f rest %.1f scan %.1f | "
			"rays a tick: down %.2f (%.1f us) sight %.2f (%.1f us)", now,
			bots_profile.part_us[_bots_part_look] / ticks, bots_profile.part_us[_bots_part_fight] / ticks,
			bots_profile.part_us[_bots_part_goal] / ticks, bots_profile.part_us[_bots_part_take] / ticks,
			bots_profile.part_us[_bots_part_walk] / ticks, bots_profile.part_us[_bots_part_rest] / ticks,
			bots_profile.part_us[_bots_part_scan] / ticks, bots_profile.below_rays / ticks,
			bots_profile.below_us / ticks, bots_profile.sight_rays / ticks, bots_profile.sight_us / ticks);
	}
	/* (capture the flag: each team's grabs, returns and captures, its flag's
	state, and its bots that defend) */
	if (bots_ctf_active())
	{
		long grabs[2] = { 0, 0 }, returns[2] = { 0, 0 }, scores[2] = { 0, 0 }, defenders[2] = { 0, 0 };
		struct data_iterator iterator;
		struct player_datum *player;
		long team;

		bots_ctf_update();
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			team = player->team_index;
			if (team != 0 && team != 1)
				continue;
			grabs[team] += player->statistics.multiplayer_statistics.ctf_statistics.flag_grabs;
			returns[team] += player->statistics.multiplayer_statistics.ctf_statistics.flag_returns;
			scores[team] += player->statistics.multiplayer_statistics.ctf_statistics.flag_scores;
			if (bots_machine_is_bot(player->network_player_data.machine_index))
			{
				struct bots_bot *bot = &bots_globals.bots[player->network_player_data.machine_index - BOTS_FIRST_MACHINE];

				if (bot->player_index == iterator.datum_index && bots_ctf_defender(bot, player))
					defenders[team]++;
			}
		}
		platform_log("bots: ctf: red %ld grabs, %ld returns, %ld captures, flag %s, %ld defending; blue %ld grabs, "
			"%ld returns, %ld captures, flag %s, %ld defending",
			grabs[0], returns[0], scores[0], !bots_globals.ctf.present[0] ? "none" : bots_globals.ctf.at_home[0] ? "home" :
				bots_globals.ctf.carrier_player_index[0] != NONE ? "carried" : "dropped", defenders[0],
			grabs[1], returns[1], scores[1], !bots_globals.ctf.present[1] ? "none" : bots_globals.ctf.at_home[1] ? "home" :
				bots_globals.ctf.carrier_player_index[1] != NONE ? "carried" : "dropped", defenders[1]);
	}
	/* (king of the hill: the time on the hill, seconds, by team or the bots'
	and the players' in free for all; the hill's number and who is on it) */
	if (bots_king_active())
	{
		long seconds[2] = { 0, 0 }, on[2] = { 0, 0 }, guards = 0;
		struct data_iterator iterator;
		struct player_datum *player;
		boolean teams = game_engine_has_teams();

		bots_king_update();
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			boolean bot = bots_machine_is_bot(player->network_player_data.machine_index);
			long side = teams ? (player->team_index == 1) : bot ? 0 : 1;

			seconds[side] += player->statistics.multiplayer_statistics.king_statistics.time_on_hill / TICKS_PER_SECOND;
			if (bots_king_on_hill(player->unit_index))
				on[side]++;
			if (bot)
			{
				struct bots_bot *own = &bots_globals.bots[player->network_player_data.machine_index - BOTS_FIRST_MACHINE];

				if (own->player_index == iterator.datum_index && bots_king_guard(own, player))
					guards++;
			}
		}
		platform_log("bots: king: hill %ld%s; %s %ld s on the hill, %ld on it now; %s %ld s, %ld on it now; %ld guarding",
			bots_globals.king.hill_id, bots_globals.king.present ? "" : " (none)", teams ? "red" : "bots", seconds[0],
			on[0], teams ? "blue" : "players", seconds[1], on[1], guards);
	}
}

void bots_update_actions(struct player_action *actions)
{
	struct data_iterator iterator;
	struct player_datum *player;
	unsigned long long started;
	long count = 0;

	/* (the host's alone: a client drives the bots with the input it relays) */
	if (!game_engine_running() || !bots_game_allowed())
		return;
	started = bots_time_us();
	bots_profile_enabled();
	bots_globals.skill = bots_skill();
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long bot_index;
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct bots_bot *bot;

		if (player->local_player_index != NONE || !bots_machine_is_bot(player->network_player_data.machine_index))
			continue;
		/* (the helper thread and the roaming places once a game has bots) */
		if (!bots_helper.started)
			bots_helper_start();
		if (!bots_globals.map_ready)
			bots_prepare_map();
		if (!count)
		{
			unsigned long long scan_started = bots_profile.enabled > 0 ? bots_time_us() : 0;

			bots_ground_memo_begin();
			bots_powerups_find();
			bots_paths_scan();
			/* port: the map's lanes and vantage points (a bounded step a
			tick), the roles (bots_tactics.h) */
			bots_tactics_update();
			bots_profile_part(_bots_part_scan, &scan_started);
		}
		bot_index = player->network_player_data.machine_index - BOTS_FIRST_MACHINE;
		bot = &bots_globals.bots[bot_index];
		if (bot->player_index != iterator.datum_index)
			bots_bot_reset(bot, iterator.datum_index, bot_index);
		if (absolute_index >= 0 && absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		{
			bots_bot_update(bot, bot_index, &actions[absolute_index]);
			/* (what the clients are relayed of it) */
			update_server_note_bot_action((short)absolute_index, &actions[absolute_index]);
		}
		count++;
	}
	bots_ground_memo.active = FALSE;
	if (count)
	{
		bots_globals.tick_us += bots_time_us() - started;
		bots_globals.ticks++;
		bots_globals.bot_ticks += count;
		bots_report();
		bots_vehicle_census();
	}
}
