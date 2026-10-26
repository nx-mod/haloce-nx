/*
OVERLAY_COMMANDS.C

Commands from outside the game, one at a time, through a file: an overlay
(the Switch's Ultrahand, say) writes a line to command.txt in its package's
overlay folder (OVERLAY_PACKAGE_FOLDER), and once a second the game looks for
it, takes it (the file is deleted), and runs it. The line is a console command, run as typed at the
console (hs_compile_and_evaluate: cheats, map_name, game_revert...), or
"config <setting> <value>" (or "config <setting> = <value>", the line as
config.toml has it), which sets and saves a setting as the menus do
(config_set_text). result.txt, beside it, gets the command and what came of
it, for looking at when one does not work. And what is on: each boolean the
commands set, a console global (cheat_deathless_player) or a setting
(display.vsync), has an empty file of its name in the folder's flags
(cheat_deathless_player.flag) while it is true, however it was set, looked at
once a second (overlay_flags_update); show_hud's is hud.flag. The folder's
game has OpenCE.flag, the engine the package is talking to. The whole protocol is the overlay's
spec, halo-overlay-command-spec.md; one command in the file at a time, and
its contents are trusted.

The Switch's only (its Ultrahand overlays are the reason; the other builds
have an empty overlay_commands_update), and only with the overlay installed:
its package's folder, /switch/.packages/halo/overlay, there at the start
(hardran3's OpenCE-Ultrahand). Then the game says so at the main menu, and
empties the package's overlay/game and overlay/flags folders (making them if
they are not there), which are the game's and the overlay's for this run;
and looks for the command once a second while game.overlay_commands is on.
Without that folder none of this is done, not even the look.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "hs/hs.h"
#include "interface/hud.h"
#include "interface/terminal.h"
#include "../src/posix.h"

#ifdef HALO_SWITCH

#include <stdio.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
extern FILE *platform_fopen_host(char const *path, char const *mode);
extern int platform_remove_host(char const *path);
extern int platform_remove_directory_host(char const *path);
extern int config_boolean(const char *name);
extern int config_set_text(const char *name, const char *value, char *error, size_t error_size);
extern int config_boolean_setting(unsigned index, const char **name, int *value);
extern void platform_log(const char *format, ...);
/* the console's globals (hs_globals_external.c) */
extern struct hs_external_global_definition *hs_external_globals[];
extern short const hs_external_global_count;

enum
{
	OVERLAY_COMMAND_SIZE = 1024,
	OVERLAY_LOOK_MILLISECONDS = 1000,
};

/* the Ultrahand package's folder: the overlay installed */
#define OVERLAY_PACKAGE_FOLDER "sdmc:/switch/.packages/halo/overlay"

static boolean overlay_is_folder(
	char const *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_directory) != 0;
}

/* everything in a folder deleted, the folders in it with what they hold; the folder kept */
static void overlay_folder_empty(
	char const *path,
	int depth)
{
	void *directory;
	char name[256];

	if (depth > 8 || !(directory = posix_directory_open(path)))
		return;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		char entry[1024];

		snprintf(entry, sizeof(entry), "%s/%s", path, name);
		if (overlay_is_folder(entry))
		{
			overlay_folder_empty(entry, depth + 1);
			if (platform_remove_directory_host(entry) != 0)
				platform_log("overlay: could not delete the folder %s", entry);
		}
		else if (platform_remove_host(entry) != 0)
		{
			platform_log("overlay: could not delete %s", entry);
		}
	}
	posix_directory_close(directory);
}

/* one of the package's folders for this run: emptied, or made */
static void overlay_folder_prepare(
	char const *name)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s", OVERLAY_PACKAGE_FOLDER, name);
	if (overlay_is_folder(path))
		overlay_folder_empty(path, 0);
	else
		posix_make_directory(path);
}

static void overlay_flags_update(void);

/* (once, at the first look) whether the overlay is installed, and if it is, its folders made ready and the flags of
what is on from the start written into flags at once (the settings' and the console's globals' values as the game
starts: their defaults, or config.toml's) */
static boolean overlay_present(
	void)
{
	static int present = -1;

	if (present < 0)
	{
		present = overlay_is_folder(OVERLAY_PACKAGE_FOLDER);
		if (present)
		{
			overlay_folder_prepare("game");
			overlay_folder_prepare("flags");
			/* (the game the overlay is talking to: game/<engine>.flag, which the package shows) */
			{
				char path[1024];
				FILE *file;

				snprintf(path, sizeof(path), "%s/game/OpenCE.flag", OVERLAY_PACKAGE_FOLDER);
				if ((file = platform_fopen_host(path, "wb")) != NULL)
					fclose(file);
			}
			overlay_flags_update();
			platform_log("overlay: the Ultrahand package's folder is there (%s): its commands are taken",
				OVERLAY_PACKAGE_FOLDER);
		}
	}
	return present > 0;
}

/* the result, for debugging: the command, then ok or why not */
static void overlay_result_write(
	char const *command,
	boolean succeeded,
	char const *message)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/result.txt", OVERLAY_PACKAGE_FOLDER);
	file = platform_fopen_host(path, "wb");
	if (!file)
		return;
	fprintf(file, "%s\n%s%s%s\n", command, succeeded ? "ok" : "error", message && *message ? ": " : "",
		message ? message : "");
	fclose(file);
}

