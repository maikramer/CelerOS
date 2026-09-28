# CelerOS JavaScript Engine - Comprehensive Reference Manual

**English** | [Português (BR)](JS_API_Guide.pt-BR.md)

Welcome to the **CelerOS JavaScript API Reference**. This document provides deep technical details on the underlying JavaScript engine specifications, performance characteristics, and every native API exposed by the C++ kernel for interacting with the ESP32 hardware.

---
## CelerOS JS Runtime Version
### JS Runtime: v1.0.0
### API Level: 6
---

## 1. Engine Specifications & ECMAScript Compliance

**CelerOS JavaScript Runtime** uses the **Duktape 2.x**

### 1.1 ECMAScript Compliance
- **ES5 / ES5.1 Compliant:** The engine is fully compliant with the ECMAScript 5.1 specification. 
- **Partial ES6 (ES2015) Support:** Supports modern built-ins such as `TypedArrays` (Uint8Array, Int32Array, etc.), `Proxy`, and `Reflect`.
- **Unsupported Modern Syntax:** Because it prioritizes ultra-low memory, modern syntactic sugar is **NOT SUPPORTED**. You cannot use:
  - Arrow functions `() => {}`
  - `let` and `const` (Use `var`)
  - ES6 `class` definitions (Use traditional prototype-based inheritance)
  - Template literals `` `string ${var}` ``
  - `Promise` (the built-in is compiled out to save flash — use the blocking calls instead, e.g. `Net.get`)

### 1.2 Memory & Performance Limits
- **Execution Strategy:** Bytecode compiled natively and executed by a virtual stack machine.
- **Garbage Collection (GC):** Reference counting frees most objects immediately; a full mark-and-sweep (for cycles) runs from `System.delay(ms)` at most once per second, or right away when the heap is tight. The time spent collecting is subtracted from the requested delay.
- **Maximum Heap Size:** ~90KB of usable free RAM per script (when WiFi is disabled). Always minimize dynamic array allocations inside high-speed animation loops.

---

## 2. Global Object: `System`

The `System` object provides low-level hardware-accelerated bindings to the ESP32 OS.

### Display Properties

#### `System.screenWidth()`
- **Returns:** `Integer` — always `240` (CelerOS 1.1+ virtual canvas).
- **Description:** Width of the design canvas apps draw in. On larger panels the runtime scales everything to the physical screen (see the virtual canvas note in section 3).

#### `System.screenHeight()`
- **Returns:** `Integer` — always `320` (CelerOS 1.1+ virtual canvas).
- **Description:** Height of the design canvas apps draw in.

> Hardcoding 240x320 is the intended pattern: the runtime scales drawing,
> sprites and touch to the physical panel, so the same app renders identically
> (and fullscreen) on every board.

### OS Utilities

#### `System.getOSVersion()`
- **Returns:** `String` (e.g., `"1.0.0"`)
- **Description:** Returns the current OS version string.

#### `System.getAPILevel()`
- **Returns:** `Integer` (e.g., `1`)
- **Description:** Returns the OS API Level integer.

#### `System.millis()`
- **Returns:** `Integer`
- **Description:** Returns the total uptime of the ESP32 hardware in milliseconds since the device booted. Used for delta-time physics and loop timing.

#### `System.micros()`
- **Returns:** `Integer`
- **Description:** Returns the total uptime of the ESP32 hardware in microseconds since the device booted. Essential for extreme high-resolution timing (e.g., custom bit-banged protocols). Note that the 32-bit integer rolls over every ~71 minutes.

#### `System.getTemperature()`
- **Returns:** `Float`
- **Description:** Reads the ESP32's internal core temperature sensor and returns the value in Celsius.

#### `System.hasTemperatureSensor()`
- **Returns:** `Boolean`
- **Description:** Checks if the currently installed ESP32 hardware revision actually supports the internal temperature sensor (some newer chips remove it). Returns `true` if supported.

#### `System.delay(ms)`
- **Parameters:** `ms` (Integer) - The amount of milliseconds to pause execution.
- **Returns:** `undefined`
- **Description:** Pauses JavaScript execution. **CRITICAL:** This function commands the C++ kernel to perform Garbage Collection in the background. If you have an infinite `while(true)` loop, you MUST include a `System.delay(10)` call to prevent the OS from crashing due to heap exhaustion.

#### `System.delayMicroseconds(us)`
- **Parameters:** `us` (Integer) - The amount of microseconds to pause execution.
- **Returns:** `undefined`
- **Description:** Provides highly accurate sub-millisecond delays natively. This blocks the CPU execution cleanly, without triggering Garbage Collection.

#### `System.print(str)`
- **Parameters:** `str` (String)
- **Returns:** `undefined`
- **Description:** Prints a message to the physical USB Serial Monitor on a connected computer (Baud rate 115200). Useful for debugging variables when the screen is rendering frames.

