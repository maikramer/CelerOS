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

| Splash de boot | Launcher | Terminal (teclado acoplado) |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-splash.png" width="240" alt="Splash de boot"/> | <img src="Documentation/assets/imgs/celeros-launcher.png" width="240" alt="Launcher"/> | <img src="Documentation/assets/imgs/celeros-terminal.png" width="240" alt="Terminal"/> |

*Capturas do framebuffer real de uma SmartDisplay 4" via `celerctl screencap`.*

| Launcher na CYD | App Store na CYD | Settings na CYD |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/cyd-launcher.png" width="240" alt="Launcher na CYD"/> | <img src="Documentation/assets/imgs/cyd-appstore.png" width="240" alt="App Store na CYD"/> | <img src="Documentation/assets/imgs/cyd-settings.png" width="240" alt="Settings na CYD"/> |

*CYD (320x240, sem PSRAM): mesma base, experiência mais simples — veja
[Placas suportadas](/maikramer/CelerOS/wiki/Placas-Suportadas#cyd-esp32-clássico).*

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
  Remote no hub. Sem pareamento na v1: brinquedos e protótipos, nada
  sensível.
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
