/*
OBJECT_BOUNDS_CACHE.C

The objects' bounding spheres packed by absolute index, so collision's object
loops (collisions.c) pass over the objects of a crowded cluster that are far
from a sphere, or that a vector misses, without reading each one. Written by
their only writer (object_compute_node_matrices); an entry counts only for its
datum index and until the game state is next replaced (game_state.c). The
same tests on the same values: what the loops find does not change. (Kept out
of the game state, whose layout the saved games share.)
*/

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"

struct object_bounds
{
	long object_index;
	long epoch;
	real_point3d center;
	real radius;
	long type;
};

static struct object_bounds object_bounds[MAXIMUM_OBJECTS_PER_MAP];
static long object_bounds_epoch = 1;

void object_bounds_cache_update(
	long object_index,
	short type,
	real_point3d const *center,
	real radius)
{
	struct object_bounds *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	bounds->object_index = object_index;
	bounds->epoch = object_bounds_epoch;
	bounds->center = *center;
	bounds->radius = radius;
	bounds->type = type;
}

void object_bounds_cache_invalidate(
	void)
{
	object_bounds_epoch++;
}

/* whether the object's bounding sphere is known to be out of the reach of
the point (collision's own test on the copy); FALSE when it is not known */
boolean object_bounds_cache_out_of_reach(
	long object_index,
	real_point3d const *point,
	real radius)
{
	struct object_bounds const *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	return bounds->object_index == object_index && bounds->epoch == object_bounds_epoch &&
		!point_in_sphere(point, &bounds->center, bounds->radius + radius);
}

/* whether the vector is known to pass the object by, as collision's vector
test would find it (the type not among the flags', or the bounding sphere
missed: the same test on the copy); FALSE when it is not known. Only for an
object in a cluster's list, which has no siblings to be tested with it. */
boolean object_bounds_cache_vector_misses(
	long object_index,
	unsigned long flags,
	short first_type_bit,
	real_point3d const *point,
	real_vector3d const *vector)
{
	struct object_bounds const *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	return bounds->object_index == object_index && bounds->epoch == object_bounds_epoch &&
		(!TEST_FLAG(flags, bounds->type + first_type_bit) ||
			!fast_vector_intersects_sphere(point, vector, &bounds->center, bounds->radius));
}
