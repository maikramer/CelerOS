# Guia da Game Engine CelerOS

[English](Game_Engine_Guide.md) | **Português (BR)**

Uma game engine 2D completa para apps CelerOS, entregue como dois módulos JS que você vendoriza na pasta do app — sem mudança de firmware, sem permissão extra:

- **`engine.js`** — game loop com cenas, reconhecimento de gestos de toque, desenho com câmera, sprites, partículas, tweens/timers, áudio chiptune com relógio de batida e saves no NVS.
- **`physics.js`** (opcional) — física 2D arcade: círculos/AABB, gravidade, restituição, atrito, sub-passos anti-túnel, tilemaps e corda/pano por Verlet. Matemática pura, zero dependências.

Os dois são ES5 (Duktape) e detectam os recursos do firmware em tempo de execução, então o mesmo jogo roda em toda placa — e sem mudanças no harness/emulador Node.

## 1. Requisitos

| | |
|---|---|
| API do firmware | 23+ (recursos novos — primitivas smooth, `playMusic`, canvas nativo — são auto-detectados) |
| Placas-alvo | As ESP32-S3 com PSRAM: SmartDisplay, watch Waveshare, cão SpotPear |
| Orçamento de tamanho | `engine.js` ≈ 33 KB + `physics.js` ≈ 16 KB. Declare `"requires": ["psram"]` no `app.json` para subir o teto de JS de 48 KB para 128 KB (a loja passa a bloquear install em placa sem PSRAM — que é o que você quer num jogo com engine) |
| Sabor do app | `"topbar": false` para jogos em tela cheia (como o Supernova) é recomendado; o botão de saída mora no menu de título do seu jogo (`System.exitApp()`) |
| CYD (sem PSRAM) | Jogos com engine não cabem no teto de 48 KB. Ou vendorize só o `engine.js` (≈ 33 KB, sobram ~15 KB para o seu código) ou escreva jogos de canvas puro |

## 2. Começando

```bash
# scaffold de jogo pronto pra rodar (app.json + jogo Quica de exemplo + engine):
node tools/sdk/celer.js new MeuJogo --game

# adiciona/atualiza a engine numa pasta de app EXISTENTE:
node tools/sdk/celer.js engine caminho/MeuApp

# itere (lint a cada save; emulador renderiza PNG; device faz live reload):
node tools/sdk/celer.js lint MeuJogo
node tools/sdk/celer.js emu MeuJogo
python3 tools/celerctl.py dev MeuJogo
```

O `new --game` scaffolds o **Quica**, um joguinho completo de manter-a-bola-no-ar (cenas titulo/jogo/fim, raquete por arrasto, bolas com física, partículas, sfx, recorde) — leia o `main.js` dele; é o exemplo canônico.

## 3. Quickstart: um jogo completo em ~30 linhas

```js
// main.js
var E = require("engine");
var P = require("physics");

E.init({ dir: "Bolas", fps: 30, save: "bolas." });
var W = E.W, H = E.H;

var world = P.world({ gravity: { x: 0, y: 300 },
                      bounds: { x: 0, y: 0, w: W, h: H }, walls: "contain" });
var balls = [];

E.run({
  jogo: {
    update: function (dt) {
      if (E.input.tap) {
        balls.push(world.add({ x: E.input.tap.x, y: E.input.tap.y, r: 8,
                               vx: E.m.rand(-120, 120), vy: 0, bounce: 0.85 }));
        E.audio.sfx("ui");
      }
      world.step(dt);
    },
    draw: function () {
      System.fillScreen(E.theme.bg);
      for (var i = 0; i < world.count; i++) {
        var b = world.all[i];
        E.gfx.circle(b.x, b.y, b.r, E.theme.accent);
      }
      E.gfx.text(balls.length + " bolas", W / 2, 8,
                 { align: "center", color: E.theme.textDim, font: 1 });
      E.fx.draw();
    }
  }
}, "jogo");
```

