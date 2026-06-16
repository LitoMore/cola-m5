# cola-m5 plugin

This is the Cola channel plugin that bridges M5Stack firmware targets to Cola over local Wi-Fi with WebSocket.

The plugin exposes a channel named `M5Stack`. M5Stack devices connect to the computer running Cola on port `8787` by default.

## Build

From the repository root:

```bash
npm install
npm run build
```

## Install

Install the local `plugin/` directory in Cola's plugin settings.

## Settings

The plugin has these channel settings:

- `Enable plugin`: starts the WebSocket gateway when Cola launches.
- `WebSocket port`: the port that M5Stack devices connect to on this computer. The default is `8787`.

If your local network needs a different port, change it in the channel config and use the same port in the firmware setup.

To find the LAN IP for firmware setup, run one of these on the computer running Cola:

```bash
ipconfig getifaddr en0
ipconfig getifaddr en1
```
