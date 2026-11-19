/*
BIPED_RAGDOLL.C

A body falls as a ragdoll (game.ragdoll_players, game.ragdoll_ai), in a
campaign or a multiplayer game, from the tick its unit dies, in place of
its dying animation and the limp body it lies as afterwards (bipeds.c,
biped_limp_noodle.c). "woody" is the ragdoll below;
"halo2" one after Halo 2's (Havok's, as its physics models describe them:
rigid bodies of some size with friction in their joints), a heavy torso,
limbs with a particle midway along each that keep out of the torso and each
other, joints that drag and twist less, and blasts that throw it further.

Each of the model's nodes is a particle, and the head has one more at its
top. Bones keep their lengths, the joints where a bone branches (the hips,
the chest) are rigid, and a joint bends no further than it can (the spine and
neck a little, the limbs a long way). Each tick, in two halves: gravity and
the particles' speeds move them, the bones pull them back together, and every
move is clipped against the world (the structure and the objects a dead
biped's physics collides with). The nodes' rotations follow their bones,
twisting no more than their parents do. A body that lies still for half a
second, or has fallen for ten, sleeps until a blast moves it again.

The killing blow pushes the body hardest nearest where it hit (an
explosion's center, a bullet's impact), so a body spins and folds as it was
hit, and a later blast moves it too.

It is drawn, not played. On a client, or in a game of one machine, the
ragdoll moves the body itself: its nodes, its origin and its bounds, in place
of its physics and its dying animation. A client decides nothing of the game
(the host does), so the host's word on where the body is doesn't move it
(network_objects.c). The host of a network game, whose bodies' places and
poses decide what its shots hit and what it sends its clients, leaves its
bodies as the stock game moves and poses them, and only draws them as their
ragdolls lie (render_interpolation.c). So a host sends its clients what any
build expects, and machines with and without ragdolls (the setting off, or a
build without it, at the same network version) play together, each drawing
bodies its own way. Every machine runs its own ragdolls from its own copy of
the killing blow (the host's, replayed on the clients: network_damage.c).
The weapons it drops are the host's, as before.
*/

#include "cseries.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "physics/collision_features.h"
#include "physics/collision_usage.h"
#include "physics/collisions.h"
#include "physics/physics.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "units/unit_definitions.h"
#include "units/units.h"
#include "biped_ragdoll.h"

/* port/linux/src/port_config.c */
const char *config_string(const char *name);
unsigned long config_changes(void);
/* port/linux/game/object_bounds_cache.c */
void object_bounds_cache_update(long object_index, short type, real_point3d const *center, real radius);

/* ---------- constants */

enum
{
	MAXIMUM_RAGDOLLS = 32,
	MAXIMUM_RAGDOLL_TIPS = 4,
	MAXIMUM_RAGDOLL_MIDDLES = 16,
	MAXIMUM_RAGDOLL_PARTICLES = MAXIMUM_NODES_PER_MODEL + MAXIMUM_RAGDOLL_TIPS + MAXIMUM_RAGDOLL_MIDDLES,
	MAXIMUM_RAGDOLL_CONSTRAINTS = 6 * MAXIMUM_RAGDOLL_PARTICLES,
	MAXIMUM_RAGDOLL_BLOWS = 16,
	MAXIMUM_RAGDOLL_JOINTS = 16,

	RAGDOLL_SUBSTEPS = 2,
	RAGDOLL_ITERATIONS = 6,
	/* still this long, it sleeps; falling this long, it sleeps anyway */
	RAGDOLL_SLEEP_TICKS = TICKS_PER_SECOND / 2,
	RAGDOLL_MAXIMUM_TICKS = 10 * TICKS_PER_SECOND,
	/* a blow this many ticks before it starts pushes it */
	RAGDOLL_BLOW_TICKS = 2,
};

/* game.ragdoll_players and game.ragdoll_ai: how a body falls */
enum
{
	_ragdoll_style_none = 0,
	/* light, even limbs, swinging freely within their joints */
	_ragdoll_style_woody,
	/* as Halo 2's bodies fall (Havok's ragdolls): a heavy torso, limbs of
	some thickness that keep out of it and of each other, joints that drag
	and twist little, thrown further by blasts */
	_ragdoll_style_halo2,
};

enum
{
	_ragdoll_constraint_length = 0,
	/* no shorter than its length: how far a joint bends */
	_ragdoll_constraint_minimum,
};

enum
{
	/* bends one way, in one plane: a knee, an elbow */
	_ragdoll_joint_hinge = 0,
	/* swings within a cone: a hip, a shoulder */
	_ragdoll_joint_cone,
};

/* a particle's distance from the world (world units: a node's thickness,
less than an ankle's height off the ground) */
#define RAGDOLL_RADIUS 0.025f
/* world units a tick */
#define RAGDOLL_MAXIMUM_SPEED 0.5f
#define RAGDOLL_SLEEP_SPEED 0.002f
/* what a particle's speed keeps each half tick in the air, and along what it
lies on */
#define RAGDOLL_DAMPING 0.995f
#define RAGDOLL_FRICTION 0.6f
/* how far from a blow's epicenter its push falls to half */
#define RAGDOLL_BLOW_RADIUS 0.12f
/* a bullet's own push where it hits (world units a tick), on top of its
damage's acceleration, which most bullets have none of */
#define RAGDOLL_HIT_SPEED 0.02f
#define RAGDOLL_HIT_RADIUS 0.05f
/* and the whole body's, the way the blow came (world units a tick) */
#define RAGDOLL_BLOW_SPEED 0.02f
/* the head's top, from the neck */
#define RAGDOLL_TIP_MINIMUM_LENGTH 0.04f
#define RAGDOLL_TIP_MAXIMUM_LENGTH 0.08f
/* how far a joint bends from straight: the spine, neck and head's, and the
rest */
#define RAGDOLL_STIFF_BEND_COSINE 0.819f /* 35 degrees */
#define RAGDOLL_BEND_COSINE -0.866f /* 150 degrees */
/* how far a knee and an elbow twist out of their plane (a fraction of the
bone's length), how far a hip and a shoulder swing from where they were (the
cone's cosine), and how much of the way back a joint is put each time */
#define RAGDOLL_KNEE_TWIST 0.2f
#define RAGDOLL_ELBOW_TWIST 0.45f
#define RAGDOLL_HIP_COSINE 0.087f /* 85 degrees, round a middle tilted forward: */
#define RAGDOLL_HIP_TILT 0.6f /* 35 degrees (forward to 120, back to 50) */
#define RAGDOLL_SHOULDER_COSINE -0.5f /* 120 degrees */
#define RAGDOLL_JOINT_STIFFNESS 0.35f
/* halo2: the knees' and elbows' twist, the shoulders' cone tilted forward,
what a limb's speed keeps of its swing about its joint each half tick and
along what it lies on, how much harder blows push, how heavy each part is
(as an inverse: the torso's least), and how far limbs keep out of the torso
and each other */
#define RAGDOLL_HALO2_KNEE_TWIST 0.1f
#define RAGDOLL_HALO2_ELBOW_TWIST 0.3f
#define RAGDOLL_HALO2_SHOULDER_TILT 0.4f
#define RAGDOLL_HALO2_SHOULDER_COSINE -0.17f /* 100 degrees */
#define RAGDOLL_HALO2_JOINT_FRICTION 0.06f
#define RAGDOLL_HALO2_FRICTION 0.75f
#define RAGDOLL_HALO2_BLAST_SCALE 1.5f
#define RAGDOLL_HALO2_TORSO_INVERSE_MASS 0.35f
#define RAGDOLL_HALO2_HEAD_INVERSE_MASS 0.6f
#define RAGDOLL_HALO2_UPPER_INVERSE_MASS 0.8f
#define RAGDOLL_HALO2_END_INVERSE_MASS 1.3f
#define RAGDOLL_HALO2_TORSO_CLEARANCE 0.055f
#define RAGDOLL_HALO2_LIMB_CLEARANCE 0.03f

/* ---------- structures */

struct ragdoll_constraint
{
	short a;
	short b;
	short type;
	real length;
};

struct ragdoll_rotation
{
	real_vector3d columns[3];
};

/* a joint's limit, held in the frame of the rigid part it hangs from (the
hips for a leg, the chest for an arm), which turns as the body does: the
hinge's axis, or the cone's middle */
struct ragdoll_joint
{
	short type;
	short frame;
	short grandparent;
	short parent;
	short child;
	real_vector3d axis;
	real limit;
};

