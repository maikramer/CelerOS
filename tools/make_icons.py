#!/usr/bin/env python3
"""Gera o pacote de icones 64x64 do KryonOS.

Pipeline:
  1. text2d (FLUX.2 Klein local) gera as artes 512x512 em tools/icons_src/
     a partir de tools/icons.json — pulando o que ja existe (use --regen
     para forcar).
  2. Pillow po-processa: recorte quadrado, Lanczos para 64x64, conversao
     para RGB565 little-endian com arredondamento + dithering Floyd-Steinberg
     (gradientes suaves sem banding) e mascara de transparencia 4-bit do
     quadrado arredondado (AA por supersampling 4x).
  3. Saida em data/icons/<id>.bin no formato v2 do firmware (Icon.h):
     RGB565 [64*64*2] + A4 [64*64/2] = 10240 bytes. O firmware compoe o
     alpha por software — cantos transparentes de verdade, em qualquer fundo.
  4. Gera a folha de contato docs/assets/icons_preview.png.

Se quiser substituir uma arte: apague tools/icons_src/<id>.png, edite o
prompt em tools/icons.json e rode de novo (ou coloque um PNG 512x512 seu
no lugar — ele sera respeitado).
"""
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "icons_src"
OUT = ROOT / "data" / "icons"
PREVIEW = ROOT / "docs" / "assets" / "icons_preview.png"
MANIFEST = ROOT / "tools" / "icons.json"

SIZE = 64
RADIUS = 14
# Fundo usado apenas na folha de contato (o .bin tem alpha real)
THEME_BG = (13, 17, 23)


def rgb565_dither(img: Image.Image) -> bytes:
    """RGB -> RGB565 LE com arredondamento e Floyd-Steinberg.

    Truncamento simples transformava os gradientes/glow das artes em faixas
    visiveis (banding); a difusao de erro distribui a quantizacao como ruido
    de baixa amplitude, que o olho le como gradiente continuo.
    """
    w, h = img.size
    work = [list(c) for c in img.convert("RGB").getdata()]
    out = bytearray()
    for y in range(h):
        for x in range(w):
            i = y * w + x
            r, g, b = work[i]
            qr = round(r * 31 / 255)
            qg = round(g * 63 / 255)
            qb = round(b * 31 / 255)
            out += ((qr << 11) | (qg << 5) | qb).to_bytes(2, "little")
            errs = (r - qr * 255 / 31, g - qg * 255 / 63, b - qb * 255 / 31)
            for dx, dy, f in ((1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    j = ny * w + nx
                    work[j][0] += errs[0] * f
                    work[j][1] += errs[1] * f
                    work[j][2] += errs[2] * f
    return bytes(out)


def alpha4_mask() -> bytes:
    """Mascara A4 (2 px/byte, nibble alto = esquerda) do quadrado arredondado.

    Desenha em 4x e reduz com BOX: cada byte-alvo acumula a cobertura exata
    de um bloco 4x4 -> 16 niveis de anti-aliasing por pixel.
    """
    ss = 4
    big = Image.new("L", (SIZE * ss, SIZE * ss), 0)
    ImageDraw.Draw(big).rounded_rectangle(
        (0, 0, SIZE * ss - 1, SIZE * ss - 1), radius=RADIUS * ss, fill=255
    )
    cov = big.resize((SIZE, SIZE), Image.BOX)
    out = bytearray()
    for y in range(SIZE):
        for xb in range(SIZE // 2):
            hi = cov.getpixel((xb * 2, y)) >> 4
            lo = cov.getpixel((xb * 2 + 1, y)) >> 4
            out.append((hi << 4) | lo)
    return bytes(out)


def process(src_png: Path, dst_bin: Path, mask: bytes) -> None:
    img = Image.open(src_png).convert("RGB")
    # Recorte quadrado central
    side = min(img.size)
    left = (img.width - side) // 2
    top = (img.height - side) // 2
    img = img.crop((left, top, left + side, top + side))
    img = img.resize((SIZE, SIZE), Image.LANCZOS)

    dst_bin.parent.mkdir(parents=True, exist_ok=True)
    dst_bin.write_bytes(rgb565_dither(img) + mask)


def contact_sheet(items: list) -> None:
    cell = 128
    cols = 4
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * cell + 16), THEME_BG)
    d = ImageDraw.Draw(sheet)
    mask = alpha4_mask()
    from PIL import ImageFont
    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14
        )
    except Exception:
        font = ImageFont.load_default()
    for i, item in enumerate(items):
        png = SRC / item["output"]
        if not png.exists():
            continue
        icon = Image.open(png).convert("RGB")
        side = min(icon.size)
        icon = icon.crop((0, 0, side, side)).resize((SIZE, SIZE), Image.LANCZOS)
        icon.putalpha(Image.frombytes("L", (SIZE, SIZE), bytes(
            m for p in mask for m in ((p >> 4) * 17, (p & 0x0F) * 17)
        )))
        bgc = Image.new("RGB", (SIZE, SIZE), THEME_BG)
        icon = Image.alpha_composite(bgc.convert("RGBA"), icon.convert("RGBA"))
        icon = icon.convert("RGB").resize((96, 96), Image.NEAREST)
        x = (i % cols) * cell
        y = (i // cols) * cell
        sheet.paste(icon, (x + (cell - 96) // 2, y + 8))
        d.text((x + cell // 2, y + 112), item["id"], anchor="mm",
               fill=(200, 210, 220), font=font)
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(PREVIEW)
    print(f"preview: {PREVIEW}")


def main() -> None:
    regen = "--regen" in sys.argv
    items = json.loads(MANIFEST.read_text())
    SRC.mkdir(parents=True, exist_ok=True)

    missing = [it for it in items if not (SRC / it["output"]).exists()]
    if regen:
        missing = items
        for it in items:
            p = SRC / it["output"]
            if p.exists():
                p.unlink()
    if missing:
        print(f"gerando {len(missing)} artes com text2d (FLUX.2 Klein)...")
        subprocess.run(
            ["text2d", "generate-batch", str(MANIFEST), "--output-dir", str(SRC),
             "--width", "512", "--height", "512", "--steps", "4"],
            check=True,
        )

    OUT.mkdir(parents=True, exist_ok=True)
    mask = alpha4_mask()  # geometria igual para todos os icones
    for it in items:
        src_png = SRC / it["output"]
        if not src_png.exists():
            print(f"AVISO: {src_png} ausente, pulando {it['id']}")
            continue
        dst = OUT / f"{it['id']}.bin"
        process(src_png, dst, mask)
        print(f"ok  {dst.relative_to(ROOT)}")
    contact_sheet(items)


if __name__ == "__main__":
    main()
