/*
CUSTOM_CAMPAIGNS.H

Custom campaigns (docs/custom_campaigns.md): a campaign is a folder of
custom_campaigns (d:\custom_campaigns\<folder>\), Xbox maps or Custom
Edition maps, played alone or in split screen. One is mounted at a time:
while it is, a map is looked for by the name in its header in the
campaign's folder first, then where the game's own are (an Xbox
campaign's) or where Custom Edition's resource maps are (a Custom Edition
campaign's); its ui.map, if it has one, is the menus' map, whose own main
menu is shown with BACK TO HALO added; and the saved game is the
campaign's own.
*/

#ifndef __CUSTOM_CAMPAIGNS_H
#define __CUSTOM_CAMPAIGNS_H

/* ---------- constants */

#define CUSTOM_CAMPAIGNS_DIRECTORY "d:\\custom_campaigns\\"
/* a campaign's main menu, when it has a ui.map: its own, as the PC's and the
Xbox's maps name it */
#define CUSTOM_CAMPAIGN_MAIN_MENU "ui\\shell\\main_menu\\main_menu"

enum custom_campaign_kind
{
	_custom_campaign_kind_none = 0,
	_custom_campaign_kind_xbox,
	_custom_campaign_kind_custom_edition,
};

/* ---------- prototypes/CUSTOM_CAMPAIGNS.C */

/* Mounts the campaign of the folder `folder` (custom_campaigns\<folder>):
its maps are indexed by the names in their headers, the map cache is
emptied (cache_files_forget_cached_maps), and the next time the menus' map
loads it is the campaign's ui.map if it has one. FALSE, with the reason
logged, if the folder is no campaign this build can play. */
boolean custom_campaign_mount(
	char const *folder);
/* The stock game again: the campaign let go, the map cache emptied. */
void custom_campaign_unmount(
	void);
/* BACK TO HALO: the campaign let go and the stock menus loaded again. */
void custom_campaign_back_to_halo(
	void);
/* The campaign debug.custom_campaign names, mounted as the game first
loads its menus (until the menus list the campaigns). */
void custom_campaign_startup(
	void);

/* the campaign mounted: its kind (_custom_campaign_kind_none when none is),
its folder's name ("" when none is), whether it has a ui.map */
short custom_campaign_kind(
	void);
char const *custom_campaign_folder(
	void);
boolean custom_campaign_has_ui(
	void);

/* The mounted campaign's file of the map `map_name` (a level name or a file
name: by the name its header gives), into `path` (256 characters): of any
kind, or only a Custom Edition map's (custom_edition) or an Xbox map's.
FALSE if the campaign has none (or none is mounted). */
boolean custom_campaign_map_path(
	char const *map_name,
	boolean custom_edition,
	char *path);
/* whether the mounted campaign is a Custom Edition one that has the map
`map_name` (a level name or a file name): its levels keep their own names
(lumoria_a, a30), which the Custom Edition loader then reads */
boolean custom_campaign_custom_edition_map(
	char const *map_name);
/* Whether the map `map_name` (a level name or a file name) is one of the
mounted campaign's, of either kind. */
boolean custom_campaign_has_map(
	char const *map_name);
/* Whether a script of the map `map_name` may call the script function
`function_name`, which maps' scripts may not (hs.c's
hs_function_allowed_in_maps): a mounted campaign's maps change the level
and its difficulty as the game's own campaign does (map_name,
game_difficulty_set, map_reset). */
boolean custom_campaign_script_may_call(
	char const *map_name,
	char const *function_name);

/* The mounted campaign's file `name` (bitmaps.map: its own resource maps),
into `path` (256 characters); FALSE if it has none. */
boolean custom_campaign_file_path(
	char const *name,
	char *path);

/* a mounted campaign's scripted camera (camera_set), logged while campaigns
are new to the port: the scenario's camera point `camera_point_index` */
void custom_campaign_note_camera(
	short camera_point_index);

/* The saved games' folder `directory` (a profile's: ...\<profile>\, 256
characters) made the mounted campaign's own, ...\<profile>\campaigns\<folder>\,
made if it is not there; left alone when no campaign is mounted. FALSE if
it cannot be. */
boolean custom_campaign_save_directory(
	char *directory);

#endif
