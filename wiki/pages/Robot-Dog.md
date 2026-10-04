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
| USB | Native USB-Serial/JTAG: console, esptool, OpenOCD debugging **and** `celerctl` (console+link multiplexed on the same port via `CELEROS_LINK_ON_USJ`) — no adapter needed |
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
| Battery divider | **2** | ADC1_CH1, 2:1 divider (≈2066 mV at the pin under USB; `System.battery()` returns the cell, ≈4130 mV) |

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
* **[done]** **Celer Link over BLE** (API 9) + **servos in JS**
  (`System.gpio.servo`, API 10): the SmartDisplay 4848 drives the dog with
  the **Celer Remote** hub app — D-pad commands and telemetry (battery,
  state) over the link. Robot-board hardware (battery, mic, touch pad,
  NeoPixel) joined the JS API at level 10 as well.
* **[done]** Board port `main/Boards/spotpear-dog/`: boots into the
  launcher and opens the **Dog Face** app by itself (the profile's
  `homeApp`; board-exclusive app via the `boards/<board>/data/` overlay).
  Dog Face: expressive eyes, reactions to touch and sound, battery, sleep
  on idle, and ramped gaits (walk/back/turn/sit/lie/stretch) with a
  dead-man keepalive — the dog stops by itself if the remote lets go of
  the arrow or the link drops.
* **[done]** **Dog Face 1.5.1**: legs ported from the ESP-Hi (Espressif)
  C tables — cyclic gaits walk/back/left/right plus the "creep"
  (centipede) march as the default `esphi` mode; postures
  stand/lie/stretch/sit; FR/BL servo mirroring with per-leg signs.
  Per-leg calibration via Celer Remote/nRF Connect (`{"type":"calib"}`,
  fine tuning `{"type":"tune"}`) saved to `/local/dogtune.json`; the
  `modes` command lists the gaits.
* **[done]** **Repertoire 2.0** (Dog Face 1.7/1.8): a **sequencer** runs
  step queues `{do:"move"|"pose"|"trick"|"bark"|"leds"|"emotion"|
  "wait"|"say", ...}` at the main loop's pace, with safety clamps
  (12 steps, 3 s per step, 12 s per queue, unknown acts dropped). The
  same queue powers:
  * **Native tricks**: `dance` (choreography **reshuffled** on every call —
    never the same dance), `spin`, `shake` (paw), `pushup`, `excited`,
    `hello` and `pee` (the three-legged classic); new postures `beg`
    and `pee`.
  * **Real barks**: 5 synthesized WAVs (`woof`, `yip`, `growl`,
    `whine`, `howl`) bundled with the app — generated by
    `tools/dog/barks.py` (pure-stdlib additive synthesis, fixed seed);
    falls back to a `playTone` melody when the file is missing.
  * **Emotions**: `love` (hearts), `angry` (V brows), `sad` (tear),
    `sleepy` (floating Zs), `curious`/`alert` (pupils) on the face +
    LED-ring animations (rainbow while dancing, pink heartbeat, alert
    blink).
  * **Guards**: battery <15% refuses heavy tricks with a whine + sad
    face + "cansado" on the remote; crossing 20% whines once (25%
    hysteresis to rearm).
  * **Protocol**: `{"type":"trick","name":"dance"}` over Celer Link
    answers `{"type":"trick_res","ok":...}`; "hi celer", the D-pad, the
    touch pad and stop interrupt a running sequence instantly.
* **[done]** **Teachable tricks** (`/local/dogtricks.json`): the owner
  registers named sequences in **the same schema the LLM composes** —
  `{"Super Truco":[{"do":"bark","kind":"howl"},{"do":"pose","name":"lie",
  "ms":400}]}` via `celerctl push` or the web editor. Names
  (normalized: "Super Truco" → `super_truco`) join the `dog_trick` tool
  and the prompt, so **"hi celer, do the super truco" just works**.
  `{"type":"tricks_reload"}` reloads over the link; telemetry lists
  `tricks`.
* **[done]** **Celer Remote 1.5**: tricks grid on the control screen
  (the list comes from the robot via `tel.tricks` — old robots don't
  show it) and the **dog's answers** (`{"type":"say"}` from `dog_say`)
  show up as a 6 s note.
* **[done]** **Battery watchdog** in deep sleep: the ULP-RISC-V
  coprocessor (`DogUlp.cpp` + `ulp/ulp_main.c`) reads the battery ADC
  (ADC1_CH1, GPIO2, 2:1 divider) about every 60 s and wakes the cores if
  the cell drops below ~3.30 V — the dog sleeps safely. The dog only
  sleeps via `System.deepSleep(ms)` and the timer always wakes it.

### Voice & wake word

* **[done]** **Wake word "Hi Celer"** on-device: own microWakeWord
  detector (`main/Hardware/WakeWord.cpp`) streaming TFLite Micro — int8
  model in flash (~60 KB), a 32 KB arena that goes to PSRAM when
  available, on its own task. JS
  `WakeWord.start()/stop()/poll()/level()/running()` (API 20), guarded
  by the same `mic` permission in app.json as `Mic.*`.
* **[done]** **Voice 2.0 — the LLM choreographs** (Dog Face 1.8): "hi
  celer" opens a listening window (ack beep + LED ring), records up to
  3.5 s and sends the audio to qwen omni (OpenRouter) — which now gets
  **8 tools** instead of an 8-value enum:
  * `dog_move(direction, ms)` — "walk a little forward" / "turn left"
    become timed movement;
  * `dog_posture(pose)` — postures including `beg` and `pee`;
  * `dog_trick(name)` — native **and owner-taught** tricks;
  * `dog_sequence(steps[])` — **free choreography**: the model writes up
    to 10 steps on the spot ("dance and then bark happily" becomes a
    different queue every time);
  * `dog_bark(kind, times)` — picks woof/yip/growl/whine/howl;
  * `dog_emotion(mood)` — face + LED ring;
  * `dog_say(text)` — **answers questions**: "you okay?", "what's your
    battery?" → full sentence on the paired remote + seven-segment
    summary on the glass + a yip. Live telemetry (battery, posture,
    gait) rides in the system prompt with the **Celercão** persona, so
    the answer comes out in a single round;
  * `dog_stop` — stop everything.
  Offline keyword fallback (no tool_call/no key) covers postures/gaits
  **and** tricks/barks/emotions: dance, paw, pee, spin, pushups, bark,
  howl, growl, I love you, angry... (PT/EN, including the accented
  variants the STT produces). Voice walk without an explicit duration
  stays capped at 3 s.
* Voice stack on the JS API: **18** AI, **19** `Mic.*`, **20** function
  calling + `WakeWord.*`.

Restoring the stock firmware at any time is a single command from a full dump
(`tools/flash_backup_restore.sh` in the dog repo) — the CelerOS port never
touches the vendor bootloader.
