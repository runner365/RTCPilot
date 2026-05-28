/**
 * Device: Handles media device capture (camera and microphone).
 * Provides methods to open devices and manage MediaStream lifecycle.
 */

export type DeviceConstraints = MediaStreamConstraints

export type DeviceOptions = {
  defaultConstraints?: DeviceConstraints
  onError?: (error: Error) => void
}

export class Device {
  private stream: MediaStream | null = null
  private options: DeviceOptions

  constructor(options: DeviceOptions = {}) {
    this.options = options
  }

  /**
   * Open camera and microphone and return the MediaStream.
   * Idempotent: returns existing stream if already opened.
   */
  async openDevices(constraints?: DeviceConstraints): Promise<MediaStream> {
    if (this.stream) return this.stream

    const finalConstraints: DeviceConstraints =
      constraints ?? this.options.defaultConstraints ?? { audio: true, video: true }

    try {
      const stream = await navigator.mediaDevices.getUserMedia(finalConstraints)
      this.stream = stream
      return stream
    } catch (err) {
      const error = err instanceof Error ? err : new Error(String(err))
      this.options.onError?.(error)
      throw error
    }
  }

  /**
   * Get the current MediaStream (may be null if not opened yet).
   */
  getStream(): MediaStream | null {
    return this.stream
  }

  /**
   * Check if devices are currently opened.
   */
  isOpen(): boolean {
    return this.stream !== null
  }

  /**
   * Stop all tracks and release the MediaStream.
   */
  close() {
    if (this.stream) {
      for (const track of this.stream.getTracks()) {
        try {
          track.stop()
        } catch {}
      }
      this.stream = null
    }
  }

  /**
   * Get available media devices (cameras, microphones, speakers).
   */
  static async enumerateDevices(): Promise<MediaDeviceInfo[]> {
    return navigator.mediaDevices.enumerateDevices()
  }

  /**
   * Get available video input devices (cameras).
   */
  static async getVideoInputDevices(): Promise<MediaDeviceInfo[]> {
    const devices = await Device.enumerateDevices()
    return devices.filter(d => d.kind === 'videoinput')
  }

  /**
   * Get available audio input devices (microphones).
   */
  static async getAudioInputDevices(): Promise<MediaDeviceInfo[]> {
    const devices = await Device.enumerateDevices()
    return devices.filter(d => d.kind === 'audioinput')
  }
}

export default Device