A engine **possui o loop**: você descreve cenas e ela chama `update(dt)`/`draw()` no fps alvo, pollando toque e tickando efeitos/timers/tweens/áudio pra você. Esse loop é seguro contra o exec-timeout (cede todo frame).

## 4. Cenas e o game loop

```js
E.init({ dir: "MeuJogo",  // nome da pasta do app → base de assets (/local/apps/...)
         fps: 30,          // alvo de frames (default 30, 0 = sem teto)
         native: false,    // true = canvas nativo (pixels físicos, API 28)
         save: "meujogo.", // prefixo das chaves NVS do E.save
         particles: 96 }); // tamanho do pool de partículas
E.run(cenas, "titulo");
```

Uma cena é `{ enter, update(dt), draw, exit }` — tudo opcional. `E.goto("fim")` troca no começo do próximo frame (rodando o `exit` da cena antiga antes); `E.quit()` encerra o `E.run()`. Use `E.data` para estado que sobrevive à troca de cena (ele vive pelo app inteiro), e `E.sceneName`/`E.dt`/`E.fps` para introspecção. A cena pode sobrepor o alvo de frames global com `fps` (menus a 30, a ação sem teto com `fps: 0`):

```js
E.run({
  menu:    { fps: 30, update, draw },
  jogando: { fps: 0,  update, draw },   // roda no que a placa der
}, "menu");
```

A cada frame a engine roda, em ordem: troca de cena pendente → dt/fps → poll de input → keep-alive de áudio → câmera → fx → timers → tweens → `update(dt)` → `draw()` → pacing do frame.

**Saindo limpo das cenas:** timers e tweens criados numa cena sobrevivem a ela. Chame `E.clearTimers()`/`E.clearTweens()` no `exit()` (ou cancele handles individuais com `E.cancel(t)`).

## 5. Input

`E.input` é atualizado 1x por frame:

| Campo | Significado |
|---|---|
| `x, y, down` | ponto/estado atual do toque |
| `justDown` / `justUp` | pressionou / soltou neste frame |
| `dx, dy` | movimento desde o frame anterior |
| `holdDx, holdDy` | movimento desde o press (arrasto relativo, ex.: raquete) |
| `moved` | arrasto passou do limiar (`dragThresh`, default 12 px) |
| `tap` | `{x, y}` ao soltar, se curto e (quase) parado (`tapMs` 350 ms) |
| `swipe` | `{dir, dx, dy, dist}` ao soltar, se arrastou ≥ `swipeMin` (30 px) |
| `longpress` | segurou ≥ `longMs` (600 ms) sem arrastar; 1 tiro por toque |
| `btn` | botão físico (devkit): 1 curto, 2 longo |

Os limiares são campos do `E.input` — ajuste por jogo. Hit-test:

```js
if (E.input.tap) { ... }      // tap cru em qualquer lugar
if (E.hit(rect)) { ... }      // tap dentro do {x,y,w,h}
if (E.press(rect)) { ... }    // dedo pressionado dentro agora (highlight de botão)
```

## 6. Desenho (E.gfx)

Todas as funções recebem **coordenadas de mundo** e aplicam a câmera; passe `{screen: true}` para desenhar HUD por cima. Cores são inteiros RGB565 — use `E.theme` (`bg, card, raised, stroke, accent, accentD, onAccent, text, textDim, ok, warn, err`) ou as constantes (`BLACK`, `WHITE`, ...). Tudo cai em primitivas simples em firmware antigo:

```js
E.gfx.rect(x, y, w, h, cor, { fill: true, r: 8 });        // r = cantos arredondados (API 22)
E.gfx.circle(x, y, r, cor, { smooth: true });              // preenchido anti-alias (API 22)
E.gfx.line(x0, y0, x1, y1, cor, { w: 3 });                 // linha grossa (API 22)
E.gfx.tri(x0, y0, x1, y1, x2, y2, cor);
E.gfx.gradient(x, y, w, h, c1, c2, { dir: "y" });
E.gfx.arc(x, y, r0, r1, a0, a1, cor);                      // graus, -90 = cima
E.gfx.text("PONTOS 42", x, y, { size: 2, font: 4, align: "center",
                                valign: "middle", color: 0xFFFF, bg: 0x0000 });
E.gfx.button("JOGAR", x, y, w, h, { primary: true });      // devolve o rect p/ E.hit()
E.gfx.bar(x, y, w, h, 0.75);                               // medidor (vida/carga)
E.gfx.panel(x, y, w, h);                                   // card com borda
```

