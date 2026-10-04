<p align="center">
  <img src="Documentation/assets/celeros_logo.png" alt="CelerOS" width="480"/>
</p>

# CelerOS

[English](README.md) | **Português (BR)**

O CelerOS é um sistema operacional com GUI, leve e de código aberto, com
runtime de apps JavaScript, para microcontroladores ESP32. Ele transforma
placas de display baratas em um pequeno dispositivo "tipo smartwatch": UI
immediate-mode sobre LovyanGFX, engine JS (Duktape) rodando apps de forma
isolada a partir da flash ou do SD, App Store com atualização over-the-air e
uma ferramenta companheira via USB (`celerctl`) para o dia a dia de
desenvolvimento.

## Novidades

Destaques da rodada de outubro de 2026 (firmware 1.5, API JS nível 20):

* **IA no aparelho (API 18–20)** — qualquer app pode chamar `AI.chat()` e conversar com a DeepSeek ou a OpenRouter direto do firmware: assíncrono sobre TLS e, com function calling, o modelo responde com `toolCalls` que comandam o dispositivo. As chaves de API nunca tocam a sandbox JS (ficam fora da jail dos apps e são provisionadas com `tools/push_ai_key.py`). O app **Chat IA**, que já vem instalado, é um cliente de chat completo.
* **Voz (API 19)** — `Mic.*` grava WAV de 16 kHz atrás de uma permissão `mic`, em base64 e pronto para modelos multimodais. O app **Qwen** é um assistente de voz push-to-talk: segure para falar, solte e a resposta volta em texto — no watch, no cão robô e em toda placa com touch.
* **Wake word "Hi Celer" (API 20)** — um detector microWakeWord autônomo (TFLite Micro, modelo int8 na flash) escuta no cão robô: diga "hi celer, senta" e ele senta. Comandos de voz para senta/deita/levanta/anda/para em português e inglês, com as pernas portadas e calibradas a partir do ESP-Hi da Espressif (Dog Face 1.5.1).
* **Debugger JavaScript** — `celerctl debug MeuApp` anexa um debugger Duktape de verdade pelo USB: breakpoints (inclusive condicionais), step into/over/out, eval, watches e call stack; o `r` reinicia o app sincronizando só o que mudou no fonte, e há pausa automática em erros não capturados. Ligado por padrão nas placas ESP32-S3.
* **Um relógio que dorme como tal** — o deep sleep agora é automático e, enquanto dorme, uma sentinela ULP-RISC-V vigia o IMU, a bateria e o cabo: o botão PWR responde em ~100 ms e levantar o pulso também acorda. O cão ganhou um watchdog de bateria por ULP só dele.
* **Celer Link na SmartDisplay** — o link BLE entre aparelhos agora também cabe na placa de 4": os pools do host NimBLE foram para a PSRAM (~22 KB de RAM interna livres após o init).
* **Sexta placa: o devkit barebone** — um devkit ESP32 de 4 MB sem tela (LED na placa + botão BOOT, `System.button()`), para apps que são só sensor/atuador. O app **Barebone** é a referência.
* **Desenvolvimento mais feliz** — o último erro de app persiste em `/local/lastcrash.txt` (leia com `lasterror` no shell — sobrevive a reboot), o `celerctl logcat` ganhou timestamps, filtros e um `--dump` não-destrutivo, e o emulador renderiza um PNG por marco do relógio com diff de pixels (`celer.js emu --frames`).

