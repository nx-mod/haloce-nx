/*
SERVER_GAMETYPES.C

A dedicated server's gametypes (server_gametypes.h; docs/dedicated_server.md):
the variant of a built-in gametype by its name (the Xbox's default gametypes'
builders, game_engine_playlist.h), of OpenCE's (game_modes.c), of a gametype
file (gametypes/<name>.toml: its base, then its rules) or of a gametype a
player's game saved (gametypes/<name>.gametype), with a rotation entry's own
rules over it. Each rule is checked to be of the variant's game engine: a
bad gametype never reaches the wire, where the clients play by it.
*/

#include "cseries.h"
#include "game/game_engine.h"
#include "game/game_engine_playlist.h"
#include "saved games/playlist_profile.h"
#include "saved games/saved_game_files.h"
#include "text/unicode.h"
#include "game_modes.h"
#include "server_gametypes.h"

#include "../src/server_mode.h" /* port: port/linux/src/server_mode.c */
#include "../src/server_toml.h" /* port: port/linux/src/server_toml.c */
#include "../src/server_json.h" /* port: port/linux/src/server_json.c */
#include "../src/posix.h" /* port: port/linux/src/posix_files.c */

#include <stdio.h>
#include <string.h>

/* (the characters playlist_profile_get_display_name fills: the XDK's,
port/include/xdk/xdk_xbox.h) */
#ifndef MAX_GAMENAME
#define MAX_GAMENAME 128
#endif

/* a saved gametype's name as a rotation's saved=<name> (server_rotation.h):
its UTF-8, a letter, digit, "_", "-" or "." as it is and any other byte as
%XX; FALSE if it does not fit */
boolean server_gametype_saved_reference(
	wchar_t const *name,
	char *text,
	long size)
{
	static char const hex[] = "0123456789ABCDEF";
	long length;

	if (size < 8 || !name || !name[0])
		return FALSE;
	strcpy(text, "saved=");
	length = 6;
	for (; *name; name++)
	{
		unsigned long character = (unsigned long)*name & 0xFFFF;
		unsigned char bytes[3];
		int count, index;

		if (character < 0x80)
			bytes[0] = (unsigned char)character, count = 1;
		else if (character < 0x800)
		{
			bytes[0] = (unsigned char)(0xC0 | (character >> 6));
			bytes[1] = (unsigned char)(0x80 | (character & 0x3F));
			count = 2;
		}
		else
		{
			bytes[0] = (unsigned char)(0xE0 | (character >> 12));
			bytes[1] = (unsigned char)(0x80 | ((character >> 6) & 0x3F));
			bytes[2] = (unsigned char)(0x80 | (character & 0x3F));
			count = 3;
		}
		for (index = 0; index < count; index++)
		{
			unsigned char byte = bytes[index];

			if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
				byte == '_' || byte == '-' || byte == '.')
			{
				if (length + 1 >= size)
					return FALSE;
				text[length++] = (char)byte;
			}
			else
			{
				if (length + 3 >= size)
					return FALSE;
				text[length++] = '%';
				text[length++] = hex[byte >> 4];
				text[length++] = hex[byte & 0xF];
			}
		}
	}
	text[length] = 0;
	return TRUE;
}

static int server_gametype_hex_digit(
	char character)
{
	return character >= '0' && character <= '9' ? character - '0' :
		character >= 'a' && character <= 'f' ? character - 'a' + 10 :
		character >= 'A' && character <= 'F' ? character - 'A' + 10 : -1;
}

boolean server_gametype_saved_name(
	char const *reference,
	wchar_t *name,
	long size)
{
	unsigned char bytes[SERVER_GAMETYPE_SIZE];
	long count = 0, index, length = 0;

	/* (the %XX undone, into bytes) */
	for (; *reference; reference++)
	{
		int high, low;

		if (count >= (long)sizeof(bytes))
			return FALSE;
		if (*reference != '%')
		{
			bytes[count++] = (unsigned char)*reference;
			continue;
		}
		high = server_gametype_hex_digit(reference[1]);
		low = high < 0 ? -1 : server_gametype_hex_digit(reference[2]);
		if (low < 0)
			return FALSE;
		bytes[count++] = (unsigned char)(high << 4 | low);
		reference += 2;
	}
	/* (then the UTF-8 read: no more than three bytes a character, as the
	game's names have) */
	for (index = 0; index < count; )
	{
		unsigned long character;
		unsigned char byte = bytes[index];

		if (byte < 0x80)
			character = byte, index += 1;
		else if ((byte & 0xE0) == 0xC0 && index + 1 < count && (bytes[index + 1] & 0xC0) == 0x80)
		{
			character = (unsigned long)(byte & 0x1F) << 6 | (bytes[index + 1] & 0x3F);
			index += 2;
		}
		else if ((byte & 0xF0) == 0xE0 && index + 2 < count && (bytes[index + 1] & 0xC0) == 0x80 &&
			(bytes[index + 2] & 0xC0) == 0x80)
		{
			character = (unsigned long)(byte & 0x0F) << 12 | (unsigned long)(bytes[index + 1] & 0x3F) << 6 |
				(bytes[index + 2] & 0x3F);
			index += 3;
		}
		else
			return FALSE;
		if (length + 1 >= size)
			return FALSE;
		name[length++] = (wchar_t)character;
	}
	name[length] = 0;
	return length > 0;
}

