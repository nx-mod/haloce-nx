/* A depth-only batch for the visibility field. The simulation cannot advance
   during these synchronous render queries. Cache cluster membership and object
   metadata for this batch, but use the engine's exact BSP/model intersection
   routines for every ray. Unsupported or malformed inputs use the stock query.
   port: past the object limit, the objects it has count, and the BSP for every
   ray: rerunning all of them through the stock query would double a busy view. */
#include "cseries.h"
#include "plasma_collision.h"
#include "physics/collisions.h"
#include "physics/collision_bsp.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_models.h"
#include "physics/breakable_surfaces.h"
#include "objects/objects.h"
#include "structures/structure_bsp_definitions.h"
#include "scenario/scenario.h"
#include <string.h>
#include <math.h>

extern boolean debug_collision_skip_vectors, debug_collision_skip_objects;

enum { TRACE_OBJECTS = 512, TRACE_REFERENCES = 4096 };
struct trace_object
{
	long index, next, child;
	real_point3d center;
	real radius;
	real_point3d relative;
	real sphere_distance;
	int eligible, model_state;
	struct collision_model_instance model;
	unsigned int pending[PLASMA_COLLISION_WORDS];
};
struct trace_batch
{
	int active, failed, count, references, defer_models;
	unsigned long flags, ray;
	int index; /* the ray's place in the batch's rays: what a deferred model test marks */
	int extent_known; /* (a share of a batch's rays: the extent its first ray gave) */
	long ignore;
	real_point3d const *point;
	struct structure_bsp const *structure;
	struct collision_bsp const *bsp;
	real extent;
	real vector_squared;
	struct { int ready; long node; short depth; } prefixes[8];
	/* port: the tables below outlive a batch, as clearing them all (300 KB) for
	   every light's trace cost more than its rays: a batch's objects, and their
	   lookups, are cleared as it ends (batch_clear); the cache's slots, clusters
	   and visits count only with this batch's generation or ray stamp. */
	unsigned long generation, stamp;
	struct collision_bsp_vector_cache cache[8];
	struct trace_object objects[TRACE_OBJECTS];
	unsigned short lookup[MAXIMUM_OBJECTS_PER_MAP];
	unsigned long visited[MAXIMUM_OBJECTS_PER_MAP];
	struct { unsigned long generation; int first, count; unsigned long visited; } clusters[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	long roots[TRACE_REFERENCES];
};
/* the game thread's; each helper has its own (plasma_collision_depths) */
static struct trace_batch game_batch;

/* All endpoints, and the origin, must lie strictly on the same side.
   The margin leaves rounding and borderline planes to engine traversal. */
static int trace_side(struct trace_batch *batch, real_plane3d const *plane, real distance, int octant)
{
	real bound, margin, low, high, components[3];
	int axis;
	bound = batch->extent * (fabsf(plane->n.i) + fabsf(plane->n.j) + fabsf(plane->n.k));
	margin = 0.0001f * (1 + bound + fabsf(plane->d) + fabsf(plane->n.i * batch->point->x) +
		fabsf(plane->n.j * batch->point->y) + fabsf(plane->n.k * batch->point->z));
	low = high = distance;
	components[0] = plane->n.i;
	components[1] = plane->n.j;
	components[2] = plane->n.k;
	for (axis = 0; axis < 3; axis++)
	{
		real endpoint = components[axis] * ((octant & (1 << axis)) ? batch->extent : -batch->extent);
		if (endpoint < 0) low += endpoint;
		else high += endpoint;
	}
	return low > margin ? 1 : high < -margin ? 0 : -1;
}

/* Prepare only the part of the world tree that this ray octant can reach.
   One-sided chains below branches are just as invariant as the common root
   prefix. Keep exact engine traversal at intersecting/borderline planes.
   The bounded cache may evict entries; a miss only loses the optimisation. */
static void trace_prepare(struct trace_batch *batch, long index, short depth, int octant, int *work, long *target, short *skipped)
{
	struct bsp3d_node const *node;
	real_plane3d const *plane;
	real distance;
	long child_target, slot;
	short child_skipped;
	int side;
	*target = index;
	*skipped = 0;
	if (index & LONG_MIN) return;
	if (*work <= 0 || index >= batch->bsp->bsp3d.nodes.count ||
		depth >= MAXIMUM_BSP3D_TRAVERSAL_DEPTH || depth >= batch->bsp->bsp3d.nodes.count) return;
	--*work;
	node = TAG_BLOCK_GET_ELEMENT(&batch->bsp->bsp3d.nodes, index, struct bsp3d_node);
	if (node->plane_designator < 0 || node->plane_designator >= batch->bsp->bsp3d.planes.count) return;
	plane = TAG_BLOCK_GET_ELEMENT(&batch->bsp->bsp3d.planes, node->plane_designator, real_plane3d);
	distance = plane3d_distance_to_point(plane, batch->point);
	side = trace_side(batch, plane, distance, octant);
	if (side >= 0)
	{
		trace_prepare(batch, node->children[side], depth + 1, octant, work, target, skipped);
		++*skipped;
	}
	else
	{
		trace_prepare(batch, node->children[0], depth + 1, octant, work, &child_target, &child_skipped);
		trace_prepare(batch, node->children[1], depth + 1, octant, work, &child_target, &child_skipped);
	}
	slot = (unsigned long)index % COLLISION_VECTOR_CACHE_NODES;
	batch->cache[octant].nodes[slot].generation = batch->cache[octant].generation;
	batch->cache[octant].nodes[slot].index_plus_one = index + 1;
	batch->cache[octant].nodes[slot].node = node;
	batch->cache[octant].nodes[slot].plane = plane;
	batch->cache[octant].nodes[slot].distance = distance;
	batch->cache[octant].nodes[slot].skip_node = *target;
	batch->cache[octant].nodes[slot].skip_count = *skipped;
}

/* Each octant shares the origin and one eighth of the batch's endpoint cube.
   Prove its common tree prefix once, then retain the original traversal below
   it. Both the origin and the entire endpoint box must be strictly on one
   side; convexity then covers every segment. Margins leave borderline planes
   to the stock arithmetic. Malformed trees keep the original root entry. */
static void trace_start(struct trace_batch *batch, int octant)
{
	long node_index = 0;
	short depth = 0;
	real extent = batch->extent;
	if (!(extent > 0 && extent < 1000)) return;
	while (!(node_index & LONG_MIN))
	{
		struct bsp3d_node const *node;
		real_plane3d const *plane;
		real distance;
		int side;
		if (node_index >= batch->bsp->bsp3d.nodes.count ||
			depth >= MAXIMUM_BSP3D_TRAVERSAL_DEPTH || depth >= batch->bsp->bsp3d.nodes.count)
			return;
		node = TAG_BLOCK_GET_ELEMENT(&batch->bsp->bsp3d.nodes, node_index, struct bsp3d_node);
		if (node->plane_designator < 0 || node->plane_designator >= batch->bsp->bsp3d.planes.count)
			return;
		plane = TAG_BLOCK_GET_ELEMENT(&batch->bsp->bsp3d.planes, node->plane_designator, real_plane3d);
		distance = plane3d_distance_to_point(plane, batch->point);
		side = trace_side(batch, plane, distance, octant);
		if (side < 0) break;
		node_index = node->children[side];
		depth++;
	}
	batch->prefixes[octant].node = node_index;
	batch->prefixes[octant].depth = depth;
}

static struct trace_object *trace_object(struct trace_batch *batch, long index)
{
	unsigned long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(index);
	struct trace_object *cached;
	struct object_datum const *object;
	real bound, margin;
	if (slot >= MAXIMUM_OBJECTS_PER_MAP) { batch->failed = 1; return NULL; }
	if (batch->lookup[slot])
	{
		cached = &batch->objects[batch->lookup[slot]-1];
		if (cached->index == index) return cached;
		batch->failed = 1;
		return NULL;
	}
	if (batch->count == TRACE_OBJECTS) return NULL;
	object = object_get(index);
	cached = &batch->objects[batch->count++];
	batch->lookup[slot] = (unsigned short)batch->count;
	cached->index = index;
	cached->next = object->object.next_object_index;
	cached->child = object->object.first_child_object_index;
	cached->center = object->object.bounding_sphere_center;
	cached->radius = object->object.bounding_sphere_radius;
	cached->relative.x = batch->point->x - cached->center.x;
	cached->relative.y = batch->point->y - cached->center.y;
	cached->relative.z = batch->point->z - cached->center.z;
	cached->sphere_distance = cached->relative.x * cached->relative.x +
		cached->relative.y * cached->relative.y + cached->relative.z * cached->relative.z - cached->radius * cached->radius;
	cached->eligible = index != batch->ignore && !TEST_FLAG(object->object.flags, _object_invisible_bit) &&
		TEST_FLAG(batch->flags, object->object.type + _collision_test_objects_first_type_bit);
	/* Every ray endpoint stays inside the batch cube. Reject a disjoint
	   sphere once per batch, rather than rechecking it for every ray. Leave
	   borderline and invalid bounds to the original sphere arithmetic. */
	bound = batch->extent + cached->radius;
	margin = 0.0001f * (1 + fabsf(batch->point->x) + fabsf(batch->point->y) +
		fabsf(batch->point->z) + fabsf(bound));
	if (cached->radius >= 0 && batch->extent > 0 &&
		(fabsf(cached->center.x - batch->point->x) > bound + margin ||
		 fabsf(cached->center.y - batch->point->y) > bound + margin ||
		 fabsf(cached->center.z - batch->point->z) > bound + margin))
		cached->eligible = FALSE;
	return cached;
}

/* The same finite-segment predicate as fast_vector_intersects_sphere. Its
   origin/sphere terms are constant for the batch and its vector length is
   constant for the ray. Keep the original arithmetic and strict comparisons,
   including tangencies; only move invariant work out of the object loop. */
static int trace_sphere(struct trace_batch const *batch, struct trace_object const *object, real_vector3d const *vector)
{
	real b, disc, neg_a_minus_b;
	if (object->sphere_distance < 0.f) return TRUE;
	b = vector->i * object->relative.x + vector->j * object->relative.y + vector->k * object->relative.z;
	if (b >= 0.f) return FALSE;
	disc = b * b - batch->vector_squared * object->sphere_distance;
	if (disc <= 0.f) return FALSE;
	neg_a_minus_b = -batch->vector_squared - b;
	return neg_a_minus_b < 0.f || neg_a_minus_b * neg_a_minus_b < disc;
}

static void trace_objects(struct trace_batch *batch, long index, real_vector3d const *vector, real *depth, int nesting)
{
	int siblings = 0;
	if (nesting >= TRACE_OBJECTS) { batch->failed = 1; return; }
	while (index != NONE && !batch->failed)
	{
		struct trace_object *object = trace_object(batch, index);
		if (!object) return;
		if (++siblings > TRACE_OBJECTS) { batch->failed = 1; return; }
		if (object->eligible && trace_sphere(batch, object, vector))
		{
			if (batch->defer_models)
				object->pending[batch->index / 32] |= 1U << (batch->index % 32);
			else
			{
				struct collision_model_test_vector_result result;
				if (!object->model_state)
					object->model_state = collision_model_instance_new(&object->model, index) ? 1 : -1;
				if (object->model_state > 0 && collision_model_test_vector(&object->model,
					FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit),
					batch->point, vector, &result) && *depth > result.bsp_result.t)
					*depth = result.bsp_result.t;
			}
			/* Children are tested only after the parent's eligibility and sphere,
			   exactly as in object_test_vector; siblings are always considered. */
			if (object->child != NONE) trace_objects(batch, object->child, vector, depth, nesting+1);
		}
		index = object->next;
	}
}

