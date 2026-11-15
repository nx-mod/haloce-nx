/*
PGBOTS_MAP.C

(port) What the Proving Grounds bots know of a map (S3 of
research_notes/ppg-bot-personalities.md): computed at a game's start from
what the engine has - the walkable pathfinding surfaces, the netgame flags,
the starting locations, the netgame equipment and the sight test - with no
hand-made data, and apart from Birchwood's vantage points and lanes.

- Reach: the walkable surfaces a body can walk to from the starting
  locations and the objectives (a flood over the surfaces' edges, walkable
  to walkable): a rock's walkable top no ramp reaches is none of the bots'
  places, which they once walked into the rock for.
- Areas: the reached walkable surfaces' middles binned on an 8 unit grid (4
  units a floor), each cell's centre the walkable surface nearest its mean. The unit
  of the callouts, the threat map, roaming and hunting.
- Mid: the walkable surface nearest the middle of the flags, hills and
  starting locations. Bases: the CTF stands, else each team's starting
  locations' middle, else the two starting locations furthest apart.
- Power positions: the areas (thinned to 400) scored first by their height
  over the floor about them and a power item near, the best 64 then by the
  share of up to 48 places (items, flags, hills, ball spawns, checkpoints,
  areas) they see within 45 units; the best 8, 9 units apart, are kept.
  Holds: each team's 3 nearest its base.
- Cover (on demand, 8 candidates at most) and overlooks (a hill's, a ball's).

The work is a fixed amount a tick (surfaces, then rays), the same ticks run
to run; until it is done the bots hold at their base and mid. Clean room:
none of Project Proving Grounds' code.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "items/items.h"
#include "items/equipment.h"
#include "items/equipment_definitions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "ai/path.h"
#include "tag_files/tag_groups.h"

#include "bots.h"
#include "bots_common.h"
#include "pgbots.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

/* the analysis' phases */
enum
{
	_map_phase_start = 0,
	_map_phase_reach,
	_map_phase_bin,
	_map_phase_centres,
	_map_phase_prescore,
	_map_phase_rays,
	_map_phase_done,
};

#define MAP_SURFACES_A_TICK 256
#define MAP_REACH_A_TICK 512
#define MAP_RAYS_A_TICK 4
#define MAP_GRID_SLOTS 2048
#define MAP (pgbots_globals.map)
#define MAP_SAMPLES 48
#define MAP_CANDIDATES 64

static struct
{
	/* the grid: a cell's key, its sums and count, its area */
	long keys[MAP_GRID_SLOTS];
	real sums[MAP_GRID_SLOTS][3];
	long counts[MAP_GRID_SLOTS];
	short areas[MAP_GRID_SLOTS];
	real nearest[MAP_GRID_SLOTS];
	/* the places power positions are scored by */
	long sample_count;
	real_point3d samples[MAP_SAMPLES];
	/* the candidates rayed: their area, the score before the rays, the
	places seen and in reach */
	long candidate_count;
	short candidates[MAP_CANDIDATES];
	real prescores[MAP_CANDIDATES];
	short seen[MAP_CANDIDATES];
	short reached[MAP_CANDIDATES];
	long ray_cursor;
} *map_work;

/* the flood of the reach: each surface reached (a byte), the surfaces to
look from, and how far through them (the game's malloc: its free takes it) */
static struct
{
	byte *reached;
	long *queue;
	long count;
	long head;
	long tail;
	long seeds;
} map_reach;

static void map_reach_release(void)
{
	if (map_reach.reached)
		free(map_reach.reached);
	if (map_reach.queue)
		free(map_reach.queue);
	csmemset(&map_reach, 0, sizeof(map_reach));
}

/* whether a surface is the bots' to go to: walkable and reached (all the
walkable ones when nothing seeded the flood) */
static boolean map_surface_reached(long surface_index)
{
	if (!bots_common_surface_walkable(surface_index))
		return FALSE;
	if (!map_reach.reached || map_reach.seeds == 0)
		return TRUE;
	return surface_index >= 0 && surface_index < map_reach.count && map_reach.reached[surface_index];
}

