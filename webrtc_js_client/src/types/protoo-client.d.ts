/**
 * Type definitions for protoo-client
 * Library doesn't provide official TypeScript types, so we define them here.
 */

declare module 'protoo-client' {
  export class WebSocketTransport {
    constructor(url: string, options?: any)
  }

  export class Peer {
    constructor(transport: WebSocketTransport)
    
    readonly closed: boolean
    
    close(): void
    
    request(method: string, data?: any): Promise<any>
    
    notify(method: string, data?: any): void
    
    on(event: 'open', listener: () => void): void
    on(event: 'close', listener: () => void): void
    on(event: 'failed', listener: (currentAttempt: number) => void): void
    on(event: 'disconnected', listener: () => void): void
    on(event: 'request', listener: (request: Request, accept: (data?: any) => void, reject: (errorCode?: number, errorReason?: string) => void) => void): void
    on(event: 'notification', listener: (notification: Notification) => void): void
    
    off(event: string, listener: (...args: any[]) => void): void
  }

  export interface Request {
    method: string
    data?: any
  }

  export interface Notification {
    method: string
    data?: any
  }
}
