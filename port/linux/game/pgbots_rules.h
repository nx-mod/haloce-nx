/* pgbots_rules.h: the Proving Grounds bots' rules (port/linux/game/pgbots_rules.c)

The second kind of bot (bots.kind "provinggrounds", next to Birchwood's
bots.c) decides with these: plain numbers in, plain numbers out, no call into
the engine, so that the harness runs them as they are
(tools/harness/tests/test_pgbots_rules.py). pgbots*.c read the game and ask
them.

Clean room: the design is research_notes/ppg-bot-personalities.md, written in
our own words after Project Proving Grounds' bots (PolyForm Noncommercial:
none of its code is here, nor may be).

Units: distances in world units, angles in radians, times in seconds unless
a name says ticks (30 a second). A `random` argument is a number from 0 to 1
(the bot's own random numbers: pgbots_rules_random01). Skill levels are
bots_combat.h's (recruit to legendary). */
#ifndef HALO_PGBOTS_RULES_H
#define HALO_PGBOTS_RULES_H

#define PGBOTS_TICKS_PER_SECOND 30
#define PGBOTS_PI 3.14159265f

/* ---------- personalities (S12) */

enum
{
	_pgbots_personality_balanced = 0,
	_pgbots_personality_rusher,
	_pgbots_personality_anchor,
	_pgbots_personality_marksman,
	_pgbots_personality_flanker,
	_pgbots_personality_grenadier,
	_pgbots_personality_objective,
	_pgbots_personality_grudger,
	NUMBER_OF_PGBOTS_PERSONALITIES,
};

/* a personality's traits, 0 to 1 (0.5 neutral) */
enum
{
	_pgbots_trait_aggression = 0,
	_pgbots_trait_caution,
	_pgbots_trait_camping,
	_pgbots_trait_objective,
	_pgbots_trait_grenades,
	_pgbots_trait_jumpy,
	_pgbots_trait_crouchy,
	_pgbots_trait_grudge,
	_pgbots_trait_stickiness,
	_pgbots_trait_greed,
	_pgbots_trait_route_style,
	NUMBER_OF_PGBOTS_TRAITS,
};

/* ---------- weapon classes (S9), from tag data alone */

enum
{
	_pgbots_class_objective = 0,
	_pgbots_class_charge,
	_pgbots_class_close,
	_pgbots_class_explosive,
	_pgbots_class_sniper,
	_pgbots_class_precision,
	_pgbots_class_automatic,
	NUMBER_OF_PGBOTS_CLASSES,
};

/* ---------- the skill tables (S5) */

/* the hands: how quick and how true (difficulty, never personality) */
struct pgbots_hands
{
	float reaction;
	float track;
	float lag;
	float flick;
	float overshoot;
	float aim_error;
	float lead;
	float turn_degrees;
	float memory;
	float head_share;
	float press_share;
	/* the loose trigger: seconds it keeps firing after losing sight, and the
	share of a flick's end it fires in (0 0: none, legendary) */
	float loose_follow;
	float loose_early;
	/* a ram's heading error, degrees (negative: no ramming) */
	float ram_error_degrees;
	float callout_chance;
	float hear_delay;
};

/* the craft: what a player of the level knows to do, 0 to 1 */
struct pgbots_craft
{
	float jump;
	float strafe;
	float juke;
	float routes;
	float map_iq;
	float tactics;
	float aware;
	float radar;
	float teamwork;
};

struct pgbots_personality_definition
{
	char const *name;
	float traits[NUMBER_OF_PGBOTS_TRAITS];
	/* weapon taste: the classes liked (+1.0) and disliked (-0.5), as bits */
	unsigned long liked;
	unsigned long disliked;
};

/* a bot's profile: its level's hands and craft and its personality's traits,
each jittered once (S4, S12) */
struct pgbots_profile
{
	short skill;
	short personality;
	struct pgbots_hands hands;
	struct pgbots_craft craft;
	float traits[NUMBER_OF_PGBOTS_TRAITS];
	unsigned long liked;
	unsigned long disliked;
};

