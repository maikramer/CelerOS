# Placas suportadas

[English](/maikramer/CelerOS/wiki/Supported-Boards) | **Português (BR)**

| SmartDisplay 4" | CYD |
| :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-home.jpg" width="300" alt="SmartDisplay rodando o CelerOS"/> | <img src="Documentation/assets/imgs/CYD2432S028R.jpg" width="300" alt="CYD"/> |

| Placa | SoC | Display | Toque | Observações |
|---|---|---|---|---|
| **SmartDisplay 4"** (Guition ESP32-S3-4848S040) | ESP32-S3-N16R8 | IPS 4" 480x480 RGB (ST7701) | Capacitivo GT911 | 16 MB flash / 8 MB PSRAM, microSD, alto-falante I2S (NS4168); SKUs "Y" com relés |
| **CYD** (ESP32-2432S028R, "Cheap Yellow Display") | ESP32 | ILI9341 2.8" 320x240 SPI | Resistivo XPT2046 | Sem PSRAM; serial CH340; pede calibração de toque no primeiro boot; UI mais simples ([veja abaixo](#cyd-esp32-clássico)) |
| **CYD-VSPI** (variante não testada) | ESP32 | ILI9341 2.8" 320x240 SPI | Resistivo XPT2046 | Pinout legado (TFT no VSPI 18/23/19, barramento de toque compartilhado, backlight GPIO22) mantido para placas cabladas assim — **nunca testada no hardware**; build com `-DCELEROS_BOARD=cyd-vspi` |
| **Cão robô** (SpotBear/ZZPET `zzpet-s3`) | ESP32-S3R8 (8 MB PSRAM embutida) | OLED SH1106 128x64 de 1,3" (cara) | Pad capacitivo (GPIO10) | 4 servos (pernas), microfone + alto-falante I²S, 2x WS2812, bateria no ADC; abre o app Dog Face no boot (`homeApp` do perfil); controlado pelo app Celer Remote via Celer Link BLE; build com `-DCELEROS_BOARD=spotpear-dog` — veja [Robô cachorro](/maikramer/CelerOS/wiki/Robo-Cachorro) |

## Onde a placa é definida

* `boards/<placa>/sdkconfig.defaults` — defaults de sdkconfig por alvo.
* `main/Boards/<placa>/` — mapa de pinos, driver de display e `BoardTraits.h`
  (diferenças de compilação, ex.: `largeUi`, `hasPsram`).
* A seleção acontece na build com `-DCELEROS_BOARD=smartdisplay|cyd`
  (qualquer outro valor é erro fatal no CMake). Veja
  [Compilando e gravando](/maikramer/CelerOS/wiki/Compilando-e-Gravando).

A UI é adaptativa à resolução — tudo é desenhado num canvas virtual 240x320
e escalado — então adicionar um painel novo é, na maior parte, criar um
perfil de placa novo.

## Adicionando uma placa

1. `main/Boards/<placa>/` com os três arquivos (`Board.cpp`,
   `BoardDisplay.h`, `BoardTraits.h`).
2. Um `elseif` em [main/CMakeLists.txt](main/CMakeLists.txt) apontando o
   diretório da placa no include path privado.
3. `boards/<placa>/sdkconfig.defaults`.
4. Um canal `updates/<canal>/update.json` para OTA.

## Notas por placa

### SmartDisplay 4" (ESP32-S3)

* Não ligue `CONFIG_CELEROS_USB_NATIVE` nesta placa: GPIO19/20 são o I2C do
  GT911 e uma linha de dados RGB — habilitar o USB nativo conflita com o
  toque e o vídeo. O `celerctl` fala com ela pela UART (conector USB é o
  CH340 em cima da UART0, pinos 43/44).
* RGB + PSRAM permitem sprite de frame inteiro; a CYD não tem esse luxo.

**Periféricos da placa usados pelo sistema** (mapa do material do fabricante
"4.0inch_ESP32-4848S040"): microSD em SPI compartilhado com o init do painel
(CS=42, SCK=48, MISO=41, MOSI=47, montado em `/sd`), alto-falante via
amplificador digital Nsiway NS4168 em I2S (DOUT=GPIO40, BCLK=GPIO1,
LRC=GPIO2, sem MCLK — `System.beep` toca uma senoide) e as SKUs "Y" (caixa
de parede 86 switch com 1 ou 3 relés): L1=GPIO40, L2=GPIO2, L3=GPIO1, os
mesmos pinos do alto-falante. Firmware compilado com
`CONFIG_CELEROS_SMARTDISPLAY_RELAYS=N` troca o alto-falante por N relés
controláveis com `System.relay` (partem desligados no boot).

**GPIOs livres para apps** (`System.gpio`): IO35, IO36 e IO37 do header
(o IO0 é o BOOT e linha R4 do display; IO43/44 são a serial do console).

### CYD (ESP32 clássico)

| Launcher | App Store | Settings |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/cyd-launcher.png" width="240" alt="Launcher na CYD"/> | <img src="Documentation/assets/imgs/cyd-appstore.png" width="240" alt="App Store na CYD"/> | <img src="Documentation/assets/imgs/cyd-settings.png" width="240" alt="Settings na CYD"/> |
| **Terminal** | **HTTP Demo** | **Snake** |
| <img src="Documentation/assets/imgs/cyd-terminal.png" width="240" alt="Terminal na CYD"/> | <img src="Documentation/assets/imgs/cyd-httpdemo.png" width="240" alt="HTTP Demo na CYD"/> | <img src="Documentation/assets/imgs/cyd-snake.png" width="240" alt="Snake na CYD"/> |

**O que esperar:**


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
  Também dá para operar a tela pelo navegador com o
  [espelho de tela ao vivo](/maikramer/CelerOS/wiki/Interface-Web).
* **Sem cartão SD** por enquanto (o slot divide o barramento do display).

**Periféricos da placa usados pelo sistema:** LED RGB no verso
(`System.led`), sensor de luz ao lado da tela (`System.lightLevel` e brilho
automático em Configurações → Tela) e saída de alto-falante no GPIO26
(`System.beep`).

**Por dentro (para quem mexe no firmware):**

* 4 MB de flash: a imagem é mais apertada, os apps de sistema em JS na
  LittleFS (~330 KB economizados) foram decisivos para caber.
* Toque resistivo XPT2046: a calibração roda no primeiro boot.
* Sem PSRAM: evite churn de heap no hot path — o sprite de frame só existe
  quando o perfil da placa tem PSRAM. A UI do sistema é composta em duas
  faixas ping-pong; cache de ícones e faixas são liberados quando um app abre.
* Unicore com a IRAM livre acessível a byte (fonte do compile, buffers TLS e
  transbordo do heap JS). Detalhes e números em
  [Documentation/ENGINE_NOTES.md](Documentation/ENGINE_NOTES.md).
