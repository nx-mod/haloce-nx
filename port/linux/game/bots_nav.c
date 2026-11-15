/*
BOTS_NAV.C

(port) The ground the bots' searches walk, mended and mapped (bots_nav.h).
*/

#include "cseries.h"
#include "tag_files/tag_groups.h"
#include "physics/collision_bsp_definitions.h"

#include "bots_nav.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* collision_surface's flags (collision_bsp.c's) */
#define BOTS_NAV_TWO_SIDED_BIT 0
#define BOTS_NAV_CLIMBABLE_BIT 2
#define BOTS_NAV_BREAKABLE_BIT 3
#define BOTS_NAV_MAXIMUM_NEIGHBOURS 16
/* (the game's free asserts on NULL: debug_memory.c) */
#define BOTS_NAV_FREE(pointer) do { if (pointer) free(pointer); } while (0)

/* the surfaces across a surface's edges */
static long bots_nav_neighbours(struct collision_bsp const *bsp, long surface_index, long *neighbours)
{
	struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index,
		struct collision_surface);
	long edge_index = surface->first_edge_index, count = 0, steps = 0;

	do
	{
		struct collision_edge const *edge;
		boolean right;
		long other;

		if (edge_index < 0 || edge_index >= bsp->edges.count)
			break;
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		right = edge->surface_indices[1] == surface_index;
		other = edge->surface_indices[!right];
		if (other != NONE && other >= 0 && other < bsp->surfaces.count && count < BOTS_NAV_MAXIMUM_NEIGHBOURS)
			neighbours[count++] = other;
		edge_index = edge->edge_indices[right];
	} while (edge_index != surface->first_edge_index && ++steps < 64);
	return count;
}

/* how upright the surface's face is (its normal's upward part; a two-sided
one either face's) */
static real bots_nav_surface_k(struct collision_bsp const *bsp, long surface_index)
{
	struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index,
		struct collision_surface);
	long plane_index = surface->plane_designator & LONG_MAX;
	real k;

	if (plane_index < 0 || plane_index >= bsp->bsp3d.planes.count)
		return -1.0f;
	k = TAG_BLOCK_GET_ELEMENT(&bsp->bsp3d.planes, plane_index, real_plane3d)->n.k;
	if (surface->plane_designator < 0)
		k = -k;
	if (TEST_FLAG(surface->flags, BOTS_NAV_TWO_SIDED_BIT))
		k = (real)fabs(k);
	return k;
}

long bots_nav_open_ramps(struct collision_bsp const *bsp, byte *walkable, byte *vehicle_walkable, long count,
	short walkable_bit, real minimum_k, real vehicle_minimum_k, long *vehicle_opened)
{
	long *queue;
	long head = 0, tail = 0, opened = 0, surface_index;

	*vehicle_opened = 0;
	if (!bsp || count <= 0)
		return 0;
	count = MIN(count, bsp->surfaces.count);
	queue = (long *)malloc((size_t)count * sizeof(long));
	if (!queue)
		return 0;
	for (surface_index = 0; surface_index < count; surface_index++)
	{
		if (TEST_FLAG(walkable[surface_index], walkable_bit))
			queue[tail++] = surface_index;
	}
	while (head < tail)
	{
		long neighbours[BOTS_NAV_MAXIMUM_NEIGHBOURS];
		long neighbour_count = bots_nav_neighbours(bsp, queue[head++], neighbours), index;

		for (index = 0; index < neighbour_count; index++)
		{
			long other = neighbours[index];
			struct collision_surface const *surface;
			real k;

			if (other >= count || TEST_FLAG(walkable[other], walkable_bit))
				continue;
			surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, other, struct collision_surface);
			if (TEST_FLAG(surface->flags, BOTS_NAV_CLIMBABLE_BIT) || TEST_FLAG(surface->flags, BOTS_NAV_BREAKABLE_BIT))
				continue;
			k = bots_nav_surface_k(bsp, other);
			if (k < minimum_k)
				continue;
			walkable[other] |= (byte)FLAG(walkable_bit);
			opened++;
			if (vehicle_walkable && k >= vehicle_minimum_k)
			{
				vehicle_walkable[other] |= (byte)FLAG(walkable_bit);
				(*vehicle_opened)++;
			}
			if (tail < count)
				queue[tail++] = other;
		}
	}
	BOTS_NAV_FREE(queue);
	return opened;
}