struct ragdoll
{
	long object_index;
	long start_time;
	short node_count;
	short particle_count;
	short constraint_count;
	short ticks;
	short still_ticks;
	boolean asleep;
	/* hosting a network game: drawn only, its body left as the stock game
	moves and poses it (and as the host sends it); else the ragdoll moves
	the body itself */
	boolean drawn_only;
	/* game.ragdoll's, as it was when the body died */
	short style;
	real friction;
	real joint_friction;
	real blast_scale;
	/* the nodes, parents first */
	short order[MAXIMUM_NODES_PER_MODEL];
	short parents[MAXIMUM_RAGDOLL_PARTICLES];
	/* the particles a node's rotation follows: its bone's end, and another
	where it branches */
	short primary[MAXIMUM_NODES_PER_MODEL];
	short secondary[MAXIMUM_NODES_PER_MODEL];
	real_point3d positions[MAXIMUM_RAGDOLL_PARTICLES];
	real_vector3d velocities[MAXIMUM_RAGDOLL_PARTICLES];
	/* how readily each moves when the bones pull it (1: as any other) */
	real inverse_masses[MAXIMUM_RAGDOLL_PARTICLES];
	struct ragdoll_constraint constraints[MAXIMUM_RAGDOLL_CONSTRAINTS];
	short joint_count;
	struct ragdoll_joint joints[MAXIMUM_RAGDOLL_JOINTS];
	/* the nodes as drawn, put back over the body's own each tick */
	real_matrix4x3 pose[MAXIMUM_NODES_PER_MODEL];
};

struct ragdoll_blow
{
	long unit_index;
	long time;
	real_point3d epicenter;
	real_vector3d direction;
	real speed;
};

/* ---------- globals */

static struct ragdoll ragdolls[MAXIMUM_RAGDOLLS];
static struct ragdoll_blow ragdoll_blows[MAXIMUM_RAGDOLL_BLOWS];
static long ragdoll_next_blow;
static struct collision_feature_list ragdoll_features;
static boolean ragdolls_initialized = FALSE;

/* ---------- private code */

/* a setting's style; none leaves the original death animations */
static short ragdoll_named(
	char const *name)
{
	return !strcmp(name, "halo2") ? _ragdoll_style_halo2 :
		!strcmp(name, "woody") ? _ragdoll_style_woody : _ragdoll_style_none;
}

/* the body's style: a player's (game.ragdoll_players) or the AI's
(game.ragdoll_ai). none for anything that is not a biped */
static short ragdoll_style(
	long biped_index)
{
	static unsigned long changes = (unsigned long)-1;
	static short players = _ragdoll_style_none;
	static short ai = _ragdoll_style_none;
	struct biped_datum *biped;

	if (changes != config_changes())
	{
		changes = config_changes();
		players = ragdoll_named(config_string("game.ragdoll_players"));
		ai = ragdoll_named(config_string("game.ragdoll_ai"));
	}
	biped = biped_try_and_get(biped_index);
	if (!biped)
		return _ragdoll_style_none;
	return biped->unit.player_index != NONE ? players : ai;
}

static void ragdolls_initialize(
	void)
{
	long index;

	if (ragdolls_initialized)
		return;
	ragdolls_initialized = TRUE;
	for (index = 0; index < MAXIMUM_RAGDOLLS; index++)
		ragdolls[index].object_index = NONE;
	for (index = 0; index < MAXIMUM_RAGDOLL_BLOWS; index++)
		ragdoll_blows[index].unit_index = NONE;
	ragdoll_next_blow = 0;
}

static struct ragdoll *ragdoll_find(
	long object_index)
{
	long index;

	ragdolls_initialize();
	if (object_index == NONE)
		return NULL;
	for (index = 0; index < MAXIMUM_RAGDOLLS; index++)
	{
		if (ragdolls[index].object_index == object_index)
			return &ragdolls[index];
	}
	return NULL;
}

/* the dead biped a ragdoll moves, or NULL once it is gone (deleted, another
object in its place, or alive again) */
static struct biped_datum *ragdoll_body(
	struct ragdoll const *ragdoll)
{
	struct biped_datum *biped = (struct biped_datum *)object_try_and_get_and_verify_type(
		ragdoll->object_index, _object_mask_biped);

	return biped && TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ? biped : NULL;
}

static boolean name_contains(
	char const *name,
	char const *part)
{
	long start;

	for (start = 0; name[start]; start++)
	{
		long offset;

		for (offset = 0; part[offset]; offset++)
		{
			char c = name[start + offset];

			if (c >= 'A' && c <= 'Z')
				c = (char)(c - 'A' + 'a');
			if (c != part[offset])
				break;
		}
		if (!part[offset])
			return TRUE;
	}
	return FALSE;
}

static real ragdoll_clamp(
	real value,
	real minimum,
	real maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

static void ragdoll_clamp_speed(
	real_vector3d *velocity)
{
	real speed = magnitude3d(velocity);

	if (speed > RAGDOLL_MAXIMUM_SPEED)
		scale_vector3d(velocity, RAGDOLL_MAXIMUM_SPEED / speed, velocity);
}

/* a point moved as far towards where it is going as the world lets it;
TRUE when it hit something, the last thing's normal in *normal */
static boolean ragdoll_move(
	real_point3d *position,
	real_point3d const *destination,
	real_vector3d *normal)
{
	struct collision_plane collisions[4];
	real_vector3d motion;
	real_vector3d clipped_motion;
	real_point3d moved;
	short count;

	vector_from_points3d(position, destination, &motion);
	if (fabs(motion.i) < _real_epsilon && fabs(motion.j) < _real_epsilon && fabs(motion.k) < _real_epsilon)
		return FALSE;
	count = collision_move_point(position, &motion, &ragdoll_features, &moved, &clipped_motion,
		(short)NUMBEROF(collisions) - 1, collisions);
	*position = moved;
	if (count > 0 && normal)
		*normal = collisions[count - 1].plane.n;
	return count > 0;
}

/* the particles' middle, and how far round it they reach (and will have
moved, with their speeds: a tick's worth of the world to collide with) */
static void ragdoll_bounds(
	struct ragdoll const *ragdoll,
	boolean with_speeds,
	real_point3d *center,
	real *radius)
{
	real reach = 0.f;
	short index;

	center->x = center->y = center->z = 0.f;
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		center->x += ragdoll->positions[index].x;
		center->y += ragdoll->positions[index].y;
		center->z += ragdoll->positions[index].z;
	}
	center->x /= ragdoll->particle_count;
	center->y /= ragdoll->particle_count;
	center->z /= ragdoll->particle_count;
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		real distance = distance3d(center, &ragdoll->positions[index]);

		if (with_speeds)
			distance += magnitude3d(&ragdoll->velocities[index]);
		if (distance > reach)
			reach = distance;
	}
	*radius = reach;
}

static void ragdoll_gather_features(
	struct ragdoll const *ragdoll,
	long biped_index)
{
	real_point3d center;
	real radius;

	ragdoll_bounds(ragdoll, TRUE, &center, &radius);
	collision_get_features_in_sphere(
		_collision_test_for_bipeds_dead_flags | FLAG(_collision_test_ignore_invisible_surfaces_bit),
		&center,
		radius + 2.f * RAGDOLL_RADIUS,
		0.f,
		RAGDOLL_RADIUS,
		biped_index,
		&ragdoll_features);
}

/* ---------- rotations */

static void rotation_identity(
	struct ragdoll_rotation *rotation)
{
	set_real_vector3d(&rotation->columns[0], 1.f, 0.f, 0.f);
	set_real_vector3d(&rotation->columns[1], 0.f, 1.f, 0.f);
	set_real_vector3d(&rotation->columns[2], 0.f, 0.f, 1.f);
}

static void rotation_transform(
	struct ragdoll_rotation const *rotation,
	real_vector3d const *vector,
	real_vector3d *result)
{
	real_vector3d out;

	out.i = rotation->columns[0].i * vector->i + rotation->columns[1].i * vector->j + rotation->columns[2].i * vector->k;
	out.j = rotation->columns[0].j * vector->i + rotation->columns[1].j * vector->j + rotation->columns[2].j * vector->k;
	out.k = rotation->columns[0].k * vector->i + rotation->columns[1].k * vector->j + rotation->columns[2].k * vector->k;
	*result = out;
}

