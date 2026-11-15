/*
SERVER_GAMETYPE_RULES.C (test)

A round's own options as rotation rules (port/linux/game/server_gametypes.c's
server_gametype_rules_text, Host Server's Round Options): what differs from
the gametype written as +key=value, read back by server_rotation.c, applied
as server_gametype_build applies them (the rules, the teams' vehicles
settled, blue's own last), and compared with the options edited: nothing
may differ. test_server_gametype_rules.py takes the code.
*/

#include "harness.h"

#include <ctype.h>

typedef unsigned short word;
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define TICKS_PER_SECOND 30
#define SERVER_ROTATION_NO_FILES

#include "enums.inc"
#include "structures.inc"
#include "rotation.inc"
#include "under_test.inc"

/* a Slayer of the Xbox's kind: the gametype a round's options start from */
static void slayer(struct game_variant *variant, struct game_variant_options *options)
{
	memset(variant, 0, sizeof(*variant));
	memset(options, 0, sizeof(*options));
	variant->game_engine_index = game_engine_slayer;
	variant->universal_variant.flags = FLAG(_game_variant_draw_object_in_motion_sensor_bit) |
		FLAG(_game_variant_allow_friendly_navpoints_bit);
	variant->universal_variant.health = 1.0f;
	variant->universal_variant.respawn_time = 5 * TICKS_PER_SECOND;
	variant->universal_variant.score_to_win = 25;
	variant->universal_variant.vehicle_set = 0;
	options->vehicle_respawn_time = 30;
	options->friendly_fire = 1;
}

static char rules[SERVER_ENTRY_TEXT_SIZE];
static int rule_count;
static struct server_entry entry;

/* the rules for the edited options, read back as a rotation entry on
bloodgulch, and applied over the gametype: TRUE if they were written (and
read and applied), the options they give in *rebuilt */
static boolean written(struct game_variant const *base_variant, struct game_variant_options const *base_options,
	struct game_variant const *variant, struct game_variant_options const *options,
	struct game_variant *rebuilt_variant, struct game_variant_options *rebuilt_options)
{
	char text[SERVER_ENTRY_TEXT_SIZE + 32];
	char reason[SERVER_REASON_SIZE];
	boolean team_vehicles_given = FALSE, vehicle_counts_given = FALSE;
	long vehicle_set_before;
	int length;

	if (!server_gametype_rules_text(base_variant, base_options, variant, options, rules, sizeof(rules), &rule_count))
		return FALSE;
	length = snprintf(text, sizeof(text), "bloodgulch:slayer%s", rules);
	CHECK(server_rotation_parse_entry(text, length, &entry) && !entry.rejected[0], "%s was rejected: %s", text,
		entry.rejected);
	*rebuilt_variant = *base_variant;
	*rebuilt_options = *base_options;
	vehicle_set_before = rebuilt_variant->universal_variant.vehicle_set;
	CHECK(server_gametype_apply_rules(entry.rules, entry.rule_count, rebuilt_variant, rebuilt_options,
		&team_vehicles_given, &vehicle_counts_given, reason, sizeof(reason)), "%s: %s", text, reason);
	server_gametype_vehicles_settle(rebuilt_variant, rebuilt_options, vehicle_set_before, team_vehicles_given,
		vehicle_counts_given);
	server_gametype_apply_blue(entry.rules, entry.rule_count, rebuilt_variant, rebuilt_options);
	return TRUE;
}

/* the edited options written, read back and applied: none differ */
static void round_trip(struct game_variant const *base_variant, struct game_variant_options const *base_options,
	struct game_variant const *variant, struct game_variant_options const *options)
{
	struct game_variant rebuilt_variant;
	struct game_variant_options rebuilt_options;
	char left[SERVER_ENTRY_TEXT_SIZE];
	int left_count;

	CHECK(written(base_variant, base_options, variant, options, &rebuilt_variant, &rebuilt_options),
		"the rules were not written");
	CHECK(server_gametype_rules_text(&rebuilt_variant, &rebuilt_options, variant, options, left, sizeof(left),
		&left_count), "the rules read back differ unwritably");
	CHECK(!left_count, "rules %s read back still differ by %s", rules, left);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct game_variant base_variant, variant;
	struct game_variant_options base_options, options;

	slayer(&base_variant, &base_options);
	variant = base_variant;
	options = base_options;

