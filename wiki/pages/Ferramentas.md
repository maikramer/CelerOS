# Ferramentas

[English](/maikramer/CelerOS/wiki/Tools) | **Português (BR)**

Utilitários do lado do host (Python/shell) em [tools/](tools/). Instalação:
`pip install -r tools/requirements.txt` (pyserial; `make_icons` também
precisa de Pillow, fora da lista).

| Ferramenta | Papel |
|---|---|
| [celerctl.py](tools/celerctl.py) | Cliente HostLink: controle do dispositivo pela serial — referência completa em [celerctl (USB)](/maikramer/CelerOS/wiki/celerctl-USB) |
| [flash_data.sh](tools/flash_data.sh) | Constrói a imagem LittleFS de `data/` e grava a partição `littlefs` |
| [ota_server.py](tools/ota_server.py) | Servidor local de `update.json` + firmware para testes de OTA |
| [celerhub.py](tools/celerhub.py) | Publica/lista/remove apps no hub (valida as pastas de app; token por args/env; `--hub` ou `CELER_HUB`, default `https://os.celer.tec.br`) |
| [sdk/celer.js](tools/sdk/celer.js) | SDK de apps: scaffold, lint, types para o editor, emulador headless e publicação na loja (Node, zero deps npm) |
| [make_icons.py](tools/make_icons.py) | `icons.json` → `icons_src/*.png` (512 px) → `data/icons/*.png` (64 px, RGB565 ditherizado) |
| [make_splash.py](tools/make_splash.py) | PNG → `main/Assets/SplashLogo.h` (RGB565 sobre o THEME_BG) |

## celerctl — o canivete do dia a dia

```bash
python3 tools/celerctl.py devices            # lista dispositivos
python3 tools/celerctl.py shell              # shell interativo no aparelho
python3 tools/celerctl.py push main.js /local/apps/MeuApp/main.js
python3 tools/celerctl.py logcat --dump      # copia o ring de logs (repetível; --ts/--grep)
python3 tools/celerctl.py debug MeuApp       # debugger JS: breakpoints/step/eval
python3 tools/celerctl.py ota push build/CelerOS.bin   # firmware pela serial
python3 tools/celerctl.py screencap out.png  # captura do framebuffer
python3 tools/celerctl.py tap 120 160        # injeta um toque
```

Ele conversa pela UART do console (CH340, `/dev/ttyUSB0`; `-b` muda o
baud) — ou pela CDC1 em placas com `CELEROS_USB_NATIVE` (watch) e pelo
próprio USB-Serial/JTAG nas placas `CELEROS_LINK_ON_USJ` (dog). Com
firmware proto 2 todo frame carrega um CRC32 e as transferências usam
janela deslizante (negociada no HELLO; firmware antigo segue no caminho
legado — veja [`tools/README_USBTOOL.md`](tools/README_USBTOOL.md)).
Várias placas: `-p` aceita o prefixo de serial USB exibido por `devices`.
Os opcodes vivem em `main/USBDevice/HostLink.h` e a ferramenta lê esse
arquivo por regex: uma fonte da verdade para os dois lados. Não rode
`celerctl` com o `idf.py monitor` segurando a mesma porta.

`logcat --dump` copia o ring buffer sem drená-lo (repetível); `--ts`
prefixa timestamps do host e `--grep` filtra linhas no host. Todo erro de
app é persistido em `/local/lastcrash.txt` (versão do OS, data, uptime,
app + stack completa) e sobrevive a reboot — o `lasterror` do shell
serial mostra tamanho e horário da gravação; o file manager web lê o
arquivo.

`celerctl debug MeuApp` (placas com `CONFIG_CELEROS_JS_DEBUGGER`, default
nos alvos ESP32-S3) sobe um proxy TCP e o REPL do debugger Duktape: o app
pausa no attach (primeira linha) e no throw de um erro não capturado, com
breakpoints, step, eval, watches e `r` (reinicia relendo o `main.js` e
mantendo breakpoints). Referência completa em
[`tools/README_USBTOOL.md`](tools/README_USBTOOL.md).

## celerhub — publicar no hub

```bash
python3 tools/celerhub.py list               # versões locais vs hub
python3 tools/celerhub.py publish hub_apps/2048
```

## celer.js — o SDK de apps

```bash
node tools/sdk/celer.js new MeuApp      # scaffold: app.json + main.js + ícone + types para o editor
node tools/sdk/celer.js lint MeuApp     # checa ES5 + API contra o manifest real do firmware (app_lint)
node tools/sdk/celer.js test MeuApp     # roda o app no harness Node (APIs do aparelho stubadas)
node tools/sdk/celer.js emu MeuApp --frames "0,600,1500"   # emulador: PNG por marco do relógio + diff de pixels
python3 tools/celerctl.py dev MeuApp    # ao vivo no dispositivo (push + relançamento)
node tools/sdk/celer.js publish MeuApp  # publica na loja do hub
```

`lint`/`check` delegam ao `tools/app_lint` (o manifest é derivado do código do
firmware, então o linter conhece a superfície real da API); `dev`/`publish`
delegam ao celerctl/celerhub. O `emu` roda o app headless e tira snapshot
PNG da tela; com `--frames "0,600,1500"` renderiza um PNG por marco do
relógio (`tela-0000.png`, ..., na pasta `.dev` do app) e imprime o diff de
pixels entre frames consecutivos. Zero dependências npm — o acorn é
vendorado e o codificador PNG usa o zlib do Node.

## Flash sem toolchain — CelerOS Flasher

Toda tag `v*` publica, nos
[releases do GitHub](https://github.com/maikramer/CelerOS/releases):

* um zip por placa — firmware + imagem LittleFS + `flash.json` (o plano de
  gravação), e um `README.txt` com a linha `esptool` equivalente para quem
  preferir gravar na mão;
* o **CelerOS Flasher**, um flasher gráfico para Linux e Windows com o
  esptool embutido — baixe ele e o zip da sua placa na mesma pasta, escolha a
  placa, conecte o aparelho e grave. Sem instalar o ESP-IDF.

## Geradores de assets

`make_icons.py` e `make_splash.py` são passos manuais (não fazem parte do
`idf.py build`); os outputs são commitados (`data/icons/`,
`SplashLogo.h`).

Para trocar a arte de um ícone: edite o `tools/icons.json` e/ou apague o
`icons_src/<id>.png` e rode `make_icons.py --regen` — nunca edite
`data/icons/*.png` na mão (um PNG 512 px manual em `icons_src/` é
respeitado como está).