static void trace_cluster(struct trace_batch *batch, long cluster, real_vector3d const *vector, real *depth)
{
	int i;
	if (batch->clusters[cluster].visited == batch->stamp) return;
	batch->clusters[cluster].visited = batch->stamp;
	if (batch->clusters[cluster].generation != batch->generation)
	{
		long reference, index;
		batch->clusters[cluster].first = batch->references;
		for (index = cluster_get_first_collideable_object(&reference, (short)cluster); index != NONE;
			index = cluster_get_next_collideable_object(&reference))
		{
			struct trace_object *object = trace_object(batch, index);
			if (!object)
			{
				if (batch->failed) return;
				break;
			}
			if (!object->eligible && object->next == NONE) continue;
			if (batch->references == TRACE_REFERENCES) break;
			batch->roots[batch->references++] = index;
		}
		batch->clusters[cluster].count = batch->references - batch->clusters[cluster].first;
		batch->clusters[cluster].generation = batch->generation;
	}
	for (i = 0; i < batch->clusters[cluster].count && !batch->failed; i++)
	{
		long index = batch->roots[batch->clusters[cluster].first+i];
		unsigned long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(index);
		if (batch->visited[slot] != batch->stamp)
		{
			batch->visited[slot] = batch->stamp;
			trace_objects(batch, index, vector, depth, 0);
		}
	}
}

