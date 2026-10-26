/*
HOST_VK_PROFILE.C

The GPU's time by rendering, under the Vulkan renderer, while a profile is recorded (debug.profile_run: host_main.c
names the folder, host_vk_profile_folder). The GPU writes a timestamp (vkCmdWriteTimestamp) where each frame's
command buffer begins, just before each rendering begins and just after it ends (host_vk_profile_rendering_begin and
_end: the game's renderings, host_vk_render.c, and the anti-aliasing's, host_vk_post.c), and where the command buffer
ends. The time from one to the next is that rendering's - its target's load, its draws and its store - or, between
renderings, the copies, blits, uploads and barriers there. Each is named for the part of the frame the game was
drawing when it began (VK_COMMAND_MARKER: "sky", "objects", "hud"... from halo_render_marker, as the Switch's renderer
has them, port/switch/host/host_dk.c's debug.gpu_profile) and for what it draws into (its size, colour and depth).

Timestamps only between renderings: a phone's GPU draws a rendering in tiles, after sorting its triangles into them,
and Qualcomm's driver gives every timestamp written inside one the same time (a first profile on an Adreno 840 put
91% of the frame in its first part and 0.002 ms in each of the others). A rendering's time is its own; within one,
the parts are not told apart.

A frame slot's timestamps are read once its fence has passed (host_vk_profile_frame_begin, as the slot is recorded
again). Every PROFILE_FRAMES frames the averages go into gpu.txt in the profile's folder: the GPU's frame, the time from
one present to the next (the CPU's and GPU's together, as the player sees it), the renderings a frame, then the time
by target and by part and target, the longest first. A benchmark's map (port/linux/game/benchmark.c: the markers
"benchmark <map>" and "benchmark end") is one window of its own, named for it, whatever its frames. */

#include "host.h"
#include "host_vk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define B host_vkb

#define PROFILE_MARKS 512
#define PROFILE_LABELS 512
#define PROFILE_NAME_SIZE 48
#define PROFILE_FRAMES 300
/* the part before the game names one */
#define PROFILE_FIRST_PART "(before the game's first part)"
#define BETWEEN "between renderings"

char host_vk_profile_folder[512];
int host_vk_profile_split;

/* an interval's name: the game's part (parts[]) and what it is (whats[]: a rendering's target, or BETWEEN) */
struct label
{
	uint16_t part, what;
};

static struct
{
	int on;
	VkQueryPool pool;
	double ns_per_tick;
	uint64_t valid_mask;
	/* each slot's marks: the label of the interval each begins, and how many */
	uint16_t label[HOST_VK_FRAMES][PROFILE_MARKS];
	uint32_t count[HOST_VK_FRAMES];
	uint32_t renderings[HOST_VK_FRAMES];
	/* the names */
	char parts[PROFILE_LABELS][PROFILE_NAME_SIZE];
	char whats[PROFILE_LABELS][PROFILE_NAME_SIZE];
	uint32_t part_count, what_count;
	struct label labels[PROFILE_LABELS];
	uint32_t label_count;
	/* the part being drawn */
	uint16_t part;
	/* the window's sums: each label's, the GPU's frames, the presents' */
	uint64_t ns[PROFILE_LABELS];
	uint64_t gpu_total_ns, gpu_longest_ns, rendering_total;
	uint32_t gpu_frames, gpu_slow;
	uint64_t present_last_ns, present_total_ns, present_longest_ns;
	uint32_t presents, presents_slow;
	uint32_t window, lost;
	/* the benchmark's map being measured (its window ends at "benchmark end"), or empty */
	char title[VK_MARKER_NAME_SIZE];
	FILE *file;
} profile;

static uint64_t now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

