# Arquitetura

[English](/maikramer/CelerOS/wiki/Architecture) | **Português (BR)**

Como o firmware está organizado, o que roda a partir de quê e onde mexer em
cada tipo de mudança.

## Visão geral em camadas

```
+--------------------------------------------------------------+
| data/  (particao LittleFS em /local)                         |
|   Apps de sistema em JavaScript: Settings, App Store,         |
|   Installer, Terminal, Help, Web Server, Snake, demos        |
+------------------------------^-------------------------------+
                               | Duktape (ES5)
+------------------------------|-------------------------------+
| main/  (componente app do ESP-IDF, C++)                      |
|   Kernel/Core  CelerKernel: heap Duktape, roda o main.js     |
|   Runtime      JSBindings: globals System / Net / FS          |
|   UI           Kui (canvas/screen/widget), teclado acoplado  |
|   Launcher     descobre apps em /local/apps e /sd/apps       |
|   Display      Layout 240x320 virtual -> fisico, tema, icones|
|   FileSystem   LittleFS (/local) + SD (/sd), escrita atomica |
|   WebManager   httpd + file manager + upload OTA + portal    |
|   OTA          OtaManager: update.json v2 + esp_https_ota    |
|   USBDevice    CelerShell, HostLink (celerctl), SerialLink  |
|   Bluetooth    CelerLink: BLE entre aparelhos (Celer Link) |
|   Boards/<b>/  HAL por placa (pinos, display, traits)        |
+------------------------------^-------------------------------+
                               | inclui/REQUIRES
+------------------------------|-------------------------------+
| components/  (vendados da lib esp_components + terceiros)    |
|   Wifi, Connection (NetworkManager: unico dono do radio),    |
|   Http, System (SystemInfo), Utility (Event/Singleton),      |
|   ErrorCodes, JsonModels, config, LovyanGFX, duktape         |
+--------------------------------------------------------------+
```

Os componentes `BluetoothServer`, `Drivers`, `IoUtility`,
`SafeContainers`, `Supabase`, `UI` (LVGL), `UserManaging` e `Time` são
excluídos da build pelo `EXCLUDE_COMPONENTS` da raiz — estão dormantes.

Atenção aos nomes: `main/` tem os seus próprios `TimeManager`, `OtaManager`,
`UI/Kui` e `UI/Keyboard`, que **não** são as classes de mesmo nome em
`components/`.

## Boot flow

`app_main` chama `celerSetup()` uma vez e `celerLoop()` para sempre
([main/main.cpp](main/main.cpp)):

1. `Board::init` — HAL da placa compilada (`main/Boards/<placa>/`)
2. `UI::init(w, h)` — display e layout virtual 240x320
3. `FileSystem::init` — LittleFS em `/local`, SD em `/sd`
4. `SerialLink` → `USBDevice` — canal do `celerctl`
5. `Backlight` → `TimeManager` (NTP/fuso) → `WebManager::startAsync`
6. `CelerKernel::init` — heap Duktape
7. `LauncherUI` (ou calibração de toque no primeiro boot da CYD) e
   `kui::Navigator::begin` + push do launcher

O loop principal roda na task main: `Navigator::tick()`,
`WebManager::tick()` (reboot adiado pós-OTA web), `TimeManager::tick()`,
`delay(5)`.

## O runtime JavaScript

* Globals expostas pelos bindings (`main/Runtime/JSBindings.cpp` +
  `Js{System,Net,Fs,Gfx,Gpio,Keypad,Link,SystemApps}.cpp`): `System`, `Net`,
  `FS` e `CelerLink` (o link BLE, em builds com Bluetooth).
* Apps são ES5 puro (sem `Promise` — o builtin foi compilado fora do
  Duktape), com loop `while` bloqueante e `System.delay()`.
* Toda geometria é desenhada em coordenadas virtuais 240x320 e escalada por
  `UI::sx()/sy()` — apps nunca veem pixels físicos.