/* the objects a batch read, and their lookups: all else of the last batch's is
   out of date by its generation or stamp */
static void batch_clear(struct trace_batch *batch)
{
	int i;
	for (i = 0; i < batch->count; i++)
	{
		unsigned long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(batch->objects[i].index);
		if (slot < MAXIMUM_OBJECTS_PER_MAP) batch->lookup[slot] = 0;
	}
	memset(batch->objects, 0, (size_t)batch->count * sizeof(batch->objects[0]));
	batch->count = 0;
}

static void batch_begin(struct trace_batch *batch, unsigned long flags, real_point3d const *point, long ignore)
{
	unsigned long supported = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) |
		FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
		FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_machines_bit);
	int octant;
	batch_clear(batch);
	/* (a generation that comes round again could find a slot of its last time
	   round: those are cleared, once in four billion batches) */
	if (!++batch->generation)
	{
		memset(batch->cache, 0, sizeof(batch->cache));
		int cluster;
		for (cluster = 0; cluster < MAXIMUM_CLUSTERS_PER_STRUCTURE; cluster++) batch->clusters[cluster].generation = 0;
		batch->generation = 1;
	}
	for (octant = 0; octant < 8; octant++)
		batch->cache[octant].generation = batch->generation;
	memset(batch->prefixes, 0, sizeof(batch->prefixes));
	batch->failed = batch->references = batch->defer_models = 0;
	batch->ray = 0;
	batch->index = 0;
	batch->extent_known = 0;
	batch->structure = NULL;
	batch->bsp = NULL;
	batch->extent = batch->vector_squared = 0;
	batch->active = flags == supported || flags == (supported | FLAG(_collision_test_objects_vehicles_bit));
	batch->flags = flags;
	batch->point = point;
	batch->ignore = ignore;
	if (batch->active)
	{
		batch->structure = global_structure_bsp_get();
		batch->bsp = global_collision_bsp_get();
	}
}

