# Arrasa!

Estilingue contra a fortaleza dos goblins, para [CelerOS](https://os.celer.tec.br)
(ESP32-S3 com PSRAM, JavaScript ES5/Duktape). Física de **corpo rígido nativa**
(API 33, `System.rigid*` via `P.rigid` da `celeros.physics` 1.5): tábuas e
blocos giram, empilham, tombam e quebram pelo impacto; a pedra estica o
elástico de couro, voa e nunca atravessa uma peça.

## Arquivos

| arquivo | vai pro device? | o que é |
|---|---|---|
| `app.json` | sim | manifesto: versão, api 33, deps (engine + physics) |
| `main.js` | sim | cenas, mira por toque, HUD, eventos -> efeitos e som |
| `mundo.js` | sim | física de jogo: dano por impacto, TNT/bomba, tiros, turnos, estrelas |
| `arte.js` | sim | sprites redimensionados para a tela, peças giradas, elástico |
| `niveis.js` | sim | as 10 fases (construtor por empilhamento) |
| `*.png` | sim | arte gerada: personagens, cenário com estilingue, texturas (na RAIZ: o pacote do hub é flat) |
| `test.js` | não | bateria do harness (`celer.js test`, roda no CI) |
| `celer.d.ts`, `celeros.engine.d.ts` | não | tipos para o editor |

A arte sai de `tools/arrasa_sprites.py` (text2d + texture2d locais; fontes
em `tools/arrasa_src/`): `python3 tools/arrasa_sprites.py [--regen]`.

## Como joga

- Arraste a pedra no estilingue para trás e solte; os pontinhos mostram o
  começo da trajetória e o rastro do último tiro fica na tela.
- Tipos de pedra (toque no ar ativa o poder):
  - **pedra**: comum;
  - **pedrão**: pesado, bom contra pedra;
  - **tripla**: racha em 3;
  - **flecha**: dispara reto, 2,4× mais rápida (rasga fileiras);
  - **bomba**: explode (sozinha 1,5 s depois de bater);
  - **chocadeira**: solta um ovo-bomba que cai reto e ela sobe;
  - **bumerangue**: volta num arco e acerta por trás de muros.
- Madeira, gelo e pedra aguentam pancadas diferentes; TNT explode e
  arrasta a vizinhança. Goblins morrem de pancada ou de queda.
- 1 estrela por limpar a fase; 2 e 3 pedem estrago e tiros sobrando
  (cada tiro não usado vale 1000).

## Comandos (dentro do repo CelerOS)

```bash
node tools/sdk/celer.js lint hub_apps/Arrasa
node tools/sdk/celer.js test hub_apps/Arrasa
python3 tools/celerctl.py dev hub_apps/Arrasa
```
