#!/usr/bin/env python3
"""Gera o header da logo de boot do CelerOS (main/Assets/SplashLogo.h).

A splash roda antes do LittleFS, entao a logo vai embutida em flash como
array RGB565 opaco (ja composta sobre THEME_BG — sem alpha em runtime).
O desenho usa pushImageRotateZoom com zoom = UI::W/240, entao basta uma
unica resolucao na escala do design 240x320.

Uso:  python3 tools/make_splash.py            (usa Documentation/assets/celeros_logo.png)
      python3 tools/make_splash.py --src caminho.png --bg 0x080C18 --out main/Assets/SplashLogo.h
"""
import argparse
import pathlib

from PIL import Image

REPO = pathlib.Path(__file__).resolve().parent.parent

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--src", default=str(REPO / "Documentation/assets/celeros_logo.png"))
ap.add_argument("--out", default=str(REPO / "main/Assets/SplashLogo.h"))
ap.add_argument("--width", type=int, default=200, help="largura na escala do design 240 (default 200)")
ap.add_argument("--bg", default="0x080C18", help="cor de fundo THEME_BG (hex RRGGBB com ou sem 0x)")
args = ap.parse_args()

bg_hex = args.bg.replace("0x", "").replace("0X", "")
bg = tuple(int(bg_hex[i:i + 2], 16) for i in (0, 2, 4))

img = Image.open(args.src).convert("RGBA")
bbox = img.getbbox()  # recorta margens transparentes
if bbox:
    img = img.crop(bbox)

w = args.width
h = round(img.height * w / img.width)
img = img.resize((w, h), Image.LANCZOS)

# Composicao sobre THEME_BG: canal alpha na propria escala (0..255), igual
# ao blit A4 do Icon.cpp.
px = img.load()
out = []
for y in range(h):
    for x in range(w):
        r, g, b, a = px[x, y]
        ia = 255 - a
        out.append(((r * a + bg[0] * ia + 127) // 255,
                    (g * a + bg[1] * ia + 127) // 255,
                    (b * a + bg[2] * ia + 127) // 255))

def rgb565(c):
    return ((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3)

lines = []
for i in range(0, len(out), 12):
    lines.append("    " + " ".join("0x%04X," % rgb565(c) for c in out[i:i + 12]))

header = f"""\
// Gerado por tools/make_splash.py a partir de {pathlib.Path(args.src).name} — nao editar a mao.
// Logo composta sobre THEME_BG ({args.bg}), RGB565 r5g6b5 little-endian;
// o setSwapBytes(true) do Board::init cuida da ordem no barramento.
#ifndef CELEROS_ASSETS_SPLASH_LOGO_H
#define CELEROS_ASSETS_SPLASH_LOGO_H

#include <stdint.h>

#define SPLASH_LOGO_W {w}
#define SPLASH_LOGO_H {h}

static const uint16_t kSplashLogo[SPLASH_LOGO_W * SPLASH_LOGO_H] = {{
{chr(10).join(lines)}
}};

#endif  // CELEROS_ASSETS_SPLASH_LOGO_H
"""

pathlib.Path(args.out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(args.out).write_text(header)
print(f"{args.out}: {w}x{h}, {w * h * 2 / 1024:.1f} KB de flash")
