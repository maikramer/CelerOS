# Ferramentas

Utilitários do lado do host (Python/shell) em [tools/](tools/). Instalação:
`pip install -r tools/requirements.txt` (pyserial; `make_icons` também
precisa de Pillow, fora da lista).

| Ferramenta | Papel |
|---|---|
| [celerctl.py](tools/celerctl.py) | Cliente CelerLink: controle do dispositivo pela serial — referência completa em [celerctl (USB)](/maikramer/CelerOS/wiki/celerctl-USB) |
| [flash_data.sh](tools/flash_data.sh) | Constrói a imagem LittleFS de `data/` e grava a partição `littlefs` |
| [ota_server.py](tools/ota_server.py) | Servidor local de `update.json` + firmware para testes de OTA |
| [celerhub.py](tools/celerhub.py) | Publica/lista/remove apps no hub (valida as pastas de app; token por args/env; `--hub` ou `CELER_HUB`, default `https://os.celer.tec.br`) |
| [make_icons.py](tools/make_icons.py) | `icons.json` → `icons_src/*.png` (512 px) → `data/icons/*.png` (64 px, RGB565 ditherizado) |
| [make_splash.py](tools/make_splash.py) | PNG → `main/Assets/SplashLogo.h` (RGB565 sobre o THEME_BG) |

## celerctl — o canivete do dia a dia

```bash
python3 tools/celerctl.py devices            # lista dispositivos
python3 tools/celerctl.py shell              # shell interativo no aparelho
python3 tools/celerctl.py push main.js /local/apps/MeuApp/main.js
python3 tools/celerctl.py logcat --dump      # ring buffer de logs
python3 tools/celerctl.py ota push build/CelerOS.bin   # firmware pela serial
python3 tools/celerctl.py screencap out.png  # captura do framebuffer
python3 tools/celerctl.py tap 120 160        # injeta um toque
```

Ele conversa pela UART do console (CH340, `/dev/ttyUSB0`; `-b` muda o
baud) — ou pela CDC1 em placas com `CELEROS_USB_NATIVE`. Os opcodes vivem
em `main/USBDevice/CelerLink.h` e a ferramenta lê esse arquivo por regex:
uma fonte da verdade para os dois lados. Não rode `celerctl` com o
`idf.py monitor` segurando a mesma porta.

## celerhub — publicar no hub

```bash
python3 tools/celerhub.py list               # versões locais vs hub
python3 tools/celerhub.py publish hub_apps/2048
```

## Geradores de assets

`make_icons.py` e `make_splash.py` são passos manuais (não fazem parte do
`idf.py build`); os outputs são commitados (`data/icons/`,
`SplashLogo.h`).

Para trocar a arte de um ícone: edite o `tools/icons.json` e/ou apague o
`icons_src/<id>.png` e rode `make_icons.py --regen` — nunca edite
`data/icons/*.png` na mão (um PNG 512 px manual em `icons_src/` é
respeitado como está).
