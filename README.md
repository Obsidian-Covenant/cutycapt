# CutyCapt

## Modernization Covenant

This is a refactored and modernized edition of [CutyCapt](https://cutycapt.sourceforge.net/), a Qt and WebEngine-based command-line utility that captures a web page's rendered output.

Key changes:

- ✅ Qt5 replaced by Qt6 libraries
- ✅ Deprecated WebKit replaced by WebEngine
- ✅ Replaced qmake by the Qt6-recommended build system CMake
- ✅ Removed deprecated libraries and functions
- ✅ Refactored code to be used with latest CMake version

### Installation

#### Dependencies

Build dependencies:
```
cmake ninja
```

Runtime dependencies:
```
qt6-base qt6-webengine qt6-webchannel qt6-svg
```

#### From source

```bash
git clone https://github.com/Obsidian-Covenant/CutyCapt.git
cd cutycapt
cmake -S . -B build -GNinja
ninja -C build
sudo install -Dm 755 -t /usr/bin/ build/cutycapt
```

### Usage

**CutyCapt** is a command-line utility that loads a web page using Qt WebEngine and captures its rendered output to an image or document file.


#### Basic example

```shell
cutycapt --url=https://example.com --out=example.png
```
This loads the page and captures it once loading completes.


#### Capturing dynamic pages

Many modern pages continue modifying the DOM after the initial load (JavaScript, animations, delayed content). In such cases, capturing immediately after page load may be too early.

You can handle this in two ways.

1. Delay-based capture

   Wait a fixed amount of time after load:
   ```shell
   cutycapt \
     --url=https://example.com \
     --out=delayed.png \
     --delay=1000
   ```
   This waits one second after the page finishes loading before capturing.

2. JavaScript-triggered capture (recommended)

   For precise control, you can inject JavaScript and trigger capture when the page is truly "ready".
   
   Example JavaScript (`inject.js`):
   ```javascript
   document.body.style.background = "pink";
   document.body.insertAdjacentHTML("afterbegin", "<h2>Injected OK</h2>");
   
   setTimeout(() => {
     alert("READY");
   }, 50);
   ```
   
   Capture command:
   ```shell
   cutycapt \
     --url=https://example.com \
     --out=scripted.png \
     --inject-script=inject.js \
     --expect-alert=READY
   ```
   
   How this works
   * The script is injected at DocumentReady.
   * It modifies the DOM (e.g. styles, content).
   * After a short delay, it calls alert("READY").
   * CutyCapt intercepts JavaScript alerts internally.
   * When the alert text matches `--expect-alert`, the capture is triggered.
   
   The small `setTimeout` delay allows the browser to finish layout and painting before the screenshot is taken.
   
   Tip: Instead of a fixed timeout, advanced scripts can wait for specific DOM conditions, network completion, or animation frames before triggering the alert.


#### Headless / server environments

Qt WebEngine requires a display server. On headless systems, run CutyCapt under a virtual X server:
```shell
xvfb-run cutycapt --url=https://example.com --out=example.png
```
