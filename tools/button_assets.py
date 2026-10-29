#!/usr/bin/env python3
"""Makes high-res textures of the menus' controller button icons
(port/assets/buttons/*.png) from hand-drawn SVG redraws of them
(port/assets/buttons/svg):

    python tools/button_assets.py layout --map assets/maps/ui.map
    python tools/button_assets.py build
    python tools/button_assets.py check --map assets/maps/ui.map --out /tmp/button_check

The icons beside the menus' key labels (A SELECT, B BACK, X DELETE, Y CREATE
NEW) are 32x32 bitmaps of ui.map with their art in the top left corner,
which the widgets' backgrounds draw at one texel to the pixel
(draw_bitmap_in_rect crops a bitmap larger than its widget, in coordinates
of the bitmap's size); the dialogs, the pause menu and the help screens draw
smaller A and B icons, bitmaps of their own in every map. So a texture of 8x
a bitmap's size, in its layout, draws in its place unchanged, as the
high-res HUD's do (port/linux/src/hud_hires.c), and with the high-res text
(display.high_res_text), as the menus' titles do.

Each redraw is drawn in the texels (viewBox 0 0 32 32) of the large icon it
is named after, over the map's art: a tilted oval with a pale rim, dark at
the top and bright at the bottom, and its letter in white (Y's casting a
dark shadow). A small icon is the same art smaller.

The menus' text draws the same buttons as message icons ("Press %a-button
to Join": draw_string_and_hack_in_icons, ui_widget.c), sprites of a sheet
of every map, hud_msg_icons_sm, that the game tints: stencils, each oval in
white with its letter cut out. They are drawn from the redraws' oval and
letter (their ids "oval" and "letter"), into a texture of the sheet's
layout that has only those sprites: the sheet's other icons (triggers and
sticks, with English words) keep the map's, so the texture stands for the
sprites alone, and the game draws them from it through a placeholder
(port/linux/game/hud_hires_tags.c).

layout: reads each icon's bitmap from the map (its size, format and the CRC
    of its pixels, which the game checks before drawing the texture in its
    place) and places its redraw on it: a small icon's scaled and moved as
    the map's small art is from its large art (so that their shapes' area
    and centre are the same), and a message icon's stencil where it covers
    the sprite's best. Then each icon's letter (id "letter", with Y's
    shadow, id "shadow") is moved on its oval to where the map's letter is
    on that icon: the map's letters do not sit alike on every icon, so one
    redraw's letter does not fit them all. Writes
    port/assets/buttons/buttons.json.
build: renders the redraws as buttons.json places them into
    port/assets/buttons/*.png (committed; the builds embed them:
    tools/embed_assets.py).
check: compares each PNG, reduced to its bitmap's size, with the map's
    bitmap and writes side-by-side images into --out.
platform --platform switch|ps5: renders the port's own art (Kenney's Input
    Prompts) in place of each PNG, into port/assets/buttons/platforms/<port>
    with its list, buttons.json (embed_assets.py --platform embeds them).

build and platform take --only NAME..., to render those PNGs alone (the
others, and their lists' entries, kept as they are).

A replay's button bar (port/linux/game/network_replay_playback.c) draws
every button it names as a picture of its own, the prompts (prompt_*__0,
bitmaps of the port's own, ui\\hud\\buttons\\prompt_*): Kenney's Xbox Series
pictures (port/assets/buttons/kenney) in common, the port's own on a port.

Needs rsvg-convert, Pillow, NumPy and SciPy.
"""

import argparse
import json
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hud_assets import (FORMATS, XboxMap, bleed, decode_bitmap, level0_size, overlap,  # noqa: E402
                        pixel_rectangle, render_svg)

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "port/assets/buttons"
LIST = ASSETS / "buttons.json"
SCALE = 8

BITMAPS = "ui\\shell\\bitmaps\\"
# each icon's bitmap and its redraw (drawn over the bitmap it is named
# after): the button keys' (ui.map), and the smaller ones of the dialogs,
# the pause menu and the help screens (every map)
BUTTONS = {
    BITMAPS + "a_butn": "a_butn.svg",
    BITMAPS + "b_butn": "b_butn.svg",
    BITMAPS + "x_butn": "x_butn.svg",
    BITMAPS + "y_butn": "y_butn.svg",
    BITMAPS + "a_butn_sm": "a_butn.svg",
    BITMAPS + "b_butn_sm": "b_butn.svg",
}
# the message icons' sheet, and the redraw of each of its buttons, by the
# sequence of its sprite (on the sheet's first bitmap)
MESSAGE_ICONS = "ui\\hud\\bitmaps\\hud_msg_icons_sm"
MESSAGE_BUTTONS = {0: "a_butn.svg", 1: "b_butn.svg", 2: "x_butn.svg", 3: "y_butn.svg"}