static float batch_depth(struct trace_batch *batch, unsigned long flags, real_point3d const *point,
	real_vector3d const *vector, long ignore, int index)
{
	struct collision_result fallback;
	struct collision_bsp_test_vector_result result;
	real depth = 1.0f;
	int i, hit, use_start, octant;
	if (!batch->active || batch->failed || flags != batch->flags || point != batch->point || ignore != batch->ignore)
		goto stock;
	if (debug_collision_skip_vectors) return 2.0f;
	if (!batch->ray && !batch->extent_known) batch->extent = MAX(fabsf(vector->i), MAX(fabsf(vector->j), fabsf(vector->k)));
	use_start = fabsf(vector->i) <= batch->extent && fabsf(vector->j) <= batch->extent &&
		fabsf(vector->k) <= batch->extent;
	/* Cached object rejection applies only inside the original cube. */
	if (!use_start) goto stock;
	octant = (vector->i >= 0) | ((vector->j >= 0) << 1) | ((vector->k >= 0) << 2);
	if (!batch->prefixes[octant].ready)
	{
		long target;
		short skipped;
		int work = COLLISION_VECTOR_CACHE_NODES;
		batch->prefixes[octant].ready = 1;
		trace_start(batch, octant);
		if (batch->extent > 0 && batch->extent < 1000)
			trace_prepare(batch, batch->prefixes[octant].node, batch->prefixes[octant].depth, octant, &work, &target, &skipped);
	}
	batch->ray++;
	batch->index = index;
	/* (a stamp that comes round again: the visits marked with it are cleared) */
	if (!++batch->stamp)
	{
		memset(batch->visited, 0, sizeof(batch->visited));
		for (i = 0; i < MAXIMUM_CLUSTERS_PER_STRUCTURE; i++) batch->clusters[i].visited = 0;
		batch->stamp = 1;
	}
	batch->vector_squared = vector->i * vector->i + vector->j * vector->j + vector->k * vector->k;
	hit = collision_bsp_test_vector_from_node(FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_back_facing_surfaces_bit), batch->bsp,
		MAXIMUM_BREAKABLE_SURFACES_PER_MAP, breakable_surface_flags_get(), point, vector, REAL_MAX, &result,
		batch->prefixes[octant].node, batch->prefixes[octant].depth, &batch->cache[octant]);
	if (hit) depth = result.t;
	if (!debug_collision_skip_objects)
		for (i = 0; i < result.leaf_count && !batch->failed; i++)
		{
			long leaf = result.leaf_indices[i], cluster;
			if (leaf == NONE) continue;
			if ((leaf & LONG_MAX) >= batch->structure->leaves.count) goto stock;
			cluster = TAG_BLOCK_GET_ELEMENT(&batch->structure->leaves, leaf & LONG_MAX, struct structure_leaf)->cluster_index;
			if (cluster < 0 || cluster >= MAXIMUM_CLUSTERS_PER_STRUCTURE) continue;
			trace_cluster(batch, cluster, vector, &depth);
		}
	if (batch->failed) goto stock;
	return hit || depth < 1.0f ? depth : 2.0f;