#### `System.getTouch()`
- **Returns:** `Object` -> `{ x: Integer, y: Integer, touched: Boolean }`
- **Description:** Polls the SPI Touch Controller. 
  - `touched` is `true` if a finger/stylus is pressing the screen.
  - `x` and `y` represent pixel coordinates. If `touched` is `false`, `x` and `y` default to 0.
- **Hidden Exit Trigger:** If a user touches `x >= 200` and `y <= 40` (Top-Right corner), the C++ Kernel will instantly abort the JS Engine and force-close the app to prevent users from getting permanently locked out of the OS.

#### `System.getInfo()`
- **Returns:** `Object` -> `{ totalRAM: Integer, freeRAM: Integer, minFreeRAM: Integer, maxAllocRAM: Integer, cpuFreqMHz: Integer, chipModel: String, chipCores: Integer, chipRevision: Integer, flashSize: Integer, uptimeMs: Integer }`
- **Description:** Returns an object containing the current state of the ESP32 hardware, including memory usage, CPU speed, and hardware specifications. Useful for debugging memory leaks and checking uptime.
  - `minFreeRAM`: The lowest free RAM amount recorded since boot.
  - `maxAllocRAM`: The largest single contiguous block of RAM you can allocate.

#### `System.getIPAddress()`
- **Returns:** String
- **Description:** Returns the current local IP address of the ESP32 (e.g. "192.168.1.11") if WiFi is connected.

#### `System.isWiFiActive()`
- **Returns:** Boolean
- **Description:** Returns `true` if the ESP32 is currently connected to a WiFi network.

#### `System.restart()`
- **Returns:** None
- **Description:** Instantly reboots the ESP32 hardware.

#### `System.getTime()`
- **Returns:** `String` (e.g., `"14:30"` or `"02:30 PM"`)
- **Description:** Returns the OS-formatted current local time, automatically respecting the user's 12-hour or 24-hour preference setting.

#### `System.getSeconds()`
- **Returns:** `Integer` (0-59)
- **Description:** Returns the current local second directly from the RTC.

#### `System.getDate()`
- **Returns:** `String` (e.g., `"15/06/2026"`)
- **Description:** Returns the current local date formatted as DD/MM/YYYY.

#### `System.getYear()`
- **Returns:** `Integer` (e.g., `2026`)
- **Description:** Returns the current local 4-digit year.

#### `System.getMonth()`
- **Returns:** `Integer` (1-12)
- **Description:** Returns the current local month.

#### `System.getDay()`
- **Returns:** `Integer` (1-31)
- **Description:** Returns the current local day of the month.

#### `System.getTimezone()`
- **Returns:** `String` (e.g., `"UTC-8"`)
- **Description:** Returns the user's currently configured timezone offset region.

#### `System.prompt(promptMsg, initialText)`
- **Parameters:** 
  - `promptMsg` (String) - Header text displayed above the keyboard.
  - `initialText` (String) - Text pre-filled into the keyboard input box.
- **Returns:** `String`
- **Description:** Completely suspends JavaScript execution and opens the native C++ full-screen QWERTY touch keyboard (with shift and two symbol pages). Once the user taps "OK", execution resumes and the typed string is returned. Returns an empty string `""` if the user taps "X" (cancel).

---

## 3. Display Drawing Pipeline

**Automatic frame buffer (CelerOS 1.2+, boards with PSRAM):** every draw call goes to an off-screen frame the size of the panel, and the frame is shown on the glass only when the app *yields*: `System.delay()`, `System.getTouch()`, `System.prompt()`, and right before blocking calls (`Net.*`, `System.wifiScan/wifiConnect`, `System.otaCheck/otaStart` — also after each OTA progress callback —, `FS.copyFile/copyDirectory/removeDirectory`). Apps that clear and redraw the whole screen on every event no longer flicker, with no code change. `System.present()` forces the frame to the glass (animations or long computations that never yield); `System.isBuffered()` tells whether the board has the frame (`false` on the CYD, where drawing still goes straight to the TFT).

> Rule of thumb: draw the complete screen, then yield. Something drawn right before a long C++ call that is not listed above appears only at the next yield — call `System.present()` first.

### Color Engine
Colors in JS are always **16-bit RGB565** integers (e.g. `0xF800` = red) on every board — use `System.color(r, g, b)` or `System.theme()` to build them. (CelerOS 1.2 fixed a runtime bug where, on 16-bit panels, RGB565 values were passed through as RGB888 and came out with the wrong hue.)

