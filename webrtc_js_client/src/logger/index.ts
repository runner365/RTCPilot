export enum LogLevel {
  DEBUG = 0,
  INFO = 1,
  WARN = 2,
  ERROR = 3,
}

const LEVEL_LABELS: Record<LogLevel, string> = {
  [LogLevel.DEBUG]: 'DEBUG',
  [LogLevel.INFO]: 'INFO',
  [LogLevel.WARN]: 'WARN',
  [LogLevel.ERROR]: 'ERROR',
}

let currentLevel: LogLevel = LogLevel.INFO

export function setLogLevel(level: LogLevel): void {
  currentLevel = level
}

export function getLogLevel(): LogLevel {
  return currentLevel
}

function formatNow(): string {
  return new Date().toISOString()
}

function log(level: LogLevel, label: string, args: any[]): void {
  if (level < currentLevel) return
  const prefix = `[${formatNow()}] [${label}]`
  switch (level) {
    case LogLevel.ERROR:
      console.error(prefix, ...args)
      break
    case LogLevel.WARN:
      console.warn(prefix, ...args)
      break
    default:
      console.log(prefix, ...args)
  }
}

export const logger = {
  debug(...args: any[]): void {
    log(LogLevel.DEBUG, 'DEBUG', args)
  },
  info(...args: any[]): void {
    log(LogLevel.INFO, 'INFO', args)
  },
  warn(...args: any[]): void {
    log(LogLevel.WARN, 'WARN', args)
  },
  error(...args: any[]): void {
    log(LogLevel.ERROR, 'ERROR', args)
  },
}
