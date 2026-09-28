# Engine & runtime — notas técnicas (F4)

Avaliação data-driven dos dois pontos abertos da Fase 4: **ES2015+** e
**acentos nas fontes**. Escrita depois da implementação de permissões /
`FS.appData()` / `System.toast` / `System.beep` / i18n nativo.

## ES2015+: QuickJS vs Duktape

O runtime atual é **Duktape 2.7** (ES5.1 + TypedArrays; `Promise`,
`let`/`const`, arrow functions e template literals não existem e **não há
flag de compile que os habilite** — são limitação da implementação). O
validador do hub (`tools/celerhub.py`) já avisa sobre sintaxe ES6 em `main.js`.

Trocar para **QuickJS** (ES2020: `let`/`const`, arrows, classes, `Promise`,
`async`/`await`, módulos) é viável (port ESP32 maduro) mas muda o contrato
inteiro da camada nativa:

| critério | Duktape 2.7 (hoje) | QuickJS |
|---|---|---|
| binário (estimado) | ~450 KB no link atual | ~600–700 KB (+150–250 KB) |
| heap por app | ~90 KB (custom allocator PSRAM) | precisa re-implementar o alocador com reserva interna |
| bindings | 127 `duk_push_c_function` | reescrita completa (JS_SetPropertyFn / classdefs) |
| CYD (slot 1,75 MB) | folga ~260 KB | folga some ou aperta — exigiria `-Os` agressivo |
| GC p/ `System.delay` | mark-and-sweep dirigido | `JS_ExecutePendingJob` + `JS_RunGC` (modelo diferente) |
| isolamento por app | heap destruído por app (funciona bem) | mesmo desenho é possível (runtime por app) |

**Decisão recomendada:** manter Duktape enquanto o alvo CYD (4 MB) estiver
no conjunto de placas suportadas; reavaliar quando (a) a CYD sair de linha
ou (b) o `main.js` > 48 KB virar gargalo real dos apps. O caminho
intermediário — transpilar ES6→ES5 no publish do hub (sucrase/esbuild no
`celerhub.py`) — entrega ES2015 **para quem escreve app** sem tocar no
firmware; ficou anotado como evolução do hub (repo CelerOS-Server).

## Acentos nas fontes

As fontes builtin 1/2 do LovyanGFX (compat Adafruit) são ASCII — acentos
saem como glifos vazios. O LGFX já embute a família **u8g2** com Latin
completo (por ex. `lgfx::v1::fonts::lgfx_minifont` / `Wendl610`), pagando
~10–30 KB por fonte. Caminho sem tocar no firmware de cada app:

1. expor uma fonte UTF-8 pequena como `System.setFont(3)` no runtime
   (`JSBindings::js_setTextSize` ganha o caso 3 → `setTextFont(&lgfx_minifont)`);
2. apps escrevem acentuado nela; os docs de app passam a recomendar ASCII
   nas fontes 1/2.

Não foi ligado nesta fase porque as strings de UI dos apps estão em
português sem acentos por convenção (e o custo de binário na CYD); a
infraestrutura (i18n do firmware + `System.toast`) já cobre o caso nativo.

## O que a F4 entregou no fim

- `permissions` no `app.json` filtrando `FS`/`Net`/`System.gpio` e as
  chamadas perigosas de `System` (default: tudo — compat).
- `FS.appData()` — pasta privada por `packageName`.
- `System.toast(msg)` e `System.beep(freq, ms)` (LEDC no `speakerPin` do
  BoardProfile; ambas as placas atuais saem com `-1` até buzzer existir).
- i18n do firmware nativo (`Utils/I18n.h`, `System.setting("lang")`).
- Background/services: adiado deliberadamente — depende da `CELEROS_APP_TASK`
  amadurecer em campo; `System.toast` é a fundação de UI para notificações.
