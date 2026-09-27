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

| SmartDisplay 4" (ESP32-S3, 480x480) | CYD (ESP32, 240x320) |
| :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-home.jpg" width="300" alt="Home do CelerOS"/> | <img src="Documentation/assets/imgs/CYD2432S028R.jpg" width="300" alt="CYD"/> |

## Funcionalidades

* **Runtime de apps JavaScript** — apps interativos em ES5 rodam nativamente via Duktape (API nível 5): desenho estilo canvas, toque e teclado na tela acoplado (`System.keypad*`), sistema de arquivos e rede HTTP/JSON.
* **UI immediate-mode** — layout adaptativo (`main/Display/Layout.h`): os mesmos apps escalam de 240x320 até 480x480, com ícones PNG decodificados para um cache RGB565+A4.
* **Apps de sistema em JS** — Settings, App Store, Installer, Help, Web Server, Terminal, Calculator e Snake moram na partição LittleFS; o firmware carrega só o core (isso cortou ~330 KB da imagem da CYD).
* **App Store e Installer** — navegue e instale apps do [CelerOS Hub](https://os.celer.tec.br) via Wi-Fi, ou instale manualmente a partir do cartão SD.
* **Atualização over-the-air** — firmware direto do aparelho (Settings → System Updates), pelo navegador (página de upload `/update`) ou via `celerctl ota push`. Veja [tools/README_OTA.md](tools/README_OTA.md).
* **Configuração de Wi-Fi por portal cativo** — sem credenciais salvas? O aparelho abre o access point `CelerOS-Setup-XXXX` e você configura o Wi-Fi pelo celular. O Wi-Fi reconecta sozinho se o roteador cair.
* **Companheiro USB `celerctl`** — ferramenta estilo adb pelo link serial: shell interativo, push/pull de arquivos, logcat ao vivo, atualização de firmware in-place e screencap. Veja [tools/README_USBTOOL.md](tools/README_USBTOOL.md).
* **PIN nas Settings** — PIN numérico opcional (hash MD5) protege as Settings, com sessão de desbloqueio de 60 s.
* **Gerenciador de arquivos** — explorador e editor de texto no LittleFS e no cartão SD.

## Placas Suportadas

| Placa | SoC | Display | Toque | Observações |
|---|---|---|---|---|
| **SmartDisplay 4"** (Guition ESP32-S3-4848S040) | ESP32-S3-N16R8 | IPS 4" 480x480 RGB (ST7701) | Capacitivo GT911 | 16 MB flash / 8 MB PSRAM, opção de USB nativo |
| **CYD** (ESP32-2432S028R, "Cheap Yellow Display") | ESP32 | ILI9341 2.8" 240x320 SPI | Resistivo XPT2046 | Serial CH340; pede calibração de toque no primeiro boot |

As definições de placa ficam em `boards/<placa>/` (defaults de sdkconfig) e
`main/Boards/<placa>/` (mapa de pinos e driver de display). Selecione o alvo
com `-DCELEROS_BOARD=smartdisplay|cyd`. A UI é adaptativa à resolução, então
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

# Imagem LittleFS de data/ (apps de sistema + ícones + demos)
tools/flash_data.sh smartdisplay /dev/ttyUSB0    # ou: cyd <porta>

# Servidor OTA local de testes
python3 tools/ota_server.py --board smartdisplay
```

Os componentes de terceiros (ArduinoJson, esp_littlefs, nlohmann/json) são
baixados pelo component manager do ESP-IDF; o LovyanGFX é um submodule —
clonar com `--recurse-submodules` ou rodar `git submodule update --init`.

## Apps JS e Documentação

* [Guia de Desenvolvimento de Apps](Documentation/App_Development_Guide.pt-BR.md) ([in English](Documentation/App_Development_Guide.md)) — como empacotar um app JS (`app.json`, estrutura de pastas, ícones).
* [Guia da API JavaScript](Documentation/JS_API_Guide.pt-BR.md) ([in English](Documentation/JS_API_Guide.md)) — referência completa do runtime JS e dos bindings nativos (API nível 5).
* [tools/README_USBTOOL.pt-BR.md](tools/README_USBTOOL.pt-BR.md) ([in English](tools/README_USBTOOL.md)) — referência de comandos do `celerctl` e o protocolo do link.
* [tools/README_OTA.pt-BR.md](tools/README_OTA.pt-BR.md) ([in English](tools/README_OTA.md)) — esquema de manifest OTA (`update.json`) e canais de atualização.
* [components/README.md](components/README.md) — componentes auxiliares vendados e patches locais.

Harness desktop para os apps pré-instalados (sem hardware):

```bash
node test/js_harness/run.js
```

## Roadmap

* Mais placas (ajuda com bring-up é bem-vinda — perfis de placa são pequenos e autocontidos).
* Mais APIs de hardware no runtime JS (Bluetooth, sensores I2C/SPI, gerenciamento de energia mais fino).

## História e Créditos

O CelerOS começou como um fork do
[KryonOS](https://github.com/Haris16-code/KryonOS), do Haris, e desde então
divergiu bastante: a base Arduino/PlatformIO foi trocada por ESP-IDF 6.1, o
stack gráfico migrou para o LovyanGFX, os apps de sistema foram para
JavaScript e as ferramentas foram reconstruídas em torno do `celerctl` e do
CelerOS Hub. Obrigado, Haris, pelo excelente ponto de partida!

## Licença

O CelerOS é licenciado sob a [GNU General Public License v3.0](./LICENSE).