`bg` omitido no `text` = fundo transparente (chamada de 1 argumento do `setTextColor`). Misturar cores: `System.mixColor(c1, c2, pct)` (API 22).

## 7. Câmera

```js
E.cam.follow(player, 0.15);  // lerp rumo a um alvo {x, y} (centralizado na tela)
E.cam.bounds = { x: 0, y: 0, w: 960, h: 320 };  // clamp dentro do mundo
E.cam.shake(5, 0.3);         // potência (px), duração (s) — decai sozinho
E.cam.center(x, y);          // snap
E.cam.reset();               // identidade (chame no enter da cena)
```

`wx()/wy()` convertem mundo→tela se for desenhar com `System.*` cru.

## 8. Entidades: grupos e pools

```js
var inimigos = E.group();
inimigos.add({ x: 10, y: 10, update: function (dt) { this.x += 20 * dt; },
               draw: function () { E.gfx.circle(this.x, this.y, 6, 0xF800); } });
inimigos.update(E.dt);   // o.dead = true → removido sozinho (swap-pop)
inimigos.draw();

var balas = E.pool(32, function () { return { x: 0, y: 0, vx: 0, vy: 0 }; });
var b = balas.spawn();   // objeto reciclado, ou null quando cheio
b.x = 100; b.y = 200; b.dead = false;
balas.update(E.dt); balas.draw();
```

Pools pré-alocam tudo: zero alocação por frame significa zero engasgo de GC.

## 9. Sprites e animação

O pool de sprites do firmware tem **8 slots** em PSRAM desde a API 29 (`System.spriteSlots()` informa o limite da placa; a engine 1.1 consulta sozinha). O `E.spr.load` decodifica cada PNG **uma vez** para dentro de um slot e guarda um `paint` procedural de fallback, então o jogo roda mesmo sem os assets ou em placa sem sprites:

```js
E.spr.load([
  { name: "nave", file: "nave", w: 72, h: 72,
    paint: function (w, h, x, y) {           // fallback: desenha com primitivas
      E.gfx.tri(x + w / 2, y, x, y + h, x + w, y + h, E.theme.accent);
    } },
  { name: "inimigo", file: "inimigo", w: 56, h: 56, paint: ... },
]);
E.spr.blit("nave", x, y, { cx: true, cy: true, key: 0x0000 });  // key = cor-chave
```

- Os assets são sondados em `E.spr.bases` (o `init({dir})` monta `/local/apps/<dir>/assets/` e `/sd/apps/<dir>/assets/`).
- **Convenção de transparência:** masterize os PNGs com fundo *preto puro* (a cor-chave) e transforme os pretos internos em quase-preto `(0,0,8)` — o mesmo truque do Supernova. Só sprite em slot aceita a chave; o painter desenha o que você mandar.
- Mais defs que slots → os excedentes viram painter automaticamente. `E.spr.backed(name)` diz se o sprite ganhou slot real (blit com cor-chave rápido) ou vai desenhar pelo painter. `E.caps.slots` guarda o limite sondado (8 nas placas PSRAM com API 29, 4 em firmware velho) — orce suas defs contra ele.

Flipbook sem slot extra: os frames são painters (ou nomes de sprite carregado):

```js
var boom = E.anim([function (x, y) { ... }, function (x, y) { ... }], 12, false);
// por frame: boom.tick(E.dt); if (!boom.done) boom.draw(x, y);
```

## 10. FX