static void map_reach_seed(real_point3d const *point)
{
	real_point3d ground;
	long surface = pgbots_ground(point, &ground);

	if (surface == NONE || surface >= map_reach.count || !map_reach.reached || map_reach.reached[surface] ||
		!bots_common_surface_walkable(surface))
	{
		return;
	}
	map_reach.reached[surface] = 1;
	map_reach.queue[map_reach.tail++] = surface;
	map_reach.seeds++;
}

/* a step of the flood: walkable neighbours of the surfaces reached */
static void map_reach_step(void)
{
	long steps = 0;

	while (map_reach.head < map_reach.tail && steps++ < MAP_REACH_A_TICK)
	{
		long neighbours[16];
		long count = bots_common_surface_neighbours(map_reach.queue[map_reach.head++], neighbours, 16), index;

		for (index = 0; index < count; index++)
		{
			long surface = neighbours[index];

			if (surface >= 0 && surface < map_reach.count && !map_reach.reached[surface] &&
				bots_common_surface_walkable(surface))
			{
				map_reach.reached[surface] = 1;
				map_reach.queue[map_reach.tail++] = surface;
			}
		}
	}
	if (map_reach.head >= map_reach.tail)
	{
		platform_log("pgbots: map: %ld walkable surfaces reached from %ld seeds", map_reach.tail, map_reach.seeds);
		if (map_reach.queue)
			free(map_reach.queue);
		map_reach.queue = NULL;
		MAP.cursor = 0;
		MAP.phase = _map_phase_bin;
	}
}


static long map_cell_key(real_point3d const *point)
{
	long x = (long)floor(point->x / PGBOTS_AREA_SIZE) & 0x3FF;
	long y = (long)floor(point->y / PGBOTS_AREA_SIZE) & 0x3FF;
	long z = (long)floor(point->z / 4.0f) & 0x3FF;

	return (x << 20) | (y << 10) | z;
}

/* the grid's slot for a key (a new one when asked and room is left) */
static long map_cell(long key, boolean add)
{
	unsigned long hash = (unsigned long)key * 2654435761UL;
	long slot = (long)(hash % MAP_GRID_SLOTS), tries;

	for (tries = 0; tries < MAP_GRID_SLOTS; tries++)
	{
		if (map_work->keys[slot] == key)
			return slot;
		if (map_work->keys[slot] == NONE)
		{
			if (!add)
				return NONE;
			map_work->keys[slot] = key;
			return slot;
		}
		slot = (slot + 1) % MAP_GRID_SLOTS;
	}
	return NONE;
}

static boolean map_surface_middle(long surface_index, real_point3d *middle)
{
	real_point3d corners[16];
	long count = bots_common_surface_corners(surface_index, corners), corner;

	if (count < 3)
		return FALSE;
	middle->x = middle->y = middle->z = 0.0f;
	for (corner = 0; corner < count; corner++)
	{
		middle->x += corners[corner].x / (real)count;
		middle->y += corners[corner].y / (real)count;
		middle->z += corners[corner].z / (real)count;
	}
	return TRUE;
}

void pgbots_map_release(void)
{
	map_reach_release();
	if (MAP.areas)
		free(MAP.areas);
	MAP.areas = NULL;
	if (map_work)
		free(map_work);
	map_work = NULL;
	MAP.area_count = 0;
}

void pgbots_map_reset(void)
{
	pgbots_map_release();
	csmemset(&MAP, 0, sizeof(MAP));
	MAP.mid_surface = NONE;
	MAP.base_surfaces[0] = MAP.base_surfaces[1] = NONE;
}

static void map_add_sample(real_point3d const *point)
{
	if (map_work->sample_count < MAP_SAMPLES)
		map_work->samples[map_work->sample_count++] = *point;
}