# Standalone Xbox faces for gameplay prompts, using the existing redraws.
HUD_BUTTONS = [dict(name=f"{button}_butn__0", tag=BITMAPS + f"{button}_butn",
                   bitmap=0, width=32, height=32, format="dxt3", scale=8,
                   crc=0, svg=f"{button}_butn.svg", place=[1, 0, 0])
               for button in ("black", "white")]

# Plain gameplay faces use the same hand-drawn oval and letter as the
# message stencils, with solid colour and no rim or shadow.
CLASSIC_BUTTONS = [dict(name=f"classic_{button}__0", tag=f"ui\\hud\\buttons\\classic_{button}",
                       bitmap=0, width=32, height=32, format="dxt3", scale=8,
                       crc=0, svg=f"{button}_butn.svg", place=[1, 0, 0], classic=color)
                   for button, color in (("a", "#40eb40"), ("b", "#fa5946"),
                                         ("x", "#3998fa"), ("y", "#f9e928"),
                                         ("black", "#000000"), ("white", "#ffffff"))]

# Supplied Xbox gameplay gems, fitted to the existing 21x26 gameplay cell.
# Black and White retain the existing gem redraws in both gem styles.
GEM_BUTTONS = [dict(name=f"gem_{button}__0", tag=f"ui\\hud\\buttons\\gem_{button}",
                   bitmap=0, width=32, height=32, format="dxt3", scale=8, crc=0,
                   source_png=f"source/{button}_gem.png") for button in "abxy"]


# Supplied Xbox trigger and three thumbstick press states, shared by every style.
CONTROL_BUTTONS = [dict(name=f"{name}__0", tag=f"ui\\hud\\buttons\\{name}",
                        bitmap=0, width=64, height=64, format="a8r8g8b8", scale=8,
                        crc=0, source_png=f"source/{name}.png", control_cell=cell)
                   for name, cell in [("trigger_left", [50,35]), ("trigger_right", [50,35])] +
                   [(f"thumbstick{state}_{side}", [36,40])
                    for state in ("", "-halfway", "-full") for side in ("left", "right")]]


# A replay's button prompts, every button the bar names (hud_hires_prompt_draw, hud_hires_tags.c): Kenney's Xbox
# Series pictures (kenney/, CC0: platforms/Kenney-LICENSE.txt), each as large as fits its square cell, kept to its
# proportions, a texel of the cell's size clear round it
PROMPT_ART = {"a": "xbox_button_color_a", "b": "xbox_button_color_b", "x": "xbox_button_color_x",
              "y": "xbox_button_color_y", "black": "xbox_rb", "white": "xbox_lb",
              "dpad_horizontal": "xbox_dpad_horizontal", "back": "xbox_button_view", "start": "xbox_button_menu",
              "stick_right_press": "xbox_stick_r_press"}
PROMPT_BUTTONS = [dict(name=f"prompt_{button}__0", tag=f"ui\\hud\\buttons\\prompt_{button}",
                       bitmap=0, width=64, height=64, format="a8r8g8b8", scale=8, crc=0, kenney=art)
                  for button, art in PROMPT_ART.items()]


def prompt(entry: dict) -> np.ndarray:
    """A prompt's Kenney picture in its cell."""
    scale = entry["scale"]
    size = (entry["width"] * scale, entry["height"] * scale)
    image = Image.new("RGBA", size)
    placed_face(image, kenney_face(ASSETS / "kenney", entry["kenney"], (size[0] - 2 * scale, size[1] - 2 * scale)),
                (0, 0, *size))
    return bleed(np.asarray(image).copy())


