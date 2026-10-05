# celerctl — ferramenta USB/serial do CelerOS (estilo adb)

[English](README_USBTOOL.md) | **Português (BR)**

`celerctl.py` conversa com o firmware pelo canal **HostLink**: um protocolo
binario leve que roda sobre a UART do console — na pratica, o CH340 que o
PC ve como `/dev/ttyUSB*` — na CDC1 do USB nativo
(`CONFIG_CELEROS_USB_NATIVE`, ex. o watch Waveshare) ou na propria
USB-Serial/JTAG do S3 (`CONFIG_CELEROS_LINK_ON_USJ`, ex. o cao SpotPear).
Uma ferramenta so, todas as placas.

## Protocolo do fio (proto 1 e 2)

```
proto 1: [0x43 'C'][cmd u8][len u16 LE][payload]
proto 2: [0x43 'C'][cmd u8][len u16 LE][crc32 u32][payload]
```

O formato da sessao e negociado no HELLO: a ferramenta envia o payload
`"CELERCTL2"` e um firmware proto 2 responde `...|proto 2|chunk W|win K`,
ativando CRC32 em todo frame e a janela deslizante de chunks. Firmware
antigo responde `proto 1` e a sessao segue exatamente como antes —
ferramenta nova conversa com firmware antigo e vice-versa (`--proto 1`
forca o caminho legado de proposito). O CRC32 e o classico 0xEDB88320
(mesmo valor do `zlib.crc32` do Python).

Extras do proto 2 no fio:

- payload de `WRITE_CHUNK`/`OTA_CHUNK` e `[seq u16][dados]`; o ACK e
  `[proximo seq u16][total aplicado u32]` — chunk reenviado cujo ACK se
  perdeu **nao** e gravado 2x, e o CRC/tamanho final cobre o arquivo/imagem
  de ponta a ponta (OTA corrompida nunca chega a marca de boot).
- respostas de `READ` vem com o offset prefixado: pulls pipelined.
- `LS` aceita cursor (diretorios grandes paginam), `DELETE` aceita flag
  recursiva, `COREDUMP` aceita flag para nao apagar.

Os opcodes vivem em `main/USBDevice/HostLink.h` (o framing em si esta no
`HostFrame.h`, testado no host) e a ferramenta le esse arquivo por regex:
so existe uma fonte da verdade para os dois lados. O
`test/test_celerctl.py` exercita a ferramenta contra um device simulado
(chunk perdido, byte corrompido, modo legado).

## Instalacao

```bash
pip install -r tools/requirements.txt   # pyserial (screencap pede Pillow)
```

## Comandos

```bash
python3 tools/celerctl.py devices            # lista placas conectadas (+ serial USB)
python3 tools/celerctl.py info               # versao/board/heap/rede/FS
python3 tools/celerctl.py shell              # shell interativo (help)
python3 tools/celerctl.py shell "ls /local"  # executa e imprime
python3 tools/celerctl.py ls -l /local/apps
python3 tools/celerctl.py cat /local/wifi.txt
python3 tools/celerctl.py push app.zip /local/tmp_download/app.zip
python3 tools/celerctl.py pull /local/apps/HTTP\ Demo/app.json .
python3 tools/celerctl.py rm /local/old.txt
python3 tools/celerctl.py rm -r /local/apps/AppVelho   # apaga recursivo
python3 tools/celerctl.py reboot
python3 tools/celerctl.py logcat             # logs ao vivo (Ctrl-C sai)
python3 tools/celerctl.py top                # foto de profiling: CPU% por task, stack, heap
python3 tools/celerctl.py top -w --sort cpu  # ao vivo como o top classico (Ctrl-C sai)
python3 tools/celerctl.py stats --json       # uma foto crua em JSON (taxas desde o boot)
python3 tools/celerctl.py ota push build/CelerOS.bin   # firmware sem esptool
python3 tools/celerctl.py screencap tela.png # captura do display -> PNG (RLE: ~10x mais rapida)
python3 tools/celerctl.py coredump            # dump do ultimo crash (ELF) -> coredump.elf
python3 tools/celerctl.py coredump --keep     # baixa sem apagar o dump
python3 tools/celerctl.py shell "run Snake"  # abre um app (pasta, nome ou pacote)
python3 tools/celerctl.py tap 120 160        # injeta um toque (navegar via USB)
python3 tools/celerctl.py swipe 120 400 120 40  # injeta um arrasto (scroll)
python3 tools/celerctl.py apps list             # apps instalados (local + sd)
python3 tools/celerctl.py apps install "data/apps/Web Server"  # instala pasta
python3 tools/celerctl.py apps install meuapp --sd             # no cartão
python3 tools/celerctl.py apps rm "Touch Test"                  # desinstala
```

### Varias placas na mesma maquina

O `-p` aceita caminho da porta **ou prefixo do serial USB da placa** (o
serial `K...` derivado da MAC que os firmwares S3 expoem). O `devices`
lista:

```bash
python3 tools/celerctl.py devices          # mostra o serial de cada placa
python3 tools/celerctl.py -p K7B4 screencap dog.png
```

### Iterar na UI sem tocar na placa

`tap`/`swipe` + `screencap` formam um laco adb-like: o gesto e enfileirado
no firmware (opcode `KL_TOUCH`), executado pelo TouchPump da UI como se
fosse dedo fisico — vale para o Navigator e para modais (teclado QWERTY) —
e o `screencap` le o framebuffer real. Exemplo de sessao:

