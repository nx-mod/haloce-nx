# Controller button art

The controller buttons the port draws are high-resolution textures drawn in
place of the game's own 32x32 bitmaps. This folder holds their sources, the
PNGs made from them, and `buttons.json`, which says what each PNG stands for.
This guide covers changing them, adding a design, and giving a port
(the Switch, say) designs of its own.

## What is drawn where

| Family | Seen | PNGs | Stands for | Drawn by |
| --- | --- | --- | --- | --- |
| Menu keys | Beside the menus' key labels (A SELECT, B BACK), and smaller in dialogs, the pause menu and the help screens | `a_butn__0` … `y_butn__0`, `a_butn_sm__0`, `b_butn_sm__0`, `black_butn__0`, `white_butn__0` | The map's `ui\shell\bitmaps\*_butn` bitmaps | The texture cache, in place of the bitmap (`port/linux/src/hud_hires.c`) |
| Message icons | Inside text: "Press (A) to join" | `hud_msg_icons_sm__0`: white stencils of each oval with its letter cut out, which the game tints | The A, B, X and Y sprites of the sheet `ui\hud\bitmaps\hud_msg_icons_sm` | A placeholder for those sprites (`port/linux/game/hud_hires_tags.c`) |
| Gameplay prompts | The HUD's button prompts in a game, with `display.high_res_hud` on | `classic_*__0`, `gem_*__0`, and for Dark Gems the menu keys above | Bitmaps of the port's own, `ui\hud\buttons\*` (no map has them) | `hud_hires_button_draw` (`hud_hires_tags.c`), in the style `display.hud_button_style` names |
| Sticks and triggers | Gameplay prompts for them, in every style | `trigger_*__0`, `thumbstick*_*__0` | `ui\hud\buttons\*` | The same |
| Replay prompts | A replay's button bar and pause menu | `prompt_*__0`: Kenney's Xbox Series pictures (`kenney/`), every button the bar names, the d-pad, Back and Start too | `ui\hud\buttons\prompt_*` | `hud_hires_prompt_draw` (`hud_hires_tags.c`), from `port/linux/game/network_replay_playback.c` |

