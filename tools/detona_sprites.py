#!/usr/bin/env python3
"""Sprites do Detona! (hub_apps/Detona) — geracao + masterizacao.

Pipeline (irmao do supernova_sprites.py / make_wallpapers.py):
  1. text2d (FLUX.2 Klein local) gera a arte a partir dos prompts/seeds
     daqui — renders nao sao reprodutiveis de verdade, o script e a
     documentacao (padrao barks_ai.py).
  2. Pillow masteriza por TIPO de item:
     - sprite (entidades): fundo escuro conectado a borda vira preto
       puro (cor-chave do pushSprite cromatico, API 28), crop por
       conteudo, Lanczos para o tamanho FINAL em pixels nativos (grade
       de celula 32 no canvas 480x480 da 4848), RGB565 + dithering
       Floyd-Steinberg e unblack (0,0,8) — pretos internos solidos.
     - tile (blocos macio/duro): quadro cheio opaco — sem crop/mascara,
       so resize + dither + unblack (o blit SEMPRE leva cor-chave, entao
       preto puro na arte vira buraco; o unblank protege).
     - wide (titulo): fundo escuro -> preto, resize direto + dither.
     - icon: fundo -> alfa (launcher).
  3. Saida em hub_apps/Detona/assets/*.png + icon.png 64x64 RGBA.

Chao da arena, powerups e porta de saida sao procedurais no jogo
(lajotas por celula via rng do nivel) — aqui so entra o que e arte.

Uso: python3 tools/detona_sprites.py [--regen]
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from make_icons import rgb565_dither  # mesma quantizacao dos icones

SRC = ROOT / "tools" / "detona_src"
OUT = ROOT / "hub_apps" / "Detona" / "assets"
ICON = ROOT / "hub_apps" / "Detona" / "icon.png"

NO_TEXT = ", no text, no letters, no watermark"
STYLE = "vivid 16-bit arcade style, crisp clean edges"

# id -> dict(prompt, seed, gen (W,H), final (w,h), kind, alpha)
# kind: sprite | tile | wide | icon
ITEMS = {
    "jogador": {
        "prompt": (
            "retro arcade top-down game hero sprite, cute round bomber "
            "character with white helmet and blue visor, small pink cheeks, "
            "white and cyan suit, seen from directly above, single centered "
            "sprite filling most of the frame, on pure solid black "
            "background, " + STYLE + NO_TEXT),
        "seed": 6101, "gen": (512, 512), "final": (30, 30),
        "kind": "sprite",
    },
    "bomba": {
        "prompt": (
            "retro arcade cartoon bomb sprite, perfect glossy black sphere "
            "with bright highlight, short rope fuse with glowing orange "
            "spark on top, top-down centered, single sprite filling most of "
            "the frame, on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 6202, "gen": (512, 512), "final": (28, 28),
        "kind": "sprite",
    },
    "balao": {
        "prompt": (
            "retro arcade enemy sprite, cute round pink balloon creature "
            "with sleepy droopy eyes and tiny stubby fins, top-down view, "
            "single centered sprite filling most of the frame, on pure "
            "solid black background, " + STYLE + NO_TEXT),
        "seed": 6303, "gen": (512, 512), "final": (30, 30),
        "kind": "sprite",
    },
    "fantasma": {
        "prompt": (
            "retro arcade enemy sprite, small teal ghost with soft glowing "
            "body, mischievous grin, wavy tail, top-down view, single "
            "centered sprite filling most of the frame, on pure solid black "
            "background, " + STYLE + NO_TEXT),
        "seed": 6404, "gen": (512, 512), "final": (30, 30),
        "kind": "sprite",
    },
    "perseguidor": {
        "prompt": (
            "retro arcade enemy sprite, angry orange spiky slime creature "
            "with sharp eyebrows and hungry yellow eyes, top-down view, "
            "single centered sprite filling most of the frame, on pure "
            "solid black background, " + STYLE + NO_TEXT),
        "seed": 6505, "gen": (512, 512), "final": (30, 30),
        "kind": "sprite",
    },
    "chefe": {
        "prompt": (
            "retro arcade boss sprite, huge round bomb-king monster with "
            "golden crown, glowing red eyes, dark iron body with lit fuse "
            "on top, top-down view, single centered sprite filling most of "
            "the frame, on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 6606, "gen": (512, 512), "final": (92, 92),
        "kind": "sprite",
    },
    "bloco_macio": {
        "prompt": (
            "retro arcade destructible crate block tile, top-down view, "
            "warm wooden box with metal corners and faint cracks, the tile "
            "fills the ENTIRE frame edge to edge, flat lighting, seamless "
            "square texture, " + STYLE + NO_TEXT),
        "seed": 6707, "gen": (512, 512), "final": (32, 32),
        "kind": "tile",
    },
    "bloco_duro": {
        "prompt": (
            "retro arcade indestructible stone block tile, top-down view, "
            "heavy grey granite slab with beveled edges and subtle carved "
            "details, the tile fills the ENTIRE frame edge to edge, flat "
            "lighting, seamless square texture, " + STYLE + NO_TEXT),
        "seed": 6808, "gen": (512, 512), "final": (32, 32),
        "kind": "tile",
    },
    "titulo": {
        "prompt": (
            "wide retro arcade menu backdrop, top-down view of a grey "
            "cobblestone arena floor with clearly visible tile seams and a "
            "warm orange explosion glow with rising sparks in the top half, "
            "one small glossy cartoon bomb with lit golden fuse near the "
            "center, rich contrast, bottom third darker and empty for menu "
            "buttons" + NO_TEXT),
        "seed": 6909, "gen": (1024, 512), "final": (480, 230),
        "kind": "wide",
    },
    "icon": {
        "prompt": (
            "arcade game app icon, round glossy black cartoon bomb with "
            "bright lit golden fuse spark, small cyan visor hero helmet "
            "reflected on the bomb surface, dark background, vivid neon "
            "colors, bold simple shapes readable at small size" + NO_TEXT),
        "seed": 7010, "gen": (512, 512), "final": (64, 64),
        "kind": "icon",
    },
}


def snap_black(img: Image.Image, thr: int) -> Image.Image:
    """Funde o fundo escuro em preto puro."""
    a = np.asarray(img.convert("RGB"), dtype=np.uint8).copy()
    lum = a.max(axis=2)
    a[lum < thr] = 0
    return Image.fromarray(a, "RGB")


def content_square(img: Image.Image, pad: float) -> Image.Image:
    """Crop no conteudo (bbox de luminancia) + moldura quadrada com folga."""
    a = np.asarray(img.convert("RGB"), dtype=np.uint8)
    lum = a.max(axis=2)
    ys, xs = np.where(lum > 14)
    if len(xs) == 0:
        return img
    x0, x1 = xs.min(), xs.max()
    y0, y1 = ys.min(), ys.max()
    w, h = x1 - x0 + 1, y1 - y0 + 1
    side = int(max(w, h) * (1 + pad))
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    half = side // 2
    W, H = img.size
    x0 = max(0, min(cx - half, W - side))
    y0 = max(0, min(cy - half, H - side))
    return img.crop((x0, y0, x0 + side, y0 + side))


def alpha_from_lum(img: Image.Image) -> Image.Image:
    """Fundo preto -> alfa (icone do launcher sobre o tema)."""
    a = np.asarray(img.convert("RGB"), dtype=np.uint8)
    lum = a.max(axis=2).astype(np.int32)
    alpha = np.clip(lum * 3, 0, 255).astype(np.uint8)
    return Image.fromarray(np.dstack([a, alpha]), "RGBA")


def unblack(img: Image.Image) -> Image.Image:
    """Preto puro -> quase-preto (0,0,8): o preto e a COR-CHAVE do
    pushSprite transparente (API 28). Vale para sprite E tile — o blit
    sempre leva a chave, preto puro na arte viraria buraco."""
    a = np.asarray(img.convert("RGB"), dtype=np.uint8).copy()
    m = (a[:, :, 0] == 0) & (a[:, :, 1] == 0) & (a[:, :, 2] == 0)
    a[m, 2] = 8
    return Image.fromarray(a, "RGB")


def bg_mask_of(img: Image.Image, thr: int) -> Image.Image:
    """Mascara L do fundo: regiao escura CONECTADA a borda. Pretos
    internos ficam de fora — o unblack os torna solidos."""
    a = np.asarray(img.convert("RGB"), dtype=np.uint8)
    dark = a.max(axis=2) < thr
    bg = np.zeros_like(dark)
    bg[0, :] = dark[0, :]
    bg[-1, :] = dark[-1, :]
    bg[:, 0] = dark[:, 0]
    bg[:, -1] = dark[:, -1]
    while True:
        grown = bg.copy()
        grown[1:, :] |= bg[:-1, :]
        grown[:-1, :] |= bg[1:, :]
        grown[:, 1:] |= bg[:, :-1]
        grown[:, :-1] |= bg[:, 1:]
        grown &= dark
        if (grown == bg).all():
            return Image.fromarray((bg * 255).astype(np.uint8), "L")
        bg = grown


def main() -> None:
    regen = "--regen" in sys.argv
    todo = {k: v for k, v in ITEMS.items()
            if regen or not (SRC / f"{k}_src.png").exists()}
    if todo:
        SRC.mkdir(parents=True, exist_ok=True)
        manifest = SRC / "manifest.json"
        manifest.write_text(json.dumps(
            [{"id": k, "prompt": v["prompt"], "seed": v["seed"],
              "output": f"{k}_src.png"} for k, v in todo.items()], indent=2))
        subprocess.run(
            ["text2d", "generate-batch", str(manifest), "--output-dir", str(SRC),
             "--width", "512", "--height", "512", "--steps", "8"],
            check=True)

    OUT.mkdir(parents=True, exist_ok=True)
    for k, it in ITEMS.items():
        img = Image.open(SRC / f"{k}_src.png").convert("RGB")
        final = it["final"]
        if it["kind"] == "sprite":
            # cor-chave: fundo (escuro ligado a borda) vira preto puro; o
            # resto da arte (incl. pretos internos) segue solido
            bg = bg_mask_of(img, 26)
            a = np.asarray(img, dtype=np.uint8).copy()
            a[np.asarray(bg) > 0] = 0
            img = content_square(Image.fromarray(a, "RGB"), 0.06)
            img = img.resize(final, Image.LANCZOS)
            bg = bg.resize(final, Image.NEAREST)
            img = unblack(rgb565_dither(img))
            ia = np.asarray(img.convert("RGB"), dtype=np.uint8).copy()
            ia[np.asarray(bg) > 0] = 0
            Image.fromarray(ia, "RGB").save(OUT / f"{k}.png")
        elif it["kind"] == "tile":
            img = img.resize(final, Image.LANCZOS)
            unblack(rgb565_dither(img)).save(OUT / f"{k}.png")
        elif it["kind"] == "wide":
            img = snap_black(img, 20)
            img = img.resize(final, Image.LANCZOS)
            rgb565_dither(img).save(OUT / f"{k}.png")
        else:  # icon
            img = snap_black(img, 26)
            img = content_square(img, 0.06).resize(final, Image.LANCZOS)
            alpha_from_lum(img).save(ICON)
        dst = ICON if it["kind"] == "icon" else OUT / f"{k}.png"
        print(f"ok  {dst.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