/* a name's index in a table, added if it is new; the last if the table is full */
static uint16_t name_index(char (*table)[PROFILE_NAME_SIZE], uint32_t *count, const char *name)
{
	uint32_t index;

	for (index = 0; index < *count && strcmp(table[index], name); index++)
		;
	if (index == *count)
	{
		if (*count == PROFILE_LABELS)
			return (uint16_t)(*count - 1);
		snprintf(table[index], PROFILE_NAME_SIZE, "%s", name);
		(*count)++;
	}
	return (uint16_t)index;
}

/* the label of the part being drawn and what */
static uint16_t label_index(const char *what)
{
	uint16_t what_index = name_index(profile.whats, &profile.what_count, what);
	uint32_t index;

	for (index = 0; index < profile.label_count; index++)
	{
		if (profile.labels[index].part == profile.part && profile.labels[index].what == what_index)
			return (uint16_t)index;
	}
	if (profile.label_count == PROFILE_LABELS)
		return (uint16_t)(PROFILE_LABELS - 1);
	profile.labels[profile.label_count].part = profile.part;
	profile.labels[profile.label_count].what = what_index;
	return (uint16_t)profile.label_count++;
}

void host_vk_profile_initialize(void)
{
	VkQueueFamilyProperties families[16];
	uint32_t family_count = 16;
	VkQueryPoolCreateInfo info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	uint32_t bits;
	char path[600];

	if (!host_vk_profile_folder[0] || profile.on)
		return;
	host_vk.vkGetPhysicalDeviceQueueFamilyProperties(host_vk.physical, &family_count, NULL);
	if (family_count > 16)
		family_count = 16;
	host_vk.vkGetPhysicalDeviceQueueFamilyProperties(host_vk.physical, &family_count, families);
	bits = host_vk.queue_family < family_count ? families[host_vk.queue_family].timestampValidBits : 0;
	if (!bits || !host_vk.properties.limits.timestampComputeAndGraphics)
	{
		host_logf(HOST_LOG_WARN, "vk: the GPU's queue keeps no timestamps; gpu.txt has no GPU times");
		return;
	}
	info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	info.queryCount = HOST_VK_FRAMES * PROFILE_MARKS;
	if (!HOST_VK_CHECK(vkCreateQueryPool(B.device, &info, NULL, &profile.pool)))
		return;
	snprintf(path, sizeof(path), "%s/gpu.txt", host_vk_profile_folder);
	profile.file = fopen(path, "w");
	if (!profile.file)
	{
		host_logf(HOST_LOG_WARN, "vk: cannot write %s; no GPU profile", path);
		vkDestroyQueryPool(B.device, profile.pool, NULL);
		profile.pool = VK_NULL_HANDLE;
		return;
	}
	profile.ns_per_tick = (double)host_vk.properties.limits.timestampPeriod;
	profile.valid_mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
	profile.part = name_index(profile.parts, &profile.part_count, PROFILE_FIRST_PART);
	profile.on = 1;
	fprintf(profile.file, "GPU %s, %u timestamp bits, %.3f ns a tick; every %u frames, the GPU's time by rendering "
		"(host_vk_profile.c): each named for the part of the frame it began in and what it draws into; \""
		BETWEEN "\" is the copies, blits, uploads and barriers between them%s\n\n", host_vk.properties.deviceName,
		(unsigned)bits, profile.ns_per_tick, (unsigned)PROFILE_FRAMES, host_vk_profile_split ?
		". debug.gpu_split_parts: a rendering ends at each part, so each part's time is its own; the stores and loads "
		"that adds make every frame longer than it is" : "");
	fflush(profile.file);
	host_logf(HOST_LOG_INFO, "vk: the GPU's time by rendering goes to %s", path);
}

static void mark(VkCommandBuffer command, unsigned slot, uint16_t label)
{
	uint32_t index = profile.count[slot];

	if (index >= PROFILE_MARKS)
		return;
	vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, profile.pool, slot * PROFILE_MARKS + index);
	profile.label[slot][index] = label;
	profile.count[slot] = index + 1;
}