#if HALO_HOST_BUILD

/* the gametype in the game's saved games (not one of the built-in ones it
keeps there too) whose name is `name`, its own case first, then in any case
of its ASCII letters; NONE if none */
static long server_gametype_saved_find(
	wchar_t const *name)
{
	long indices[256];
	word count = (word)NUMBEROF(indices);
	long found = NONE;
	word index;

	playlist_profiles_enumerate_available_to_local_player_index(0, &count, indices);
	for (index = 0; index < count; index++)
	{
		wchar_t shown[MAX_GAMENAME];
		long character;

		if (TEST_FLAG(indices[index], _saved_game_file_index_read_only_bit) ||
			!playlist_profile_get_display_name(indices[index], shown))
		{
			continue;
		}
		if (!ustrcmp(shown, name))
			return indices[index];
		for (character = 0; shown[character] && name[character]; character++)
		{
			wchar_t left = shown[character], right = name[character];

			if (left >= L'A' && left <= L'Z')
				left = (wchar_t)(left + 32);
			if (right >= L'A' && right <= L'Z')
				right = (wchar_t)(right + 32);
			if (left != right)
				break;
		}
		if (!shown[character] && !name[character] && found == NONE)
			found = indices[index];
	}
	return found;
}

/* the built-in gametypes, by name, and what the lobby and the server
browser call them (11 characters) */
static struct
{
	char const *name;
	struct game_variant *(*build)(struct game_variant *variant);
	wchar_t const *shown;
} const server_builtin_gametypes[] =
{
	{ "slayer", build_game_variant_slayer, L"Slayer" },
	{ "team_slayer", build_game_variant_team_slayer, L"Team Slayer" },
	{ "slayer_pro", build_game_variant_slayer_pro, L"Slayer Pro" },
	{ "elimination", build_game_variant_elimination, L"Elimination" },
	{ "phantoms", build_game_variant_phantoms, L"Phantoms" },
	{ "endurance", build_game_variant_endurance, L"Endurance" },
	{ "rockets", build_game_variant_rockets, L"Rockets" },
	{ "snipers", build_game_variant_snipers, L"Snipers" },
	{ "stalker", build_game_variant_stalker, L"Stalker" },
	{ "ctf", build_game_variant_ctf, L"CTF" },
	{ "ironctf", build_game_variant_iron_ctf, L"Iron CTF" },
	{ "ctf_pro", build_game_variant_ctf_pro, L"CTF Pro" },
	{ "invasion", build_game_variant_invasion, L"Invasion" },
	{ "oddball", build_game_variant_oddball, L"Oddball" },
	{ "team_oddball", build_game_variant_team_oddball, L"Team Ball" },
	{ "reverse_tag", build_game_variant_reverse_tag, L"Reverse Tag" },
	{ "accumulation", build_game_variant_accumulation, L"Accumulate" },
	{ "juggernaut", build_game_variant_juggernaut, L"Juggernaut" },
	{ "king", build_game_variant_king, L"King" },
	{ "team_king", build_game_variant_team_king, L"Team King" },
	{ "king_pro", build_game_variant_king_pro, L"King Pro" },
	{ "crazy_king", build_game_variant_crazy_king, L"Crazy King" },
	{ "race", build_game_variant_race, L"Race" },
	{ "team_race", build_game_variant_team_race, L"Team Race" },
	{ "rally", build_game_variant_rally, L"Rally" },
	{ "team_rally", build_game_variant_team_rally, L"Team Rally" },
};

/* Halo PC's dedicated server's names for them */
static struct
{
	char const *alias;
	char const *name;
} const server_gametype_aliases[] =
{
	{ "ffa", "slayer" },
	{ "ffa_slayer", "slayer" },
	{ "teamslayer", "team_slayer" },
	{ "tslayer", "team_slayer" },
	{ "koth", "king" },
	{ "team_koth", "team_king" },
	{ "iron_ctf", "ironctf" },
	{ "teamrace", "team_race" },
	{ "teamoddball", "team_oddball" },
};

static char const *const server_opence_gametypes[] =
{
	"swat", "infection", "gun_game", "hardcore", "hardcore_ctf", "hardcore_oddball", "hardcore_king",
};

char const *server_gametype_builtin_name(
	long index)
{
	if (index >= 0 && index < (long)NUMBEROF(server_builtin_gametypes))
		return server_builtin_gametypes[index].name;
	index -= (long)NUMBEROF(server_builtin_gametypes);
	if (index >= 0 && index < (long)NUMBEROF(server_opence_gametypes))
		return server_opence_gametypes[index];
	return NULL;
}

char const *server_gametype_engine_name(
	long game_engine_index)
{
	switch (game_engine_index)
	{
	case game_engine_ctf: return "ctf";
	case game_engine_slayer: return "slayer";
	case game_engine_oddball: return "oddball";
	case game_engine_king: return "king";
	case game_engine_race: return "race";
	default: return "none";
	}
}

/* a built-in's or OpenCE's gametype by its name (or alias): TRUE */
static boolean server_gametype_base(
	char const *name,
	struct game_variant *variant,
	struct game_variant_options *options)
{
	struct game_variant temporary;
	long index;