/* "config <setting> <value>" or "config <setting> = <value>" */
static void overlay_config(
	char const *command,
	char *arguments)
{
	char error[256] = "";
	char *name, *value, *end;

	while (*arguments == ' ' || *arguments == '\t')
		arguments++;
	name = arguments;
	end = name + strcspn(name, " \t=");
	value = end;
	while (*value == ' ' || *value == '\t' || *value == '=')
		value++;
	*end = 0;
	if (!*name || !*value)
	{
		overlay_result_write(command, FALSE, "config takes a setting and a value: config display.vsync true");
		return;
	}
	if (config_set_text(name, value, error, sizeof(error)))
	{
		platform_log("overlay: %s set to %s", name, value);
		overlay_result_write(command, TRUE, NULL);
	}
	else
	{
		platform_log("overlay: %s", error);
		overlay_result_write(command, FALSE, error);
	}
}

/* the command in command.txt, if there is one: taken (the file deleted) and run */
static void overlay_command_take(
	void)
{
	char path[1024];
	char command[OVERLAY_COMMAND_SIZE + 1];
	size_t length;
	FILE *file;

	snprintf(path, sizeof(path), "%s/command.txt", OVERLAY_PACKAGE_FOLDER);
	/* (looked for first by the host, which says nothing of a file that is not there: the guest's open of a missing
	file is logged, once a second) */
	{
		struct posix_file_information information;

		if (posix_stat(path, &information) != 0)
			return;
	}
	file = platform_fopen_host(path, "rb");
	if (!file)
		return;
	length = fread(command, 1, OVERLAY_COMMAND_SIZE, file);
	fclose(file);
	platform_remove_host(path);
	command[length] = 0;
	/* (the first line, without its end or the spaces around it) */
	command[strcspn(command, "\r\n")] = 0;
	while (length = strlen(command), length && (command[length - 1] == ' ' || command[length - 1] == '\t'))
		command[length - 1] = 0;
	{
		char *start = command;

		while (*start == ' ' || *start == '\t')
			start++;
		if (start != command)
			memmove(command, start, strlen(start) + 1);
	}
	if (!*command)
		return;
	if (!strncmp(command, "config ", 7) || !strncmp(command, "config\t", 7))
	{
		char copy[OVERLAY_COMMAND_SIZE + 1];

		snprintf(copy, sizeof(copy), "%s", command);
		overlay_config(command, copy + 7);
		return;
	}
	platform_log("overlay: %s", command);
	if (hs_compile_and_evaluate(command))
		overlay_result_write(command, TRUE, NULL);
	else
		overlay_result_write(command, FALSE, "the console did not run it (debug.txt says why)");
}

/* A flag's file, flags/<name>.flag: there while the boolean of that name is true. */
static void overlay_flag_write(
	char const *name,
	boolean value)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/flags/%s.flag", OVERLAY_PACKAGE_FOLDER, name);
	if (value)
	{
		FILE *file = platform_fopen_host(path, "wb");

		if (file)
			fclose(file);
		else
			platform_log("overlay: could not make the flag %s", path);
	}
	else
	{
		platform_remove_host(path);
	}
}

/* Each boolean the overlay's commands set, the console's globals and this platform's settings, compared with what it
was at the last look, and its flag made or deleted when it changed. The flags folder was emptied at the start
(overlay_present), so at the first look each true one is made and nothing deleted. */
static void overlay_flags_update(
	void)
{
	enum { MAXIMUM_SETTINGS = 1024 };
	/* -1: not looked at yet */
	static signed char globals_were[1024];
	static signed char settings_were[MAXIMUM_SETTINGS];
	static signed char hud_was = -1;
	static boolean begun;
	short index;
	unsigned setting;

	if (!begun)
	{
		memset(globals_were, -1, sizeof(globals_were));
		memset(settings_were, -1, sizeof(settings_were));
		begun = TRUE;
	}
	for (index = 0; index < hs_external_global_count && index < (short)sizeof(globals_were); index++)
	{
		struct hs_external_global_definition const *global = hs_external_globals[index];
		boolean value;

		if (!global || global->type != _hs_type_boolean || !global->address)
			continue;
		value = *(boolean const *)global->address != FALSE;
		if (globals_were[index] != (signed char)value)
		{
			if (value || globals_were[index] >= 0)
				overlay_flag_write(global->name, value);
			globals_were[index] = (signed char)value;
		}
	}
	/* (and show_hud, a function the console calls with true or false: its flag is hud, as the package names it; the
	HUD's state is the map's, so looked at only with one loaded) */
	if (hud_scripted_globals && game_map_loaded())
	{
		boolean value = hud_scripted_globals->show_hud != FALSE;

		if (hud_was != (signed char)value)
		{
			if (value || hud_was >= 0)
				overlay_flag_write("hud", value);
			hud_was = (signed char)value;
		}
	}
	for (setting = 0; setting < MAXIMUM_SETTINGS; setting++)
	{
		char const *name;
		int value;
		int found = config_boolean_setting(setting, &name, &value);

		if (found < 0)
			break;
		if (!found)
			continue;
		value = value != 0;
		if (settings_were[setting] != (signed char)value)
		{
			if (value || settings_were[setting] >= 0)
				overlay_flag_write(name, (boolean)value);
			settings_were[setting] = (signed char)value;
		}
	}
}

/* (main_loop's, every frame: once a second, the command taken and the flags brought up to date) */
void overlay_commands_update(
	void)
{
	static unsigned long looked_at;
	unsigned long now = system_milliseconds();

	if (!overlay_present())
		return;
	/* (said once the main menu's map is loaded, where the game's messages are shown) */
	{
		static boolean said;

		if (!said && game_map_loaded())
		{
			said = TRUE;
			terminal_printf(global_real_argb_green, "Ultrahand Overlay present and enabled");
		}
	}
	if (now - looked_at < OVERLAY_LOOK_MILLISECONDS)
		return;
	looked_at = now;
	if (!config_boolean("game.overlay_commands"))
		return;
	overlay_command_take();
	overlay_flags_update();
}

#else

void overlay_commands_update(
	void)
{
	return;
}

#endif