/* the shortest turn from one direction to another (both normal) */
static void rotation_between(
	real_vector3d const *from,
	real_vector3d const *to,
	struct ragdoll_rotation *rotation)
{
	real_vector3d axis;
	real sine;
	real cosine = dot_product3d(from, to);
	real versine;
	long column;

	cross_product3d(from, to, &axis);
	sine = magnitude3d(&axis);
	if (sine < 1e-6f || cosine < -0.999f)
	{
		rotation_identity(rotation);
		return;
	}
	scale_vector3d(&axis, 1.f / sine, &axis);
	versine = 1.f - cosine;
	/* (Rodrigues: each column the turn of an axis) */
	for (column = 0; column < 3; column++)
	{
		real_vector3d unit;
		real_vector3d across;
		real along;

		set_real_vector3d(&unit, column == 0 ? 1.f : 0.f, column == 1 ? 1.f : 0.f, column == 2 ? 1.f : 0.f);
		cross_product3d(&axis, &unit, &across);
		along = dot_product3d(&axis, &unit) * versine;
		rotation->columns[column].i = unit.i * cosine + across.i * sine + axis.i * along;
		rotation->columns[column].j = unit.j * cosine + across.j * sine + axis.j * along;
		rotation->columns[column].k = unit.k * cosine + across.k * sine + axis.k * along;
	}
}

/* an orthonormal basis from a direction and another across it; FALSE when
they are (nearly) the same direction */
static boolean basis_from_directions(
	real_vector3d const *primary,
	real_vector3d const *secondary,
	struct ragdoll_rotation *basis)
{
	real_vector3d across;
	real along;

	basis->columns[0] = *primary;
	if (normalize3d(&basis->columns[0]) < 1e-6f)
		return FALSE;
	across = *secondary;
	along = dot_product3d(&across, &basis->columns[0]);
	basis->columns[1].i = across.i - basis->columns[0].i * along;
	basis->columns[1].j = across.j - basis->columns[0].j * along;
	basis->columns[1].k = across.k - basis->columns[0].k * along;
	if (normalize3d(&basis->columns[1]) < 1e-4f)
		return FALSE;
	cross_product3d(&basis->columns[0], &basis->columns[1], &basis->columns[2]);
	return TRUE;
}

/* the turn that takes one basis to another */
static void rotation_between_bases(
	struct ragdoll_rotation const *from,
	struct ragdoll_rotation const *to,
	struct ragdoll_rotation *rotation)
{
	long column;

	/* (to times from's transpose: each world axis put in from's terms, then
	taken out of to's) */
	for (column = 0; column < 3; column++)
	{
		real_vector3d unit;
		real_vector3d local;

		set_real_vector3d(&unit, column == 0 ? 1.f : 0.f, column == 1 ? 1.f : 0.f, column == 2 ? 1.f : 0.f);
		local.i = dot_product3d(&from->columns[0], &unit);
		local.j = dot_product3d(&from->columns[1], &unit);
		local.k = dot_product3d(&from->columns[2], &unit);
		rotation_transform(to, &local, &rotation->columns[column]);
	}
}

/* the frame of a rigid part: its node to its two first ends (the hips, the
chest); FALSE if they lie in a line */
static boolean ragdoll_frame(
	struct ragdoll const *ragdoll,
	short node,
	struct ragdoll_rotation *frame)
{
	real_vector3d primary;
	real_vector3d secondary;

	if (node == NONE || ragdoll->primary[node] == NONE || ragdoll->secondary[node] == NONE)
		return FALSE;
	vector_from_points3d(&ragdoll->positions[node], &ragdoll->positions[ragdoll->primary[node]], &primary);
	vector_from_points3d(&ragdoll->positions[node], &ragdoll->positions[ragdoll->secondary[node]], &secondary);
	return basis_from_directions(&primary, &secondary, frame);
}

/* a world direction in a frame's terms */
static void rotation_untransform(
	struct ragdoll_rotation const *rotation,
	real_vector3d const *vector,
	real_vector3d *result)
{
	real_vector3d out;

	out.i = dot_product3d(&rotation->columns[0], vector);
	out.j = dot_product3d(&rotation->columns[1], vector);
	out.k = dot_product3d(&rotation->columns[2], vector);
	*result = out;
}

/* the rigid part a node hangs from: its nearest ancestor that branches */
static short ragdoll_frame_node(
	struct ragdoll const *ragdoll,
	short node)
{
	short count;

	for (count = 0; node != NONE && count < MAXIMUM_RAGDOLL_PARTICLES; count++)
	{
		if (node < ragdoll->node_count && ragdoll->secondary[node] != NONE)
			return node;
		node = ragdoll->parents[node];
	}
	return NONE;
}

/* a hinge at parent, between the bones from grandparent and to child,
which bends towards bend_direction (a world direction across the upper
bone), as the frame it hangs from has it now */
static void ragdoll_add_hinge(
	struct ragdoll *ragdoll,
	short grandparent,
	short parent,
	short child,
	real_vector3d const *bend_direction,
	real twist)
{
	short frame_node = ragdoll_frame_node(ragdoll, grandparent);
	struct ragdoll_rotation frame;
	struct ragdoll_joint *joint;
	real_vector3d upper;
	real_vector3d axis;

	if (ragdoll->joint_count >= MAXIMUM_RAGDOLL_JOINTS || !ragdoll_frame(ragdoll, frame_node, &frame))
		return;
	vector_from_points3d(&ragdoll->positions[grandparent], &ragdoll->positions[parent], &upper);
	cross_product3d(&upper, bend_direction, &axis);
	if (normalize3d(&axis) < 1e-4f)
		return;
	joint = &ragdoll->joints[ragdoll->joint_count++];
	joint->type = _ragdoll_joint_hinge;
	joint->frame = frame_node;
	joint->grandparent = grandparent;
	joint->parent = parent;
	joint->child = child;
	rotation_untransform(&frame, &axis, &joint->axis);
	joint->limit = twist;
}

/* a cone the bone from parent to child swings within, round where it
points now, turned towards forward by tilt (a fraction of its own length) */
static void ragdoll_add_cone(
	struct ragdoll *ragdoll,
	short parent,
	short child,
	real_vector3d const *forward,
	real tilt,
	real cosine)
{
	short frame_node = ragdoll_frame_node(ragdoll, ragdoll->parents[parent]);
	struct ragdoll_rotation frame;
	struct ragdoll_joint *joint;
	real_vector3d middle;

	if (ragdoll->joint_count >= MAXIMUM_RAGDOLL_JOINTS || !ragdoll_frame(ragdoll, frame_node, &frame))
		return;
	vector_from_points3d(&ragdoll->positions[parent], &ragdoll->positions[child], &middle);
	if (normalize3d(&middle) < 1e-4f)
		return;
	middle.i += forward->i * tilt;
	middle.j += forward->j * tilt;
	middle.k += forward->k * tilt;
	if (normalize3d(&middle) < 1e-4f)
		return;
	joint = &ragdoll->joints[ragdoll->joint_count++];
	joint->type = _ragdoll_joint_cone;
	joint->frame = frame_node;
	joint->grandparent = NONE;
	joint->parent = parent;
	joint->child = child;
	rotation_untransform(&frame, &middle, &joint->axis);
	joint->limit = cosine;
}

/* a joint put back within its limit, most of the way: the child moved, the
bone's length kept */
static void ragdoll_hold_joint(
	struct ragdoll *ragdoll,
	struct ragdoll_joint const *joint)
{
	struct ragdoll_rotation frame;
	real_point3d *parent = &ragdoll->positions[joint->parent];
	real_point3d *child = &ragdoll->positions[joint->child];
	real_vector3d axis;
	real_vector3d bone;
	real_vector3d held;
	real length;

	if (!ragdoll_frame(ragdoll, joint->frame, &frame))
		return;
	rotation_transform(&frame, &joint->axis, &axis);
	vector_from_points3d(parent, child, &bone);
	length = magnitude3d(&bone);
	if (length < 1e-4f)
		return;

