#!/usr/bin/env python3
"""Gera os wallpapers de fundo do CelerOS (relogio watch + SmartDisplay 4").

Pipeline (irmão do make_icons.py):
  1. text2d (FLUX.2 Klein local) gera a arte em 768x1024 (3:4) a partir de
     tools/wallpapers.json — pulando o que ja existe (--regen forc?a).
  2. Pillow: Lanczos para 240x320 (o canvas virtual dos apps — o firmware
     escala para o vidro de cada placa, entao UM arquivo serve em qualquer
     resolucao) + quantizacao RGB565 com dithering Floyd-Steinberg (mesmo
     motivo dos icones: gradiente sem banding).
  3. Saida em tools/wallpapers_out/wallpaper_<id>.png (RGBA opaco).

Instalacao (o wallpaper mora no SD da placa, nao na flash):
  SmartDisplay: python3 tools/celerctl.py -p /dev/ttyUSB0 push <png> /sd/wallpaper.png
  Watch: copiar para o cartao (leitor) como wallpaper.png na raiz do SD.
O app Watchface detecta /sd/wallpaper.png e desenha por cima (drawPNG).
"""
import json
import subprocess
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from make_icons import rgb565_dither  # mesma quantizacao dos icones

MANIFEST = ROOT / "tools" / "wallpapers.json"
SRC = ROOT / "tools" / "wallpapers_src"
OUT = ROOT / "tools" / "wallpapers_out"
W, H = 240, 320
GEN_W, GEN_H = 768, 1024


def main() -> None:
    regen = "--regen" in sys.argv
    items = json.loads(MANIFEST.read_text())
    missing = [it for it in items if regen or not (SRC / it["output"]).exists()]
    if missing:
        SRC.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["text2d", "generate-batch", str(MANIFEST), "--output-dir", str(SRC),
             "--width", str(GEN_W), "--height", str(GEN_H), "--steps", "8"],
            check=True,
        )

    OUT.mkdir(parents=True, exist_ok=True)
    for it in items:
        src_png = SRC / it["output"]
        if not src_png.exists():
            print(f"AVISO: {src_png} ausente, pulando {it['id']}")
            continue
        img = Image.open(src_png).convert("RGB")
        # recorte central para 3:4 e Lanczos para o canvas virtual
        img = img.resize((W, H), Image.LANCZOS)
        img = rgb565_dither(img)
        dst = OUT / f"wallpaper_{it['id']}.png"
        img.convert("RGBA").save(dst)
        print(f"ok  {dst.relative_to(ROOT)}  (copie para o SD como wallpaper.png)")


if __name__ == "__main__":
    main()
