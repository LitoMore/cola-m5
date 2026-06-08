import {
  defineChannel,
  type DeliverFn,
  type PluginLogger,
  type OutboundContext
} from '@marswave/cola-plugin-sdk'
import { WebSocket, WebSocketServer, type RawData } from 'ws'

type M5PluginState = {
  server?: WebSocketServer
  clients: Map<string, M5Client>
}

type M5Client = {
  socket: WebSocket
  deviceId: string
  deviceModel: string
  connectedAt: number
}

type DeviceMessage =
  | {
      type: 'hello'
      protocolVersion?: number
      deviceId?: string
      deviceModel?: string
      firmware?: string
    }
  | {
      type: 'message'
      protocolVersion?: number
      deviceId?: string
      deviceModel?: string
      text?: string
    }

const DEFAULT_PORT = 8787
const PROTOCOL_VERSION = 1
const pluginState: M5PluginState = {
  clients: new Map()
}

function getPort(config: Readonly<Record<string, unknown>>): number {
  const value = Number(config.port ?? DEFAULT_PORT)

  if (!Number.isInteger(value) || value <= 0 || value > 65_535) {
    return DEFAULT_PORT
  }

  return value
}

function parseDeviceMessage(raw: RawData): DeviceMessage | null {
  try {
    const parsed = JSON.parse(rawDataToString(raw)) as unknown

    if (!parsed || typeof parsed !== 'object') {
      return null
    }

    const message = parsed as Partial<DeviceMessage>

    if (message.type !== 'hello' && message.type !== 'message') {
      return null
    }

    return message as DeviceMessage
  } catch {
    return null
  }
}

function rawDataToString(raw: RawData): string {
  if (Array.isArray(raw)) {
    return Buffer.concat(raw).toString('utf8')
  }

  if (raw instanceof ArrayBuffer) {
    return Buffer.from(raw).toString('utf8')
  }

  return raw.toString('utf8')
}

function normalizeDeviceId(deviceId: string | undefined): string | null {
  const normalized = deviceId?.trim()

  if (!normalized) {
    return null
  }

  return normalized.slice(0, 128)
}

function normalizeDeviceModel(deviceModel: string | undefined): string {
  const normalized = deviceModel?.trim()

  if (!normalized) {
    return 'unknown'
  }

  return normalized.slice(0, 64)
}

function sendJson(socket: WebSocket, payload: Record<string, unknown>): void {
  if (socket.readyState !== WebSocket.OPEN) {
    return
  }

  socket.send(JSON.stringify({ protocolVersion: PROTOCOL_VERSION, ...payload }))
}

function closeGateway(): void {
  for (const client of pluginState.clients.values()) {
    client.socket.close()
  }

  pluginState.clients.clear()
  pluginState.server?.close()
  pluginState.server = undefined
}

function rememberClient(
  state: M5PluginState,
  socket: WebSocket,
  deviceId: string,
  deviceModel: string
): M5Client {
  const existing = state.clients.get(deviceId)

  if (existing && existing.socket !== socket) {
    existing.socket.close()
  }

  const client: M5Client = {
    socket,
    deviceId,
    deviceModel,
    connectedAt: Date.now()
  }

  state.clients.set(deviceId, client)

  socket.once('close', () => {
    const current = state.clients.get(deviceId)

    if (current?.socket === socket) {
      state.clients.delete(deviceId)
    }
  })

  return client
}

async function handleDeviceMessage(
  ctx: {
    deliver: DeliverFn
    logger: PluginLogger
    runtime: {
      identity: {
        bind(senderId: string): Promise<void>
      }
    }
  },
  socket: WebSocket,
  raw: RawData
): Promise<void> {
  const message = parseDeviceMessage(raw)

  if (!message) {
    sendJson(socket, { type: 'error', message: 'Invalid JSON message' })
    return
  }

  const deviceId = normalizeDeviceId(message.deviceId)

  if (!deviceId) {
    sendJson(socket, { type: 'error', message: 'Missing deviceId' })
    return
  }

  const deviceModel = normalizeDeviceModel(message.deviceModel)
  rememberClient(pluginState, socket, deviceId, deviceModel)

  if (message.type === 'hello') {
    await ctx.runtime.identity.bind(deviceId)
    sendJson(socket, {
      type: 'status',
      connected: true,
      message: 'Connected to Cola'
    })
    ctx.logger.info(`M5 device connected: ${deviceId} (${deviceModel})`)
    return
  }

  const text = message.text?.trim()

  if (!text) {
    sendJson(socket, { type: 'error', message: 'Message text is empty' })
    return
  }

  await ctx.runtime.identity.bind(deviceId)
  await ctx.deliver({
    sessionId: ['device', deviceId],
    sender: {
      id: deviceId,
      name: `M5 ${deviceModel}`
    },
    deliveryContext: {
      to: deviceId,
      accountId: deviceModel
    },
    conversation: {
      kind: 'direct',
      id: deviceId,
      name: `M5 ${deviceModel}`
    },
    message: text
  })
}

export default defineChannel({
  id: 'm5',
  meta: {
    label: 'M5Stack',
    description: 'Connect M5Stack devices to Cola.',
    markdownCapable: false
  },
  capabilities: {
    receive: {
      text: true
    },
    send: {
      text: true
    }
  },
  config: {
    schema: {
      fields: [
        {
          key: 'port',
          path: ['port'],
          label: 'WebSocket port',
          description: 'Port that M5Stack devices connect to on this computer.',
          type: 'number',
          required: true,
          defaultValue: DEFAULT_PORT
        }
      ]
    }
  },
  gateway: {
    async start(ctx) {
      const port = getPort(ctx.config)

      closeGateway()

      const server = new WebSocketServer({ port })

      pluginState.server = server

      server.on('error', (error) => {
        ctx.logger.error('M5Stack WebSocket gateway error', error)
      })

      server.on('connection', (socket) => {
        socket.on('message', (raw) => {
          void handleDeviceMessage(ctx, socket, raw).catch((error: unknown) => {
            ctx.logger.error('Failed to handle M5 device message', error)
            sendJson(socket, {
              type: 'error',
              message: 'Failed to deliver message'
            })
          })
        })
      })

      ctx.abortSignal.addEventListener(
        'abort',
        () => {
          server.close()
          if (pluginState.server === server) {
            pluginState.server = undefined
          }
        },
        { once: true }
      )

      ctx.logger.info(`M5Stack WebSocket gateway listening on port ${port}`)
    },
    async stop() {
      closeGateway()
    },
    getStatus() {
      return {
        connected: Boolean(pluginState.server),
        configured: true,
        message: `${pluginState.clients.size} device(s) connected`
      }
    }
  },
  outbound: {
    textChunkLimit: 800,
    async sendText(ctx: OutboundContext) {
      const deviceId = ctx.deliveryContext.to
      const client = pluginState.clients.get(deviceId)

      if (!client || client.socket.readyState !== WebSocket.OPEN) {
        ctx.logger.warn(`M5 device is not connected: ${deviceId}`)
        return
      }

      sendJson(client.socket, {
        type: 'reply',
        text: ctx.text
      })
    },
    sanitizeText(text) {
      return text.replace(/\r\n/g, '\n').trim()
    }
  }
})
