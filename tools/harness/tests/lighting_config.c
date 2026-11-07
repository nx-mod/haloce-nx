#include "harness.h"
#include <stdint.h>
typedef struct { long long QuadPart; } LARGE_INTEGER;
typedef long long LONGLONG;
static long long counter=2000000;
static void QueryPerformanceFrequency(LARGE_INTEGER *v) { v->QuadPart=2000000; }
static void QueryPerformanceCounter(LARGE_INTEGER *v) { v->QuadPart=counter; }
struct lighting_budget { int fields; long rays, time_us; };
static unsigned long generation;
static int enabled, reads, change_at;
unsigned long config_changes(void) { return generation; }
int config_boolean(const char *name)
{ reads++;if(reads==change_at){enabled=1;generation++;}return enabled; }
static int string_compares;
static int counted_strcmp(const char *a,const char *b) { string_compares++;return (strcmp)(a,b); }
#define strcmp counted_strcmp
#include "under_test.inc"
int main(int argc,char **argv)
{
 unsigned int i;int before;
 if(argc>1&&!strcmp(argv[1],"budget"))
 {
  printf("BUDGET %d %ld %ld\n",lighting_budget()->fields,lighting_budget()->rays,lighting_budget()->time_us);
  CHECK(lighting_budget_clock_us()==1000000,"the clock is in microseconds, whatever the counter's rate");
  return 0;
 }
 if(argc>1&&!strcmp(argv[1],"moving"))
 {
  printf("MOVING %d\n",lighting_moving_lights_384());
  CHECK(lighting_forced_off("debug.moving_lights_384")==lighting_moving_lights_384(),"the overlay reads the grid as it is");
  CHECK(lighting_force_off("debug.moving_lights_384",0)&&!lighting_moving_lights_384(),"the overlay can choose the full grid");
  CHECK(lighting_force_off("debug.moving_lights_384",1)&&lighting_moving_lights_384(),"and the 384");
  return 0;
 }
 if(argc>1&&!strcmp(argv[1],"cap"))
 {
  CHECK(!lighting_promoted_cap(),"every platform starts with no cap");
  CHECK(lighting_force_off("debug.promoted_cap_6",1)&&lighting_promoted_cap()==6,"a row sets its cap");
  CHECK(lighting_forced_off("debug.promoted_cap_6")&&!lighting_forced_off("debug.promoted_cap_8"),"and only it reads as on");
  CHECK(lighting_force_off("debug.promoted_cap_4",1)&&lighting_promoted_cap()==4&&!lighting_forced_off("debug.promoted_cap_6"),"another replaces it");
  CHECK(lighting_force_off("debug.promoted_cap_6",0)&&lighting_promoted_cap()==4,"turning off one not chosen changes nothing");
  CHECK(lighting_force_off("debug.promoted_cap_4",0)&&!lighting_promoted_cap(),"turning off the chosen one lifts the cap");
  return 0;
 }
 if(argc>1&&!strcmp(argv[1],"lookups"))
 {
  static const char muzzle[]="display.muzzle_lighting",plasma[]="display.plasma_lighting";
  char other[64];
  int compares;
  enabled=1;generation++;
  CHECK(lighting_config_boolean(muzzle)&&lighting_config_boolean(plasma),"a first lookup finds its setting");
  compares=string_compares;
  for(i=0;i<100;i++) CHECK(lighting_config_boolean(muzzle)&&lighting_config_boolean(plasma),"a later lookup finds it too");
  CHECK(string_compares==compares,"a name seen before needs no string comparison");
  /* each source file has its own copy of a literal */
  {
   static char copies[4][64];
   for(i=0;i<4;i++)strcpy(copies[i],i&1?plasma:muzzle);
   for(i=0;i<4;i++)CHECK(lighting_config_boolean(copies[i]),"a copy of a name finds its setting");
   compares=string_compares;
   for(i=0;i<400;i++)CHECK(lighting_config_boolean(copies[i%4]),"and finds it again");
   CHECK(string_compares==compares,"no copy compares strings a second time");
  }
  /* more addresses than there is room for */
  {
   static char many[1000][64];
   for(i=0;i<1000;i++)
   {
    strcpy(many[i],i&1?plasma:muzzle);
    CHECK(lighting_config_boolean(many[i]),"past the table's room, a name still finds its setting");
   }
   lighting_force_off(muzzle,1);
   for(i=0;i<1000;i++)CHECK(lighting_config_boolean(many[i])==(int)(i&1),"each address has its own setting");
   lighting_force_off(muzzle,0);
   CHECK(lighting_config_boolean(muzzle)&&lighting_config_boolean(plasma),"and those seen first still do");
  }
  strcpy(other,muzzle);
  CHECK(lighting_config_boolean(other),"the same text at another address still finds it");
  CHECK(lighting_force_off(muzzle,1)&&!lighting_config_boolean(muzzle)&&!lighting_config_boolean(other),"forced off reads off by either");
  CHECK(lighting_config_boolean(plasma),"and it is the right setting");
  lighting_force_off(muzzle,0);
  enabled=0;generation++;
  CHECK(!lighting_config_boolean(muzzle)&&!lighting_config_boolean(plasma),"a settings change reaches a name seen before");
  return 0;
 }
 if(argc>1&&!strcmp(argv[1],"limited"))
 {
  enabled=1;generation++;
  CHECK(!lighting_config_boolean("display.muzzle_lighting"),"a key the platform leaves out reads as off");
  CHECK(lighting_config_boolean("display.plasma_lighting"),"and the others as they are");
  return 0;
 }
 if(argc>1&&!strcmp(argv[1],"forced"))
 {
  enabled=1;generation++;
  CHECK(lighting_config_boolean("display.muzzle_lighting"),"a setting on in the config reads on");
  CHECK(lighting_force_off("display.muzzle_lighting",1),"a lighting setting can be forced off");
  CHECK(lighting_forced_off("display.muzzle_lighting"),"and says so");
  CHECK(!lighting_config_boolean("display.muzzle_lighting"),"forced off reads off with the config on");
  CHECK(lighting_config_boolean("display.plasma_lighting"),"the others are as the config has them");
  generation++;
  CHECK(!lighting_config_boolean("display.muzzle_lighting"),"it outlasts a settings change");
  CHECK(lighting_force_off("display.muzzle_lighting",0),"it can be lifted");
  CHECK(lighting_config_boolean("display.muzzle_lighting"),"lifted, the config decides again, with no settings change");
  enabled=0;generation++;
  CHECK(lighting_force_off("display.muzzle_lighting",0)&&!lighting_config_boolean("display.muzzle_lighting"),"lifting never turns on what the config has off");
  CHECK(!lighting_force_off("unrelated.setting",1),"only lighting settings can be forced");
  CHECK(!lighting_forced_off("unrelated.setting"),"and an unrelated one is not forced");
  return 0;
 }
 CHECK(!lighting_config_boolean("display.plasma_lighting"),"initial zero generation loads off defaults");
 before=reads;
 for(i=0;i<10000;i++) CHECK(!lighting_config_boolean(preferences[i%13].name),"cached value remains off");
 CHECK(reads==before,"ordinary reads do not reenter general configuration");
 enabled=1;generation++;
 for(i=0;i<13;i++) CHECK(lighting_config_boolean(preferences[i].name),"apply refreshes every related preference");
 CHECK(reads==before+13,"one refresh for a settings generation");
 enabled=0;generation++;
 CHECK(!lighting_config_boolean("display.vehicle_lighting"),"defaults/disable is visible immediately");
 before=reads;CHECK(!lighting_config_boolean("unrelated.setting")&&reads==before+1,"unknown names retain ordinary lookup behavior");
 generation++;change_at=reads+2;
 CHECK(!lighting_config_boolean("display.plasma_lighting"),"first value was read before intervening change");
 CHECK(lighting_config_boolean("display.plasma_lighting"),"intervening change cannot leave a stale value cached");
 return 0;
}
