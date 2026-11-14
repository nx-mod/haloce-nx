/*
CUSTOM_CAMPAIGNS.C

Custom campaigns (custom_campaigns.h, docs/custom_campaigns.md).

A campaign's folder is listed once, when it is mounted: each .map's header
says what it is (its cache version: 5 an Xbox map, 609 a Custom Edition
one) and which map it is (the name at 0x20, which may not be its file's:
Covenant Edition's c10-longest-night-grunt.map is c10). Every map of a
campaign must be of one kind. The maps keep the names their headers give
them, so the levels' scripts (map_name lumoria_b), game_won and the stock
level order find the campaign's maps as they found the stock ones:

- an Xbox campaign's maps are found by cache_files.c and
  cache_files_windows.c (custom_campaign_map_path), its others (a level it
  has not, its ui.map if it has none) in maps as before; the map cache is
  emptied when a campaign is mounted or let go, its files being found
  again by name (cache_files_forget_cached_maps);
- a Custom Edition campaign's maps are Custom Edition maps
  (custom_edition_level_name, custom_campaign_custom_edition_map), read in
  place from the campaign's folder, and its resource maps (bitmaps.map,
  sounds.map, loc.map) the campaign's own if it has them.

The saved game (savegame.bin) is the campaign's own, in the profile's
campaigns\<folder>\ (custom_campaign_save_directory).
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "cache/cache_files.h"
#include "main/main.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "custom_campaigns.h"

#include <string.h>
#include <xtl.h>

/* menu_tags.c's: a campaign ui.map's Halo PC functions given the port's numbers */
void menu_tags_custom_campaign_install(void);

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
char const *config_string(char const *name);

/* ---------- constants */

#define PATH_SIZE 256
#define MAXIMUM_CAMPAIGN_MAPS 64
#define MAP_NAME_SIZE 32
#define FOLDER_NAME_SIZE 64
/* a cache header's fields (cache_files.c's struct cache_file_header) */
#define CACHE_HEADER_BYTES 0x64
#define CACHE_HEADER_VERSION_OFFSET 0x04
#define CACHE_HEADER_NAME_OFFSET 0x20
#define CACHE_HEADER_SCENARIO_TYPE_OFFSET 0x60
#define XBOX_CACHE_VERSION 5
#define CUSTOM_EDITION_CACHE_VERSION 609

/* ---------- globals */

struct custom_campaign_map
{
	char name[MAP_NAME_SIZE];
	char path[PATH_SIZE];
};

static struct
{
	short kind;
	char folder[FOLDER_NAME_SIZE];
	boolean has_ui;
	short map_count;
	struct custom_campaign_map maps[MAXIMUM_CAMPAIGN_MAPS];
} custom_campaign;

/* ---------- private */

static boolean name_ends_with(
	char const *name,
	char const *ending)
{
	size_t length = strlen(name), ending_length = strlen(ending);

	return length > ending_length && !csstrcasecmp(name + length - ending_length, ending);
}

/* the cache header of the file at `path`: its version and its map's name */
static boolean map_header_read(
	char const *path,
	long *version,
	char *name)
{
	byte header[CACHE_HEADER_BYTES];
	unsigned long bytes_read = 0;
	HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	boolean read;

	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	read = ReadFile(file, header, sizeof(header), &bytes_read, NULL) && bytes_read == sizeof(header);
	CloseHandle(file);
	if (!read || memcmp(header, "daeh", 4) != 0)
		return FALSE;
	memcpy(version, header + CACHE_HEADER_VERSION_OFFSET, sizeof(*version));
	memcpy(name, header + CACHE_HEADER_NAME_OFFSET, MAP_NAME_SIZE);
	name[MAP_NAME_SIZE - 1] = 0;

	return name[0] != 0;
}

/* the campaign's map named `map_name` (a level name or a file name, by the
name its header gives), or NULL */
static struct custom_campaign_map const *campaign_map(
	char const *map_name)
{
	char const *name;
	short index;

	if (!custom_campaign.kind || !map_name || !map_name[0])
		return NULL;
	name = tag_name_strip_path(map_name);
	for (index = 0; index < custom_campaign.map_count; index++)
	{
		if (!csstrcasecmp(custom_campaign.maps[index].name, name))
			return &custom_campaign.maps[index];
	}

	return NULL;
}

