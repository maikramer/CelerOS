#!/usr/bin/env python3
"""Gera as fontes GFX do CelerOS com acentos (Latin-1, U+0020..U+00FF).

As fontes embutidas no LovyanGFX (DejaVu*, FreeSans*) cobrem so ASCII
(0x20..0x7E): texto acentuado sumia na tela. Visual preservado:

- ASCII: copiado VERBATIM das fontes originais do LovyanGFX (nenhum texto
  existente muda um pixel).
- Letras acentuadas (a-agudo, c-cedilha, o-til...): a letra base ORIGINAL +
  so o acento, extraido de um render FreeType (fontconvert do Adafruit: mesmo
  TTF e tamanho) como diferenca entre a letra acentuada e a base.
- Demais simbolos Latin-1 (graus, ordinais, ss, AE...): render FreeType.

`--verify` mostra quanto do ASCII o render atual reproduz (versoes novas
dos TTFs/FreeType mudam o hinting de alguns glifos — por isso a copia).

Uso (freetype-py):
    uv run --with freetype-py python3 tools/make_fonts.py            # gera
    uv run --with freetype-py python3 tools/make_fonts.py --verify   # confere ASCII

Saida: main/Assets/Fonts/CelerFonts.{h,cpp} (GERADOS — nao editar a mao).
Fontes que a placa nao referencia saem do binario (gc-sections).
"""
import argparse
import pathlib
import re
import sys
import unicodedata

import freetype

ROOT = pathlib.Path(__file__).resolve().parent.parent
LGFX = ROOT / "components" / "LovyanGFX" / "src" / "lgfx" / "Fonts"
OUT_H = ROOT / "main" / "Assets" / "Fonts" / "CelerFonts.h"
OUT_CPP = ROOT / "main" / "Assets" / "Fonts" / "CelerFonts.cpp"

DEJAVU = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FREESANS = "/usr/share/fonts/truetype/freefont/FreeSans.ttf"
FREESANS_BOLD = "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf"

# (nome, ttf, (modo, tamanho), fonte original no LovyanGFX)
# modo "pt": fontconvert do Adafruit (pontos a 141 dpi, fontes "NNpt7b")
# modo "px": altura em pixels + 1px de avanco (conversor das DejaVuNN do
#            LovyanGFX)
# O yAdvance (entrelinha) vem da fonte original: as versoes antigas dos TTFs
# tinham metricas verticais diferentes das atuais — manter o original deixa
# o layout de todas as telas exatamente como era.
FONTS = [
    ("DejaVu9", DEJAVU, ("px", 9), "Custom/DejaVu9.h"),
    ("DejaVu12", DEJAVU, ("px", 12), "Custom/DejaVu12.h"),
    ("DejaVu24", DEJAVU, ("px", 24), "Custom/DejaVu24.h"),
    ("DejaVu40", DEJAVU, ("px", 40), "Custom/DejaVu40.h"),
    ("FreeSans9pt", FREESANS, ("pt", 9), "GFXFF/FreeSans9pt7b.h"),
    ("FreeSans12pt", FREESANS, ("pt", 12), "GFXFF/FreeSans12pt7b.h"),
    ("FreeSansBold12pt", FREESANS_BOLD, ("pt", 12), "GFXFF/FreeSansBold12pt7b.h"),
    ("FreeSansBold18pt", FREESANS_BOLD, ("pt", 18), "GFXFF/FreeSansBold18pt7b.h"),
    ("FreeSansBold24pt", FREESANS_BOLD, ("pt", 24), "GFXFF/FreeSansBold24pt7b.h"),
]
FIRST, LAST = 0x20, 0xFF


def set_size(face, mode, size):
    if mode == "pt":
        face.set_char_size(size << 6, 0, 141, 0)  # fontconvert: DPI 141
    else:
        face.set_pixel_sizes(0, size)


