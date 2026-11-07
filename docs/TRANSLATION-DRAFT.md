# Translation: draft

Status: draft, open for decisions (see the end). Nothing here is built yet.

Goals:

1. Show everything in the menus in the chosen language, with strings
   translated by us.
2. Put the game itself in that language when the disc has that language's
   `maps_xx` folder.

Background: until commit `2d64fc28d` the PC menus took their text from a
Custom Edition `loc.map` in `custom_maps` (`display.menu_language = "maps"`),
so players with an English phone and an English disc saw Spanish menus when
their `loc.map` came from a Spanish Custom Edition. That path is gone. The
menus are always our own English text now, and this draft is how to translate
them properly.

## What we have today

### The game data

The original game code already handles languages:

- `cache_files_map_directory` (`source/cache/cache_files.c:612`) asks
  `XGetLanguage()` and loads the maps from `d:\maps_de\`, `d:\maps_fr\`,
  `d:\maps_es\` or `d:\maps_it\`. If that folder doesn't exist, it falls back
  to `d:\maps\`.
- The intro movies follow `XGetLanguage()` too (`source/interface/attract_mode.c:115`).
- Our `XGetLanguage()` (`port/linux/src/xbox_xapi.c:45`) returns
  `game.language` (`""` English, `ja`, `de`, `fr`, `es` or `it`).

What gets in the way is extraction. On a multi-language European disc, both
`port/android/.../XisoExtractor.java:156` and `port/linux/src/xiso.c:448`
(Switch and PS5) pick one `maps_xx` folder by the device's language and
extract it as plain `maps`. Afterwards the game can't tell which language it
has, and can't switch.

### Where the menus' text is

- `port/assets/menus/ce/strings.xml`: about 1,300 strings (lists shown by number).
- The `text=` and `strings=` attributes in the 36 other menu XMLs: about 2,200.
- Text drawn into pictures: the screens' titles and the main menu's items
  (`tools/title_assets.py`, Newtown font).
- The port's own C strings: notices, errors and dialogs in the game.
- Native UI: the Android launcher and touch controls, and the Switch's and
  PS5's host messages.
- The Xbox menus (`display.menus = "xbox"`) come from `ui.map`, so they are
  already in the language of the extracted maps.

## Design

### 1. One setting drives both

`game.language`: `"auto"` (the phone's or console's language), or `en`, `es`,
`fr`, `de` or `it`.

- The menus always follow it.
- The game data follows it when that language's maps are present; otherwise
  it stays English.
- It also shows up in the menus' Settings.

### 2. Our translations

- **One file per language, keyed by the English text**
  (e.g. `port/assets/menus/lang/es.po`, or a simpler equivalent), not by list
  and index. Then reordering or inserting strings in `strings.xml` doesn't
  break translations, and the files work with Weblate or Crowdin if the
  community helps. Where one English text needs two translations, a context
  (the list's name) tells them apart.
- **The lookup happens in one place:** `string_list_build` in
  `port/linux/game/menu_tags.c`. Every menu string already passes through it,
  whichever XML it came from. A missing translation falls back to English, so
  a partial translation still ships.
- **The files are embedded** with the other menu assets
  (`tools/embed_assets.py`).
- **Pictures with words:** generated per language at build time from the
  same files, with the existing title generator.
- **Port C strings:** go through the same lookup.
- **Android UI:** Android's own resources (`values-es/strings.xml`).
  Switch and PS5 host messages: the same files as the menus.

### 3. The game data in the chosen language

- The extractors stop flattening the disc and keep its `maps_xx` folder
  names, so the original `cache_files_map_directory` logic works as written.
- The port's places that hard-code `d:\maps` (the `ui.map` check in
  `port/linux/src/xbox_files.c:518`, the maps-folder checks, custom maps and
  so on) resolve through one function that returns the active folder.
- Two ways to get the folders onto the device:
  - **Extract every language on first start.** Switching is instant, but it
    costs disk space for each language.
  - **Extract only the chosen language, and re-extract when the setting
    changes.** The ISO stays on the device, so this is just a progress bar.
    This is the recommendation.
- Installs from before this change have a plain `maps` folder of unknown
  language. Treat it as the fallback (as the original code does), and
  re-extract when the player picks a language the folder doesn't match.

## Things to check first

- **Fonts.** Whether the English disc's `ui.map` fonts have glyphs for
  á é í ó ú ñ ü ¿ ¡ ç à è ß. If they don't, Spanish menus on an English disc
  show holes, and the PC menus need their own font tags (as they already add
  their own bitmaps). This can be read offline from a copy of `ui.map`; no
  phone needed. The title font
  (Newtown) needs the same check. This could change the scope, so do it
  before anything else.
- **Japanese.** `XGetLanguage` knows Japanese, but European discs have no
  `maps_ja`, and it needs a CJK font. Leave it out.
- **Disc layout.** Confirm how much of each `maps_xx` folder is the same
  across languages, which affects the disk cost of extracting them all.

## Decisions

1. **Languages:** Spanish only at first, or all five European languages?
2. **Default:** `"auto"` from the device's language, or English unless the
   player chooses?
3. **Translators:** us only, or open to the community (which decides whether
   the Weblate-friendly `.po` format matters)?
4. **Disc languages:** extract all of them up front, or re-extract on change
   (recommended)?