* O **nível de API** (`System.getAPILevel()`, hoje 12) é o contrato de
  feature-detection dos apps. Histórico: 1 base (draw/touch/GPIO/FS/time) ·
  2 `Net` · 3 apps de sistema em JS · 5 teclado acoplado · 6 topbar
  customizada + `Net.download` em streaming · 7 LED RGB, sensor de luz com
  brilho automático, alto-falante e prompts mascarados · 8 relés ·
  9 Celer Link (BLE) · 10 servos + hardware de robô (bateria, microfone,
  pad de toque, NeoPixel) · 11 pareamento por código no Celer Link +
  `{hint:"num"}` no teclado (página numérica) · 12 `setTimeout`/`setInterval`,
  `Storage` (NVS privado), sprites múltiplos, `FS.readFile`/`writeFile`
  binário e `setTextDatum`.
* Ao adicionar/chamar uma API nova: bump de `CELEROS_API_LEVEL`, doc nos
  dois idiomas do `JS_API_Guide` e stub no harness `test/js_harness/run.js`.

## Onde mexer

| Tarefa | Onde |
|---|---|
| Adicionar um arquivo-fonte | `CELEROS_SRCS` em [main/CMakeLists.txt](main/CMakeLists.txt) (não é glob) |
| Bump de versão / nível de API | `main/CMakeLists.txt` (`CELEROS_VERSION`, `CELEROS_API_LEVEL`) **e** `project(VERSION)` da raiz — manter os dois em sincronia |
| Nova placa | `main/Boards/<b>/` + `elseif` no CMake + `boards/<b>/sdkconfig.defaults` + `updates/<canal>/update.json` |
| Gestos/toque | `UI/Kui.cpp` (TouchPump; toques injetados pelo celerctl passam pelo TouchInjector) |
| Lado dispositivo do celerctl | `USBDevice/HostLink.cpp` (opcodes), `SerialLink.cpp` (UART) |
| Celer Link (BLE entre aparelhos) | `Bluetooth/CelerLink.cpp` + `Runtime/JsLink.cpp` (global `CelerLink`) |
| Página web do file manager e do espelho de tela | `WebManager/*.html` (re-embutidas como headers gzipped na build) |
| Radio Wi-Fi / credenciais | `components/Connection` (`NetworkManager` singleton) |

## Convenções que valem a pena conhecer

* **Nunca** `#ifdef` de placa fora de `main/Boards/<placa>/` — diferenças de
  compilação vivem no `BoardTraits.h` da placa.
* Logging por `celer_log_printf/println` (vai para UART/CDC **e** para o
  buffer do `logcat`), nunca `printf` cru.
* Estado persistente via `FileSystem::writeTextFile` (tmp + rename,
  resistente a perda de energia).
* Operações falíveis retornam `ErrorCode` (registro em `components/ErrorCodes`),
  não `esp_err_t`.
* Singletons: `class X : public Singleton<X>` com ctor recebendo `token`.
* Handlers de `Event<...>` só setam flags — o `trigger()` segura o mutex
  durante todo o disparo.
* Nunca chame `Storage::initialize()` de components: ele montaria SPIFFS
  sobre a partição que o CelerOS formata como LittleFS. O filesystem do OS é
  `main/FileSystem`.
* Comentários, logs e strings de CLI são em português, sem acentos.

## Pontos quentes do código

| Arquivo | ~LOC | Papel |
|---|---|---|
| `main/Runtime/JSBindings.cpp` + `Js*.cpp` | ~2,9 K | toda a superfície JS |
| `main/WebManager/WebManager.cpp` | ~960 | httpd, file manager, upload OTA, espelho de tela |
| `main/Bluetooth/CelerLink.cpp` | ~1070 | Celer Link: GATT NimBLE + advertising + pareamento por codigo (API 11) |
| `main/UI/Kui.cpp` | ~900 | framework de UI immediate-mode |
| `main/Launcher/Screens.cpp` | ~810 | telas de sistema |
| `main/FileSystem/FileSystem.cpp` | ~640 | FS atômico + MD5 |

Mais detalhe por diretório: os `AGENTS.md` de [main/](main/AGENTS.md),
[components/](components/AGENTS.md),
[main/Runtime/](main/Runtime/AGENTS.md) e
[Network/](components/Network/AGENTS.md) no repositório.