struct pgbots_hands const *pgbots_rules_hands(short skill);
struct pgbots_craft const *pgbots_rules_craft(short skill);
struct pgbots_personality_definition const *pgbots_rules_personality(short personality);
/* a personality by its name (HALO_PGBOT_PERSONALITY), NONE (-1) for none */
short pgbots_rules_personality_named(char const *name);
/* the personality dealt to the bot of this rank in its team (0 up), from the
game's offset: round-robin in the table's order, the same offset for both
teams, so the rosters mirror */
short pgbots_rules_deal(long team_rank, unsigned long offset);
/* a value x U(0.9, 1.1) */
float pgbots_rules_jitter(float value, float random);
/* the profile for a level and a personality, its jitter from *random */
void pgbots_rules_profile(struct pgbots_profile *profile, short skill, short personality, unsigned long *random);
/* novice behaviours: easy and recruit (tactics 0) */
int pgbots_rules_novice(struct pgbots_profile const *profile);

/* ---------- random numbers (xorshift32, as bots.c's) */

unsigned long pgbots_rules_random(unsigned long *state);
float pgbots_rules_random01(unsigned long *state);
/* a standard normal number from two uniform ones (Box-Muller) */
float pgbots_rules_gaussian(float random_a, float random_b);

/* ---------- perception (S6) */

/* seconds before a seen enemy is recognised: reaction x exp(N(0, 0.25)),
x1.6 outside the foveal cone, x0.35 when lost under 3 s ago */
float pgbots_rules_recognition_seconds(float reaction, int peripheral, int relost, float gaussian);
/* whether a point at this bearing (off the view, radians) and distance is in
view: 55 degrees and 80 units, or anything within 2.5 */
int pgbots_rules_in_view(float bearing, float distance);
/* the foveal cone: 35 degrees */
int pgbots_rules_foveal(float bearing);
/* the radar's chance a scan (every scan_ticks) */
float pgbots_rules_radar_chance(float radar, long scan_ticks);

/* ---------- hands: aim (S7) */

/* seconds between samples of the target's velocity: track x U(0.8, 1.2) */
float pgbots_rules_sample_seconds(float track, float random);
/* the lead taken this sample: lead x U(0.6, 1.2) */
float pgbots_rules_lead_scale(float lead, float random);
/* focus on one target, 0 to 1, built over 1.5 s */
float pgbots_rules_focus(float seconds_on_target);
/* the aim error's spread (yaw; pitch is 0.6 of it) */
float pgbots_rules_aim_sigma(float aim_error, float focus);
/* a flick's seconds for a turn of delta radians: Fitts' law, scaled by the
track and the level's flick, never quicker than the turn speed */
float pgbots_rules_flick_seconds(float delta, float track, float flick, float turn_radians_per_second);
/* whether a turn is a flick (over 0.35 rad) */
int pgbots_rules_is_flick(float delta);
/* smoothstep easing of a flick, 0 to 1 */
float pgbots_rules_ease(float t);
/* a flick's overshoot (0 for none, else 0.08 to 0.18 of the turn) */
float pgbots_rules_overshoot(float chance, float random_roll, float random_size);
/* the pursuit's step toward an error of `error` radians this tick: the gain
1 - exp(-dt/lag), capped at 9 rad/s */
float pgbots_rules_pursuit_step(float error, float dt, float lag);
/* the head (1) or chest (0): rolled once per target, weapon and shield state,
a precision-class weapon at the head-aim share once the shield is down (the
novices regardless of it) */
int pgbots_rules_aim_head(int precision_class, int shield_down, int novice, float head_share, float random);

/* ---------- hands: the trigger (S7, S9) */

struct pgbots_fire_input
{
	/* seconds since the target was last seen; the loose follow-through */
	float seen_age;
	float loose_follow;
	/* the line clear this tick */
	int line_clear;
	/* the reticle's offset from the aim point at the target (units) */
	float across;
	float vertical;
	/* in a flick: how far through (0 to 1), and the loose early share */
	int flicking;
	float flick_progress;
	float loose_early;
	/* the target's distance, the weapon's class, its band's high end and
	blast radius; being hit now (a deterrent burst's window open) */
	float distance;
	short weapon_class;
	float band_high;
	float blast_radius;
	int deterrent;
	/* a teammate in the line of fire, or the blast's reach */
	int teammate_in_line;
};