def render(ttf, mode, size, first, last):
    """Replica o fontconvert: bitmaps mono concatenados bit a bit."""
    face = freetype.Face(ttf)
    set_size(face, mode, size)
    bits = []  # lista de bits (0/1) do bitmap global
    glyphs = []
    for cp in range(first, last + 1):
        if 0x7F <= cp <= 0x9F:  # C1/DEL: glifo vazio (faixa contigua)
            glyphs.append((len(bits) // 8, 0, 0, 0, 0, 0))
            continue
        face.load_char(cp, freetype.FT_LOAD_TARGET_MONO)
        face.glyph.render(freetype.FT_RENDER_MODE_MONO)
        bm = face.glyph.bitmap
        offset = len(bits) // 8
        adv = (face.glyph.advance.x >> 6) + (1 if mode == "px" else 0)
        if face.glyph.outline.n_points == 0:  # espaco: sem bitmap (fontconvert)
            glyphs.append((offset, 0, 0, adv, 0, 1))
            continue
        for y in range(bm.rows):
            for x in range(bm.width):
                byte = bm.buffer[y * bm.pitch + (x >> 3)]
                bits.append((byte >> (7 - (x & 7))) & 1)
        # fontconvert: cada glifo comeca num byte novo
        while len(bits) % 8:
            bits.append(0)
        glyphs.append((offset, bm.width, bm.rows, adv,
                       face.glyph.bitmap_left, 1 - face.glyph.bitmap_top))
    data = bytes(int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))
    yadv = face.size.height >> 6
    return data, glyphs, yadv


def parse_lgfx(path):
    """Le bitmap/glifos/yAdvance de uma fonte GFX do LovyanGFX."""
    text = (LGFX / path).read_text(errors="replace")
    bm = re.search(r"Bitmaps\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", text, re.S).group(1)
    data = bytes(int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", bm))
    gl = re.search(r"Glyphs\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", text, re.S).group(1)
    glyphs = [tuple(int(v) for v in m) for m in
              re.findall(r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", gl)]
    yadv = int(re.findall(r"0x[0-9a-fA-F]+,\s*0x[0-9a-fA-F]+,\s*(\d+)\s*\}", text)[-1])
    return data, glyphs, yadv


def verify():
    ok = True
    for name, ttf, (mode, size), orig in FONTS:
        d0, g0, y0 = parse_lgfx(orig)
        d1, g1, _ = render(ttf, mode, size, 0x20, 0x7E)
        y1 = y0  # entrelinha herdada da original (ver FONTS)
        same = d0 == d1 and g0 == g1 and y0 == y1
        diff = sum(1 for a, b in zip(g0, g1) if a != b)
        print("%-18s %s  (glifos diferentes: %d, bytes %d/%d, yAdv %d/%d)" %
              (name, "IDENTICA" if same else "DIFERE", diff, len(d0), len(d1), y0, y1))
        ok &= same
    return ok


def glyph_pixels(data, glyph):
    """Pixels acesos de um glifo, em coordenadas absolutas (origem da pena,
    y relativo a linha de base)."""
    off, w, h, _adv, xo, yo = glyph
    px = set()
    for i in range(w * h):
        byte = data[off + (i >> 3)]
        if (byte >> (7 - (i & 7))) & 1:
            px.add((xo + i % w, yo + i // w))
    return px


def pack(px, adv):
    """Pixels absolutos -> (bits do bitmap, (w, h, adv, xo, yo))."""
    if not px:
        return [], (0, 0, adv, 0, 1)
    x0 = min(x for x, _ in px)
    x1 = max(x for x, _ in px)
    y0 = min(y for _, y in px)
    y1 = max(y for _, y in px)
    w, h = x1 - x0 + 1, y1 - y0 + 1
    bits = [1 if (x0 + i % w, y0 + i // w) in px else 0 for i in range(w * h)]
    return bits, (w, h, adv, x0, y0)


def decompose_base(cp):
    """Letra base ASCII de uma letra acentuada Latin-1 (None se nao houver)."""
    dec = unicodedata.decomposition(chr(cp))
    if not dec or dec.startswith("<"):
        return None
    base = int(dec.split()[0], 16)
    return base if 0x20 < base < 0x7F else None


def build(ttf, mode, size, orig):
    """Glifos 0x20..0xFF: ASCII original, acentuadas compostas, resto gerado."""
    d0, g0, yadv = parse_lgfx(orig)
    fd, fg, _ = render(ttf, mode, size, FIRST, LAST)
    fresh = {cp: glyph_pixels(fd, fg[cp - FIRST]) for cp in range(FIRST, LAST + 1)}
    fresh_adv = {cp: fg[cp - FIRST][3] for cp in range(FIRST, LAST + 1)}
    orig_px = {cp: glyph_pixels(d0, g0[cp - 0x20]) for cp in range(0x20, 0x7F)}
    orig_adv = {cp: g0[cp - 0x20][3] for cp in range(0x20, 0x7F)}

    bits_all, glyphs, composed = [], [], 0
    for cp in range(FIRST, LAST + 1):
        if cp <= 0x7E:
            px, adv = orig_px[cp], orig_adv[cp]
        elif cp <= 0x9F:
            px, adv = set(), 0
        else:
            base = decompose_base(cp)
            if base is not None:
                # acento = o que o render da acentuada tem a mais que o da base
                mark = fresh[cp] - fresh[base]
                px, adv = orig_px[base] | mark, orig_adv[base]
                composed += 1
            else:
                px, adv = fresh[cp], fresh_adv[cp]
        bits, g = pack(px, adv)
        glyphs.append((len(bits_all) // 8,) + g)
        bits_all += bits
        while len(bits_all) % 8:
            bits_all.append(0)
    data = bytes(int("".join(map(str, bits_all[i:i + 8])), 2) for i in range(0, len(bits_all), 8))
    return data, glyphs, yadv, composed


def emit():
    head = ["// GERADO por tools/make_fonts.py — nao editar a mao.",
            "// Fontes GFX (Adafruit) com Latin-1 (U+0020..U+00FF): ASCII identico",
            "// ao do LovyanGFX; acentuadas = letra base original + acento.",
            "// DejaVu Sans: https://dejavu-fonts.github.io/License.html",
            "// GNU FreeFont (FreeSans): GPLv3 com excecao de fonte embutida."]
    h = head + ["#pragma once", "#include <lgfx/v1/lgfx_fonts.hpp>", "",
                "namespace celer {", "namespace fonts {"]
    c = head + ['#include "CelerFonts.h"', "", "namespace celer {", "namespace fonts {", ""]
    total = 0
    for name, ttf, (mode, size), orig in FONTS:
        data, glyphs, yadv, composed = build(ttf, mode, size, orig)
        total += len(data) + len(glyphs) * 8
        print("  %-18s %5d B de bitmap, %d acentuadas compostas" % (name, len(data), composed))
        h.append("extern const lgfx::GFXfont %s;" % name)
        c.append("static const uint8_t %sBitmaps[] = {" % name)
        for i in range(0, len(data), 16):
            c.append("  " + ", ".join("0x%02x" % v for v in data[i:i + 16]) + ",")
        c.append("};")
        c.append("static const lgfx::GFXglyph %sGlyphs[] = {" % name)
        for cp, g in zip(range(FIRST, LAST + 1), glyphs):
            c.append("  {%6d, %3d, %3d, %3d, %4d, %4d},  // U+%04X" % (g + (cp,)))
        c.append("};")
        c.append("const lgfx::GFXfont %s = {(uint8_t*)%sBitmaps, (lgfx::GFXglyph*)%sGlyphs,"
                 " 0x%02X, 0x%02X, %d};" % (name, name, name, FIRST, LAST, yadv))
        c.append("")
    h += ["}  // namespace fonts", "}  // namespace celer", ""]
    c += ["}  // namespace fonts", "}  // namespace celer", ""]
    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("\n".join(h))
    OUT_CPP.write_text("\n".join(c))
    print("%s + .cpp: %d fontes, ~%d KB se todas forem usadas" % (OUT_H.relative_to(ROOT), len(FONTS), total // 1024))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--verify", action="store_true", help="quanto do ASCII o render atual reproduz")
    args = ap.parse_args()
    if args.verify:
        verify()
        return
    emit()


if __name__ == "__main__":
    main()
