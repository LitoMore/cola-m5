# M5Stack StickS3 firmware

This target connects the M5Stack StickS3 to Cola over local Wi-Fi with a setup portal, two-button shortcut input, and replies on the built-in screen.

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

The StickS3 target uses a setup portal because the device does not have a keyboard. You can leave `include/config.h` with safe defaults, or set `COLA_HOST` as a prefilled default for the setup page:

```cpp
#define COLA_HOST "192.168.1.23"
#define COLA_PORT 8787
#define DEVICE_ID "m5-stick-s3-01"
```

Upload from the repository root:

```bash
cd firmware/stick-s3
pio run -t upload
```

Open the serial monitor:

```bash
pio device monitor
```

## Usage

On first boot, or when saved setup is missing, the StickS3 starts a Wi-Fi setup access point:

1. Connect a phone or computer to the AP shown on the StickS3 screen, such as `Cola-StickS3-123ABC`.
2. Open `http://192.168.4.1`.
3. Enter the Wi-Fi ID, Wi-Fi password, Cola host, and Cola port.
4. Save the form and wait for the device to connect.

After setup:

1. Press `A` to cycle the selected shortcut. If a Cola reply has multiple pages, `A` advances the page first.
2. Press `B` to send the selected shortcut to Cola.
3. Hold `A` to go to the previous reply page.
4. Hold `A+B` to reopen setup mode and replace the saved Wi-Fi or Cola host settings.
