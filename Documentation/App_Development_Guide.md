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
  "api": 5,
  "changelog": "Initial release."
}
```

### Field Details:
- **`name`**: The display name of your app. This is what the user sees in the Home.
- **`packageName`**: A globally unique identifier for your app. **Rules: lowercase, dot-separated style, no spaces** (e.g., `com.yourname.appname`). The OS uses this to detect if your app is already installed.
- **`version`**: Semantic versioning (e.g. `1.0.0`, `1.2.1`). If a user installs an app with the same `packageName` but a higher version number, the OS will smartly prompt them to "Update" rather than "Install".
- **`metaUrl`** *(optional)*: The raw URL to the `app.json` on the internet (e.g. your GitHub repository). The App Store uses this URL to automatically check for new versions of your app.
- **`author`**: Your name or studio. If someone else tries to install an app with your `packageName` but a different `author` name, the OS will throw a conflict warning to protect your app from being overwritten by malicious developers.
- **`description`**: A short summary of your app, displayed to the user when they install your app for the first time. Note: system apps avoid accented characters — stick to plain ASCII for maximum compatibility with the built-in font.
- **`type`**: The broad classification (e.g., `App` or `Game`). You can type any value here without restriction.
- **`category`**: The specific category (e.g., `Utilities`, `Games`, `Tools`). You can type any value here without restriction.
- **`api`**: The CelerOS API level your app targets (see the [JS API Guide](JS_API_Guide.md) — currently `5`). This is verified by the system at install time.
- **`changelog`**: A brief string detailing what changed. When a user updates your app, this replaces the description and shows up under a "What's New" header!

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