/* ---------- public */

boolean custom_campaign_mount(
	char const *folder)
{
	char pattern[PATH_SIZE];
	WIN32_FIND_DATAA data;
	HANDLE find;
	short kind = _custom_campaign_kind_none;
	boolean mixed = FALSE;

	custom_campaign_unmount();
	if (!folder || !folder[0] || strlen(folder) >= FOLDER_NAME_SIZE ||
		strchr(folder, '\\') || strchr(folder, '/') ||
		strlen(CUSTOM_CAMPAIGNS_DIRECTORY) + strlen(folder) + 4 >= sizeof(pattern))
	{
		platform_log("custom campaigns: '%s' is no campaign folder's name", folder ? folder : "");
		return FALSE;
	}
	csstrcpy(custom_campaign.folder, folder);
	sprintf(pattern, "%s%s\\*.map", CUSTOM_CAMPAIGNS_DIRECTORY, folder);
	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
	{
		platform_log("custom campaigns: %s%s has no maps", CUSTOM_CAMPAIGNS_DIRECTORY, folder);
		custom_campaign.folder[0] = 0;
		return FALSE;
	}
	do
	{
		struct custom_campaign_map *map;
		char path[PATH_SIZE];
		char name[MAP_NAME_SIZE];
		long version;
		short map_kind;

		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !name_ends_with(data.cFileName, ".map"))
			continue;
		if (strlen(CUSTOM_CAMPAIGNS_DIRECTORY) + strlen(folder) + 1 + strlen(data.cFileName) >= sizeof(path))
			continue;
		sprintf(path, "%s%s\\%s", CUSTOM_CAMPAIGNS_DIRECTORY, folder, data.cFileName);
		/* (Custom Edition's resource maps are found by their file names) */
		if (!csstrcasecmp(data.cFileName, "bitmaps.map") || !csstrcasecmp(data.cFileName, "sounds.map") ||
			!csstrcasecmp(data.cFileName, "loc.map"))
		{
			continue;
		}
		if (!map_header_read(path, &version, name))
		{
			platform_log("custom campaigns: %s is no map", path);
			continue;
		}
		map_kind = version == XBOX_CACHE_VERSION ? _custom_campaign_kind_xbox :
			version == CUSTOM_EDITION_CACHE_VERSION ? _custom_campaign_kind_custom_edition :
			_custom_campaign_kind_none;
		if (map_kind == _custom_campaign_kind_none)
		{
			platform_log("custom campaigns: %s is a map of cache version %ld, which campaigns cannot have",
				path, version);
			continue;
		}
		if (kind != _custom_campaign_kind_none && kind != map_kind)
			mixed = TRUE;
		kind = map_kind;
		if (campaign_map(name) || custom_campaign.map_count >= MAXIMUM_CAMPAIGN_MAPS)
		{
			platform_log("custom campaigns: %s (%s) left out: the campaign has a map of that name already, or too many",
				path, name);
			continue;
		}
		map = &custom_campaign.maps[custom_campaign.map_count++];
		csstrcpy(map->name, name);
		csstrcpy(map->path, path);
		/* (indexed before the kind is set: campaign_map looks at it) */
		custom_campaign.kind = map_kind;
	} while (FindNextFileA(find, &data));
	CloseHandle(find);
	if (mixed || !custom_campaign.map_count)
	{
		platform_log("custom campaigns: %s cannot be played: %s", folder,
			mixed ? "it has both Xbox and Custom Edition maps" : "it has no maps this build can play");
		custom_campaign_unmount();
		return FALSE;
	}
	custom_campaign.kind = kind;
	custom_campaign.has_ui = campaign_map("ui") != NULL;
	if (custom_campaign.has_ui)
		menu_tags_custom_campaign_install();
	cache_files_forget_cached_maps();
	platform_log("custom campaigns: %s mounted: %s, %d maps%s", folder,
		kind == _custom_campaign_kind_xbox ? "Xbox" : "Custom Edition", custom_campaign.map_count,
		custom_campaign.has_ui ? ", its own ui.map" : "");
	{
		short index;

		for (index = 0; index < custom_campaign.map_count; index++)
			platform_log("custom campaigns:   %s: %s", custom_campaign.maps[index].name, custom_campaign.maps[index].path);
	}

	return TRUE;
}

