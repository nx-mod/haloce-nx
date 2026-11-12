#include "harness.h"
#include <math.h>

#include "config.inc"

/* One object of two nodes, drawn between the game's ticks (render_interpolation.c):
each tick keeps its pose, unless it is the pose both snapshots already hold, and a
frame draws the blend of the last two, or the pose as it is when they are the same. */
#define OBJECT 0x10002L
#define NODES 2

struct object_iterator { long index; int done; };
struct object_datum
{
	struct
	{
		unsigned long damage_flags;
		struct { short size; } node_matrices;
	} object;
};
enum { _object_dead_bit, _object_mask_all = -1 };
struct observer_result { real_point3d position; };
static real_vector3d zero;
static real_vector3d const *global_zero_vector3d = &zero;
static struct object_datum object = { { 0, { NODES * sizeof(real_matrix4x3) } } };
static real_matrix4x3 nodes[NODES];
static long interpolation_tick, interpolation_frame;
static boolean interpolation_rendering = TRUE, interpolation_drawing = TRUE;
static real interpolation_fraction;

static boolean halo_interpolation_enabled(void) { return TRUE; }
static void object_iterator_new(struct object_iterator *iterator, long mask, long flags) { iterator->done = FALSE; }
static struct object_datum *object_iterator_next(struct object_iterator *iterator)
{
	if (iterator->done)
		return NULL;
	iterator->done = TRUE;
	iterator->index = OBJECT;
	return &object;
}
static real_matrix4x3 *object_get_node_matrices(long index) { return nodes; }
static real_matrix4x3 *biped_ragdoll_drawn_nodes(long index) { return NULL; }

#include "under_test.inc"

/* both nodes so far along x, the second a unit past the first */
static void pose(real x)
{
	short node;

	for (node = 0; node < NODES; node++)
	{
		memset(&nodes[node], 0, sizeof(nodes[node]));
		nodes[node].scale = 1.0f;
		nodes[node].forward.i = 1.0f;
		nodes[node].left.j = 1.0f;
		nodes[node].up.k = 1.0f;
		nodes[node].position.x = x + node;
	}
}

static void tick(real x)
{
	pose(x);
	render_interpolation_tick();
}

/* where the root is drawn a fraction into the tick */
static real drawn(real fraction)
{
	real_matrix4x3 *blended;

	interpolation_frame++;
	interpolation_fraction = fraction;
	blended = interpolated_object_node_matrices(OBJECT);
	CHECK(blended, "drawn from its snapshots");
	return blended[0].position.x;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct interpolated_object *record;
	byte latest;

	if (!strcmp(case_name, "moving"))
	{
		tick(0.0f);
		tick(1.0f);
		CHECK(fabsf(drawn(0.5f) - 0.5f) < 1e-5f, "a moving object is drawn between its ticks");
	}
	else if (!strcmp(case_name, "stops"))
	{
		tick(0.0f);
		tick(1.0f);
		tick(1.0f);
		CHECK(drawn(0.5f) == 1.0f, "once it stops, it is drawn where it stopped, not between its last two places");
		tick(1.0f);
		CHECK(drawn(0.5f) == 1.0f, "and stays there");
	}
	else if (!strcmp(case_name, "still"))
	{
		tick(2.0f);
		tick(2.0f);
		record = &interpolated_objects[OBJECT & 0xFFFF];
		CHECK(record->still, "the same pose twice: still");
		latest = record->latest;
		tick(2.0f);
		CHECK(record->latest == latest && record->tick == interpolation_tick, "a still pose found again is kept, not copied");
		CHECK(drawn(0.25f) == 2.0f, "and drawn as it is");
	}
	else if (!strcmp(case_name, "starts"))
	{
		tick(0.0f);
		tick(0.0f);
		tick(0.0f);
		tick(1.0f);
		CHECK(fabsf(drawn(0.5f) - 0.5f) < 1e-5f, "a still object that moves is blended from where it was");
	}
	else
		CHECK(FALSE, "no case %s", case_name);
	return 0;
}