/* the bases, mid and the places scored by, from the scenario */
static void map_start(void)
{
	struct scenario *scenario = global_scenario_get();
	real_point3d middle = { 0.0f, 0.0f, 0.0f }, team_sums[2] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
	long middle_count = 0, team_counts[2] = { 0, 0 }, index, other;
	boolean stands[2] = { FALSE, FALSE };

	/* (the game's malloc, which its free takes: calloc is the C library's,
	which the game's free asserts on) */
	map_work = (void *)malloc(sizeof(*map_work));
	MAP.areas = (struct pgbots_area *)malloc(PGBOTS_AREAS * sizeof(struct pgbots_area));
	if (map_work)
		csmemset(map_work, 0, sizeof(*map_work));
	if (MAP.areas)
		csmemset(MAP.areas, 0, PGBOTS_AREAS * sizeof(struct pgbots_area));
	if (!map_work || !MAP.areas || !scenario)
	{
		MAP.phase = _map_phase_done;
		return;
	}
	for (index = 0; index < MAP_GRID_SLOTS; index++)
	{
		map_work->keys[index] = NONE;
		map_work->areas[index] = NONE;
		map_work->nearest[index] = 1e30f;
	}
	for (index = 0; index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index,
			struct scenario_netgame_flag);

		if (flag->type == _netgame_flag_ctf_flag || flag->type == _netgame_flag_hill ||
			flag->type == _netgame_flag_oddball_ball_spawn || flag->type == _netgame_flag_race_track)
		{
			middle.x += flag->position.x;
			middle.y += flag->position.y;
			middle.z += flag->position.z;
			middle_count++;
			map_add_sample(&flag->position);
		}
		if (flag->type == _netgame_flag_ctf_flag && VALID_INDEX(flag->team_index, 2))
		{
			stands[flag->team_index] = TRUE;
			MAP.bases[flag->team_index] = flag->position;
		}
	}
	for (index = 0; index < scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment *equipment = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, index,
			struct scenario_netgame_equipment);

		map_add_sample(&equipment->position);
	}
	for (index = 0; index < scenario->players.count; index++)
	{
		struct player_starting_location *location = TAG_BLOCK_GET_ELEMENT(&scenario->players, index,
			struct player_starting_location);

		middle.x += location->position.x;
		middle.y += location->position.y;
		middle.z += location->position.z;
		middle_count++;
		if (VALID_INDEX(location->team_index, 2))
		{
			team_sums[location->team_index].x += location->position.x;
			team_sums[location->team_index].y += location->position.y;
			team_sums[location->team_index].z += location->position.z;
			team_counts[location->team_index]++;
		}
	}
	if (middle_count > 0)
	{
		middle.x /= (real)middle_count;
		middle.y /= (real)middle_count;
		middle.z /= (real)middle_count;
	}
	MAP.mid = middle;
	if (stands[0] && stands[1])
		MAP.has_bases = TRUE;
	else if (team_counts[0] > 0 && team_counts[1] > 0)
	{
		for (index = 0; index < 2; index++)
		{
			MAP.bases[index].x = team_sums[index].x / (real)team_counts[index];
			MAP.bases[index].y = team_sums[index].y / (real)team_counts[index];
			MAP.bases[index].z = team_sums[index].z / (real)team_counts[index];
		}
		MAP.has_bases = TRUE;
	}
	else
	{
		real best = -1.0f;

		for (index = 0; index < scenario->players.count; index++)
		{
			for (other = index + 1; other < scenario->players.count; other++)
			{
				struct player_starting_location *a = TAG_BLOCK_GET_ELEMENT(&scenario->players, index,
					struct player_starting_location);
				struct player_starting_location *b = TAG_BLOCK_GET_ELEMENT(&scenario->players, other,
					struct player_starting_location);
				real distance = pgbots_distance(&a->position, &b->position);

				if (distance > best)
				{
					best = distance;
					MAP.bases[0] = a->position;
					MAP.bases[1] = b->position;
				}
			}
		}
		MAP.has_bases = best > 0.0f;
	}
	MAP.mid_surface = pgbots_ground(&MAP.mid, &MAP.mid);
	for (index = 0; index < 2; index++)
		MAP.base_surfaces[index] = pgbots_ground(&MAP.bases[index], &MAP.bases[index]);
	/* (the flood seeded from the starting locations, the flags, hills, balls
	and checkpoints) */
	map_reach_release();
	map_reach.count = bots_common_surface_count();
	if (map_reach.count > 0)
	{
		map_reach.reached = (byte *)malloc((size_t)map_reach.count);
		map_reach.queue = (long *)malloc(sizeof(long) * (size_t)map_reach.count);
	}
	if (map_reach.reached && map_reach.queue)
	{
		csmemset(map_reach.reached, 0, (size_t)map_reach.count);
		for (index = 0; index < scenario->players.count; index++)
			map_reach_seed(&TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location)->position);
		for (index = 0; index < scenario->netgame_flags.count; index++)
		{
			struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index,
				struct scenario_netgame_flag);

			if (flag->type == _netgame_flag_ctf_flag || flag->type == _netgame_flag_hill ||
				flag->type == _netgame_flag_oddball_ball_spawn || flag->type == _netgame_flag_race_track ||
				flag->type == _netgame_flag_teleporter_target)
			{
				map_reach_seed(&flag->position);
			}
		}
	}
	MAP.cursor = 0;
	MAP.phase = map_reach.queue ? _map_phase_reach : _map_phase_bin;
}

