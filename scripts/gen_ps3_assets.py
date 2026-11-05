#!/usr/bin/env python3
"""Generate PS3 homebrew assets (ICON0.PNG, PIC1.PNG) and optionally PARAM.SFO.

Uses Pillow when available to build the XMB icon and background from
`assets/icon_source.jpg`. Falls back to copying pre-existing assets.
"""

import os
import shutil
import struct
import sys


# ---------------------------------------------------------------------------
# PARAM.SFO writer
# ---------------------------------------------------------------------------

def _sfo_pad(data, align=4):
    pad = (-len(data)) % align
    return data + b"\x00" * pad


def write_param_sfo(path, title="Novuss (LNF)", title_id="NOVU00001",
                    category="HG", app_ver="01.00", version="01.00",
                    attribute=0, bootable=1, parental_level=1,
                    resolution=63, sound_format=1):
    entries = [
        ("APP_VER", "UTF8", app_ver),
        ("ATTRIBUTE", "UINT32", attribute),
        ("BOOTABLE", "UINT32", bootable),
        ("CATEGORY", "UTF8", category),
        ("PARENTAL_LEVEL", "UINT32", parental_level),
        ("RESOLUTION", "UINT32", resolution),
        ("SOUND_FORMAT", "UINT32", sound_format),
        ("TITLE", "UTF8", title),
        ("TITLE_ID", "UTF8", title_id),
        ("VERSION", "UTF8", version),
    ]
    entries.sort(key=lambda e: e[0])

    key_table = bytearray()
    key_offsets = []
    for (k, _fmt, _v) in entries:
        key_offsets.append(len(key_table))
        key_table.extend(k.encode("ascii") + b"\x00")
    key_table = _sfo_pad(bytes(key_table))

    data_table = bytearray()
    data_offsets = []
    data_lens = []
    for (_k, fmt, v) in entries:
        if fmt == "UTF8":
            vb = v.encode("utf-8") + b"\x00"
        elif fmt == "UINT32":
            vb = struct.pack("<I", int(v))
        else:
            raise ValueError("bad fmt")
        data_offsets.append(len(data_table))
        data_lens.append(len(vb))
        data_table.extend(vb)
        while len(data_table) % 4 != 0:
            data_table.append(0)
    data_table = bytes(data_table)

    header_size = 20
    index_size = 16 * len(entries)
    key_table_start = header_size + index_size
    data_table_start = key_table_start + len(key_table)

    out = bytearray()
    out.extend(b"\x00PSF")
    out.extend(struct.pack("<I", 0x00000101))
    out.extend(struct.pack("<I", key_table_start))
    out.extend(struct.pack("<I", data_table_start))
    out.extend(struct.pack("<I", len(entries)))

    for i, (k, fmt, _v) in enumerate(entries):
        key_off = key_offsets[i]
        fmt_val = 0x0004 if fmt == "UTF8" else 0x0404
        data_len = data_lens[i]
        data_max = data_len
        data_off = data_offsets[i]
        out.extend(struct.pack("<HHIII", key_off, fmt_val, data_len, data_max, data_off))

    out.extend(key_table)
    out.extend(data_table)

    with open(path, "wb") as f:
        f.write(bytes(out))


# ---------------------------------------------------------------------------
# Pillow-based asset builders
# ---------------------------------------------------------------------------

def _build_icon0(src_path, dst_path):
    from PIL import Image, ImageDraw, ImageFilter

    W, H = 320, 176
    ic_w, ic_h = 172, 172
    radius = 12

    card = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    src = Image.open(src_path).convert("RGBA")
    art = src.resize((ic_w, ic_h), Image.Resampling.LANCZOS)

    # Rounded rectangle mask for the artwork
    mask = Image.new("L", (ic_w, ic_h), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, ic_w - 1, ic_h - 1), radius=radius, fill=255
    )

    ic_x = (W - ic_w) // 2
    ic_y = 2

    # Subtle drop shadow
    shadow = Image.new("RGBA", (ic_w + 16, ic_h + 16), (0, 0, 0, 0))
    shadow_mask = Image.new("L", (ic_w + 16, ic_h + 16), 0)
    ImageDraw.Draw(shadow_mask).rounded_rectangle(
        (8, 8, 8 + ic_w - 1, 8 + ic_h - 1), radius=radius, fill=120
    )
    shadow_mask = shadow_mask.filter(ImageFilter.GaussianBlur(4))
    shadow.putalpha(shadow_mask)
    card.alpha_composite(shadow, (ic_x - 8, ic_y - 8))

    # Paste artwork with rounded mask
    card.paste(art, (ic_x, ic_y), mask)

    # Subtle outline border
    draw = ImageDraw.Draw(card)
    draw.rounded_rectangle(
        (ic_x, ic_y, ic_x + ic_w - 1, ic_y + ic_h - 1),
        radius=radius,
        outline=(200, 205, 215, 180),
        width=1,
    )

    card.save(dst_path, "PNG")


