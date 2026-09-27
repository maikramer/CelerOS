# Atualização OTA do CelerOS

O CelerOS tem dois mecanismos de atualização de firmware, ambos portados do
conceito do [satisfaction-hub](https://github.com/maikramer) e hoje nativos do
ESP-IDF:

1. **OTA pelo canal de updates** — o dispositivo consulta um `update.json`,
   compara versões e flashea o firmware sozinho com barra de progresso
   (Settings → System Updates → **INSTALL**).
2. **Upload web** — com o servidor web ligado, a página `/update` do file
   manager flashea um `firmware.bin` enviado pelo navegador.

As tabelas de partição (`partitions_16MB.csv` SmartDisplay e
`partitions_4MB.csv` CYD) já têm slots `ota_0`/`ota_1` + `otadata`. O flash é
feito pelo componente `WifiOta` (`esp_https_ota`): gravado no slot inativo e só
ativado depois de validado (checksum no `esp_https_ota_finish`) — se algo
falhar no meio, o sistema atual continua no ar. Credenciais WiFi não se perdem
com update: vivem no NVS (NetworkCredentialStore).

## Esquema do update.json (v2)

```json
{
  "version": "1.1.0",
  "api_version": 2,
  "major_update": true,
  "minor_update": false,
  "security_update": false,
  "changelog": "- Feature A\n- Feature B",
  "guide": "Tap INSTALL to update directly.",
  "firmware_url": "https://.../firmware.bin"
}
```

- Sem `firmware_url`, a tela de update cai no fluxo legado (mostra só o
  changelog + guia manual). Com o campo, aparece o botão **INSTALL**.
- `firmware_url` relativa (ex.: `"firmware.bin"`) resolve contra o diretório
  do próprio `update.json` — útil para servidores locais.
- Canal por placa: `updates/esp32/update.json` (placa clássica) e
  `updates/smartdisplay_4848S040/update.json` (SmartDisplay 4"). A base está
  em `CELEROS_UPDATE_BASE` em `main/OTA/OtaManager.cpp`.

## Publicar uma versão no CelerOS Hub (canal oficial)

O canal canônico é o hub próprio: `https://os.celer.tec.br/updates` (ver
`CELEROS_UPDATE_BASE` em `main/OTA/OtaManager.cpp`). Repo do servidor:
`~/GitClones/CelerOS-Server` (stack denv `celeros-hub`).

```bash
idf.py -B build-cyd build    # ou build-smartdisplay
python3 ~/GitClones/CelerOS-Server/tools/publish_firmware.py esp32 \
  build-cyd/CelerOS.bin --version 1.3.0 --changelog "- novidade X" \
  --token $CELER_HUB_TOKEN
# SmartDisplay 4": canal smartdisplay_4848S040
```

O `update.json` de cada canal (`esp32`, `smartdisplay_4848S040`) é gerado pelo
publish; publicar sem o caminho do `.bin` atualiza só o manifest (o aparelho
mostra as novidades, sem botão INSTALL). Os `updates/*/update.json` deste repo
ficaram como espelho legado do canal antigo no GitHub.

## Testar na LAN (sem o hub)

```bash
idf.py -B build-cyd build
python3 tools/ota_server.py --board smartdisplay
```

O servidor imprime a URL para gravar em **`/local/ota_url.txt`** no
dispositivo (pelo web file manager ou cartão SD). Enquanto esse arquivo
existir, ele substitui o canal oficial do hub — apague-o para voltar ao
normal.
A versão publicada vem de `CELEROS_VERSION` (`main/CMakeLists.txt`); para o
dispositivo "ver" a atualização, a versão precisa ser maior que a instalada.

## Testar pelo cabo USB (sem rede e sem esptool)

Com a placa ligada no cabo serial, o `celerctl` grava o firmware direto
na partição OTA inativa e reinicia:

```bash
python3 tools/celerctl.py -b 921600 ota push build/CelerOS.bin
```

Detalhes em [README_USBTOOL.md](README_USBTOOL.md).

## Migração W8 (tabela de partições nova na CYD) — reflash por cabo

O W8 encolheu o core (telas de sistema viraram apps JS no LittleFS) e a
tabela da CYD mudou (`partitions_4MB.csv`): slots OTA de 1,875 MB → 1,75 MB
cada e LittleFS de 128 KB → 384 KB. **OTA em rede não reescreve tabela de
partições** — a migração de um firmware com tabela antiga é uma única vez,
por cabo:

```bash
idf.py -B build-cyd build
python3 -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 write-flash \
  0x1000 build-cyd/bootloader/bootloader.bin 0x8000 build-cyd/partition_table/partition-table.bin \
  0xe000 build-cyd/ota_data_initial.bin 0x10000 build-cyd/CelerOS.bin
tools/flash_data.sh cyd /dev/ttyUSB0    # LittleFS: ícones + apps de sistema
```

Depois disso as OTAs normais (rede, web ou `celerctl ota push`) voltam a
funcionar sem cabo. O SmartDisplay 4" (16 MB) não mudou de tabela.

Os apps de sistema (Settings, App Store, Installer, Help, Web Server) agora
vivem no LittleFS e não são afetados por OTA — atualize-os com
`celerctl apps install data/apps/<Nome>` ou pela própria App Store.

> Publicar 1.2.0 no canal: os manifests em `updates/` seguem em 1.1.0 até
> que as placas CYD em campo tenham feito a migração por cabo acima (um
> firmware novo com tabela antiga até caberia no slot, mas o LittleFS de
> 128 KB não comporta os apps de sistema). Quando for publicar, atualize
> `version`/`firmware_url` do manifest do canal da placa.

## Limitações conhecidas

- TLS sem validação de certificado (`setInsecure`), herdado do updater
  original — pin de CA fica como evolução futura.
- O bootloader rollback do ESP-IDF não está habilitado no core Arduino
  pré-compilado, então não há rollback automático pós-boot; a proteção é a
  validação de checksum antes da ativação do slot.
- Nem o canal de updates nem o `/update` têm autenticação (o mesmo vale para
  o file manager inteiro).