/* the walkable surfaces' middles binned (pass one), then each cell's
nearest surface its area's centre (pass two) */
static void map_bin_step(boolean centres)
{
	long count = bots_common_surface_count();
	long last = MIN(MAP.cursor + MAP_SURFACES_A_TICK, count);

	for (; MAP.cursor < last; MAP.cursor++)
	{
		real_point3d middle;
		long slot;

		if (!map_surface_reached(MAP.cursor) || !map_surface_middle(MAP.cursor, &middle))
			continue;
		slot = map_cell(map_cell_key(&middle), !centres);
		if (slot == NONE)
			continue;
		if (!centres)
		{
			map_work->sums[slot][0] += middle.x;
			map_work->sums[slot][1] += middle.y;
			map_work->sums[slot][2] += middle.z;
			map_work->counts[slot]++;
		}
		else if (map_work->counts[slot] > 0)
		{
			real_point3d mean;
			real distance;

			mean.x = map_work->sums[slot][0] / (real)map_work->counts[slot];
			mean.y = map_work->sums[slot][1] / (real)map_work->counts[slot];
			mean.z = map_work->sums[slot][2] / (real)map_work->counts[slot];
			distance = pgbots_distance(&mean, &middle);
			if (map_work->areas[slot] == NONE && MAP.area_count < PGBOTS_AREAS)
				map_work->areas[slot] = (short)MAP.area_count++;
			if (map_work->areas[slot] != NONE && distance < map_work->nearest[slot])
			{
				struct pgbots_area *area = &MAP.areas[map_work->areas[slot]];

				map_work->nearest[slot] = distance;
				area->center = middle;
				area->surface = MAP.cursor;
				area->visited_tick = NONE;
			}
		}
	}
	if (MAP.cursor >= count)
	{
		MAP.cursor = 0;
		MAP.phase = centres ? _map_phase_prescore : _map_phase_centres;
	}
}

/* the score before the rays: height over the floor about (median within
15 u, clipped -2 to 8) and a power item within 15 u; the best 64 kept */
static void map_prescore(void)
{
	long stride = MAP.area_count > 400 ? (MAP.area_count + 399) / 400 : 1;
	long index, other;
	struct object_iterator iterator;
	real_point3d items[16];
	long item_count = 0;

	/* (the power items on the map now: their spawns) */
	object_iterator_new(&iterator, _object_mask_item, 0);
	while (object_iterator_next(&iterator) && item_count < 16)
	{
		if (pgbots_map_item_power(iterator.index) >= 0.6f)
			items[item_count++] = object_get(iterator.index)->object.position;
	}
	for (index = 0; index < MAP.area_count && map_work->sample_count < MAP_SAMPLES; index += MAX(1, MAP.area_count / 16))
		map_add_sample(&MAP.areas[index].center);
	map_work->candidate_count = 0;
	for (index = 0; index < MAP.area_count; index += stride)
	{
		real heights[48], height, score;
		long height_count = 0, slot, a, b;
		boolean item = FALSE;

		for (other = 0; other < MAP.area_count && height_count < 48; other++)
		{
			if (pgbots_distance2d(&MAP.areas[index].center, &MAP.areas[other].center) < 15.0f)
				heights[height_count++] = MAP.areas[other].center.z;
		}
		/* (the median, by a small sort) */
		for (a = 1; a < height_count; a++)
		{
			real value = heights[a];

			for (b = a - 1; b >= 0 && heights[b] > value; b--)
				heights[b + 1] = heights[b];
			heights[b + 1] = value;
		}
		height = height_count ? MAP.areas[index].center.z - heights[height_count / 2] : 0.0f;
		for (other = 0; other < item_count; other++)
		{
			if (pgbots_distance(&items[other], &MAP.areas[index].center) < 15.0f)
				item = TRUE;
		}
		score = 0.3f * PIN(height, -2.0f, 8.0f) / 10.0f + (item ? 0.2f : 0.0f);
		/* (kept among the best 64) */
		slot = map_work->candidate_count < MAP_CANDIDATES ? map_work->candidate_count++ : NONE;
		if (slot == NONE)
		{
			long worst = 0;

			for (other = 1; other < MAP_CANDIDATES; other++)
			{
				if (map_work->prescores[other] < map_work->prescores[worst])
					worst = other;
			}
			if (map_work->prescores[worst] >= score)
				continue;
			slot = worst;
		}
		map_work->candidates[slot] = (short)index;
		map_work->prescores[slot] = score;
		map_work->seen[slot] = 0;
		map_work->reached[slot] = 0;
	}
	map_work->ray_cursor = 0;
	MAP.phase = _map_phase_rays;
}