The menu keys and message icons count as the menus' art: `display.high_res_text`
turns them on and off. The gameplay prompts are the HUD's: `display.high_res_hud`.
A replay's prompts are drawn whatever those say: the game has no pictures of
its own of the d-pad, Back or Start to fall back to (its message icons'
d-pad is the A's), so without them the bar writes the buttons' names. A
replay swaps the Switch's A and B as the menus do, so its A and B prompts
come twice there, as the menus' do.

To render the prompts alone, after adding one to `PROMPT_ART` and
`PLATFORM_ART` in `tools/button_assets.py` (and its name to
`hud_prompt_names` in `hud_hires_tags.c`):

```
python tools/button_assets.py build --only prompt_a__0 ...
python tools/button_assets.py platform --platform switch --only prompt_a__0 ...
python tools/button_assets.py platform --platform ps5 --only prompt_a__0 ...
```

## The pipeline

```
svg/*.svg, source/*.png ──tools/button_assets.py build──▶ *.png (committed)
                                                        buttons.json (committed)
          *.png + buttons.json ──tools/embed_assets.py──▶ hud_hires_assets.c (each build)
                                                        hud_hires_embedded[] in the game
```

- `svg/` has hand-drawn redraws of the Xbox buttons, one per letter, in the
  texels of a 32x32 icon (`viewBox="0 0 32 32"`). Each has a shape with
  `id="oval"` and one with `id="letter"`, and Y also has `id="shadow"`. The
  message icons' stencils are cut from the oval and letter, and the classic
  style fills them with one colour.
- `source/` has art supplied as PNGs: the gems, triggers and sticks. They
  are scaled into the bitmap's cell, keeping their proportions.
- `tools/button_assets.py` has three commands:
  - `layout --map ui.map` reads each map bitmap's size, format and pixel
    CRC, fits each redraw on it, and writes `buttons.json`.
  - `build` renders the PNGs from `buttons.json`.
  - `check --map ui.map --out DIR` writes each PNG, reduced to its bitmap's
    size, beside the map's bitmap.

  It needs `rsvg-convert`, Pillow, NumPy and SciPy.
- `tools/embed_assets.py` turns every PNG listed in `buttons.json` (and the
  HUD's and titles' lists) into C data at build time. Every port's build
  calls it (`hud_assets_build`), so the PNGs are the only thing committed.

### `buttons.json`

One entry per PNG:

| Key | Meaning |
| --- | --- |
| `name` | The PNG, without `.png` |
| `tag`, `bitmap` | The bitmap it stands for: a map's tag, or `ui\hud\buttons\…` for the port's own |
| `width`, `height`, `format` | That bitmap's size and format |
| `scale` | The PNG is this many times the bitmap's size: 8 |
| `crc` | The CRC-32 of the map bitmap's pixels. The game draws the PNG only over a bitmap with exactly these pixels: another language's or a modified map's are drawn as they are (and logged). `0` for the port's own bitmaps |
| `svg` / `source_png` | What it is rendered from |
| `place`, `letter` | Where `layout` put the redraw on the bitmap, and where it moved the letter to sit as the map's does |
| `classic` | The classic style's fill colour |
| `sprites` | For the message icon sheet: each sprite's sequence and place |
| `control_cell` | For triggers and sticks: the cell the art is fitted into |

## Change an existing button's look

1. Edit its SVG in `svg/`, or replace its PNG in `source/`. Keep:
   - the 32x32 viewBox, drawing where the old one did (the menus draw the
     bitmap from its top left, one texel to a pixel, so art elsewhere is cut
     off or misplaced);
   - the `oval` and `letter` ids (and `shadow` on Y), which the stencils and
     the classic style are cut from.
2. `python tools/button_assets.py build`.
3. `python tools/button_assets.py check --map <data>/maps/ui.map --out /tmp/buttons`,
   and look at the pairs: the new art should cover what the old did.
4. Build a port and look at each place in "What is drawn where": a menu
   (A SELECT), the pause menu (the small A and B), a "Press A" message, and
   the HUD prompts in each `display.hud_button_style`.
5. Commit the SVG or source and the PNGs together.

Run `layout` again only for a new map bitmap or a new kind of entry: it
rewrites `buttons.json` from the map.

## Add a gameplay button style

The gameplay prompts are the one family the player chooses between.

1. Add the art: a list like `GEM_BUTTONS` in `tools/button_assets.py`, with
   tags `ui\hud\buttons\<style>_a` … (the port's own, `crc` 0), and
   `build`. Black and White can reuse an existing style's, as gems do.
2. Name the tags in `hud_hires_button_draw` (`port/linux/game/hud_hires_tags.c`):
   a `<style>_names[]` beside `classic_names[]` and `gem_names[]`, and a
   number for the style where `display.hud_button_style` is read.
3. Add the value to `display.hud_button_style` (its help text in
   `port/linux/src/port_config.c`, its row in `port/linux/README.md`) and to
   the BUTTON STYLE spinner in `tools/port_settings.py`. Then regenerate the
   Graphics screen (`settings_files()`) into `port/assets/menus/ce/`; the
   test `tools/test_web_build.py` checks it matches.

## Art for one port only

A port's art is a folder of overrides beside the common art. The Switch and
the PS5 have one each, drawn from [Kenney](https://kenney.nl/)'s Input
Prompts (CC0, `platforms/Kenney-LICENSE.txt`, which their releases carry):

```
port/assets/buttons/
    a_butn__0.png             common PNGs (Android and the desktop builds)
    kenney/*.svg              Kenney's Xbox Series pictures, a replay's prompts' in common
    platforms/Kenney-LICENSE.txt
    platforms/switch/
        kenney/*.svg          Kenney's pictures the port uses
        a_butn__0.png         the port's PNGs, the same names
        a_butn__0.menus.png   (a second picture: see "The Switch" below)
        buttons.json          each PNG, the entry it stands for and its contexts
    platforms/ps5/            the same
```

- **Same entries, other pixels.** A port's PNG stands for the same entry in
  `buttons.json`, with the same tag, size and CRC. Only the pixels differ,
  so the game's code is the same for every port. A name the port has no PNG
  for uses the common one.
- **Rendering.** `tools/button_assets.py platform --platform switch` draws
  each entry's Kenney picture (`PLATFORM_ART` in the script) in the box the
  common PNG's art takes, or each sprite's for the message icons, and
  writes the PNGs and `platforms/switch/buttons.json`.
- **By place.** The game's buttons are the Xbox's by where they sit (SDL's
  south, east, west and north: `xinput_sdl.c`), so each port draws the
  button in that place: the PS5's cross for A, circle for B, square for X,
  triangle for Y, R1 and L1 for Black and White, L2 and R2 for the
  triggers; the Switch's B (the bottom button) for A, A for B, Y for X, X for
  Y, R and L, ZL and ZR.
- **Embedding.** `hud_assets_build` passes a port that has a folder to
  `embed_assets.py --platform <port>`, which embeds its PNGs beside the
  common ones, each with its contexts (`hud_hires.h`: where its buttons are
  right, and whose art it is). The game draws the port's, or with
  `display.xbox_buttons = true` the common Xbox ones (from the next map).

### The Switch

- **The menus' A and B.** The Switch's own menus choose with the right
  button (marked A) and go back with the bottom one (marked B), and so do
  the game's menus while `controls.nintendo_menu_buttons` is on (the
  default; `xinput_sdl.c` swaps the two while a menu is up). A game's
  controls keep the buttons by place. So the pictures the menus draw for A
  and B (`a_butn`, `b_butn`, their `_sm`, and the message icons in the main
  menu's text) come twice: `*.menus.png` by letter, for the menus with the
  swap on (context 1), and the others by place, for the menus without it
  and for a game (contexts 2 and 4). The game takes the one that is right
  as each map loads (`hud_hires_tags.c`), so a change of the setting shows
  in the pictures at the next map.
- **Black and White** have no Switch button: they are R and L, where the
  port puts them (the shoulders).