```js
E.fx.burst(x, y, { n: 14, colors: [0xFFE0, 0xFD20], speed: 120, life: 0.6,
                   grav: 200, shape: "dot" });   // dot | spark | ring
E.fx.ring(x, y, { speed: 700, color: 0xFFE0 });  // onda de choque (círculo que expande)
E.fx.popText(x, y, "+10", { color: 0xFFE0 });    // pontuação flutuante
E.fx.flash(0xFFFF, 150);                         // flash de tela cheia
var estrelas = E.fx.stars(60, { colors: [0x39E7, 0xC5F9] });  // parallax (guarde a ref)
estrelas.update(E.dt); estrelas.draw();
E.fx.draw();                                     // chame no FIM do draw da cena
```

Partículas/floaters/flash são tickados pelo loop; só o `draw()` é seu.

## 11. Tweens e timers

Tickados pela engine (não são `setTimeout` — sem limite de 8, determinísticos no harness):

```js
E.tween(botao, { y: 160 }, 500, { ease: E.m.outBack, onDone: function () {} });
E.after(1200, function () { E.goto("fim"); });
var id = E.every(2.5 * 1000, novaOnda);
E.cancel(id); E.clearTimers(); E.clearTweens();
```

Easings: `E.m.linear/inQuad/outQuad/inOutQuad/outBack`.

## 12. Áudio

Um slot exclusivo de alto-falante no device: `playMusic` OU `playWav`/`playTone` por vez. O `E.audio` cuida da música e mantém os sfx educados:

```js
E.audio.music({ bpm: 132, loops: 0, tracks: [
  { wave: "sq",  vol: 70, notes: [[69,4],[71,4],[72,4],[69,4]] },   // [midi, semicolcheias]
  { wave: "tri", vol: 60, drum: true, notes: [[36,2],[0,2],[38,2],[0,2]] },
]});
E.audio.beat();        // posição na batida (1.0 = tempo forte); -1 sem música
E.audio.sfx("coin");   // sfx nomeado (pula enquanto a música toca; sfxOverMusic=true sobrepõe)
E.audio.sfx([880, 60]);           // ou [freq, ms] cru
E.audio.sfx([[660,60],[880,80]]); // ou melodia curta (BLOQUEANTE — mantenha curta)
E.audio.stop(); E.audio.mute(true); E.audio.volume(80);
E.audio.duck(600);  // abafa a trilha por ms (um sfx alto rouba o canal);
                    // a engine retoma do ponto onde parou — e o sfx
                    // destrava na janela (playing() esta falso)
```

A música reinicia sozinha quando os loops acabam (keep-alive). Spawnar na batida: `if (Math.floor(E.audio.beat()) !== ultimaBatida) spawna()`. Tabela de sfx default: `ui, ok, back, bad, hit, coin, boom` — troque entradas no `E.audio.sfxTable`. Placas sem alto-falante (CYD) não fazem nada.

## 13. Save (recordes, ajustes)

NVS (sem permissão), com namespace pelo prefixo `save` do `init`:

```js
E.save.set("skin", "azul");
var skin = E.save.get("skin", "verde");
var recorde = E.save.num("recorde", 0);
if (E.save.best("recorde", pontos)) { /* novo recorde! */ }
```

## 14. Matemática e RNG

`E.m`: `clamp, lerp, map, rand(a,b), randInt, pick, dist, dist2, ang, approach, wrap, sign` + easings. `E.rng(seed)` devolve uma função PRNG determinística — seede a geração do nível e seus testes ficam reprodutíveis.

## 15. Física (`physics.js`, opcional)

`require("physics")` — matemática pura, nenhuma chamada a `System`, então testa unitariamente em qualquer lugar. Coordenadas: y cresce para **baixo** (tela); o `x, y` do corpo é o **centro**; corpo circular tem `r`, caixa `w/h`.

```js
var P = require("physics");
var w = P.world({ gravity: { x: 0, y: 900 },
                  bounds: { x: 0, y: 0, w: 240, h: 320 },
                  walls: "contain" });          // contain | wrap | none

var bola = w.add({ x: 120, y: 40, r: 8, bounce: 0.8, friction: 0.1 });
var raquete = w.add({ x: 120, y: 300, w: 64, h: 10, static: true });

w.step(dt);   // 1x por frame, depois do input
```

