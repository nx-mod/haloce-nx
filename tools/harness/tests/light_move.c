#include "harness.h"
#include <string.h>
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define SET_FLAG(f,b,v) ((v)?((f)|=(1<<(b))):((f)&=~(1<<(b))))
enum { _point_light_connects_to_map_bit, _point_light_connected_to_map_bit, MAXIMUM_LIGHTS_PER_MAP=16 };
struct light_datum { short flags; long cluster_reference; real_point3d position; };
static struct light_datum the_light;
static struct { unsigned long radius_writes; } lights_port;
static struct light_datum *light_get(long i) { return &the_light; }
static real_point3d pose, sphere_center;
static real sphere_radius=1;
static int poses, joins, leaves;
static void light_compute_pose(struct light_datum *light) { poses++; light->position=pose; sphere_center=pose; }
static void light_compute_bounding_sphere(long i,boolean m,boolean s,boolean f,real_point3d *p,real *r) { *p=sphere_center; *r=sphere_radius; }
/* what connects: the real function's record of it */
static struct lights_port_connection { long light_index; long cluster_reference; real_point3d center; real radius; } lights_port_connections[MAXIMUM_LIGHTS_PER_MAP];
static void light_connect_to_clusters(long i)
{
 struct lights_port_connection *c=&lights_port_connections[DATUM_INDEX_TO_ABSOLUTE_INDEX(i)];
 joins++;the_light.cluster_reference=7;SET_FLAG(the_light.flags,_point_light_connected_to_map_bit,TRUE);
 c->light_index=i;c->cluster_reference=7;c->center=sphere_center;c->radius=sphere_radius;
}
static void light_disconnect_from_map(long i)
{
 CHECK(TEST_FLAG(the_light.flags,_point_light_connected_to_map_bit),"only a connected light leaves");
 leaves++;SET_FLAG(the_light.flags,_point_light_connected_to_map_bit,FALSE);
 lights_port_connections[DATUM_INDEX_TO_ABSOLUTE_INDEX(i)].light_index=NONE;
}
#include "under_test.inc"
#define LIGHT 0x12340003
#define MOVE(dx) {pose.x+=(dx);light_move_with_object(LIGHT);}
int main(void)
{
 unsigned long writes;
 SET_FLAG(the_light.flags,_point_light_connects_to_map_bit,TRUE);
 /* connected, as a new light is */
 SET_FLAG(the_light.flags,_point_light_connected_to_map_bit,TRUE);
 light_move_with_object(LIGHT);
 CHECK(joins==1&&leaves==1,"a light with no record joins again");
 joins=leaves=0;
 writes=lights_port.radius_writes;
 light_move_with_object(LIGHT);
 CHECK(!joins&&!leaves,"a light that didn't move stays in its clusters");
 CHECK(lights_port.radius_writes==writes,"and what was kept for its clusters still holds");
 MOVE(.1f);
 CHECK(!joins&&!leaves&&the_light.position.x==pose.x,"a short move takes the new position, in the same clusters");
 CHECK(lights_port.radius_writes==writes+1,"and what was kept for its clusters is dropped");
 /* creeping: from where it joined, not from the last tick */
 MOVE(.1f);
 CHECK(!joins,"still near where it joined");
 MOVE(.1f);
 CHECK(joins==1&&leaves==1,"small moves add up");
 joins=leaves=0;
 /* its radius: the sphere is another */
 sphere_radius=2;
 light_move_with_object(LIGHT);
 CHECK(joins==1&&leaves==1,"a new radius joins again");
 joins=leaves=0;
 sphere_radius=1;
 light_move_with_object(LIGHT);
 CHECK(!joins&&!leaves,"a smaller sphere stays in the clusters of the larger");
 joins=leaves=0;
 /* a small light goes less far */
 sphere_radius=.2f;
 light_move_with_object(LIGHT);
 joins=leaves=0;
 MOVE(.04f);
 CHECK(!joins,"a small light may move a little");
 MOVE(.04f);
 CHECK(joins==1,"but only a part of its radius");
 joins=leaves=0;
 sphere_radius=1;
 light_move_with_object(LIGHT);
 joins=leaves=0;
 /* the slot's next light, another's clusters, or disconnected */
 light_move_with_object(0x56780003);
 CHECK(joins==1&&leaves==1,"another light at that slot joins for itself");
 joins=leaves=0;
 light_move_with_object(LIGHT);
 CHECK(joins==1,"and the first then has no record");
 joins=leaves=0;
 the_light.cluster_reference=99;
 light_move_with_object(LIGHT);
 CHECK(joins==1,"a restored game's lists aren't what was recorded");
 joins=leaves=0;
 lights_port_connections[3].light_index=LIGHT;
 SET_FLAG(the_light.flags,_point_light_connected_to_map_bit,FALSE);
 SET_FLAG(the_light.flags,_point_light_connected_to_map_bit,TRUE);
 light_disconnect_from_map(LIGHT);
 SET_FLAG(the_light.flags,_point_light_connected_to_map_bit,TRUE);
 light_move_with_object(LIGHT);
 CHECK(joins==1,"one that left has no record");
 /* a light with no clusters still moves */
 joins=leaves=poses=0;
 SET_FLAG(the_light.flags,_point_light_connects_to_map_bit,FALSE);
 MOVE(5);
 CHECK(poses==1&&the_light.position.x==pose.x&&!joins&&!leaves,"one that doesn't connect only moves");
 return 0;
}
