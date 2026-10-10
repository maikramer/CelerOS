#!/usr/bin/env python3
"""Sprites do Detona! (hub_apps/Detona) — geracao + masterizacao.

Pipeline (irmao do supernova_sprites.py / make_wallpapers.py):
  1. text2d (FLUX.2 Klein local) gera a arte a partir dos prompts/seeds
     daqui — renders nao sao reprodutiveis de verdade, o script e a
     documentacao (padrao barks_ai.py). texture2d (SD1.5 seamless) gera
     os MATERIAIS dos blocos duros por mundo (textura de verdade).
  2. Pillow masteriza por TIPO de item:
     - sprite (entidades): fundo escuro conectado a borda vira preto
       puro (cor-chave do pushSprite cromatico, API 28), crop por
       conteudo, Lanczos para o tamanho FINAL em pixels nativos (grade
       de celula 32 no canvas 480x480 da 4848), RGB565 + dithering
       Floyd-Steinberg e unblack (0,0,8) — pretos internos solidos.
     - tile (blocos): quadro cheio opaco — sem crop/mascara, so resize
       + dither + unblack (o blit SEMPRE leva cor-chave, entao preto
       puro na arte vira buraco; o unblank protege).
     - texture (duros por mundo): material seamless do texture2d,
       recorte central (bordas do padding circular nao vem tileadas),
       resto igual ao tile.
     - item (powerups): icone text2d recortado sobre uma PLACA 32x32
       desenhada aqui — borda na COR RGB565 EXATA do jogo (round-trip
       pelos bytes expandidos; o firmware re-quantiza ao desenhar) e
       cantos com ALFA 0: o drawPNG da celula compoe o alfa por cima
       do piso (repinta rara: so quando um bicho cruza a celula).
     - wide (titulo): fundo escuro -> preto, resize direto + dither.
     - icon: fundo -> alfa (launcher).
  3. Saida em hub_apps/Detona/assets/*.png + icon.png 64x64 RGBA.

O CHAO da arena continua procedural no jogo (lajotas por celula via rng
do nivel): bench no 4848 mediu drawPNG 32x32 a ~9,4 ms/tile — piso em
arquivo custaria ~24 decodificacoes por quadro (a camada suja repinta
as celulas sob cada entidade). Saida, portais e respiros idem: pulsam
todo quadro, precisam do desenho barato.

Uso: python3 tools/detona_sprites.py [--regen] [--only id ...] [--sheet]
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from make_icons import rgb565_dither  # mesma quantizacao dos icones

SRC = ROOT / "tools" / "detona_src"
OUT = ROOT / "hub_apps" / "Detona" / "assets"
ICON = ROOT / "hub_apps" / "Detona" / "icon.png"

NO_TEXT = ", no text, no letters, no watermark"
STYLE = "vivid 16-bit arcade style, crisp clean edges"
ICON_STYLE = "bold simple shapes readable at small size, single centered object filling most of the frame"

# id -> dict(prompt, seed, gen (W,H), final (w,h), kind, ...)
# kind: sprite | tile | texture | item | wide | icon
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
    "cuspidor": {
        "prompt": (
            "retro arcade enemy sprite, round volcanic rock creature with "
            "cracked dark basalt shell, glowing orange lava mouth wide "
            "open ready to spit fire, small ember eyes, top-down view, "
            "single centered sprite filling most of the frame, on pure "
            "solid black background, " + STYLE + NO_TEXT),
        "seed": 7206, "gen": (512, 512), "final": (30, 30),
        "kind": "sprite",
    },
    "ladrao": {
        "prompt": (
            "retro arcade enemy sprite, sneaky grey raccoon thief with "
            "black eye mask, striped tail, clutching a small bulging beige "
            "loot sack, top-down view, single centered sprite filling most "
            "of the frame, on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7217, "gen": (512, 512), "final": (30, 30),
        "kind": "sprite",
    },
    "chefe": {
        "prompt": (
            "retro arcade boss sprite, huge round bomb-king monster with "
            "golden crown, glowing red eyes, dark iron body with lit fuse "
            "on top, top-down view, single centered sprite filling most of "
            "the frame, on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 6606, "gen": (512, 512), "final": (60, 60),   # slot 2x2: CELL*2-4
        "kind": "sprite",
    },
    # blocos destrutiveis por mundo (1 Jardim / 2 Forno / 3 Nucleo)
    "bloco_macio_1": {
        "prompt": (
            "retro arcade destructible crate block tile, top-down view, "
            "warm wooden box with metal corners, soft green moss patches "
            "on the edges, faint cracks, the tile fills the ENTIRE frame "
            "edge to edge, flat lighting, seamless square texture, " + STYLE + NO_TEXT),
        "seed": 7308, "gen": (512, 512), "final": (32, 32),
        "kind": "tile",
    },
    "bloco_macio_2": {
        "prompt": (
            "retro arcade destructible volcanic block tile, top-down view, "
            "dark cracked magma rock with glowing orange ember veins, the "
            "tile fills the ENTIRE frame edge to edge, flat lighting, "
            "seamless square texture, " + STYLE + NO_TEXT),
        "seed": 7319, "gen": (512, 512), "final": (32, 32),
        "kind": "tile",
    },
    "bloco_macio_3": {
        "prompt": (
            "retro arcade destructible tech crate block tile, top-down "
            "view, dark gunmetal panel box with small rivets, glowing "
            "magenta seam lines, the tile fills the ENTIRE frame edge to "
            "edge, flat lighting, seamless square texture, " + STYLE + NO_TEXT),
        "seed": 7320, "gen": (512, 512), "final": (32, 32),
        "kind": "tile",
    },
    # blocos fixos por mundo: MATERIAL seamless (texture2d/SD1.5)
    "bloco_duro_1": {
        "prompt": (
            "seamless weathered grey granite stone texture, natural rock "
            "surface with subtle carved beveled square tile and soft green "
            "moss in the crevices, flat even lighting, top-down, game "
            "block material"),
        "seed": 7411, "gen": (512, 512), "final": (32, 32),
        "kind": "texture", "preset": "stone",
    },
    "bloco_duro_2": {
        "prompt": (
            "seamless black obsidian basalt texture, dark volcanic glass "
            "rock with faint ember orange cracks, flat even lighting, "
            "top-down, game block material"),
        "seed": 7422, "gen": (512, 512), "final": (32, 32),
        "kind": "texture", "preset": "stone",
    },
    "bloco_duro_3": {
        "prompt": (
            "seamless dark sci-fi armor plating texture, gunmetal metal "
            "panels with rivets and subtle cyan edge glow, flat even "
            "lighting, top-down, game block material"),
        "seed": 7433, "gen": (512, 512), "final": (32, 32),
        "kind": "texture", "preset": "metal",
    },
    # powerups: icone text2d sobre placa na COR RGB565 do corPower() do jogo
    "pw_bomba": {
        "prompt": (
            "retro arcade game item icon, small round black bomb with "
            "short lit fuse and bright spark, " + ICON_STYLE +
            ", on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7501, "gen": (512, 512), "final": (32, 32),
        "kind": "item", "frame": 0x07FF,   # ciano  (+BOMBA)
    },
    "pw_chama": {
        "prompt": (
            "retro arcade game item icon, single bright orange fire flame "
            "with golden core, " + ICON_STYLE +
            ", on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7512, "gen": (512, 512), "final": (32, 32),
        "kind": "item", "frame": 0xFD20,   # laranja (+CHAMA)
    },
    "pw_veloz": {
        "prompt": (
            "retro arcade game item icon, single blue and white sneaker "
            "boot with speed lines, " + ICON_STYLE +
            ", on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7523, "gen": (512, 512), "final": (32, 32),
        "kind": "item", "frame": 0x07E0,   # verde  (VELOCIDADE)
    },
    "pw_chute": {
        "prompt": (
            "retro arcade game item icon, single soccer cleat boot kicking "
            "a small white ball, " + ICON_STYLE +
            ", on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7534, "gen": (512, 512), "final": (32, 32),
        "kind": "item", "frame": 0xF81F,   # magenta (CHUTE)
    },
    "pw_boom": {
        "prompt": (
            "retro arcade game item icon, plunger detonator box with red "
            "handle and small antenna, " + ICON_STYLE +
            ", on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7545, "gen": (512, 512), "final": (32, 32),
        "kind": "item", "frame": 0xF800,   # vermelho (DETONADOR)
    },
    "pw_escudo": {
        "prompt": (
            "retro arcade game item icon, round silver knight shield with "
            "golden rim and shine, " + ICON_STYLE +
            ", on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7556, "gen": (512, 512), "final": (32, 32),
        "kind": "item", "frame": 0xFFFF,   # branco (ESCUDO)
    },
    "pw_vida": {
        "prompt": (
            "retro arcade game item icon, single glossy red heart with "
            "bright highlight, " + ICON_STYLE +
            ", on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7567, "gen": (512, 512), "final": (32, 32),
        "kind": "item", "frame": 0xFFE0,   # ouro (+VIDA)
    },
    "pw_furo": {
        "prompt": (
            "retro arcade game item icon, fiery golden arrow piercing "
            "through a small cracked stone block, " + ICON_STYLE +
            ", on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7578, "gen": (512, 512), "final": (32, 32),
        "kind": "item", "frame": 0xFC9F,   # rosa   (PERFURANTE)
    },
    "pw_relogio": {
        "prompt": (
            "retro arcade game item icon, round pocket clock with white "
            "face and blue rim, hands almost at twelve, " + ICON_STYLE +
            ", on pure solid black background, " + STYLE + NO_TEXT),
        "seed": 7589, "gen": (512, 512), "final": (32, 32),
        "kind": "item", "frame": 0xAFE5,   # azul claro (+30 S)
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


def rgb565_to_rgb888(v: int):
    """Expande um RGB565 nos mesmos bytes que o firmware re-quantiza
    (>>3/>>2 no draw): round-trip exato, a placa chega na cor do jogo."""
    r5, g6, b5 = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def mix888(a, b, pct):
    """Mistura duas cores 888 (0..255) — pct do b."""
    return tuple(int(a[i] + (b[i] - a[i]) * pct / 100.0 + 0.5) for i in range(3))


# ---------------------------------------------------------------------
# Icones de powerup em PIL (desenho direto, sem GPU): o fallback quando
# o text2d nao coube na VRAM — o --regen com GPU livre substitui pelos
# renders. Tudo em 96px, o make_item reduz/dithera como qualquer arte.
# ---------------------------------------------------------------------
def _ico_bomba(d, s):
    # corpo claro (o quase-preto sumia na placa escura) + faísca grande
    c = s // 2
    r = int(s * 0.30)
    d.ellipse([c - r, c - r + s // 12, c + r, c + r + s // 12], fill=(105, 110, 128))
    d.ellipse([c - r + 3, c + 4, c + r - 3, c + r + s // 12], fill=(70, 74, 88))
    d.ellipse([c - r + s // 12, c - r + s // 8, c - r + s // 7, c - r + s // 5],
              fill=(225, 230, 240))
    d.line([c, c - r + s // 10, c + s // 10, c - r - s // 10], fill=(150, 100, 40), width=s // 16)
    for rr, cor in ((s // 12, (255, 140, 0)), (s // 18, (255, 230, 90))):
        d.ellipse([c + s // 10 - rr, c - r - s // 8 - rr, c + s // 10 + rr, c - r - s // 8 + rr],
                  fill=cor)


def _ico_chama(d, s):
    c = s // 2
    d.polygon([(c, s // 8), (c + s // 4, s * 5 // 12), (c + s // 5, s // 2),
               (c + s // 6, s * 7 // 8), (c - s // 6, s * 7 // 8), (c - s // 5, s // 2),
               (c - s // 4, s * 5 // 12)], fill=(255, 90, 0))
    d.polygon([(c, s // 3), (c + s // 8, s * 5 // 8), (c + s // 10, s * 3 // 4),
               (c - s // 10, s * 3 // 4), (c - s // 8, s * 5 // 8)], fill=(255, 200, 0))
    d.ellipse([c - s // 12, s * 5 // 8, c + s // 12, s * 3 // 4], fill=(255, 255, 230))


def _ico_veloz(d, s):
    # tenis: corpo BRANCO gigante com contorno, azul so na sola e no detalhe
    pts = [(s // 7, s // 6), (s // 2, s // 6), (s // 2, s * 5 // 8),
           (s * 6 // 7, s * 5 // 8), (s * 6 // 7, s * 5 // 6), (s // 7, s * 5 // 6)]
    d.polygon(pts, outline=(40, 60, 90), fill=(252, 252, 255))
    d.rectangle([s // 7, s * 11 // 16, s * 6 // 7, s * 5 // 6], fill=(50, 140, 255))
    d.rectangle([s * 5 // 21, s // 3, s * 13 // 42, s * 23 // 48], fill=(50, 140, 255))
    for i in range(2):
        y = s // 3 + i * s // 5
        d.line([s // 28, y, s // 9, y], fill=(150, 220, 255), width=s // 22)


def _ico_boom(d, s):
    # dinamite: 3 bananas vermelhas amarradas, pavios num ponto com faisca
    xs = [s // 5, s * 5 // 12, s * 11 // 20]
    w = s // 6
    topo = s // 3
    fundo = s * 5 // 6
    for x in xs:
        d.rectangle([x, topo, x + w, fundo], fill=(215, 45, 45), outline=(150, 20, 25))
        d.rectangle([x + w // 5, topo + s // 6, x + w - w // 5, topo + s // 5], fill=(245, 245, 250))
        d.line([(x + w // 2), topo, s // 2, s // 6], fill=(120, 80, 35), width=s // 22)
    d.ellipse([s // 2 - s // 14, s // 6 - s // 14, s // 2 + s // 14, s // 6 + s // 14],
              fill=(255, 200, 40))
    d.polygon([(s // 2, s // 18), (s * 19 // 40, s // 6), (s // 2, s * 5 // 36),
               (s * 21 // 40, s // 6)], fill=(255, 245, 150))


def _ico_chute(d, s):
    # bola GRANDE e clara a direita; chuteira pequena a esquerda
    rb = int(s * 0.26)
    bx, by = s - rb - s // 8, s - rb - s // 8
    d.ellipse([bx - rb, by - rb, bx + rb, by + rb], fill=(250, 250, 255), outline=(70, 70, 80))
    for i in range(3):
        a = -0.9 + i * 0.7
        d.polygon([(bx + int(rb * 0.45), by + int(rb * 0.1)),
                   (bx + int(rb * 0.85), by + int(rb * (0.1 + a * 0.5))),
                   (bx + int(rb * 0.5), by + int(rb * 0.45))], fill=(70, 70, 80))
    d.polygon([(s // 10, s // 10), (s * 2 // 5, s // 10), (s * 2 // 5, s // 3),
               (s * 5 // 8, s * 7 // 16), (s * 5 // 8, s * 9 // 16), (s // 10, s * 9 // 16)],
              fill=(170, 95, 40))
    d.rectangle([s // 10, s * 7 // 16, s * 5 // 8, s * 17 // 32], fill=(245, 245, 250))
    for i in range(2):
        x = s // 5 + i * s // 6
        d.rectangle([x, s * 9 // 16, x + s // 20, s * 5 // 8], fill=(245, 245, 250))


def _ico_escudo(d, s):
    d.polygon([(s // 2, s // 8), (s * 7 // 8, s // 4), (s * 13 // 16, s * 5 // 8),
               (s // 2, s * 7 // 8), (s * 3 // 16, s * 5 // 8), (s // 8, s // 4)],
              outline=(255, 210, 60), fill=(205, 215, 230), width=s // 20)
    d.line([s * 3 // 8, s * 3 // 8, s * 3 // 8, s * 5 // 8], fill=(110, 120, 140), width=s // 28)
    d.line([s * 3 // 8, s * 5 // 8, s * 5 // 8, s * 5 // 8], fill=(110, 120, 140), width=s // 28)
    d.line([s * 5 // 16, s // 4, s * 7 // 16, s // 4], fill=(255, 255, 255), width=s // 30)


def _ico_vida(d, s):
    r = s // 4
    d.ellipse([s // 6, s // 5, s // 6 + 2 * r, s // 5 + 2 * r], fill=(235, 35, 45))
    d.ellipse([s - s // 6 - 2 * r, s // 5, s - s // 6, s // 5 + 2 * r], fill=(235, 35, 45))
    d.polygon([(s // 6, s * 2 // 5), (s - s // 6, s * 2 // 5), (s // 2, s * 5 // 6)], fill=(235, 35, 45))
    d.ellipse([s * 3 // 10, s // 4, s * 2 // 5, s * 7 // 20], fill=(255, 160, 160))


def _ico_furo(d, s):
    # seta de fogo HORIZONTAL atravessando o bloco central (ponta grande)
    d.rectangle([s // 5, s // 5, s * 4 // 5, s * 4 // 5], fill=(140, 140, 152))
    d.rectangle([s // 5, s // 5, s * 4 // 5, s * 7 // 20], fill=(172, 172, 186))
    d.line([s * 2 // 5, s * 3 // 5, s * 3 // 5, s * 4 // 5], fill=(85, 85, 96), width=s // 26)
    c = s // 2
    gros = s // 6
    d.rectangle([s // 8, c - gros, s * 11 // 16, c + gros], fill=(255, 175, 0))
    d.polygon([(s * 11 // 16, c - gros - s // 8), (s * 15 // 16, c),
               (s * 11 // 16, c + gros + s // 8)], fill=(255, 120, 0))
    d.rectangle([s // 8, c - gros // 3, s * 5 // 8, c + gros // 3], fill=(255, 235, 100))


def _ico_relogio(d, s):
    c = s // 2
    d.ellipse([s // 6, s // 6, s * 5 // 6, s * 5 // 6], outline=(40, 110, 220), width=s // 12)
    d.ellipse([s // 4, s // 4, s * 3 // 4, s * 3 // 4], fill=(245, 248, 255))
    d.line([c, c, c, s // 3], fill=(60, 60, 70), width=s // 24)
    d.line([c, c, s * 5 // 8, c + s // 8], fill=(60, 60, 70), width=s // 24)
    d.ellipse([c - s // 24, c - s // 24, c + s // 24, c + s // 24], fill=(220, 30, 30))
    d.line([s * 3 // 8, s // 12, s * 5 // 8, s // 12], fill=(40, 110, 220), width=s // 20)


PIL_ICONES = {
    "pw_bomba": _ico_bomba, "pw_chama": _ico_chama, "pw_veloz": _ico_veloz,
    "pw_chute": _ico_chute, "pw_boom": _ico_boom, "pw_escudo": _ico_escudo,
    "pw_vida": _ico_vida, "pw_furo": _ico_furo, "pw_relogio": _ico_relogio,
}


def placa_item(size: int, cor565: int, icone: Image.Image) -> Image.Image:
    """Placa arredondada com borda na COR DO JOGO + icone quantizado no
    centro; cantos (fora do rounded rect) ficam com alfa 0 — o drawPNG
    da celula compoe sobre o piso."""
    cor = rgb565_to_rgb888(cor565)
    fundo = mix888((0, 0, 0), cor, 26)
    borda = mix888(cor, (255, 255, 255), 12)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = max(4, size // 5)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=r, fill=fundo + (255,),
                        outline=borda + (255,), width=2)
    # brilho interno no topo (leitura de "chapa")
    d.rounded_rectangle([3, 3, size - 4, size // 2 - 1], radius=max(2, r - 3),
                        outline=mix888(fundo, (255, 255, 255), 22) + (255,), width=1)
    # icone: quantizado RGB565 (mesmo dither do resto da arte)
    iq = rgb565_dither(icone)
    ia = np.asarray(iq.convert("RGB"), dtype=np.uint8)
    # dentro do icone, preto puro vira alfa 0 (deixa a placa aparecer)
    key = (ia[:, :, 0] == 0) & (ia[:, :, 1] == 0) & (ia[:, :, 2] == 0)
    alpha = np.where(key, 0, 255).astype(np.uint8)
    ic = Image.fromarray(np.dstack([ia, alpha]), "RGBA")
    img.paste(ic, ((size - iq.width) // 2, (size - iq.height) // 2), ic)
    return img


def make_item(src, final, cor565: int, pil_id=None):
    """Item = icone (text2d se houver src; senao pixel-art PIL) sobre a
    placa na cor do jogo, com alfa nos cantos."""
    if src is not None and Path(src).exists():
        img = Image.open(src).convert("RGB")
        img = snap_black(img, 26)
        img = content_square(img, 0.10)
    elif pil_id and pil_id in PIL_ICONES:
        base = 96
        img = Image.new("RGB", (base, base), (8, 8, 10))
        PIL_ICONES[pil_id](ImageDraw.Draw(img), base)
    else:
        return None
    lado = max(2, final[0] - 8)
    img = img.resize((lado, lado), Image.LANCZOS)
    return placa_item(final[0], cor565, img)


def make_texture(src: Path, final) -> Image.Image:
    """Material seamless do texture2d: recorte central (o padding circular
    deixa a moldura menos tileada) + resize + dither + unblack."""
    img = Image.open(src).convert("RGB")
    w, h = img.size
    side = int(min(w, h) * 0.86)
    img = img.crop(((w - side) // 2, (h - side) // 2,
                    (w + side) // 2, (h + side) // 2))
    img = img.resize(final, Image.LANCZOS)
    return unblack(rgb565_dither(img))


def sheet(paths, out: Path) -> None:
    """Folha de contato (QA visual): grade com labels."""
    cell = 96
    cols = 5
    rows = (len(paths) + cols - 1) // cols
    img = Image.new("RGB", (cols * cell, rows * cell + 8), (24, 24, 28))
    d = ImageDraw.Draw(img)
    for i, p in enumerate(paths):
        x, y = (i % cols) * cell, (i // cols) * cell + 8
        try:
            a = Image.open(p).convert("RGB")
        except OSError:
            continue
        a.thumbnail((cell - 12, cell - 26))
        # xadrez de fundo revela o alfa dos itens
        for ty in range(0, a.height, 8):
            for tx in range(0, a.width, 8):
                if (tx // 8 + ty // 8) % 2 == 0:
                    d.rectangle([x + 6 + tx, y + 20 + ty,
                                 x + 6 + min(tx + 8, a.width) - 1,
                                 y + 20 + min(ty + 8, a.height) - 1],
                                fill=(60, 60, 66))
        img.paste(a, (x + 6 + (cell - 12 - a.width) // 2, y + 20))
        d.text((x + 6, y + 4), p.stem, fill=(220, 220, 220))
    img.save(out)


def main() -> None:
    argv = sys.argv[1:]
    regen = "--regen" in argv
    only = []
    if "--only" in argv:
        only = argv[argv.index("--only") + 1:]
    if "--sheet" in argv:
        saidas = sorted(OUT.glob("*.png"))
        sheet(saidas, SRC / "sheet.png")
        print(f"ok  {SRC / 'sheet.png'} ({len(saidas)} assets)")
        return
    todo = {k: v for k, v in ITEMS.items()
            if (regen or not (SRC / f"{k}_src.png").exists())
            and (not only or k in only)}
    # text2d: entidades, tiles, itens e o wide — um manifest so. Se a GPU
    # nao couber (VRAM ocupada), o lote falha inteiro e SEGUE: itens caem
    # no pixel-art PIL, entidades/tiles ficam pro proximo --only/--regen
    texto = {k: v for k, v in todo.items() if v["kind"] != "texture"}
    if texto:
        SRC.mkdir(parents=True, exist_ok=True)
        manifest = SRC / "manifest.json"
        manifest.write_text(json.dumps(
            [{"id": k, "prompt": v["prompt"], "seed": v["seed"],
              "output": f"{k}_src.png"} for k, v in texto.items()], indent=2))
        try:
            subprocess.run(
                ["text2d", "generate-batch", str(manifest), "--output-dir", str(SRC),
                 "--width", "512", "--height", "512", "--steps", "8"],
                check=True)
        except subprocess.CalledProcessError:
            print("aviso: text2d falhou (VRAM?) — itens seguem no pixel-art PIL;"
                  " rode --regen/--only com a GPU livre p/ os renders")
    # texture2d: materiais seamless dos duros
    for k, v in todo.items():
        if v["kind"] != "texture":
            continue
        SRC.mkdir(parents=True, exist_ok=True)
        cmd = ["texture2d", "generate", v["prompt"], "-o", str(SRC / f"{k}_src.png"),
               "--width", "512", "--height", "512", "--seed", str(v["seed"])]
        if v.get("preset"):
            cmd += ["--preset", v["preset"]]
        subprocess.run(cmd, check=True)

    OUT.mkdir(parents=True, exist_ok=True)
    alvo = {k: v for k, v in ITEMS.items() if not only or k in only}
    feitos = []
    for k, it in alvo.items():
        src = SRC / f"{k}_src.png"
        if it["kind"] not in ("item",) and not src.exists():
            continue   # render text2d ainda nao saiu (VRAM): fica pro --regen
        final = it["final"]
        if it["kind"] == "sprite":
            img = Image.open(src).convert("RGB")
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
            img = Image.open(src).convert("RGB").resize(final, Image.LANCZOS)
            unblack(rgb565_dither(img)).save(OUT / f"{k}.png")
        elif it["kind"] == "texture":
            make_texture(src, final).save(OUT / f"{k}.png")
        elif it["kind"] == "item":
            img = make_item(src if src.exists() else None, final, it["frame"], pil_id=k)
            if img is None:
                continue
            img.save(OUT / f"{k}.png")
        elif it["kind"] == "wide":
            img = snap_black(Image.open(src).convert("RGB"), 20)
            img = img.resize(final, Image.LANCZOS)
            rgb565_dither(img).save(OUT / f"{k}.png")
        else:  # icon
            img = snap_black(Image.open(src).convert("RGB"), 26)
            img = content_square(img, 0.06).resize(final, Image.LANCZOS)
            alpha_from_lum(img).save(ICON)
        dst = ICON if it["kind"] == "icon" else OUT / f"{k}.png"
        feitos.append(dst)
        print(f"ok  {dst.relative_to(ROOT)}")
    sheet(sorted(set(feitos) - {ICON}), SRC / "sheet.png")
    print(f"ok  {SRC / 'sheet.png'}")


if __name__ == "__main__":
    main()