/* the order of a window's sums, the longest first (ns_sort: the sums sorted) */
static const uint64_t *ns_sort;

static int longest_first(const void *first, const void *second)
{
	uint64_t a = ns_sort[*(const uint16_t *)first], b = ns_sort[*(const uint16_t *)second];

	return a < b ? 1 : a > b ? -1 : 0;
}

/* the window's averages into gpu.txt */
static void window_write(void)
{
	uint16_t order[PROFILE_LABELS];
	uint64_t by_what[PROFILE_LABELS];
	uint32_t index, listed;
	double frames = profile.gpu_frames ? (double)profile.gpu_frames : 1.0;

	profile.window++;
	if (profile.title[0])
		fprintf(profile.file, "%s, ", profile.title);
	fprintf(profile.file, "window %u: %u frames timed; the GPU %.2f ms a frame on average, %.2f the longest, %u over "
		"33 ms; %.1f renderings a frame", (unsigned)profile.window, (unsigned)profile.gpu_frames,
		(double)profile.gpu_total_ns / frames / 1e6, (double)profile.gpu_longest_ns / 1e6, (unsigned)profile.gpu_slow,
		(double)profile.rendering_total / frames);
	if (profile.presents)
	{
		fprintf(profile.file, "; presents %.2f ms apart on average (%.1f a second), %.2f the longest, %u over 33 ms",
			(double)profile.present_total_ns / profile.presents / 1e6,
			profile.present_total_ns ? 1e9 * profile.presents / (double)profile.present_total_ns : 0.0,
			(double)profile.present_longest_ns / 1e6, (unsigned)profile.presents_slow);
	}
	if (profile.lost)
		fprintf(profile.file, "; %u marks not read", (unsigned)profile.lost);
	fprintf(profile.file, "\n  by what it draws into:\n");
	memset(by_what, 0, sizeof(by_what));
	for (index = 0; index < profile.label_count; index++)
		by_what[profile.labels[index].what] += profile.ns[index];
	for (index = 0; index < profile.what_count; index++)
		order[index] = (uint16_t)index;
	ns_sort = by_what;
	qsort(order, profile.what_count, sizeof(order[0]), longest_first);
	for (index = 0; index < profile.what_count && by_what[order[index]]; index++)
	{
		fprintf(profile.file, "  %8.3f ms  %5.1f%%  %s\n", (double)by_what[order[index]] / frames / 1e6,
			profile.gpu_total_ns ? 100.0 * (double)by_what[order[index]] / (double)profile.gpu_total_ns : 0.0,
			profile.whats[order[index]]);
	}
	fprintf(profile.file, "  by part of the frame and what it draws into (the 40 longest):\n");
	for (index = 0; index < profile.label_count; index++)
		order[index] = (uint16_t)index;
	ns_sort = profile.ns;
	qsort(order, profile.label_count, sizeof(order[0]), longest_first);
	for (listed = 0; listed < profile.label_count && listed < 40 && profile.ns[order[listed]]; listed++)
	{
		const struct label *label = &profile.labels[order[listed]];

		fprintf(profile.file, "  %8.3f ms  %5.1f%%  %s: %s\n", (double)profile.ns[order[listed]] / frames / 1e6,
			profile.gpu_total_ns ? 100.0 * (double)profile.ns[order[listed]] / (double)profile.gpu_total_ns : 0.0,
			profile.parts[label->part], profile.whats[label->what]);
	}
	fprintf(profile.file, "\n");
	fflush(profile.file);
	memset(profile.ns, 0, sizeof(profile.ns));
	profile.gpu_total_ns = profile.gpu_longest_ns = profile.rendering_total = 0;
	profile.gpu_frames = profile.gpu_slow = 0;
	profile.present_total_ns = profile.present_longest_ns = 0;
	profile.presents = profile.presents_slow = 0;
	profile.lost = 0;
}