stock:
	/* port: a deferred batch that fails has all its rays traced again by the
	   stock query, by the game thread once its helpers are done (the stock query
	   marks objects: it is the game thread's alone), so this ray's is not wanted */
	if (batch->defer_models)
	{
		batch->failed = 1;
		return 2.0f;
	}
	return collision_test_vector(flags, point, vector, ignore, &fallback) ? fallback.t : 2.0f;
}

/* the deferred model tests: each object a ray reached, once, for all its rays */
static void batch_models(struct trace_batch *batch, real_point3d const *point, real_vector3d const *vectors,
	int count, float *depths)
{
	int object_index, word;
	for (object_index = 0; object_index < batch->count; object_index++)
	{
		struct trace_object *object = &batch->objects[object_index];
		for (word = 0; word < (count + 31) / 32; word++)
			if (object->pending[word]) break;
		if (word == (count + 31) / 32) continue;
		if (collision_model_instance_new(&object->model, object->index))
			plasma_collision_model_depths(&object->model, point, vectors, count, object->pending, depths);
	}
}

static void batch_end(struct trace_batch *batch)
{
	/* Model instances borrow tag and object matrices only during the batch;
	   the cache's slots, the tree's nodes (out of date with the generation). */
	batch_clear(batch);
	batch->active = 0;
	batch->point = NULL;
	batch->structure = NULL;
	batch->bsp = NULL;
}

void plasma_collision_begin(unsigned long flags, real_point3d const *point, long ignore)
{
	batch_begin(&game_batch, flags, point, ignore);
}

float plasma_collision_depth(unsigned long flags, real_point3d const *point, real_vector3d const *vector, long ignore)
{
	return batch_depth(&game_batch, flags, point, vector, ignore, (int)game_batch.ray);
}

void plasma_collision_end(void)
{
	batch_end(&game_batch);
}

static void stock_depths(unsigned long flags, real_point3d const *point, real_vector3d const *vectors,
	int count, long ignore, float *depths)
{
	int ray;
	for (ray = 0; ray < count; ray++)
	{
		struct collision_result result;
		depths[ray] = collision_test_vector(flags, point, &vectors[ray], ignore, &result) ? result.t : 2.0f;
	}
}