Opções do corpo: `vx, vy, ax, ay, gravity` (multiplicador), `bounce` (0..1), `friction` (0..1), `drag`, `mass`, `static`, `sensor` (só evento), `group`/`mask` (bitmask: o par colide quando `a.mask & b.group && b.mask & a.group`), `tiles: false` (ignora o tilemap), `drop` (atravessa plataformas one-way), `onCollide(me, other, info{nx,ny,overlap})` (1 tiro por par por step) e `grounded` (apoiado em algo).

Objetos rápidos são **sub-passados** automaticamente — nada atravessa parede fina (`world.maxSub` limita o trabalho, default 8). Bordas: `"contain"` (clamp + quique), `"wrap"` (borda estilo Pac-Man) ou `"none"`.

### Tilemaps (platformer)

```js
var grid = [
  "............",
  "..==...==...",
  "............",
  "####...####.",
];
var tiles = P.tiles(grid, 16, 16);   // '#' sólido, '=' one-way (customize em opts)
w.addTiles(tiles);
tiles.tileAt(px, py); tiles.setTile(col, row, "#");
```

Resolução por eixo, `grounded` no pouso, plataforma one-way só pega quem cai de cima (`body.drop = true` para atravessar de propósito).

### Jogos de grade (top-down)

O mesmo tilemap conduz movimento estilo Bomberman/Zelda: `gravity: {x:0, y:0}` e a resolução por eixo para o corpo nas paredes nas quatro direções, deslizando no eixo livre.

```js
var w = P.world({ gravity: {x:0, y:0} });
w.addTiles(P.tiles(grid, 32, 32, {
  solid: function (ch) { return ch === "#" || ch === "%" || ch === "B"; }
}));
var heroi = w.add({ x: 48, y: 48, r: 11 });  // r ~ 1/3 da célula desliza bem
heroi.vx = 90; heroi.vy = 0;                 // sete pelo input a cada frame
```

Sete `vx/vy` pelo input a cada frame; `bounce` 0 (default) para seco na parede. A "ajuda de canto" clássica (conduzir para o corredor aberto quando raspar numa quina) fica no jogo: bloqueado no eixo do movimento e desalinhado menos que ~40% da célula no outro eixo, esterça para o centro da lane.

### Campos de fluxo (IA de perseguição)

`P.flow(tiles, cx, cy, opts)` inunda um campo de distância BFS a partir de uma célula (tipicamente a do jogador). Perseguidores leem `next()` e descem o gradiente — sem A* por corpo.

```js
var flow = P.flow(tiles, colDoJogador, linhaDoJogador);  // recompute ao mudar o labirinto ou ~2x/s
var passo = flow.next(colDoInimigo, linhaDoInimigo);     // {c, r} uma célula mais perto, ou null
```

`opts.passable(ch)` troca a andabilidade (um fantasma que atravessa bloco macio passa o próprio predicado); células inalcançáveis leem `Infinity`. Custo O(células) por recompute, determinístico e independente de ordem.

### Corda / pano / softbody (Verlet)

```js
var pts = []; for (var i = 0; i < 8; i++) pts.push({ x: 120, y: 30 + i * 8 });
var corda = P.verlet({ points: pts, sticks: [{ a: 0, b: 1 }, ...], iterations: 4 });
corda.pin(0);
corda.step(dt);      // opts: gravity, damp, bounds, bounce
```

### Receitas

- **Platformer:** herói = corpo AABB (`friction` 1, `bounce` 0); mova setando `vx`; pule quando `grounded`; a câmera dá `follow` no herói.
- **Breakout:** raquete = corpo `static` que você reposiciona; bola = círculo com `bounce: 1`; tijolos = caixas que você `remove()` ao acertar (ou tilemap + `setTile`).
- **Shooter top-down:** `gravity: {x:0, y:0}`, `drag` para dar atrito; inimigos/balas em pools; corpos `sensor` para pickups.
- **Bomberman:** top-down + tiles com `solid` custom (bloco macio e bomba viva inclusos); bombas, labaredas e saída são estado da grade, não corpos; perseguidores seguem um campo `P.flow`; a explosão anda pela grade para fora a partir do centro, um bloco macio de profundidade.