	for (index = 0; index < (long)NUMBEROF(server_gametype_aliases); index++)
	{
		if (!strcmp(name, server_gametype_aliases[index].alias))
			name = server_gametype_aliases[index].name;
	}
	for (index = 0; index < (long)NUMBEROF(server_builtin_gametypes); index++)
	{
		if (!strcmp(name, server_builtin_gametypes[index].name))
		{
			*variant = *server_builtin_gametypes[index].build(&temporary);
			game_variant_options_default(variant, options);
			if (!variant->human_readable_game_description[0])
			{
				ustrncpy(variant->human_readable_game_description, server_builtin_gametypes[index].shown,
					NUMBEROF(variant->human_readable_game_description) - 1);
			}
			return TRUE;
		}
	}
	return game_modes_gametype_by_name(name, variant, options);
}

static void server_gametype_flag(
	struct game_variant *variant,
	long bit,
	long value)
{
	SET_FLAG(variant->universal_variant.flags, bit, value != 0);
}

/* a team's vehicle set by team_vehicles' word: the vehicle set's
(gametype), PC's, the counts (custom), or one of the vehicle options' sets */
static byte server_gametype_team_vehicles(
	long value,
	struct universal_variant const *universal)
{
	return value == 1 ? VARIANT_VEHICLE_SET_PC : value == 2 ? VARIANT_VEHICLE_SET_CUSTOM :
		value >= 3 ? (byte)(value - 3) : (byte)universal->vehicle_set;
}

/* a rule over the variant and options (its engine already checked) */
static void server_gametype_rule(
	struct server_rule const *rule,
	struct game_variant *variant,
	struct game_variant_options *options,
	boolean *team_vehicles_given,
	boolean *vehicle_counts_given)
{
	char const *key = rule->key;
	long value = rule->value;
	struct universal_variant *universal = &variant->universal_variant;
	static char const *const vehicles[NUMBER_OF_VARIANT_VEHICLES] =
		{ "warthogs", "ghosts", "scorpions", "rocket_warthogs", "banshees", "gun_turrets" };
	long index;

	if (!strcmp(key, "score_to_win")) universal->score_to_win = value;
	else if (!strcmp(key, "lives")) universal->lives = value;
	else if (!strcmp(key, "respawn_time")) universal->respawn_time = value * TICKS_PER_SECOND;
	else if (!strcmp(key, "respawn_time_growth")) universal->respawn_time_growth = value * TICKS_PER_SECOND;
	else if (!strcmp(key, "suicide_penalty")) universal->suicide_penalty = value * TICKS_PER_SECOND;
	else if (!strcmp(key, "health")) universal->health = (real)value / 100.0f;
	else if (!strcmp(key, "teams")) universal->teams = (boolean)(value != 0);
	else if (!strcmp(key, "odd_man_out")) universal->odd_man_out = (boolean)(value != 0);
	else if (!strcmp(key, "radar")) server_gametype_flag(variant, _game_variant_draw_object_in_motion_sensor_bit, value);
	else if (!strcmp(key, "friend_indicators")) server_gametype_flag(variant, _game_variant_allow_friendly_navpoints_bit, value);
	else if (!strcmp(key, "infinite_grenades")) server_gametype_flag(variant, _game_variant_infinite_grenades_bit, value);
	else if (!strcmp(key, "no_shields")) server_gametype_flag(variant, _game_variant_no_shields_bit, value);
	else if (!strcmp(key, "invisible")) server_gametype_flag(variant, _game_variant_always_invisible_bit, value);
	else if (!strcmp(key, "generic_equipment")) server_gametype_flag(variant, _game_variant_generic_starting_equipment_bit, value);
	else if (!strcmp(key, "vehicle_riding")) server_gametype_flag(variant, _game_variant_vehicle_riding_bit, value);
	else if (!strcmp(key, "weapon_set")) universal->weapon_set = value;
	else if (!strcmp(key, "vehicle_set")) universal->vehicle_set = value;
	else if (!strcmp(key, "goal_radar")) universal->goal_radar = value;
	else if (!strcmp(key, "time_limit")) options->time_limit = (short)value;
	else if (!strcmp(key, "friendly_fire")) options->friendly_fire = (short)value;
	else if (!strcmp(key, "friendly_fire_penalty")) options->friendly_fire_penalty = (short)value;
	else if (!strcmp(key, "vehicle_respawn_time")) options->vehicle_respawn_time = (short)value;
	else if (!strcmp(key, "auto_team_balance")) options->auto_team_balance = (boolean)(value != 0);
	else if (!strcmp(key, "radar_players")) options->radar_players = (byte)value;
	else if (!strcmp(key, "loadout")) options->loadout = (byte)value;
	else if (!strcmp(key, "primary_weapon")) options->primary_weapon = (byte)value;
	else if (!strcmp(key, "secondary_weapon")) options->secondary_weapon = (byte)value;
	else if (!strcmp(key, "no_map_weapons")) options->no_map_weapons = (boolean)(value != 0);
	else if (!strcmp(key, "hardcore")) options->hardcore = (boolean)(value != 0);
	else if (!strcmp(key, "force_team_spawns")) options->force_team_spawns = (boolean)(value != 0);
	else if (!strcmp(key, "team_vehicles"))
	{
		options->vehicle_set[0] = options->vehicle_set[1] = server_gametype_team_vehicles(value, universal);
		*team_vehicles_given = TRUE;
	}
	/* (blue's own: applied last, server_gametype_apply_blue) */
	else if (!strncmp(key, "blue_", 5))
		;
	else if (!strcmp(key, "assault")) variant->game_engine_variant.ctf.assault = (boolean)(value != 0);
	else if (!strcmp(key, "reset_on_capture")) variant->game_engine_variant.ctf.reset_on_capture = (boolean)(value != 0);
	else if (!strcmp(key, "flag_must_reset")) variant->game_engine_variant.ctf.flag_must_reset = (boolean)(value != 0);
	else if (!strcmp(key, "flag_at_home_to_score")) variant->game_engine_variant.ctf.flag_at_home_to_score = (boolean)(value != 0);
	else if (!strcmp(key, "single_flag_time")) variant->game_engine_variant.ctf.single_flag_time = value * TICKS_PER_SECOND;
	else if (!strcmp(key, "no_death_bonus")) variant->game_engine_variant.slayer.no_death_bonus = (boolean)(value != 0);
	else if (!strcmp(key, "no_kill_penalty")) variant->game_engine_variant.slayer.no_kill_penalty = (boolean)(value != 0);
	else if (!strcmp(key, "kill_in_order")) variant->game_engine_variant.slayer.kill_in_order = (boolean)(value != 0);
	else if (!strcmp(key, "moving_hill")) variant->game_engine_variant.king.moving_hill = (boolean)(value != 0);
	else if (!strcmp(key, "random_start")) variant->game_engine_variant.oddball.random_start = (boolean)(value != 0);
	else if (!strcmp(key, "ball_spawn_delay")) variant->game_engine_variant.oddball.ball_spawn_delay = (boolean)(value != 0);
	else if (!strcmp(key, "speed_with_ball")) variant->game_engine_variant.oddball.speed_with_ball = value;
	else if (!strcmp(key, "trait_with_ball")) variant->game_engine_variant.oddball.trait_with_ball = value;
	else if (!strcmp(key, "trait_without_ball")) variant->game_engine_variant.oddball.trait_without_ball = value;
	else if (!strcmp(key, "ball_type")) variant->game_engine_variant.oddball.oddball_ball_type = value;
	else if (!strcmp(key, "ball_spawn_count")) variant->game_engine_variant.oddball.ball_spawn_count = value;
	else if (!strcmp(key, "race_type")) variant->game_engine_variant.race.race_type = value;
	else if (!strcmp(key, "team_scoring")) variant->game_engine_variant.race.team_scoring = value;
	else
	{
		for (index = 0; index < NUMBER_OF_VARIANT_VEHICLES; index++)
		{
			if (!strcmp(key, vehicles[index]))
			{
				options->vehicle_counts[0][index] = options->vehicle_counts[1][index] = (byte)value;
				*vehicle_counts_given = TRUE;
			}
		}
	}
}