void host_vk_profile_frame_begin(unsigned slot, VkCommandBuffer command)
{
	uint32_t count;

	if (!profile.on)
		return;
	count = profile.count[slot];
	/* (the slot's last frame, its fence passed: read, then its queries reset for this one) */
	if (count >= 2)
	{
		uint64_t ticks[PROFILE_MARKS];

		if (vkGetQueryPoolResults(B.device, profile.pool, slot * PROFILE_MARKS, count, sizeof(ticks), ticks,
			sizeof(ticks[0]), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
		{
			uint64_t frame_ns = 0;
			uint32_t index;

			for (index = 0; index + 1 < count; index++)
			{
				uint64_t start = ticks[index] & profile.valid_mask, end = ticks[index + 1] & profile.valid_mask;
				uint64_t ns = (uint64_t)((double)((end - start) & profile.valid_mask) * profile.ns_per_tick);

				profile.ns[profile.label[slot][index]] += ns;
				frame_ns += ns;
			}
			profile.gpu_total_ns += frame_ns;
			if (frame_ns > profile.gpu_longest_ns)
				profile.gpu_longest_ns = frame_ns;
			if (frame_ns > 33333333ull)
				profile.gpu_slow++;
			profile.rendering_total += profile.renderings[slot];
			profile.gpu_frames++;
		}
		else
			profile.lost += count;
		if (profile.gpu_frames >= PROFILE_FRAMES && !profile.title[0])
			window_write();
	}
	profile.count[slot] = 0;
	profile.renderings[slot] = 0;
	vkCmdResetQueryPool(command, profile.pool, slot * PROFILE_MARKS, PROFILE_MARKS);
	mark(command, slot, label_index(BETWEEN));
}

void host_vk_profile_mark(const struct vk_command_marker *command)
{
	char name[VK_MARKER_NAME_SIZE];

	if (!profile.on)
		return;
	memcpy(name, command->name, sizeof(name));
	name[sizeof(name) - 1] = 0;
	/* (a benchmark's map: its frames a window of their own, the frames before it written as they are) */
	if (!strncmp(name, "benchmark ", 10))
	{
		if (profile.gpu_frames)
			window_write();
		if (strcmp(name, "benchmark end"))
			snprintf(profile.title, sizeof(profile.title), "%s", name);
		else
			profile.title[0] = 0;
		return;
	}
	/* (debug.gpu_split_parts: the rendering ended at each part, so that its timestamps tell the parts apart - the stores
and loads it adds make the frame longer: for ranking the parts, not for the frame's time) */
	if (host_vk_profile_split)
		host_vk_rendering_end();
	profile.part = name_index(profile.parts, &profile.part_count, name);
}

void host_vk_profile_rendering_begin(VkCommandBuffer command, const char *what)
{
	unsigned slot = (unsigned)B.frame;

	if (!profile.on)
		return;
	mark(command, slot, label_index(what));
	profile.renderings[slot]++;
}

void host_vk_profile_rendering_end(VkCommandBuffer command)
{
	if (!profile.on)
		return;
	mark(command, (unsigned)B.frame, label_index(BETWEEN));
}

void host_vk_profile_frame_end(unsigned slot, VkCommandBuffer command, int presented)
{
	uint64_t now;

	if (!profile.on)
		return;
	/* (the last mark ends the last interval: its own time is none) */
	mark(command, slot, label_index(BETWEEN));
	if (!presented)
		return;
	now = now_ns();
	if (profile.present_last_ns)
	{
		uint64_t apart = now - profile.present_last_ns;

		profile.present_total_ns += apart;
		if (apart > profile.present_longest_ns)
			profile.present_longest_ns = apart;
		if (apart > 33333333ull)
			profile.presents_slow++;
		profile.presents++;
#ifndef HALO_PS5
		/* (the CPU profile's frames, for its slow frames' samples: host_debug.c) */
		host_debug_profile_frame(apart);
#endif
	}
	profile.present_last_ns = now;
}
