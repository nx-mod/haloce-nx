# Custom campaigns

Plan, branch `custom_campaigns` (2026-11-14). Nothing here is built yet.

Today a custom campaign is played as CUSTOM SINGLEPLAYER maps in
`custom_maps`. That stays, but it cannot hold two campaigns that use the
same map names (a map's level name is `custom_maps\<name>`, and the copy at
the top of `custom_maps` wins over a subfolder's,
`port/linux/game/custom_edition_maps.c`), and it has no place for a
campaign's own `ui.map`. Custom campaigns are for singleplayer and local
split screen only.

## Goals

1. Xbox and Custom Edition campaigns booted from `custom_campaigns/`, one
   campaign per subfolder.
2. Whether a campaign is Xbox or Custom Edition found from its files.
3. Maps of the same name in different campaigns never interfere.
4. A campaign with a `ui.map` uses it as the menu.
5. A campaign without one is shown by its folder name, and booted from
   there.
6. One saved game (and progress) per campaign, saved and loaded as the
   game's own are (a moved memory window included).

## Test campaigns

| Campaign | Kind | Files | Level changes |
|---|---|---|---|
| Lumoria (TM Mapping Team) | CE (609) | `ui.map`, `lumoria_a`..`lumoria_d` | each level's script `map_name`s the next; the `ui.map`'s "Lumoria" button starts `lumoria_a` from its own script |
| Covenant Edition 2: Jackal's Journey | Xbox (5, packed) | `a10 a30 a50 b30 b40 c10 c20`, no `ui.map` | `game_won` in all but `c20` (the last): the stock level table picks the next |
| Covenant Edition: Longest Night | Xbox (5, packed) | `c10-longest-night-{grunt,hunter,jackal}.map`, each named `c10` in its header | one level, three versions |

What they show:

- A map's name is the one in its header (offset 0x20), not its file's:
  Longest Night's files are all `c10`. Its readme says to rename one to
  `c10.map`; here nobody renames anything.
- Jackal's Journey keeps stock names over other content (its `b30` is the
  Truth and Reconciliation interior), so the stock titles and pictures would
  be wrong for it.
- Level changes come two ways: `map_name <name>` from a script, or
  `game_won`, after which `main_won_map_private` (`source/main/main.c`)
  picks the next stock level from the 10-level table (a level not in it goes
  back to the main menu).

## Mounting a campaign

One campaign is mounted at a time. Its maps keep their own names (`a30`,
`lumoria_b`), so scripts' `map_name`, `game_won` and the stock level table
work unchanged. While a campaign is mounted:

- A map is looked for by its header name in the campaign's folder first
  (the folder is indexed when mounted: header name -> file), then:
  - an Xbox campaign: the stock `maps` (Xbox mods replaced files and kept
    the rest);
  - a Custom Edition campaign: the campaign's folder for `bitmaps.map`,
    `sounds.map` and `loc.map`, then where they are found today
    (`custom_maps`, the Custom Edition install).