/* the rays: from each candidate's eyes to each place within 45 u */
static void map_rays_step(void)
{
	long total = map_work->candidate_count * map_work->sample_count;
	long rays = 0;

	while (map_work->ray_cursor < total && rays < MAP_RAYS_A_TICK)
	{
		long candidate = map_work->ray_cursor / map_work->sample_count;
		long sample = map_work->ray_cursor % map_work->sample_count;
		real_point3d eye = MAP.areas[map_work->candidates[candidate]].center, at = map_work->samples[sample];

		map_work->ray_cursor++;
		eye.z += 0.6f;
		at.z += 0.5f;
		if (pgbots_distance(&eye, &at) > 45.0f || pgbots_distance(&eye, &at) < 1.0f)
			continue;
		map_work->reached[candidate]++;
		rays++;
		if (bots_common_line_of_sight(NONE, &eye, &at, NONE))
			map_work->seen[candidate]++;
	}
	if (map_work->ray_cursor >= total)
	{
		long index, team;

		/* the best 8, 9 units apart */
		MAP.power_count = 0;
		while (MAP.power_count < PGBOTS_POWER)
		{
			long best = NONE;
			real best_score = -1e30f;

			for (index = 0; index < map_work->candidate_count; index++)
			{
				real score;
				long other;
				boolean apart = TRUE;

				if (map_work->candidates[index] == NONE)
					continue;
				score = map_work->prescores[index] + 0.5f * (map_work->sample_count > 0 ?
					(real)map_work->seen[index] / (real)map_work->sample_count : 0.0f);
				for (other = 0; other < MAP.power_count; other++)
				{
					if (pgbots_distance(&MAP.power[other].point, &MAP.areas[map_work->candidates[index]].center) < 9.0f)
						apart = FALSE;
				}
				if (apart && score > best_score)
				{
					best_score = score;
					best = index;
				}
			}
			if (best == NONE)
				break;
			MAP.power[MAP.power_count].point = MAP.areas[map_work->candidates[best]].center;
			MAP.power[MAP.power_count].surface = MAP.areas[map_work->candidates[best]].surface;
			MAP.power[MAP.power_count].score = best_score;
			MAP.power_count++;
			map_work->candidates[best] = NONE;
		}
		/* each team's holds: its three nearest its base */
		for (team = 0; team < PGBOTS_TEAMS; team++)
		{
			boolean taken[PGBOTS_POWER];

			csmemset(taken, 0, sizeof(taken));
			MAP.hold_count[team] = 0;
			while (MAP.has_bases && MAP.hold_count[team] < 3 && MAP.hold_count[team] < MAP.power_count)
			{
				long best = NONE;

				for (index = 0; index < MAP.power_count; index++)
				{
					if (!taken[index] && (best == NONE || pgbots_distance(&MAP.power[index].point, &MAP.bases[team]) <
						pgbots_distance(&MAP.power[best].point, &MAP.bases[team])))
					{
						best = index;
					}
				}
				taken[best] = TRUE;
				MAP.holds[team][MAP.hold_count[team]++] = (short)best;
			}
		}
		MAP.phase = _map_phase_done;
		MAP.ready = TRUE;
		platform_log("pgbots: map: %ld areas, %ld power positions in %ld ticks (%.2f ms in all); mid (%.1f %.1f %.1f)%s",
			MAP.area_count, MAP.power_count, MAP.ticks, (double)MAP.spent_us / 1000.0, MAP.mid.x, MAP.mid.y, MAP.mid.z,
			MAP.has_bases ? "" : ", no bases");
		for (index = 0; index < MAP.power_count; index++)
			platform_log("pgbots: map: power position %ld at (%.1f %.1f %.1f), score %.2f", index + 1,
				MAP.power[index].point.x, MAP.power[index].point.y, MAP.power[index].point.z, MAP.power[index].score);
	}
}