long bots_nav_components(struct collision_bsp const *bsp, byte const *walkable, long count, short walkable_bit,
	short *component)
{
	long *queue;
	long pieces = 0, surface_index;

	for (surface_index = 0; surface_index < count; surface_index++)
		component[surface_index] = NONE;
	if (!bsp || count <= 0)
		return 0;
	queue = (long *)malloc((size_t)count * sizeof(long));
	if (!queue)
		return 0;
	for (surface_index = 0; surface_index < count && surface_index < bsp->surfaces.count; surface_index++)
	{
		long head = 0, tail = 0;

		if (component[surface_index] != NONE || !TEST_FLAG(walkable[surface_index], walkable_bit))
			continue;
		/* (more pieces than a short holds: the rest one piece, which keeps
		every way through portals open) */
		component[surface_index] = (short)MIN(pieces, 32766);
		queue[tail++] = surface_index;
		while (head < tail)
		{
			long neighbours[BOTS_NAV_MAXIMUM_NEIGHBOURS];
			long neighbour_count = bots_nav_neighbours(bsp, queue[head++], neighbours), index;

			for (index = 0; index < neighbour_count; index++)
			{
				long other = neighbours[index];

				if (other >= count || component[other] != NONE || !TEST_FLAG(walkable[other], walkable_bit))
					continue;
				component[other] = component[surface_index];
				if (tail < count)
					queue[tail++] = other;
			}
		}
		pieces++;
	}
	BOTS_NAV_FREE(queue);
	return pieces;
}

static short bots_nav_component_of(short const *component, long count, long surface_index)
{
	return surface_index >= 0 && surface_index < count ? component[surface_index] : NONE;
}

static real bots_nav_distance(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

#define BOTS_NAV_MAXIMUM_PORTALS 64

long bots_nav_route(short const *component, long count, struct bots_nav_portal const *portals, long portal_count,
	long start_surface_index, real_point3d const *start, long goal_surface_index, real_point3d const *goal)
{
	real cost[BOTS_NAV_MAXIMUM_PORTALS];
	long first[BOTS_NAV_MAXIMUM_PORTALS];
	boolean done[BOTS_NAV_MAXIMUM_PORTALS];
	short start_piece = bots_nav_component_of(component, count, start_surface_index);
	short goal_piece = bots_nav_component_of(component, count, goal_surface_index);
	real best_cost = 1e30f;
	long best_first = NONE, index;

	if (!component || start_piece == NONE || goal_piece == NONE || start_piece == goal_piece)
		return NONE;
	portal_count = MIN(portal_count, BOTS_NAV_MAXIMUM_PORTALS);
	for (index = 0; index < portal_count; index++)
	{
		cost[index] = 1e30f;
		first[index] = NONE;
		done[index] = FALSE;
		if (portals[index].usable &&
			bots_nav_component_of(component, count, portals[index].entrance_surface_index) == start_piece &&
			bots_nav_component_of(component, count, portals[index].exit_surface_index) != NONE &&
			bots_nav_component_of(component, count, portals[index].exit_surface_index) != start_piece)
		{
			cost[index] = bots_nav_distance(start, &portals[index].entrance);
			first[index] = index;
		}
	}
	/* (Dijkstra's over the portals: from one's exit to another's entrance on
	the same piece, by straight lines) */
	for (;;)
	{
		long current = NONE, next;
		short exit_piece;

		for (index = 0; index < portal_count; index++)
		{
			if (!done[index] && cost[index] < 1e29f && (current == NONE || cost[index] < cost[current]))
				current = index;
		}
		if (current == NONE || cost[current] >= best_cost)
			break;
		done[current] = TRUE;
		exit_piece = bots_nav_component_of(component, count, portals[current].exit_surface_index);
		if (exit_piece == goal_piece)
		{
			real total = cost[current] + bots_nav_distance(&portals[current].exit, goal);

			if (total < best_cost)
			{
				best_cost = total;
				best_first = first[current];
			}
			continue;
		}
		for (next = 0; next < portal_count; next++)
		{
			real through;

			if (done[next] || !portals[next].usable ||
				bots_nav_component_of(component, count, portals[next].entrance_surface_index) != exit_piece)
			{
				continue;
			}
			through = cost[current] + bots_nav_distance(&portals[current].exit, &portals[next].entrance);
			if (through < cost[next])
			{
				cost[next] = through;
				first[next] = first[current];
			}
		}
	}
	return best_first;
}

/* ---------- the coarse map */

#define BOTS_NAV_CELL_SIZE 6.0f
#define BOTS_NAV_CELL_HEIGHT 3.0f
#define BOTS_NAV_MAXIMUM_CELLS 4096
#define BOTS_NAV_CELL_LINKS 12
#define BOTS_NAV_HASH_SIZE 8192

struct bots_nav_coarse
{
	long cell_count;
	long surface_count;
	/* a cell's ground (its first surface's middle) and that surface, its
	links; each surface's cell (NONE) */
	real_point3d *center;
	long *surface;
	short *links;
	byte *link_count;
	short *cell_of;
	/* the search's own (the tick's alone) */
	real *cost;
	short *parent;
	byte *closed;
	short *heap;
	real *heap_cost;
};

/* a surface's middle (its ring's corners' mean); FALSE for one with fewer
than three */
static boolean bots_nav_surface_middle(struct collision_bsp const *bsp, long surface_index, real_point3d *middle)
{
	struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index,
		struct collision_surface);
	long edge_index = surface->first_edge_index, count = 0, steps = 0;

	middle->x = middle->y = middle->z = 0.0f;
	do
	{
		struct collision_edge const *edge;
		boolean right;
		long vertex;

		if (edge_index < 0 || edge_index >= bsp->edges.count)
			break;
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		right = edge->surface_indices[1] == surface_index;
		vertex = edge->vertex_indices[right];
		if (vertex >= 0 && vertex < bsp->vertices.count)
		{
			real_point3d const *point = &TAG_BLOCK_GET_ELEMENT(&bsp->vertices, vertex, struct collision_vertex)->point;

			middle->x += point->x;
			middle->y += point->y;
			middle->z += point->z;
			count++;
		}
		edge_index = edge->edge_indices[right];
	} while (edge_index != surface->first_edge_index && ++steps < 64);
	if (count < 3)
		return FALSE;
	middle->x /= (real)count;
	middle->y /= (real)count;
	middle->z /= (real)count;
	return TRUE;
}

