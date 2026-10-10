#!/usr/bin/env python3
"""Arte do Arrasa! (hub_apps/Arrasa) — geracao + masterizacao.

Pipeline (irmao do supernova_sprites.py):
  1. text2d (FLUX.2 Klein local) gera personagens, estilingue e o cenario;
     texture2d (SD1.5 seamless) gera as texturas de material (madeira,
     pedra, gelo) e a terra do chao. Renders nao sao reprodutiveis de
     verdade: os prompts/seeds daqui sao a documentacao. Sem --regen o
     script so masteriza o que ja esta em tools/arrasa_src/.
  2. Pillow:
     - personagens (pedra, goblin, rei): fundo branco LIGADO A BORDA vira
       preto puro (a cor-chave do drawSprite); pretos internos viram
       quase-preto (unblack) para nao furar a arte; crop no conteudo e
       Lanczos para 64x64 — o jogo reduz no load (drawSprite smooth) para o
       tamanho exato do corpo na tela da placa;
     - texturas: recorte + Lanczos para o tamanho de textura do jogo (a
       peca escala a textura para as suas medidas, girada);
     - fundo.png 480x480 = o MUNDO inteiro (320x320 unidades, 1,5 px por
       unidade): ceu e morros do text2d, gramado e terra (texture2d) a
       partir do chao (y=290) e o estilingue plantado em x=52. Paleta de
       128 cores (arte chapada comprime bem; o pacote do hub tem teto).
  3. Imprime as pontas do garfo do estilingue em unidades do mundo (as
     constantes GARFO do mundo.js).

Uso: python3 tools/arrasa_sprites.py [--regen]
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "arrasa_src"
OUT = ROOT / "hub_apps" / "Arrasa"   # FLAT: o pacote do hub nao tem subpastas
ICON = ROOT / "hub_apps" / "Arrasa" / "icon.png"

NO_TEXT = ", no text, no letters, no watermark"
OUTLINE = ", thick dark outline, bold cel shading"
WHITE_BG = ", centered, filling most of the frame, isolated on plain solid white background"

# mundo do jogo (mundo.js): 320 de largura, chao em 290; o fundo cobre
# x 0..320, y 0..320 a 1,5 px por unidade (480 nativo da 4848)
MW, GROUND, PX = 320, 290, 1.5
SLING_X, SLING_H = 52, 60

# text2d: id -> (prompt, seed, (W, H))
ART = {
    "pedra": ("cartoon mobile game projectile character, a single perfectly round grey "
              "granite boulder with an angry face, furrowed thick eyebrows and determined "
              "eyes, small cracks and moss speckles, thick dark outline, bold cel shading"
              + WHITE_BG + NO_TEXT, 7101, (512, 512)),
    "goblin": ("cartoon mobile game enemy character, a single round chubby green goblin "
               "head with big pointy ears, mischievous grin showing two teeth, big glossy "
               "eyes, thick dark outline, bold cel shading, front view" + WHITE_BG + NO_TEXT,
               7202, (512, 512)),
    "rei": ("cartoon mobile game boss character, a single round chubby green goblin king "
            "head wearing a shiny golden crown with red jewels, big pointy ears, smug grin, "
            "bushy eyebrows, thick dark outline, bold cel shading, front view"
            + WHITE_BG + NO_TEXT, 7303, (512, 512)),
    "estilingue": ("cartoon mobile game prop, a giant classic toy slingshot catapult made of "
                   "one wooden stick with exactly two prongs forming a letter Y, straight "
                   "vertical handle, two short prongs angled up and outward, smooth polished "
                   "light brown wood with dark grain, leather wrap on the handle, thick dark "
                   "outline, bold cel shading, front view" + WHITE_BG
                   + ", no rubber band, no tree, no leaves" + NO_TEXT, 7411, (512, 512)),
    "pedrao": ("cartoon mobile game projectile character, a single huge perfectly round dark "
               "reddish brown granite boulder with a furious face, very heavy thick eyebrows, "
               "small mean eyes, deep cracks" + OUTLINE + WHITE_BG + NO_TEXT, 7111, (512, 512)),
    "tripla": ("cartoon mobile game projectile character, a single small perfectly round pale "
               "blue stone with a cheeky excited face, big eyes and open grin, three little "
               "white sparkle marks" + OUTLINE + WHITE_BG + NO_TEXT, 7121, (512, 512)),
    "flecha": ("cartoon mobile game projectile character, a single round bright yellow stone "
               "with a pointy wedge tip on the right side, determined squinting eyes and angry "
               "eyebrows, speed lines style" + OUTLINE + WHITE_BG + NO_TEXT, 7131, (512, 512)),
    "bomba": ("cartoon mobile game projectile character, a single perfectly round black iron "
              "bomb ball with an angry face, red glowing eyes, short lit fuse with spark on top"
              + OUTLINE + WHITE_BG + NO_TEXT, 7141, (512, 512)),
    "chocadeira": ("cartoon mobile game projectile character, a single perfectly round white "
                   "chalk stone with a grumpy hen face, small orange beak, tiny red comb on top"
                   + OUTLINE + WHITE_BG + NO_TEXT, 7151, (512, 512)),
    "bumerangue": ("cartoon mobile game projectile character, a single perfectly round green "
                   "mossy stone with a sly smirking face and a big curved boomerang shaped "
                   "yellow stripe across it" + OUTLINE + WHITE_BG + NO_TEXT, 7161, (512, 512)),
    "ovo": ("cartoon mobile game item, a single white speckled egg with a lit fuse on top, "
            "bomb egg, glossy" + OUTLINE + WHITE_BG + NO_TEXT, 7171, (512, 512)),
    "fundo": ("2D side scrolling mobile physics puzzle game background, cartoon style, "
              "bright cheerful blue sky with soft fluffy clouds, layered rolling green hills "
              "with small trees, a distant purple mountain range, flat open grass meadow in "
              "the lower part, empty foreground with no objects, vivid saturated colors, "
              "clean vector illustration, no characters, no buildings in foreground"
              + NO_TEXT, 7505, (1024, 768)),
}

# texture2d: id -> (prompt, negativo, preset, seed)
TEX = {
    "madeira": ("cartoon wooden plank texture, horizontal wood grain, warm orange brown, "
                "clean stylized mobile game style, bold grain lines", "", "wood", 81),
    "pedra": ("hand painted cartoon grey rock texture, stylized stone surface with a few "
              "large dark cracks and lighter chips, mobile game asset, flat shading",
              "photo, realistic, pebbles, cobblestones, bricks, tiles", "none", 93),
    "vidro": ("hand painted cartoon ice texture, pale cyan frozen surface with white "
              "diagonal highlight streaks and small cracks, mobile game asset, flat shading",
              "photo, realistic, water, bubbles, tiles, grid, mosaic", "none", 94),
    "terra": ("cartoon brown soil dirt texture with small pebbles and roots, side view of "
              "earth, clean stylized mobile game style", "", "dirt", 84),
}

# lado do sprite / diametro do corpo (o arte.js dimensiona o sprite assim)
FATOR = {}

# tamanho final das texturas (px): o jogo escala para a peca
TEX_SIZE = {"madeira": (64, 12), "pedra": (40, 40), "vidro": (40, 40)}


def gerar(regen: bool) -> None:
    SRC.mkdir(parents=True, exist_ok=True)
    for k, (prompt, seed, (w, h)) in ART.items():
        dst = SRC / f"{k}_src.png"
        if dst.exists() and not regen:
            continue
        man = SRC / f"manifest_{k}.json"
        man.write_text(json.dumps([{"id": k, "prompt": prompt, "seed": seed,
                                    "output": dst.name}], indent=1))
        subprocess.run(["text2d", "generate-batch", str(man), "-O", str(SRC),
                        "-W", str(w), "-H", str(h), "-s", "8", "--force"], check=True)
    for k, (prompt, neg, preset, seed) in TEX.items():
        dst = SRC / f"tex_{k}_src.png"
        if dst.exists() and not regen:
            continue
        cmd = ["texture2d", "generate", prompt, "-W", "512", "-H", "512",
               "--seed", str(seed), "-o", str(dst)]
        if neg:
            cmd += ["-n", neg]
        if preset != "none":
            cmd += ["-p", preset]
        subprocess.run(cmd, check=True)


def fundo_branco(img: Image.Image, thr: int = 200) -> np.ndarray:
    """Mascara do fundo: claro LIGADO A BORDA (brancos internos — olhos,
    brilho — nao encostam na borda por causa do contorno escuro)."""
    a = np.asarray(img.convert("RGB"), dtype=np.int32)
    light = a.min(axis=2) >= thr
    bg = np.zeros_like(light)
    bg[0, :] = light[0, :]
    bg[-1, :] = light[-1, :]
    bg[:, 0] = light[:, 0]
    bg[:, -1] = light[:, -1]
    while True:
        g = bg.copy()
        g[1:, :] |= bg[:-1, :]
        g[:-1, :] |= bg[1:, :]
        g[:, 1:] |= bg[:, :-1]
        g[:, :-1] |= bg[:, 1:]
        g &= light
        if (g == bg).all():
            return bg
        bg = g


def recorta(img: Image.Image, mask_bg: np.ndarray, quadrado: bool) -> tuple:
    ys, xs = np.where(~mask_bg)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    if quadrado:
        s = max(x1 - x0, y1 - y0)
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        x0, y0 = cx - s // 2, cy - s // 2
        x1, y1 = x0 + s, y0 + s
    return (int(x0), int(y0), int(x1), int(y1))


def rgba_de(img: Image.Image) -> Image.Image:
    """Arte em fundo branco -> RGBA (alfa 0 no fundo ligado a borda)."""
    bg = fundo_branco(img)
    a = np.asarray(img.convert("RGB"), dtype=np.uint8)
    alpha = np.where(bg, 0, 255).astype(np.uint8)
    rgba = Image.fromarray(np.dstack([a, alpha]), "RGBA")
    # 1 px de erosao na borda: o halo branco do antialias some
    al = rgba.getchannel("A").filter(ImageFilter.MinFilter(3))
    rgba.putalpha(al)
    return so_o_corpo(rgba)


def so_o_corpo(rgba: Image.Image) -> Image.Image:
    """Descarta pedacos soltos pequenos (brilhos, sombra no chao): fica a
    maior ilha de arte e as que tem >= 4% dela (estopim, crista colados)."""
    a = np.asarray(rgba, dtype=np.uint8).copy()
    m = a[:, :, 3] > 0
    lab = np.zeros(m.shape, dtype=np.int32)
    areas = [0]
    h, w = m.shape
    for y0 in range(h):
        for x0 in range(w):
            if not m[y0, x0] or lab[y0, x0]:
                continue
            n = len(areas)
            pilha = [(y0, x0)]
            lab[y0, x0] = n
            cnt = 0
            while pilha:
                y, x = pilha.pop()
                cnt += 1
                for yy, xx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                    if 0 <= yy < h and 0 <= xx < w and m[yy, xx] and not lab[yy, xx]:
                        lab[yy, xx] = n
                        pilha.append((yy, xx))
            areas.append(cnt)
    big = max(areas)
    manter = np.array([ar >= big * 0.04 for ar in areas])
    a[~manter[lab], 3] = 0
    return Image.fromarray(a, "RGBA")


def chaveado(rgba: Image.Image) -> Image.Image:
    """RGBA -> RGB com fundo PRETO PURO (cor-chave) e pretos internos em
    quase-preto (0,0,8): no RGB565 do device so o fundo casa com a chave."""
    a = np.asarray(rgba, dtype=np.uint8).copy()
    rgb = a[:, :, :3]
    alpha = a[:, :, 3]
    preto = (rgb[:, :, 0] < 8) & (rgb[:, :, 1] < 4) & (rgb[:, :, 2] < 8)
    rgb[preto] = (0, 0, 8)
    rgb[alpha < 128] = 0
    return Image.fromarray(rgb, "RGB")


def personagem(k: str, size: int = 64) -> None:
    img = Image.open(SRC / f"{k}_src.png").convert("RGB")
    rgba = rgba_de(img)
    vazio = np.asarray(rgba.getchannel("A")) == 0
    ys, xs = np.where(~vazio)
    dw, dh = int(np.ptp(xs)) + 1, int(np.ptp(ys)) + 1
    lado, corpo = max(dw, dh), min(dw, dh)
    FATOR[k] = round(lado / corpo, 2)
    box = recorta(img, vazio, True)
    rgba = rgba.crop(box).resize((size, size), Image.LANCZOS)
    chaveado(rgba).save(OUT / f"{k}.png", optimize=True)
    print(f"ok  {k}.png ({(OUT / f'{k}.png').stat().st_size} B)")


def textura(k: str) -> None:
    img = Image.open(SRC / f"tex_{k}_src.png").convert("RGB")
    w, h = TEX_SIZE[k]
    if k == "madeira":
        # UMA tabua: a faixa de 512/7 px mais limpa (a textura tem 7 tabuas)
        band = 512 // 7
        img = img.crop((0, band * 2 + 2, 512, band * 3 - 2))
    img = img.resize((w, h), Image.LANCZOS)
    img.save(OUT / f"tex_{k}.png", optimize=True)
    print(f"ok  tex_{k}.png ({(OUT / f'tex_{k}.png').stat().st_size} B)")


def fundo() -> dict:
    W = H = int(MW * PX)                       # 480
    gy = int(GROUND * PX)                      # 435: linha do chao
    art = Image.open(SRC / "fundo_src.png").convert("RGB")
    aw, ah = art.size
    art = art.resize((W, int(ah * W / aw)), Image.LANCZOS)
    # o gramado da arte comeca em ~75% da altura: alinha com o chao
    a = np.asarray(art, dtype=np.int32)
    meadow = int(art.size[1] * 0.75)
    top = gy - meadow
    canvas = Image.new("RGB", (W, H))
    # ceu acima da arte: degradê da cor do topo da arte
    c0 = a[2, :, :].mean(axis=0)
    for y in range(max(0, top)):
        k = y / max(1, top)
        c = (c0 * (0.82 + 0.18 * k)).astype(int)
        canvas.paste(tuple(int(v) for v in c), (0, y, W, y + 1))
    canvas.paste(art, (0, top))
    # chao: labio de grama (da arte) + terra seamless com sombra no topo
    terra = Image.open(SRC / "tex_terra_src.png").convert("RGB").resize((96, 96), Image.LANCZOS)
    lip = 9
    for x in range(0, W, 96):
        for y in range(gy + lip, H, 96):
            canvas.paste(terra, (x, y))
    t = np.asarray(canvas, dtype=np.float32)
    for i in range(10):                        # sombra do gramado sobre a terra
        y = gy + lip + i
        if y < H:
            t[y] *= 0.62 + 0.038 * i
    grama_cor = np.array([118, 196, 52], dtype=np.float32)
    t[gy:gy + lip] = grama_cor * np.linspace(1.12, 0.86, lip)[:, None, None]
    t[gy] = grama_cor * 1.25                   # brilho na borda do gramado
    canvas = Image.fromarray(np.clip(t, 0, 255).astype(np.uint8), "RGB")
    # estilingue plantado em x=52, base 2 unidades abaixo da grama
    est = Image.open(SRC / "estilingue_src.png").convert("RGB")
    rgba = rgba_de(est)
    box = recorta(est, np.asarray(rgba.getchannel("A")) == 0, False)
    rgba = rgba.crop(box)
    sh = int(SLING_H * PX)
    sw = int(rgba.size[0] * sh / rgba.size[1])
    rgba = rgba.resize((sw, sh), Image.LANCZOS)
    sx = int(SLING_X * PX - sw / 2)
    sy = int((GROUND + 2) * PX) - sh
    canvas.paste(rgba, (sx, sy), rgba)
    # pontas do garfo: o pixel mais alto de cada metade do sprite
    al = np.asarray(rgba.getchannel("A")) > 128
    tips = []
    for half in (range(0, sw // 2), range(sw // 2, sw)):
        best = None
        for x in half:
            ys = np.where(al[:, x])[0]
            if len(ys) and (best is None or ys[0] < best[1]):
                best = (x, ys[0])
        tips.append(best)
    garfo = [{"x": round((sx + bx) / PX, 1), "y": round((sy + by + 4) / PX, 1)}
             for bx, by in tips]
    q = canvas.quantize(colors=160, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q.save(OUT / "fundo.png", optimize=True)
    print(f"ok  fundo.png ({(OUT / 'fundo.png').stat().st_size} B)")
    return {"garfo": garfo}


def icone() -> None:
    """64x64 RGBA: a pedra brava sobre um disco de ceu com gramado."""
    s = 256
    disco = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    bg = Image.open(OUT / "fundo.png").convert("RGB").crop((60, 250, 300, 490)).resize((s, s))
    mask = Image.new("L", (s, s), 0)
    from PIL import ImageDraw
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, s - 1, s - 1), radius=56, fill=255)
    disco.paste(bg, (0, 0), mask)
    pedra = Image.open(SRC / "pedra_src.png").convert("RGB")
    rgba = rgba_de(pedra)
    box = recorta(pedra, np.asarray(rgba.getchannel("A")) == 0, True)
    rgba = rgba.crop(box).resize((176, 176), Image.LANCZOS)
    disco.alpha_composite(rgba, (40, 46))
    disco.resize((64, 64), Image.LANCZOS).save(ICON, optimize=True)
    print(f"ok  icon.png ({ICON.stat().st_size} B)")


def main() -> None:
    gerar("--regen" in sys.argv)
    # o ovo-bomba fica procedural no jogo: o pacote do hub tem teto de 16
    # arquivos extras (3 modulos + 13 PNGs ja e o limite)
    for k in ("pedra", "goblin", "rei", "pedrao", "tripla", "flecha", "bomba",
              "chocadeira", "bumerangue"):
        personagem(k)
    for k in TEX_SIZE:
        textura(k)
    info = fundo()
    icone()
    tot = sum(p.stat().st_size for p in OUT.glob("*.png") if p.name != "icon.png")
    print(f"assets: {tot} B no total")
    print("GARFO (mundo.js):", json.dumps(info["garfo"]))
    print("TIRO_ARTE (arte.js, lado/corpo):", json.dumps(FATOR))


if __name__ == "__main__":
    main()
