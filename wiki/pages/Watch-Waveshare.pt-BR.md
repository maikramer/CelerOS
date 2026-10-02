# Watch Waveshare AMOLED 2.06

[English](/maikramer/CelerOS/wiki/Waveshare-Watch) | **Português (BR)**

O primeiro wearable do CelerOS: o **Waveshare ESP32-S3-Touch-AMOLED-2.06**
(id de board `waveshare-amoled206`), um smartwatch redondo de AMOLED 2.06"
sobre o ESP32-S3R8 — 32 MB de flash, 8 MB de PSRAM embutida, um PMU de
hardware e um bom conjunto de sensores atrás do vidro. O mapa de pinos e as
sequências de init do painel foram portados do firmware Rust
**`waveshare-watch-rs`** (firmware standalone para o mesmo watch), o que
poupou desta placa a arqueologia de registradores por JTAG que o
[robô cachorro](/maikramer/CelerOS/wiki/Robo-Cachorro) precisou.

## Hardware

| Item | Valor |
|---|---|
| SoC | **ESP32-S3R8** — LX7 dual-core, 8 MB PSRAM octal **embutida** |
| Flash | 32 MB, DIO @ 80 MHz — espaço de sobra, firmware compila com `-O2` |
| USB | USB-Serial/JTAG nativo: console, esptool **e** `celerctl` (CDC dupla) no único conector USB |
| Display | AMOLED redondo 2.06" 410x502, **CO5300 via QSPI** a 80 MHz (vidro visível no offset de coluna 22) |
| Toque | FT3168 capacitivo no I²C |
| PMU | AXP2101 — trilhos do display, medição de bateria e a tecla física de power (PEK) |
| Sensores | IMU QMI8658 (pedômetro, raise-to-wake), RTC PCF85063, codec de áudio ES8311 — todos no mesmo barramento I²C0 |

### Pinout

| Função | GPIO | Observações |
|---|---|---|
| AMOLED — QSPI SDIO0..3 | **4 / 5 / 6 / 7** | pixels via cmd 0x32 (quad), registradores via 0x02 (single) |
| AMOLED — SCLK / CS / RST | **11 / 12 / 8** | RST pede pulso longo: 200 ms em low (pulso curto não levanta o painel) |
| Barramento I²C0 | **SDA 15 / SCL 14** | touch FT3168 (0x38), PMU AXP2101 (0x34), RTC PCF85063, IMU QMI8658, codec ES8311 |
| Touch FT3168 — RST / INT | **9 / 38** | precisa do reg 0xA5 = monitor mode no init |
| Áudio ES8311 — I²S0 | **DOUT 40, BCLK 41, LRC 45, MCLK 16** | 16 kHz, MCLK 4,096 MHz; PA enable no **46** (alto só durante o beep) |
| Microfone — ADC do ES8311 | **ASDOUT 42** | I²S1 RX slave nos clocks do codec |
| microSD — SPI3 | **CS 17, SCK 2, MOSI 1, MISO 3** | montado em `/sd` |
| Botão BOOT | **0** | curto = home/encerra app, segurar = screenshot |
| Tecla de power | via **PEK do AXP2101** | poll pelo ScreenPower; acorda do deep sleep |

O GPIO10 **não** é botão nesta HW: lê LOW com pull-up (armadilha herdada da
família da placa do cachorro).

## A experiência de watch

* O `homeApp` do perfil é o app **Watchface** (`celeros.watchface`): o
  mostrador do relógio **é** a tela inicial; swipe pra cima abre o launcher.
  O `screenInset` afasta relógio e botões X dos cantos arredondados.
* **Escada de energia da tela** (ScreenPower): pleno → **dim** após 8 s →
  **AOD** aos 15 s (mostrador de baixa frequência com deslocamento anti
  burn-in, painel em SLPIN) → off → **deep sleep**, acordando por EXT1 nos
  botões. Levantar o pulso acende um "glance" AOD pelo IMU.
* A hora sobrevive a reboot sem rede (PCF85063 — gravado após sincronizar
  NTP ou ajuste manual), a contagem de passos do dia vai para o NVS antes de
  dormir, e a bateria vem do AXP2101 no `System.battery()`.
* Superfície JS específica do watch: `Sensors.accel()/steps()/temp()` e
  `System.setVolume()/getVolume()` (ambos API nível 13), além do
  `System.micLevel()` pelo ES8311.

## Armadilhas conhecidas

1. O **AXP2101 precisa ser configurado primeiro**: sem DCDC1 + ALDO1 em
   3,3 V o AMOLED fica preto mesmo inicializado (o `Board::init()` faz isso
   antes do init do painel).
2. O CO5300 quer **reset longo** (200 ms em low) e um patch pós-init
   (`0x58=0x00` contrast off, `0x36=0x00` MADCTL portrait) que a lista de
   init do LovyanGFX de fábrica (do T-Watch-Ultra) não tem.
3. O painel só aceita **janelas de escrita alinhadas a par** e não tem
   readback — o display roda num framebuffer do LovyanGFX na PSRAM
   (~402 KB), que em troca devolve o `readRect` (screenshots, espelho web).
4. A camada I²C do próprio LovyanGFX falha no IDF 6.1: o FT3168 é dirigido
   pelo driver `i2c_master` do IDF em `main/Display/Touch_FT3168_IDF.h`
   (mesmo padrão do GT911).
5. Brilho é o WRDISBV do AMOLED (DCS 0x51) — não há backlight PWM; o
   Configurações → Tela fala com o driver do painel.
6. Arquivos de `sdkconfig.defaults` **não aceitam comentário inline** — o
   USB nativo precisa de `CONFIG_TINYUSB_CDC_COUNT=2` (shell +
   `celerctl`), e um comentário nessa linha quebra o init do USB em
   silêncio.

## Situação no CelerOS

* **[feito]** Porte da placa `main/Boards/waveshare-watch/`, validado no
  hardware: display + toque, RTC, bateria, IMU (pedômetro +
  raise-to-wake), escada de tela com AOD e deep sleep, beep + microfone
  pelo ES8311, volume, Celer Link BLE e `celerctl` pelo USB nativo.
* **[em andamento]** Monitoramento de movimento por ULP-RISC-V durante o
  deep sleep (raise-to-wake sem os núcleos principais).

Restaurar o firmware original da Waveshare é uma gravação comum do esptool
pela mesma USB — o flash do CelerOS nunca toca no bootloader.
