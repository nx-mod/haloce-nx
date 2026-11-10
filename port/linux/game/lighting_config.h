/* Game/render-thread lighting preferences. Host policy is checked separately. */
#ifndef LIGHTING_CONFIG_H
#define LIGHTING_CONFIG_H
int lighting_config_boolean(const char *name);
/* debugging: reads a setting as off for this run, without changing it; 0 if the
   setting is not one of these */
int lighting_force_off(const char *name, int forced);
int lighting_forced_off(const char *name);
int lighting_moving_lights_384(void);
/* Dynamic Lighting's LIGHTING (display.shield_glow_intensity): 1, 1.5, 2 or 3 */
float lighting_glow_scale(void);
/* LIGHTING at STOCK: every added light off, as the original Xbox */
int lighting_stock(void);
void lighting_glow_brighten(float scale, float *red, float *green, float *blue);
/* the overlay's Fast Occlusion Math, for this run: 0 unless turned on there */
int lighting_fast_occlusion_math(void);
int lighting_parallel_light_rays(void);
int lighting_background_light_rays(void);
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