static void bots_nav_coarse_link(struct bots_nav_coarse *coarse, short a, short b)
{
	long index;

	for (index = 0; index < coarse->link_count[a]; index++)
	{
		if (coarse->links[a * BOTS_NAV_CELL_LINKS + index] == b)
			return;
	}
	if (coarse->link_count[a] < BOTS_NAV_CELL_LINKS)
		coarse->links[a * BOTS_NAV_CELL_LINKS + coarse->link_count[a]++] = b;
}

void bots_nav_coarse_free(struct bots_nav_coarse *coarse)
{
	if (!coarse)
		return;
	BOTS_NAV_FREE(coarse->center);
	BOTS_NAV_FREE(coarse->surface);
	BOTS_NAV_FREE(coarse->links);
	BOTS_NAV_FREE(coarse->link_count);
	BOTS_NAV_FREE(coarse->cell_of);
	BOTS_NAV_FREE(coarse->cost);
	BOTS_NAV_FREE(coarse->parent);
	BOTS_NAV_FREE(coarse->closed);
	BOTS_NAV_FREE(coarse->heap);
	BOTS_NAV_FREE(coarse->heap_cost);
	BOTS_NAV_FREE(coarse);
}

long bots_nav_coarse_cells(struct bots_nav_coarse const *coarse)
{
	return coarse ? coarse->cell_count : 0;
}