void pgbots_map_step(void)
{
	unsigned long long started;

	if (MAP.generation != bots_common_map_generation())
	{
		pgbots_map_reset();
		MAP.generation = bots_common_map_generation();
	}
	if (MAP.phase == _map_phase_done)
		return;
	started = bots_time_us();
	switch (MAP.phase)
	{
	case _map_phase_start:
		MAP.begun = TRUE;
		map_start();
		break;
	case _map_phase_reach:
		map_reach_step();
		break;
	case _map_phase_bin:
		map_bin_step(FALSE);
		break;
	case _map_phase_centres:
		map_bin_step(TRUE);
		break;
	case _map_phase_prescore:
		map_prescore();
		break;
	case _map_phase_rays:
		map_rays_step();
		break;
	}
	MAP.spent_us += bots_time_us() - started;
	MAP.ticks++;
}

long pgbots_map_area_of(real_point3d const *point)
{
	long dx, dy, dz, best = NONE;
	real best_distance = 1e30f;

	if (!MAP.areas || MAP.area_count <= 0 || !map_work || MAP.phase < _map_phase_prescore)
		return NONE;
	/* (the point's cell and those about it, the nearest area of them) */
	for (dz = 0; dz < 3; dz++)
	{
		for (dx = -1; dx <= 1; dx++)
		{
			for (dy = -1; dy <= 1; dy++)
			{
				real_point3d at = *point;
				long slot;
				real distance;

				at.x += (real)dx * PGBOTS_AREA_SIZE;
				at.y += (real)dy * PGBOTS_AREA_SIZE;
				at.z += dz == 1 ? 4.0f : dz == 2 ? -4.0f : 0.0f;
				slot = map_cell(map_cell_key(&at), FALSE);
				if (slot == NONE || map_work->areas[slot] == NONE)
					continue;
				distance = pgbots_distance(point, &MAP.areas[map_work->areas[slot]].center);
				if (distance < best_distance)
				{
					best_distance = distance;
					best = map_work->areas[slot];
				}
			}
		}
		if (best != NONE)
			return best;
	}
	return NONE;
}

long pgbots_map_roam(struct pgbots_bot *bot, real_point3d const *position)
{
	long tries, best = NONE;
	long now = game_time_get();

	if (MAP.area_count <= 0 || MAP.phase < _map_phase_prescore)
		return NONE;
	for (tries = 0; tries < 4; tries++)
	{
		long area = (long)(pgbots_rules_random(&bot->random) % (unsigned long)MAP.area_count);

		if (pgbots_distance(position, &MAP.areas[area].center) < 10.0f)
			continue;
		if (best == NONE || MAP.areas[area].visited_tick < MAP.areas[best].visited_tick)
			best = area;
	}
	if (best != NONE)
		MAP.areas[best].visited_tick = now;
	return best;
}

