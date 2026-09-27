# Atualização OTA do KryonOS

O KryonOS tem dois mecanismos de atualização de firmware, ambos portados do
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
  em `KRYONOS_UPDATE_BASE` em `src/OTA/OtaManager.cpp`.

## Publicar uma versão no GitHub

1. Suba `KRYONOS_VERSION` no `platformio.ini` e faça o build da(s) env(s).
2. Copie `.pio/build/<env>/firmware.bin` para `updates/<canal>/firmware.bin`.
3. Atualize `updates/<canal>/update.json` (versão, changelog, flags) e push.

## Testar na LAN (sem GitHub)

```bash
pio run -e smartdisplay_4848S040
python3 tools/ota_server.py --env smartdisplay_4848S040
```

O servidor imprime a URL para gravar em **`/local/ota_url.txt`** no
dispositivo (pelo web file manager ou cartão SD). Enquanto esse arquivo
existir, ele substitui o canal do GitHub — apague-o para voltar ao normal.
A versão publicada vem do `KRYONOS_VERSION` do `platformio.ini`; para o
dispositivo "ver" a atualização, a versão precisa ser maior que a instalada.

## Testar pelo cabo USB (sem rede e sem esptool)

Com a placa ligada no cabo serial, o `kryonctl` grava o firmware direto
na partição OTA inativa e reinicia:

```bash
python3 tools/kryonctl.py -b 921600 ota push build/KryonOS.bin
```

Detalhes em [README_USBTOOL.md](README_USBTOOL.md).

## Limitações conhecidas

- TLS sem validação de certificado (`setInsecure`), herdado do updater
  original — pin de CA fica como evolução futura.
- O bootloader rollback do ESP-IDF não está habilitado no core Arduino
  pré-compilado, então não há rollback automático pós-boot; a proteção é a
  validação de checksum antes da ativação do slot.
- Nem o canal de updates nem o `/update` têm autenticação (o mesmo vale para
  o file manager inteiro).