void plasma_collision_depths(unsigned long flags, real_point3d const *point,
	real_vector3d const *vectors, int count, long ignore, float *depths)
{
	struct trace_batch *batch = &game_batch;
	int ray;
	batch_begin(batch, flags, point, ignore);
	batch->defer_models = count > 0 && count <= PLASMA_COLLISION_RAYS;
	if (batch->active && batch->defer_models)
	{
		/* Preserve cluster, sphere and parent/child gates for every ray. Only
		   defer the expensive model queries; a bitset deduplicates identical
		   queries reached through more than one object/cluster reference. */
		for (ray = 0; ray < count && !batch->failed; ray++)
			depths[ray] = batch_depth(batch, flags, point, &vectors[ray], ignore, ray);
		if (!batch->failed)
			batch_models(batch, point, vectors, count, depths);
	}
	else batch->failed = 1;
	if (batch->failed)
		stock_depths(flags, point, vectors, count, ignore, depths);
	batch_end(batch);
}

/* ---------- a light's rays on helper threads

port: one light's rays, shared out an octant at a time between the game thread
and helpers. Each takes octants as they come, with its own batch, and makes its
octants' model tests once they have all been taken; each ray's depth is the
batch's, whoever traced it (each ray's tests are its own: only which objects a
batch had to look up is shared, and each looks up its own). An octant whose
batch failed is traced by the stock query once all are done, by the game
thread. The game thread either waits for the light at once, helping
(plasma_collision_depths_finish right after start), or, for a light it only
needs next frame, goes on and collects it then: the helpers read the world
(the BSP, the objects, their models and matrices) while it draws, which writes
none of that, and it collects every light before the next tick. */
enum { TRACE_CHUNKS = 8, TRACE_TASKS = 16 };
struct trace_task
{
	/* (written by the game thread before `next` is set to 0) */
	unsigned long flags;
	real_point3d point;
	long ignore;
	int count;
	real extent;
	real_vector3d vectors[PLASMA_COLLISION_RAYS];
	float depths[PLASMA_COLLISION_RAYS];
	short order[PLASMA_COLLISION_RAYS]; /* the rays, octant by octant */
	short starts[TRACE_CHUNKS + 1];
	/* the next octant to take (TRACE_CHUNKS and past: none), the octants
	   finished, and those whose batch failed */
	int next, finished, failed;
	int published; /* helpers may take its octants */
	int used; /* (the game thread's) */
};

/* At most limit of the task's octants, as long as there are any; how many. */
static int task_work(struct trace_task *task, struct trace_batch *batch, int limit)
{
	int chunk, done = 0, mine = 0;
	while (done < limit && __atomic_load_n(&task->next, __ATOMIC_RELAXED) < TRACE_CHUNKS &&
		(chunk = __atomic_fetch_add(&task->next, 1, __ATOMIC_ACQ_REL)) < TRACE_CHUNKS)
	{
		int i;
		if (!done)
		{
			batch_begin(batch, task->flags, &task->point, task->ignore);
			batch->defer_models = 1;
			batch->extent = task->extent;
			batch->extent_known = 1;
			if (!batch->active)
				batch->failed = 1;
		}
		done++;
		mine |= 1 << chunk;
		for (i = task->starts[chunk]; i < task->starts[chunk + 1] && !batch->failed; i++)
		{
			int ray = task->order[i];
			task->depths[ray] = batch_depth(batch, task->flags, &task->point, &task->vectors[ray], task->ignore, ray);
		}
		/* (a failed batch takes no more: the game thread traces what it took) */
		if (batch->failed)
			break;
	}
	if (!done)
		return 0;
	if (!batch->failed)
		batch_models(batch, &task->point, task->vectors, task->count, task->depths);
	if (batch->failed)
		__atomic_fetch_or(&task->failed, mine, __ATOMIC_RELAXED);
	batch_end(batch);
	__atomic_fetch_add(&task->finished, done, __ATOMIC_RELEASE);
	return done;
}