int pgbots_rules_may_fire(struct pgbots_fire_input const *input);
/* seconds between presses of a semi-automatic weapon (precision, sniper):
its rate of fire, faster spam within 4 u, paced at middle range, 1.1-1.35
slower past 10 u */
float pgbots_rules_press_interval(float rate_of_fire, float distance, float random);
/* an automatic's burst and gap (seconds): spray within 4 u (gap 0) */
void pgbots_rules_burst(float distance, float random_a, float random_b, float *burst, float *gap);

/* ---------- target choice (S9) */

struct pgbots_threat_input
{
	int power_weapon;
	int in_band;
	float distance;
	int shield_down;
	int aiming_at_us;
	int aiming_away;
	int shooting_teammate;
	int carrier;
	int teammate_on_him;
	int last_killer;
	int shooting_us;
	int team_focus;
};

float pgbots_rules_threat(struct pgbots_threat_input const *input, float tactics, float teamwork, float grudge);
/* the factor a new target's threat must beat the current one's by */
float pgbots_rules_switch_factor(float tactics, float damage_done, float stickiness);
/* whether to switch: past the factor, the current one held 1 s at least */
int pgbots_rules_switch(float current, float candidate, float factor, float held_seconds);

/* ---------- weapons (S9) */

/* a weapon's tag facts, for its class */
struct pgbots_weapon_facts
{
	int is_flag;
	float charging_time;
	short projectiles_per_shot;
	/* the projectile's maximum range (0: none set), its blast radius (0:
	none), danger radius and speed (units a second; 0 hitscan or none) */
	float maximum_range;
	float blast_radius;
	float danger_radius;
	float speed;
	short zoom_levels;
	float zoom_maximum;
	float error;
	float rate_of_fire;
};

short pgbots_rules_weapon_class(struct pgbots_weapon_facts const *facts);
/* a class's band (low to high), the high end capped at the projectile's range */
void pgbots_rules_class_band(short weapon_class, float maximum_range, float *low, float *high);
float pgbots_rules_class_rank(short weapon_class);
/* how well a band suits a range: 1 inside, to 0 at half the low end and 1.5x
the high end; a negative distance (no fight) weighs typical ranges: within 4,
4-10, 10-20 and past 20 u by 0.3, 0.35, 0.25 and 0.1 */
float pgbots_rules_band_fit(float low, float high, float distance);
/* a weapon's score for a bot: rank + 2 x band fit + taste (+1 liked, -0.5
disliked) */
float pgbots_rules_weapon_score(short weapon_class, float low, float high, float distance, unsigned long liked,
	unsigned long disliked);
/* whether to switch to a weapon: a gain over 1.0 */
int pgbots_rules_weapon_switch(float current, float candidate);
/* the zoom level to hold (0 none) */
short pgbots_rules_zoom(short weapon_class, short zoom_levels, float distance);

/* ---------- goals (S8) */

enum
{
	_pgbots_goal_none = 0,
	_pgbots_goal_fight,
	_pgbots_goal_react,
	_pgbots_goal_attacker,
	_pgbots_goal_retreat,
	_pgbots_goal_recharge,
	_pgbots_goal_hunt,
	_pgbots_goal_objective,
	_pgbots_goal_hold,
	_pgbots_goal_order,
	_pgbots_goal_health,
	_pgbots_goal_powerup,
	_pgbots_goal_weapon,
	_pgbots_goal_vehicle,
	_pgbots_goal_roam,
	NUMBER_OF_PGBOTS_GOALS,
};

struct pgbots_goal_input
{
	/* what it sees and knows */
	int target_visible;
	float target_distance;
	/* a committed fight: the target unseen under 6 s (10 s with its shield
	down) */
	int committed;
	/* a radar contact or a hit within 3.5 u in the last second */
	int react;
	/* hit by an attacker it knows but cannot see; hit by one it does not know */
	int attacker_unseen;
	int hit_unknown;
	/* the retreat's three conditions held, and a shield to recharge */
	int retreat_ok;
	int recharge;
	int recharging;
	/* an unseen belief to hunt, its target weak (shield down) */
	int hunt_belief;
	int hunt_weak;
	float hunt_distance;
	/* alone (no teammate near) or free for all */
	int alone;
	/* the objective: there is one, carried by it, a grab within 8 u, an
	unguarded enemy stand */
	int objective;
	int carrying;
	int grab_near;
	/* the team's carrier going back for the flag it let go, its return near
	(bots_team.h): before any fight */
	int regrab_urgent;
	int stand_unguarded;
	/* a runner (the objective its role) */
	int runner;
	/* a hold of its team's */
	int hold;
	/* the director's order, accepted, and how far it is from its point */
	int order;
	float order_distance;
	/* items: health lost (0 to 1) and the nearest pack's distance (negative:
	none); a powerup's value and distance (negative: none), and the greed
	bias and camouflage or overshield; a weapon: dry, an upgrade, a team
	fetch, its taste bias */
	float hurt;
	float health_distance;
	float powerup_value;
	float powerup_distance;
	int powerup_camouflage;
	int powerup_overshield;
	int dry;
	int upgrade;
	int fetch;
	float weapon_taste;
	/* a vehicle's score (S10), 0 for none */
	float vehicle;
	/* the modifiers: posture, outnumbered, just hit, no damage either way
	for 6 s, a race */
	float posture;
	int outnumbered;
	int just_hit;
	int stalemate;
	int race;
	/* goals banned (no headway), as bits */
	unsigned long banned;
};

