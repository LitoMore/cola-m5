# M5Stack Xiaozhi Card Kit firmware

This target connects the M5Stack Xiaozhi Card Kit to Cola over local Wi-Fi with an e-paper setup flow and simple shortcut input.

This target is experimental. It treats the M5Stack Xiaozhi Card Kit as an ESP32-S3 e-paper device and uses M5Unified board detection with a PaperS3 fallback. It does not assume a Cardputer keyboard.

## Flash

Install PlatformIO first:

```bash
python3 -m pip install platformio
```

Install and run the Cola plugin from the [plugin README](../../plugin/README.md) before using the firmware. The plugin exposes a channel named `M5Stack` and listens on port `8787` by default.

To find the LAN IP for the computer running Cola, run one of these on that computer:

```bash
ipconfig getifaddr en0
ipconfig getifaddr en1
```

Edit `include/config.h`:

```cpp
#define COLA_HOST "192.168.1.23"
#define COLA_PORT 8787
#define DEVICE_ID "m5-xiaozhi-card-01"
```

Connect the device over USB-C, then upload from the repository root:

```bash
cd firmware/xiaozhi-card-kit
pio run -t upload
```

Open the serial monitor:

```bash
pio device monitor
```

## Usage

The Xiaozhi Card Kit target uses the same setup portal style as StickS3. On first boot, or when saved setup is missing, connect a phone or computer to the AP shown on the e-paper screen and open `http://192.168.4.1`.

After setup, tap the left half of the screen, or press `A` if available, to cycle the selected shortcut or advance a reply page. Tap the right half of the screen, or press `B` if available, to send the selected shortcut. Hold touch, or press `C` if available, to reopen setup mode.

Double-press the e-paper back key to power off. Press the back key again to wake the device.
