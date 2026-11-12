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
/* display.occlusion_lights, and how often it is read */
static long occlusion_lights=6;
static int integer_reads;
long config_integer(const char *name) { integer_reads++;return occlusion_lights; }
int config_boolean(const char *name)
{ reads++;if(reads==change_at){enabled=1;generation++;}return enabled; }
/* display.shield_glow_intensity, and how often it is read */
static const char *intensity="default";
static int string_reads;
const char *config_string(const char *name) { string_reads++;return intensity; }
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
 if(argc>1&&!strcmp(argv[1],"lights"))
 {
  int before_reads;
  CHECK(lighting_budget()->fields==6,"six lights by default");
  occlusion_lights=8;generation++;
  CHECK(lighting_budget()->fields==8,"eight when the setting says so");
  occlusion_lights=4;generation++;
  CHECK(lighting_budget()->fields==4,"and four");
  occlusion_lights=5;generation++;
  CHECK(lighting_budget()->fields==6,"anything else is six");
  CHECK(lighting_budget()->time_us==4000&&!lighting_budget()->rays,"the time budget stays");
  before_reads=integer_reads;
  for(i=0;i<1000;i++) CHECK(lighting_budget()->fields==6,"read again");
  CHECK(integer_reads==before_reads,"the setting is read only when the settings change");
  return 0;
 }
 if(argc>1&&!strcmp(argv[1],"glow"))
 {
  static const struct { const char *value; float scale; } levels[]={
   {"default",1.0f},{"1.5x",1.5f},{"2x",2.0f},{"light_show",3.0f},{"bogus",1.0f}};
  float r,g,b;int before_reads;
  for(i=0;i<sizeof(levels)/sizeof(levels[0]);i++)
  {
   intensity=levels[i].value;generation++;
   CHECK(lighting_glow_scale()==levels[i].scale,"each Glow Intensity has its scale");
  }
  intensity=NULL;generation++;
  CHECK(lighting_glow_scale()==1.0f,"no setting is as made");
  intensity="2x";generation++;
  CHECK(lighting_glow_scale()==2.0f,"read again when the settings change");
  before_reads=string_reads;
  for(i=0;i<1000;i++) CHECK(lighting_glow_scale()==2.0f,"read again");
  CHECK(string_reads==before_reads,"the setting is read only when the settings change");
  r=.2f;g=.4f;b=.1f;lighting_glow_brighten(2.0f,&r,&g,&b);
  CHECK(r==.4f&&g==.8f&&b==.2f,"a dim light is brighter by the scale");
  r=.5f;g=1.0f;b=.25f;lighting_glow_brighten(3.0f,&r,&g,&b);
  CHECK(r==.5f&&g==1.0f&&b==.25f,"a light at full stays at full, its hue kept");
  r=.4f;g=.2f;b=.1f;lighting_glow_brighten(3.0f,&r,&g,&b);
  CHECK(r==1.0f&&g>.49f&&g<.51f&&b>.24f&&b<.26f,"past full, its brightest channel is full, its hue kept");
  r=.3f;g=.6f;b=.9f;lighting_glow_brighten(1.0f,&r,&g,&b);
  CHECK(r==.3f&&g==.6f&&b==.9f,"1x changes nothing");
  /* LIGHTING at STOCK: every added light off, whatever its toggle; at 1x and up, the toggles' */
  enabled=1;intensity="stock";generation++;
  CHECK(lighting_stock()&&lighting_glow_scale()==1.0f,"STOCK is 1x, and stock");
  CHECK(!lighting_config_boolean("display.plasma_lighting")&&!lighting_config_boolean("display.shield_glow"),"STOCK turns every light off");
  intensity=NULL;generation++;
  CHECK(lighting_stock()&&!lighting_config_boolean("display.vehicle_lighting"),"no setting is STOCK");
  intensity="default";generation++;
  CHECK(!lighting_stock()&&lighting_config_boolean("display.plasma_lighting"),"1x lets the toggles say");
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