/* every goal's score (before the commitment bonus and the noise); a goal not
possible scores -1 */
void pgbots_rules_goal_scores(struct pgbots_goal_input const *input, struct pgbots_profile const *profile,
	short personality, float *scores);
/* the goal chosen: the current one +0.2 (holds excepted), each score with
U(0, 0.15) of noise (randoms[goal]), the highest */
short pgbots_rules_goal_pick(float const *scores, short current, float const *randoms);

/* ---------- fighting (S9) */

/* the shield counts as cracked below this (while health is under 0.8) */
float pgbots_rules_cracked(float posture);
/* how far it retreats to cover at most */
float pgbots_rules_retreat_range(float tactics);
/* the opener's chance, rolled once per fight at 4-25 u */
float pgbots_rules_grenade_opener(float grenades, float tactics);
/* the range kept: 0.6 to 1.0 of the band's high end, aggression shorter and
caution longer by up to 25% */
void pgbots_rules_keep_range(float band_high, float aggression, float caution, float *near, float *far);
/* the jump chances: on a strafe flip, a second travelling, a second idle, a
shot */
float pgbots_rules_flip_jump(float jump, float jumpy);
float pgbots_rules_travel_hops(float jump, float jumpy);
float pgbots_rules_idle_hops(float jump, float jumpy);
float pgbots_rules_shot_jump(float jump, float jumpy);
/* a detour's chance on a trip over 15 u */
float pgbots_rules_detour_chance(float routes, float route_style);

/* ---------- the director (S11) */

float pgbots_rules_posture(long our_score, long their_score, long score_limit, float elapsed, float time_limit,
	long alive_us, long alive_them, long team_size);
/* a bot's cost for a role slot */
float pgbots_rules_slot_cost(float distance, int role_changes, float fit);

/* ---------- vehicles (S10) */

struct pgbots_drive_input
{
	/* the signed yaw from the chassis forward to the lookahead point, and
	its distance */
	float alpha;
	float lookahead;
	float wheelbase;
	/* forward and lateral speed, the top speed and acceleration */
	float forward_speed;
	float lateral_speed;
	float top_speed;
	float acceleration;
	/* the largest turn within the stopping distance ahead */
	float turn_ahead;
	/* reversing now, and for how long; the lookahead point's distance; stuck
	recovery asks to reverse */
	int reversing;
	float reverse_seconds;
	float point_distance;
	int stuck_reverse;
	/* a Ghost (strafes against cross-track error) and that error; a tank
	(its hull turns only while moving) */
	int ghost;
	float cross_track;
	int tank;
	/* distance left to the goal (negative: not arriving) */
	float arrival;
};

struct pgbots_drive_output
{
	/* the desired facing, relative to the chassis */
	float steer;
	float throttle;
	float strafe;
	int brake;
	int reversing;
};

float pgbots_rules_lookahead(float path_radius, float speed);
float pgbots_rules_speed_target(float top_speed, float turn_ahead);
void pgbots_rules_drive(struct pgbots_drive_input const *input, struct pgbots_drive_output *output);
/* the Banshee's pitch to hold its height */
float pgbots_rules_banshee_pitch(float height_error, float vertical_speed, float horizontal_distance);
/* a boarding's score (value x near x trip) */
float pgbots_rules_board_score(float value, float distance, float trip);

#endif
