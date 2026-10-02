# CelerOS

[English](/maikramer/CelerOS/wiki/Home) | **Português (BR)**

<p align="center">
  <img src="Documentation/assets/celeros_logo.png" alt="CelerOS" width="420"/>
</p>

O CelerOS é um sistema operacional com GUI, leve e de código aberto, com
runtime de apps JavaScript, para microcontroladores ESP32. Ele transforma
placas de display baratas em um pequeno dispositivo "tipo smartwatch": UI
immediate-mode sobre LovyanGFX, engine JS (Duktape) rodando apps de forma
isolada a partir da flash ou do SD, App Store com atualização over-the-air e
uma ferramenta companheira via USB (`celerctl`) para o dia a dia de
desenvolvimento.

| Launcher | App Store | Settings |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-launcher.png" width="240" alt="Launcher"/> | <img src="Documentation/assets/imgs/celeros-appstore.png" width="240" alt="App Store"/> | <img src="Documentation/assets/imgs/celeros-settings.png" width="240" alt="Settings"/> |

*Capturas do framebuffer real de uma SmartDisplay 4" (firmware 1.4, API nível 13) via `celerctl screencap`.*

| Watchface no watch Waveshare | Launcher no watch |
| :---: | :---: |
| <img src="Documentation/assets/imgs/watch-watchface.png" width="200" alt="Mostrador no watch Waveshare"/> | <img src="Documentation/assets/imgs/watch-launcher.png" width="200" alt="Launcher do watch"/> |

## Novidades da 1.5

Destaques dos lançamentos de outubro de 2026 (firmware 1.5, API JS nível 16):

