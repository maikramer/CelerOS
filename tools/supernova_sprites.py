#!/usr/bin/env python3
"""Sprites do Supernova (hub_apps/Supernova) — geracao + masterizacao.

Pipeline (irmao do make_wallpapers.py):
  1. text2d (FLUX.2 Klein local) gera a arte a partir dos prompts/seeds
     daqui — renders nao sao reprodutiveis de verdade, o script e a
     documentacao (padrao barks_ai.py).
  2. Pillow: fundo para PRETO puro (o blit do sprite no jogo e opaco —
     preto some no espaco), crop por conteudo com folga, aspect-pad para
     quadrado quando pedido, Lanczos para o tamanho FINAL em pixels
     NATIVOS (o jogo roda no canvas 480x480 da 4848, API 28) e quantizacao
     RGB565 com dithering Floyd-Steinberg (mesma dos wallpapers/icones).
  3. Saida em hub_apps/Supernova/assets/*.png + icon.png 64x64 RGBA
     (fundo vira alfa para o launcher).

Uso: python3 tools/supernova_sprites.py [--regen]
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from make_icons import rgb565_dither  # mesma quantizacao dos icones

SRC = ROOT / "tools" / "supernova_src"
OUT = ROOT / "hub_apps" / "Supernova" / "assets"
ICON = ROOT / "hub_apps" / "Supernova" / "icon.png"

NO_TEXT = ", no text, no letters, no watermark"

# id -> (prompt, seed, gen (W,H), final (w,h) ou None=manter aspecto, alpha)
ITEMS = {
    "nave": (
        "retro arcade space shooter player ship sprite, top-down view, nose "
        "pointing up, sleek chrome hull with cyan neon trim and two glowing "
        "engine exhausts, single centered sprite filling most of the frame, "
        "on pure solid black background, crisp clean edges, vivid 16-bit "
        "arcade style" + NO_TEXT,
        4101, (512, 512), (72, 72), False),
    "inimigo": (
        "retro arcade space shooter enemy drone sprite, top-down view, "
        "aggressive crimson and magenta alien interceptor with glowing pink "
        "core, angular wings, single centered sprite filling most of the "
        "frame, on pure solid black background, crisp clean edges, vivid "
        "16-bit arcade style" + NO_TEXT,
        4202, (512, 512), (56, 56), False),
    "olho": (
        "retro arcade space shooter enemy sentinel sprite, top-down view, "
        "floating alien eye orb with violet carapace armor plates and "
        "glowing green iris, single centered sprite filling most of the "
        "frame, on pure solid black background, crisp clean edges, vivid "
        "16-bit arcade style" + NO_TEXT,
        4303, (512, 512), (56, 56), False),
    "orbe": (
        "retro arcade power-up sprite, one perfectly round glowing sphere of "
        "golden plasma energy, radially symmetric ball of light with a "
        "bright white hot core and subtle energy rays, single centered "
        "sphere filling most of the frame, on pure solid black background, "
        "crisp clean edges, vivid 16-bit arcade style, not a chess piece, "
        "not a pawn" + NO_TEXT,
        4414, (512, 512), (44, 44), False),
    "titulo": (
        "wide dark space arcade menu backdrop, mostly empty deep black "
        "space with a faint violet and teal nebula glow in the top half, "
        "one tiny chrome spaceship with cyan engine trail flying upward "
        "near the center, very dark and minimal, bottom third almost pure "
        "solid black and completely empty" + NO_TEXT,
        4515, (1024, 512), (480, 230), False),
    "icon": (
        "arcade game app icon, glowing golden supernova star explosion with "
        "a tiny cyan spaceship silhouette flying away, dark space "
        "background, vivid neon colors, bold simple shapes readable at "
        "small size" + NO_TEXT,
        4606, (512, 512), (64, 64), True),
}


def snap_black(img: Image.Image, thr: int) -> Image.Image:
    """Funde o fundo escuro em preto puro (o blit opaco some no espaco)."""
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
    # alfa sobe rapido: o que nao e quase-preto ja e arte
    alpha = np.clip(lum * 3, 0, 255).astype(np.uint8)
    rgba = np.dstack([a, alpha])
    return Image.fromarray(rgba, "RGBA")


def unblack(img: Image.Image) -> Image.Image:
    """Preto puro -> quase-preto (0,0,8): o preto e a COR-CHAVE do
    pushSprite transparente (API 28) — o fundo some no blit, mas pixels
    escuros INTERIORES da arte nao podem virar buraco. O azul +8 e o bit
    menos significativo do RGB565: invisivel no vidro, solido no blit."""
    a = np.asarray(img.convert("RGB"), dtype=np.uint8).copy()
    m = (a[:, :, 0] == 0) & (a[:, :, 1] == 0) & (a[:, :, 2] == 0)
    a[m, 2] = 8
    return Image.fromarray(a, "RGB")


def bg_mask_of(img: Image.Image, thr: int) -> Image.Image:
    """Mascara L do fundo: regiao escura CONECTADA a borda (dilatacao a
    partir das bordas restrita ao escuro). Pretos internos (sombras sob o
    casco, carcaça do olho) NAO encostam na borda e ficam de fora — e o
    unblack os torna solidos depois."""
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
        manifest.write_text(__import__("json").dumps(
            [{"id": k, "prompt": v[0], "seed": v[1], "output": f"{k}_src.png"}
             for k, v in todo.items()], indent=2))
        subprocess.run(
            ["text2d", "generate-batch", str(manifest), "--output-dir", str(SRC),
             "--width", "512", "--height", "512", "--steps", "8"],
            check=True)

    OUT.mkdir(parents=True, exist_ok=True)
    for k, (prompt, seed, gen, final, is_alpha) in ITEMS.items():
        img = Image.open(SRC / f"{k}_src.png").convert("RGB")
        if k == "titulo":
            img = snap_black(img, 20)
            img = img.resize(final, Image.LANCZOS)
            img = rgb565_dither(img)
            img.save(OUT / f"{k}.png")
        elif is_alpha:
            img = snap_black(img, 26)
            img = content_square(img, 0.06).resize(final, Image.LANCZOS)
            alpha_from_lum(img).save(ICON)
        else:
            # sprite com COR-CHAVE: fundo (escuro ligado a borda) vira preto
            # puro; o resto da arte (incl. pretos internos) segue solido —
            # o unblack final garante que so o fundo casa com a chave.
            bg = bg_mask_of(img, 26)
            a = np.asarray(img.convert("RGB"), dtype=np.uint8).copy()
            a[np.asarray(bg) > 0] = 0
            img = Image.fromarray(a, "RGB")
            img = content_square(img, 0.06).resize(final, Image.LANCZOS)
            bg = bg.resize(final, Image.NEAREST)
            img = unblack(rgb565_dither(img))
            ia = np.asarray(img.convert("RGB"), dtype=np.uint8).copy()
            ia[np.asarray(bg) > 0] = 0
            Image.fromarray(ia, "RGB").save(OUT / f"{k}.png")
        dst = ICON if is_alpha else OUT / f"{k}.png"
        print(f"ok  {dst.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