static void task_fill(struct trace_task *task, unsigned long flags, real_point3d const *point,
	real_vector3d const *vectors, int count, long ignore)
{
	int ray, chunk, counts[TRACE_CHUNKS + 1];
	task->flags = flags;
	task->point = *point;
	task->ignore = ignore;
	task->count = count;
	memcpy(task->vectors, vectors, (size_t)count * sizeof(vectors[0]));
	/* (the extent the whole batch would have had: its first ray's) */
	task->extent = MAX(fabsf(vectors[0].i), MAX(fabsf(vectors[0].j), fabsf(vectors[0].k)));
	memset(counts, 0, sizeof(counts));
	for (ray = 0; ray < count; ray++)
		counts[(vectors[ray].i >= 0) | ((vectors[ray].j >= 0) << 1) | ((vectors[ray].k >= 0) << 2)]++;
	task->starts[0] = 0;
	for (chunk = 0; chunk < TRACE_CHUNKS; chunk++)
	{
		task->starts[chunk + 1] = (short)(task->starts[chunk] + counts[chunk]);
		counts[chunk] = task->starts[chunk];
	}
	for (ray = 0; ray < count; ray++)
		task->order[counts[(vectors[ray].i >= 0) | ((vectors[ray].j >= 0) << 1) | ((vectors[ray].k >= 0) << 2)]++] = (short)ray;
	task->finished = 0;
	task->failed = 0;
}

/* (once all its octants are finished) the stock query's depths for any whose
   batch failed */
static void task_stock(struct trace_task *task)
{
	int chunk, i;
	int failed = __atomic_load_n(&task->failed, __ATOMIC_RELAXED);
	for (chunk = 0; chunk < TRACE_CHUNKS; chunk++)
		if (failed & (1 << chunk))
			for (i = task->starts[chunk]; i < task->starts[chunk + 1]; i++)
				stock_depths(task->flags, &task->point, &task->vectors[task->order[i]], 1, task->ignore,
					&task->depths[task->order[i]]);
}

#if defined(PLASMA_COLLISION_NO_THREADS)
int plasma_collision_depths_start(unsigned long flags, real_point3d const *point,
	real_vector3d const *vectors, int count, long ignore)
{
	(void)flags; (void)point; (void)vectors; (void)count; (void)ignore;
	return -1;
}
int plasma_collision_depths_finish(int handle, float *depths)
{
	(void)handle; (void)depths;
	return 0;
}
#else
#include <pthread.h>
#if defined(__aarch64__) || defined(__arm__)
#define trace_relax() __asm__ __volatile__("yield")
#elif defined(__x86_64__) || defined(__i386__)
#define trace_relax() __builtin_ia32_pause()
#else
#define trace_relax() ((void)0)
#endif
/* The Switch's game has a core to itself and the process two more, which its
   other threads (the audio mixer, the GPU's) share; phones and the PS5 have
   more. A helper looks for work for a while after its last, as a view's lights
   are traced one after another, before it sleeps. */
#if defined(HALO_SWITCH)
enum { TRACE_HELPERS = 2 };
#else
enum { TRACE_HELPERS = 3 };
#endif
enum { TRACE_HELPER_SPINS = 4000 };
/* (each helper is woken on its own, with a signal: no broadcast, whose waking
   of more than one waiter the Switch's host once got wrong, and woke one) */
struct trace_helper
{
	pthread_mutex_t lock;
	pthread_cond_t wake;
	struct trace_batch *batch;
};
static struct
{
	int started, helpers;
	unsigned long published;
	struct trace_task *tasks;
	struct trace_helper helper[TRACE_HELPERS];
} pool;

/* a published task's octants, any; whether there were any */
static int pool_help(struct trace_batch *batch)
{
	int index, worked = 0;
	for (index = 0; index < TRACE_TASKS; index++)
	{
		struct trace_task *task = &pool.tasks[index];
		if (__atomic_load_n(&task->published, __ATOMIC_ACQUIRE) &&
			__atomic_load_n(&task->next, __ATOMIC_RELAXED) < TRACE_CHUNKS &&
			task_work(task, batch, TRACE_CHUNKS))
			worked = 1;
	}
	return worked;
}

