/* Avoid repeated locked, full-table configuration searches per light/draw.
   Cache values only until the settings generation changes; names and defaults
   remain owned by port_config.c. No map, object or host-policy state is cached. */
#include "lighting_config.h"
#include <stdint.h>
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
   slows down as it heats up (docs/dynamic_lighting_performance_plan.md); and
   how many lights a frame is the player's (display.occlusion_lights: 8, 6 or 4,
   Video Setup's Dynamic Lighting). On a Nokia 8 at 720p, eight came to 37 frames
   a second in a 48-player plasma fight, 52 with no dynamic lighting. */
const struct lighting_budget *lighting_budget(void)
{
#if defined(HALO_ANDROID) && !defined(HALO_SWITCH) && !defined(HALO_PS5)
	extern long config_integer(const char *name);
	static struct lighting_budget budget = {6, 0, 4000};
	static unsigned long changes;
	static int ready;

	if (!ready || changes != config_changes())
	{
		long lights;

		changes = config_changes();
		lights = config_integer("display.occlusion_lights");
		budget.fields = lights == 8 || lights == 4 ? (int)lights : 6;
		ready = 1;
	}
	return &budget;
#else
	static const struct lighting_budget budget =
#if defined(HALO_SWITCH)
		{3, 1800, 0};
#else
		{8, 0, 0};
#endif
	return &budget;
#endif
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

/* This runs per light and per draw, and with lighting off a check walks every
   setting it depends on. Callers name settings with string literals, so a name's
   address, once looked up, says which it is with no string comparison. Each
   source file has its own copy of a literal (the guest's link doesn't merge them),
   so there are many addresses, not one a setting. An entry is only ever added:
   past the table's room, a name is compared as it always was. */
enum { PREFERENCE_ADDRESSES = 256 };
static struct
{
	const char *name;
	int preference;
} preference_addresses[PREFERENCE_ADDRESSES];
static int preference_by_name(const char *name)
{
	unsigned int i;
	for (i = 0; i < sizeof(preferences) / sizeof(preferences[0]); i++)
		if (!strcmp(name, preferences[i].name)) return (int)i;
	return -1;
}
static int preference_index(const char *name)
{
	unsigned int slot = ((unsigned int)(uintptr_t)name * 2654435761u) >> 24, probes;
	for (probes = 0; probes < 8; probes++, slot = (slot + 1) % PREFERENCE_ADDRESSES)
	{
		if (preference_addresses[slot].name == name)
			return preference_addresses[slot].preference;
		if (!preference_addresses[slot].name)
		{
			preference_addresses[slot].preference = preference_by_name(name);
			preference_addresses[slot].name = name;
			return preference_addresses[slot].preference;
		}
	}
	return preference_by_name(name);
}

int lighting_config_boolean(const char *name)
{
	static unsigned long changes;
	static int ready;
	unsigned int i;
	int preference;
	if (!ready || changes != config_changes())
	{
		/* A concurrent settings change must cause another refresh, rather
		   than label partially read preferences with the newer generation. */
		changes = config_changes();
		for (i = 0; i < sizeof(preferences) / sizeof(preferences[0]); i++)
			preferences[i].value = platform_allows(preferences[i].name) && config_boolean(preferences[i].name);
		ready = 1;
	}
	preference = preference_index(name);
	if (preference >= 0) return preferences[preference].value && !preferences[preference].forced_off;
	return config_boolean(name);
}

/* A moving light's grid. The Switch traces 384 rays: at a Pillar of Autumn firefight,
   59.6 fps to 1,536's 57.1, with room for more lights to have occlusion
   (docs/dynamic_lighting_performance_plan.md). The overlay can change it for this
   run, to compare; it is not saved. */
#if defined(HALO_SWITCH)
static int moving_lights_384 = 1;
#else
static int moving_lights_384;
#endif
int lighting_moving_lights_384(void)
{
	return moving_lights_384;
}

/* port: the overlay's Fast Occlusion Math: light occlusion's field rebuilt with
   reciprocal estimates for its divides and square roots, not the exact ones.
   Off unless the overlay turns it on, for this run only, never saved: to see
   how it looks before it is anything more (plasma_occlusion.c). */
static int fast_occlusion_math;
int lighting_fast_occlusion_math(void)
{
	return fast_occlusion_math;
}

/* the overlay's comparison of caps on promoted lights: for this run, not saved */
static int promoted_cap;
int lighting_promoted_cap(void)
{
	return promoted_cap;
}
static const struct
{
	const char *name;
	int cap;
} promoted_caps[] = {
	{"debug.promoted_cap_8", 8},
	{"debug.promoted_cap_6", 6},
	{"debug.promoted_cap_4", 4}
};

int lighting_force_off(const char *name, int forced)
{
	unsigned int i;
	if (!strcmp(name, "debug.moving_lights_384"))
	{
		moving_lights_384 = forced != 0;
		return 1;
	}
	if (!strcmp(name, "debug.fast_occlusion_math"))
	{
		fast_occlusion_math = forced != 0;
		return 1;
	}
	for (i = 0; i < sizeof(promoted_caps) / sizeof(promoted_caps[0]); i++)
		if (!strcmp(name, promoted_caps[i].name))
		{
			/* (one at a time: turning one off leaves another alone) */
			if (forced)
				promoted_cap = promoted_caps[i].cap;
			else if (promoted_cap == promoted_caps[i].cap)
				promoted_cap = 0;
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
	/* port: a preference by its name's address (preference_index), as the
	   settings that read this every frame are: shield_glow_enabled, through
	   every lighting check, compared the name with all of the rows below */
	int preference = preference_index(name);
	if (preference >= 0)
		return preferences[preference].forced_off;
	if (!strcmp(name, "debug.moving_lights_384"))
		return moving_lights_384;
	if (!strcmp(name, "debug.fast_occlusion_math"))
		return fast_occlusion_math;
	for (i = 0; i < sizeof(promoted_caps) / sizeof(promoted_caps[0]); i++)
		if (!strcmp(name, promoted_caps[i].name))
			return promoted_cap == promoted_caps[i].cap;
	for (i = 0; i < sizeof(preferences) / sizeof(preferences[0]); i++)
		if (!strcmp(name, preferences[i].name)) return preferences[i].forced_off;
	return 0;
}
