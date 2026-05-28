/**
 * PCWrap: Minimal helper around RTCPeerConnection for pushing local media.
 * Features:
 * 1) Capture camera and microphone via getUserMedia
 * 2) Initialize a WebRTC RTCPeerConnection and attach local tracks
 * 3) Create an SDP offer suitable for pushing media and emit it to upper layer
 */

import { logger } from '../logger'

export type PCDirection = 'sendonly' | 'recvonly' | 'sendrecv'

export interface RecvTransceiverSpec {
	type: 'audio' | 'video'
	pusher_id: string
}

export type PCWrapOptions = {
	iceServers?: RTCIceServer[]
	mediaConstraints?: MediaStreamConstraints
	onOffer?: (offer: RTCSessionDescriptionInit) => void
	onIceCandidate?: (candidate: RTCIceCandidateInit | null) => void
	/**
	 * If true (default), use trickle ICE: emit offer immediately and send ICE candidates as they arrive.
	 * If false, wait for ICE gathering to complete before emitting the offer.
	 */
	trickleIce?: boolean
	/**
	 * Direction of media flow: 'sendonly' (push), 'recvonly' (subscribe), 'sendrecv' (bidirectional).
	 * Defaults to 'sendonly' for backward compatibility.
	 */
	direction?: PCDirection
}

export type OfferEventDetail = RTCSessionDescriptionInit

/** Simple typed event emitter using EventTarget */
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

type PCWrapEventMap = {
	offer: OfferEventDetail
	error: Error
	connectionstatechange: RTCPeerConnectionState
	icecandidate: RTCIceCandidateInit | null
	track: RTCTrackEvent
}

export class PCWrap {
	private pc: RTCPeerConnection | null = null
	private localStream: MediaStream | null = null
	private options: PCWrapOptions
	private events = new TypedEventTarget<PCWrapEventMap>()
	private direction: PCDirection
	private recvMap: Map<string, string> = new Map() // pusher_id -> mid

	constructor(options: PCWrapOptions = {}) {
		this.options = options
		this.direction = options.direction ?? 'sendonly'
	}

	/**
	 * Subscribe to internal events (e.g., 'offer', 'error', 'connectionstatechange').
	 */
		on<K extends keyof PCWrapEventMap & string>(type: K, listener: (detail: PCWrapEventMap[K]) => void) {
			return this.events.addEventListener(type, listener)
		}

	/**
	 * Get the current direction mode (sendonly/recvonly/sendrecv).
	 */
	getDirection(): PCDirection {
		return this.direction
	}

	/**
	 * Get the underlying RTCPeerConnection instance.
	 */
	getPeerConnection(): RTCPeerConnection | null {
		return this.pc
	}

	/**
	 * Set the local MediaStream from an external source (e.g., Device.ts).
	 * If pc is already initialized, tracks will be attached immediately.
	 */
	setLocalStream(stream: MediaStream) {
		this.localStream = stream
		// Attach tracks to existing pc if already initialized
		if (this.pc) {
			this.attachLocalTracks()
		}
	}

	/**
	 * Get the current local MediaStream.
	 */
	getLocalStream(): MediaStream | null {
		return this.localStream
	}

	/**
	 * Open devices and capture camera + microphone.
	 * Idempotent: returns existing stream if already opened.
	 * @deprecated Consider using Device.ts and setLocalStream instead for better separation of concerns.
	 */
	async openDevices(constraints?: MediaStreamConstraints): Promise<MediaStream> {
		if (this.localStream) return this.localStream
		if (this.direction === 'recvonly') {
			throw new Error('openDevices() not valid in recvonly mode')
		}
		const mediaConstraints: MediaStreamConstraints =
			constraints ?? this.options.mediaConstraints ?? { audio: true, video: true }
		try {
			const stream = await navigator.mediaDevices.getUserMedia(mediaConstraints)
			this.localStream = stream
			return stream
		} catch (err) {
			const error = err instanceof Error ? err : new Error(String(err))
			this.events.dispatchEvent('error', error)
			throw error
		}
	}

	/**
	 * Add recvonly transceivers based on a spec list.
	 * Each spec: { type: 'audio'|'video', pusher_id: string }
	 * Stores mapping pusher_id -> transceiver.mid for later correlation.
	 */
	addTransceivers(specs: RecvTransceiverSpec[]): RTCPeerConnection {
		if (this.direction !== 'recvonly') {
			throw new Error('addTransceivers() only valid when direction=recvonly')
		}
		const pc = this.pc ?? this.initPeerConnection()
		// Create a placeholder MediaStream to ensure Offer contains msid
		const placeholderStream = new MediaStream()
		for (const spec of specs) {
			// Prevent duplicate by same type & pusher_id (mid uniqueness after createOffer)
			if ([...this.recvMap.keys()].includes(spec.pusher_id)) continue
			const transceiver = pc.addTransceiver(spec.type, { 
				direction: 'recvonly',
				streams: [placeholderStream]
			})
			// mid is assigned after offer creation; temporarily store placeholder
			this.recvMap.set(spec.pusher_id, transceiver.mid || '')
		}
		return pc
	}