- The Custom Edition loader is chosen by the map's header version (609), not
  by the `custom_maps\` level name prefix it keys on today
  (`custom_edition_level_name`). To check: how far that prefix reaches
  (`custom_edition_cache.c`, `cache_files_windows.c`, the tag loading).
- The saved game and progress are the campaign's (below).
- The map cache is emptied (below).

Unmounting (Back to Halo) puts the stock lookup, `ui.map`, saved game and
progress back, and empties the map cache again.

## Scanning `custom_campaigns`

Each subfolder is a campaign. Its `.map` files' headers are read:

- every map version 5: an Xbox campaign;
- every map version 609: a Custom Edition campaign (OpenSauce caches refused
  as in `custom_maps`, and its resource maps checked as they are there);
- anything else, or a mix: listed, greyed out, with the reason (mixed Xbox
  and Custom Edition maps, an MCC or OpenSauce map, a resource map missing).

Several maps of one header name in a folder (Longest Night) make one entry
each, named by the folder and the part of the file name that differs:
"Longest Night: grunt", "...: hunter", "...: jackal". The scan keeps each
map's kind by path, size and time, as `custom_edition_maps.c`'s
identity_cache does, so a reopened list reads only what changed.

A campaign may have `<folder>.bmp` and `<folder>.txt` beside its maps (or
inside the folder) for its row's picture and description, and
`<map>.bmp`/`<map>.txt` for a level's, as `custom_maps` has.

## The menus

Campaign -> Custom Campaigns:

- No campaigns: a screen saying to put each campaign in its own folder in
  `custom_campaigns` (with the folder's place on this platform).
- Otherwise the list: folder name (or entry name), picture, description,
  and the reason for a campaign that cannot run.

Booting a campaign:

- **With a `ui.map`**: the campaign is mounted and its `ui.map` loaded as the
  shell. Its main menu is kept as it is (logo, background, music, its own
  buttons and their scripts), with one button of ours added: **Back to
  Halo**, which is essential. Our settings and pause menu stay ours. To
  check: how `menu_tags.c` builds our XML menus into a `ui.map`'s widget tags
  (it must not cover the campaign's `main_menu`), and which PC event handler
  and game data functions such menus use that the port does not answer.
- **Without one**: the campaign is mounted and the stock `ui.map` stays. A
  level screen of ours lists the levels the folder has, in stock order for
  stock names, then levels no other level's script `map_name`s, then the rest;
  unlocked by the campaign's own progress; titled by the map's own name or
  its `<map>.txt`. A campaign of one level goes straight to the difficulty.
  This screen has Back to Halo too.

Touch: the menus' tap targets are noted from the widgets as they are drawn
(`ui_mouse_note_target`, `source/interface/ui_widget.c`), whatever map they
came from, so a campaign `ui.map`'s items should be tappable as they are. To
check on Lumoria with `debug.touch_targets`: its buttons hit, a screen that
can only be left with B is left with Android's back, and the 96-target
limit holds. Add only what is found missing.

## Saved games and progress

- `savegame.bin` for a campaign goes in
  `<profile>/campaigns/<id>/savegame.bin`: the path from
  `game_state_get_persistent_storage_path` (`source/saved
  games/game_state_xbox.c`) while a campaign is mounted. The same code saves
  and loads it, so it follows the game's own rules (a moved memory window).
- The campaign's progress (levels reached, difficulties beaten) is kept
  beside it, so a campaign's `a30` never counts as the stock one's.
- `<id>`: the folder name (plus the entry's suffix for a folder of several
  versions). Renaming the folder loses the save; updating the campaign
  does not. Continue checks that the saved map is still the campaign's
  (its header's checksum) before offering it.

## The map cache: in memory everywhere

Xbox maps are packed. The game unpacks each into a cache file,
`z:\cacheNNN.map` (`cache_files_precache_map_begin`,
`source/cache/cache_files_windows.c`), and finds one again by the map name
in its header. Custom Edition and MCC maps are read in place.

- **Every platform keeps the cache in memory**, as the Switch does
  (`port/switch/host/host_memory_files.c`: slots of 1 MB pieces, answered
  from host memory through `host_syscall.c`). Android's host
  (`port/android/host/host_syscall.c`) and the PS5's
  (`port/ps5/host/host_syscall.c`) get the same: the file moves where all
  three hosts can build it, and their `host_syscall.c`s send the cache
  files' calls to it as the Switch's does.
- **A mounted campaign empties the cache**: mounting and unmounting give
  back every slot (truncated, as `platform_file_discard` does) and forget
  the game's records of them, so the next map is unpacked from the
  campaign's file. Without this, a stock `a10` played before would be
  played for Jackal's Journey's `a10` (the "clear your cache" of the
  campaigns' readmes).
- **First start deletes any cache folder on disk**: the cache files a build
  that kept them there left (`<save root>/z/cache*.map`, some 730 MB), once,
  at start. The Switch does it a slot at a time when a slot is first named
  (`forget_card_file`).
- To watch: the memory held. A slot holds about its map's size, and campaign
  levels are 100-250 MB unpacked; several slots held at once on a phone
  with little memory could get the app killed. If so, the host gives back
  the least recently used slot that no file has open when it is short.

## Findings (2026-11-14)

### The `custom_maps\` prefix

Every place that tells a Custom Edition map from the game's own asks
`custom_edition_level_name` (`port/linux/game/custom_edition_cache.c`):

- `source/cache/cache_files.c`: the build check (1171), the network
  version (1256), whether a map is present (1292), and the tag loading,
  which sends the map to `custom_edition_cache_tags_load` (1443);
- `source/cache/cache_files_windows.c`: read in place, not copied (593),
  and refused when missing (623);
- `source/main/main.c` 1300: never one of the campaign's levels;
- `source/networking/network_client_manager.c` 3203 (network only).

The file is found by `custom_edition_map_named`, over `maps_folder()`'s
two folders (`custom_maps`, the install) and one level of subfolders, by
file name. So a mounted Custom Edition campaign needs:

- `custom_edition_level_name` TRUE for a name the campaign's index has
  (its header version 609), `ui` included;
- `maps_folder()` with the campaign's folder first, looked up by header
  name through the index rather than by file name;
- `main_get_solo_level_from_name` not refusing a mounted campaign's names,
  so a Custom Edition campaign of stock names that ends its levels with
  `game_won` goes on to the next.

A script's `map_name` passes its name unchanged (`HS_EVALUATE_VOID_STRING`,
`source/hs/hs.c` 15230): Lumoria's `lumoria_a` asks for `lumoria_b`, with
no prefix, so the loader looks for `d:\maps\lumoria_b.map`. As far as the
code reads, a Custom Edition campaign that changes levels by script does not
go on to its next level from `custom_maps` today; mounted, it does.

### Xbox map paths

Three places build `d:\maps\<name>.map` from `cache_files_map_directory()`:
`cache_file_get_map_path` (`cache_files_windows.c` 1286, which the copy
into the cache and `cache_file_read_header_from_dvd` use) and
`cache_files.c` 1181 and 1307. One function giving a map's path (the
campaign's file for the name, else the stock one) replaces them. `ui.map`
is found the same way (`levels\ui\ui`, `main.c` 1932).

### A campaign's `ui.map` and our menus

- Our menus are added to the loaded map's tag table as tags of their own,
  named `pc\...` (`menu_tags.c`), never replacing the map's; the main
  menu opened is `pc_menus_root_name()`'s (`ui_widget.c` 5227). For a
  campaign with a `ui.map` it names the map's own main menu instead:
  Lumoria's `ui.map` has `ui\shell\main_menu\main_menu`.
- Back to Halo is added to that screen as `pause_patch` and
  `pause_campaign_patch` add SETTINGS and END GAME to the pause screens.
- Widget event handlers' scenario scripts run (`hs_evaluate_by_name`,
  `ui_widget.c` 3492), so a campaign menu's buttons that run its `ui.map`'s
  scripts (Lumoria's `map_name lumoria_a`) work.
- Our menus name `ui.map`'s tags by path (fonts, sounds, pictures). They
  were made from Custom Edition's `ui.map` (`tools/ce_menus.py`), and
  Lumoria's is one with its own screens added, so they should resolve; any
  that does not is logged and left out, not fatal.

Risks:

- A Custom Edition `ui.map` (scenario type 2) has never been loaded through
  the Custom Edition loader; `menu_tags_loaded` keys on the name `ui`, which
  that path passes too.
- The Custom Edition menus run the PC's event handler functions, most of
  which do nothing in the port yet (`menu_tags.c`). Lumoria's own New Game,
  Continue and difficulty screens are the PC's: those functions must start
  the mounted campaign's level and load its saved game.

### Lumoria's `ui.map`, read offline

Its widgets, walked from `ui\shell\main_menu\main_menu` (659 reachable),
with Invader's names for the PC's functions:

- The campaign does not use the stock New Game. Campaign opens
  `campaign_select\campaign_menu`, whose items are Lumoria's own:
  `lumoria_campaign\new_game_lumoria` runs the `ui.map` script
  `lumoria_a`, and LOAD opens `lumoria_campaign_level_list`, whose items run
  `lumoria_a` to `lumoria_d`. The main menu's own open runs `ring_loop`.
  So Lumoria's campaign starts from scripts alone (scenario script handlers
  run, `ui_widget.c` 3492), with no PC function. It has no difficulty choice
  (its readme says to use the console's `game_difficulty_set`): ours is to be
  offered around it.
- The PC-only functions it uses are mostly the PC's mouse helpers (108
  `mouse emit accept event` on 152 widgets, 151 `mouse spinner 1wide
  click` on 87, 160 `single prev cl item activated`), 169 `difficulty item
  select`, and its settings, profile and multiplayer screens' (gamespy,
  controls, video, audio, network: many not answered by the port).
- The Custom Edition loader clears every handler's PC function (number 102
  and up) as the map loads (`widget_pc_functions_clear`,
  `port/linux/game/cache_file_formats.c`): this build answers none by the
  PC's numbers. For a campaign's `ui.map` they are to be renumbered to the
  port's own instead (`PC_MENU_FUNCTION_BASE` plus the name's place in
  `port_function_names`, `menu_tags.c`, which has most of the PC's names),
  and the game data inputs from 41 the same way.
- Its main menu also has Multiplayer, Profiles, Settings, Credits and Quit,
  which open the PC's screens. Proposed: Campaign and Credits stay the
  campaign's, Settings and Profiles open ours, Multiplayer is hidden
  (single player only), Quit becomes Back to Halo.

## Done

- The map cache in memory on every host (2026-11-14):
  `port/android/host/host_memory_files.c` (moved from the Switch's host, which
  builds it from there), Android's and the PS5's `host_syscall.c` sending a
  call on a cache file's descriptor or path to it first, and the five
  posix.h helpers the guest imports for such files going through it
  (`tools/android_imports.py`, `host_memory_posix_*`). The cache files a
  build before left on the storage are deleted the first time a run names
  one. `platform_file_discard` empties a slot before its next map on every
  platform. Tested in `port/android/tests/host_syscall_test.c`.
- `cache_files_forget_cached_maps` (`source/cache/cache_files_windows.c`),
  for mounting and unmounting: every cache file's map forgotten, emptied at
  once unless a map is open from it or being copied into it, which are
  forgotten (stale) until a map is copied into them again.

- Mounting (`port/linux/game/custom_campaigns.c`, 2026-11-14): a folder of
  `custom_campaigns` indexed by its maps' header names, Xbox or Custom
  Edition (a mix refused); its maps found first by `cache_file_get_map_path`
  and `cache_files.c` (Xbox) or `custom_edition_map_named`,
  `custom_edition_level_name` and the resource maps' lookup (Custom
  Edition); the saved game in the profile's `campaigns\<folder>\`
  (`game_state_get_persistent_storage_path`, and the check ahead); the map
  cache emptied on mount and unmount. Until the menus list the campaigns,
  `debug.custom_campaign = "<folder>"` mounts one as the menus first load.
- A campaign's `ui.map`: its own main menu is the menus' root, with BACK TO
  HALO (`custom_campaign/y_butn_back_to_halo`, `tools/port_settings.py`'s
  `_custom_campaign`) added at its bottom left and a Y handler running
  `port custom campaign back`, which unmounts and loads the stock menus
  (`menu_tags.c`'s `custom_campaign_patch`). Its widgets' Halo PC functions
  and game data inputs are given the port's numbers of their names instead
  of being cleared (`custom_edition_widget_renumber_set`,
  `cache_file_formats.c`; the PC's names from Invader in `menu_tags.c`).

## Still to check

- Loading Lumoria's `ui.map` on a device: that the Custom Edition loader
  takes a `ui.map` (scenario type 2), and how it draws.
- That packed Xbox maps from outside `d:\maps` go through the copy as the
  stock ones do (the path is built by `cache_file_get_map_path`).
- The memory of in-memory slots on Android phones and the PS5.