* **Phone Link** — o relógio conversa com o Android pelo [Gadgetbridge](https://gadgetbridge.org) via BLE (ele se passa por um Bangle.js): notificações com alerta em tela cheia, informações e controles de música, clima, chamadas recebidas e achar celular. Pareamento por código de 6 dígitos; enlace criptografado com proteção MITM (`Phone.*`, API 15 — [página do watch](/maikramer/CelerOS/wiki/Watch-Waveshare)).
* **Plugins de watchface (API 16)** — apps instalados acrescentam linhas de widget ao mostrador embarcando um `watchface.js`; plugin com defeito entra em quarentena, sem derrubar o relógio. O primeiro da loja: **Previsao** (previsão do tempo) — [página de plugins](/maikramer/CelerOS/wiki/Plugins-de-Watchface).
* **Experiência de relógio de verdade (API 15)** — central de notificações nativa, alarmes persistentes com tela de disparo própria, painéis rápidos (lanterna, brilho, não perturbe, achar celular) e seis apps novos de relógio: Timer, Clima, Música, Alarmes, Celular e Atividade.
* **APIs de runtime 12–16** — timers, `Storage` privado por app, sprites múltiplos, E/S binária de arquivos, timeout de tela / deep sleep / alarmes, `playTone`/`playWav`, HTTP assíncrono (`Net.beginGet/pollGet/cancelGet`) e consentimento de permissões para o que os apps declaram.
* **Celer Link amadurece** — pareamento por código de 6 dígitos e desafio-resposta com chave por bond; o aviso "sem pareamento na v1" se aposenta.
* **CelerOS Flasher** — pacotes por placa (firmware + imagem LittleFS + plano de gravação) e um flasher gráfico sem toolchain para Linux/Windows em todo [release do GitHub](https://github.com/maikramer/CelerOS/releases) ([ferramentas](/maikramer/CelerOS/wiki/Ferramentas)).
* **SDK de apps** — `node tools/sdk/celer.js new|lint|types|test|emu|publish`: scaffold, types para o editor, lint contra a API real do firmware, emulador headless com snapshots PNG e publicação na loja ([ferramentas](/maikramer/CelerOS/wiki/Ferramentas)).

## Mapa da wiki

| Página | O que cobre |
|---|---|
| [Placas suportadas](/maikramer/CelerOS/wiki/Placas-Suportadas) | SmartDisplay 4", família CYD, robô cachorro e watch Waveshare — e como adicionar uma placa nova |
| [Compilando e gravando](/maikramer/CelerOS/wiki/Compilando-e-Gravando) | ESP-IDF 6.1, build por placa, partição LittleFS e o harness de testes |
| [Solução de problemas](/maikramer/CelerOS/wiki/Solução-de-Problemas) | Problemas comuns de build, flash, toque, Wi-Fi e web — e os consertos |
| [Arquitetura](/maikramer/CelerOS/wiki/Arquitetura) | Boot flow, camadas do firmware, runtime JS e convenções do código |
| [Robô cachorro](/maikramer/CelerOS/wiki/Robo-Cachorro) | O cachorro robô SpotPear/ZZPET: pinout completo de engenharia reversa, firmware de bring-up e o board CelerOS + controle "Celer Link" BLE |
| [Watch Waveshare](/maikramer/CelerOS/wiki/Watch-Waveshare) | A placa de smartwatch AMOLED 2.06: pinout portado do firmware Rust, app Watchface como casa, escada de tela AOD/deep sleep e a API `Sensors` |
| [Apps de sistema](/maikramer/CelerOS/wiki/Apps-de-Sistema) | O que mora em `data/`, regras do `app.json` e como apps chegam ao dispositivo |
| [Interface web](/maikramer/CelerOS/wiki/Interface-Web) | File manager, upload de firmware e o espelho de tela ao vivo no navegador |
| [Ferramentas](/maikramer/CelerOS/wiki/Ferramentas) | `celerctl`, `celerhub`, servidor OTA local e geradores de assets |
| [celerctl (USB)](/maikramer/CelerOS/wiki/celerctl-USB-(Português)) | Referência de comandos e protocolo HostLink |
| [Atualização OTA](/maikramer/CelerOS/wiki/Atualizacao-OTA) | Manifest `update.json`, canais e upload web |
| [Guia de apps](/maikramer/CelerOS/wiki/Guia-de-Apps) | Como empacotar um app JS: `app.json`, pastas, ícones |
| [Plugins de watchface](/maikramer/CelerOS/wiki/Plugins-de-Watchface) | Como apps instalados acrescentam widgets ao mostrador do relógio (`watchface.js`, API 16) |
| [API JS](/maikramer/CelerOS/wiki/API-JS) | Referência completa do runtime (globals `System`, `Net`, `FS`) |
| [Contribuindo](/maikramer/CelerOS/wiki/Contribuindo) | Convenções, testes, CI e como enviar um PR |

As páginas em inglês (originais) ficam nas seções acima da sidebar; as
traduções ficam na seção **Português (BR)**.

## Funcionalidades em resumo

* **Runtime de apps JavaScript** — apps interativos em ES5 rodam nativamente
  via Duktape: desenho estilo canvas, toque e teclado na tela acoplado,
  sistema de arquivos e rede HTTP/JSON.
* **UI immediate-mode** — os mesmos apps escalam de 240x320 até 480x480.
* **Apps de sistema em JS** — Settings, App Store, Installer, Help, Web
  Server, Terminal, Snake e as demos moram na partição LittleFS.
* **App Store e Installer** — instale apps do
  [CelerOS Hub](https://os.celer.tec.br) via Wi-Fi ou a partir do SD.
* **Atualização over-the-air** — do próprio aparelho, pelo navegador ou via
  `celerctl ota push`.
* **Tela ao vivo no navegador** — `/screen` espelha o display via Wi-Fi e
  repassa seus cliques como toques.
* **Hardware da placa em JS** — LED RGB, sensor de luz com brilho
  automático, alto-falante (`System.beep`), linhas de relé e servos
  (`System.gpio.servo`) nas placas que têm; placas de robô acrescentam
  bateria, microfone, pad capacitivo e NeoPixel
  (`System.battery`/`micLevel`/`touchPad`/`neopixel`); o watch acrescenta
  IMU com pedômetro e raise-to-wake (`Sensors.accel/steps/temp`), bateria,
  RTC que segura a hora sem rede e volume de áudio (`System.setVolume`).
* **Energia de smartwatch** — a escada de tela dim depois de alguns
  segundos, cai para um mostrador always-on com anti burn-in e então vai a
  deep sleep (acorda por EXT1); o watch boota direto no mostrador.
* **Celer Link (BLE)** — link Bluetooth LE entre CelerOS próximos (API 9):
  a placa do robô roda `CelerLink.start()`, a outra dirige com
  `scan()`/`connect()`/`send()` — veja a página do
  [robô cachorro](/maikramer/CelerOS/wiki/Robo-Cachorro) e o app Celer
  Remote no hub. Desde a API 11 o link pareia com código de 6 dígitos e
  desafio-resposta com chave por bond.
* **Phone Link (Gadgetbridge)** — o relógio pareia com o Android via BLE se
  passando por um Bangle.js: notificações com alerta em tela cheia, controle
  de música, clima, chamadas recebidas e achar celular (`Phone.*`, API 15 —
  [página do watch](/maikramer/CelerOS/wiki/Watch-Waveshare)).
* **Notificações e alarmes** — central de notificações do sistema
  (`System.notify`; no watch uma notificação nova acorda a tela) e alarmes
  persistentes com tela de disparo própria (API 15).
* **Plugins de watchface** — apps instalados estendem o mostrador do relógio
  com linhas de widget (`watchface.js` + `System.launchApp`, API 16 —
  [página de plugins](/maikramer/CelerOS/wiki/Plugins-de-Watchface)).
* **CelerOS Flasher e SDK de apps** — flash gráfico sem toolchain pelos
  [releases do GitHub](https://github.com/maikramer/CelerOS/releases), e o
  `tools/sdk/celer.js` para criar, lintar, emular e publicar apps
  ([ferramentas](/maikramer/CelerOS/wiki/Ferramentas)).
* **Wi-Fi por portal cativo** — access point `CelerOS-Setup-XXXX` para
  configurar pelo celular.
* **`celerctl`** — companheiro USB estilo adb: shell, push/pull, logcat,
  screencap e atualização de firmware in-place.

## Links

* [Repositório](https://github.com/maikramer/CelerOS) ·
  [README em português](https://github.com/maikramer/CelerOS/blob/main/README.pt-BR.md) ·
  [README in English](https://github.com/maikramer/CelerOS/blob/main/README.md)
* [Issues](https://github.com/maikramer/CelerOS/issues) — bugs e ideias
* História: o CelerOS começou como fork do
  [KryonOS](https://github.com/Haris16-code/KryonOS), do Haris.

---

Esta wiki é gerada automaticamente por CI a partir de
[`wiki/`](https://github.com/maikramer/CelerOS/tree/main/wiki) no
repositório — edite lá, não pela web.
