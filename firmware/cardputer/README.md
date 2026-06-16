# M5Stack Cardputer firmware

This target connects the M5Stack Cardputer to Cola over local Wi-Fi with keyboard text input and replies on the built-in screen.

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

The firmware includes a default `config.h` with an empty Cola host. Edit `include/config.h`:

```cpp
#define COLA_HOST "192.168.1.23"
#define COLA_PORT 8787
#define DEVICE_ID "m5-cardputer-01"
```

- `COLA_HOST` should be the LAN IP of the computer running Cola. The computer and Cardputer must be on the same Wi-Fi.
- The Wi-Fi ID and password are entered on the Cardputer during boot and saved on the device.
- `config.h` is committed with safe defaults.

Connect the Cardputer over USB-C. If upload fails, put it into download mode:

1. Set the top power switch to `OFF`.
2. Hold `G0`.
3. Plug in USB-C.
4. Release `G0`.

Upload from the repository root:

```bash
cd firmware/cardputer
pio run -t upload
```

Open the serial monitor:

```bash
pio device monitor
```

## Usage

After the firmware boots:

1. Confirm the Wi-Fi ID. If a saved ID exists, it is already filled in; press `Enter` to use it or `Del` to clear it.
2. Confirm the Wi-Fi password. If a saved password exists, it is already filled in and shown as `*`; press `Enter` to use it or `Del` to clear it.
3. Wait until the screen shows that Wi-Fi and Cola are connected.
4. Type a message on the Cardputer keyboard.
5. Press `Enter` to send it to Cola.

Cola replies are rendered on the Cardputer screen.