	if (joint->type == _ragdoll_joint_hinge)
	{
		real_vector3d upper;
		real_vector3d bend;
		real along;
		real across;
		real twist;
		real limit = joint->limit * length;

		vector_from_points3d(&ragdoll->positions[joint->grandparent], parent, &upper);
		if (normalize3d(&upper) < 1e-4f)
			return;
		/* (the axis square to the upper bone, and the way it bends square
		to both) */
		along = dot_product3d(&axis, &upper);
		axis.i -= upper.i * along;
		axis.j -= upper.j * along;
		axis.k -= upper.k * along;
		if (normalize3d(&axis) < 1e-4f)
			return;
		cross_product3d(&axis, &upper, &bend);
		along = dot_product3d(&bone, &upper);
		across = dot_product3d(&bone, &bend);
		twist = dot_product3d(&bone, &axis);
		if (across >= 0.f && twist >= -limit && twist <= limit)
			return;
		/* (bent the wrong way: straight; twisted: back to the limit) */
		if (across < 0.f)
			across = 0.f;
		twist = ragdoll_clamp(twist, -limit, limit);
		held.i = upper.i * along + bend.i * across + axis.i * twist;
		held.j = upper.j * along + bend.j * across + axis.j * twist;
		held.k = upper.k * along + bend.k * across + axis.k * twist;
	}
	else
	{
		real cosine;
		real sine;
		real_vector3d across;

		if (normalize3d(&axis) < 1e-4f)
			return;
		scale_vector3d(&bone, 1.f / length, &bone);
		cosine = dot_product3d(&bone, &axis);
		if (cosine >= joint->limit)
			return;
		/* (turned towards the cone's middle, onto its edge) */
		across.i = bone.i - axis.i * cosine;
		across.j = bone.j - axis.j * cosine;
		across.k = bone.k - axis.k * cosine;
		if (normalize3d(&across) < 1e-4f)
			perpendicular3d(&axis, &across);
		sine = square_root(MAX(0.f, 1.f - joint->limit * joint->limit));
		held.i = axis.i * joint->limit + across.i * sine;
		held.j = axis.j * joint->limit + across.j * sine;
		held.k = axis.k * joint->limit + across.k * sine;
	}
	if (normalize3d(&held) < 1e-4f)
		return;
	child->x += (parent->x + held.i * length - child->x) * RAGDOLL_JOINT_STIFFNESS;
	child->y += (parent->y + held.j * length - child->y) * RAGDOLL_JOINT_STIFFNESS;
	child->z += (parent->z + held.k * length - child->z) * RAGDOLL_JOINT_STIFFNESS;
}

/* ---------- building */

static void ragdoll_add_constraint(
	struct ragdoll *ragdoll,
	short a,
	short b,
	short type,
	real length)
{
	struct ragdoll_constraint *constraint;

	if (ragdoll->constraint_count >= MAXIMUM_RAGDOLL_CONSTRAINTS || a == b)
		return;
	constraint = &ragdoll->constraints[ragdoll->constraint_count++];
	constraint->a = a;
	constraint->b = b;
	constraint->type = type;
	constraint->length = length;
}

/* a joint that bends no further than its limit from straight, nor less
than it is bent now: the grandparent and the child no closer */
static void ragdoll_add_bend(
	struct ragdoll *ragdoll,
	short grandparent,
	short parent,
	short child,
	real bend_cosine)
{
	real a = distance3d(&ragdoll->positions[grandparent], &ragdoll->positions[parent]);
	real b = distance3d(&ragdoll->positions[parent], &ragdoll->positions[child]);
	real current = distance3d(&ragdoll->positions[grandparent], &ragdoll->positions[child]);
	real limit_squared = a * a + b * b + 2.f * a * b * bend_cosine;
	real limit = limit_squared > 0.f ? square_root(limit_squared) : 0.f;

	ragdoll_add_constraint(ragdoll, grandparent, child, _ragdoll_constraint_minimum,
		current < limit ? current : limit);
}

static boolean ragdoll_stiff_node(
	struct model_node const *node)
{
	return name_contains(node->name, "spine") || name_contains(node->name, "neck") ||
		name_contains(node->name, "pelvis") || name_contains(node->name, "head");
}

/* the model's nodes, linked as they say (a map's counts and indices); the
nodes parents first in order[] */
static boolean ragdoll_order_nodes(
	struct model *model,
	short node_count,
	short *order)
{
	short count = 1;
	short head = 0;

	order[0] = 0;
	while (head < count)
	{
		struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, order[head], struct model_node);
		short child = node->first_child_node_index;

		head++;
		while (child != NONE)
		{
			struct model_node *child_node;

			if (!VALID_INDEX(child, node_count) || count >= node_count)
				return FALSE;
			child_node = TAG_BLOCK_GET_ELEMENT(&model->nodes, child, struct model_node);
			if (child_node->parent_node_index != order[head - 1])
				return FALSE;
			order[count++] = child;
			child = child_node->next_sibling_node_index;
		}
	}
	return count == node_count;
}

/* the parts of the body, by their nodes' names (as Halo's bipeds name them) */
enum
{
	_ragdoll_part_other = 0,
	_ragdoll_part_torso,
	_ragdoll_part_head,
	_ragdoll_part_upper_limb,
	_ragdoll_part_lower_limb,
	_ragdoll_part_end,
};

static short ragdoll_node_part(
	struct model_node const *node)
{
	if (name_contains(node->name, "pelvis") || name_contains(node->name, "spine") ||
		name_contains(node->name, "neck"))
	{
		return _ragdoll_part_torso;
	}
	if (name_contains(node->name, "head"))
		return _ragdoll_part_head;
	if (name_contains(node->name, "thigh") || name_contains(node->name, "upperarm") ||
		name_contains(node->name, "clavicle"))
	{
		return _ragdoll_part_upper_limb;
	}
	if (name_contains(node->name, "calf") || name_contains(node->name, "forearm"))
		return _ragdoll_part_lower_limb;
	if (name_contains(node->name, "hand") || name_contains(node->name, "foot") ||
		name_contains(node->name, "finger") || name_contains(node->name, "toe"))
	{
		return _ragdoll_part_end;
	}
	return _ragdoll_part_other;
}

/* how many particles apart two are along the skeleton, up to limit */
static short ragdoll_separation(
	struct ragdoll const *ragdoll,
	short a,
	short b,
	short limit)
{
	short up_a;
	short steps_a;

	for (up_a = a, steps_a = 0; up_a != NONE && steps_a <= limit; up_a = ragdoll->parents[up_a], steps_a++)
	{
		short up_b;
		short steps_b;

		for (up_b = b, steps_b = 0; up_b != NONE && steps_a + steps_b <= limit;
			up_b = ragdoll->parents[up_b], steps_b++)
		{
			if (up_a == up_b)
				return (short)(steps_a + steps_b);
		}
	}
	return (short)(limit + 1);
}

/* halo2's body: the parts' weights; a particle midway along each long bone
(a limb's and the spine's), which gives them thickness against the world;
and the limbs kept out of the torso and of each other */
static void ragdoll_build_halo2(
	struct ragdoll *ragdoll,
	struct model *model)
{
	short parts[MAXIMUM_RAGDOLL_PARTICLES];
	short node_count = ragdoll->node_count;
	short particle_count = ragdoll->particle_count;
	short middles = 0;
	short index;

	for (index = 0; index < particle_count; index++)
	{
		short part = index < node_count ?
			ragdoll_node_part(TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node)) :
			_ragdoll_part_head; /* (the head's top) */