void custom_campaign_unmount(
	void)
{
	boolean was_mounted = custom_campaign.kind != _custom_campaign_kind_none;

	if (was_mounted)
		platform_log("custom campaigns: %s let go", custom_campaign.folder);
	csmemset(&custom_campaign, 0, sizeof(custom_campaign));
	if (was_mounted)
		cache_files_forget_cached_maps();
}

void custom_campaign_back_to_halo(
	void)
{
	custom_campaign_unmount();
	/* (the menus' map loaded again, the stock one: main_menu_load) */
	main_menu_unload();
	main_goto_main_menu();
}

void custom_campaign_startup(
	void)
{
	static boolean done;
	char const *folder;

	if (done)
		return;
	done = TRUE;
	folder = config_string("debug.custom_campaign");
	if (folder && folder[0])
		custom_campaign_mount(folder);
}

short custom_campaign_kind(
	void)
{
	return custom_campaign.kind;
}

char const *custom_campaign_folder(
	void)
{
	return custom_campaign.folder;
}

boolean custom_campaign_has_ui(
	void)
{
	return custom_campaign.kind != _custom_campaign_kind_none && custom_campaign.has_ui;
}

boolean custom_campaign_map_path(
	char const *map_name,
	boolean custom_edition,
	char *path)
{
	struct custom_campaign_map const *map = campaign_map(map_name);

	if (!map || (custom_campaign.kind == _custom_campaign_kind_custom_edition) != (custom_edition != FALSE))
		return FALSE;
	csstrcpy(path, map->path);

	return TRUE;
}

boolean custom_campaign_custom_edition_map(
	char const *map_name)
{
	return custom_campaign.kind == _custom_campaign_kind_custom_edition && campaign_map(map_name) != NULL;
}

boolean custom_campaign_has_map(
	char const *map_name)
{
	return campaign_map(map_name) != NULL;
}

boolean custom_campaign_script_may_call(
	char const *map_name,
	char const *function_name)
{
	static char const *const campaign_functions[] = { "map_name", "game_difficulty_set", "map_reset" };
	short index;

	if (!function_name || !custom_campaign_has_map(map_name))
		return FALSE;
	for (index = 0; index < (short)NUMBEROF(campaign_functions); index++)
	{
		if (!csstrcasecmp(function_name, campaign_functions[index]))
			return TRUE;
	}

	return FALSE;
}

boolean custom_campaign_file_path(
	char const *name,
	char *path)
{
	if (!custom_campaign.kind ||
		strlen(CUSTOM_CAMPAIGNS_DIRECTORY) + strlen(custom_campaign.folder) + 1 + strlen(name) >= PATH_SIZE)
	{
		return FALSE;
	}
	sprintf(path, "%s%s\\%s", CUSTOM_CAMPAIGNS_DIRECTORY, custom_campaign.folder, name);

	return GetFileAttributesA(path) != (DWORD)-1;
}

boolean custom_campaign_save_directory(
	char *directory)
{
	size_t length = strlen(directory);

	if (!custom_campaign.kind)
		return TRUE;
	if (length + strlen("campaigns\\") + strlen(custom_campaign.folder) + 2 + strlen("savegame.bin") >= PATH_SIZE)
		return FALSE;
	sprintf(directory + length, "campaigns\\");
	CreateDirectoryA(directory, NULL);
	sprintf(directory + strlen(directory), "%s\\", custom_campaign.folder);
	CreateDirectoryA(directory, NULL);

	return TRUE;
}

void custom_campaign_note_camera(
	short camera_point_index)
{
	struct scenario *scenario;
	struct scenario_cutscene_camera_point *camera_point;

	if (!custom_campaign.kind || !(scenario = global_scenario_get()) ||
		camera_point_index < 0 || camera_point_index >= scenario->cutscene_camera_points.count)
	{
		return;
	}
	camera_point = TAG_BLOCK_GET_ELEMENT(&scenario->cutscene_camera_points, camera_point_index,
		struct scenario_cutscene_camera_point);
	error(_error_silent, "custom campaign: camera_set %d '%s' (%.2f %.2f %.2f)", camera_point_index,
		camera_point->name, camera_point->position.x, camera_point->position.y, camera_point->position.z);
}
