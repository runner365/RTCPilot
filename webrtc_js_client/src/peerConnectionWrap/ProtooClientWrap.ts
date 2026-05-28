/**
 * ProtooClientWrap: TypeScript wrapper around protoo-client library.
 * Provides type-safe methods for sending requests/notifications and handling incoming messages.
 */

import * as protooClient from 'protoo-client'
import { logger } from '../logger'

export type ProtooRequest = {
  method: string
  data?: any
}

export type ProtooNotification = {
  method: string
  data?: any
}

export type ProtooRequestHandler = (request: ProtooRequest, accept: (data?: any) => void, reject: (errorCode?: number, errorReason?: string) => void) => void

export type ProtooNotificationHandler = (notification: ProtooNotification) => void

export type ProtooClientWrapOptions = {
  onRequest?: ProtooRequestHandler
  onNotification?: ProtooNotificationHandler
  onOpen?: () => void
  onClose?: () => void
  onFailed?: (currentAttempt: number) => void
  onDisconnected?: () => void
}

/** Simple typed event emitter */
class TypedEventTarget<TEvents extends Record<string, any>> {
  private target = new EventTarget()

  addEventListener<K extends keyof TEvents & string>(
    type: K,
    listener: (detail: TEvents[K]) => void,
    options?: boolean | AddEventListenerOptions
  ) {
    const wrapped = (ev: Event) => listener((ev as CustomEvent<TEvents[K]>).detail)
    this.target.addEventListener(type, wrapped as EventListener, options)
    return () => this.target.removeEventListener(type, wrapped as EventListener, options as any)
  }

  dispatchEvent<K extends keyof TEvents & string>(type: K, detail: TEvents[K]) {
    const ev = new CustomEvent(type, { detail })
    this.target.dispatchEvent(ev)
  }
}

type ProtooClientEventMap = {
  open: void
  close: void
  failed: number
  disconnected: void
  request: { request: ProtooRequest; accept: (data?: any) => void; reject: (errorCode?: number, errorReason?: string) => void }
  notification: ProtooNotification
}

export class ProtooClientWrap {
  private peer: any | null = null
  private options: ProtooClientWrapOptions
  private events = new TypedEventTarget<ProtooClientEventMap>()
  private url: string | null = null

  constructor(options: ProtooClientWrapOptions = {}) {
    this.options = options
  }

  /**
   * Subscribe to internal events.
   * Events: 'open', 'close', 'failed', 'disconnected', 'request', 'notification'
   */
  on<K extends keyof ProtooClientEventMap & string>(
    type: K,
    listener: (detail: ProtooClientEventMap[K]) => void
  ) {
    return this.events.addEventListener(type, listener)
  }

  /**
   * Connect to the protoo server via WebSocket.
   */
  async connect(url: string): Promise<void> {
    if (this.peer) {
      throw new Error('Already connected. Call close() first.')
    }

    this.url = url

    logger.info('Connecting to protoo server at', url)
    const transport = new protooClient.WebSocketTransport(url)
    this.peer = new protooClient.Peer(transport)

    // Setup event handlers
    this.peer.on('open', () => {
      logger.info('Protoo connection opened')
      this.options.onOpen?.()
      this.events.dispatchEvent('open', undefined)
    })

    this.peer.on('close', () => {
      logger.info('Protoo connection closed')
      this.options.onClose?.()
      this.events.dispatchEvent('close', undefined)
    })

    this.peer.on('failed', (currentAttempt: number) => {
      logger.info('Protoo connection failed:', currentAttempt)
      this.options.onFailed?.(currentAttempt)
      this.events.dispatchEvent('failed', currentAttempt)
    })

    this.peer.on('disconnected', () => {
      logger.info('Protoo connection disconnected')
      this.options.onDisconnected?.()
      this.events.dispatchEvent('disconnected', undefined)
    })

    this.peer.on('request', (request: any, accept: any, reject: any) => {
      const typedRequest: ProtooRequest = {
        method: request.method,
        data: request.data,
      }
      this.options.onRequest?.(typedRequest, accept, reject)
      this.events.dispatchEvent('request', { request: typedRequest, accept, reject })
    })

    this.peer.on('notification', (notification: any) => {
      const typedNotification: ProtooNotification = {
        method: notification.method,
        data: notification.data,
      }
      logger.info('Received notification:', typedNotification.method, typedNotification.data)
      this.options.onNotification?.(typedNotification)
      this.events.dispatchEvent('notification', typedNotification)
    })

    // Wait for connection to open
    return new Promise((resolve, reject) => {
      const onOpen = () => {
        cleanup()
        resolve()
      }
      const onFailed = () => {
        cleanup()
        reject(new Error('Failed to connect to protoo server'))
      }
      const cleanup = () => {
        this.peer?.off('open', onOpen)
        this.peer?.off('failed', onFailed)
      }
      this.peer.on('open', onOpen)
      this.peer.on('failed', onFailed)
    })
  }

  /**
   * Send a request to the server and wait for response.
   */
  async sendRequest(method: string, data?: any): Promise<any> {
    if (!this.peer) {
      throw new Error('Not connected. Call connect() first.')
    }
    if (this.peer.closed) {
      throw new Error('Peer is closed.')
    }
    return this.peer.request(method, data)
  }

  /**
   * Send a notification to the server (no response expected).
   */
  async sendNotify(method: string, data?: any): Promise<void> {
    if (!this.peer) {
      throw new Error('Not connected. Call connect() first.')
    }
    if (this.peer.closed) {
      throw new Error('Peer is closed.')
    }
    this.peer.notify(method, data)
  }

  /**
   * Check if currently connected.
   */
  isConnected(): boolean {
    return this.peer !== null && !this.peer.closed
  }

  /**
   * Get connection state.
   */
  getState(): 'new' | 'connecting' | 'connected' | 'closed' {
    if (!this.peer) return 'new'
    if (this.peer.closed) return 'closed'
    // protoo-client doesn't expose connecting state directly, assume connected if not closed
    return 'connected'
  }

  /**
   * Close the connection.
   */
  close() {
    if (this.peer) {
      try {
        this.peer.close()
      } catch {}
      this.peer = null
    }
    this.url = null
  }

  /**
   * Reconnect using the same URL.
   */
  async reconnect(): Promise<void> {
    if (!this.url) {
      throw new Error('No URL to reconnect to. Call connect() first.')
    }
    this.close()
    return this.connect(this.url)
  }
}

export default ProtooClientWrap