static void *pool_helper(void *argument)
{
	struct trace_helper *helper = (struct trace_helper *)argument;
	unsigned long seen = 0;
	for (;;)
	{
		int spins;
		pthread_mutex_lock(&helper->lock);
		while (__atomic_load_n(&pool.published, __ATOMIC_ACQUIRE) == seen)
			pthread_cond_wait(&helper->wake, &helper->lock);
		seen = __atomic_load_n(&pool.published, __ATOMIC_ACQUIRE);
		pthread_mutex_unlock(&helper->lock);
		for (spins = 0; spins < TRACE_HELPER_SPINS; spins++)
		{
			if (pool_help(helper->batch))
				spins = 0;
			else
				trace_relax();
		}
	}
	return NULL;
}

static int pool_start(void)
{
	int index;
	if (pool.started)
		return pool.helpers > 0;
	pool.started = 1;
	pool.tasks = (struct trace_task *)calloc(TRACE_TASKS, sizeof(struct trace_task));
	if (!pool.tasks)
		return 0;
	for (index = 0; index < TRACE_TASKS; index++)
		pool.tasks[index].next = TRACE_CHUNKS;
	for (index = 0; index < TRACE_HELPERS; index++)
	{
		struct trace_helper *helper = &pool.helper[pool.helpers];
		pthread_attr_t attributes;
		pthread_t thread;
		helper->batch = (struct trace_batch *)calloc(1, sizeof(struct trace_batch));
		if (!helper->batch)
			break;
		pthread_mutex_init(&helper->lock, NULL);
		pthread_cond_init(&helper->wake, NULL);
		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, 256 * 1024);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&thread, &attributes, pool_helper, helper))
		{
			pthread_attr_destroy(&attributes);
			free(helper->batch);
			helper->batch = NULL;
			break;
		}
		pthread_attr_destroy(&attributes);
		pool.helpers++;
	}
	return pool.helpers > 0;
}

/* The light's rays handed to the helpers; its handle, or -1 if there are no
   helpers or no room (plasma_collision_depths then, as before). */
int plasma_collision_depths_start(unsigned long flags, real_point3d const *point,
	real_vector3d const *vectors, int count, long ignore)
{
	int index, handle;
	if (count <= 0 || count > PLASMA_COLLISION_RAYS || !pool_start())
		return -1;
	for (index = 0; index < TRACE_TASKS; index++)
		if (!pool.tasks[index].used)
			break;
	if (index == TRACE_TASKS)
		return -1;
	handle = index;
	{
		struct trace_task *task = &pool.tasks[index];
		task->used = 1;
		task_fill(task, flags, point, vectors, count, ignore);
		__atomic_store_n(&task->next, 0, __ATOMIC_RELEASE);
		__atomic_store_n(&task->published, 1, __ATOMIC_RELEASE);
	}
	__atomic_fetch_add(&pool.published, 1, __ATOMIC_RELEASE);
	for (index = 0; index < pool.helpers; index++)
	{
		pthread_mutex_lock(&pool.helper[index].lock);
		pthread_cond_signal(&pool.helper[index].wake);
		pthread_mutex_unlock(&pool.helper[index].lock);
	}
	return handle;
}

/* The light's depths, once all its octants are traced: the game thread takes
   any not yet taken, of it or another light, while it waits. */
int plasma_collision_depths_finish(int handle, float *depths)
{
	struct trace_task *task;
	if (handle < 0 || handle >= TRACE_TASKS || !pool.tasks || !pool.tasks[handle].used)
		return 0;
	task = &pool.tasks[handle];
	task_work(task, &game_batch, TRACE_CHUNKS);
	while (__atomic_load_n(&task->finished, __ATOMIC_ACQUIRE) < TRACE_CHUNKS)
		if (!pool_help(&game_batch))
			trace_relax();
	__atomic_store_n(&task->published, 0, __ATOMIC_RELAXED);
	task_stock(task);
	memcpy(depths, task->depths, (size_t)task->count * sizeof(depths[0]));
	task->used = 0;
	return 1;
}
#endif