Antes disso, no ciclo da 1.5: **Phone Link** pelo [Gadgetbridge](https://gadgetbridge.org) (notificações, música, clima, chamadas — `Phone.*`, API 15), **plugins de watchface** (`watchface.js`, API 16), a experiência completa de relógio (central de notificações, alarmes, painéis rápidos, seis apps de watch), **APIs de runtime 12–16** (timers, `Storage` por app, HTTP assíncrono, `playTone`/`playWav`, permissões), pareamento do Celer Link por código de 6 dígitos com chave por bond, o **CelerOS Flasher** e o **SDK de apps**.

## Screenshots

| Launcher | App Store | Settings |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-launcher.png" width="260" alt="Launcher"/> | <img src="Documentation/assets/imgs/celeros-appstore.png" width="260" alt="App Store"/> | <img src="Documentation/assets/imgs/celeros-settings.png" width="260" alt="Settings"/> |
| **Terminal (teclado acoplado)** | **Snake** | |
| <img src="Documentation/assets/imgs/celeros-terminal.png" width="260" alt="Terminal"/> | <img src="Documentation/assets/imgs/celeros-snake.png" width="260" alt="Snake"/> | |

*Capturas do framebuffer real de uma SmartDisplay 4" (firmware 1.5) via `celerctl screencap`.*

### Watch Waveshare AMOLED 2.06

| Watchface (tela inicial) | Launcher |
| :---: | :---: |
| <img src="Documentation/assets/imgs/watch-watchface.png" width="220" alt="Mostrador do relógio"/> | <img src="Documentation/assets/imgs/watch-launcher.png" width="220" alt="Launcher do watch"/> |

*O watch boota direto no mostrador (papel de parede, passos, bateria); swipe pra cima abre o launcher. Desde a 1.5 o mostrador também carrega notificações do celular, clima, música e widgets de plugins. Mesmo `celerctl screencap`, pelo USB nativo.*

### CYD (2.8" 320x240, sem PSRAM)

A CYD roda o mesmo firmware e os mesmos apps, mas é uma máquina bem menor que
a SmartDisplay (ESP32 com ~320 KB de RAM e sem PSRAM, painel SPI de 2.8" e
toque resistivo), então a experiência é visivelmente mais simples:

* **Apps esticados.** Os apps JS são desenhados para um canvas 240x320 em
  retrato; no vidro 320x240 em paisagem eles são escalados 1,33x na largura e
  0,75x na altura — texto e formas ficam achatados.
* **Apps podem piscar.** Não há RAM para um quadro fora da tela, então os
  apps JS desenham direto no painel (`System.isBuffered()` é `false`). A UI
  do sistema (launcher, diálogos, barra superior dos apps) é composta em duas
  faixas pequenas e não pisca, mas um app que limpa e redesenha a tela
  inteira a cada evento vai piscar.
* **Mais lenta.** Um redesenho de tela cheia é limitado pelo SPI de 40 MHz
  (~31 ms); apps grandes levam 1–2 s compilando ao abrir (Settings, App Store).
* **Memória apertada.** Um app recebe ~220 KB de RAM interna (parte em IRAM,
  mais lenta); o teto prático é um `main.js` de ~60 KB, e apps grandes levam
  alguns segundos para abrir. Apps maiores aparecem como "Requer PSRAM" na
  loja.
* **Toque resistivo.** Pede um toque mais firme e calibração no primeiro
  boot; os alvos são pequenos (ícones de 48 px, barra superior de 20 px).
  Também dá para operar a tela pelo navegador com o espelho ao vivo.
* **Hardware da placa também em JS.** O LED RGB do verso, o sensor de luz
  (brilho automático) e o conector de alto-falante funcionam via
  `System.led` / `lightLevel` / `beep`.
* **Sem cartão SD** por enquanto (o slot divide o barramento do display).

## Funcionalidades

* **Runtime de apps JavaScript** — apps interativos em ES5 rodam nativamente via Duktape (API nível 20): desenho estilo canvas, toque e teclado na tela acoplado (`System.keypad*`), sistema de arquivos com storage privado por app, timers e rede HTTP/JSON (bloqueante e assíncrona).
* **IA e voz no aparelho** — `AI.chat()` conversa com a DeepSeek ou a OpenRouter de qualquer app (assíncrono, TLS, function calling; as chaves ficam fora da sandbox JS), `Mic.*` grava WAV de 16 kHz atrás de uma permissão `mic`, e os apps **Chat IA** e **Qwen** são um cliente de chat e um assistente de voz push-to-talk. No cão robô, a wake word **"Hi Celer"** roda no próprio aparelho (detector microWakeWord próprio, TFLite Micro) e comandos de voz ("hi celer, senta") dirigem as pernas (`WakeWord.*`, API 20).
* **Debugger JavaScript** — `celerctl debug MeuApp` anexa um debugger Duktape pelo USB: breakpoints (inclusive condicionais), step into/over/out, eval, watches, call stack, reinício com sync do fonte e pausa automática em erros não capturados (alvos ESP32-S3).
* **UI immediate-mode** — layout adaptativo (`main/Display/Layout.h`): os mesmos apps escalam de 240x320 até 480x480, com ícones PNG decodificados para um cache RGB565+A4.
* **Apps de sistema em JS** — Settings, App Store, Installer, Help, Web Server, Terminal, Snake, Chat IA, Qwen e as demos (HTTP Demo, Touch Test) moram na partição LittleFS; o firmware carrega só o core (isso cortou ~330 KB da imagem da CYD). As placas sobrepõem seus apps de casa (Watchface no watch, Dog Face no cão, Barebone no devkit).
* **App Store e Installer** — navegue e instale apps do [CelerOS Hub](https://os.celer.tec.br) via Wi-Fi, ou instale manualmente a partir do cartão SD.
* **Atualização over-the-air** — firmware direto do aparelho (Settings → System Updates), pelo navegador (página de upload `/update`) ou via `celerctl ota push`. Veja [tools/README_OTA.md](tools/README_OTA.md).
* **Configuração de Wi-Fi por portal cativo** — sem credenciais salvas? O aparelho abre o access point `CelerOS-Setup-XXXX` e você configura o Wi-Fi pelo celular. O Wi-Fi reconecta sozinho se o roteador cair.
* **Tela ao vivo no navegador** — `/screen` espelha o display via Wi-Fi (quadros RLE servidos bloco de linhas por bloco, então o aparelho segue fluido) e repassa seus cliques como toques.
* **Hardware da placa em JS** — LED RGB (`System.led`), sensor de luz com brilho automático (`System.lightLevel`), alto-falante (`System.beep`), linhas de relé nas SKUs "Y" da SmartDisplay (`System.relay`), servos (`System.gpio.servo`) e hardware de placa de robô — bateria, microfone, pad capacitivo, NeoPixel (`System.battery`/`micLevel`/`touchPad`/`neopixel`). O watch acrescenta IMU com pedômetro e raise-to-wake (`Sensors.*`), bateria, RTC que segura a hora sem rede e volume de áudio (`System.setVolume`). O devkit sem tela expõe o botão BOOT (`System.button()`, API 17).
* **Energia de smartwatch** — no watch Waveshare a escada de tela dim depois de alguns segundos, cai para um mostrador always-on com anti burn-in e então entra em deep sleep automático, guardado por uma sentinela ULP-RISC-V que vigia os botões, a bateria e o cabo (o PWR acorda em ~100 ms, e levantar o pulso também); o aparelho boota direto no mostrador. O cão robô mantém um watchdog de bateria por ULP enquanto dorme.
* **Celer Link (BLE)** — link Bluetooth LE entre CelerOS próximos (API 9): ponha uma placa num robô e dirija pelo app de outra placa (`CelerLink.scan/connect/send` — o app Celer Remote do hub faz exatamente isso). Desde a API 11 o link pareia com código de 6 dígitos e desafio-resposta com chave por bond. Roda na SmartDisplay, no watch e no cão robô.
* **Phone Link (Gadgetbridge)** — o relógio pareia com o Android via BLE se passando por um Bangle.js (app [Gadgetbridge](https://gadgetbridge.org)): notificações com alerta em tela cheia, controle de música, clima, chamadas recebidas e achar celular (`Phone.*`, API 15).
* **Notificações e alarmes** — central de notificações do sistema (apps disparam toasts/histórico via `System.notify`; no watch uma notificação nova acorda a tela com alerta em tela cheia) e alarmes persistentes com tela de disparo própria (API 15).
* **Plugins de watchface** — apps instalados estendem o mostrador do relógio com linhas de widget (`watchface.js` + `System.launchApp`, API 16); veja a [wiki](https://github.com/maikramer/CelerOS/wiki/Plugins-de-Watchface).
* **Flash sem toolchain** — todo [release no GitHub](https://github.com/maikramer/CelerOS/releases) traz pacotes por placa (firmware + imagem LittleFS) e o **CelerOS Flasher**, um flasher gráfico para Linux/Windows com esptool embutido.
* **SDK de apps** — `tools/sdk/celer.js`: scaffold, lint contra a API real do firmware, types para o editor, emulador headless (snapshots PNG, um PNG por marco do relógio com diff de pixels), dev loop no aparelho e publicação, com zero dependências npm.
* **Companheiro USB `celerctl`** — ferramenta estilo adb pelo link serial: shell interativo, push/pull de arquivos, logcat ao vivo (timestamps, filtros e dump não-destrutivo), o debugger JS, atualização de firmware in-place e screencap. O último erro de app persiste em `/local/lastcrash.txt` (`lasterror` no shell). Veja [tools/README_USBTOOL.md](tools/README_USBTOOL.md).
* **PIN nas Settings** — PIN numérico opcional (SHA-256 com salt, tratado nativamente) protege as Settings, com sessão de desbloqueio de 60 s.
* **Gerenciador web com autenticação** — arquivos, editor de texto e upload de firmware pelo navegador, protegidos por HTTP Basic Auth (senha exibida no app Web Server ou no `celerctl info`).
* **Gerenciador de arquivos** — explorador e editor de texto no LittleFS e no cartão SD.

## Placas Suportadas

| SmartDisplay 4" | CYD |
| :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-home.jpg" width="300" alt="SmartDisplay rodando o CelerOS"/> | <img src="Documentation/assets/imgs/CYD2432S028R.jpg" width="300" alt="CYD"/> |

| Placa | SoC | Display | Toque | Observações |
|---|---|---|---|---|
| **SmartDisplay 4"** (Guition ESP32-S3-4848S040) | ESP32-S3-N16R8 | IPS 4" 480x480 RGB (ST7701) | Capacitivo GT911 | 16 MB flash / 8 MB PSRAM, microSD (`/sd`), alto-falante I2S (NS4168 — `System.beep`); SKUs "Y" de parede com 1 ou 3 relés (`System.relay`) |
| **CYD** (ESP32-2432S028R, "Cheap Yellow Display") | ESP32 | ILI9341 2.8" 240x320 SPI | Resistivo XPT2046 | Variante clássica witnessmenow (TFT no HSPI 14/13/12, toque em pinos dedicados, backlight GPIO21); LED RGB, sensor de luz e alto-falante (GPIO26) em JS; slot SD desligado por enquanto; calibração de toque no primeiro boot |
| **CYD-VSPI** (variante não testada) | ESP32 | ILI9341 2.8" 240x320 SPI | Resistivo XPT2046 | Pinout legado (TFT no VSPI 18/23/19, barramento de toque compartilhado, backlight GPIO22) mantido para placas cabladas assim — **nunca testada no hardware**; build com `-DCELEROS_BOARD=cyd-vspi` |
| **Cão robô** (SpotPear ESP32-S3 AI Robot Dog, ZZPET `zzpet-s3`) | ESP32-S3R8 | OLED SH1106 128x64 de 1,3" (cara) | Pad capacitivo (GPIO10) | 16 MB flash / 8 MB PSRAM embutida, 4 servos de perna, microfone + alto-falante I²S, 2x WS2812, bateria no ADC; boota no app **Dog Face** (olhos expressivos, gaits com rampas e keepalive dead-man); controlado por outra placa CelerOS via Celer Link BLE ([wiki](https://github.com/maikramer/CelerOS/wiki/Robo-Cachorro)) |
| **Watch Waveshare AMOLED 2.06** (ESP32-S3-Touch-AMOLED-2.06) | ESP32-S3R8 | AMOLED redondo 2.06" 410x502 QSPI (CO5300) | Capacitivo FT3168 | 32 MB flash / 8 MB PSRAM embutida, PMU AXP2101, RTC PCF85063 + IMU QMI8658 (pedômetro) + codec ES8311 no I²C, microSD; boota no app **Watchface**; escada de tela com always-on display e deep sleep automático sob uma sentinela ULP-RISC-V (o PWR acorda em ~100 ms); `celerctl` pelo USB nativo ([wiki](https://github.com/maikramer/CelerOS/wiki/Watch-Waveshare)) |
| **Devkit barebone** (qualquer placa ESP32 comum, ex. DOIT DevKit v1) | ESP32 | nenhuma — LED na placa (GPIO2) | Botão BOOT (GPIO0) | 4 MB flash, sem tela: apps rodam headless via `System.button()` + `System.led`; Wi-Fi pelo shell serial; canal OTA `updates/devkit`; boota no app **Barebone** ([wiki](https://github.com/maikramer/CelerOS/wiki/Devkit-Barebone)) |

As definições de placa ficam em `boards/<placa>/` (defaults de sdkconfig) e
`main/Boards/<placa>/` (mapa de pinos e driver de display). Selecione o alvo
com `-DCELEROS_BOARD=<placa>` (os ids da tabela). A UI é adaptativa à resolução, então
adicionar um painel é, na maior parte, um novo perfil de placa.

## Compilando e Gravando

O CelerOS 1.2+ usa **ESP-IDF 6.1 puro** (sem camada Arduino/PlatformIO).

```bash
git clone https://github.com/maikramer/CelerOS.git && cd CelerOS
git submodule update --init          # LovyanGFX
source ~/esp/v6.1/esp-idf/export.sh  # ESP-IDF v6.1 instalado

# SmartDisplay 4" (ESP32-S3)
idf.py -B build -DSDKCONFIG=build/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/smartdisplay/sdkconfig.defaults" \
  -DCELEROS_BOARD=smartdisplay set-target esp32s3
idf.py -B build build flash -p /dev/ttyUSB0 monitor

# CYD (ESP32 clássico)
idf.py -B build-cyd -DSDKCONFIG=build-cyd/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/cyd/sdkconfig.defaults" \
  -DCELEROS_BOARD=cyd set-target esp32
idf.py -B build-cyd build flash -p /dev/ttyUSB0 monitor

# Watch Waveshare AMOLED 2.06 (ESP32-S3, USB-Serial/JTAG nativo)
idf.py -B build-watch -DSDKCONFIG=build-watch/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/waveshare-watch/sdkconfig.defaults" \
  -DCELEROS_BOARD=waveshare-watch set-target esp32s3
idf.py -B build-watch build flash -p /dev/ttyACM0 monitor

# Cão robô SpotPear (ESP32-S3, USB-Serial/JTAG nativo)
idf.py -B build-dog -DSDKCONFIG=build-dog/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/spotpear-dog/sdkconfig.defaults" \
  -DCELEROS_BOARD=spotpear-dog set-target esp32s3
idf.py -B build-dog build flash -p /dev/ttyACM0 monitor

# Devkit barebone (ESP32 clássico, sem tela)
idf.py -B build-devkit -DSDKCONFIG=build-devkit/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/devkit/sdkconfig.defaults" \
  -DCELEROS_BOARD=devkit set-target esp32
idf.py -B build-devkit build flash -p /dev/ttyUSB0 monitor

# Imagem LittleFS de data/ (apps de sistema + ícones + demos)
tools/flash_data.sh smartdisplay /dev/ttyUSB0    # ou: cyd|spotpear-dog|waveshare-watch|devkit <porta>

# Servidor OTA local de testes
python3 tools/ota_server.py --board smartdisplay
```

Os componentes de terceiros (ArduinoJson, esp_littlefs, nlohmann/json) são
baixados pelo component manager do ESP-IDF; o LovyanGFX é um submodule —
clonar com `--recurse-submodules` ou rodar `git submodule update --init`.

## Apps JS e Documentação

* [Wiki](https://github.com/maikramer/CelerOS/wiki) — arquitetura, build, placas, ferramentas e guias (gerada por CI a partir de [`wiki/`](wiki/) no repo).
* [Guia de Desenvolvimento de Apps](Documentation/App_Development_Guide.pt-BR.md) ([in English](Documentation/App_Development_Guide.md)) — como empacotar um app JS (`app.json`, estrutura de pastas, ícones).
* [Guia da API JavaScript](Documentation/JS_API_Guide.pt-BR.md) ([in English](Documentation/JS_API_Guide.md)) — referência completa do runtime JS e dos bindings nativos (inclui AI, Mic e WakeWord).
* [tools/README_USBTOOL.pt-BR.md](tools/README_USBTOOL.pt-BR.md) ([in English](tools/README_USBTOOL.md)) — referência de comandos do `celerctl` e o protocolo do link.
* [tools/README_OTA.pt-BR.md](tools/README_OTA.pt-BR.md) ([in English](tools/README_OTA.md)) — esquema de manifest OTA (`update.json`) e canais de atualização.
* [components/README.md](components/README.md) — componentes auxiliares vendados e patches locais.

Harness desktop para os apps pré-instalados (sem hardware):

```bash
node test/js_harness/run.js
```

## Roadmap

* Mais placas (ajuda com bring-up é bem-vinda — perfis de placa são pequenos e autocontidos; o [devkit barebone](https://github.com/maikramer/CelerOS/wiki/Devkit-Barebone), o [cachorro robô SpotPear](https://github.com/maikramer/CelerOS/wiki/Robo-Cachorro) e o [watch Waveshare AMOLED 2.06](https://github.com/maikramer/CelerOS/wiki/Watch-Waveshare) foram os três últimos a pousar).
* Mais APIs de hardware no runtime JS (sensores I2C/SPI), e voz/wake word em mais placas.

## História e Créditos

O CelerOS começou como um fork do
[KryonOS](https://github.com/Haris16-code/KryonOS), do Haris, e desde então
divergiu bastante: a base Arduino/PlatformIO foi trocada por ESP-IDF 6.1, o
stack gráfico migrou para o LovyanGFX, os apps de sistema foram para
JavaScript e as ferramentas foram reconstruídas em torno do `celerctl` e do
CelerOS Hub. Obrigado, Haris, pelo excelente ponto de partida!

## Licença

O CelerOS é licenciado sob a [GNU General Public License v3.0](./LICENSE).