	/* the gametype as it is: no rules */
	CASE("unchanged")
	{
		CHECK(server_gametype_rules_text(&base_variant, &base_options, &variant, &options, rules, sizeof(rules),
			&rule_count), "no rules could not be written");
		CHECK(!rule_count && !rules[0], "%d rules: %s", rule_count, rules);
		return 0;
	}
	/* the player, item and indicator options of the variant's own */
	CASE("universal")
	{
		variant.universal_variant.lives = 3;
		variant.universal_variant.health = 4.0f;
		variant.universal_variant.respawn_time = 15 * TICKS_PER_SECOND;
		variant.universal_variant.respawn_time_growth = 10 * TICKS_PER_SECOND;
		variant.universal_variant.suicide_penalty = 5 * TICKS_PER_SECOND;
		variant.universal_variant.odd_man_out = TRUE;
		variant.universal_variant.weapon_set = 6;
		variant.universal_variant.goal_radar = 2;
		variant.universal_variant.flags = FLAG(_game_variant_no_shields_bit) | FLAG(_game_variant_always_invisible_bit) |
			FLAG(_game_variant_infinite_grenades_bit) | FLAG(_game_variant_vehicle_riding_bit) |
			FLAG(_game_variant_generic_starting_equipment_bit);
		round_trip(&base_variant, &base_options, &variant, &options);
		CHECK(strstr(rules, "+respawn=15"), "the respawn in seconds: %s", rules);
		CHECK(strstr(rules, "+health=400"), "the health in percent: %s", rules);
		CHECK(strstr(rules, "+weapons=rocket_launchers"), "the weapon set by its word: %s", rules);
		return 0;
	}
	/* the options' own: teamplay's, the loadout, the radar's players */
	CASE("options")
	{
		options.friendly_fire = 3;
		options.friendly_fire_penalty = 10;
		options.auto_team_balance = TRUE;
		options.force_team_spawns = TRUE;
		options.loadout = 1;
		options.primary_weapon = _loadout_weapon_shotgun;
		options.secondary_weapon = _loadout_weapon_needler;
		options.no_map_weapons = TRUE;
		options.radar_players = 2;
		options.vehicle_respawn_time = 120;
		variant.universal_variant.flags &= ~FLAG(_game_variant_draw_object_in_motion_sensor_bit);
		round_trip(&base_variant, &base_options, &variant, &options);
		CHECK(strstr(rules, "+force_team_spawns=on"), "force_team_spawns: %s", rules);
		return 0;
	}
	/* red's and blue's vehicles each their own: a set, and custom counts */
	CASE("vehicles-sides")
	{
		options.vehicle_set[0] = 5;
		options.vehicle_set[1] = VARIANT_VEHICLE_SET_CUSTOM;
		options.vehicle_counts[1][_variant_vehicle_ghost] = 2;
		options.vehicle_counts[1][_variant_vehicle_scorpion] = 1;
		round_trip(&base_variant, &base_options, &variant, &options);
		CHECK(strstr(rules, "+team_vehicles=rocket_warthogs"), "red's set: %s", rules);
		CHECK(strstr(rules, "+blue_team_vehicles=custom") && strstr(rules, "+blue_ghosts=2"), "blue's: %s", rules);
		return 0;
	}
	/* both teams' the same custom counts: no blue_ rule */
	CASE("vehicles-same")
	{
		options.vehicle_set[0] = options.vehicle_set[1] = VARIANT_VEHICLE_SET_CUSTOM;
		options.vehicle_counts[0][_variant_vehicle_banshee] = options.vehicle_counts[1][_variant_vehicle_banshee] = 3;
		round_trip(&base_variant, &base_options, &variant, &options);
		CHECK(!strstr(rules, "blue_"), "blue's own for the same vehicles: %s", rules);
		return 0;
	}
	/* each vehicle set by its name, and PC's */
	CASE("vehicles-presets")
	{
		byte set;

		for (set = 0; set < 8; set++)
		{
			options = base_options;
			options.vehicle_set[0] = set;
			options.vehicle_set[1] = (byte)(7 - set);
			round_trip(&base_variant, &base_options, &variant, &options);
		}
		options = base_options;
		options.vehicle_set[0] = options.vehicle_set[1] = VARIANT_VEHICLE_SET_PC;
		round_trip(&base_variant, &base_options, &variant, &options);
		return 0;
	}
	/* the Xbox's vehicle set, which the teams' follow */
	CASE("vehicle-set")
	{
		variant.universal_variant.vehicle_set = 3;
		options.vehicle_set[0] = options.vehicle_set[1] = 3;
		round_trip(&base_variant, &base_options, &variant, &options);
		return 0;
	}
	/* a value past its rule's range cannot be kept */
	CASE("refused")
	{
		struct game_variant rebuilt_variant;
		struct game_variant_options rebuilt_options;

		variant.universal_variant.health = 4.5f;
		CHECK(!written(&base_variant, &base_options, &variant, &options, &rebuilt_variant, &rebuilt_options),
			"health of 450%% was written: %s", rules);
		return 0;
	}
	/* every rule written is one the rotation reads */
	CASE("parsed")
	{
		struct game_variant rebuilt_variant;
		struct game_variant_options rebuilt_options;

		variant.universal_variant.lives = 5;
		options.time_limit = 20;
		options.vehicle_set[0] = VARIANT_VEHICLE_SET_CUSTOM;
		options.vehicle_counts[0][_variant_vehicle_warthog] = 4;
		options.vehicle_set[1] = 1;
		CHECK(written(&base_variant, &base_options, &variant, &options, &rebuilt_variant, &rebuilt_options),
			"not written");
		CHECK(entry.rule_count == rule_count, "%d rules written, %d read: %s", rule_count, entry.rule_count, rules);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
