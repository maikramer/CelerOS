# Robot dog (SpotPear ZZPET S3)

**English** | [Português (BR)](/maikramer/CelerOS/wiki/Robo-Cachorro)

CelerOS's first robotics target: the **SpotPear ESP32-S3 AI Robot Dog**
(internally **ZZPET `zzpet-s3`**), a ~US$20 ESP32-S3R8 quadruped with OLED
face, microphone, speaker, touch and WS2812 lamps. The vendor refuses to
publish the schematic, so the complete hardware map below was **extracted live
via the built-in USB JTAG** (GPIO matrix register dumps) and validated with a
custom bring-up firmware. The community reference lives in
[maikramer/zzpet-s3-dog](https://github.com/maikramer/zzpet-s3-dog) — pinouts,
firmware analysis, dump/restore tools and the bring-up firmware.

| With case | Bare PCB |
| :---: | :---: |
| <img src="Documentation/assets/imgs/zzpet-dog-case.jpg" width="360" alt="Robot dog with case"/> | <img src="Documentation/assets/imgs/zzpet-dog-pcb.jpg" width="360" alt="Robot dog PCB"/> |

## Hardware

| Item | Value |
|---|---|
| SoC | **ESP32-S3R8** — dual-core LX7, 8 MB **embedded** octal PSRAM (listing wrongly says S3R2) |
| Flash | 16 MB (Boya), DIO @ 80 MHz — same class as the SmartDisplay |
| USB | Native USB-Serial/JTAG: console, esptool **and** OpenOCD debugging without any adapter |
| Stock firmware | XiaoZhi AI chatbot v1.9.2 (ESP-IDF 5.5.1) — closed-source board fork of [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) |

### Pinout (hardware truth)

| Function | GPIO | Notes |
|---|---|---|
| Servo — rear right leg | **14** | LEDC, 50 Hz |
| Servo — front left leg | **17** | |
| Servo — front right leg | **13** | |
| Servo — rear left leg | **18** | |
| Servo — *unpopulated* (tail option) | **12** | channel exists, no servo on shipped units |
| Mic — I²S1 RX (WS / BCK / DATA) | **4 / 5 / 6** | standard I²S MEMS mic, audio on the **left** slot |
| Speaker — I²S0 TX (DOUT / BCLK / LRCK) | **7 / 15 / 16** | class-D amp, 16 kHz mono |
| OLED — I²C0 (SDA / SCL) | **41 / 42** | SH1106 1.3" @ 0x3C (or SSD1306 0.96" by config) |
| WS2812 strips | **8** and **48** | 4 LEDs each, RMT |
| Touch pad | **10** | tap = chat, double = action, long = lamp |
| BOOT button | **0** | |
| Battery divider | **2** | ADC1_CH1 (≈2066 mV under USB) |

The OLED is mounted rotated in the head: driving it natively requires
**rotating each glyph 90° CW in place plus mirroring the whole frame in X and
Y** (equivalent to the vendor's `SWAP_XY+MIRROR_X+MIRROR_Y`). Reference
implementation in the bring-up firmware's `oled_px()`/`glyph_px()`.

### Known traps

1. I²C **must** run at 100 kHz — no external pull-ups on 41/42.
2. **Never** configure GPIO4 as ADC — it's the mic's I²S clock; doing so kills
   the microphone until the I²S channel is re-initialized.
3. Holding servos under load browns out PC-USB power — use the battery.
4. Two RMT channels with DMA fail; use non-DMA with `mem_block_symbols=96`.

## Status in CelerOS

* **[done]** Full hardware dossier (pinout, display orientation, audio
  loopback, servo→leg mapping, battery channel) — see the
  [zzpet-s3-dog repo](https://github.com/maikramer/zzpet-s3-dog).
* **[planned]** Board port `main/Boards/spotpear-dog/`: S3R8 + SH1106 face +
  servo JS API with preset gaits (API level 8, alongside I2S/relays).
* **[planned]** **Celer Link over BLE**: the SmartDisplay 4848 becomes the
  dog's remote — D-pad and gait triggers from a CelerOS app, telemetry
  (battery, state) back over the link.

Restoring the stock firmware at any time is a single command from a full dump
(`tools/flash_backup_restore.sh` in the dog repo) — the CelerOS port never
touches the vendor bootloader.
