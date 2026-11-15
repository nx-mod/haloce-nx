/*
SERVER_GAMETYPES.H

A dedicated server's gametypes (server_gametypes.c): a rotation entry's
gametype (a built-in's name, an OpenCE gametype's, a gametype file's, a saved
gametype's) and its rules made into the game's variant and PC options.
*/

#ifndef __SERVER_GAMETYPES_H
#define __SERVER_GAMETYPES_H

#include "../src/server_rotation.h" /* port: port/linux/src/server_rotation.c */

struct game_variant;
struct game_variant_options;

/* the variant and options of the gametype, the rules over it: TRUE, else
FALSE and why */
boolean server_gametype_build(char const *gametype, struct server_rule const *rules, long rule_count,
	struct game_variant *variant, struct game_variant_options *options, char *reason, long reason_size);
/* the entry rejected if its gametype and rules cannot be built (or it is a
co-op entry: those need nothing more) */
void server_gametype_check_entry(struct server_entry *entry);
/* the built-in gametypes' names (game_engine_get_variant_by_name's and the
Xbox's other default gametypes'), and OpenCE's; NULL past the last */
char const *server_gametype_builtin_name(long index);
/* a gametype in the game's saved games (Edit Gametypes') named in a
rotation: saved=<its name, its UTF-8's other bytes as %XX>, made from its
name (FALSE if it does not fit), and the name read back from what follows
"saved=" (FALSE if it is no such name) */
boolean server_gametype_saved_reference(wchar_t const *name, char *text, long size);
boolean server_gametype_saved_name(char const *reference, wchar_t *name, long size);
/* the rules (+key=value...) that make the base variant and options the
other ones, into text, and how many (none: text empty); FALSE if one of
them cannot be written (no rule has it, or its value is past the rule's
range), or they do not fit */
boolean server_gametype_rules_text(struct game_variant const *base_variant,
	struct game_variant_options const *base_options, struct game_variant const *variant,
	struct game_variant_options const *options, char *text, long size, int *count);
/* the name of a game engine (game_engine_index): "ctf", "slayer", "oddball",
"king", "race" */
char const *server_gametype_engine_name(long game_engine_index);

#endif