/* each team's vehicles after the rules: the vehicle set's unless said, a
count's making them custom (blue's own come after: server_gametype_apply_blue) */
static void server_gametype_vehicles_settle(
	struct game_variant *variant,
	struct game_variant_options *options,
	long vehicle_set_before,
	boolean team_vehicles_given,
	boolean vehicle_counts_given)
{
	if (vehicle_counts_given && !team_vehicles_given)
		options->vehicle_set[0] = options->vehicle_set[1] = VARIANT_VEHICLE_SET_CUSTOM;
	else if (!team_vehicles_given && vehicle_set_before != variant->universal_variant.vehicle_set)
		options->vehicle_set[0] = options->vehicle_set[1] = (byte)variant->universal_variant.vehicle_set;
}

/* blue's own vehicles (blue_team_vehicles, blue_<vehicle>), over what the
other rules gave both teams: its counts alone make its set custom */
static void server_gametype_apply_blue(
	struct server_rule const *rules,
	long rule_count,
	struct game_variant *variant,
	struct game_variant_options *options)
{
	static char const *const vehicles[NUMBER_OF_VARIANT_VEHICLES] =
		{ "warthogs", "ghosts", "scorpions", "rocket_warthogs", "banshees", "gun_turrets" };
	boolean set_given = FALSE, counts_given = FALSE;
	long index, vehicle;

	for (index = 0; index < rule_count; index++)
	{
		char const *key = rules[index].key;

		if (strncmp(key, "blue_", 5))
			continue;
		if (!strcmp(key + 5, "team_vehicles"))
		{
			options->vehicle_set[1] = server_gametype_team_vehicles(rules[index].value, &variant->universal_variant);
			set_given = TRUE;
		}
		for (vehicle = 0; vehicle < NUMBER_OF_VARIANT_VEHICLES; vehicle++)
		{
			if (!strcmp(key + 5, vehicles[vehicle]))
			{
				options->vehicle_counts[1][vehicle] = (byte)rules[index].value;
				counts_given = TRUE;
			}
		}
	}
	if (counts_given && !set_given)
		options->vehicle_set[1] = VARIANT_VEHICLE_SET_CUSTOM;
}