	/**
	 * Initialize the RTCPeerConnection and add local tracks if available.
	 * Idempotent: returns existing pc if already initialized.
	 */
	initPeerConnection(): RTCPeerConnection {
		if (this.pc) return this.pc
		const configuration: RTCConfiguration = {
			iceServers: this.options.iceServers ?? [
				{ urls: 'stun:stun.l.google.com:19302' },
			],
		}
		const pc = new RTCPeerConnection(configuration)

		pc.onconnectionstatechange = () => {
			this.events.dispatchEvent('connectionstatechange', pc.connectionState)
		}

		pc.onicecandidate = (ev) => {
			const cand = ev.candidate ? ev.candidate.toJSON() : null
			this.options.onIceCandidate?.(cand)
			this.events.dispatchEvent('icecandidate', cand)
		}

		pc.ontrack = (ev) => {
			logger.info('PCWrap ontrack event:', ev);
			this.events.dispatchEvent('track', ev)
		}

		if (this.direction === 'recvonly') {
			// Defer adding transceivers to openRecvOnly()
		} else {
			if (this.localStream) {
				for (const track of this.localStream.getTracks()) {
					pc.addTrack(track, this.localStream)
				}
			}
		}

		this.pc = pc
		return pc
	}

	/**
	 * Ensure local tracks are added after devices open or when new devices appear.
	 */
	attachLocalTracks() {
		if (!this.pc || !this.localStream) return
		const senders = this.pc.getSenders()
		for (const track of this.localStream.getTracks()) {
			const already = senders.find((s) => s.track && s.track.kind === track.kind)
			if (!already) this.pc.addTrack(track, this.localStream)
		}
	}

	/**
	 * Create an SDP offer for sending local media and emit it.
	 * Waits for ICE gathering to complete to provide a complete SDP.
	 */
	async createOfferAndEmit(options?: RTCOfferOptions): Promise<RTCSessionDescriptionInit> {
		const pc = this.pc ?? this.initPeerConnection()
		if (this.direction === 'recvonly') {
			// Verify transceivers were added via addTransceivers()
			if (pc.getTransceivers().length === 0) {
				throw new Error('No transceivers found. Call addTransceivers() before createOfferAndEmit() in recvonly mode.')
			}
		} else {
			this.attachLocalTracks()
		}

		const offer = await pc.createOffer({
			...options,
			// For pure push, these are not strictly required when addTrack is used
		})

			await pc.setLocalDescription(offer)
			if (this.options.trickleIce === false) {
				await this.waitForIceGatheringComplete(pc)
			}
		const localDesc = pc.localDescription!

		// Emit via callback and event
		this.options.onOffer?.(localDesc)
		this.events.dispatchEvent('offer', localDesc)
		return localDesc
	}

		/** Public: await ICE gathering completion if caller needs a complete SDP. */
		async waitForIceGathering(): Promise<void> {
			const pc = this.pc ?? this.initPeerConnection()
			return this.waitForIceGatheringComplete(pc)
		}

	/**
	 * Set the remote description (answer from the remote peer).
	 * Throws if pc is not initialized yet.
	 */
	setRemoteDescription(answer: RTCSessionDescriptionInit): Promise<void> {
		if (!this.pc) {
			throw new Error('RTCPeerConnection not initialized. Call initPeerConnection() first.')
		}
		try {
			logger.info('setting remote sdp description:', answer);
			this.pc.setRemoteDescription(answer)
		} catch (err) {
			const error = err instanceof Error ? err : new Error(String(err))
			this.events.dispatchEvent('error', error)
			throw error
		}
		logger.info('set remote sdp description success')
	}

	/** Utility: Wait until ICE gathering completes (or already complete). */
	private waitForIceGatheringComplete(pc: RTCPeerConnection): Promise<void> {
		if (pc.iceGatheringState === 'complete') return Promise.resolve()
		return new Promise((resolve, reject) => {
			const onStateChange = () => {
				if (pc.iceGatheringState === 'complete') {
					pc.removeEventListener('icegatheringstatechange', onStateChange)
					resolve()
				}
			}
			const onConnectionStateChange = () => {
				if (pc.connectionState === 'failed' || pc.connectionState === 'closed' || pc.connectionState === 'disconnected') {
					pc.removeEventListener('icegatheringstatechange', onStateChange)
					pc.removeEventListener('connectionstatechange', onConnectionStateChange)
					reject(new Error('ICE gathering failed due to connection state: ' + pc.connectionState))
				}
			}
			pc.addEventListener('icegatheringstatechange', onStateChange)
			pc.addEventListener('connectionstatechange', onConnectionStateChange)
		})
	}

	/** Stop tracks and close the peer connection */
	close() {
		if (this.pc) {
			try { this.pc.close() } catch {}
			this.pc = null
		}
		if (this.localStream) {
			for (const track of this.localStream.getTracks()) {
				try { track.stop() } catch {}
			}
			this.localStream = null
		}
	}
}

export default PCWrap

