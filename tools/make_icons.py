#!/usr/bin/env python3
"""Gera o pacote de icones 64x64 do KryonOS.

Pipeline:
  1. text2d (FLUX.2 Klein local) gera as artes 512x512 em tools/icons_src/
     a partir de tools/icons.json — pulando o que ja existe (use --regen
     para forcar).
  2. Pillow po-processa: recorte quadrado, Lanczos para 64x64, cantos
     arredondados preenchidos com a cor de fundo do tema e conversao para
     RGB565 little-endian em data/icons/<id>.bin.
  3. Gera a folha de contato docs/assets/icons_preview.png.

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
# Mesma cor de RGB888 do Theme::BG (src/Display/Theme.h)
THEME_BG = (13, 17, 23)


def rgb565_bytes(img: Image.Image) -> bytes:
    px = img.convert("RGB")
    out = bytearray()
    for r, g, b in px.getdata():
        out += ((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)).to_bytes(2, "little")
    return bytes(out)


def process(src_png: Path, dst_bin: Path) -> None:
    img = Image.open(src_png).convert("RGB")
    # Recorte quadrado central
    side = min(img.size)
    left = (img.width - side) // 2
    top = (img.height - side) // 2
    img = img.crop((left, top, left + side, top + side))
    img = img.resize((SIZE, SIZE), Image.LANCZOS)

    # Cantos arredondados preenchidos com o fundo do tema (blit opaco sem
    # alpha no firmware; corners == BG funde com o card)
    mask = Image.new("L", (SIZE, SIZE), 0)
    d = ImageDraw.Draw(mask)
    d.rounded_rectangle((0, 0, SIZE - 1, SIZE - 1), radius=RADIUS, fill=255)
    bg = Image.new("RGB", (SIZE, SIZE), THEME_BG)
    img = Image.composite(img, bg, mask)

    dst_bin.parent.mkdir(parents=True, exist_ok=True)
    dst_bin.write_bytes(rgb565_bytes(img))


def contact_sheet(items: list) -> None:
    cell = 128
    cols = 4
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * cell + 16), THEME_BG)
    d = ImageDraw.Draw(sheet)
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
        mask = Image.new("L", (SIZE, SIZE), 0)
        ImageDraw.Draw(mask).rounded_rectangle(
            (0, 0, SIZE - 1, SIZE - 1), radius=RADIUS, fill=255
        )
        bgc = Image.new("RGB", (SIZE, SIZE), THEME_BG)
        icon = Image.composite(icon, bgc, mask).resize((96, 96), Image.NEAREST)
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
    for it in items:
        src_png = SRC / it["output"]
        if not src_png.exists():
            print(f"AVISO: {src_png} ausente, pulando {it['id']}")
            continue
        dst = OUT / f"{it['id']}.bin"
        process(src_png, dst)
        print(f"ok  {dst.relative_to(ROOT)}")
    contact_sheet(items)


if __name__ == "__main__":
    main()
