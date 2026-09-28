# Placas suportadas

| SmartDisplay 4" | CYD |
| :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-home.jpg" width="300" alt="SmartDisplay rodando o CelerOS"/> | <img src="Documentation/assets/imgs/CYD2432S028R.jpg" width="300" alt="CYD"/> |

| Placa | SoC | Display | Toque | Observações |
|---|---|---|---|---|
| **SmartDisplay 4"** (Guition ESP32-S3-4848S040) | ESP32-S3-N16R8 | IPS 4" 480x480 RGB (ST7701) | Capacitivo GT911 | 16 MB flash / 8 MB PSRAM, opção de USB nativo |
| **CYD** (ESP32-2432S028R, "Cheap Yellow Display") | ESP32 | ILI9341 2.8" 240x320 SPI | Resistivo XPT2046 | Serial CH340; pede calibração de toque no primeiro boot |

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

* 4 MB de flash: a imagem é mais apertada, os apps de sistema em JS na
  LittleFS (~330 KB economizados) foram decisivos para caber.
* Toque resistivo XPT2046: a calibração roda no primeiro boot.
* Sem PSRAM: evite churn de heap no hot path — o sprite de frame só existe
  quando o perfil da placa tem PSRAM.
