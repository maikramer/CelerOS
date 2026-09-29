# Placas suportadas

| SmartDisplay 4" | CYD |
| :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-home.jpg" width="300" alt="SmartDisplay rodando o CelerOS"/> | <img src="Documentation/assets/imgs/CYD2432S028R.jpg" width="300" alt="CYD"/> |

| Placa | SoC | Display | Toque | Observações |
|---|---|---|---|---|
| **SmartDisplay 4"** (Guition ESP32-S3-4848S040) | ESP32-S3-N16R8 | IPS 4" 480x480 RGB (ST7701) | Capacitivo GT911 | 16 MB flash / 8 MB PSRAM, opção de USB nativo |
| **CYD** (ESP32-2432S028R, "Cheap Yellow Display") | ESP32 | ILI9341 2.8" 320x240 SPI | Resistivo XPT2046 | Sem PSRAM; serial CH340; pede calibração de toque no primeiro boot; UI mais simples ([veja abaixo](#cyd-esp32-clássico)) |

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
  toque e o vídeo. O `celerctl` fala com ela pela UART.
* RGB + PSRAM permitem sprite de frame inteiro; a CYD não tem esse luxo.

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
* **Memória apertada.** Um app recebe ~100 KB; o teto prático é um `main.js`
  de ~45 KB. A App Store funciona, perto do limite; apps maiores aparecem como
  "Requer PSRAM" na loja.
* **Toque resistivo.** Pede um toque mais firme e calibração no primeiro
  boot; os alvos são pequenos (ícones de 48 px, barra superior de 20 px).
* **Sem cartão SD** por enquanto (o slot divide o barramento do display).

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