/* the rules over the variant, each of its engine: FALSE and why if one is
another's */
static boolean server_gametype_apply_rules(
	struct server_rule const *rules,
	long rule_count,
	struct game_variant *variant,
	struct game_variant_options *options,
	boolean *team_vehicles_given,
	boolean *vehicle_counts_given,
	char *reason,
	long reason_size)
{
	long engine_bit = variant->game_engine_index >= game_engine_ctf && variant->game_engine_index <= game_engine_race ?
		1L << (variant->game_engine_index - game_engine_ctf) : 0;
	long index;

	for (index = 0; index < rule_count; index++)
	{
		struct server_rule_definition const *definition = server_rule_find(rules[index].key);

		if (!definition)
		{
			snprintf(reason, (size_t)reason_size, "no rule is named %s", rules[index].key);
			return FALSE;
		}
		if (!(definition->engines & engine_bit))
		{
			snprintf(reason, (size_t)reason_size, "%s is a %s gametype's rule, and this one is %s", definition->key,
				definition->table, server_gametype_engine_name(variant->game_engine_index));
			return FALSE;
		}
		server_gametype_rule(&rules[index], variant, options, team_vehicles_given, vehicle_counts_given);
	}
	return TRUE;
}

boolean server_gametype_build(
	char const *gametype,
	struct server_rule const *rules,
	long rule_count,
	struct game_variant *variant,
	struct game_variant_options *options,
	char *reason,
	long reason_size)
{
	boolean team_vehicles_given = FALSE;
	boolean vehicle_counts_given = FALSE;
	struct universal_variant before;
	/* (a gametype file's, whose blue_ rules are applied last too) */
	struct server_gametype_file file;
	boolean have_file = FALSE;

	csmemset(variant, 0, sizeof(*variant));
	csmemset(options, 0, sizeof(*options));
	reason[0] = 0;
	if (!strncmp(gametype, "file=", 5))
	{
		/* a gametype a player's game saved, copied into the gametypes folder
		(the .toml files' own: beside config.toml first, server_toml.c's
		server_gametypes_folder) */
		byte block[SAVED_GAME_FILE_BLOCK_SIZE];
		int size;

		if (!server_gametype_file_name_valid(gametype + 5))
		{
			snprintf(reason, (size_t)reason_size, "\"%s\" is not a gametype file's name", gametype + 5);
			return FALSE;
		}
		size = server_gametype_saved_read(gametype + 5, block, sizeof(block));
		if (size < 0 || !playlist_profile_get_from_data_with_options(block, (unsigned long)size, variant, options))
		{
			snprintf(reason, (size_t)reason_size, "gametypes/%s.gametype is missing, or not a saved gametype",
				gametype + 5);
			return FALSE;
		}
		if (variant->game_engine_index < game_engine_ctf || variant->game_engine_index > game_engine_race)
		{
			snprintf(reason, (size_t)reason_size, "gametypes/%s.gametype has no game engine", gametype + 5);
			return FALSE;
		}
	}
	else if (!strncmp(gametype, "saved=", 6))
	{
		/* a gametype in the game's saved games (Edit Gametypes'), found by
		its name each time, so that its later changes are played */
		wchar_t name[NUMBEROF(variant->human_readable_game_description)];
		long profile_index;

		if (!server_gametype_saved_name(gametype + 6, name, NUMBEROF(name)))
		{
			snprintf(reason, (size_t)reason_size, "\"%s\" is not a saved gametype's name", gametype + 6);
			return FALSE;
		}
		profile_index = server_gametype_saved_find(name);
		if (profile_index == NONE || !playlist_profile_get(profile_index, variant) ||
			!playlist_profile_get_options(profile_index, options))
		{
			snprintf(reason, (size_t)reason_size, "there is no saved gametype named %s (Edit Gametypes')",
				gametype + 6);
			return FALSE;
		}
		if (variant->game_engine_index < game_engine_ctf || variant->game_engine_index > game_engine_race)
		{
			snprintf(reason, (size_t)reason_size, "the saved gametype %s has no game engine", gametype + 6);
			return FALSE;
		}
	}
	else if (!server_gametype_base(gametype, variant, options))
	{
		int absent;

		have_file = TRUE;
		if (!server_gametype_file_read(gametype, &file, &absent))
		{
			if (absent)
			{
				snprintf(reason, (size_t)reason_size,
					"no gametype is named %s (a built-in's, OpenCE's, or gametypes/%s.toml)", gametype, gametype);
			}
			else if (file.error_line > 0)
			{
				snprintf(reason, (size_t)reason_size, "gametypes/%s.toml line %d: %s", gametype, file.error_line,
					file.error);
			}
			else
			{
				snprintf(reason, (size_t)reason_size, "gametypes/%s.toml: %s", gametype, file.error);
			}
			return FALSE;
		}
		if (!server_gametype_base(file.base, variant, options))
		{
			snprintf(reason, (size_t)reason_size, "gametypes/%s.toml: its base, %s, is no built-in gametype", gametype,
				file.base);
			return FALSE;
		}
		if (file.engine_table[0] && strcmp(file.engine_table, server_gametype_engine_name(variant->game_engine_index)))
		{
			snprintf(reason, (size_t)reason_size, "gametypes/%s.toml: [%s] is not its base's game engine (%s)",
				gametype, file.engine_table, server_gametype_engine_name(variant->game_engine_index));
			return FALSE;
		}
		if (!server_gametype_apply_rules(file.rules, file.rule_count, variant, options, &team_vehicles_given,
			&vehicle_counts_given, reason, reason_size))
		{
			char why[SERVER_REASON_SIZE];

			snprintf(why, sizeof(why), "%s", reason);
			snprintf(reason, (size_t)reason_size, "gametypes/%s.toml: %s", gametype, why);
			return FALSE;
		}
		if (file.name[0])
		{
			long index;

			csmemset(variant->human_readable_game_description, 0, sizeof(variant->human_readable_game_description));
			for (index = 0; file.name[index] && index < (long)NUMBEROF(variant->human_readable_game_description) - 1;
				index++)
			{
				variant->human_readable_game_description[index] = (wchar_t)(unsigned char)file.name[index];
			}
		}
	}
	before = variant->universal_variant;
	if (!server_gametype_apply_rules(rules, rule_count, variant, options, &team_vehicles_given, &vehicle_counts_given,
		reason, reason_size))
	{
		return FALSE;
	}
	server_gametype_vehicles_settle(variant, options, before.vehicle_set, team_vehicles_given, vehicle_counts_given);
	if (have_file)
		server_gametype_apply_blue(file.rules, file.rule_count, variant, options);
	server_gametype_apply_blue(rules, rule_count, variant, options);
	/* (CTF always has teams) */
	if (variant->game_engine_index == game_engine_ctf)
		variant->universal_variant.teams = TRUE;
	game_modes_options_sanitize(options);
	return TRUE;
}

