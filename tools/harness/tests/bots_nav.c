/*
BOTS_NAV.C (test)

The bots' ground mended and mapped (port/linux/game/bots_nav.c, compiled as
it is; test_bots_nav.py brings it): a made-up level of square surfaces in a
row - a floor, a ramp steeper than the walkable surfaces took, an upper
floor, then a lone ledge and a wall's top joined by no walkable edge -
opened as a biped walks (the ramp, the upper floor beyond it), the
vehicles' flatter share; its pieces numbered; and portals routed between
pieces, the shortest way through them.
*/

#include "harness.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LONG_MAX 0x7FFFFFFFL
#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct tag_block
{
	long count;
	void *address;
};
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])

typedef struct
{
	real_vector3d n;
	real d;
} real_plane3d;

struct collision_surface
{
	long plane_designator;
	long first_edge_index;
	byte flags;
	byte breakable_surface_index;
	short material_index;
};

struct collision_edge
{
	long vertex_indices[2];
	long edge_indices[2];
	long surface_indices[2];
};

struct bsp3d
{
	struct tag_block nodes;
	struct tag_block planes;
};

struct collision_vertex
{
	real_point3d point;
	long first_edge_index;
};

struct collision_bsp
{
	struct bsp3d bsp3d;
	struct tag_block surfaces;
	struct tag_block edges;
	struct tag_block vertices;
};

#include "under_test.inc"

#define WALK 6
#define SURFACES 7

/* a row of surfaces, each a single edge ring of two edges to its neighbours
(left and right): surface i's edges 2i (to i-1) and 2i+1 (to i+1) */
static struct collision_surface surfaces[SURFACES];
static struct collision_edge edges[SURFACES * 2];
static real_plane3d planes[4];
static struct collision_bsp bsp;