boolean pgbots_map_hold(short team, long index, real_point3d *point, long *surface)
{
	if (team < 0 || team >= PGBOTS_TEAMS)
		return FALSE;
	if (MAP.ready && MAP.hold_count[team] > 0)
	{
		long hold = MAP.holds[team][index % MAP.hold_count[team]];

		*point = MAP.power[hold].point;
		*surface = MAP.power[hold].surface;
		return TRUE;
	}
	/* (until the analysis is done: the base, else mid) */
	if (MAP.has_bases && MAP.base_surfaces[team] != NONE)
	{
		*point = index == 2 ? MAP.mid : MAP.bases[team];
		*surface = index == 2 ? MAP.mid_surface : MAP.base_surfaces[team];
		return *surface != NONE;
	}
	return FALSE;
}

boolean pgbots_map_cover(struct pgbots_bot *bot, real_point3d const *position, real_point3d const *threat,
	real reach, real_point3d *cover, long *surface)
{
	long index, candidates = 0;
	real_point3d threat_eye = *threat;
	real away;

	threat_eye.z += 0.6f;
	(void)bot;
	for (index = 0; index < MAP.area_count && candidates < 8; index++)
	{
		real_point3d chest = MAP.areas[index].center;
		real distance = pgbots_distance(position, &chest);

		if (distance > reach || distance < 1.0f || pgbots_distance(&chest, threat) < pgbots_distance(position, threat) * 0.7f)
			continue;
		candidates++;
		chest.z += 0.5f;
		pgbots_globals.rays++;
		if (!bots_common_line_of_sight(NONE, &threat_eye, &chest, NONE))
		{
			*cover = MAP.areas[index].center;
			*surface = MAP.areas[index].surface;
			return TRUE;
		}
	}
	/* (none: a point 12 u straight away from the threat, on the ground) */
	away = pgbots_yaw_to(threat, position);
	*cover = *position;
	cover->x += (real)cos(away) * MIN(12.0f, reach * 2.0f);
	cover->y += (real)sin(away) * MIN(12.0f, reach * 2.0f);
	*surface = pgbots_ground(cover, cover);
	return *surface != NONE;
}

boolean pgbots_map_overlook(real_point3d const *point, long which, real_point3d *overlook, long *surface)
{
	static real const rings[3] = { 9.0f, 6.0f, 4.0f };
	long index, found = 0, ring, step;
	real_point3d at = *point;

	at.z += 0.5f;
	for (index = 0; index < MAP.power_count; index++)
	{
		real distance = pgbots_distance(&MAP.power[index].point, point);
		real_point3d eye = MAP.power[index].point;

		eye.z += 0.6f;
		if (distance < 4.0f || distance > 16.0f)
			continue;
		pgbots_globals.rays++;
		if (!bots_common_line_of_sight(NONE, &eye, &at, NONE))
			continue;
		if (found++ == which)
		{
			*overlook = MAP.power[index].point;
			*surface = MAP.power[index].surface;
			return TRUE;
		}
	}
	for (ring = 0; ring < 3; ring++)
	{
		for (step = 0; step < 8; step++)
		{
			real angle = (real)step * PGBOTS_PI / 4.0f + (real)ring * 0.4f;
			real_point3d candidate = *point;

			candidate.x += (real)cos(angle) * rings[ring];
			candidate.y += (real)sin(angle) * rings[ring];
			candidate.z += 1.0f;
			if (found++ != which)
				continue;
			*surface = pgbots_ground(&candidate, overlook);
			if (*surface != NONE)
				return TRUE;
			found--;
		}
	}
	return FALSE;
}

real pgbots_map_item_power(long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);

	if (!object)
		return 0.0f;
	if (TEST_FLAG(_object_mask_weapon, object->object.type))
	{
		real blast = 0.0f;
		short weapon_class = pgbots_weapon_class(object->definition_index, &blast);

		if (weapon_class == _pgbots_class_explosive)
			return blast >= 2.5f ? 1.0f : 0.6f;
		if (weapon_class == _pgbots_class_sniper)
			return 0.8f;
		return 0.0f;
	}
	if (TEST_FLAG(_object_mask_equipment, object->object.type))
	{
		switch (equipment_definition_get(object->definition_index)->equipment.powerup_type)
		{
		case _equipment_powerup_overshield: return 0.9f;
		case _equipment_powerup_active_camouflage: return 0.7f;
		}
	}
	return 0.0f;
}