/* (the web's: server_http.c) a gametype file's text checked as the
rotation would take it: TRUE, else why and its line */
int server_gametype_check_text(
	char const *text,
	int length,
	char *reason,
	long reason_size,
	int *line)
{
	struct server_gametype_file file;
	struct game_variant variant;
	struct game_variant_options options;
	boolean team_vehicles_given = FALSE, vehicle_counts_given = FALSE;

	*line = 0;
	if (!server_gametype_file_parse(text, length, &file))
	{
		snprintf(reason, (size_t)reason_size, "%s", file.error);
		*line = file.error_line;
		return FALSE;
	}
	if (!server_gametype_base(file.base, &variant, &options))
	{
		snprintf(reason, (size_t)reason_size, "base = \"%s\" is no built-in gametype", file.base);
		return FALSE;
	}
	if (file.engine_table[0] && strcmp(file.engine_table, server_gametype_engine_name(variant.game_engine_index)))
	{
		snprintf(reason, (size_t)reason_size, "[%s] is not its base's game engine (%s)", file.engine_table,
			server_gametype_engine_name(variant.game_engine_index));
		return FALSE;
	}
	return server_gametype_apply_rules(file.rules, file.rule_count, &variant, &options, &team_vehicles_given,
		&vehicle_counts_given, reason, reason_size);
}

/* (the web's) the gametype files there are: "files":[{name, base,
description}] */
void server_gametype_files_json(
	struct server_json *json)
{
	char folder[1024];
	char name[256];
	void *directory;

	server_gametypes_folder(folder, sizeof(folder));
	server_json_array_begin(json, "files");
	directory = posix_directory_open(folder);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		size_t length = strlen(name);
		struct server_gametype_file file;
		char base_name[64];
		int absent;

		if (length < 6 || length - 5 >= sizeof(base_name) || strcmp(name + length - 5, ".toml"))
			continue;
		snprintf(base_name, sizeof(base_name), "%.*s", (int)(length - 5), name);
		server_json_object_begin(json, NULL);
		server_json_string(json, "name", base_name);
		if (server_gametype_file_read(base_name, &file, &absent))
		{
			server_json_string(json, "base", file.base);
			server_json_string(json, "description", file.name);
			server_json_null(json, "error");
		}
		else
		{
			server_json_string(json, "base", "");
			server_json_string(json, "description", "");
			server_json_string(json, "error", file.error);
		}
		server_json_object_end(json);
	}
	if (directory)
		posix_directory_close(directory);
	server_json_array_end(json);
}

void server_gametype_check_entry(
	struct server_entry *entry)
{
	struct game_variant variant;
	struct game_variant_options options;
	char reason[SERVER_REASON_SIZE];

	if (entry->rejected[0] || !strcmp(entry->gametype, "coop"))
		return;
	if (!server_gametype_build(entry->gametype, entry->rules, entry->rule_count, &variant, &options, reason,
		sizeof(reason)))
	{
		snprintf(entry->rejected, sizeof(entry->rejected), "%s", reason);
	}
}