> **Virtual canvas (CelerOS 1.1+):** apps always run in a **240x320 design canvas**. On boards with larger panels (e.g. the SmartDisplay 4"/480x480), `System.screenWidth()/screenHeight()` report 240/320, all drawing coordinates/sizes are scaled to the physical screen, sprites are allocated at the scaled size, and `System.getTouch()` returns coordinates in the 240x320 space — the same app renders identically (and in fullscreen) on every board. Colors are RGB565 everywhere; the runtime converts to the panel's native format.

#### `System.color(r, g, b)`
- **Parameters:** `r`, `g`, `b` (Integers 0-255)
- **Returns:** `Integer` (16-bit packed color)
- **Description:** Packs 24-bit 8/8/8 RGB color values into the 16-bit 5/6/5 RGB format expected by the hardware.

### Graphics APIs

#### `System.fillScreen(color)`
- **Parameters:** `color` (16-bit Integer)
- **Description:** Floods the entire screen with a single color. Extremely fast as it bypasses the pixel loop and uses hardware SPI DMA directly.

#### `System.drawPixel(x, y, color)`
- **Parameters:** `x` (Int), `y` (Int), `color` (16-bit Int)
- **Description:** Renders a single pixel.

#### `System.drawLine(x1, y1, x2, y2, color)`
- **Parameters:** `x1`, `y1`, `x2`, `y2` (Ints), `color` (16-bit Int)
- **Description:** Uses Bresenham's line algorithm to render a straight line between two points.

#### `System.drawRect(x, y, w, h, color)`
#### `System.fillRect(x, y, w, h, color)`
- **Parameters:** `x`, `y` (Top-Left coords), `w` (Width), `h` (Height), `color` (16-bit Int)
- **Description:** Draws hollow or filled rectangles.

#### `System.drawRoundRect(x, y, w, h, radius, color)`
#### `System.fillRoundRect(x, y, w, h, radius, color)`
- **Parameters:** `x` (Int), `y` (Int), `w` (Int), `h` (Int), `radius` (Int), `color` (Int)
- **Description:** Fills a rectangle with rounded corners using the specified color.

#### `System.drawBMP(path, x, y)`
- **Parameters:** `path` (String), `x` (Int), `y` (Int)
- **Returns:** `Boolean` (`true` if successful, `false` if unsupported or file missing)
- **Description:** Reads a 16-, 24- or 32-bit `.bmp` image from the FileSystem (`/sd/` or `/local/`) and streams it at `x, y` without using JavaScript RAM. The image is scaled with the virtual canvas (a 240-px-wide BMP fills the screen width on every board).
#### `System.drawPNG(path, x, y)`
- **Parameters:** `path` (String), `x` (Int), `y` (Int)
- **Returns:** `Boolean` (`true` if successful, `false` if path/decode failed)
- **Description:** Draws a `.png` image from the FileSystem (`/sd/` or `/local/`) at `x, y`, decoded in streaming line-by-line (no full-framebuffer RAM spike; only the ~44 KB deflate window during decode). PNG alpha is blended over the existing background. Like every other draw call, the image is scaled with the virtual 240x320 canvas (CelerOS 1.2+; before, the PNG kept its native pixel size on large panels). Interlaced PNGs are supported. Ideal for backgrounds and photos; use `System.drawIcon()` for launcher-style 64x64 icons.

#### `System.drawCircle(x, y, radius, color)`
#### `System.fillCircle(x, y, radius, color)`
- **Parameters:** `x`, `y` (Center coords), `radius` (Int), `color` (16-bit Int)
- **Description:** Renders perfect hollow or filled circles.

#### `System.drawTriangle(x1, y1, x2, y2, x3, y3, color)`
#### `System.fillTriangle(x1, y1, x2, y2, x3, y3, color)`
- **Parameters:** `x1, y1, x2, y2, x3, y3` (Vertex coords), `color` (16-bit Int)
- **Description:** Renders hollow or filled triangles. Useful for 3D projections or UI indicators.

#### `System.drawFastVLine(x, y, h, color)`
- **Parameters:** `x, y` (Start coords), `h` (Height), `color` (16-bit Int)
- **Description:** Hardware-accelerated vertical line drawing. Substantially faster than `System.fillRect()` for rendering raycaster slices.

#### `System.drawFastHLine(x, y, w, color)`
- **Parameters:** `x, y` (Start coords), `w` (Width), `color` (16-bit Int)
- **Description:** Hardware-accelerated horizontal line drawing.

---

### Hardware Double Buffering (Mini-Sprites)
Double Buffering allows you to draw shapes invisibly to an off-screen RAM buffer (a Sprite) and then "push" the completed frame to the physical screen in a single instant hardware DMA transfer. This **completely eliminates 3D screen flickering**.

> [!CAUTION]
> **Severe RAM Limitations & Heap Fragmentation**
> The ESP32-WROOM has very limited contiguous RAM (~320KB total, but due to fragmentation from WiFi/WebManager, the max allocatable block is often under ~30KB). Allocating a massive buffer (e.g. `240x320` at 16-bit color takes 153.6 KB) will cause the engine to instantly return `false` from `System.createSprite`.
> **Always** keep your buffers as small as possible. The recommended architecture is **Sliced Rendering**: divide the screen into small horizontal slices (e.g. 10 slices of 32px height) or vertical columns.
> `System.createSprite()` now automatically triggers aggressive Garbage Collection to defragment memory before allocation, and will automatically fall back to lower-quality 8-bit color to prevent crashes if RAM is too fragmented for 16-bit color.

#### `System.createSprite(width, height)`
- **Parameters:** `width, height` (Integer)
- **Returns:** `Boolean` (`true` if successfully allocated a 16-bit or 8-bit color buffer in RAM, `false` if RAM exhausted)
- **Description:** Allocates a persistent off-screen Sprite buffer in RAM. Automatically forces GC and falls back to 8-bit color to secure contiguous memory.

#### `System.bindSprite(enabled)`
- **Parameters:** `enabled` (Boolean)
- **Description:** If `true`, **ALL** subsequent `System.draw...` and `System.fill...` API calls are automatically intercepted and drawn *invisibly* to the persistent Sprite instead of the screen. If `false`, resumes drawing directly to the TFT.

#### `System.pushSprite(x, y)`
- **Parameters:** `x, y` (Integer - Top-left coordinates to paste the buffer on the physical screen)
- **Description:** Pushes the entire hidden buffer onto the physical screen instantly via DMA. The buffer remains in RAM and can be modified and pushed again.

#### `System.deleteSprite()`
- **Description:** Instantly destroys the Sprite and frees the RAM. You must call this when you are done to prevent severe memory leaks!

### Text APIs

#### `System.setTextColor(fg_color, bg_color)`
- **Parameters:** `fg_color` (Foreground), `bg_color` (Background)
- **Description:** Sets the active text rendering colors. Providing a `bg_color` enables hardware-level text overwriting, wiping the previous pixels completely without needing to draw a rectangle manually.

#### `System.setTextSize(size)`
- **Parameters:** `size` (Integer 1-5)
- **Description:** Multiplies the default pixel-font scaling.

#### `System.drawString(text, x, y, font)`
- **Parameters:** 
  - `text` (String) - Text to render.
  - `x`, `y` (Ints) - Top-Left coordinate to begin rendering.
  - `font` (Integer 1, 2, or 4) - Hardware font selection. 2 is standard, 4 is bold/large.
- **Description:** Renders high-speed string buffers to the display.

---

## 4. Hardware GPIO (General Purpose Input/Output)

CelerOS enables direct hardware control of the ESP32 microcontroller pins via `System.gpio`.

### Constants
- `System.gpio.INPUT`
- `System.gpio.OUTPUT`
- `System.gpio.INPUT_PULLUP`
- `System.gpio.HIGH`
- `System.gpio.LOW`

### Functions

#### `System.gpio.pinMode(pin, mode)`
- **Parameters:** `pin` (Integer hardware pin number), `mode` (GPIO Constant)
- **Description:** Sets the physical electrical state of an ESP32 pin (e.g. setting pin 2 to OUTPUT to drive an LED).

#### `System.gpio.digitalWrite(pin, state)`
- **Parameters:** `pin` (Integer), `state` (HIGH or LOW)
- **Description:** Outputs 3.3V (HIGH) or 0V (LOW) to a specific pin.

#### `System.gpio.digitalRead(pin)`
- **Parameters:** `pin` (Integer)
- **Returns:** `Integer` (1 for HIGH, 0 for LOW)
- **Description:** Reads the physical voltage state of a pin.

#### `System.gpio.analogRead(pin)`
- **Parameters:** `pin` (Integer)
- **Returns:** `Integer` (0 to 4095)
- **Description:** Triggers the ESP32 12-bit Analog-to-Digital Converter (ADC) to read a continuous voltage level.

#### `System.gpio.analogWrite(pin, pwmValue)`
- **Parameters:** `pin` (Integer), `pwmValue` (0 to 255)
- **Description:** Initiates an automatic hardware PWM (Pulse Width Modulation) signal on a pin. Useful for motor control or dimming LEDs.

#### `System.gpio.pulseIn(pin, state, [timeout])`
- **Parameters:** `pin` (Integer), `state` (HIGH or LOW), `timeout` (Optional Integer in microseconds, defaults to 1,000,000)
- **Returns:** `Integer` (Length of the pulse in microseconds, or 0 if timeout occurred)
- **Description:** **Native Hardware Pulse Measurement.** Suspends the JS engine and delegates to the C++ Kernel to accurately measure the duration of an incoming hardware pulse. This bypasses the JavaScript execution overhead entirely, giving you absolute microsecond precision (crucial for reading HC-SR04 ultrasonic sensors).

---

## 5. Unified File System (FS)

The `FS` global object controls the C++ virtual file system layer. It dynamically routes operations to the physical SD Card (prefixed with `/sd/`) or the high-speed Internal Flash (prefixed with `/local/`).

#### `FS.exists(path)`
- **Parameters:** `path` (String)
- **Returns:** `Boolean`
- **Description:** Validates if a file or folder physically exists.

#### `FS.readTextFile(path)`
- **Parameters:** `path` (String)
- **Returns:** `String` (or `null` if the file doesn't exist)
- **Description:** High-speed RAM loader. Reads the entire file into a contiguous String block in RAM. Do not use on files larger than ~20KB!

#### `FS.writeTextFile(path, content)`
- **Parameters:** `path` (String), `content` (String)
- **Returns:** `Boolean`
- **Description:** Erases any existing file and writes the entirety of `content` to disk.

#### `FS.appendTextFile(path, content)`
- **Parameters:** `path` (String), `content` (String)
- **Returns:** `Boolean`
- **Description:** Appends the given string to the end of an existing file.

#### `FS.deleteFile(path)`
- **Parameters:** `path` (String)
- **Returns:** `Boolean`
- **Description:** Permanently deletes a file from the disk partition.

#### `FS.renameFile(pathFrom, pathTo)`
- **Parameters:** `pathFrom` (String), `pathTo` (String)
- **Returns:** `Boolean`
- **Description:** Renames a file or moves it between directories on the same partition.

#### `FS.listDir(path)`
- **Parameters:** `path` (String)
- **Returns:** `Array[String]`
- **Description:** Iterates through a directory and returns an array of absolute file paths (e.g. `["/local/app.js"]`).

#### `FS.mkdir(path)`
- **Parameters:** `path` (String)
- **Returns:** `Boolean`
- **Description:** Creates a new directory.

#### `FS.rmdir(path)`
- **Parameters:** `path` (String)
- **Returns:** `Boolean`
- **Description:** Removes an empty directory.

#### `FS.isDirectory(path)` / `FS.isFile(path)`
- **Parameters:** `path` (String)
- **Returns:** `Boolean`
- **Description:** Evaluates if the target path is a directory or a file.

#### `FS.getFileSize(path)`
- **Parameters:** `path` (String)
- **Returns:** `Integer` (bytes)
- **Description:** Returns the total physical size of a file in bytes.

#### `FS.getTotalSpace(drive)` / `FS.getUsedSpace(drive)` / `FS.getFreeSpace(drive)`
- **Parameters:** `drive` (String - either `"/local"` or `"/sd"`)
- **Returns:** `Integer` (bytes)
- **Description:** Returns exact storage metrics for the specified partition.

#### `FS.getFileMD5(path)`
- **Parameters:** `path` (String)
- **Returns:** `String` (Hex representation of MD5 hash)
- **Description:** Leverages hardware-accelerated `mbedtls` cryptographic engine to stream the file and return its precise MD5 hash.

#### `FS.mountSD()` / `FS.unmountSD()`
- **Parameters:** None
- **Returns:** `Boolean` (mount returns success status)
- **Description:** Triggers an SPI remount/unmount of the physical SD card.

---
**Take Apps and Games from the CelerOS Hub As Example: https://os.celer.tec.br/store (catalog) — see also `data/apps/` in this repository**
---
*Document Version: 1.1 (Built for CelerOS JavaScript Environment)*

## 6. Networking: `Net` (API Level 2)

HTTP client for JS apps. Calls are **blocking** (the script waits for the
response, timeout 10s). HTTP and HTTPS are both supported; responses larger
than 32 KB are truncated.

> Memory note: HTTPS (TLS) takes ~45 KB of heap during the call and shares
> memory with the JS runtime (~90 KB) — keep payloads small, especially on
> boards without PSRAM.

#### `Net.isConnected()`
- **Returns:** Boolean
- **Description:** Returns `true` if WiFi is currently connected.

#### `Net.get(url)`
- **Parameters:** `url` (String, `http://` or `https://`)
- **Returns:** String (response body) or `null` on failure (DNS, timeout, HTTP status outside 2xx).
- **Description:** Performs an HTTP GET. Follows redirects. Throws an error if WiFi is not connected.

#### `Net.getJSON(url)`
- **Parameters:** `url` (String)
- **Returns:** Parsed JS object/array, or `null` on request failure.
- **Description:** Like `Net.get()`, but parses the body as JSON. A malformed JSON body throws a visible script error.

#### `Net.post(url, body, contentType)`
- **Parameters:**
  - `url` (String)
  - `body` (String) — request body
  - `contentType` (String, optional — defaults to `"text/plain"`, e.g. `"application/json"`)
- **Returns:** String (response body) or `null` on failure.
- **Description:** Performs an HTTP POST. Throws an error if WiFi is not connected.

#### `Net.download(url, filePath, onProgress)` (API 6)
- **Parameters:**
  - `url` (String, `http://` or `https://`)
  - `filePath` (String) — destination VFS path (e.g. `"/local/apps/<pkg>/main.js.new"`)
  - `onProgress` (Function, optional) — called per chunk with `(bytesSoFar, totalBytes)`; `totalBytes` is `-1` when the server sends no `Content-Length`
- **Returns:** Boolean — `true` on success; on failure the partial file is removed
- **Description:** Downloads straight to a file in **streaming** mode — the body never goes through the JS heap, so there is **no 32 KB cap** (this is how the App Store updates apps; the hub enforces the size limits). Errors inside `onProgress` don't abort the download. This is what the App Store uses to install/updates apps: it writes to a `*.new` staging file, validates `FS.getFileMD5()` against the catalog checksum, then `FS.renameFile()`s it over the old code (atomic within the same filesystem — never rename across `/local` ↔ `/sd`).

### Example

```javascript
var quote = Net.getJSON("http://economia.awesomeapi.com.br/json/last/USD-BRL");
if (quote === null) {
    System.print("request failed");
} else {
    System.print("USD/BRL: " + quote.USDBRL.bid);
}
```

## 12. API Level 3 — System Apps (W8)

Introduced with the W8 rework: the system screens (Settings, App Store,
Installer, Help, Web Server) are now JS apps living in LittleFS. Level 3
adds the bindings they need — theme colors, file copy, OTA control and the
app package format.

### 12.1 Theme & Icons

#### `System.theme()`
- **Returns:** Object `{bg, card, raised, stroke, accent, accentD, onAccent, text, textDim, ok, warn, err}` — the OS theme palette as RGB565 values, ready to pass to any drawing call. System apps use it to inherit the CelerOS look on every board.

#### `System.textWidth(str, font)`
- **Parameters:** `str` (String), `font` (Number, default 2)
- **Returns:** Number — string width in pixels in the virtual 240x320 space. `System.drawString` uses top-left datum; center manually: `x = 120 - (System.textWidth(s, 2) >> 1)`.

#### `System.drawIcon(name, x, y)`
- **Parameters:** `name` (String: `appstore`, `installer`, `settings`, `help`, `web`, `time`, `about`, `update`, `app`, `wifi_on`, `wifi_off`, `terminal`, `calculator`, `snake` — or an absolute `/local`/`/sd` path), position in virtual space
- **Description:** Draws a 64x64 icon from `/local/icons/<name>.png` with alpha blending (legacy `.bin` accepted).

### 12.2 App Lifecycle

#### `System.exitApp()`
Closes the app and returns to the launcher (same as touching the top-right corner).

#### `System.rescanApps()`
Asks the launcher to rescan `/local/apps` and `/sd/apps`. Call after installing/removing apps.

#### `System.openWifiSetup()`
Pushes the native WiFi setup screen. Since JS apps run synchronously, call `System.exitApp()` right after — the setup screen takes over when the script yields.

#### `System.present()`
Shows the automatic frame buffer on the glass now (no-op without it). Only needed in loops that never call `delay()`/`getTouch()`.

#### `System.fontHeight(font)`
Height in pixels (virtual canvas) of a numeric font (1/2/4) as rendered on this board — use `y - (System.fontHeight(f) >> 1)` to center text vertically.

#### `System.setClip(x, y, w, h)` / `System.clearClip()`
Restricts drawing to a rectangle of the virtual canvas (anything outside is discarded) — e.g. a scrolling list whose partial rows must not paint over the header. `clearClip()` restores the full screen. The clip is reset when an app starts.

#### `System.isBuffered()`
Returns `true` when the board has the automatic frame buffer (PSRAM boards).

### 12.3 Hardware & System

#### `System.setBrightness(level)` / `System.getBrightness()` / `System.backlightSupported()`
Backlight control (5–100). `setBrightness` persists to `/local/brightness.txt`. On boards without PWM backlight `backlightSupported()` returns `false` and the setters are no-ops.

#### `System.wifiStatus()`
- **Returns:** `{connected, ip, webServer, savedNetworks}` (Booleans/String).

#### `System.webActive()` / `System.webSetActive(bool)`
Web server (file manager + web upload) state and toggle — live, no reboot.

#### `System.md5(str)`
- **Returns:** lowercase hex MD5 of the string (same format as `FS.getFileMD5`). Kept for legacy data only — **do not use for passwords** (see `setPin` below).

#### `System.setPin(pin)` / `System.verifyPin(pin)` / `System.pinClear()` / `System.pinState()`
Settings PIN, handled natively since 1.3: salted SHA-256 (`settings_pin2.bin`), no hash exposed to JS. `setPin` accepts 4–6 digits; `verifyPin` transparently upgrades a legacy MD5 PIN on first success. `pinState()` returns `0` (no PIN), `1` (active) or `2` (corrupted — flag set but file missing; the UI should ask for a redefinition).

#### `System.webAuthInfo()` / `System.webAuthSetPass(pass)`
Web server credentials (Basic Auth since 1.3 — every route requires the password). `webAuthInfo()` → `{user, pass}` for display to the device owner; `webAuthSetPass` accepts 6–31 characters.

#### `System.otaCheck()`
- **Returns:** `{fetchFailed, available, hasFirmware, version, url, changelog, guide, type}` — result of the device's update channel manifest.

#### `System.otaStart(url, progressCallback)`
- **Parameters:** `url` from `otaCheck()`, callback receiving `percent` (0–100) during the flash
- **Returns:** `{ok, error?}` — flashes the inactive OTA slot; on success the app should offer `System.restart()`.

#### `System.setTimezone(tz)` / `System.setManualTime(year, month, day, hour, minute)` / `System.set24hFormat(bool)` / `System.get24hFormat()` / `System.setNtpEnabled(bool)` / `System.getNtpEnabled()`
Time configuration (persisted by TimeManager).

#### `System.factoryReset(mode)`
- `"configs"` — clears configuration files in `/local` and saved WiFi networks, **keeps** apps and icons.
- `"total"` — formats the whole LittleFS partition (**apps are erased**; recovery requires `tools/flash_data.sh` or `celerctl apps install`). Always confirm twice in the UI.

### 12.4 WiFi (Net)

- `Net.wifiScan()` → array `[{ssid, rssi, secure}]` (blocking, ~2s).
- `Net.wifiConnect(ssid, password)` → Boolean (blocking, up to 15s; saves credentials).
- `Net.wifiDisconnect()` — disconnects STA, keeps saved networks.

### 12.5 File copy (FS)

- `FS.copyFile(src, dst)` → Boolean — binary safe.
- `FS.copyDirectory(srcDir, dstDir)` → Boolean — recursive copy.
- `FS.removeDirectory(path)` → Boolean — **recursive** delete (unlike `FS.rmdir`, which requires an empty dir).

### 12.6 App package (app.json)

```json
{
  "name": "My App",
  "packageName": "celeros.myapp",
  "version": "1.0.0",
  "author": "you",
  "description": "...",
  "type": "Utility",
  "category": "Utility",
  "api": 3,
  "topbar": true,
  "system": true,
  "order": 30,
  "icon": "settings"
}
```

- `system: true` — system app: sorted first in the launcher grid (native system screens are apps like this now).
- `order` — position among system apps.
- `icon` — icon name in `/local/icons`. **Preferred:** ship `icon.png` (64x64 with alpha; decoded on load — CelerOS 1.2+) inside the app folder — it overrides the name and travels with the package when installed via SD/celerctl. Legacy `icon.bin` (v2 RGB565+A4) is still accepted.
- `packageName` — identity used by the launcher dedup, installer and App Store.
- `topbar: true` — fixed system topbar (see section 14). Without the field the app runs full screen with the retractable topbar.
- Install paths: `/local/apps/<Name>/` (LittleFS) or `/sd/apps/<Name>/` (SD card). Reinstall/update with `celerctl apps install <folder> [--sd]`.

---

## 13. API Level 5 — Docked Keyboard (W9)

Introduced with the W9 apps (Terminal, Calculator, Snake): a **non-blocking
keyboard session** the app controls from its own loop, so text input can live
side by side with the app's UI (a shell input line, chat fields, forms…).
`System.prompt()` (section 2) stays the right choice for simple one-shot
dialogs.

The keyboard renders on the **same target as the app** (app sprite > PSRAM
frame > display), so it survives `present()` and composes with the automatic
frame buffer on every board. With `field: false` the keyboard is drawn as a
compact block anchored to the bottom of the screen; everything above
`System.keypadRect().y` belongs to the app.

### 13.1 Session API

#### `System.keypadOpen(options)` → Boolean
- **Parameters:** `options` (Object, optional): `{title, initial, maxLen, field}`.
  - `title` (String) — header label (only shown with a field).
  - `initial` (String) — pre-filled text.
  - `maxLen` (Number, default 64, max 256) — buffer limit.
  - `field` (Boolean, default true) — draw the native input field + X button on top. `field: false` draws the bare keyboard docked at the bottom (the app echoes the line itself).
- **Returns:** `false` if a session is already open (one at a time) or there is no display.
- **Description:** Opens the keyboard session and draws it immediately. Enter (OK) does **not** close the session: it clears the buffer and keeps the keyboard open — ideal for line-at-a-time UIs. The session ends on `X` (only with `field: true`, reported as a `cancel` event) or `System.keypadClose()`.

#### `System.keypadPoll()` → Object|null
- **Returns:** `null` when nothing happened, or one event object per call:
  - `{type: "change"}` — the buffer changed (key/backspace/space); read it with `System.keypadText()`.
  - `{type: "enter", text: "..."}` — OK pressed; `text` is the line, the buffer is cleared afterwards.
  - `{type: "cancel"}` — X pressed; the session **closed itself** (redraw your screen, the keyboard is gone).
- **Description:** Pumps the touch for the keyboard, redraws its keys when needed (key press feedback, layout pages, cursor blink) and reports at most one event per call. The top-right OS exit corner keeps working while the keyboard is open. Call this every loop iteration while the session is open.

#### `System.keypadText()` → String
Current input buffer (same text the native field would show).

#### `System.keypadRect()` → `{x, y, w, h}`
Area occupied by the keyboard in the 240x320 virtual canvas (`h` is 0 when the session is closed). The app must not draw inside it.

#### `System.keypadDraw()`
Re-blits the keyboard after the app repaints a region that overlaps it (not needed if the app only draws above `keypadRect().y`).

#### `System.keypadClose()`
Closes the session and frees the keyboard. The next `present()` restores the app's frame. Sessions are also closed automatically when an app exits.

### 13.2 Example — input line above a docked keyboard

```javascript
var T = System.theme();
System.keypadOpen({ field: false, maxLen: 96 });
var kbTop = System.keypadRect().y;   // app owns 0..kbTop

while (true) {
    var ev = System.keypadPoll();
    if (ev && ev.type === "enter") {
        handleLine(ev.text);          // ex.: comando, mensagem, busca...
        redrawScreen();
    } else if (ev && ev.type === "change") {
        redrawInputLine(System.keypadText());
    }
    System.delay(20);                 // GC + present (obrigatorio)
}
```

Reference implementation: `data/apps/Terminal/main.js` (preinstalled W9 app).

### 13.3 Global color constants (API 5 fix)

`BLACK, WHITE, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, ORANGE, DARKGREY`
are now registered as global RGB565 constants (before W9 the registration
was a no-op — apps had to use `System.color()`). `System.color()` and
`System.theme()` remain the recommended way to get colors.

---

## 14. System Topbar

Every JS app runs under the **system topbar** — a strip drawn by the core with
the app's name and the **X** exit button on the right. The mode comes from the
package, not from code:

- **Fixed** — `"topbar": true` in `app.json`: the strip is always visible and
  the 240x320 virtual canvas maps to the area below it (`getTouch` coordinates
  already account for the strip). Use it for system-style apps with
  menus/lists/forms — Settings, App Store, Terminal.
- **Retractable (default)** — no `topbar` field: the app runs **full screen**
  (the canvas maps 1:1 over the whole panel, exactly like pre-topbar apps).
  Swiping down from the very top edge reveals the strip for **3 seconds**; the
  whole reveal gesture is consumed by the system — the app never sees its
  press/release. Ideal for games (Snake, 2048, Breakout).

Notes:

- In fixed mode touches in the strip never reach the app; in retractable mode
  only while the strip is visible (X included — it fires on release, debounced).
- On PSRAM boards the strip is composed into the frame (no flicker, and hiding
  it restores the area atomically). On the CYD the strip is stamped on the
  glass at each yield; when it hides, it lingers until the app repaints that
  region (games cover it on the next frame).
- Nothing to code in either mode: the same main.js renders correctly on both —
  the field in `app.json` is the whole contract.

### 14.1 Custom content (API 6)

Apps can put their own text and touchable **chips** in the strip:

#### `System.topbarText(text)`
Replaces the app name shown in the strip (`""` restores the name). Redraws automatically.

#### `System.topbarButtons(labels)` → Number
Replaces the strip content with up to 3 touchable chips, laid out right-to-left before the X (`labels` = array of short strings, e.g. `["+", "Limpar"]`). Returns how many fit. `[]` clears them.

#### `System.topbarPop()` → String|null
Pops the oldest chip tap since the last call (FIFO). The chip highlights while pressed and fires on release (same debounced contract as the X).

```javascript
System.topbarText("Toques: 0");
System.topbarButtons(["+", "Limpar"]);
while (true) {
    var id = System.topbarPop();
    if (id === "+") { n += 10; System.topbarText("Toques: " + n); }
    else if (id === "Limpar") { n = 0; System.topbarText("Toques: 0"); }
    System.delay(20);
}
```

In retractable mode chips are tappable only while the strip is visible. Apps
should feature-detect (`typeof System.topbarText === "function"`) to stay
installable on older firmware.

Reference implementation: `data/apps/Touch Test/main.js`.
