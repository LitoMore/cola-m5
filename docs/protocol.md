# cola-m5 protocol

The transport is WebSocket. Payloads are JSON text frames.

## Device to plugin

### Hello

Sent by the device after opening the socket.

```json
{
  "type": "hello",
  "protocolVersion": 1,
  "deviceId": "m5-cardputer-01",
  "deviceModel": "cardputer",
  "firmware": "0.1.0"
}
```

### Message

Sent when the user submits text on the device.

```json
{
  "type": "message",
  "protocolVersion": 1,
  "deviceId": "m5-cardputer-01",
  "deviceModel": "cardputer",
  "text": "Hello Cola"
}
```

## Plugin to device

### Reply

Sent when Cola has text to deliver back to the device.

```json
{
  "type": "reply",
  "protocolVersion": 1,
  "text": "Hello from Cola"
}
```

### Status

Sent by the plugin when it accepts a device.

```json
{
  "type": "status",
  "protocolVersion": 1,
  "connected": true,
  "message": "Connected to Cola"
}
```

### Error

Sent when the plugin rejects a malformed request.

```json
{
  "type": "error",
  "protocolVersion": 1,
  "message": "Missing deviceId"
}
```