/* a rule's value in a variant and options, as the rule sets it (the
inverse of server_gametype_rule); FALSE for the vehicles' rules, which
server_gametype_rules_text takes together, and a rule of no field */
static boolean server_gametype_rule_get(
	char const *key,
	struct game_variant const *variant,
	struct game_variant_options const *options,
	long *value)
{
	struct universal_variant const *universal = &variant->universal_variant;
	union game_engine_variant const *engine = &variant->game_engine_variant;

#define FLAG_OF(bit) (TEST_FLAG(universal->flags, bit) ? 1 : 0)
	if (!strcmp(key, "score_to_win")) *value = universal->score_to_win;
	else if (!strcmp(key, "lives")) *value = universal->lives;
	else if (!strcmp(key, "respawn_time")) *value = universal->respawn_time / TICKS_PER_SECOND;
	else if (!strcmp(key, "respawn_time_growth")) *value = universal->respawn_time_growth / TICKS_PER_SECOND;
	else if (!strcmp(key, "suicide_penalty")) *value = universal->suicide_penalty / TICKS_PER_SECOND;
	else if (!strcmp(key, "health")) *value = (long)(universal->health * 100.0f + 0.5f);
	else if (!strcmp(key, "teams")) *value = universal->teams != 0;
	else if (!strcmp(key, "odd_man_out")) *value = universal->odd_man_out != 0;
	else if (!strcmp(key, "radar")) *value = FLAG_OF(_game_variant_draw_object_in_motion_sensor_bit);
	else if (!strcmp(key, "friend_indicators")) *value = FLAG_OF(_game_variant_allow_friendly_navpoints_bit);
	else if (!strcmp(key, "infinite_grenades")) *value = FLAG_OF(_game_variant_infinite_grenades_bit);
	else if (!strcmp(key, "no_shields")) *value = FLAG_OF(_game_variant_no_shields_bit);
	else if (!strcmp(key, "invisible")) *value = FLAG_OF(_game_variant_always_invisible_bit);
	else if (!strcmp(key, "generic_equipment")) *value = FLAG_OF(_game_variant_generic_starting_equipment_bit);
	else if (!strcmp(key, "vehicle_riding")) *value = FLAG_OF(_game_variant_vehicle_riding_bit);
	else if (!strcmp(key, "weapon_set")) *value = universal->weapon_set;
	else if (!strcmp(key, "goal_radar")) *value = universal->goal_radar;
	else if (!strcmp(key, "time_limit")) *value = options->time_limit;
	else if (!strcmp(key, "friendly_fire")) *value = options->friendly_fire;
	else if (!strcmp(key, "friendly_fire_penalty")) *value = options->friendly_fire_penalty;
	else if (!strcmp(key, "vehicle_respawn_time")) *value = options->vehicle_respawn_time;
	else if (!strcmp(key, "auto_team_balance")) *value = options->auto_team_balance != 0;
	else if (!strcmp(key, "radar_players")) *value = options->radar_players;
	else if (!strcmp(key, "loadout")) *value = options->loadout;
	else if (!strcmp(key, "primary_weapon")) *value = options->primary_weapon;
	else if (!strcmp(key, "secondary_weapon")) *value = options->secondary_weapon;
	else if (!strcmp(key, "no_map_weapons")) *value = options->no_map_weapons != 0;
	else if (!strcmp(key, "hardcore")) *value = options->hardcore != 0;
	else if (!strcmp(key, "force_team_spawns")) *value = options->force_team_spawns != 0;
	else if (!strcmp(key, "assault")) *value = engine->ctf.assault != 0;
	else if (!strcmp(key, "reset_on_capture")) *value = engine->ctf.reset_on_capture != 0;
	else if (!strcmp(key, "flag_must_reset")) *value = engine->ctf.flag_must_reset != 0;
	else if (!strcmp(key, "flag_at_home_to_score")) *value = engine->ctf.flag_at_home_to_score != 0;
	else if (!strcmp(key, "single_flag_time")) *value = engine->ctf.single_flag_time / TICKS_PER_SECOND;
	else if (!strcmp(key, "no_death_bonus")) *value = engine->slayer.no_death_bonus != 0;
	else if (!strcmp(key, "no_kill_penalty")) *value = engine->slayer.no_kill_penalty != 0;
	else if (!strcmp(key, "kill_in_order")) *value = engine->slayer.kill_in_order != 0;
	else if (!strcmp(key, "moving_hill")) *value = engine->king.moving_hill != 0;
	else if (!strcmp(key, "random_start")) *value = engine->oddball.random_start != 0;
	else if (!strcmp(key, "ball_spawn_delay")) *value = engine->oddball.ball_spawn_delay != 0;
	else if (!strcmp(key, "speed_with_ball")) *value = engine->oddball.speed_with_ball;
	else if (!strcmp(key, "trait_with_ball")) *value = engine->oddball.trait_with_ball;
	else if (!strcmp(key, "trait_without_ball")) *value = engine->oddball.trait_without_ball;
	else if (!strcmp(key, "ball_type")) *value = engine->oddball.oddball_ball_type;
	else if (!strcmp(key, "ball_spawn_count")) *value = engine->oddball.ball_spawn_count;
	else if (!strcmp(key, "race_type")) *value = engine->race.race_type;
	else if (!strcmp(key, "team_scoring")) *value = engine->race.team_scoring;
	else
		return FALSE;
#undef FLAG_OF
	return TRUE;
}

/* "+key=value" appended to text (the rule's alias if it has one: the
shorter); FALSE when it does not fit, or its value is not the rule's */
static boolean server_gametype_rule_append(
	struct server_rule_definition const *definition,
	long value,
	char *text,
	long size,
	long *length,
	int *count)
{
	char value_text[32];
	int written;

	if (definition->type == _server_rule_boolean)
		snprintf(value_text, sizeof(value_text), "%s", value ? "on" : "off");
	else if (definition->type == _server_rule_choice)
	{
		long index;

		for (index = 0; definition->choices[index] && index < value; index++)
			;
		if (value < 0 || index != value || !definition->choices[index])
			return FALSE;
		snprintf(value_text, sizeof(value_text), "%s", definition->choices[index]);
	}
	else
	{
		if (value < definition->minimum || value > definition->maximum)
			return FALSE;
		snprintf(value_text, sizeof(value_text), "%ld", value);
	}
	written = snprintf(text + *length, (size_t)(size - *length), "+%s=%s",
		definition->alias ? definition->alias : definition->key, value_text);
	if (written < 0 || *length + written >= size)
		return FALSE;
	*length += written;
	(*count)++;
	return TRUE;
}