		parts[index] = part;
		ragdoll->inverse_masses[index] =
			part == _ragdoll_part_torso ? RAGDOLL_HALO2_TORSO_INVERSE_MASS :
			part == _ragdoll_part_head ? RAGDOLL_HALO2_HEAD_INVERSE_MASS :
			part == _ragdoll_part_upper_limb ? RAGDOLL_HALO2_UPPER_INVERSE_MASS :
			part == _ragdoll_part_end ? RAGDOLL_HALO2_END_INVERSE_MASS : 1.f;
	}

	/* (midway along a bone from a limb's or the spine's node to its one
	child, held there by the bone's two halves) */
	for (index = 1; index < node_count && middles < MAXIMUM_RAGDOLL_MIDDLES; index++)
	{
		short parent = ragdoll->parents[index];
		short middle = ragdoll->particle_count;
		real length;

		if (parent == NONE || ragdoll->primary[parent] != index || ragdoll->secondary[parent] != NONE ||
			(parts[parent] != _ragdoll_part_upper_limb && parts[parent] != _ragdoll_part_lower_limb &&
				parts[parent] != _ragdoll_part_torso))
		{
			continue;
		}
		length = distance3d(&ragdoll->positions[parent], &ragdoll->positions[index]);
		if (length < 4.f * RAGDOLL_RADIUS)
			continue;
		ragdoll->positions[middle].x = 0.5f * (ragdoll->positions[parent].x + ragdoll->positions[index].x);
		ragdoll->positions[middle].y = 0.5f * (ragdoll->positions[parent].y + ragdoll->positions[index].y);
		ragdoll->positions[middle].z = 0.5f * (ragdoll->positions[parent].z + ragdoll->positions[index].z);
		ragdoll->parents[middle] = parent;
		ragdoll->velocities[middle] = *global_zero_vector3d;
		ragdoll->inverse_masses[middle] = ragdoll->inverse_masses[parent];
		parts[middle] = parts[parent];
		ragdoll_add_constraint(ragdoll, parent, middle, _ragdoll_constraint_length, 0.5f * length);
		ragdoll_add_constraint(ragdoll, middle, index, _ragdoll_constraint_length, 0.5f * length);
		ragdoll->particle_count++;
		middles++;
	}

	/* (no closer than they are now, if that is already closer) */
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		short other;
		boolean limb = parts[index] == _ragdoll_part_lower_limb || parts[index] == _ragdoll_part_end;

		if (!limb)
			continue;
		for (other = 0; other < ragdoll->particle_count; other++)
		{
			real clearance;
			real now;

			if (other == index)
				continue;
			if (parts[other] == _ragdoll_part_torso || parts[other] == _ragdoll_part_head)
				clearance = RAGDOLL_HALO2_TORSO_CLEARANCE;
			else if ((parts[other] == _ragdoll_part_lower_limb || parts[other] == _ragdoll_part_end) && other > index)
				clearance = RAGDOLL_HALO2_LIMB_CLEARANCE;
			else
				continue;
			if (ragdoll_separation(ragdoll, index, other, 3) <= 3)
				continue;
			now = distance3d(&ragdoll->positions[index], &ragdoll->positions[other]);
			ragdoll_add_constraint(ragdoll, index, other, _ragdoll_constraint_minimum,
				now < clearance ? now : clearance);
		}
	}
}

static boolean ragdoll_build(
	struct ragdoll *ragdoll,
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	struct model *model;
	real_matrix4x3 *node_matrices;
	short node_count;
	short tips = 0;
	short index;
	short children[MAXIMUM_RAGDOLL_PARTICLES][3];
	short child_counts[MAXIMUM_RAGDOLL_PARTICLES];
	boolean halo2;

	if (definition->object.model.index == NONE)
		return FALSE;
	model = model_definition_get(definition->object.model.index);
	node_count = (short)model->nodes.count;
	if (node_count < 2 || node_count > MAXIMUM_NODES_PER_MODEL ||
		node_count > biped->object.node_matrices.size / (long)sizeof(real_matrix4x3))
	{
		return FALSE;
	}
	if (!ragdoll_order_nodes(model, node_count, ragdoll->order))
		return FALSE;

	halo2 = ragdoll->style == _ragdoll_style_halo2;
	ragdoll->friction = halo2 ? RAGDOLL_HALO2_FRICTION : RAGDOLL_FRICTION;
	ragdoll->joint_friction = halo2 ? RAGDOLL_HALO2_JOINT_FRICTION : 0.f;
	ragdoll->blast_scale = halo2 ? RAGDOLL_HALO2_BLAST_SCALE : 1.f;
	node_matrices = object_get_node_matrices(biped_index);
	csmemcpy(ragdoll->pose, node_matrices, node_count * sizeof(real_matrix4x3));
	ragdoll->node_count = node_count;
	ragdoll->particle_count = node_count;
	ragdoll->constraint_count = 0;
	csmemset(child_counts, 0, sizeof(child_counts));
	for (index = 0; index < node_count; index++)
	{
		struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);

		ragdoll->positions[index] = node_matrices[index].position;
		ragdoll->parents[index] = index ? node->parent_node_index : NONE;
		if (index && child_counts[node->parent_node_index] < 3)
			children[node->parent_node_index][child_counts[node->parent_node_index]++] = index;
	}

	/* the head's top, beyond the neck as the neck goes */
	for (index = 1; index < node_count && tips < MAXIMUM_RAGDOLL_TIPS; index++)
	{
		struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);
		short tip = ragdoll->particle_count;
		real_vector3d direction;
		real length;

		if (node->first_child_node_index != NONE || !name_contains(node->name, "head"))
			continue;
		vector_from_points3d(&ragdoll->positions[node->parent_node_index], &ragdoll->positions[index], &direction);
		length = normalize3d(&direction);
		if (length < 1e-4f)
			direction = node_matrices[index].forward;
		length = ragdoll_clamp(length * 1.5f, RAGDOLL_TIP_MINIMUM_LENGTH, RAGDOLL_TIP_MAXIMUM_LENGTH);
		point_from_line3d(&ragdoll->positions[index], &direction, length, &ragdoll->positions[tip]);
		ragdoll->parents[tip] = index;
		children[index][child_counts[index]++] = tip;
		ragdoll->particle_count++;
		tips++;
	}

	for (index = 0; index < ragdoll->particle_count; index++)
	{
		short parent = ragdoll->parents[index];
		short grandparent = parent != NONE ? ragdoll->parents[parent] : NONE;

		/* the bones */
		if (parent != NONE)
		{
			ragdoll_add_constraint(ragdoll, parent, index, _ragdoll_constraint_length,
				distance3d(&ragdoll->positions[parent], &ragdoll->positions[index]));
		}
		/* rigid where a bone branches (hips, chest, hands): its ends, and
		them with where it comes from */
		if (child_counts[index] >= 2)
		{
			short first;

			for (first = 0; first < child_counts[index]; first++)
			{
				short second;

				for (second = first + 1; second < child_counts[index]; second++)
				{
					ragdoll_add_constraint(ragdoll, children[index][first], children[index][second],
						_ragdoll_constraint_length,
						distance3d(&ragdoll->positions[children[index][first]],
							&ragdoll->positions[children[index][second]]));
				}
				if (parent != NONE)
				{
					ragdoll_add_constraint(ragdoll, parent, children[index][first], _ragdoll_constraint_length,
						distance3d(&ragdoll->positions[parent], &ragdoll->positions[children[index][first]]));
				}
			}
		}
		/* how far a joint bends, where it doesn't branch */
		if (grandparent != NONE && child_counts[parent] == 1)
		{
			boolean stiff = parent < node_count && ragdoll_stiff_node(
				TAG_BLOCK_GET_ELEMENT(&model->nodes, parent, struct model_node));

			if (index >= node_count)
				stiff = TRUE;
			ragdoll_add_bend(ragdoll, grandparent, parent, index,
				stiff ? RAGDOLL_STIFF_BEND_COSINE : RAGDOLL_BEND_COSINE);
		}
	}

	for (index = 0; index < node_count; index++)
	{
		short other;
		real squarest = 2.f;

		ragdoll->primary[index] = child_counts[index] > 0 ? children[index][0] : NONE;
		ragdoll->secondary[index] = NONE;
		/* (of the others, the end most nearly square to it: not the hips'
		two sides, nearly in a line through the pelvis) */
		for (other = 1; other < child_counts[index]; other++)
		{
			real_vector3d first;
			real_vector3d second;
			real cosine;

			vector_from_points3d(&ragdoll->positions[index], &ragdoll->positions[children[index][0]], &first);
			vector_from_points3d(&ragdoll->positions[index], &ragdoll->positions[children[index][other]], &second);
			if (normalize3d(&first) < 1e-4f || normalize3d(&second) < 1e-4f)
				continue;
			cosine = (real)fabs(dot_product3d(&first, &second));
			if (cosine < squarest)
			{
				squarest = cosine;
				ragdoll->secondary[index] = children[index][other];
			}
		}
	}

	/* the knees bend back and the elbows forward (as the body faces, or as
	an elbow is bent already), and the hips and shoulders swing only so far */
	ragdoll->joint_count = 0;
	for (index = 1; index < node_count; index++)
	{
		struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);
		short parent = node->parent_node_index;
		short grandparent = parent ? ragdoll->parents[parent] : NONE;
		struct model_node *parent_node = TAG_BLOCK_GET_ELEMENT(&model->nodes, parent, struct model_node);
		real_vector3d bend;

		if (name_contains(parent_node->name, "thigh"))
			ragdoll_add_cone(ragdoll, parent, index, &biped->object.forward, RAGDOLL_HIP_TILT, RAGDOLL_HIP_COSINE);
		else if (name_contains(parent_node->name, "upperarm"))
			ragdoll_add_cone(ragdoll, parent, index, &biped->object.forward,
				halo2 ? RAGDOLL_HALO2_SHOULDER_TILT : 0.f, halo2 ? RAGDOLL_HALO2_SHOULDER_COSINE : RAGDOLL_SHOULDER_COSINE);
		if (grandparent == NONE || child_counts[parent] != 1)
			continue;
		if (name_contains(parent_node->name, "calf"))
		{
			scale_vector3d(&biped->object.forward, -1.f, &bend);
			ragdoll_add_hinge(ragdoll, grandparent, parent, index, &bend,
				halo2 ? RAGDOLL_HALO2_KNEE_TWIST : RAGDOLL_KNEE_TWIST);
		}
		else if (name_contains(parent_node->name, "forearm"))
		{
			real_vector3d upper;
			real_vector3d lower;

			vector_from_points3d(&ragdoll->positions[grandparent], &ragdoll->positions[parent], &upper);
			vector_from_points3d(&ragdoll->positions[parent], &ragdoll->positions[index], &lower);
			normalize3d(&upper);
			normalize3d(&lower);
			if (dot_product3d(&upper, &lower) < 0.94f) /* (bent more than 20 degrees) */
				bend = lower;
			else
				bend = biped->object.forward;
			ragdoll_add_hinge(ragdoll, grandparent, parent, index, &bend,
				halo2 ? RAGDOLL_HALO2_ELBOW_TWIST : RAGDOLL_ELBOW_TWIST);
		}
	}

	for (index = 0; index < ragdoll->particle_count; index++)
		ragdoll->inverse_masses[index] = 1.f;
	if (halo2)
		ragdoll_build_halo2(ragdoll, model);

	/* where the world lets each particle be, reached from its parent's (an
	animated pose can reach into a wall or the ground) */
	{
		for (index = 0; index < ragdoll->particle_count; index++)
			ragdoll->velocities[index] = *global_zero_vector3d;
		ragdoll_gather_features(ragdoll, biped_index);
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			short particle = index < node_count ? ragdoll->order[index] : index;
			short parent = ragdoll->parents[particle];
			real_point3d desired = ragdoll->positions[particle];
			real_point3d start = parent != NONE ? ragdoll->positions[parent] : biped->object.position;

			ragdoll->positions[particle] = start;
			ragdoll_move(&ragdoll->positions[particle], &desired, NULL);
		}
	}
	return TRUE;
}

