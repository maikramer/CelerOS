# CelerOS App Development Guide

**English** | [Português (BR)](App_Development_Guide.pt-BR.md)

Welcome to the CelerOS App Development Guide! Developing apps for CelerOS is simple. Apps are written in JavaScript and use a standard folder structure containing metadata and code.

## 1. App Folder Structure

In CelerOS, an app is not just a single `.js` file. Instead, every app is a **folder** containing all its necessary files. When you install your app with `celerctl apps install <folder>`, from the SD card (Installer) or through the App Store, the whole folder is what gets deployed.

A standard app folder looks like this:
```text
MyAwesomeApp/
├── app.json
├── icon.png
└── main.js
```

## 2. The `app.json` File (App Metadata)

The `app.json` file is the heart of your app's identity. The CelerOS Installer reads this file to securely install, update, and categorize your application.

### Example Format:
```json
{
  "name": "My App",
  "packageName": "com.developer.myapp",
  "version": "1.0.0",
  "metaUrl": "https://raw.githubusercontent.com/.../myapp/app.json",
  "author": "John Doe",
  "description": "A cool app that does awesome things.",
  "type": "App",
  "category": "Utility",
  "api": 6,
  "changelog": "Initial release."
}
```

### Field Details:
- **`name`**: The display name of your app. This is what the user sees in the Home.
- **`packageName`**: A globally unique identifier for your app. **Rules: lowercase, dot-separated style, no spaces** (e.g., `com.yourname.appname`). The OS uses this to detect if your app is already installed.
- **`version`**: Semantic versioning (e.g. `1.0.0`, `1.2.1`). If a user installs an app with the same `packageName` but a higher version number, the OS will smartly prompt them to "Update" rather than "Install".
- **`metaUrl`** *(optional)*: The raw URL to the `app.json` on the internet (e.g. your GitHub repository). Apps published on the **CelerOS Hub don't need it** — the store catalog itself carries name, version, changelog, size and the MD5 checksum of your `main.js`; `metaUrl` is only for self-hosted update checks outside the hub.
- **`author`**: Your name or studio. If someone else tries to install an app with your `packageName` but a different `author` name, the OS will throw a conflict warning to protect your app from being overwritten by malicious developers.
- **`description`**: A short summary of your app, displayed to the user when they install your app for the first time. Note: system apps avoid accented characters — stick to plain ASCII for maximum compatibility with the built-in font.
- **`type`**: The broad classification (e.g., `App` or `Game`). You can type any value here without restriction.
- **`category`**: The specific category (e.g., `Utilities`, `Games`, `Tools`). You can type any value here without restriction.
- **Boards without PSRAM** (e.g. the classic CYD): apps run in internal RAM with WiFi on, network apps included. The practical ceiling is a `main.js` of ~60KB (comments and indentation are stripped before compiling, so they cost nothing); the store computes the limit from `System.getInfo().appRAM` and marks bigger apps "Requer PSRAM".
- **`permissions`** (optional, F4): array of capabilities — `"fs"`, `"net"`, `"gpio"`, `"system"`. Without the field the app keeps everything (compat with the existing store); with it, only what is declared is registered (`FS` / `Net` / `System.gpio` and the device-affecting calls such as `restart`/`otaStart` are filtered at runtime). System apps (`"system": true`) always get everything.
- **`api`**: The CelerOS API level your app targets (see the [JS API Guide](JS_API_Guide.md) — currently `6`). This is verified by the system at install time.
- **`changelog`**: A brief string detailing what changed (one line per version works well, e.g. `"1.1.0 - fixed crash\n1.0.0 - first release"`). The hub publishes it with the catalog and the device store shows it under a **"Novidades" / What's New** header on the update screen.

### Fields managed by the CelerOS Hub

When you publish through the hub (`tools/celerhub.py` / `publish_app.py`), four
fields of `app.json` are computed and written by the server — **don't set them
by hand** (a manual publish overwrites them again):

- **`size`** — byte size of `main.js` (the store checks free disk space before downloading).
- **`md5`** — checksum of `main.js`; the device validates it after downloading and aborts the update on mismatch, leaving the installed version untouched.
- **`published_at`** — UTC timestamp of the publish.
- **`publisher`** — token name that published the package.

### App update rules (CelerOS Hub)

- **The catalog is the update channel.** The device store compares each catalog version against the installed one; newer versions show an **"Atualizar"** badge in the list, the **Atualizações** tab and the **"Atualizar tudo"** button.
- **Bump `version` on every publish.** The hub rejects versions ≤ the published one (`--force` to override, e.g. to republish a fixed package).
- **You own your `packageName`.** The first publisher becomes the owner; only the same token (or the hub root) can update or remove the package afterwards.
- **Size limits:** `main.js` ≤ **48 KB** — apps above **30 KB** must declare `api: 6` (old firmware downloaded via `Net.get`, which truncates at 32 KB; the API 6 streaming downloader has no cap). `icon.png` ≤ 16 KB.
- **The icon ships with the update:** keep an `icon.png` (64×64 PNG, ≤ 16 KB) in the package — the store downloads it on install/update and the launcher refreshes its icon cache automatically.
- **Updates land in the folder the launcher runs** (resolved by `packageName`): updating a preinstalled app updates it in place instead of creating a shadow copy.
- **The App Store updates itself:** the store is a regular hub package (`celeros.appstore`). When the catalog has a newer store, it shows up like any update — after installing, the store asks to exit and reopen (the new `main.js` is read from disk on the next launch).

## 3. The `icon.png` File (App Icon)

A 64x64 (or larger, square) PNG shown in the Launcher. On install it is
decoded into the system icon cache (RGB565 + 4-bit alpha), so transparency
and rounded corners work. Apps without an `icon.png` get a generic icon.

## 4. The `main.js` File (App Logic)

The `main.js` file is the entry point of your application. When a user taps your app in the Launcher, the OS loads and executes this JavaScript file.

Because CelerOS handles the underlying C++ translation, you can write simple, high-level JavaScript to draw graphics, read files, and trigger UI components.

### Your First App (`main.js`)
Here is a simple example that turns the screen blue, prints "Hello CelerOS!", waits 3 seconds, and then gracefully exits back to the Launcher:

```javascript
// Clear the screen
Graphics.fillScreen(Graphics.COLOR_BLUE);

// Draw some text in the center
Graphics.setTextColor(Graphics.COLOR_WHITE);
Graphics.drawString("Hello CelerOS!", 120, 160, 2);

// Wait for 3 seconds
System.delay(3000);

// Close the app and return to the OS Launcher
System.exit();
```

> [!IMPORTANT]
> To see everything you can do in `main.js`, please check out the full **[JS API Guide](JS_API_Guide.md)**! It contains all the documentation you need for Graphics, GPIO pins, File Systems, UI Components, and more.
