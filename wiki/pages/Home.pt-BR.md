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

*Capturas do framebuffer real de uma SmartDisplay 4" (firmware 1.5) via `celerctl screencap`.*

| Watchface no watch Waveshare | Launcher no watch |
| :---: | :---: |
| <img src="Documentation/assets/imgs/watch-watchface.png" width="200" alt="Mostrador no watch Waveshare"/> | <img src="Documentation/assets/imgs/watch-launcher.png" width="200" alt="Launcher do watch"/> |

## Novidades

Destaques das rodadas de outubro de 2026 (firmware 1.6–1.7, API JS nível 24):

* **O cão fala (API 24)** — `AI.speak()` transforma texto em voz direto no alto-falante: o TTS da OpenRouter (vozes Gemini, ~30 vozes naturais com português de fábrica) toca **ao vivo** enquanto descarrega — nada passa pela RAM. Pergunte qualquer coisa ao cão robô e ele responde em voz alta, no idioma em que você falou ([API JS](/maikramer/CelerOS/wiki/API-JS), [robô cachorro](/maikramer/CelerOS/wiki/Robo-Cachorro)).
* **A IA escreve a coreografia** — tool `dog_script` do cão: a LLM escreve JavaScript ES5 ela mesma e roda no cão numa sandbox de coreografia (cara, pernas, LEDs e som juntos; clamps, prazo e toque aborta). A marcha hop anda a 660 ms/ciclo, o hop assimétrico guina e o giro derruba o cão de barriga — tudo afinável ao vivo pelo painel AFINAR do Celer Remote por BLE, sem cabo ([robô cachorro](/maikramer/CelerOS/wiki/Robo-Cachorro)).
* **Áudio QOA** — um decoder ponto-fixo de ~60 linhas (QOA do phoboslab, MIT) substitui a pilha MP3: latidos reais renderizados por IA viajam como arquivos `.qoa` 5x menores, e os pacotes de app podem levar assets de som `.qoa`.
* **celerctl por Wi-Fi — a Celer Debug Bridge** — shell, push/pull, logcat, debugger JS, screencap e OTA por TCP, sem cabo; o `devices` descobre os bridges, pareie uma vez com `pair`, e o `provision` deixa uma placa pronta num comando ([ferramentas](/maikramer/CelerOS/wiki/Ferramentas), [celerctl](/maikramer/CelerOS/wiki/celerctl-USB-(Português))).
* **Profiling e logs persistentes** — `celerctl top` (CPU por task, heap, watermark de stack, fps do app) e `/local/log/kern.log` + `apps.log` no estilo Linux com rotação: `dmesg`/`appslog` no Terminal, `celerctl cat` no host — sobrevivem a reboot.
* **Rodada de robustez** — binds bloqueantes longos renovam a janela do exec-timeout (nada de RangeError depois de um TLS lento), os sons da UI saíram da thread da UI, o DNS do captive portal sobrevive a pacotes malformados e os caminhos de falha com buffers grandes não vazam nem abortam.

O firmware 1.6, no mesmo outubro: o **toolkit UI** (`UI.*`, API 22), **apps multi-arquivo** com `require()` (API 23) e instalação multi-arquivo na App Store, **voz 2.0** (a LLM coreografa o cão), **Celer Link selado** com quadros AES-GCM (API 21) e o Celer Remote reescrito no toolkit.

Antes disso, no ciclo da 1.5: IA no aparelho (`AI.chat`, API 18–20), voz e a **wake word "Hi Celer"** no chip (API 19–20), o debugger JavaScript por USB, o deep sleep do watch com sentinela ULP-RISC-V, o **Phone Link** pelo Gadgetbridge (`Phone.*`, API 15), **plugins de watchface** (API 16), APIs de runtime 12–17, a placa devkit barebone, o **CelerOS Flasher** e o **SDK de apps**.

## Mapa da wiki