/* team_vehicles' word's value for a team's vehicle set */
static long server_gametype_team_vehicles_word(
	byte set)
{
	return set == VARIANT_VEHICLE_SET_PC ? 1 : set == VARIANT_VEHICLE_SET_CUSTOM ? 2 : 3 + (long)set;
}

/* whether a team's vehicles are not the same (its set, or a custom set's
counts) */
static boolean server_gametype_team_vehicles_differ(
	struct game_variant_options const *a,
	struct game_variant_options const *b,
	short side)
{
	return a->vehicle_set[side] != b->vehicle_set[side] || (b->vehicle_set[side] == VARIANT_VEHICLE_SET_CUSTOM &&
		memcmp(a->vehicle_counts[side], b->vehicle_counts[side], sizeof(a->vehicle_counts[side])));
}

boolean server_gametype_rules_text(
	struct game_variant const *base_variant,
	struct game_variant_options const *base_options,
	struct game_variant const *variant,
	struct game_variant_options const *options,
	char *text,
	long size,
	int *count)
{
	static char const *const vehicles[NUMBER_OF_VARIANT_VEHICLES] =
		{ "warthogs", "ghosts", "scorpions", "rocket_warthogs", "banshees", "gun_turrets" };
	long engine_bit = variant->game_engine_index >= game_engine_ctf && variant->game_engine_index <= game_engine_race ?
		1L << (variant->game_engine_index - game_engine_ctf) : 0;
	long length = 0;
	boolean kept = TRUE;
	int index;

	*count = 0;
	if (size < 1)
		return FALSE;
	text[0] = 0;
	for (index = 0; index < server_rule_definition_count(); index++)
	{
		struct server_rule_definition const *definition = &server_rule_definitions[index];
		long before, after;

		if (!(definition->engines & engine_bit) ||
			!server_gametype_rule_get(definition->key, base_variant, base_options, &before) ||
			!server_gametype_rule_get(definition->key, variant, options, &after) || before == after)
		{
			continue;
		}
		kept &= server_gametype_rule_append(definition, after, text, size, &length, count);
	}
	/* (the vehicles: red's set and counts for both teams, then blue's own
	where they are not red's; with the Xbox's vehicle set, which a team's
	set follows) */
	if (base_variant->universal_variant.vehicle_set != variant->universal_variant.vehicle_set ||
		server_gametype_team_vehicles_differ(base_options, options, 0) ||
		server_gametype_team_vehicles_differ(base_options, options, 1))
	{
		byte counts[2][NUMBER_OF_VARIANT_VEHICLES];
		long vehicle;

		memcpy(counts, base_options->vehicle_counts, sizeof(counts));
		if (base_variant->universal_variant.vehicle_set != variant->universal_variant.vehicle_set)
		{
			kept &= server_gametype_rule_append(server_rule_find("vehicle_set"), variant->universal_variant.vehicle_set,
				text, size, &length, count);
		}
		kept &= server_gametype_rule_append(server_rule_find("team_vehicles"),
			server_gametype_team_vehicles_word(options->vehicle_set[0]), text, size, &length, count);
		for (vehicle = 0; options->vehicle_set[0] == VARIANT_VEHICLE_SET_CUSTOM &&
			vehicle < NUMBER_OF_VARIANT_VEHICLES; vehicle++)
		{
			if (counts[0][vehicle] == options->vehicle_counts[0][vehicle])
				continue;
			kept &= server_gametype_rule_append(server_rule_find(vehicles[vehicle]),
				options->vehicle_counts[0][vehicle], text, size, &length, count);
			counts[0][vehicle] = counts[1][vehicle] = options->vehicle_counts[0][vehicle];
		}
		if (options->vehicle_set[1] != options->vehicle_set[0])
		{
			kept &= server_gametype_rule_append(server_rule_find("blue_team_vehicles"),
				server_gametype_team_vehicles_word(options->vehicle_set[1]), text, size, &length, count);
		}
		for (vehicle = 0; options->vehicle_set[1] == VARIANT_VEHICLE_SET_CUSTOM &&
			vehicle < NUMBER_OF_VARIANT_VEHICLES; vehicle++)
		{
			char key[32];

			if (counts[1][vehicle] == options->vehicle_counts[1][vehicle])
				continue;
			snprintf(key, sizeof(key), "blue_%s", vehicles[vehicle]);
			kept &= server_gametype_rule_append(server_rule_find(key), options->vehicle_counts[1][vehicle], text, size,
				&length, count);
		}
	}
	return kept;
}

#else

boolean server_gametype_build(char const *gametype, struct server_rule const *rules, long rule_count,
	struct game_variant *variant, struct game_variant_options *options, char *reason, long reason_size)
{
	return FALSE;
}

void server_gametype_check_entry(struct server_entry *entry)
{
}

char const *server_gametype_builtin_name(long index)
{
	return NULL;
}

char const *server_gametype_engine_name(long game_engine_index)
{
	return "none";
}

boolean server_gametype_rules_text(struct game_variant const *base_variant,
	struct game_variant_options const *base_options, struct game_variant const *variant,
	struct game_variant_options const *options, char *text, long size, int *count)
{
	*count = 0;
	return FALSE;
}

#endif