def _build_pic1(src_path, dst_path):
    from PIL import Image, ImageDraw, ImageFilter

    W, H = 1920, 1080
    card_w, card_h = 1024, 1024
    radius = 24

    # Vertical dark slate gradient background
    bg = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    top = (18, 19, 22, 255)
    bottom = (10, 11, 14, 255)
    grad = Image.new("RGBA", (1, H))
    gpx = grad.load()
    for y in range(H):
        t = y / float(H - 1)
        r = int(top[0] + (bottom[0] - top[0]) * t)
        g = int(top[1] + (bottom[1] - top[1]) * t)
        b = int(top[2] + (bottom[2] - top[2]) * t)
        gpx[0, y] = (r, g, b, 255)
    bg.paste(grad.resize((W, H), Image.Resampling.NEAREST), (0, 0))

    src = Image.open(src_path).convert("RGBA")
    art = src.resize((card_w, card_h), Image.Resampling.LANCZOS)

    mask = Image.new("L", (card_w, card_h), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, card_w - 1, card_h - 1), radius=radius, fill=255
    )

    card_x = 830
    card_y = 28

    # Soft drop shadow
    shadow = Image.new("RGBA", (card_w + 80, card_h + 80), (0, 0, 0, 0))
    shadow_mask = Image.new("L", (card_w + 80, card_h + 80), 0)
    ImageDraw.Draw(shadow_mask).rounded_rectangle(
        (40, 40, 40 + card_w - 1, 40 + card_h - 1), radius=radius, fill=160
    )
    shadow_mask = shadow_mask.filter(ImageFilter.GaussianBlur(24))
    shadow.putalpha(shadow_mask)
    bg.alpha_composite(shadow, (card_x - 40, card_y - 40))

    # Paste artwork with rounded mask
    bg.paste(art, (card_x, card_y), mask)

    # Subtle border
    draw = ImageDraw.Draw(bg)
    draw.rounded_rectangle(
        (card_x, card_y, card_x + card_w - 1, card_y + card_h - 1),
        radius=radius,
        outline=(180, 185, 195, 200),
        width=2,
    )

    bg.save(dst_path, "PNG")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv):
    out_dir = "."
    force_sfo = False
    rebuild = False
    args = list(argv[1:])
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--sfo":
            force_sfo = True
        elif a == "--rebuild":
            rebuild = True
        elif a in ("-h", "--help"):
            sys.stdout.write(
                "usage: gen_ps3_assets.py [OUT_DIR] [--sfo] [--rebuild]\n"
                "  OUT_DIR    directory to write assets (default: .)\n"
                "  --sfo      force rewrite of PARAM.SFO\n"
                "  --rebuild  force rebuild of ICON0.PNG / PIC1.PNG from source\n"
            )
            return 0
        else:
            out_dir = a
        i += 1

    os.makedirs(out_dir, exist_ok=True)
    usrdir = os.path.join(out_dir, "USRDIR")
    os.makedirs(usrdir, exist_ok=True)

    assets_dir = "assets"
    icon_src = os.path.join(assets_dir, "ICON0.PNG")
    pic_src = os.path.join(assets_dir, "PIC1.PNG")
    jpg_src = os.path.join(assets_dir, "icon_source.jpg")

    need_build = rebuild or not os.path.exists(icon_src) or not os.path.exists(pic_src)
    if need_build and os.path.exists(jpg_src):
        try:
            import PIL  # noqa: F401
            have_pil = True
        except ImportError:
            have_pil = False
        if have_pil:
            _build_icon0(jpg_src, icon_src)
            sys.stdout.write("built %s\n" % icon_src)
            _build_pic1(jpg_src, pic_src)
            sys.stdout.write("built %s\n" % pic_src)

    # Copy assets into out_dir
    if os.path.exists(icon_src):
        shutil.copyfile(icon_src, os.path.join(out_dir, "ICON0.PNG"))
        sys.stdout.write("copied ICON0.PNG\n")
    if os.path.exists(pic_src):
        shutil.copyfile(pic_src, os.path.join(out_dir, "PIC1.PNG"))
        sys.stdout.write("copied PIC1.PNG\n")

    snd_src = os.path.join(assets_dir, "SND0.AT3")
    if os.path.exists(snd_src):
        shutil.copyfile(snd_src, os.path.join(out_dir, "SND0.AT3"))
        sys.stdout.write("copied SND0.AT3\n")

    bgm_src = os.path.join(assets_dir, "menu_bgm.adpcm")
    if os.path.exists(bgm_src):
        shutil.copyfile(bgm_src, os.path.join(usrdir, "menu_bgm.adpcm"))
        sys.stdout.write("copied menu_bgm.adpcm\n")

    board_src = os.path.join(assets_dir, "board.png")
    if os.path.exists(board_src):
        shutil.copyfile(board_src, os.path.join(usrdir, "board.png"))
        sys.stdout.write("copied board.png\n")

    ini_src = "settings.ini"
    if os.path.exists(ini_src):
        shutil.copyfile(ini_src, os.path.join(usrdir, "settings.ini"))
        sys.stdout.write("copied settings.ini\n")

    sfo_path = os.path.join(out_dir, "PARAM.SFO")
    if force_sfo or not os.path.exists(sfo_path):
        write_param_sfo(sfo_path)
        sys.stdout.write("wrote %s\n" % sfo_path)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