/* a push on the body, hardest nearest the epicenter: the same in all as a
push on the whole body, it turns the body too */
static void ragdoll_push(
	struct ragdoll *ragdoll,
	real_point3d const *epicenter,
	real_vector3d const *speed,
	real blow_radius,
	boolean spread)
{
	real weights[MAXIMUM_RAGDOLL_PARTICLES];
	real total = 0.f;
	short index;

	for (index = 0; index < ragdoll->particle_count; index++)
	{
		real distance = distance3d(epicenter, &ragdoll->positions[index]) / blow_radius;

		weights[index] = 1.f / (1.f + distance * distance);
		total += weights[index];
	}
	if (total <= 0.f)
		return;
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		real scale = spread ?
			ragdoll_clamp(weights[index] * ragdoll->particle_count / total, 0.25f, 3.f) :
			weights[index];
		real_vector3d push;

		scale_vector3d(speed, scale, &push);
		add_vectors3d(&ragdoll->velocities[index], &push, &ragdoll->velocities[index]);
		ragdoll_clamp_speed(&ragdoll->velocities[index]);
	}
}

static struct ragdoll_blow const *ragdoll_killing_blow(
	long unit_index)
{
	long now = game_time_get();
	long index;

	for (index = 0; index < MAXIMUM_RAGDOLL_BLOWS; index++)
	{
		struct ragdoll_blow const *blow = &ragdoll_blows[index];

		if (blow->unit_index == unit_index && blow->time <= now && now - blow->time <= RAGDOLL_BLOW_TICKS)
			return blow;
	}
	return NULL;
}

/* lying still: its pose kept as it is; a body the ragdoll moves comes to rest
as a limp body that has finished relaxing, so that the stock game's code
finds its pose fixed where it lies (should it lose its ragdoll) */
static void ragdoll_sleep(
	struct ragdoll *ragdoll)
{
	struct biped_datum *biped = ragdoll_body(ragdoll);
	short index;

	ragdoll->asleep = TRUE;
	for (index = 0; index < ragdoll->particle_count; index++)
		ragdoll->velocities[index] = *global_zero_vector3d;
	if (!biped || ragdoll->drawn_only)
		return;
	biped->object.translational_velocity = *global_zero_vector3d;
	biped->object.angular_velocity = *global_zero_vector3d;
	SET_FLAG(biped->object.flags, _object_at_rest_bit, TRUE);
	SET_FLAG(biped->biped.flags, _biped_airborne_bit, FALSE);
	SET_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit, TRUE);
	SET_FLAG(biped->unit.animation.flags, _unit_animation_ignore_translation_bit, TRUE);
	biped->biped.limp_body_current_relaxation_iterations = biped->biped.limp_body_max_relaxation_iterations;
}

static void ragdoll_wake(
	struct ragdoll *ragdoll,
	struct biped_datum *biped)
{
	ragdoll->asleep = FALSE;
	ragdoll->still_ticks = 0;
	ragdoll->ticks = 0;
	if (ragdoll->drawn_only)
		return;
	SET_FLAG(biped->object.flags, _object_at_rest_bit, FALSE);
	SET_FLAG(biped->object.flags, _object_do_not_recompute_node_matrices_bit, TRUE);
	SET_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit, FALSE);
}

/* a body that loses its ragdoll is drawn as the stock game draws it: by its
animation, or as a limp body if it lies as one */
static void ragdoll_release(
	struct ragdoll *ragdoll)
{
	struct biped_datum *biped = ragdoll_body(ragdoll);

	if (biped && !ragdoll->drawn_only && !TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit))
		SET_FLAG(biped->object.flags, _object_do_not_recompute_node_matrices_bit, FALSE);
	ragdoll->object_index = NONE;
}

/* a ragdoll for a body: a free one, one whose body is gone, or the longest
asleep (or else the oldest), whose body the stock game draws again */
static struct ragdoll *ragdoll_new(
	long object_index)
{
	struct ragdoll *chosen = NULL;
	long index;

	ragdolls_initialize();
	for (index = 0; index < MAXIMUM_RAGDOLLS; index++)
	{
		struct ragdoll *ragdoll = &ragdolls[index];

		if (ragdoll->object_index == NONE || !ragdoll_body(ragdoll))
		{
			chosen = ragdoll;
			break;
		}
		if (!chosen ||
			(ragdoll->asleep && !chosen->asleep) ||
			(ragdoll->asleep == chosen->asleep && ragdoll->start_time < chosen->start_time))
		{
			chosen = ragdoll;
		}
	}
	if (chosen->object_index != NONE)
		ragdoll_release(chosen);
	csmemset(chosen, 0, sizeof(*chosen));
	chosen->object_index = object_index;
	chosen->start_time = game_time_get();
	return chosen;
}

static boolean ragdoll_should_start(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	long now = game_time_get();

	/* a cinematic, and a body the scene marks as an actor, keep the
	scripted death */
	if (cinematic_in_progress() || TEST_FLAG(biped->object.flags, _object_movie_star_bit))
		return FALSE;
	return ragdoll_style(biped_index) != _ragdoll_style_none &&
		TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
		biped->object.parent_object_index == NONE &&
		biped->unit.time_of_death != NONE &&
		biped->unit.time_of_death <= now &&
		now - biped->unit.time_of_death <= RAGDOLL_BLOW_TICKS &&
		!TEST_FLAG(definition->unit.flags, _unit_is_destroyed_after_dying_bit);
}