## 16. Canvas nativo (tela cheia, API 28)

`E.init({ native: true })` troca desenho/toque para os **pixels físicos do vidro** (ex.: 480×480 no SmartDisplay em vez do 240×320 escalado) — mais nítido e rápido para jogos fullscreen, mas as formas deixam de ser escaladas uniformemente. Exige `"topbar": false` no `app.json` (sem isso o pedido falha graciosamente e `E.caps.native` fica false — sempre ramifique por ele). `E.W/E.H` refletem o modo ativo.

## 17. Testando seu jogo

O harness roda seu jogo sem tela, com relógio virtual — física, engine e tudo:

```js
// MeuJogo/test.js (dev-only, nunca publicado)
module.exports.wire = function (env) {
  env.__harness.tap(120, 178);                       // aperta JOGAR
  env.setTimeout(function () {
    if (typeof __harness !== "undefined") { /* o app expôs hooks no __harness */ }
  }, 2000);
};
```

```bash
node tools/sdk/celer.js test MeuJogo       # rodada headless
node tools/sdk/celer.js emu MeuJogo --ms 2000 --out tela.png
node tools/sdk/celer.js emu MeuJogo --frames 0,600,1500   # PNG por marco + diff de pixels
```

No app, exponha um hook de introspecção tipo `if (typeof __harness !== "undefined") __harness.meuJogo = { state: ... };` e dirija tudo deterministicamente (veja o padrão do test.js do Supernova).

## 18. Tamanho e performance

- Engine+física+jogo passam de 48 KB → mantenha `"requires": ["psram"]`. Tamanhos atuais: `engine.js` ≈ 33 KB, `physics.js` ≈ 16 KB.
- **Zero alocação por frame**: use `E.pool`, remoção swap-pop e reúso de objetos. Um `new`/`[...]` por frame por entidade é o que dispara pausa de GC.
- `fillScreen` + redesenho total a 30 fps dá conta nas placas S3; para poucos objetos em movimento prefira apagar-só-o-velho (erase da posição antiga, desenha a nova).
- `world.step` é O(n²) no número de corpos no pior caso, mas um **sweep-and-prune** (corpos ordenados por x a cada sub-passo, corte cedo pela distância em x) o mantém quase linear em cenas espalhadas — shooters com ~40 corpos rodam folgados.
- Melodias do `E.audio.sfx` são **bloqueantes** (`playTone`): mantenha abaixo de ~300 ms.
- Rajada de JS puro acima de ~1 s esbarra no exec-timeout do firmware — o loop do `E.run` cede todo frame, então fique dentro dele.

## 19. Cola de API

| Chamada | Faz |
|---|---|
| `E.init(opts)` / `E.run(cenas, primeira)` | detecta caps / possui o loop |
| `E.goto(nome)` / `E.quit()` | troca de cena / sai do loop |
| `E.input`, `E.hit(r)`, `E.press(r)` | estado do toque, tap no rect, pressão no rect |
| `E.gfx.*`, `E.cam` | desenho com câmera, scroll/shake/follow |
| `E.group()`, `E.pool(n, f)` | listas de entidades, reciclagem pré-alocada |
| `E.spr.load/blit`, `E.anim(frames, fps)` | sprites PNG com painter de fallback, flipbooks |
| `E.fx.burst/popText/flash/stars/draw` | juice |
| `E.tween/after/every/cancel/clear*` | tempo tickado pela engine |
| `E.audio.music/beat/sfx/stop/mute/volume` | chiptune + sfx educado |
| `E.save.get/set/num/best` | persistência NVS |
| `E.m.*`, `E.rng(seed)` | matemática, RNG determinístico |
| `P.world/add/step`, `P.tiles`, `P.verlet`, `P.hit` | física arcade (módulo opcional) |

O autocomplete do editor vem no scaffold: `engine.d.ts` (engine + física) junto do `celer.d.ts` (API do firmware).
