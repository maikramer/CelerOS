# kryonctl — ferramenta USB/serial do KryonOS (estilo adb)

`kryonctl.py` conversa com o firmware pelo canal **KryonLink**: um protocolo
binario leve (`[0x4B 'K'][cmd][len u16 LE][payload]`) que roda sobre a UART
do console — na pratica, o CH340 que o PC ve como `/dev/ttyUSB*` (ou a CDC1
do USB nativo, em placas com `CONFIG_KRYONOS_USB_NATIVE`).

Os opcodes vivem em `main/USBDevice/KryonLink.h` e a ferramenta le esse
arquivo por regex: so existe uma fonte da verdade para os dois lados.

## Instalacao

```bash
pip install -r tools/requirements.txt   # pyserial (screencap pede Pillow)
```

## Comandos

```bash
python3 tools/kryonctl.py devices            # lista placas conectadas
python3 tools/kryonctl.py info               # versao/board/heap/rede/FS
python3 tools/kryonctl.py shell              # shell interativo (help)
python3 tools/kryonctl.py shell "ls /local"  # executa e imprime
python3 tools/kryonctl.py ls -l /local/apps
python3 tools/kryonctl.py cat /local/wifi.txt
python3 tools/kryonctl.py push app.zip /local/tmp_download/app.zip
python3 tools/kryonctl.py pull /local/apps/HTTP\ Demo/app.json .
python3 tools/kryonctl.py rm /local/old.txt
python3 tools/kryonctl.py reboot
python3 tools/kryonctl.py logcat             # logs ao vivo (Ctrl-C sai)
python3 tools/kryonctl.py ota push build/KryonOS.bin   # firmware sem esptool
python3 tools/kryonctl.py screencap tela.png # captura do display -> PNG
python3 tools/kryonctl.py tap 120 160        # injeta um toque (navegar via USB)
python3 tools/kryonctl.py swipe 120 400 120 40  # injeta um arrasto (scroll)
python3 tools/kryonctl.py apps list             # apps instalados (local + sd)
python3 tools/kryonctl.py apps install "data/apps/Web Server"  # instala pasta
python3 tools/kryonctl.py apps install meuapp --sd             # no cartão
python3 tools/kryonctl.py apps rm "Touch Test"                  # desinstala
```

### Iterar na UI sem tocar na placa

`tap`/`swipe` + `screencap` formam um laco adb-like: o gesto e enfileirado
no firmware (opcode `KL_TOUCH`), executado pelo TouchPump da UI como se
fosse dedo fisico — vale para o Navigator e para modais (teclado QWERTY) —
e o `screencap` le o framebuffer real. Exemplo de sessao:

```bash
python3 tools/kryonctl.py tap 360 88 && python3 tools/kryonctl.py screencap s.png
```

## Acelerando transferencias (-b)

O canal nasce a 115200 baud. Com `-b 921600` a ferramenta negocia a troca
com o firmware e reabre a porta mais rapida:

```bash
python3 tools/kryonctl.py -b 921600 push firmware.bin /sd/fw.bin
```

Medido na SmartDisplay (CH340): push ~57 KB/s, pull ~190 KB/s, OTA ~60 KB/s.

## Como o canal convive com o console

A UART do console multiplexa dois modos (`main/USBDevice/SerialLink.cpp`):

- **console** — shell interativo humano (eco, prompt `kryon> `) e logs
  ESP_LOG/Serial visiveis. Abrir o minicom/monitor e usar.
- **link** — acionado pela chegada de um frame HELLO (`0x4B 0x01 ...`);
  digitacao humana nao produz essa sequencia. Logs sao suspensos na UART
  (teclados num ring de 8 KB) e a sessao volta ao console apos ~8s sem
  frames, restaurando o baud.

Com `logcat`, o ring e drenado (historico desde o boot) e os logs seguem
como frames ao vivo na propria ferramenta.

## Nota sobre USB nativo / Mass Storage

O ESP32-S3 tem USB-OTG (GPIO19/20), mas **na SmartDisplay 4848S040 esses
pinos sao usados pela placa** (GPIO19 = SDA do touch GT911, GPIO20 = linha
G1 do display RGB) e o conector USB e apenas CH340. Por isso o KryonLink
roda na UART e o USB Mass Storage nao e possivel nestas placas.

O codigo do USB nativo (CDC dupla via TinyUSB, `main/USBDevice/USBDevice.cpp`)
permanece no repositorio, dormente atras de `CONFIG_KRYONOS_USB_NATIVE`
(menu KryonOS), pronto para placas cujos GPIO19/20 estejam livres — nesse
cenario o KryonLink migra para a CDC1 sem mudancas de protocolo.
