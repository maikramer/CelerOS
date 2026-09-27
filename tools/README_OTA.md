# CelerOS OTA updates

**English** | [Português (BR)](README_OTA.pt-BR.md)

CelerOS has two firmware update mechanisms, both ported from the
[satisfaction-hub](https://github.com/maikramer) concept and now native to
ESP-IDF:

1. **Updates channel OTA** — the device polls an `update.json`, compares
   versions and flashes the firmware by itself with a progress bar
   (Settings → System Updates → **INSTALL**).
2. **Web upload** — with the web server on, the file manager's `/update`
   page flashes a `firmware.bin` uploaded from the browser.

The partition tables (`partitions_16MB.csv` SmartDisplay and
`partitions_4MB.csv` CYD) already have `ota_0`/`ota_1` slots + `otadata`.
Flashing is done by the `WifiOta` component (`esp_https_ota`): written to the
inactive slot and only activated after validation (checksum in
`esp_https_ota_finish`) — if anything fails midway, the running system stays
up. Wi-Fi credentials survive updates: they live in NVS
(NetworkCredentialStore).

## update.json scheme (v2)

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

- Without `firmware_url`, the update screen falls back to the legacy flow
  (changelog + manual guide only). With it, the **INSTALL** button appears.
- A relative `firmware_url` (e.g. `"firmware.bin"`) resolves against the
  directory of the `update.json` itself — handy for local servers.
- Channel per board: `updates/esp32/update.json` (classic board) and
  `updates/smartdisplay_4848S040/update.json` (SmartDisplay 4"). The base is
  `CELEROS_UPDATE_BASE` in `main/OTA/OtaManager.cpp`.

## Publishing a release to the CelerOS Hub (official channel)

The canonical channel is the self-hosted hub: `https://os.celer.tec.br/updates`
(see `CELEROS_UPDATE_BASE` in `main/OTA/OtaManager.cpp`). Server repo:
`~/GitClones/CelerOS-Server` (denv stack `celeros-hub`).

```bash
idf.py -B build-cyd build    # or build-smartdisplay
python3 ~/GitClones/CelerOS-Server/tools/publish_firmware.py esp32 \
  build-cyd/CelerOS.bin --version 1.3.0 --changelog "- new thing X" \
  --token $CELER_HUB_TOKEN
# SmartDisplay 4": channel smartdisplay_4848S040
```

Each channel's `update.json` (`esp32`, `smartdisplay_4848S040`) is generated
by the publish script; publishing without the `.bin` path updates the
manifest only (the device shows the news, without the INSTALL button). The
`updates/*/update.json` files in this repo remain as a legacy mirror of the
old GitHub channel.

## Testing on the LAN (without the hub)

```bash
idf.py -B build-cyd build
python3 tools/ota_server.py --board smartdisplay
```

The server prints the URL to write into **`/local/ota_url.txt`** on the
device (via the web file manager or SD card). While that file exists it
overrides the official hub channel — delete it to go back to normal.
The published version comes from `CELEROS_VERSION`
(`main/CMakeLists.txt`); for the device to "see" the update, the published
version must be greater than the installed one.

## Testing over the USB cable (no network, no esptool)

With the board on the serial cable, `celerctl` flashes the firmware straight
into the inactive OTA partition and reboots:

```bash
python3 tools/celerctl.py -b 921600 ota push build/CelerOS.bin
```

Details in [README_USBTOOL.md](README_USBTOOL.md).

## W8 migration (new partition table on the CYD) — cable reflash

W8 shrank the core (system screens became JS apps on LittleFS) and the CYD
table changed (`partitions_4MB.csv`): OTA slots 1.875 MB → 1.75 MB each and
LittleFS 128 KB → 384 KB. **OTA over the network does not rewrite the
partition table** — migrating a firmware with the old table is a one-time
cable operation:

```bash
idf.py -B build-cyd build
python3 -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 write-flash \
  0x1000 build-cyd/bootloader/bootloader.bin 0x8000 build-cyd/partition_table/partition-table.bin \
  0xe000 build-cyd/ota_data_initial.bin 0x10000 build-cyd/CelerOS.bin
tools/flash_data.sh cyd /dev/ttyUSB0    # LittleFS: icons + system apps
```

After that, normal OTAs (network, web or `celerctl ota push`) work again
without the cable. The SmartDisplay 4" (16 MB) never changed tables.

System apps (Settings, App Store, Installer, Help, Web Server) now live on
LittleFS and are not touched by OTA — update them with
`celerctl apps install data/apps/<Name>` or from the App Store itself.

> Publishing 1.2.0 on the channel: the manifests under `updates/` stay at
> 1.1.0 until the CYD boards in the field have done the cable migration
> above (a new firmware with the old table would still fit the slot, but the
> 128 KB LittleFS cannot hold the system apps). When publishing, update
> `version`/`firmware_url` in the board channel manifest.

## Known limitations

- TLS without certificate validation (`setInsecure`), inherited from the
  original updater — CA pinning is future work.
- The ESP-IDF bootloader rollback is not enabled in the firmware, so there
  is no automatic post-boot rollback; the protection is the checksum
  validation before slot activation.
- Neither the updates channel nor `/update` have authentication (the same
  goes for the whole file manager).