struct bots_nav_coarse *bots_nav_coarse_build(struct collision_bsp const *bsp, byte const *walkable, long count,
	short walkable_bit)
{
	struct bots_nav_coarse *coarse;
	long *hash_keys;
	short *hash_cells;
	long surface_index;

	if (!bsp || count <= 0)
		return NULL;
	count = MIN(count, bsp->surfaces.count);
	/* (the game's malloc, which its free takes - cseries.h - not calloc, the
	C library's, which the game's free asserts on) */
	coarse = (struct bots_nav_coarse *)malloc(sizeof(*coarse));
	if (coarse)
		memset(coarse, 0, sizeof(*coarse));
	hash_keys = (long *)malloc(BOTS_NAV_HASH_SIZE * sizeof(long));
	hash_cells = (short *)malloc(BOTS_NAV_HASH_SIZE * sizeof(short));
	if (coarse)
	{
		coarse->surface_count = count;
		coarse->center = (real_point3d *)malloc(BOTS_NAV_MAXIMUM_CELLS * sizeof(real_point3d));
		coarse->surface = (long *)malloc(BOTS_NAV_MAXIMUM_CELLS * sizeof(long));
		coarse->links = (short *)malloc(BOTS_NAV_MAXIMUM_CELLS * BOTS_NAV_CELL_LINKS * sizeof(short));
		coarse->link_count = (byte *)malloc(BOTS_NAV_MAXIMUM_CELLS);
		if (coarse->link_count)
			memset(coarse->link_count, 0, BOTS_NAV_MAXIMUM_CELLS);
		coarse->cell_of = (short *)malloc((size_t)count * sizeof(short));
		coarse->cost = (real *)malloc(BOTS_NAV_MAXIMUM_CELLS * sizeof(real));
		coarse->parent = (short *)malloc(BOTS_NAV_MAXIMUM_CELLS * sizeof(short));
		coarse->closed = (byte *)malloc(BOTS_NAV_MAXIMUM_CELLS);
		coarse->heap = (short *)malloc(BOTS_NAV_MAXIMUM_CELLS * BOTS_NAV_CELL_LINKS * sizeof(short));
		coarse->heap_cost = (real *)malloc(BOTS_NAV_MAXIMUM_CELLS * BOTS_NAV_CELL_LINKS * sizeof(real));
	}
	if (!coarse || !hash_keys || !hash_cells || !coarse->center || !coarse->surface || !coarse->links ||
		!coarse->link_count || !coarse->cell_of || !coarse->cost || !coarse->parent || !coarse->closed || !coarse->heap ||
		!coarse->heap_cost)
	{
		BOTS_NAV_FREE(hash_keys);
		BOTS_NAV_FREE(hash_cells);
		bots_nav_coarse_free(coarse);
		return NULL;
	}
	for (surface_index = 0; surface_index < BOTS_NAV_HASH_SIZE; surface_index++)
		hash_cells[surface_index] = NONE;
	/* each walkable surface into its cell (by its middle) */
	for (surface_index = 0; surface_index < count; surface_index++)
	{
		real_point3d middle;
		long x, y, z, key, slot, probes;

		coarse->cell_of[surface_index] = NONE;
		if (!TEST_FLAG(walkable[surface_index], walkable_bit) || !bots_nav_surface_middle(bsp, surface_index, &middle))
			continue;
		x = (long)floor(middle.x / BOTS_NAV_CELL_SIZE) & 0x3FF;
		y = (long)floor(middle.y / BOTS_NAV_CELL_SIZE) & 0x3FF;
		z = (long)floor(middle.z / BOTS_NAV_CELL_HEIGHT) & 0x3FF;
		key = (x << 20) | (y << 10) | z;
		slot = (long)(((unsigned long)key * 2654435761UL) % BOTS_NAV_HASH_SIZE);
		for (probes = 0; probes < BOTS_NAV_HASH_SIZE; probes++, slot = (slot + 1) % BOTS_NAV_HASH_SIZE)
		{
			if (hash_cells[slot] == NONE || hash_keys[slot] == key)
				break;
		}
		if (probes >= BOTS_NAV_HASH_SIZE)
			continue;
		if (hash_cells[slot] == NONE)
		{
			if (coarse->cell_count >= BOTS_NAV_MAXIMUM_CELLS)
				continue;
			hash_keys[slot] = key;
			hash_cells[slot] = (short)coarse->cell_count;
			coarse->center[coarse->cell_count] = middle;
			coarse->surface[coarse->cell_count] = surface_index;
			coarse->cell_count++;
		}
		coarse->cell_of[surface_index] = hash_cells[slot];
	}
	BOTS_NAV_FREE(hash_keys);
	BOTS_NAV_FREE(hash_cells);
	/* the cells joined where their surfaces meet */
	for (surface_index = 0; surface_index < count; surface_index++)
	{
		long neighbours[BOTS_NAV_MAXIMUM_NEIGHBOURS];
		long neighbour_count, index;
		short cell = coarse->cell_of[surface_index];

		if (cell == NONE)
			continue;
		neighbour_count = bots_nav_neighbours(bsp, surface_index, neighbours);
		for (index = 0; index < neighbour_count; index++)
		{
			long other = neighbours[index];
			short other_cell = other < count ? coarse->cell_of[other] : NONE;

			if (other_cell != NONE && other_cell != cell)
			{
				bots_nav_coarse_link(coarse, cell, other_cell);
				bots_nav_coarse_link(coarse, other_cell, cell);
			}
		}
	}
	return coarse;
}