static void ragdoll_start(
	struct ragdoll *ragdoll,
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct object_definition *definition = object_definition_get(biped->definition_index);
	struct ragdoll_blow const *blow = ragdoll_killing_blow(biped_index);
	real_vector3d velocity = biped->object.translational_velocity;
	short index;

	ragdoll_clamp_speed(&velocity);
	for (index = 0; index < ragdoll->particle_count; index++)
		ragdoll->velocities[index] = velocity;

	if (blow)
	{
		real_vector3d direction = blow->direction;
		real_vector3d speed;

		/* (as the damage pushes a body, damage.c: up a little; half of it
		already in the body's speed, from when it was alive) */
		if (blow->speed > 0.f && definition->object.acceleration_scale > _real_epsilon)
		{
			direction.k += 0.45f;
			if (normalize3d(&direction) > 0.f)
			{
				scale_vector3d(&direction, 0.5f * blow->speed * definition->object.acceleration_scale *
					ragdoll->blast_scale, &speed);
				ragdoll_push(ragdoll, &blow->epicenter, &speed, RAGDOLL_BLOW_RADIUS, TRUE);
			}
		}
		/* the whole body pushed the way the blow came, turned by where it
		hit, and the shot's own push there */
		direction = blow->direction;
		if (normalize3d(&direction) > 0.f)
		{
			scale_vector3d(&direction, RAGDOLL_BLOW_SPEED, &speed);
			ragdoll_push(ragdoll, &blow->epicenter, &speed, RAGDOLL_BLOW_RADIUS, TRUE);
			scale_vector3d(&direction, RAGDOLL_HIT_SPEED, &speed);
			ragdoll_push(ragdoll, &blow->epicenter, &speed, RAGDOLL_HIT_RADIUS, FALSE);
		}
	}

	ragdoll->drawn_only = game_connection() == _game_connection_network_server;
	if (!ragdoll->drawn_only)
	{
		SET_FLAG(biped->object.flags, _object_do_not_recompute_node_matrices_bit, TRUE);
		SET_FLAG(biped->object.flags, _object_at_rest_bit, FALSE);
		SET_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit, FALSE);
	}
}

/* ---------- simulation */

static void ragdoll_solve(
	struct ragdoll *ragdoll)
{
	long iteration;

	for (iteration = 0; iteration < RAGDOLL_ITERATIONS; iteration++)
	{
		short index;

		/* (the joints first: the bones' lengths have the last word) */
		for (index = 0; index < ragdoll->joint_count; index++)
			ragdoll_hold_joint(ragdoll, &ragdoll->joints[index]);
		for (index = 0; index < ragdoll->constraint_count; index++)
		{
			struct ragdoll_constraint const *constraint = &ragdoll->constraints[index];
			real_point3d *a = &ragdoll->positions[constraint->a];
			real_point3d *b = &ragdoll->positions[constraint->b];
			real_vector3d offset;
			real distance;
			real correction;
			real weight_a;
			real weight_b;

			vector_from_points3d(a, b, &offset);
			distance = magnitude3d(&offset);
			if (distance < 1e-6f)
				continue;
			if (constraint->type == _ragdoll_constraint_minimum && distance >= constraint->length)
				continue;
			/* (each end moved as readily as it moves: the lighter further) */
			weight_a = ragdoll->inverse_masses[constraint->a];
			weight_b = ragdoll->inverse_masses[constraint->b];
			if (weight_a + weight_b <= 0.f)
				continue;
			correction = (distance - constraint->length) / (distance * (weight_a + weight_b));
			a->x += offset.i * correction * weight_a;
			a->y += offset.j * correction * weight_a;
			a->z += offset.k * correction * weight_a;
			b->x -= offset.i * correction * weight_b;
			b->y -= offset.j * correction * weight_b;
			b->z -= offset.k * correction * weight_b;
		}
	}
}

/* half a tick: gravity and the speeds move the particles as far as the world
lets them, the bones pull them back together, as far as the world lets
them, and what they ran into slows them */
static void ragdoll_substep(
	struct ragdoll *ragdoll)
{
	real_point3d starts[MAXIMUM_RAGDOLL_PARTICLES];
	real_point3d moved[MAXIMUM_RAGDOLL_PARTICLES];
	real_vector3d normals[MAXIMUM_RAGDOLL_PARTICLES];
	boolean hits[MAXIMUM_RAGDOLL_PARTICLES];
	real const step = 1.f / RAGDOLL_SUBSTEPS;
	short index;

	for (index = 0; index < ragdoll->particle_count; index++)
	{
		real_vector3d *velocity = &ragdoll->velocities[index];
		real_point3d destination;

		velocity->k -= global_gravity * step;
		scale_vector3d(velocity, RAGDOLL_DAMPING, velocity);
		ragdoll_clamp_speed(velocity);
		starts[index] = ragdoll->positions[index];
		point_from_line3d(&starts[index], velocity, step, &destination);
		hits[index] = ragdoll_move(&ragdoll->positions[index], &destination, &normals[index]);
		moved[index] = ragdoll->positions[index];
	}

	ragdoll_solve(ragdoll);

	for (index = 0; index < ragdoll->particle_count; index++)
	{
		real_point3d solved = ragdoll->positions[index];
		real_vector3d normal;
		real_vector3d *velocity = &ragdoll->velocities[index];

		ragdoll->positions[index] = moved[index];
		if (ragdoll_move(&ragdoll->positions[index], &solved, &normal))
		{
			hits[index] = TRUE;
			normals[index] = normal;
		}
		vector_from_points3d(&starts[index], &ragdoll->positions[index], velocity);
		scale_vector3d(velocity, 1.f / step, velocity);
		if (hits[index])
		{
			real_vector3d const *normal = &normals[index];
			real into = dot_product3d(velocity, normal);
			real away;

			/* (nothing into what it lies on, and less along it) */
			if (into < 0.f)
			{
				velocity->i -= normal->i * into;
				velocity->j -= normal->j * into;
				velocity->k -= normal->k * into;
			}
			away = dot_product3d(velocity, normal);
			velocity->i = normal->i * away + (velocity->i - normal->i * away) * ragdoll->friction;
			velocity->j = normal->j * away + (velocity->j - normal->j * away) * ragdoll->friction;
			velocity->k = normal->k * away + (velocity->k - normal->k * away) * ragdoll->friction;
		}
	}

	/* (joints that drag: a part's swing about its parent slowed) */
	if (ragdoll->joint_friction > 0.f)
	{
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			short parent = ragdoll->parents[index];
			real_vector3d *velocity = &ragdoll->velocities[index];

			if (parent == NONE)
				continue;
			velocity->i -= (velocity->i - ragdoll->velocities[parent].i) * ragdoll->joint_friction;
			velocity->j -= (velocity->j - ragdoll->velocities[parent].j) * ragdoll->joint_friction;
			velocity->k -= (velocity->k - ragdoll->velocities[parent].k) * ragdoll->joint_friction;
		}
	}
}

/* the nodes turned as their bones turned, parents first: a bone that
branches by its two ends, one that doesn't the shortest way (twisting as
little as it can), and a node with no bone of its own as its parent */
static void ragdoll_pose(
	struct ragdoll const *ragdoll,
	real_point3d const *previous,
	real_matrix4x3 *node_matrices)
{
	struct ragdoll_rotation turns[MAXIMUM_NODES_PER_MODEL];
	short order_index;

	for (order_index = 0; order_index < ragdoll->node_count; order_index++)
	{
		short node = ragdoll->order[order_index];
		short primary = ragdoll->primary[node];
		short secondary = ragdoll->secondary[node];
		struct ragdoll_rotation *turn = &turns[node];
		real_matrix4x3 *matrix = &node_matrices[node];
		boolean turned = FALSE;

		if (primary != NONE)
		{
			real_vector3d before;
			real_vector3d after;

			vector_from_points3d(&previous[node], &previous[primary], &before);
			vector_from_points3d(&ragdoll->positions[node], &ragdoll->positions[primary], &after);
			if (secondary != NONE)
			{
				real_vector3d before_across;
				real_vector3d after_across;
				struct ragdoll_rotation from;
				struct ragdoll_rotation to;

				vector_from_points3d(&previous[node], &previous[secondary], &before_across);
				vector_from_points3d(&ragdoll->positions[node], &ragdoll->positions[secondary], &after_across);
				if (basis_from_directions(&before, &before_across, &from) &&
					basis_from_directions(&after, &after_across, &to))
				{
					rotation_between_bases(&from, &to, turn);
					turned = TRUE;
				}
			}
			if (!turned && normalize3d(&before) > 1e-4f && normalize3d(&after) > 1e-4f)
			{
				rotation_between(&before, &after, turn);
				turned = TRUE;
			}
		}
		if (!turned)
		{
			if (node && ragdoll->parents[node] != NONE)
				*turn = turns[ragdoll->parents[node]];
			else
				rotation_identity(turn);
		}

		rotation_transform(turn, &matrix->forward, &matrix->forward);
		rotation_transform(turn, &matrix->up, &matrix->up);
		if (normalize3d(&matrix->forward) == 0.f)
			matrix->forward = *global_forward3d;
		cross_product3d(&matrix->up, &matrix->forward, &matrix->left);
		if (normalize3d(&matrix->left) == 0.f)
			perpendicular3d(&matrix->forward, &matrix->left);
		cross_product3d(&matrix->forward, &matrix->left, &matrix->up);
		normalize3d(&matrix->up);
		matrix->position = ragdoll->positions[node];
	}
}