def gameplay_control(entry: dict) -> np.ndarray:
    """Keep supplied artwork proportional inside a padded legacy-sized cell."""
    source = Image.open(ASSETS / entry["source_png"]).convert("RGBA")
    face = source.crop(source.getchannel("A").getbbox())
    scale = entry["scale"]
    width, height = [n * scale for n in entry["control_cell"]]
    face.thumbnail((width - 2 * scale, height - 2 * scale), Image.Resampling.LANCZOS)
    image = Image.new("RGBA", (entry["width"] * scale, entry["height"] * scale))
    image.paste(face, ((width - face.width) // 2, (height - face.height) // 2))
    # The original HUD uses ONE / INVSRCALPHA. Store premultiplied edges
    # so that blend preserves its brightness without exposing transparent RGB.
    pixels = np.asarray(image).copy()
    pixels[..., :3] = (pixels[..., :3].astype(np.uint16) * pixels[..., 3:4] + 127) // 255
    return pixels


def document(width: int, height: int, body: str) -> str:
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}">\n{body}\n</svg>\n')


# the redraws' letter, and the shadow Y's letter casts, which move together
LETTER_IDS = ("letter", "shadow")

ElementTree.register_namespace("", "http://www.w3.org/2000/svg")


def placed(svg: Path, width: int, height: int, place: list, letter: list = (0, 0)) -> str:
    """The redraw as a picture of its bitmap's size, scaled by place[0] and
    moved by place[1:] (texels), its letter moved a further letter (texels
    of the bitmap) on its oval."""
    root = ElementTree.parse(svg).getroot()
    scale, x, y = place
    for element in root.iter():
        if element.get("id") in LETTER_IDS:
            element.set("transform",
                        f"translate({letter[0] / scale} {letter[1] / scale}) {element.get('transform', '')}")
    body = "".join(ElementTree.tostring(child, encoding="unicode") for child in root)
    return document(width, height, f'<g transform="translate({x} {y}) scale({scale})">{body}</g>')


def stencil(svg: str, cell: list, place: list, index: int, letter: list = (0, 0)) -> str:
    """A redraw as the message icons draw its button, in a sprite's cell
    (texels of its sheet): its oval in white with its letter cut out,
    scaled by place[0] and moved by place[1:] from the cell's corner, its
    letter moved a further letter (texels of the sheet), and kept to the
    cell; index names its clip and mask."""
    root = ElementTree.parse(ASSETS / "svg" / svg).getroot()
    parents = {child: parent for parent in root.iter() for child in parent}
    oval, cut = root.find(".//*[@id='oval']"), root.find(".//*[@id='letter']")
    left, top, right, bottom = cell
    scale, x, y = place
    moved = f"translate({left + x} {top + y}) scale({scale})"
    area = f'x="{left}" y="{top}" width="{right - left}" height="{bottom - top}"'
    return (f'<defs><clipPath id="cell{index}"><rect {area}/></clipPath>\n'
            f'<mask id="letter{index}" maskUnits="userSpaceOnUse" {area}><rect {area} fill="#ffffff"/>\n'
            f'<path transform="{moved} translate({letter[0] / scale} {letter[1] / scale}) {cut.get("transform")}" '
            f'fill="#000000" d="{cut.get("d")}"/></mask></defs>\n'
            f'<g clip-path="url(#cell{index})" mask="url(#letter{index})">'
            f'<ellipse transform="{moved} {parents[oval].get("transform")}" rx="{oval.get("rx")}" '
            f'ry="{oval.get("ry")}" fill="#ffffff"/></g>')


def classic(svg: str, color: str, letter_move: list = (0, 0)) -> str:
    """The existing redraw's plain oval and black letter in the same cell."""
    root = ElementTree.parse(ASSETS / "svg" / svg).getroot()
    parents = {child: parent for parent in root.iter() for child in parent}
    oval, letter = root.find(".//*[@id='oval']"), root.find(".//*[@id='letter']")
    oval.set("fill", color)
    oval.set("transform", parents[oval].get("transform", ""))
    body = ElementTree.tostring(oval, encoding="unicode")
    if letter is not None:
        letter.set("fill", "#000000")
        letter.set("transform", f"translate({letter_move[0]} {letter_move[1]}) {letter.get('transform', '')}")
        body += ElementTree.tostring(letter, encoding="unicode")
    return document(32, 32, body)


def gameplay_gem(entry: dict) -> np.ndarray:
    """Fit the supplied transparent oval to the shared gameplay icon cell."""
    source = Image.open(ASSETS / entry["source_png"]).convert("RGBA")
    bounds = source.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError(f"{entry['source_png']}: empty button")
    scale = entry["scale"]
    reference = Image.open(ASSETS / "a_butn__0.png").convert("RGBA")
    # Keep the menu redraw's outer bounds and transparent inset, rather than
    # filling its whole cropped cell and making the new faces look larger.
    factor = entry["width"] * scale / reference.width
    left, top, right, bottom = [round(n * factor) for n in reference.getchannel("A").getbbox()]
    cell = (right - left, bottom - top)
    face = source.crop(bounds)
    face.thumbnail(cell, Image.Resampling.LANCZOS)
    image = Image.new("RGBA", (entry["width"] * scale, entry["height"] * scale))
    image.paste(face, (left + (cell[0] - face.width) // 2, top + (cell[1] - face.height) // 2))
    return bleed(np.asarray(image).copy())


def render(text: str, scale: int) -> np.ndarray:
    """An SVG document at scale times its size."""
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "placed.svg"
        path.write_text(text)
        return render_svg(path, scale)


def icon(xbox_map: XboxMap, tag: str) -> dict:
    """An icon's bitmap (its group's only one)."""
    if ("bitm", tag) not in xbox_map.tags:
        sys.exit(f"{tag}: not in this map")
    bitmaps = xbox_map.bitmap_group(tag)["bitmaps"]
    if len(bitmaps) != 1:
        sys.exit(f"{tag}: {len(bitmaps)} bitmaps, not the icon's one")
    return bitmaps[0]


def shape(alpha: np.ndarray, scale: int = 1) -> tuple:
    """The area and centre of a shape, in texels, from its alpha drawn at
    scale times the texels."""
    alpha = alpha.astype(float) / 255
    ys, xs = np.mgrid[0:alpha.shape[0], 0:alpha.shape[1]]
    area = alpha.sum()
    return (area / scale ** 2, ((xs + 0.5) * alpha).sum() / area / scale,
            ((ys + 0.5) * alpha).sum() / area / scale)


def stencil_place(svg: str, alpha: np.ndarray) -> list:
    """Where a redraw's stencil goes in a sprite's cell, whose alpha it is:
    where it covers it best, from where it has its area and centre."""
    from scipy import optimize

    height, width = alpha.shape
    root = ElementTree.parse(ASSETS / "svg" / svg).getroot()
    size = int(root.get("width")), int(root.get("height"))
    area, x, y = shape(render(document(*size, stencil(svg, [0, 0, *size], [1, 0, 0], 0)), SCALE)[..., 3], SCALE)
    sprite_area, sprite_x, sprite_y = shape(alpha)
    scale = (sprite_area / area) ** 0.5
    target = alpha.astype(float) / 255

    def error(place: np.ndarray) -> float:
        drawn = render(document(width, height, stencil(svg, [0, 0, width, height], list(place), 0)), SCALE)
        reduced = drawn[..., 3].astype(float).reshape(height, SCALE, width, SCALE).mean(axis=(1, 3)) / 255
        return float(((reduced - target) ** 2).sum())

    best = optimize.minimize(error, [scale, sprite_x - scale * x, sprite_y - scale * y], method="Powell",
                             options={"xtol": 1e-3, "ftol": 1e-7}).x
    return [round(float(best[0]), 4), round(float(best[1]), 3), round(float(best[2]), 3)]


def whiteness(image: np.ndarray) -> np.ndarray:
    """How white each texel is, over black: the letters' white against the
    ovals' colours and rims, which do not move with the letter."""
    alpha = image[..., 3:4].astype(float) / 255
    return (image[..., :3].astype(float) * alpha).min(axis=2) / 255


def best_letter(error) -> list:
    """The letter's move (texels) with the least error, from none."""
    from scipy import optimize

    best = optimize.minimize(lambda move: error(list(move)), [0.0, 0.0], method="Powell",
                             options={"xtol": 1e-3, "ftol": 1e-8}).x
    return [round(float(best[0]), 3), round(float(best[1]), 3)]


def button_letter(entry: dict, bitmap: dict) -> list:
    """Where a button's letter goes on its oval (a move, in texels): where
    its white is closest to the map's."""
    target = whiteness(decode_bitmap(bitmap))
    width, height = entry["width"], entry["height"]

    def error(move: list) -> float:
        drawn = render(placed(ASSETS / "svg" / entry["svg"], width, height, entry["place"], move), SCALE)
        reduced = np.asarray(Image.fromarray(drawn, "RGBA").resize((width, height), Image.BOX))
        return float(((whiteness(reduced) - target) ** 2).sum())

    return best_letter(error)


def sprite_letter(sprite: dict, alpha: np.ndarray) -> list:
    """Where a message icon's letter goes on its oval (a move, in texels of
    the sheet): where its cut-out is closest to the map's sprite, whose
    alpha it is."""
    left, top, right, bottom = sprite["cell"]
    width, height = right - left, bottom - top
    target = alpha.astype(float) / 255
    cell = [0, 0, width, height]

    def error(move: list) -> float:
        drawn = render(document(width, height, stencil(sprite["svg"], cell, sprite["place"], 0, move)), SCALE)
        reduced = drawn[..., 3].astype(float).reshape(height, SCALE, width, SCALE).mean(axis=(1, 3)) / 255
        return float(((reduced - target) ** 2).sum())

    return best_letter(error)


def layout(arguments) -> None:
    xbox_map = XboxMap(Path(arguments.map))
    entries = []
    for tag, svg in BUTTONS.items():
        bitmap = icon(xbox_map, tag)
        width, height = bitmap["width"], bitmap["height"]
        drawn_over = BITMAPS + Path(svg).stem
        place = [1, 0, 0]
        if drawn_over != tag:
            area, x, y = shape(decode_bitmap(bitmap)[..., 3])
            drawn_area, drawn_x, drawn_y = shape(decode_bitmap(icon(xbox_map, drawn_over))[..., 3])
            scale = (area / drawn_area) ** 0.5
            place = [round(scale, 4), round(x - scale * drawn_x, 3), round(y - scale * drawn_y, 3)]
        entry = {
            "name": tag.split("\\")[-1] + "__0",
            "tag": tag,
            "bitmap": 0,
            "width": width,
            "height": height,
            "format": FORMATS[bitmap["format"]],
            "scale": SCALE,
            # (the map's, which the game checks before drawing this in its
            # place: other languages' maps and modified ones may differ)
            "crc": zlib.crc32(bitmap["pixels"][:level0_size(bitmap)]),
            "svg": svg,
            "place": place,
        }
        entry["letter"] = button_letter(entry, bitmap)
        entries.append(entry)
        print(f"{tag}: {width}x{height} {FORMATS[bitmap['format']]}, {svg} at {place[0]}x, moved {place[1]}, "
              f"{place[2]}; its letter moved {entry['letter'][0]}, {entry['letter'][1]}")
    # the message icons' buttons
    if ("bitm", MESSAGE_ICONS) not in xbox_map.tags:
        sys.exit(f"{MESSAGE_ICONS}: not in this map")
    group = xbox_map.bitmap_group(MESSAGE_ICONS)
    bitmap = group["bitmaps"][0]
    width, height = bitmap["width"], bitmap["height"]
    alpha = decode_bitmap(bitmap)[..., 3]
    sprites = []
    for sequence, svg in MESSAGE_BUTTONS.items():
        cells = group["sequences"][sequence]
        if len(cells) != 1 or cells[0][0] != 0:
            sys.exit(f"{MESSAGE_ICONS}: sequence {sequence} is not one sprite of its first bitmap")
        cell = pixel_rectangle(cells[0], width, height)
        place = stencil_place(svg, alpha[cell[1]:cell[3], cell[0]:cell[2]])
        sprite = {"sequence": sequence, "cell": cell, "svg": svg, "place": place}
        sprite["letter"] = sprite_letter(sprite, alpha[cell[1]:cell[3], cell[0]:cell[2]])
        sprites.append(sprite)
        print(f"{MESSAGE_ICONS} sequence {sequence}: cell {cell}, {svg} at {place[0]}x, moved {place[1]}, "
              f"{place[2]}; its letter moved {sprite['letter'][0]}, {sprite['letter'][1]}")
    entries.append({
        "name": MESSAGE_ICONS.split("\\")[-1] + "__0",
        "tag": MESSAGE_ICONS,
        "bitmap": 0,
        "width": width,
        "height": height,
        "format": FORMATS[bitmap["format"]],
        "scale": SCALE,
        "crc": zlib.crc32(bitmap["pixels"][:level0_size(bitmap)]),
        # (the sprites it stands for, not the whole bitmap)
        "sprites": sprites,
    })
    classic_buttons = [dict(entry) for entry in CLASSIC_BUTTONS]
    for entry, sprite in zip(classic_buttons, sprites):
        entry["letter"] = [round(n / sprite["place"][0], 4) for n in sprite["letter"]]
    LIST.write_text(json.dumps({"assets": entries + HUD_BUTTONS + classic_buttons + GEM_BUTTONS + CONTROL_BUTTONS +
                                       PROMPT_BUTTONS}, indent=1) + "\n")


def build(arguments) -> None:
    description = json.loads(LIST.read_text())
    names = {f"{entry['name']}.png" for entry in description["assets"]}
    for stale in ASSETS.glob("*.png"):
        if stale.name not in names and not arguments.only:
            stale.unlink()
    for entry in description["assets"]:
        if arguments.only and entry["name"] not in arguments.only:
            continue
        if "kenney" in entry:
            image = prompt(entry)
        elif "control_cell" in entry:
            image = gameplay_control(entry)
        elif "source_png" in entry:
            image = gameplay_gem(entry)
        elif "classic" in entry:
            image = bleed(render(classic(entry["svg"], entry["classic"], entry.get("letter", (0, 0))), entry["scale"]))
        elif "sprites" in entry:
            body = "\n".join(stencil(sprite["svg"], sprite["cell"], sprite["place"], index,
                                     sprite.get("letter", (0, 0)))
                             for index, sprite in enumerate(entry["sprites"]))
            image = render(document(entry["width"], entry["height"], body), entry["scale"])
            # (grey the same as alpha, as the sheet's)
            image[..., :3] = image[..., 3:4]
        else:
            image = render(placed(ASSETS / "svg" / entry["svg"], entry["width"], entry["height"], entry["place"],
                                  entry.get("letter", (0, 0))), entry["scale"])
            # (the colour carried into the transparent texels, so that
            # filtering and the mip levels keep it at the oval's edge)
            image = bleed(image)
        Image.fromarray(image, "RGBA").save(ASSETS / f"{entry['name']}.png", optimize=True)
        print(f"{entry['name']}.png: {image.shape[1]}x{image.shape[0]}")


def check(arguments) -> None:
    xbox_map = XboxMap(Path(arguments.map))
    output = Path(arguments.out)
    output.mkdir(parents=True, exist_ok=True)
    for entry in json.loads(LIST.read_text())["assets"]:
        if ("bitm", entry["tag"]) not in xbox_map.tags:
            print(f"{entry['name']}: not in this map")
            continue
        bitmap = xbox_map.bitmap_group(entry["tag"])["bitmaps"][entry["bitmap"]]
        if zlib.crc32(bitmap["pixels"][:level0_size(bitmap)]) != entry["crc"]:
            print(f"{entry['name']}: this map's bitmap is not the one laid out")
        xbox = decode_bitmap(bitmap)
        image = Image.open(ASSETS / f"{entry['name']}.png").convert("RGBA")
        reduced = np.asarray(image.resize((entry["width"], entry["height"]), Image.BOX))
        scale = entry["scale"]
        # (each sprite's cell, or the whole bitmap)
        cells = [sprite["cell"] for sprite in entry.get("sprites", [])] or [[0, 0, entry["width"], entry["height"]]]
        for index, (left, top, right, bottom) in enumerate(cells):
            score = overlap(reduced[top:bottom, left:right, 3].astype(float), xbox[top:bottom, left:right, 3].astype(float))
            name = entry["name"] + (f"__sprite{index}" if "sprites" in entry else "")
            print(f"{name}: alpha overlap {score:.2f}")
            # the map's | ours reduced | ours, over the menus' dark blue
            ours = image.crop((left * scale, top * scale, right * scale, bottom * scale))
            panels = [Image.fromarray(np.ascontiguousarray(picture[top:bottom, left:right]), "RGBA").resize(
                ours.size, Image.NEAREST) for picture in (xbox, reduced)]
            panels.append(ours)
            width, height = ours.size
            sheet = Image.new("RGB", (width * 3 + 16, height), (255, 0, 0))
            for place, picture in enumerate(panels):
                background = Image.new("RGBA", picture.size, (16, 26, 60, 255))
                sheet.paste(Image.alpha_composite(background, picture).convert("RGB"), (place * (width + 8), 0))
            sheet.save(output / f"{name}.png")


# ---------- a port's own art (port/assets/buttons/platforms/<port>)
#
# Kenney's Input Prompts (platforms/<port>/kenney, CC0: platforms/Kenney-LICENSE.txt) drawn in place of the common
# PNGs: each in the box the common PNG's art takes (a sprite's, in its cell, for the message icons), so that it sits
# where the game draws the common one. The game's buttons are the Xbox's by where they sit (SDL's south, east, west
# and north: xinput_sdl.c), so each port draws the button in that place: on the PS5 the cross for A, on the Switch
# the B (the bottom button) for A. The Switch's menus can take its right button (marked A) as A
# (controls.nintendo_menu_buttons), so its menus' A and B pictures come twice: as they are with that on, and by
# place. Each entry says where its letters are right, as bits (embed_assets.py; hud_hires.c: contexts): 1 the menus
# with the swap on, 2 the menus with it off, 4 a game; 0 everywhere.

PLATFORMS = ASSETS / "platforms"
CONTEXT_MENUS_SWAPPED, CONTEXT_MENUS, CONTEXT_GAME = 1, 2, 4

# each Xbox button's picture on the port, by where it sits (the game's view of it everywhere but the swapped menus);
# the sticks' pictures are their rest's and their press's
PLATFORM_ART = {
    "switch": {"a": "switch_button_b", "b": "switch_button_a", "x": "switch_button_y", "y": "switch_button_x",
               "black": "switch_button_r", "white": "switch_button_l",
               "dpad_horizontal": "switch_dpad_horizontal", "back": "switch_button_minus",
               "start": "switch_button_plus",
               "trigger_left": "switch_button_zl", "trigger_right": "switch_button_zr",
               "stick_left": "switch_stick_l", "stick_right": "switch_stick_r",
               "stick_left_press": "switch_stick_l_press", "stick_right_press": "switch_stick_r_press"},
    "ps5": {"a": "playstation_button_cross", "b": "playstation_button_circle", "x": "playstation_button_square",
            "y": "playstation_button_triangle", "black": "playstation_trigger_r1", "white": "playstation_trigger_l1",
            "dpad_horizontal": "playstation_dpad_horizontal", "back": "playstation5_button_create",
            "start": "playstation5_button_options",
            "trigger_left": "playstation_trigger_l2", "trigger_right": "playstation_trigger_r2",
            "stick_left": "playstation_stick_l", "stick_right": "playstation_stick_r",
            "stick_left_press": "playstation_stick_l_press", "stick_right_press": "playstation_stick_r_press"},
}
# (the Switch's menus with the swap on: A and B by their letters)
SWAPPED_MENU_ART = {"switch": {"a": "switch_button_a", "b": "switch_button_b"}}
# the textures the menus draw, and a replay's prompts, whose buttons are swapped as the menus' (xinput_sdl.c); the
# others are a game's HUD prompts
MENU_TEXTURES = {"a_butn__0", "b_butn__0", "x_butn__0", "y_butn__0", "a_butn_sm__0", "b_butn_sm__0",
                 "black_butn__0", "white_butn__0", "hud_msg_icons_sm__0"} | {
                 entry["name"] for entry in PROMPT_BUTTONS}


def platform_button(name: str) -> str:
    """The Xbox button (or control) a common texture is of: a_butn_sm__0 is a, classic_black__0 black,
    thumbstick-full_left__0 stick_left_press."""
    stem = name[:-len("__0")]
    if stem.startswith("thumbstick"):
        state, side = stem[len("thumbstick"):].split("_")
        return f"stick_{side}" + ("_press" if state == "-full" else "")
    if stem.startswith("trigger_"):
        return stem
    for prefix in ("classic_", "gem_", "prompt_"):
        if stem.startswith(prefix):
            return stem[len(prefix):]
    return stem.split("_")[0]


def kenney_face(folder: Path, art: str, size: tuple) -> Image.Image:
    """Kenney's picture (of folder), cropped to its shape and as large as fits size (keeping its proportions)."""
    svg = folder / f"{art}.svg"
    root = ElementTree.parse(svg).getroot()
    view = [float(n) for n in (root.get("viewBox") or f"0 0 {root.get('width')} {root.get('height')}").split()]
    zoom = max(4.0, 2 * max(size[0] / view[2], size[1] / view[3]))
    face = Image.fromarray(render_svg(svg, zoom), "RGBA")
    face = face.crop(face.getchannel("A").getbbox())
    factor = min(size[0] / face.width, size[1] / face.height)
    return face.resize((max(1, round(face.width * factor)), max(1, round(face.height * factor))),
                       Image.Resampling.LANCZOS)


def placed_face(image: Image.Image, face: Image.Image, box: tuple) -> None:
    """face pasted in the middle of box (left, top, right, bottom)."""
    left, top, right, bottom = box
    image.alpha_composite(face, (left + (right - left - face.width) // 2, top + (bottom - top - face.height) // 2))


def platform_texture(platform: str, entry: dict, art: dict) -> np.ndarray:
    """A common texture redrawn with the port's art (art: Xbox button to Kenney's picture)."""
    common = Image.open(ASSETS / f"{entry['name']}.png").convert("RGBA")
    image = Image.new("RGBA", common.size)
    scale = entry["scale"]
    if "sprites" in entry:
        # (the message icons: each button's stencil in the box its sprite's art takes, white, its letter cut out;
        # grey the same as alpha, as the sheet's)
        for sprite in entry["sprites"]:
            left, top, right, bottom = [n * scale for n in sprite["cell"]]
            cell = common.crop((left, top, right, bottom)).getchannel("A").getbbox()
            if cell is None:
                continue
            box = (left + cell[0], top + cell[1], left + cell[2], top + cell[3])
            button = Path(sprite["svg"]).stem.split("_")[0]
            placed_face(image, kenney_face(PLATFORMS / platform / "kenney", art[button], (box[2] - box[0], box[3] - box[1])), box)
        pixels = np.asarray(image).copy()
        pixels[..., :3] = pixels[..., 3:4]
        return pixels
    box = common.getchannel("A").getbbox()
    placed_face(image, kenney_face(PLATFORMS / platform / "kenney", art[platform_button(entry["name"])], (box[2] - box[0], box[3] - box[1])),
                box)
    pixels = np.asarray(image).copy()
    if "control_cell" in entry:
        # (premultiplied, as gameplay_control's: the HUD's ONE / INVSRCALPHA blend)
        pixels[..., :3] = (pixels[..., :3].astype(np.uint16) * pixels[..., 3:4] + 127) // 255
        return pixels
    return bleed(pixels)


def platform(arguments) -> None:
    """Renders the port's art into platforms/<port>/*.png and lists it in platforms/<port>/buttons.json."""
    name = arguments.platform
    folder = PLATFORMS / name
    entries = json.loads(LIST.read_text())["assets"]
    overrides = []
    if arguments.only:
        # (the others' entries kept, their PNGs untouched)
        overrides = [override for override in json.loads((folder / "buttons.json").read_text())["overrides"]
                     if override["name"] not in arguments.only]
    else:
        for stale in folder.glob("*.png"):
            stale.unlink()
    for entry in entries:
        if arguments.only and entry["name"] not in arguments.only:
            continue
        variants = [(f"{entry['name']}.png", 0, PLATFORM_ART[name])]
        swapped = SWAPPED_MENU_ART.get(name, {})
        if entry["name"] in MENU_TEXTURES and swapped and (
                platform_button(entry["name"]) in swapped or "sprites" in entry):
            # (by place in a game and the menus without the swap; by letter in the menus with it)
            variants = [(f"{entry['name']}.png", CONTEXT_MENUS | CONTEXT_GAME, PLATFORM_ART[name]),
                        (f"{entry['name']}.menus.png", CONTEXT_MENUS_SWAPPED, {**PLATFORM_ART[name], **swapped})]
        for png, contexts, art in variants:
            image = platform_texture(name, entry, art)
            Image.fromarray(image, "RGBA").save(folder / png, optimize=True)
            overrides.append({"name": entry["name"], "png": png, "contexts": contexts})
            print(f"{name}/{png}: {image.shape[1]}x{image.shape[0]}" + (f", contexts {contexts}" if contexts else ""))
    (folder / "buttons.json").write_text(json.dumps({"overrides": overrides}, indent=1) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("layout")
    command.add_argument("--map", required=True)
    command = commands.add_parser("build")
    command.add_argument("--only", nargs="+", metavar="NAME")
    command = commands.add_parser("platform")
    command.add_argument("--platform", required=True, choices=sorted(PLATFORM_ART))
    command.add_argument("--only", nargs="+", metavar="NAME")
    command = commands.add_parser("check")
    command.add_argument("--map", required=True)
    command.add_argument("--out", required=True)
    arguments = parser.parse_args()
    {"layout": layout, "build": build, "check": check, "platform": platform}[arguments.command](arguments)


if __name__ == "__main__":
    sys.exit(main())