/* the shortest way through the cells from the start's to the goal's
(coarse->cost and coarse->parent hold it); FALSE for none */
static boolean bots_nav_coarse_search(struct bots_nav_coarse *coarse, long start_surface_index,
	long goal_surface_index, short *start_cell, short *goal_cell)
{
	short start, goal;
	long heap_count = 0, heap_capacity, index;

	if (!coarse || start_surface_index < 0 || start_surface_index >= coarse->surface_count ||
		goal_surface_index < 0 || goal_surface_index >= coarse->surface_count)
	{
		return FALSE;
	}
	start = coarse->cell_of[start_surface_index];
	goal = coarse->cell_of[goal_surface_index];
	*start_cell = start;
	*goal_cell = goal;
	if (start == NONE || goal == NONE || start == goal)
		return FALSE;
	heap_capacity = BOTS_NAV_MAXIMUM_CELLS * BOTS_NAV_CELL_LINKS;
	for (index = 0; index < coarse->cell_count; index++)
	{
		coarse->cost[index] = 1e30f;
		coarse->parent[index] = NONE;
		coarse->closed[index] = FALSE;
	}
	/* A*, the heap's entries (cell, cost to it and on in a straight line),
	a cell's later and dearer entries skipped once it is closed */
	coarse->cost[start] = 0.0f;
	coarse->heap[heap_count] = start;
	coarse->heap_cost[heap_count++] = bots_nav_distance(&coarse->center[start], &coarse->center[goal]);
	while (heap_count > 0)
	{
		long at = 0;
		short current = coarse->heap[0];

		/* (the least off the heap) */
		heap_count--;
		coarse->heap[0] = coarse->heap[heap_count];
		coarse->heap_cost[0] = coarse->heap_cost[heap_count];
		for (;;)
		{
			long child = at * 2 + 1, least = at;

			if (child < heap_count && coarse->heap_cost[child] < coarse->heap_cost[least])
				least = child;
			if (child + 1 < heap_count && coarse->heap_cost[child + 1] < coarse->heap_cost[least])
				least = child + 1;
			if (least == at)
				break;
			{
				short cell_swap = coarse->heap[at];
				real cost_swap = coarse->heap_cost[at];

				coarse->heap[at] = coarse->heap[least];
				coarse->heap_cost[at] = coarse->heap_cost[least];
				coarse->heap[least] = cell_swap;
				coarse->heap_cost[least] = cost_swap;
			}
			at = least;
		}
		if (coarse->closed[current])
			continue;
		coarse->closed[current] = TRUE;
		if (current == goal)
			break;
		for (index = 0; index < coarse->link_count[current]; index++)
		{
			short next = coarse->links[current * BOTS_NAV_CELL_LINKS + index];
			real through = coarse->cost[current] + bots_nav_distance(&coarse->center[current], &coarse->center[next]);

			if (coarse->closed[next] || through >= coarse->cost[next] || heap_count >= heap_capacity)
				continue;
			coarse->cost[next] = through;
			coarse->parent[next] = current;
			/* (onto the heap) */
			at = heap_count++;
			coarse->heap[at] = next;
			coarse->heap_cost[at] = through + bots_nav_distance(&coarse->center[next], &coarse->center[goal]);
			while (at > 0 && coarse->heap_cost[(at - 1) / 2] > coarse->heap_cost[at])
			{
				long up = (at - 1) / 2;
				short cell_swap = coarse->heap[at];
				real cost_swap = coarse->heap_cost[at];

				coarse->heap[at] = coarse->heap[up];
				coarse->heap_cost[at] = coarse->heap_cost[up];
				coarse->heap[up] = cell_swap;
				coarse->heap_cost[up] = cost_swap;
				at = up;
			}
		}
	}
	return coarse->closed[goal];
}

boolean bots_nav_coarse_waypoint(struct bots_nav_coarse *coarse, long start_surface_index,
	long goal_surface_index, real ahead, real_point3d *waypoint, long *waypoint_surface_index)
{
	short start, goal, cell;

	if (!bots_nav_coarse_search(coarse, start_surface_index, goal_surface_index, &start, &goal))
		return FALSE;
	/* back from the goal: the first cell no further than `ahead` along the
	way from the start */
	if (coarse->cost[goal] < ahead)
		return FALSE;
	cell = goal;
	while (coarse->parent[cell] != NONE && coarse->cost[cell] > ahead)
		cell = coarse->parent[cell];
	if (cell == start)
		return FALSE;
	*waypoint = coarse->center[cell];
	*waypoint_surface_index = coarse->surface[cell];
	return TRUE;
}

boolean bots_nav_coarse_along(struct bots_nav_coarse *coarse, long start_surface_index, long goal_surface_index,
	real fraction, real_point3d *point, long *point_surface_index, real *length)
{
	short start, goal, cell;

	if (!bots_nav_coarse_search(coarse, start_surface_index, goal_surface_index, &start, &goal))
		return FALSE;
	*length = coarse->cost[goal];
	cell = goal;
	while (coarse->parent[cell] != NONE && coarse->cost[cell] > fraction * coarse->cost[goal])
		cell = coarse->parent[cell];
	*point = coarse->center[cell];
	*point_surface_index = coarse->surface[cell];
	return TRUE;
}
