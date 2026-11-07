/* Game/render-thread lighting preferences. Host policy is checked separately. */
#ifndef LIGHTING_CONFIG_H
#define LIGHTING_CONFIG_H
int lighting_config_boolean(const char *name);
/* debugging: reads a setting as off for this run, without changing it; 0 if the
   setting is not one of these */
int lighting_force_off(const char *name, int forced);
int lighting_forced_off(const char *name);
int lighting_moving_lights_384(void);
/* how many promoted lights keep their full reach; 0: all */
int lighting_promoted_cap(void);
/* What occlusion may spend each view: active fields, and rays or microseconds of
   work (0: no limit on that) */
struct lighting_budget
{
	int fields;
	long rays, time_us;
};
const struct lighting_budget *lighting_budget(void);
unsigned long lighting_budget_clock_us(void);
#endif