```bash
python3 tools/celerctl.py tap 360 88 && python3 tools/celerctl.py screencap s.png
```

### Depurando apps JS (breakpoints, step, eval)

Placas com `CONFIG_CELEROS_JS_DEBUGGER` (padrao nas S3; desligado no CYD,
onde slot OTA e RAM sao o limite) falam o protocolo do debugger do Duktape
pelo mesmo canal (`KL_DEBUG_CTL`/`KL_DEBUG_DATA`):

```bash
python3 tools/celerctl.py debug Snake          # proxy + REPL neste terminal, com os logs do app
python3 tools/celerctl.py debug Snake --serve  # so o proxy (dmsg em :9092, logs em :9093)
node tools/debug/dbg.js                        #   ...e o REPL em outro terminal
```

O app pausa quando o debugger attacha (na primeira linha; sem nome de app, o
app que ja roda attacha no proximo yield), e erro nao capturado pausa no
throw. `h` lista os comandos: breakpoints (`b 42 if x > 3`, `tb`,
`u <linha>`, `B`, `d`, `cond`), `c`/`p` (ou Ctrl-C), `s`/`n`/`o`, inspecao
com objetos em JSON montado no device (`v`, `e`, `lc`, `cs`, `up`/`down`,
`set`), watches (`w`), `l` (fonte local), `i` (heap) e `r`: linta e envia a
pasta local editada do app e reinicia com os breakpoints restaurados. O
stdin pode ser um roteiro: cada comando espera a resposta, e `c`/`s`/`n`/`o`
esperam a proxima pausa.

Regras de robustez: o handshake e unilateral (o device manda a linha de
versao; o cliente nao escreve antes dela); `q`, fechar o cliente, matar o
proxy (ate `kill -9`: o device solta o cliente quando a sessao do host
expira em 8s) ou puxar o cabo desattacham e o app segue rodando. Protocolo e
cliente tem cobertura no host em `node test/debug/run.js` (alvo Duktape
falso).

## Acelerando transferencias (-b)

O canal UART nasce a 115200 baud. Com `-b 921600` a ferramenta negocia a
troca com o firmware e reabre a porta mais rapida:

```bash
python3 tools/celerctl.py -b 921600 push firmware.bin /sd/fw.bin
```

Medido com proto 2 (janela deslizante + CRC32; firmware 1.4.1, arquivo de
1 MB com conferencia de md5 na volta): push ~78 KB/s na UART do S3 a
921600 baud (`-b 921600` — sem ele o push fica nos ~11 KB/s do baud
padrao 115200), ~113 KB/s na USJ do dog e ~110 KB/s no CDC do watch; pull
integro nas tres vias. Proto 1 (stop-and-wait, para comparar): push
~57 KB/s, pull ~190 KB/s. Janela anunciada pelo firmware: 4 chunks nas
UARTs/USJ do S3 (anel RX de 48 KB), 1 na CYD (heap sem PSRAM nao segura
janela), 8 no CDC. Portas USB nativas (CDC/USJ) nao tem baud: `-b` avisa
e segue na velocidade do USB.

A janela anunciada pelo firmware e a que o canal dele consegue segurar.
Para atualizar por OTA um firmware ANTIGO cuja janela anuncia mais do que
o buffer dele aguenta (ex.: watch com janela 8 antes do fix do buffer
CDC), limite na mao:

```bash
python3 tools/celerctl.py --win 2 -p <porta> ota push CelerOS.bin
```

## Como o canal convive com o console

A UART do console multiplexa dois modos (`main/USBDevice/SerialLink.cpp`)
— o mesmo esquema roda na USB-Serial/JTAG com `CELEROS_LINK_ON_USJ` ligado:

- **console** — shell interativo humano (eco, prompt `celer> `) e logs
  ESP_LOG/Serial visiveis. Abrir o minicom/monitor e usar.
- **link** — acionado pela chegada de um frame `HELLO (`0x43 0x01 ...`);
  digitacao humana nao produz essa sequencia. Logs sao suspensos no canal
  (teclados num ring) e a sessao volta ao console apos ~8s sem frames,
  restaurando o baud. Abrir sessao em outro canal (ex. CDC1) assume como
  ativo.

Com `logcat`, o ring e drenado (historico desde o boot) e os logs seguem
como frames ao vivo na propria ferramenta — pelo canal ativo (UART ou CDC).

## Nota sobre USB nativo / Mass Storage

O ESP32-S3 tem USB-OTG (GPIO19/20), mas **na SmartDisplay 4848S040 esses
pinos sao usados pela placa** (GPIO19 = SDA do touch GT911, GPIO20 = linha
G1 do display RGB) e o conector USB e apenas CH340. Por isso o HostLink
roda na UART e o USB Mass Storage nao e possivel nestas placas.

O USB nativo (CDC dupla via TinyUSB, `main/USBDevice/USBDevice.cpp`) esta
vivo no watch Waveshare (`CONFIG_CELEROS_USB_NATIVE`: CDC0 shell + CDC1
celerctl) e dormente para placas cujos GPIO19/20 ficarem livres. O cao
SpotPear pega o terceiro caminho: a unica USB dele e a USB-Serial/JTAG,
entao o `CONFIG_CELEROS_LINK_ON_USJ` multiplexa console+link nessa mesma
porta — sem sacrificar console, OpenOCD ou esptool ROM.