/* a body the ragdoll moves: its nodes as the ragdoll lies, kept from being
worked out again from its animation, its origin at the root, and its bounds
round the whole of it, as the camera, the map and the renderer find it; a
ragdoll asleep keeps them */
static void ragdoll_move_body(
	struct ragdoll const *ragdoll,
	long biped_index,
	boolean moved)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct object_definition *definition = object_definition_get(biped->definition_index);
	real bounds = definition->object.bounding_radius;
	real_point3d center;
	real radius;

	csmemcpy(object_get_node_matrices(biped_index), ragdoll->pose, ragdoll->node_count * sizeof(real_matrix4x3));
	SET_FLAG(biped->object.flags, _object_do_not_recompute_node_matrices_bit, TRUE);
	if (!moved)
		return;
	ragdoll_bounds(ragdoll, FALSE, &center, &radius);
	if (biped->object.scale > 0.f)
		bounds *= biped->object.scale;
	if (radius + 2.f * RAGDOLL_RADIUS > bounds)
		bounds = radius + 2.f * RAGDOLL_RADIUS;
	object_disconnect_from_map(biped_index);
	biped->object.position = ragdoll->positions[0];
	biped->object.bounding_sphere_center = center;
	biped->object.bounding_sphere_radius = bounds;
	object_bounds_cache_update(biped_index, biped->object.type, &center, bounds);
	object_reconnect_to_map(biped_index, NULL);
	biped->object.translational_velocity = ragdoll->velocities[0];
	biped->object.angular_velocity = *global_zero_vector3d;
}

static void ragdoll_step(
	struct ragdoll *ragdoll,
	long biped_index)
{
	real_point3d previous[MAXIMUM_RAGDOLL_PARTICLES];
	real fastest = 0.f;
	short index;
	real min_x, max_x, min_y, max_y, min_z, max_z;
	boolean lying_down;

	match_assert(__FILE__, __LINE__, global_current_collision_user_depth < MAXIMUM_COLLISION_USER_STACK_DEPTH);
	global_current_collision_users[global_current_collision_user_depth++] = _collision_user_limp_body_physics;

	csmemcpy(previous, ragdoll->positions, ragdoll->particle_count * sizeof(real_point3d));
	ragdoll_gather_features(ragdoll, biped_index);
	for (index = 0; index < RAGDOLL_SUBSTEPS; index++)
		ragdoll_substep(ragdoll);

	match_assert(__FILE__, __LINE__, global_current_collision_user_depth > 1);
	--global_current_collision_user_depth;

	ragdoll_pose(ragdoll, previous, ragdoll->pose);

	min_x = max_x = ragdoll->positions[0].x;
	min_y = max_y = ragdoll->positions[0].y;
	min_z = max_z = ragdoll->positions[0].z;
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		real speed = magnitude3d(&ragdoll->velocities[index]);
		real_point3d *p = &ragdoll->positions[index];

		if (speed > fastest)
			fastest = speed;
		if (p->x < min_x) min_x = p->x; else if (p->x > max_x) max_x = p->x;
		if (p->y < min_y) min_y = p->y; else if (p->y > max_y) max_y = p->y;
		if (p->z < min_z) min_z = p->z; else if (p->z > max_z) max_z = p->z;
	}
	/* port: the body is down when it lies flatter than it stands -- its
	vertical (z) spread no greater than its widest horizontal spread. Only a
	body that is down may be put to sleep by the half-second stillness timer;
	otherwise one that stalls while still upright (a gentle death, or caught on
	geometry) would sleep standing. The 10-second RAGDOLL_MAXIMUM_TICKS safety
	sleep stays unconditional, so nothing can twitch forever. */
	lying_down = (max_z - min_z) <= MAX(max_x - min_x, max_y - min_y);
	ragdoll->still_ticks = fastest < RAGDOLL_SLEEP_SPEED ? ragdoll->still_ticks + 1 : 0;
	ragdoll->ticks++;
	if ((ragdoll->still_ticks >= RAGDOLL_SLEEP_TICKS && lying_down) || ragdoll->ticks >= RAGDOLL_MAXIMUM_TICKS)
		ragdoll_sleep(ragdoll);
}

boolean biped_ragdoll_update(
	long biped_index)
{
	struct ragdoll *ragdoll = ragdoll_find(biped_index);
	boolean moved;

	if (ragdoll && !ragdoll_body(ragdoll))
	{
		ragdoll_release(ragdoll);
		return FALSE;
	}
	if (!ragdoll)
	{
		if (!ragdoll_should_start(biped_index))
			return FALSE;
		ragdoll = ragdoll_new(biped_index);
		ragdoll->style = ragdoll_style(biped_index);
		if (!ragdoll_build(ragdoll, biped_index))
		{
			ragdoll->object_index = NONE;
			return FALSE;
		}
		ragdoll_start(ragdoll, biped_index);
	}
	moved = !ragdoll->asleep;
	if (moved)
		ragdoll_step(ragdoll, biped_index);
	if (ragdoll->drawn_only)
		return FALSE;
	ragdoll_move_body(ragdoll, biped_index, moved);
	return TRUE;
}

boolean biped_ragdoll_active(
	long object_index)
{
	struct ragdoll *ragdoll = ragdoll_find(object_index);

	return ragdoll && ragdoll_body(ragdoll);
}

boolean biped_ragdoll_accelerate(
	long biped_index,
	real_point3d const *epicenter,
	real_vector3d const *acceleration)
{
	struct ragdoll *ragdoll = ragdoll_find(biped_index);
	struct biped_datum *biped;
	real_vector3d push;

	if (!ragdoll || (biped = ragdoll_body(ragdoll)) == NULL)
		return FALSE;
	if (ragdoll->asleep)
		ragdoll_wake(ragdoll, biped);
	scale_vector3d(acceleration, ragdoll->blast_scale, &push);
	ragdoll_push(ragdoll, epicenter, &push, RAGDOLL_BLOW_RADIUS, TRUE);
	return !ragdoll->drawn_only;
}

real_matrix4x3 *biped_ragdoll_drawn_nodes(
	long object_index)
{
	struct ragdoll *ragdoll;
	struct object_datum *object = object_try_and_get_and_verify_type(object_index, _object_mask_biped);

	if (!object || !TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return NULL;
	ragdoll = ragdoll_find(object_index);
	if (!ragdoll || !ragdoll->drawn_only ||
		ragdoll->node_count != object->object.node_matrices.size / (long)sizeof(real_matrix4x3))
	{
		return NULL;
	}
	return ragdoll->pose;
}

void biped_ragdoll_note_killing_blow(
	long unit_index,
	real_point3d const *epicenter,
	real_vector3d const *direction,
	real instantaneous_acceleration)
{
	struct ragdoll_blow *blow;

	if (ragdoll_style(unit_index) == _ragdoll_style_none)
		return;
	ragdolls_initialize();
	blow = &ragdoll_blows[ragdoll_next_blow];
	ragdoll_next_blow = (ragdoll_next_blow + 1) % MAXIMUM_RAGDOLL_BLOWS;
	blow->unit_index = unit_index;
	blow->time = game_time_get();
	blow->epicenter = *epicenter;
	blow->direction = *direction;
	/* (world units a tick, as damage.c pushes) */
	blow->speed = instantaneous_acceleration > _real_epsilon ? instantaneous_acceleration / TICKS_PER_SECOND : 0.f;
}

void biped_ragdoll_reset(
	void)
{
	ragdolls_initialized = FALSE;
	ragdolls_initialize();
}
