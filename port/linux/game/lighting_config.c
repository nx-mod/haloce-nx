/* Avoid repeated locked, full-table configuration searches per light/draw.
   Cache values only until the settings generation changes; names and defaults
   remain owned by port_config.c. No map, object or host-policy state is cached. */
#include "lighting_config.h"
#include <string.h>
#include <xtl.h>

int config_boolean(const char *name);
unsigned long config_changes(void);

static struct
{
	const char *name;
	int value;
	/* the overlay's Force Lighting Off: never saved, so config.toml stays the
	   source of truth and a restart clears it */
	int forced_off;
} preferences[] = {
	{"display.plasma_lighting", 0},
	{"display.muzzle_lighting", 0},
	{"display.plasma_occlusion", 0},
	{"display.ammo_counter_lighting", 0},
	{"display.needler_lighting", 0},
	{"display.jackal_shield_lighting", 0},
	{"display.covenant_shield_lighting", 0},
	{"display.fuel_rod_lighting", 0},
	{"display.weapon_glow", 0},
	{"display.plasma_prong_lighting", 0},
	{"display.vehicle_lighting", 0},
	{"display.world_lighting", 0},
	{"display.shield_glow", 0}
};

/* port: to limit a whole platform, name a toggle's key here (it then reads as
   off, whatever config.toml says), and tag its row in tools/port_settings.py
   so that the menu does not offer it. Nothing is limited yet: those decisions
   wait for measurements (docs/dynamic_lighting_port.md). */
static const char *const platform_off[] = {
#if defined(HALO_SWITCH)
#elif defined(HALO_PS5)
#elif defined(HALO_ANDROID)
#endif
	NULL
};

static int platform_allows(const char *name)
{
	unsigned int i;
	for (i = 0; platform_off[i]; i++)
		if (!strcmp(name, platform_off[i])) return 0;
	return 1;
}

/* port: the Switch is one known machine, so it counts rays. Phones differ too
   much for one count, so Android counts time, which also follows a phone that
   slows down as it heats up (docs/dynamic_lighting_performance_plan.md). */
const struct lighting_budget *lighting_budget(void)
{
	static const struct lighting_budget budget =
#if defined(HALO_SWITCH)
		{3, 1800, 0};
#elif defined(HALO_PS5)
		{8, 0, 0};
#elif defined(HALO_ANDROID)
		{8, 0, 4000};
#else
		{8, 0, 0};
#endif
	return &budget;
}

unsigned long lighting_budget_clock_us(void)
{
	static LONGLONG frequency;
	LARGE_INTEGER count;
	if (!frequency)
	{
		LARGE_INTEGER rate;
		QueryPerformanceFrequency(&rate);
		frequency = rate.QuadPart;
	}
	QueryPerformanceCounter(&count);
	return (unsigned long)(count.QuadPart * 1000000 / frequency);
}

int lighting_config_boolean(const char *name)
{
	static unsigned long changes;
	static int ready;
	unsigned int i;
	if (!ready || changes != config_changes())
	{
		/* A concurrent settings change must cause another refresh, rather
		   than label partially read preferences with the newer generation. */
		changes = config_changes();
		for (i = 0; i < sizeof(preferences) / sizeof(preferences[0]); i++)
			preferences[i].value = platform_allows(preferences[i].name) && config_boolean(preferences[i].name);
		ready = 1;
	}
	for (i = 0; i < sizeof(preferences) / sizeof(preferences[0]); i++)
		if (!strcmp(name, preferences[i].name)) return preferences[i].value && !preferences[i].forced_off;
	return config_boolean(name);
}

/* the overlay's comparison of grids for a moving light: for this run, not saved */
static int moving_lights_384;
int lighting_moving_lights_384(void)
{
	return moving_lights_384;
}

int lighting_force_off(const char *name, int forced)
{
	unsigned int i;
	if (!strcmp(name, "debug.moving_lights_384"))
	{
		moving_lights_384 = forced != 0;
		return 1;
	}
	for (i = 0; i < sizeof(preferences) / sizeof(preferences[0]); i++)
		if (!strcmp(name, preferences[i].name))
		{
			preferences[i].forced_off = forced != 0;
			return 1;
		}
	return 0;
}

int lighting_forced_off(const char *name)
{
	unsigned int i;
	if (!strcmp(name, "debug.moving_lights_384"))
		return moving_lights_384;
	for (i = 0; i < sizeof(preferences) / sizeof(preferences[0]); i++)
		if (!strcmp(name, preferences[i].name)) return preferences[i].forced_off;
	return 0;
}