| Página | O que cobre |
|---|---|
| [Placas suportadas](/maikramer/CelerOS/wiki/Placas-Suportadas) | SmartDisplay 4", família CYD, cão robô, watch Waveshare e o devkit barebone — e como adicionar uma placa nova |
| [Compilando e gravando](/maikramer/CelerOS/wiki/Compilando-e-Gravando) | ESP-IDF 6.1, build por placa, partição LittleFS e o harness de testes |
| [Solução de problemas](/maikramer/CelerOS/wiki/Solução-de-Problemas) | Problemas comuns de build, flash, toque, Wi-Fi e web — e os consertos |
| [Arquitetura](/maikramer/CelerOS/wiki/Arquitetura) | Boot flow, camadas do firmware, runtime JS e convenções do código |
| [Cão robô](/maikramer/CelerOS/wiki/Robo-Cachorro) | O cachorro robô SpotPear/ZZPET: pinout completo de engenharia reversa, firmware de bring-up, o board CelerOS, o controle "Celer Link" BLE e os comandos por voz ("hi celer, senta") |
| [Watch Waveshare](/maikramer/CelerOS/wiki/Watch-Waveshare) | A placa de smartwatch AMOLED 2.06: pinout portado do firmware Rust, app Watchface como casa, escada de tela AOD/deep sleep (com a sentinela ULP) e a API `Sensors` |
| [Devkit barebone](/maikramer/CelerOS/wiki/Devkit-Barebone) | O devkit ESP32 de 4 MB sem tela: `System.button()` + LED, apps headless e o canal OTA |
| [Apps de sistema](/maikramer/CelerOS/wiki/Apps-de-Sistema) | O que mora em `data/`, regras do `app.json` e como apps chegam ao dispositivo |
| [Interface web](/maikramer/CelerOS/wiki/Interface-Web) | File manager, upload de firmware e o espelho de tela ao vivo no navegador |
| [Ferramentas](/maikramer/CelerOS/wiki/Ferramentas) | `celerctl`, o debugger JS, `celerhub`, servidor OTA local e geradores de assets |
| [celerctl (USB)](/maikramer/CelerOS/wiki/celerctl-USB-(Português)) | Referência de comandos e protocolo HostLink |
| [Atualização OTA](/maikramer/CelerOS/wiki/Atualizacao-OTA) | Manifest `update.json`, canais e upload web |
| [Guia de apps](/maikramer/CelerOS/wiki/Guia-de-Apps) | Como empacotar um app JS: `app.json`, pastas, ícones |
| [Plugins de watchface](/maikramer/CelerOS/wiki/Plugins-de-Watchface) | Como apps instalados acrescentam widgets ao mostrador do relógio (`watchface.js`, API 16) |
| [API JS](/maikramer/CelerOS/wiki/API-JS) | Referência completa do runtime (globals `System`, `Net`, `FS`, `AI`, `Mic`, `WakeWord`) |
| [Contribuindo](/maikramer/CelerOS/wiki/Contribuindo) | Convenções, testes, CI e como enviar um PR |

As páginas em inglês (originais) ficam nas seções acima da sidebar; as
traduções ficam na seção **Português (BR)**.

## Funcionalidades em resumo

* **Runtime de apps JavaScript** — apps interativos em ES5 rodam nativamente
  via Duktape (API nível 20): desenho estilo canvas, toque e teclado na tela
  acoplado, sistema de arquivos e rede HTTP/JSON.
* **IA e voz no aparelho** — `AI.chat()` conversa com a DeepSeek ou a
  OpenRouter de qualquer app (assíncrono, TLS, function calling; as chaves
  ficam fora da sandbox JS), `Mic.*` grava WAV de 16 kHz atrás de uma
  permissão `mic`, e os apps **Chat IA** e **Qwen** são um cliente de chat e
  um assistente de voz push-to-talk. No cão robô a wake word **"Hi Celer"**
  roda no próprio aparelho e comandos por voz ("hi celer, senta") dirigem as
  pernas (`WakeWord.*`, API 20 — [cão robô](/maikramer/CelerOS/wiki/Robo-Cachorro)).
* **Debugger JavaScript** — `celerctl debug MeuApp`: debugger Duktape pelo
  USB com breakpoints, stepping, eval, watches e pausa automática em erros
  não capturados ([ferramentas](/maikramer/CelerOS/wiki/Ferramentas)).
* **UI immediate-mode** — os mesmos apps escalam de 240x320 até 480x480.
* **Apps de sistema em JS** — Settings, App Store, Installer, Help, Web
  Server, Terminal, Snake, Chat IA, Qwen e as demos moram na partição
  LittleFS; as placas sobrepõem seus apps de casa (Watchface, Dog Face,
  Barebone).
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
  RTC que segura a hora sem rede e volume de áudio (`System.setVolume`); o
  devkit sem tela expõe o botão BOOT (`System.button()`, API 17).
* **Energia de smartwatch** — a escada de tela dim depois de alguns
  segundos, cai para um mostrador always-on com anti burn-in e então entra
  em deep sleep automático, guardado por uma sentinela ULP-RISC-V que vigia
  os botões, a bateria e o cabo (o PWR acorda em ~100 ms); o watch boota
  direto no mostrador.
* **Celer Link (BLE)** — link Bluetooth LE entre CelerOS próximos (API 9):
  a placa do robô roda `CelerLink.start()`, a outra dirige com
  `scan()`/`connect()`/`send()` — veja a página do
  [cão robô](/maikramer/CelerOS/wiki/Robo-Cachorro) e o app Celer
  Remote no hub. Desde a API 11 o link pareia com código de 6 dígitos e
  desafio-resposta com chave por bond. Roda na SmartDisplay, no watch e no
  cão robô.
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
  `tools/sdk/celer.js` para criar, lintar, emular (um PNG por marco do
  relógio com diff de pixels) e publicar apps
  ([ferramentas](/maikramer/CelerOS/wiki/Ferramentas)).
* **Wi-Fi por portal cativo** — access point `CelerOS-Setup-XXXX` para
  configurar pelo celular.
* **`celerctl`** — companheiro USB estilo adb: shell, push/pull, logcat
  (timestamps, filtros e dump não-destrutivo), o debugger JS, screencap e
  atualização de firmware in-place. O último erro de app persiste em
  `/local/lastcrash.txt` (`lasterror` no shell).

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