/* the planes: 0 flat, 1 a 40 degree ramp, 2 a 60 degree slope, 3 a wall */
static void level(long const *plane_of, byte const *flags_of, long const *joined_right)
{
	long index;

	planes[0].n.i = 0.0f, planes[0].n.j = 0.0f, planes[0].n.k = 1.0f;
	planes[1].n.i = 0.64f, planes[1].n.j = 0.0f, planes[1].n.k = 0.77f;
	planes[2].n.i = 0.87f, planes[2].n.j = 0.0f, planes[2].n.k = 0.5f;
	planes[3].n.i = 1.0f, planes[3].n.j = 0.0f, planes[3].n.k = 0.0f;
	memset(edges, 0, sizeof(edges));
	for (index = 0; index < SURFACES; index++)
	{
		long left = index * 2, right = index * 2 + 1;

		surfaces[index].plane_designator = plane_of[index];
		surfaces[index].first_edge_index = left;
		surfaces[index].flags = flags_of[index];
		/* (the ring: left to right and back; the edges' surfaces this
		surface on side 0, the neighbour on side 1) */
		edges[left].edge_indices[0] = right;
		edges[right].edge_indices[0] = left;
		edges[left].surface_indices[0] = index;
		edges[right].surface_indices[0] = index;
		edges[left].surface_indices[1] = index > 0 && joined_right[index - 1] ? index - 1 : NONE;
		edges[right].surface_indices[1] = index < SURFACES - 1 && joined_right[index] ? index + 1 : NONE;
	}
	bsp.bsp3d.planes.count = 4;
	bsp.bsp3d.planes.address = planes;
	bsp.surfaces.count = SURFACES;
	bsp.surfaces.address = surfaces;
	bsp.edges.count = SURFACES * 2;
	bsp.edges.address = edges;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	/* floor, ramp 40, upper floor, 60 degree slope, floor beyond it,
	glass, a wall's top joined to nothing */
	static long const plane_of[SURFACES] = { 0, 1, 0, 2, 0, 0, 0 };
	static byte const flags_of[SURFACES] = { 0, 0, 0, 0, 0, 8, 0 };
	static long const joined_right[SURFACES] = { 1, 1, 1, 1, 1, 0, 0 };

	CASE("open")
	{
		byte walkable[SURFACES] = { FLAG(WALK), 0, 0, 0, 0, 0, 0 };
		byte vehicle[SURFACES] = { FLAG(WALK), 0, 0, 0, 0, 0, 0 };
		long vehicle_opened = 0, opened;

		level(plane_of, flags_of, joined_right);
		/* (a biped's limit about 50 degrees, a vehicle's about 35) */
		opened = bots_nav_open_ramps(&bsp, walkable, vehicle, SURFACES, WALK, 0.65f, 0.82f, &vehicle_opened);
		CHECK(TEST_FLAG(walkable[1], WALK), "the 40 degree ramp is not opened");
		CHECK(TEST_FLAG(walkable[2], WALK), "the upper floor beyond the ramp is not opened");
		CHECK(!TEST_FLAG(walkable[3], WALK), "the 60 degree slope is opened");
		CHECK(!TEST_FLAG(walkable[4], WALK), "the floor beyond the steep slope is opened");
		CHECK(!TEST_FLAG(walkable[6], WALK), "a surface joined to no walkable one is opened");
		CHECK(opened == 2, "opened %ld, not 2", opened);
		CHECK(!TEST_FLAG(vehicle[1], WALK), "the 40 degree ramp is opened to vehicles");
		CHECK(TEST_FLAG(vehicle[2], WALK) && vehicle_opened == 1, "the flat upper floor is not opened to vehicles");
		/* (a flipped plane faces down: a ceiling, not opened) */
		{
			long flipped[SURFACES] = { 0, 1 | (long)0x80000000UL, 0, 2, 0, 0, 0 };
			byte again[SURFACES] = { FLAG(WALK), 0, 0, 0, 0, 0, 0 };

			level(flipped, flags_of, joined_right);
			bots_nav_open_ramps(&bsp, again, NULL, SURFACES, WALK, 0.65f, 0.82f, &vehicle_opened);
			CHECK(!TEST_FLAG(again[1], WALK), "a downward face is opened");
		}
		/* (glass is never opened) */
		{
			long flat[SURFACES] = { 0, 0, 0, 0, 0, 0, 0 };
			byte again[SURFACES] = { FLAG(WALK), FLAG(WALK), FLAG(WALK), FLAG(WALK), FLAG(WALK), 0, 0 };
			long all_joined[SURFACES] = { 1, 1, 1, 1, 1, 1, 0 };

			level(flat, flags_of, all_joined);
			bots_nav_open_ramps(&bsp, again, NULL, SURFACES, WALK, 0.65f, 0.82f, &vehicle_opened);
			CHECK(!TEST_FLAG(again[5], WALK), "breakable glass is opened");
		}
	}
	CASE("components")
	{
		byte walkable[SURFACES] = { FLAG(WALK), FLAG(WALK), FLAG(WALK), 0, FLAG(WALK), FLAG(WALK), FLAG(WALK) };
		short component[SURFACES];
		long pieces;

		level(plane_of, flags_of, joined_right);
		pieces = bots_nav_components(&bsp, walkable, SURFACES, WALK, component);
		CHECK(pieces == 3, "%ld pieces, not 3", pieces);
		CHECK(component[0] == component[1] && component[1] == component[2], "the joined floor is split");
		CHECK(component[3] == NONE, "a surface not walkable has a piece");
		CHECK(component[4] == component[5] && component[4] != component[0], "the far floor is not its own piece");
		CHECK(component[6] != component[5] && component[6] != NONE, "the lone surface is not its own piece");
	}
	CASE("route")
	{
		/* pieces 0 (0-2), 1 (4-5), 2 (6): a ladder 0 -> 1, a teleporter 1 -> 2,
		a long teleporter 0 -> 2, an unusable one 0 -> 2 right by the start */
		short component[SURFACES] = { 0, 0, 0, NONE, 1, 1, 2 };
		struct bots_nav_portal portals[4];
		real_point3d start = { 0.0f, 0.0f, 0.0f }, goal = { 30.0f, 0.0f, 0.0f };
		long first;

		memset(portals, 0, sizeof(portals));
		portals[0].entrance_surface_index = 1, portals[0].exit_surface_index = 4, portals[0].usable = TRUE;
		portals[0].entrance.x = 2.0f, portals[0].exit.x = 10.0f;
		portals[1].entrance_surface_index = 5, portals[1].exit_surface_index = 6, portals[1].usable = TRUE;
		portals[1].entrance.x = 12.0f, portals[1].exit.x = 29.0f;
		portals[2].entrance_surface_index = 2, portals[2].exit_surface_index = 6, portals[2].usable = TRUE;
		portals[2].entrance.x = -40.0f, portals[2].exit.x = 31.0f;
		portals[3].entrance_surface_index = 0, portals[3].exit_surface_index = 6, portals[3].usable = FALSE;
		portals[3].entrance.x = 0.5f, portals[3].exit.x = 30.0f;
		first = bots_nav_route(component, SURFACES, portals, 4, 0, &start, 6, &goal);
		CHECK(first == 0, "the way through the ladder then the teleporter is not taken (%ld)", first);
		portals[0].usable = FALSE;
		first = bots_nav_route(component, SURFACES, portals, 4, 0, &start, 6, &goal);
		CHECK(first == 2, "the long teleporter is not taken with the ladder out (%ld)", first);
		first = bots_nav_route(component, SURFACES, portals, 4, 0, &start, 2, &goal);
		CHECK(first == NONE, "a goal on the same piece is routed through a portal");
		first = bots_nav_route(component, SURFACES, portals, 4, 0, &start, 3, &goal);
		CHECK(first == NONE, "a goal on no piece is routed");
		portals[2].usable = FALSE;
		first = bots_nav_route(component, SURFACES, portals, 4, 0, &start, 6, &goal);
		CHECK(first == NONE, "a goal no usable portal leads to is routed (%ld)", first);
	}
	CASE("coarse")
	{
		/* a U of triangles 6 units apart: along y 0 from x 0 to 60, up x 60
		to y 12, back along y 12 to x 0 - the goal 12 units from the start
		across a wall, 132 along the way - and a triangle apart */
		enum { CHAIN = 24, ALL = CHAIN + 1 };
		static struct collision_surface u_surfaces[ALL];
		static struct collision_edge u_edges[ALL * 3];
		static struct collision_vertex u_vertices[ALL * 3];
		static byte u_walkable[ALL];
		real_point3d centers[ALL];
		struct bots_nav_coarse *coarse;
		real_point3d waypoint;
		long surface = NONE, index;

		for (index = 0; index < ALL; index++)
		{
			long corner;

			if (index <= 10)
				centers[index].x = 6.0f * (real)index, centers[index].y = 0.0f;
			else if (index == 11)
				centers[index].x = 60.0f, centers[index].y = 6.0f;
			else if (index < CHAIN)
				centers[index].x = 60.0f - 6.0f * (real)(index - 12), centers[index].y = 12.0f;
			else
				centers[index].x = 200.0f, centers[index].y = 200.0f;
			centers[index].z = 0.0f;
			u_surfaces[index].first_edge_index = index * 3;
			u_walkable[index] = FLAG(WALK);
			for (corner = 0; corner < 3; corner++)
			{
				struct collision_edge *edge = &u_edges[index * 3 + corner];
				real angle = (real)corner * 2.0944f;

				u_vertices[index * 3 + corner].point.x = centers[index].x + (real)cos(angle);
				u_vertices[index * 3 + corner].point.y = centers[index].y + (real)sin(angle);
				u_vertices[index * 3 + corner].point.z = 0.0f;
				edge->vertex_indices[0] = index * 3 + corner;
				edge->vertex_indices[1] = index * 3 + (corner + 1) % 3;
				edge->edge_indices[0] = index * 3 + (corner + 1) % 3;
				edge->surface_indices[0] = index;
				edge->surface_indices[1] = NONE;
				if (index < CHAIN && corner == 0 && index > 0)
					edge->surface_indices[1] = index - 1;
				if (index < CHAIN && corner == 1 && index < CHAIN - 1)
					edge->surface_indices[1] = index + 1;
			}
		}
		bsp.surfaces.count = ALL;
		bsp.surfaces.address = u_surfaces;
		bsp.edges.count = ALL * 3;
		bsp.edges.address = u_edges;
		bsp.vertices.count = ALL * 3;
		bsp.vertices.address = u_vertices;
		coarse = bots_nav_coarse_build(&bsp, u_walkable, ALL, WALK);
		CHECK(coarse && bots_nav_coarse_cells(coarse) == ALL, "%ld cells, not %d", bots_nav_coarse_cells(coarse), ALL);
		CHECK(bots_nav_coarse_waypoint(coarse, 0, CHAIN - 1, 25.0f, &waypoint, &surface),
			"no waypoint to a goal across the wall");
		CHECK(fabs(waypoint.y) < 1.0f && waypoint.x > 17.0f && waypoint.x < 31.0f,
			"the waypoint (%.1f %.1f) is not about 25 units along the way (24 0)", waypoint.x, waypoint.y);
		CHECK(surface == 4, "the waypoint's surface %ld, not 4", surface);
		CHECK(!bots_nav_coarse_waypoint(coarse, 0, 3, 25.0f, &waypoint, &surface), "a waypoint to a goal near along the way");
		CHECK(!bots_nav_coarse_waypoint(coarse, 0, CHAIN, 25.0f, &waypoint, &surface), "a waypoint to a goal no cell leads to");
		bots_nav_coarse_free(coarse);
	}
	return 0;
}
