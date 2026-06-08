# cola-m5

Connect M5Stack devices to Cola.

This repository has two parts:

- `plugin/`: the Cola plugin that runs on the computer with Cola.
- `firmware/cardputer/`: the first M5Stack firmware target, for Cardputer.

The current transport is local Wi-Fi with WebSocket:

```text
M5Stack device <-> cola-m5 plugin <-> Cola
```

The protocol is intentionally model-agnostic so more M5Stack devices can be added later.

## Layout

```text
cola-m5/
  plugin/
    src/index.ts
  firmware/
    cardputer/
      platformio.ini
      src/main.cpp
      include/config.example.h
  docs/
    protocol.md
```

## Install the Cola plugin

Build the plugin:

```bash
npm install
npm run build
```

Then install the local `plugin/` directory in Cola's plugin settings.

The plugin exposes a channel named `M5Stack` and listens on port `8787` by default. If your local network needs a different port, change it in the channel config.

To find the LAN IP for the Cardputer firmware, run one of these on the computer running Cola:

```bash
ipconfig getifaddr en0
ipconfig getifaddr en1
```

## Flash the Cardputer firmware

Install PlatformIO first:

```bash
python3 -m pip install platformio
```

Then create a local firmware config:

```bash
cp firmware/cardputer/include/config.example.h firmware/cardputer/include/config.h
```

Edit `firmware/cardputer/include/config.h`:

```cpp
#define WIFI_SSID "Your Wi-Fi"
#define WIFI_PASSWORD "Your Wi-Fi password"
#define COLA_HOST "192.168.1.23"
#define COLA_PORT 8787
#define DEVICE_ID "m5-cardputer-01"
```

`COLA_HOST` should be the LAN IP of the computer running Cola. The computer and Cardputer must be on the same Wi-Fi.
`config.h` is ignored by git because it contains local Wi-Fi settings.

Connect the Cardputer over USB-C. If upload fails, put it into download mode:

1. Set the top power switch to `OFF`.
2. Hold `G0`.
3. Plug in USB-C.
4. Release `G0`.

Upload:

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

1. Wait until the screen shows that Wi-Fi and Cola are connected.
2. Type a message on the Cardputer keyboard.
3. Press `Enter` to send it to Cola.

Cola replies are rendered on the Cardputer screen.

## Notes

The first version auto-binds device identities in the Cola plugin to keep hardware development fast. Before using this on an untrusted network, add a pairing token or another auth flow.

## License

MIT
