# Robô cachorro (SpotPear ZZPET S3)

[English](/maikramer/CelerOS/wiki/Robot-Dog) | **Português (BR)**

O primeiro alvo de robótica do CelerOS: o **cachorro robô ESP32-S3 da
SpotPear** (internamente **ZZPET `zzpet-s3`**), um quadrúpede de ~US$20 com
ESP32-S3R8, tela OLED de cara, microfone, alto-falante, touch e lâmpadas
WS2812. O fornecedor se recusa a publicar o esquemático, então o mapa de
hardware completo abaixo foi **extraído ao vivo pelo USB JTAG embutido** (dump
de registradores da matriz GPIO) e validado com um firmware de bring-up
próprio. A referência da comunidade fica em
[maikramer/zzpet-s3-dog](https://github.com/maikramer/zzpet-s3-dog) — pinouts,
análise do firmware, ferramentas de dump/restauração e o firmware de bring-up.

| Com carcaça | Placa nua |
| :---: | :---: |
| <img src="Documentation/assets/imgs/zzpet-dog-case.jpg" width="360" alt="Robô cachorro com carcaça"/> | <img src="Documentation/assets/imgs/zzpet-dog-pcb.jpg" width="360" alt="Placa do robô cachorro"/> |

## Hardware

| Item | Valor |
|---|---|
| SoC | **ESP32-S3R8** — LX7 dual-core, 8 MB PSRAM octal **embutida** (o anúncio mente "S3R2") |
| Flash | 16 MB (Boya), DIO @ 80 MHz — mesma classe do SmartDisplay |
| USB | USB-Serial/JTAG nativo: console, esptool **e** depuração com OpenOCD sem adaptador |
| Firmware original | Chatbot XiaoZhi AI v1.9.2 (ESP-IDF 5.5.1) — fork fechado do [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) |

### Pinout (verdade de hardware)

| Função | GPIO | Observações |
|---|---|---|
| Servo — perna traseira direita | **14** | LEDC, 50 Hz |
| Servo — perna dianteira esquerda | **17** | |
| Servo — perna dianteira direita | **13** | |
| Servo — perna traseira esquerda | **18** | |
| Servo — *despopulado* (cauda opcional) | **12** | canal existe, sem servo nas unidades vendidas |
| Mic — I²S1 RX (WS / BCK / DATA) | **4 / 5 / 6** | mic MEMS I²S padrão, áudio no slot **esquerdo** |
| Alto-falante — I²S0 TX (DOUT / BCLK / LRCK) | **7 / 15 / 16** | amp classe D, 16 kHz mono |
| OLED — I²C0 (SDA / SCL) | **41 / 42** | SH1106 1.3" @ 0x3C (ou SSD1306 0.96" por config) |
| Fitas WS2812 | **8** e **48** | 4 LEDs cada, RMT |
| Touch | **10** | toque = conversa, duplo = ação, longo = lâmpada |
| Botão BOOT | **0** | |
| Divisor da bateria | **2** | ADC1_CH1 (≈2066 mV sob USB) |

O OLED é montado girado na cabeça: para dirigi-lo nativamente é preciso
**rotacionar cada glifo 90° horário no lugar e espelhar o frame inteiro em X e
Y** (equivale ao `SWAP_XY+MIRROR_X+MIRROR_Y` do fornecedor). Implementação de
referência no `oled_px()`/`glyph_px()` do firmware de bring-up.

### Armadilhas conhecidas

1. O I²C **deve** rodar a 100 kHz — não há pull-ups externos em 41/42.
2. **Nunca** configure o GPIO4 como ADC — é o clock I²S do mic; isso mata o
   microfone até reinicializar o canal I²S.
3. Segurar servos sob carga derruba a alimentação da USB do PC — use bateria.
4. Dois canais RMT com DMA falham; use sem DMA com `mem_block_symbols=96`.

## Situação no CelerOS

* **[feito]** Dossiê de hardware completo (pinout, orientação da tela, loopback
  de áudio, mapeamento servo↔perna, canal da bateria) — veja o
  [repo zzpet-s3-dog](https://github.com/maikramer/zzpet-s3-dog).
* **[planejado]** Porte do board `main/Boards/spotpear-dog/`: S3R8 + cara
  SH1106 + API JS de servos com gaits pré-definidas (nível de API 8, junto com
  I2S/rélés).
* **[planejado]** **Celer Link via BLE**: o SmartDisplay 4848 vira controle
  remoto do cachorro — D-pad e gaits num app CelerOS, telemetria (bateria,
  estado) de volta pelo link.

Restaurar o firmware original a qualquer momento é um comando único a partir
do dump completo (`tools/flash_backup_restore.sh` no repo do cachorro) — o
porte para o CelerOS nunca toca no bootloader do fornecedor.
