<script setup lang="ts">
import { ref, computed, nextTick, watch } from 'vue'
import ProtooClientWrap from './peerConnectionWrap/ProtooClientWrap'
import PCWrap from './peerConnectionWrap/PCWrap'
import type { RecvTransceiverSpec } from './peerConnectionWrap/PCWrap'
import { useWallet } from './composables/useWallet'
import { useTokenBuy } from './composables/useTokenBuy'
import { logger } from './logger'
import { apiGet, apiPost, setToken } from './api'
import {
  createPublicClient,
  http,
  encodeFunctionData,
  decodeEventLog,
  parseUnits,
  type Address,
} from 'viem'
import { sepolia } from 'viem/chains'

/*
{
    "pushers": [
        {
            "pusherId": "a0931813-e158-15b9-ac3e-019d6da5b155",
            "rtpParam": {
                "av_type": "video",
                "clock_rate": 90000,
                "codec": "H264",
                "fmtp_param": "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f",
                "key_request": true,
                "mid_ext_id": 4,
                "payload_type": 109,
                "rtcp_features": [
                    "goog-remb",
                    "transport-cc",
                    "ccm fir",
                    "nack",
                    "nack pli"
                ],
                "rtx_payload_type": 114,
                "rtx_ssrc": 3854170276,
                "ssrc": 4201321986,
                "tcc_ext_id": 3,
                "use_nack": true
            }
        },
        {
            "pusherId": "fe14a431-522c-0e76-f686-13a590919df7",
            "rtpParam": {
                "av_type": "audio",
                "channel": 2,
                "clock_rate": 48000,
                "codec": "opus",
                "fmtp_param": "minptime=10;useinbandfec=1",
                "mid_ext_id": 4,
                "payload_type": 111,
                "rtcp_features": [
                    "transport-cc"
                ],
                "rtx_payload_type": 0,
                "rtx_ssrc": 0,
                "ssrc": 1821614664,
                "tcc_ext_id": 3,
                "use_nack": false
            }
        }
    ],
    "roomId": "hn64gxaw",
    "userId": "6725"
}
  */
// create interfaces for type safety of rtpParam and pusher
interface RtpParam {
  av_type: 'audio' | 'video'
  channel?: number
  clock_rate: number
  codec: string
  fmtp_param: string
  key_request: boolean
  mid_ext_id: number
  payload_type: number
  rtcp_features: string[]
  rtx_payload_type: number
  rtx_ssrc: number
  ssrc: number
  tcc_ext_id: number
  use_nack: boolean
}

interface Pusher {
  rtpParam: RtpParam
  pusherId: string
}

interface RemoteUser {
  userId: string
  userName: string
  pushers: Pusher[]
  videoRef?: HTMLVideoElement
  receivePcWrap?: PCWrap
  playStream?: MediaStream
  // mute states for per-puller controls
  videoMuted?: boolean
  audioMuted?: boolean
  // user connection state
  state?: 'connected' | 'disconnected'
}

// Chat message interface and state
interface ChatMessage {
  userId: string
  userName: string
  message: string
  self?: boolean
  time?: number
}

// Normalize user IDs to a stable string form for comparisons.
// Handles numbers, strings with extra whitespace, and Unicode normalization.
function normalizeId(id: any): string {
  try {
    return String(id).trim().normalize('NFC')
  } catch (e) {
    return String(id).trim()
  }
}

// These are set by createRoom() from the server response
const wsUrl = ref('')
const roomId = ref('')
const roomToken = ref('')

// --- Wallet connection ---
const {
  walletAddress,
  connectionError,
  isWalletInstalled,
  truncatedAddress,
  provider,
  connectWallet,
} = useWallet()

// userId is the wallet address (empty string when not connected)
const userId = computed(() => walletAddress.value)
// userName is the truncated display form
const userName = computed(() => truncatedAddress.value)

const wsStatus = ref<'disconnected' | 'connected' | 'joined'>('disconnected')
const sendPcWarp = ref<PCWrap | null>(null)
// Audience-only mode: disable local preview and Open button
const onlyAudience = ref<boolean>(false)

// --- Contract addresses (fetched from HTTP API) ---
interface Contracts {
  meeting_token: string
  uniswap_liquidity_setup: string
  meeting_manager: string
}
const contracts = ref<Contracts | null>(null)
const contractsError = ref<string>('')
const apiBaseUrl = ref('http://192.168.1.221:9080')

// --- Meeting Manager contract ABIs ---
const meetingManagerAbi = [
  {
    inputs: [],
    name: 'creationFee',
    outputs: [{ name: '', type: 'uint256' }],
    stateMutability: 'view',
    type: 'function',
  },
  {
    inputs: [
      { name: 'name', type: 'string' },
      { name: 'description', type: 'string' },
      { name: 'startTime', type: 'uint256' },
      { name: 'endTime', type: 'uint256' },
      { name: 'feePerAttendee', type: 'uint256' },
    ],
    name: 'createMeeting',
    outputs: [{ name: 'meetingId', type: 'uint256' }],
    stateMutability: 'nonpayable',
    type: 'function',
  },
  {
    inputs: [{ name: 'meetingId', type: 'uint256' }],
    name: 'joinMeeting',
    outputs: [],
    stateMutability: 'nonpayable',
    type: 'function',
  },
  {
    inputs: [{ name: 'meetingId', type: 'uint256' }],
    name: 'getMeeting',
    outputs: [
      {
        name: '',
        type: 'tuple',
        components: [
          { name: 'creator', type: 'address' },
          { name: 'name', type: 'string' },
          { name: 'description', type: 'string' },
          { name: 'startTime', type: 'uint256' },
          { name: 'endTime', type: 'uint256' },
          { name: 'feePerAttendee', type: 'uint256' },
          { name: 'attendeeCount', type: 'uint256' },
          { name: 'active', type: 'bool' },
        ],
      },
    ],
    stateMutability: 'view',
    type: 'function',
  },
] as const

const erc20ApproveAbi = [
  {
    inputs: [
      { name: 'spender', type: 'address' },
      { name: 'amount', type: 'uint256' },
    ],
    name: 'approve',
    outputs: [{ name: '', type: 'bool' }],
    stateMutability: 'nonpayable',
    type: 'function',
  },
] as const

const erc20BalanceOfAbi = [
  {
    inputs: [{ name: 'account', type: 'address' }],
    name: 'balanceOf',
    outputs: [{ name: '', type: 'uint256' }],
    stateMutability: 'view',
    type: 'function',
  },
] as const

const MeetingCreatedEvent = {
  type: 'event',
  name: 'MeetingCreated',
  inputs: [
    { name: 'meetingId', type: 'uint256', indexed: true },
    { name: 'creator', type: 'address', indexed: true },
    { name: 'name', type: 'string', indexed: false },
    { name: 'feePerAttendee', type: 'uint256', indexed: false },
    { name: 'startTime', type: 'uint256', indexed: false },
    { name: 'endTime', type: 'uint256', indexed: false },
  ],
} as const

const SEPOLIA_RPC = 'https://ethereum-sepolia.publicnode.com'

let _publicClient: ReturnType<typeof createPublicClient> | null = null
function getPublicClient() {
  if (!_publicClient) {
    _publicClient = createPublicClient({ chain: sepolia, transport: http(SEPOLIA_RPC) })
  }
  return _publicClient
}

// --- Tab state ---
const activeTab = ref<'create' | 'attend'>('create')

// --- Meeting form state ---
const meetingName = ref('')
const meetingFeePerAttendee = ref('0.01')
const meetingStep = ref<'idle' | 'approving' | 'creating_meeting' | 'creating_room'>('idle')
const meetingError = ref('')

// --- Attend room state ---
const attendWsUrl = ref('')
const attendStep = ref<'idle' | 'approving' | 'joining_meeting' | 'joining_room'>('idle')
const attendError = ref('')
const attendStatus = ref<'idle' | 'pending' | 'done' | 'error'>('idle')

// --- Token balance ---
const tokenBalance = ref('')
const tokenBalanceLoading = ref(false)

async function fetchTokenBalance() {
  if (loginStatus.value !== 'logged_in') return
  try {
    tokenBalanceLoading.value = true
    const data = await apiGet<{ code: number; balance?: string }>(
      `${apiBaseUrl.value}/api/v1/user/token-balance`
    )
    if (data.code === 0 && data.balance) {
      const raw = BigInt(data.balance)
      tokenBalance.value = (Number(raw) / 1e18).toFixed(2)
    }
  } catch (e) {
    logger.warn('Failed to fetch token balance:', e)
  } finally {
    tokenBalanceLoading.value = false
  }
}

// --- Login state ---
const loginStatus = ref<'idle' | 'logging_in' | 'logged_in' | 'error'>('idle')
const loginError = ref<string>('')

async function login(): Promise<void> {
  loginStatus.value = 'logging_in'
  loginError.value = ''

  // Step 0: connect wallet if not connected
  if (!walletAddress.value) {
    try {
      await connectWallet()
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      loginError.value = msg
      loginStatus.value = 'error'
      return
    }
  }

  try {
    // Step 1: request challenge message from server
    const challengeData = await apiPost<{ code: number; message?: string; error?: string }>(
      `${apiBaseUrl.value}/api/v1/auth/challenge`,
      { address: walletAddress.value },
    )
    if (challengeData.code !== 0) {
      throw new Error(challengeData.error || 'challenge failed')
    }
    const message = challengeData.message!
    logger.info('Challenge message received:', message)

    // Step 2: sign the message with wallet
    const eth = provider.value
    if (!eth) {
      throw new Error('Wallet provider not available')
    }
    const addr = walletAddress.value
    let signature: string
    try {
      signature = await eth.request({
        method: 'personal_sign',
        params: [message, addr],
      }) as string
    } catch {
      // Some wallets reverse the parameter order
      signature = await eth.request({
        method: 'personal_sign',
        params: [addr, message],
      }) as string
    }
    logger.info('Signature obtained:', signature)

    // Step 3: submit signature to server for verification
    const loginData = await apiPost<{ code: number; error?: string; token?: string; user?: unknown }>(
      `${apiBaseUrl.value}/api/v1/auth/login`,
      { address: addr, message, signature },
    )
    if (loginData.code !== 0) {
      throw new Error(loginData.error || `code ${loginData.code}`)
    }

    setToken(loginData.token!)
    loginStatus.value = 'logged_in'
    fetchTokenBalance()
    logger.info('Login success:', loginData.user)
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    loginError.value = msg
    loginStatus.value = 'error'
    logger.warn('Login failed:', msg)
  }
}

async function fetchContracts(): Promise<void> {
  try {
    const data = await apiGet<Contracts>(`${apiBaseUrl.value}/api/v1/contracts`)
    contracts.value = data
    contractsError.value = ''
    logger.info('Contract addresses loaded:', data)
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    contractsError.value = msg
    logger.warn('Failed to fetch contract addresses:', msg, '- retrying in 5s')
    setTimeout(fetchContracts, 5000)
  }
}

// Start fetching contracts on startup
fetchContracts()

// --- Create room ---
const createRoomStatus = ref<'idle' | 'pending' | 'done' | 'error'>('idle')
const createRoomError = ref<string>('')

async function createRoom(): Promise<void> {
  createRoomStatus.value = 'pending'
  createRoomError.value = ''
  meetingError.value = ''

  const c = contracts.value
  if (!c) {
    createRoomError.value = 'Contract addresses not loaded yet'
    createRoomStatus.value = 'error'
    return
  }
  const provider_ = provider.value as any
  if (!provider_) {
    createRoomError.value = 'Wallet not connected'
    createRoomStatus.value = 'error'
    return
  }
  const account = walletAddress.value
  if (!account) {
    createRoomError.value = 'Wallet address not available'
    createRoomStatus.value = 'error'
    return
  }

  const managerAddr = c.meeting_manager as Address
  const tokenAddr = c.meeting_token as Address
  const pc = getPublicClient()

  const name = meetingName.value.trim() || `Meeting ${Date.now()}`
  const nowSec = Math.floor(Date.now() / 1000)
  const startTime = nowSec
  const endTime = nowSec + 86400 // 24 hours
  const feePerAttendeeWei = parseUnits(meetingFeePerAttendee.value.trim() || '0.01', 18)

  try {
    // Step 1: Query creationFee from contract
    const creationFee = await pc.readContract({
      address: managerAddr,
      abi: meetingManagerAbi,
      functionName: 'creationFee',
    }) as bigint
    logger.info('creationFee:', creationFee.toString())

    // Step 2: Approve MTK spending
    meetingStep.value = 'approving'
    const approveData = encodeFunctionData({
      abi: erc20ApproveAbi,
      functionName: 'approve',
      args: [managerAddr, creationFee],
    })
    const approveHash = await provider_.request({
      method: 'eth_sendTransaction',
      params: [{ from: account, to: tokenAddr, data: approveData }],
    }) as string
    logger.info('Approve tx sent:', approveHash)
    const approveReceipt = await pc.waitForTransactionReceipt({ hash: approveHash as `0x${string}` })
    if (approveReceipt.status !== 'success') {
      throw new Error('MTK approve transaction failed')
    }
    logger.info('MTK approved')

    // Step 3: Create meeting on-chain
    meetingStep.value = 'creating_meeting'
    const createData = encodeFunctionData({
      abi: meetingManagerAbi,
      functionName: 'createMeeting',
      args: [name, '', BigInt(startTime), BigInt(endTime), feePerAttendeeWei],
    })
    const createHash = await provider_.request({
      method: 'eth_sendTransaction',
      params: [{ from: account, to: managerAddr, data: createData }],
    }) as string
    logger.info('createMeeting tx sent:', createHash)
    const createReceipt = await pc.waitForTransactionReceipt({ hash: createHash as `0x${string}` })
    if (createReceipt.status !== 'success') {
      throw new Error('createMeeting transaction failed')
    }

    // Step 4: Parse meetingId from event logs
    let meetingId = 0n
    for (const log of createReceipt.logs) {
      if (log.address.toLowerCase() === managerAddr.toLowerCase()) {
        try {
          const decoded = decodeEventLog({
            abi: [MeetingCreatedEvent],
            data: log.data,
            topics: log.topics,
          })
          if (decoded.eventName === 'MeetingCreated') {
            meetingId = (decoded.args as any).meetingId as bigint
            break
          }
        } catch { /* skip non-matching logs */ }
      }
    }
    if (meetingId === 0n) {
      throw new Error('Failed to parse meetingId from transaction receipt')
    }

    // Step 5: POST createroom with meetingId and blockNumber
    const blockNumber = createReceipt.blockNumber
    meetingStep.value = 'creating_room'
    logger.info('Creating room for meetingId:', meetingId.toString(), 'blockNumber:', blockNumber)
    const data = await apiPost<{ code: number; error?: string; roomId?: string; roomToken?: string; wsUrl?: string; createdAt?: string }>(
      `${apiBaseUrl.value}/api/v1/createroom`,
      { meetingId: meetingId.toString(), blockNumber: `0x${blockNumber.toString(16)}` },
    )
    if (data.code !== 0) {
      throw new Error(data.error || `code ${data.code}`)
    }
    logger.info('Create room response:', data)

    roomId.value = data.roomId!
    wsUrl.value = data.wsUrl! + '?roomid=' + data.roomId! + '&meetingId=' + meetingId.toString()
    roomToken.value = data.roomToken!
    createRoomStatus.value = 'done'
    meetingStep.value = 'idle'
    // Refresh token balance after spending
    fetchTokenBalance()
    logger.info('Room created:', data.roomId, 'wsUrl:', data.wsUrl)
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    createRoomError.value = msg
    createRoomStatus.value = 'error'
    meetingStep.value = 'idle'
    logger.warn('Create room failed:', msg)
  }
}

// --- Attend room ---
function parseUrlParam(url: string, param: string): string {
  const q = url.indexOf('?')
  if (q < 0) return ''
  const query = url.slice(q + 1)
  for (const part of query.split('&')) {
    const [k, v] = part.split('=')
    if (decodeURIComponent(k) === param) return decodeURIComponent(v || '')
  }
  return ''
}

async function attendRoom(): Promise<void> {
  attendStatus.value = 'pending'
  attendError.value = ''

  const c = contracts.value
  if (!c) {
    attendError.value = 'Contract addresses not loaded yet'
    attendStatus.value = 'error'
    return
  }
  const provider_ = provider.value as any
  if (!provider_) {
    attendError.value = 'Wallet not connected'
    attendStatus.value = 'error'
    return
  }
  const account = walletAddress.value
  if (!account) {
    attendError.value = 'Wallet address not available'
    attendStatus.value = 'error'
    return
  }

  const managerAddr = c.meeting_manager as Address
  const tokenAddr = c.meeting_token as Address
  const pc = getPublicClient()

  // Parse meetingId from the shared wsUrl
  const rawUrl = attendWsUrl.value.trim()
  const meetingIdStr = parseUrlParam(rawUrl, 'meetingId')
  const roomIdStr = parseUrlParam(rawUrl, 'roomid')

  if (!meetingIdStr) {
    attendError.value = 'Invalid URL: missing meetingId parameter'
    attendStatus.value = 'error'
    return
  }
  const mId = BigInt(meetingIdStr)

  if (mId <= 0n) {
    attendError.value = 'Invalid meetingId in URL'
    attendStatus.value = 'error'
    return
  }

  try {
    // Step 1: Query getMeeting for feePerAttendee
    attendStep.value = 'approving'
    const meeting = await pc.readContract({
      address: managerAddr,
      abi: meetingManagerAbi,
      functionName: 'getMeeting',
      args: [mId],
    }) as any
    logger.info('getMeeting result:', meeting)
    const feePerAttendee = meeting.feePerAttendee as bigint

    // Step 2: Approve MTK for feePerAttendee
    const approveData = encodeFunctionData({
      abi: erc20ApproveAbi,
      functionName: 'approve',
      args: [managerAddr, feePerAttendee],
    })
    const approveHash = await provider_.request({
      method: 'eth_sendTransaction',
      params: [{ from: account, to: tokenAddr, data: approveData }],
    }) as string
    logger.info('Attend approve tx sent:', approveHash)
    const approveReceipt = await pc.waitForTransactionReceipt({ hash: approveHash as `0x${string}` })
    if (approveReceipt.status !== 'success') {
      throw new Error('MTK approve transaction failed')
    }
    logger.info('MTK approved for join')

    // Step 3: Call joinMeeting on-chain
    attendStep.value = 'joining_meeting'
    const joinData = encodeFunctionData({
      abi: meetingManagerAbi,
      functionName: 'joinMeeting',
      args: [mId],
    })
    const joinHash = await provider_.request({
      method: 'eth_sendTransaction',
      params: [{ from: account, to: managerAddr, data: joinData }],
    }) as string
    logger.info('joinMeeting tx sent:', joinHash)
    const joinReceipt = await pc.waitForTransactionReceipt({ hash: joinHash as `0x${string}` })
    if (joinReceipt.status !== 'success') {
      throw new Error('joinMeeting transaction failed')
    }
    logger.info('joinMeeting confirmed')

    // Step 4: POST joinroom
    attendStep.value = 'joining_room'
    logger.info('Joining room for meetingId:', meetingIdStr, 'roomId:', roomIdStr)
    const data = await apiPost<{ code: number; error?: string; roomId?: string; roomToken?: string; wsUrl?: string }>(
      `${apiBaseUrl.value}/api/v1/joinroom`,
      { meetingId: meetingIdStr },
    )
    if (data.code !== 0) {
      throw new Error(data.error || `code ${data.code}`)
    }
    logger.info('Join room response:', data)

    roomId.value = data.roomId!
    wsUrl.value = data.wsUrl! + '?roomid=' + data.roomId!
    roomToken.value = data.roomToken!
    attendStatus.value = 'done'
    attendStep.value = 'idle'
    fetchTokenBalance()
    logger.info('Attend room success, roomId:', data.roomId)
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    attendError.value = msg
    attendStatus.value = 'error'
    attendStep.value = 'idle'
    logger.warn('Attend room failed:', msg)
  }
}

// --- Token purchase ---
const {
  showModal: showBuyModal,
  tokenAmountInput: buyAmount,
  step: buyStep,
  ethRequiredFormatted: buyEthQuote,
  quoteError: buyQuoteError,
  buyError,
  buyTxHash,
  openBuyModal,
  closeModal: closeBuyModal,
  requestBuy,
  confirmBuy,
  cancelConfirm,
} = useTokenBuy(
  () => contracts.value,
  () => provider.value,
  () => walletAddress.value,
)

// When enabling audience-only, stop local preview and close local devices
watch(onlyAudience, (val) => {
  if (val) {
    try {
      if (sendPcWarp.value) {
        sendPcWarp.value.close()
        sendPcWarp.value = null
      }
    } catch {}
    if (videoEl.value) {
      try {
        const src = videoEl.value.srcObject as MediaStream | null
        if (src) {
          for (const t of src.getTracks()) { try { t.stop() } catch {} }
        }
      } catch {}
      videoEl.value.srcObject = null
    }
  }
})
// Local preview element
const videoEl = ref<HTMLVideoElement | null>(null)
// Remote users
const remoteUsers = ref<RemoteUser[]>([])

/**
 * Create a RemoteUser object from server user payload and initialize recvonly PC if pushers exist.
 * @param user server payload: { userId, userName, pushers: [{ rtpParam, pusherId }] }
 */
function createRemoteUser(user: any): RemoteUser {
  const remoteUser: RemoteUser = {
    userId: normalizeId(user.userId),
    userName: user.userName,
    pushers: user.pushers || []
  }
  if (remoteUser.pushers.length > 0) {
    remoteUser.receivePcWrap = new PCWrap({ direction: 'recvonly' })
    // Initialize playStream once upfront to avoid recreating on each track
    remoteUser.playStream = new MediaStream()
    // default mute states
    remoteUser.videoMuted = false
    remoteUser.audioMuted = false
    
    const specs: RecvTransceiverSpec[] = remoteUser.pushers.map((p: any) => ({
      type: p.rtpParam.av_type as 'audio' | 'video',
      pusher_id: p.pusherId
    }))
    remoteUser.receivePcWrap.addTransceivers(specs)
    logger.info(`Initialized receivePcWrap for user ${remoteUser.userId} with specs:`, specs)
  }
  return remoteUser
}

/**
 * Upsert a user into remoteUsers by userId.
 * - If user exists, update userName/pushers and extend receivePcWrap transceivers.
 * - If not exists, create and push new RemoteUser.
 * @param user server payload: { userId, userName, pushers: [{ rtpParam, pusherId }] }
 */
function upsertRemoteUser(user: any): RemoteUser {
  logger.info('Upserting remote user:', user, 'Current remoteUsers:', remoteUsers.value);
  let idx = -1;
  for (const u of remoteUsers.value) {
    logger.info('Existing user:', u.userId, 'input userId:', user.userId)
    if (normalizeId(u.userId) === normalizeId(user.userId)) {
      idx = remoteUsers.value.indexOf(u);
      break;
    }
  }
  if (idx >= 0) {
    const existing = remoteUsers.value.find(u => normalizeId(u.userId) === normalizeId(user.userId))! as RemoteUser
    existing.userName = user.userName ?? existing.userName
    const newPushers: Pusher[] = Array.isArray(user.pushers) ? user.pushers : []
    // Merge pushers by pusherId to avoid duplicates
    const pusherIds = new Set(existing.pushers.map(p => p.pusherId))
    for (const p of newPushers) {
      if (!pusherIds.has(p.pusherId)) existing.pushers.push(p)
    }
    // If recv PC exists, add transceivers for any newly seen pushers
    if (existing.receivePcWrap && newPushers.length > 0) {
      const specs: RecvTransceiverSpec[] = newPushers
        .filter(p => !pusherIds.has(p.pusherId))
        .map(p => ({ type: p.rtpParam.av_type as 'audio' | 'video', pusher_id: p.pusherId }))
      if (specs.length > 0) {
        existing.receivePcWrap.addTransceivers(specs)
        logger.info(`Extended transceivers for existing user ${existing.userId}:`, specs)
      }
    } else if (!existing.receivePcWrap && existing.pushers.length > 0) {
      // If no recv PC yet but pushers exist, create it
      existing.receivePcWrap = new PCWrap({ direction: 'recvonly' })
      existing.playStream = new MediaStream()
      const specs: RecvTransceiverSpec[] = existing.pushers.map(p => ({
        type: p.rtpParam.av_type as 'audio' | 'video',
        pusher_id: p.pusherId
      }))
      existing.receivePcWrap.addTransceivers(specs)
      logger.info(`Initialized receivePcWrap for existing user ${existing.userId} with specs:`, specs)
    }
    return existing
  } else {
    const created = createRemoteUser(user)
    logger.info('Created new remote user:', created)
    remoteUsers.value.push(created)
    return created
  }
}

// Helper to enable/disable tracks of a given kind for a remote user and update UI
function setTrackEnabledForUser(user: RemoteUser, kind: 'audio' | 'video', enabled: boolean) {
  if (user.playStream) {
    for (const t of user.playStream.getTracks()) {
      if (t.kind === kind) {
        try { t.enabled = enabled } catch (e) { /* ignore */ }
      }
    }
  }
  // Also update the video element UI: mute property for audio, visual opacity for video
  if (kind === 'audio' && user.videoRef) {
    try { user.videoRef.muted = !enabled } catch (e) {}
  }
  if (kind === 'video' && user.videoRef) {
    try { user.videoRef.style.opacity = enabled ? '1' : '0.45' } catch (e) {}
  }
}

const toggleAudioMute = (user: RemoteUser) => {
  user.audioMuted = !user.audioMuted
  setTrackEnabledForUser(user, 'audio', !user.audioMuted)
}

const toggleVideoMute = (user: RemoteUser) => {
  user.videoMuted = !user.videoMuted
  setTrackEnabledForUser(user, 'video', !user.videoMuted)
}

const wsClient = new ProtooClientWrap({
  onRequest: (request, accept, reject) => {
    logger.info('get request:', request.method, request.data)
    if (request.method === 'ping') {
      accept({ pong: true })
    } else {
      reject(404, 'Method not found')
    }
  },
  onNotification: (notification) => {
    logger.info('get notification:', notification.method, notification.data)
    if (notification.method === 'newUser') {
      logger.info('New user joined:', notification.data);
      //call upsertRemoteUser to add the new user
      for (const u of notification.data) {
        let remoteUser = upsertRemoteUser(u);
        if (remoteUser.pushers.length === 0) {
          logger.info('New user has no pushers, skipping pull:', remoteUser);
          continue
        }
        const specs: RecvTransceiverSpec[] = remoteUser.pushers.map(pusher => ({
          type: pusher.rtpParam.av_type as 'audio' | 'video',
          pusher_id: pusher.pusherId
        }));
        logger.info(`Pulling pushers for new user notification ${remoteUser.userId} with specs:`, specs)
        pullRemoteUser(roomId.value, remoteUser.userId, specs)
      }

    } else if (notification.method === 'userDisconnect') {
      logger.info('notification userDisconnect:', notification.data);
      // Mark users as disconnected instead of removing them immediately
      const userId = notification.data.userId;
      const idx = remoteUsers.value.findIndex(u => u.userId === userId);
      if (idx !== -1) {
        remoteUsers.value[idx].state = 'disconnected'
      }
    } else if (notification.method === 'userReconnect') {
      logger.info('notification userReconnect:', notification.data);
      // Mark users as connected again
      const userId = notification.data.userId;
      const idx = remoteUsers.value.findIndex(u => u.userId === userId);
      if (idx !== -1) {
        remoteUsers.value[idx].state = 'connected'
      }
    } else if (notification.method === 'newPusher') {
      logger.info('new user pushers:', notification.data);
      const data = notification.data;
      const remoteUser = upsertRemoteUser(data);
      // call pullRemoteUser for the new pushers
      if (remoteUser.pushers.length > 0) {
        const specs: RecvTransceiverSpec[] = remoteUser.pushers.map(pusher => ({
          type: pusher.rtpParam.av_type as 'audio' | 'video',
          pusher_id: pusher.pusherId
        }))
        pullRemoteUser(roomId.value, remoteUser.userId, specs)
          .then(() => {
            logger.info(`Pulled new pushers for user ${remoteUser.userId}`)
          })
          .catch((e) => {
            logger.error(`Pulling new pushers for user ${remoteUser.userId} failed:`, e)
          });
      }
    } else if (notification.method === 'textMessage') {
      // Incoming chat message from another user
      try {
        const payload = notification.data;
        logger.info('Incoming textMessage userId:', payload.userId, 
          'message:', payload.message, 'userName:', payload.userName);
        if (payload && typeof payload.message === 'string') {
          chatMessages.value.push({
            userId: normalizeId(payload.userId),
            userName: payload.userName ?? payload.userId,
            message: payload.message,
            self: false,
            time: Date.now()
          })
          // Auto-scroll chat list to bottom
          nextTick(() => {
            if (chatListEl.value) {
              chatListEl.value.scrollTop = chatListEl.value.scrollHeight
            }
          })
        }
      } catch (e) {
        logger.warn('Failed to handle textMessage notification:', e)
      }
    } else if (notification.method === 'userLeave') {
      logger.info('User leave:', notification.data)
      const leftUserId = notification.data['userId']
      // Remove users from remoteUsers
      // Also close their receivePcWrap if exists
      // remove videoRef srcObject to stop playback
      // remove UI About the user
      const idx = remoteUsers.value.findIndex(u => u.userId === leftUserId)
      if (idx >= 0) {
        const user = remoteUsers.value[idx]
        // Close receive PC
        if (user.receivePcWrap) {
          user.receivePcWrap.close()
          user.receivePcWrap = undefined
        }
        // Remove video srcObject
        if (user.videoRef) {
          user.videoRef.srcObject = null
          user.videoRef = undefined
        }
        // Remove from array
        remoteUsers.value.splice(idx, 1)
        logger.info(`Removed user ${leftUserId} from remoteUsers`)
      }
    }
  },
  onOpen: () => {
    logger.info('connected')
    wsStatus.value = 'connected'
  },
  onClose: () => {
    logger.info('disconnected')
    wsStatus.value = 'disconnected'
  },
});

// If wallet account changes while in a room, force-disconnect
watch(walletAddress, (newAddr, oldAddr) => {
  if (oldAddr && newAddr !== oldAddr && (wsStatus.value === 'connected' || wsStatus.value === 'joined')) {
    logger.warn('Wallet account changed mid-session, disconnecting')
    wsClient.close()
    wsStatus.value = 'disconnected'
    remoteUsers.value.forEach(u => {
      if (u.receivePcWrap) { u.receivePcWrap.close(); u.receivePcWrap = undefined }
      if (u.videoRef) { u.videoRef.srcObject = null; u.videoRef = undefined }
    })
    remoteUsers.value = []
  }
})

async function copyWsUrl() {
  if (!wsUrl.value) return
  try {
    await navigator.clipboard.writeText(wsUrl.value)
    logger.info('URL copied to clipboard:', wsUrl.value)
  } catch {
    // fallback for older browsers
    const el = document.getElementById('ws-url') as HTMLInputElement | null
    if (el) {
      el.select()
      document.execCommand('copy')
    }
  }
}

// setup a timer to send heartbeat when websocket is connected
setInterval(() => {
  if (wsStatus.value === 'connected' || wsStatus.value === 'joined') {
  const reqData = {
    roomId: roomId.value,
    userId: userId.value,
    userName: userName.value,
    time: Date.now()
  }
    wsClient.sendRequest('heartbeat', reqData)
      .then((response) => {
        logger.info('heartbeat response:', response)
      })
      .catch((e) => {
        logger.error('heartbeat failed:', e)
      })
  }
}, 3000);

// Chat state and helpers
const chatMessages = ref<ChatMessage[]>([])
const chatInput = ref<string>('')
const chatListEl = ref<HTMLDivElement | null>(null)
const showChat = ref<boolean>(true)

// send notification text message in a room and append to chat list on success
async function sendMessage(message: string): Promise<void> {
  if (wsStatus.value !== 'joined') {
    alert('Please join a room first.')
    throw new Error('Not joined')
  }
  const notifData = {
    roomId: roomId.value,
    userId: userId.value,
    userName: userName.value,
    message: message,
    time: Date.now()
  }
  return wsClient.sendNotify('textMessage', notifData)
    .then(() => {
      logger.info('textMessage notification sent:', notifData)
      // Show in local chat list
      chatMessages.value.push({
        userId: userId.value,
        userName: userName.value,
        message,
        self: true,
        time: Date.now()
      })
      nextTick(() => {
        if (chatListEl.value) {
          chatListEl.value.scrollTop = chatListEl.value.scrollHeight
        }
      })
    })
    .catch((e) => {
      logger.error('textMessage notification failed:', e)
      throw e
    })
}

async function onChatEnter() {
  const text = chatInput.value.trim()
  if (!text) return
  try {
    await sendMessage(text)
    chatInput.value = ''
  } catch (e) {
    alert('Message send failed: ' + (e instanceof Error ? e.message : String(e)))
  }
}

// Stubs for future functionality
async function onJoin() {
  logger.info('Join clicked. URL:', wsUrl.value)

  if (!wsUrl.value || wsUrl.value.trim() === '') {
    alert('WebSocket URL is empty. Please create a room first.')
    return
  }

  if (!wsUrl.value.startsWith('ws://') && !wsUrl.value.startsWith('wss://')) {
    alert('Invalid WebSocket URL. Must start with ws:// or wss://')
    return
  }

  if (!walletAddress.value) {
    alert('Please connect your Ethereum wallet first.')
    return
  }

  try {
    await wsClient.connect(wsUrl.value);
    logger.info('WebSocket connected:', wsUrl.value);
    logger.info('remoteUsers before join:', remoteUsers.value);
  } catch (e) {
    logger.error('Connect failed:', e);
    alert('Failed to connect: ' + (e instanceof Error ? e.message : String(e)));
    return;
  }

  const joinData = {
    roomId: roomId.value,
    userId: userId.value,
    userName: userName.value,
    audience: onlyAudience.value,
    roomToken: roomToken.value,
  }

  try {
    logger.info('Sending join request with data:', joinData)
    const response = await wsClient.sendRequest('join', joinData);
    logger.info('Join response users:', response.users);
    logger.info('remoteUsers:', remoteUsers.value);
    wsStatus.value = 'joined';
    
    // Parse users from response
    if (response.code === 0 && response.users && Array.isArray(response.users)) {
      for (const u of response.users) {
        upsertRemoteUser(u);
      }
      logger.info('Remote users:', remoteUsers.value);
    }
    // if pushers exist, call pullRemoteUser
    remoteUsers.value.forEach(user => {
      if (user.pushers.length > 0) {
        const specs: RecvTransceiverSpec[] = user.pushers.map(pusher => ({
          type: pusher.rtpParam.av_type as 'audio' | 'video',
          pusher_id: pusher.pusherId
        }))
        pullRemoteUser(roomId.value, user.userId, specs)
      }
    });
  } catch (e) {
    logger.error('Join request failed:', e)
    alert('Join failed: ' + (e instanceof Error ? e.message : String(e)))
  }
}

async function onOpenDevices() {
  // Guard in audience-only mode
  if (onlyAudience.value) {
    alert('Only audience mode enabled: local preview is disabled.')
    return
  }
  if (wsStatus.value !== 'joined') {
    alert('Please join a room first.')
    return;
  }
  //open camera and mic
  if (!sendPcWarp.value) {
    sendPcWarp.value = new PCWrap({ direction: 'sendonly' });
  }
  logger.info('Open devices clicked')
  //设置constraints?: MediaStreamConstraints，默认是{ audio: true, video: true }，且video为640x360
  const constraints: MediaStreamConstraints = {
    audio: true,
    video: {
      width: { ideal: 640 },
      height: { ideal: 360 }
    }
  };
  try {
    const medias = await sendPcWarp.value.openDevices(constraints);
    logger.info('Obtained media streams:', medias);
    if (videoEl.value) {
      videoEl.value.srcObject = medias
      try { await videoEl.value.play() } catch {}
    }
  } catch (e) {
    logger.error('Set constraints failed:', e);
    alert('Set constraints failed: ' + (e instanceof Error ? e.message : String(e)));
    return;
  }

  try {
    const offerSdp = await sendPcWarp.value.createOfferAndEmit();
    logger.info('Peer connection created, offer sdp type:', offerSdp.type, "sdp:", offerSdp.sdp);

    const pushData = {
      sdp: offerSdp,
      roomId: roomId.value,
      userId: userId.value,
    };
    const response = await wsClient.sendRequest('push', pushData);
    logger.info('Push response:', response);
    const code = response['code'];
    const resp_msg = response['message'];
    if (code !== 0) {
      alert('Push failed with code: ' + code + ', message: ' + resp_msg);
      return;
    }
    const answerSdpStr = response['sdp'];
    const answerSdp: RTCSessionDescriptionInit = {
      type: 'answer',
      sdp: answerSdpStr
    };
    await sendPcWarp.value.setRemoteDescription(answerSdp);
  } catch (e) {
    logger.error('Create peer connection failed:', e);
    alert('Create peer connection failed: ' + (e instanceof Error ? e.message : String(e)));
    return;
  }
}

async function pullRemoteUser(
  targetRoomId: string,
  targetUserId: string,
  specs: RecvTransceiverSpec[]
): Promise<void> {
  logger.info(`Pulling remote user ${targetUserId} with specs:`, specs)
  
  // Find the remote user
  const remoteUser = remoteUsers.value.find(u => u.userId === targetUserId)
  if (!remoteUser || !remoteUser.receivePcWrap) {
    throw new Error(`Remote user ${targetUserId} not found or has no receivePcWrap`)
  }

  try {
    // Create recvonly offer (clean version)
    const offer = await remoteUser.receivePcWrap.createOfferAndEmit()

    // Send pull request to server
    const pullData = {
      sdp: offer,
      roomId: targetRoomId,
      userId: userId.value,
      targetUserId: targetUserId,
      specs: specs
    }
    logger.info(`Sending pull request for user ${targetUserId}:`, JSON.stringify(pullData))
    const response = await wsClient.sendRequest('pull', pullData)
    const code = response['code']
    const resp_msg = response['message']
    if (code !== 0) {
      throw new Error(`Pull failed with code: ${code}, message: ${resp_msg}`)
    }

    // Set remote description from answer
    const answerSdpStr = response['sdp']
    const answerSdp: RTCSessionDescriptionInit = {
      type: 'answer',
      sdp: answerSdpStr
    }
    
    // Set remote description
    remoteUser.receivePcWrap.setRemoteDescription(answerSdp)
    logger.info(`Remote description set for user ${targetUserId}`)
    
    // Collect tracks as they arrive
    const expectedTrackCount = specs.length
    const tracks: MediaStreamTrack[] = []
    
    // Register single track listener that collects all tracks
    const trackPromise = new Promise<void>((resolve) => {
      const unsubscribe = remoteUser.receivePcWrap!.on('track', (event) => {
        logger.info(`[Track] Received ${event.track.kind} track from user ${targetUserId}:`, event.track)
        tracks.push(event.track)
        
        // Once we have all expected tracks, resolve and cleanup
        if (tracks.length >= expectedTrackCount) {
          unsubscribe()
          resolve()
        }
      })
    })
    
    // Wait for all tracks to arrive
    await trackPromise
    
    // Add all tracks to playStream
    for (const track of tracks) {
      logger.info("remote user:", remoteUser, "add new track:", track);
      // Respect current mute settings when adding incoming tracks
      if (track.kind === 'video' && remoteUser.videoMuted) {
        try { track.enabled = false } catch {}
      }
      if (track.kind === 'audio' && remoteUser.audioMuted) {
        try { track.enabled = false } catch {}
      }
      remoteUser.playStream!.addTrack(track);
    }
    // Bind playStream to video element if available
    if (remoteUser.videoRef && remoteUser.playStream) {
      remoteUser.videoRef.srcObject = remoteUser.playStream
      remoteUser.videoRef.playsInline = true
      remoteUser.videoRef.muted = false
      logger.info(`[VideoBind] Bound playStream (${remoteUser.playStream.getTracks().length} tracks) to user ${targetUserId}`)
    }
  } catch (e) {
    logger.error(`Pull remote user ${targetUserId} failed:`, e)
    throw e
  }
}
</script>

<template>
  <main class="container">
    <h1 class="title">WebRTC Demo</h1>

    <div class="status-bar">
      <span class="status-label">Status:</span>
      <span 
        class="status-badge"
        :class="{
          'status-disconnected': wsStatus === 'disconnected',
          'status-connected': wsStatus === 'connected',
          'status-joined': wsStatus === 'joined'
        }"
      >
        {{ wsStatus === 'disconnected' ? 'Disconnected' : wsStatus === 'connected' ? 'Connected' : 'Joined' }}
      </span>
    </div>

    <div class="wallet-bar">
      <span v-if="!isWalletInstalled" class="wallet-badge wallet-disconnected">No Wallet</span>
      <span v-else-if="walletAddress" class="wallet-badge wallet-connected">{{ truncatedAddress }}</span>

      <button
        v-if="loginStatus !== 'logged_in'"
        type="button"
        class="btn wallet-btn btn-wallet-connect"
        :disabled="loginStatus === 'logging_in' || !isWalletInstalled"
        @click="login()"
      >
        <span v-if="loginStatus === 'logging_in'">Logging in...</span>
        <span v-else>Login</span>
      </button>

      <span v-if="loginStatus === 'logged_in'" class="wallet-badge wallet-connected">Logged in</span>

      <button
        v-if="walletAddress"
        type="button"
        class="btn btn-buy-mtk"
        @click="openBuyModal()"
      >Buy MTK</button>

      <span v-if="connectionError || loginError" class="wallet-error">{{ connectionError || loginError }}</span>
    </div>

    <!-- Buy MTK Modal -->
    <div v-if="showBuyModal" class="modal-overlay" @click.self="closeBuyModal()">
      <div class="modal-card">
        <div class="modal-header">
          <span class="modal-title">Buy MeetingToken (MTK)</span>
          <button class="modal-close-btn" @click="closeBuyModal()">✕</button>
        </div>

        <div class="modal-body">
          <!-- Input / Quoting / Error -->
          <template v-if="buyStep === 'input' || buyStep === 'quoting'">
            <label class="label" for="buy-amount">Token amount</label>
            <input
              id="buy-amount"
              v-model="buyAmount"
              class="input"
              type="text"
              placeholder="e.g. 100"
              autocomplete="off"
              spellcheck="false"
              :disabled="buyStep === 'quoting'"
            />

            <button
              type="button"
              class="btn btn-buy-confirm"
              :disabled="buyStep === 'quoting' || !buyAmount.trim()"
              @click="requestBuy()"
            >
              <span v-if="buyStep === 'quoting'">Getting quote...</span>
              <span v-else>Buy</span>
            </button>

            <div v-if="buyQuoteError" class="quote-row quote-error">{{ buyQuoteError }}</div>
          </template>

          <!-- Confirm -->
          <template v-else-if="buyStep === 'confirm'">
            <div class="confirm-text">
              Buy <strong>{{ buyAmount }} MTK</strong> for
              <strong>~{{ buyEthQuote }} ETH</strong>?
            </div>
            <div class="confirm-actions">
              <button type="button" class="btn btn-buy-confirm" @click="confirmBuy()">Confirm</button>
              <button type="button" class="btn btn-cancel" @click="cancelConfirm()">Cancel</button>
            </div>
          </template>

          <!-- Buying -->
          <template v-else-if="buyStep === 'buying'">
            <div class="quote-row">Sending transaction...</div>
          </template>

          <!-- Done -->
          <template v-else-if="buyStep === 'done'">
            <div class="quote-row quote-ok">Transaction sent!</div>
            <div class="quote-row">
              Tx: <a :href="'https://sepolia.etherscan.io/tx/' + buyTxHash" target="_blank">{{ buyTxHash.slice(0, 10) + '...' + buyTxHash.slice(-8) }}</a>
            </div>
            <button type="button" class="btn btn-buy-confirm" @click="closeBuyModal()">Close</button>
          </template>

          <div v-if="buyError" class="quote-row quote-error">{{ buyError }}</div>
        </div>
      </div>
    </div>

    <div class="info-bar">
      <div class="info-item">
        <span class="info-label">Wallet:</span>
        <span class="info-value">{{ userId || 'Not connected' }}</span>
      </div>
      <div class="info-item">
        <span class="info-label">Display:</span>
        <span class="info-value">{{ userName || '-' }}</span>
      </div>
    </div>

    <!-- Token balance (shared) -->
    <div v-if="loginStatus === 'logged_in'" class="card token-balance-card">
      <div class="token-balance-row">
        <span class="token-balance-label">MTK Balance:</span>
        <span class="token-balance-value">{{ tokenBalance || '...' }}</span>
        <button type="button" class="btn-refresh" @click="fetchTokenBalance()" :disabled="tokenBalanceLoading">↻</button>
      </div>
    </div>

    <!-- Tab bar -->
    <div class="tab-bar">
      <button
        type="button"
        class="tab-btn"
        :class="{ 'tab-active': activeTab === 'create' }"
        @click="activeTab = 'create'"
      >Create Room</button>
      <button
        type="button"
        class="tab-btn"
        :class="{ 'tab-active': activeTab === 'attend' }"
        @click="activeTab = 'attend'"
      >Attend Room</button>
    </div>

    <!-- Create Room tab -->
    <form v-if="activeTab === 'create'" class="card" @submit.prevent>
      <label class="label" for="meeting-name">Meeting Name</label>
      <input
        id="meeting-name"
        v-model="meetingName"
        class="input"
        type="text"
        placeholder="e.g. Team Standup"
        autocomplete="off"
        spellcheck="false"
      />

      <label class="label" for="meeting-fee">Fee per Attendee (MTK)</label>
      <input
        id="meeting-fee"
        v-model="meetingFeePerAttendee"
        class="input"
        type="text"
        placeholder="0.01"
        autocomplete="off"
        spellcheck="false"
      />

      <div class="actions">
        <button
          type="button"
          class="btn"
          :disabled="!walletAddress || loginStatus !== 'logged_in' || createRoomStatus === 'pending'"
          @click="createRoom()"
        >
          <span v-if="meetingStep === 'approving'">Approving MTK...</span>
          <span v-else-if="meetingStep === 'creating_meeting'">Creating Meeting...</span>
          <span v-else-if="meetingStep === 'creating_room'">Creating Room...</span>
          <span v-else>Create Room</span>
        </button>
      </div>
      <span v-if="createRoomStatus === 'done'" class="wallet-badge wallet-connected">Room created</span>
      <span v-else-if="createRoomStatus === 'pending'" class="wallet-badge" style="background:rgba(234,179,8,0.2);color:#eab308;">Creating...</span>
      <span v-if="createRoomError || meetingError" class="wallet-error">{{ createRoomError || meetingError }}</span>
    </form>

    <!-- Attend Room tab -->
    <form v-if="activeTab === 'attend'" class="card" @submit.prevent>
      <label class="label" for="attend-ws-url">WebSocket URL (shared link)</label>
      <input
        id="attend-ws-url"
        v-model="attendWsUrl"
        class="input"
        type="text"
        placeholder="ws://192.168.1.221:7443/webrtc?roomid=xxxx&meetingId=123"
        autocomplete="off"
        spellcheck="false"
      />

      <div class="actions">
        <button
          type="button"
          class="btn"
          :disabled="!walletAddress || loginStatus !== 'logged_in' || attendStatus === 'pending' || !attendWsUrl.trim()"
          @click="attendRoom()"
        >
          <span v-if="attendStep === 'approving'">Approving MTK...</span>
          <span v-else-if="attendStep === 'joining_meeting'">Joining Meeting...</span>
          <span v-else-if="attendStep === 'joining_room'">Joining Room...</span>
          <span v-else>Join Meeting</span>
        </button>
      </div>
      <span v-if="attendStatus === 'done'" class="wallet-badge wallet-connected">Room joined</span>
      <span v-else-if="attendStatus === 'pending'" class="wallet-badge" style="background:rgba(234,179,8,0.2);color:#eab308;">Joining...</span>
      <span v-if="attendError" class="wallet-error">{{ attendError }}</span>
    </form>

    <!-- WebSocket URL + Join/Open (shared) -->
    <div class="card">
      <label class="label" for="ws-url">WebSocket URL</label>
      <div class="url-copy-row">
        <input
          id="ws-url"
          :value="wsUrl"
          class="input url-input"
          type="text"
          readonly
          placeholder="Create or join a room first"
          autocomplete="off"
          spellcheck="false"
        />
        <button type="button" class="btn btn-copy" @click="copyWsUrl()" :disabled="!wsUrl">Copy</button>
      </div>

      <label class="checkbox-row" for="only-audience">
        <input id="only-audience" type="checkbox" v-model="onlyAudience" />
        <span>only audience</span>
      </label>

      <div class="actions">
        <button type="button" class="btn" :disabled="!wsUrl || !roomId" @click="onJoin">Join</button>
        <button
          type="button"
          class="btn btn-secondary"
          @click="onOpenDevices"
          :disabled="wsStatus !== 'joined' || onlyAudience"
          title="Open local devices (requires joined status)"
        >Open</button>
      </div>

      <div class="status-bar">
        <span class="status-label">Status:</span>
        <span
          class="status-badge"
          :class="{
            'status-disconnected': wsStatus === 'disconnected',
            'status-connected': wsStatus === 'connected',
            'status-joined': wsStatus === 'joined'
          }"
        >
          {{ wsStatus === 'disconnected' ? 'Disconnected' : wsStatus === 'connected' ? 'Connected' : 'Joined' }}
        </span>
      </div>
    </div>
    <section v-if="!onlyAudience" class="card">
      <label class="label" for="local-video">Local Preview</label>
      <video
        id="local-video"
        ref="videoEl"
        class="video"
        autoplay
        playsinline
        muted
      ></video>
    </section>

    <!-- Floating Chat Panel (WeChat Style) -->
    <div v-if="showChat" class="chat-floating-container">
      <div class="chat-panel-header">
        <span class="chat-panel-title">Messages</span>
        <button
          type="button"
          class="chat-close-btn"
          @click="showChat = false"
          title="Close chat"
        >✕</button>
      </div>
      <div class="chat-list" ref="chatListEl">
        <div
          v-for="(m, idx) in chatMessages"
          :key="idx"
          class="chat-message-group"
          :class="{ 'sent': m.self, 'received': !m.self }"
        >
          <div class="chat-bubble-wrapper">
            <!-- Sender name label: wide rectangle tag showing full userName -->
            <div class="chat-avatar" :title="m.self ? 'Me' : m.userName">{{ m.self ? 'Me' : m.userName }}</div>
            
            <!-- Message bubble -->
            <div class="chat-bubble">{{ m.message }}</div>
          </div>
          
          <!-- Timestamp -->
          <div class="chat-time">{{ new Date(m.time || Date.now()).toLocaleTimeString() }}</div>
        </div>
      </div>
      
      <!-- Chat input area -->
      <div class="chat-input-area">
        <input
          id="chat-input"
          v-model="chatInput"
          class="input chat-input"
          type="text"
          placeholder="Enter message and press Enter"
          autocomplete="off"
          spellcheck="false"
          :disabled="wsStatus !== 'joined'"
          @keyup.enter="onChatEnter"
        />
        <button
          type="button"
          class="btn btn-send"
          @click="onChatEnter"
          :disabled="wsStatus !== 'joined' || !chatInput.trim()"
        >Send</button>
      </div>
    </div>

    <!-- Chat toggle button (when chat is hidden) -->
    <button
      v-if="!showChat"
      type="button"
      class="chat-toggle-btn"
      @click="showChat = true"
      title="Open chat"
    >💬 {{ chatMessages.length }}</button>

    <section v-if="remoteUsers.length > 0" class="card">
      <label class="label">Remote Users ({{ remoteUsers.length }})</label>
      <div class="remote-users-grid">
        <div v-for="user in remoteUsers" :key="user.userId" class="remote-user-card" :class="{ 'user-disconnected': user.state === 'disconnected' }">
          <div class="remote-user-info">
            <span class="remote-user-name">{{ user.userName }}</span>
            <span class="remote-user-id">ID: {{ user.userId }}</span>
            <span v-if="user.state === 'disconnected'" class="user-state-badge disconnected">Disconnected</span>
          </div>
          <video
            :ref="el => { 
              if (el) {
                const videoEl = el as HTMLVideoElement
                user.videoRef = videoEl
                // If playStream already has tracks, bind it now
                if (user.playStream && user.playStream.getTracks().length > 0 && !videoEl.srcObject) {
                  videoEl.srcObject = user.playStream
                  videoEl.playsInline = true
                  videoEl.muted = false
                  logger.info(`[VideoRef] Bound existing playStream (${user.playStream.getTracks().length} tracks) to user ${user.userId}`)
                  videoEl.play().catch(err => logger.warn(`[VideoRef] Autoplay failed for user ${user.userId}:`, err.message))
                }
              }
            }"
            class="video"
            autoplay
            playsinline
          ></video>
          <div class="pusher-info">
            <span v-for="pusher in user.pushers" :key="pusher.pusherId" class="pusher-badge">
              {{ pusher.rtpParam.av_type.toUpperCase() }}
            </span>
          </div>
          <div class="mute-controls">
            <button
              type="button"
              class="btn mute-btn"
              :class="{ muted: user.audioMuted }"
              @click="toggleAudioMute(user)"
            >
              {{ user.audioMuted ? 'Unmute Audio' : 'Mute Audio' }}
            </button>
            <button
              type="button"
              class="btn mute-btn"
              :class="{ muted: user.videoMuted }"
              @click="toggleVideoMute(user)"
            >
              {{ user.videoMuted ? 'Unmute Video' : 'Mute Video' }}
            </button>
          </div>
        </div>
      </div>
    </section>
  </main>
</template>

<style scoped>
.container {
  margin: 0 auto;
  padding: 2rem 1rem;
  max-width: 720px;
}
.title {
  margin: 0 0 1rem;
  font-size: 1.75rem;
  line-height: 1.2;
}
/* Tab bar */
.tab-bar {
  display: flex;
  gap: 0;
  margin-bottom: 0;
}
.tab-btn {
  flex: 1;
  padding: 0.65rem 1rem;
  border: 1px solid rgba(128, 128, 128, 0.3);
  background: rgba(255, 255, 255, 0.03);
  color: rgba(255, 255, 255, 0.6);
  font-size: 0.95rem;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.2s, color 0.2s;
}
.tab-btn:first-child {
  border-radius: 10px 0 0 0;
}
.tab-btn:last-child {
  border-radius: 0 10px 0 0;
}
.tab-btn:hover {
  background: rgba(255, 255, 255, 0.06);
}
.tab-active {
  background: rgba(100, 108, 255, 0.15);
  color: #646cff;
  border-bottom-color: transparent;
}
.token-balance-card {
  border-radius: 12px 12px 0 0;
  border-bottom: none;
  padding-bottom: 0.5rem;
}
@media (prefers-color-scheme: light) {
  .tab-btn {
    background: rgba(0, 0, 0, 0.03);
    color: rgba(0, 0, 0, 0.5);
    border-color: rgba(0, 0, 0, 0.12);
  }
  .tab-btn:hover {
    background: rgba(0, 0, 0, 0.06);
  }
  .tab-active {
    background: rgba(59, 92, 255, 0.1);
    color: #3b5cff;
  }
}
.status-bar {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-bottom: 1rem;
  padding: 0.75rem;
  background: rgba(255, 255, 255, 0.02);
  border-radius: 8px;
}
.status-label {
  font-size: 0.9rem;
  font-weight: 500;
}
.status-badge {
  padding: 0.25rem 0.75rem;
  border-radius: 12px;
  font-size: 0.85rem;
  font-weight: 600;
}
.status-disconnected {
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
}
.status-connected {
  background: rgba(234, 179, 8, 0.2);
  color: #eab308;
}
.status-joined {
  background: rgba(34, 197, 94, 0.2);
  color: #22c55e;
}
.info-bar {
  display: flex;
  gap: 1rem;
  margin-bottom: 1rem;
  padding: 0.75rem;
  background: rgba(255, 255, 255, 0.02);
  border-radius: 8px;
  flex-wrap: wrap;
}
.info-item {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}
.info-label {
  font-size: 0.85rem;
  color: rgba(255, 255, 255, 0.6);
}
.info-value {
  font-size: 0.9rem;
  font-weight: 600;
  color: #646cff;
}
.card {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid rgba(60, 60, 60, 0.3);
  border-radius: 12px;
  padding: 1rem;
}
.label {
  font-size: 0.95rem;
}
.input {
  width: 100%;
  padding: 0.6rem 0.75rem;
  border-radius: 8px;
  border: 1px solid rgba(128, 128, 128, 0.35);
  background: transparent;
  color: inherit;
  box-sizing: border-box; /* ensure padding/border don't overflow parent */
}
.input-dynamic {
  width: auto;
  max-width: 100%;
  min-width: 200px;
}
.input:focus {
  outline: none;
  border-color: #646cff;
  box-shadow: 0 0 0 3px rgba(100, 108, 255, 0.15);
}
.actions {
  display: flex;
  gap: 0.5rem;
  margin-top: 0.25rem;
}
.btn {
  appearance: none;
  border: 1px solid transparent;
  padding: 0.55rem 1rem;
  border-radius: 10px;
  background: #646cff;
  color: white;
  font-weight: 600;
  cursor: pointer;
}
.btn:hover {
  filter: brightness(1.05);
}
.btn:active {
  transform: translateY(1px);
}
.btn-secondary {
  background: #2f855a;
}
/* Disabled button state */
.btn:disabled, .btn-secondary:disabled {
  opacity: 0.45;
  cursor: not-allowed;
  filter: none;
}
.btn:disabled:hover, .btn-secondary:disabled:hover {
  filter: none;
}
@media (prefers-color-scheme: light) {
  .card { border-color: rgba(0,0,0,0.1); }
  .btn { background: #3b5cff; }
  .btn-secondary { background: #2b6cb0; }
  .info-label { color: rgba(0, 0, 0, 0.5); }
  .info-value { color: #3b5cff; }
}

.video {
  width: 100%;
  max-height: 60vh;
  background: #111;
  border-radius: 8px;
}
/* Inline checkbox row */
.checkbox-row {
  display: inline-flex;
  align-items: center;
  gap: 0.4rem;
  font-size: 0.9rem;
}

/* Chat styles (WeChat-inspired) */
.chat-panel {
  gap: 0.75rem;
  display: flex;
  flex-direction: column;
  height: auto;
  min-height: 400px;
}
.chat-list {
  flex: 1;
  min-height: 300px;
  max-height: 400px;
  overflow-y: auto;
  border: 1px solid rgba(128, 128, 128, 0.2);
  border-radius: 8px;
  padding: 1rem;
  background: rgba(255, 255, 255, 0.01);
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}
.chat-message-group {
  display: flex;
  flex-direction: column;
  gap: 0.2rem;
}
.chat-message-group.received {
  align-items: flex-start;
}
.chat-message-group.sent {
  align-items: flex-end;
}
.chat-sender-name {
  font-size: 0.8rem;
  font-weight: 600;
  color: rgba(255, 255, 255, 0.9);
  margin-bottom: 0.3rem;
  margin-left: 0.5rem;
}
.chat-bubble-wrapper {
  display: flex;
  align-items: flex-end;
  gap: 0.5rem;
}
.chat-message-group.sent .chat-bubble-wrapper {
  flex-direction: row-reverse;
}
.chat-avatar {
  min-width: 60px;
  max-width: 150px;
  height: 24px;
  padding: 0 10px;
  border-radius: 12px;
  background: linear-gradient(135deg, #646cff, #8b93ff);
  color: white;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.75rem;
  font-weight: 600;
  flex-shrink: 0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.chat-message-group.sent .chat-avatar {
  background: linear-gradient(135deg, #16a34a, #22c55e);
}
.chat-bubble {
  max-width: 60%;
  padding: 0.6rem 0.85rem;
  border-radius: 12px;
  word-wrap: break-word;
  background: rgba(100, 108, 255, 0.15);
  color: inherit;
  font-size: 0.9rem;
  line-height: 1.4;
  text-align: left;
}
.chat-message-group.sent .chat-bubble {
  background: #16a34a;
  color: white;
}
.chat-time {
  font-size: 0.7rem;
  color: rgba(255, 255, 255, 0.4);
  margin-top: 0.2rem;
}
.chat-input-area {
  display: flex;
  gap: 0.5rem;
  align-items: center;
}
.chat-input {
  flex: 1;
  padding: 0.6rem 0.75rem;
}
.btn-send {
  padding: 0.55rem 1.2rem;
  min-width: 70px;
}

/* URL copy row */
.url-copy-row {
  display: flex;
  gap: 0.5rem;
  align-items: center;
}
.url-input {
  flex: 1;
}
.btn-copy {
  padding: 0.55rem 0.9rem;
  min-width: 60px;
  white-space: nowrap;
}

/* Floating chat panel styles */
.chat-floating-container {
  position: fixed;
  bottom: 20px;
  right: 20px;
  width: 350px;
  height: 500px;
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid rgba(60, 60, 60, 0.3);
  border-radius: 12px;
  display: flex;
  flex-direction: column;
  z-index: 9999;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
  animation: slideUp 0.3s ease-out;
}

@keyframes slideUp {
  from {
    transform: translateY(20px);
    opacity: 0;
  }
  to {
    transform: translateY(0);
    opacity: 1;
  }
}

.chat-panel-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.75rem 1rem;
  border-bottom: 1px solid rgba(128, 128, 128, 0.2);
  background: rgba(100, 108, 255, 0.1);
  border-radius: 12px 12px 0 0;
}

.chat-panel-title {
  font-weight: 600;
  font-size: 0.95rem;
}

.chat-close-btn {
  background: none;
  border: none;
  color: rgba(255, 255, 255, 0.7);
  font-size: 1.2rem;
  cursor: pointer;
  padding: 0.2rem 0.4rem;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: color 0.2s;
}

.chat-close-btn:hover {
  color: rgba(255, 255, 255, 1);
}

.chat-floating-container .chat-list {
  flex: 1;
  max-height: none;
  min-height: auto;
}

.chat-toggle-btn {
  position: fixed;
  bottom: 20px;
  right: 20px;
  width: 60px;
  height: 60px;
  border-radius: 50%;
  background: #646cff;
  color: white;
  border: none;
  font-size: 1.2rem;
  font-weight: 600;
  cursor: pointer;
  z-index: 9998;
  box-shadow: 0 4px 15px rgba(100, 108, 255, 0.4);
  transition: transform 0.2s, box-shadow 0.2s;
  display: flex;
  align-items: center;
  justify-content: center;
}

.chat-toggle-btn:hover {
  transform: scale(1.1);
  box-shadow: 0 6px 20px rgba(100, 108, 255, 0.6);
}

.chat-toggle-btn:active {
  transform: scale(0.95);
}

.remote-users-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 1rem;
  margin-top: 0.5rem;
}
.remote-user-card {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  padding: 0.75rem;
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid rgba(128, 128, 128, 0.2);
  border-radius: 8px;
}
.remote-user-card.user-disconnected {
  opacity: 0.6;
  background: rgba(255, 255, 255, 0.01);
  border-color: rgba(239, 68, 68, 0.3);
}
.remote-user-card .video {
  max-height: 200px;
}
.remote-user-info {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}
.remote-user-name {
  font-weight: 600;
  font-size: 0.95rem;
}
.remote-user-id {
  font-size: 0.75rem;
  color: rgba(255, 255, 255, 0.5);
}
.user-state-badge {
  display: inline-block;
  width: fit-content;
  padding: 0.2rem 0.5rem;
  border-radius: 4px;
  font-size: 0.7rem;
  font-weight: 600;
  text-transform: uppercase;
}
.user-state-badge.disconnected {
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
}
.pusher-info {
  display: flex;
  gap: 0.5rem;
  flex-wrap: wrap;
}
.pusher-badge {
  padding: 0.2rem 0.5rem;
  background: rgba(100, 108, 255, 0.2);
  color: #646cff;
  border-radius: 4px;
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
}

.mute-controls {
  display: flex;
  gap: 0.5rem;
  margin-top: 0.25rem;
}
.mute-btn {
  padding: 0.35rem 0.75rem;
  border-radius: 8px;
  font-weight: 700;
  font-size: 0.85rem;
}
.mute-btn.muted {
  /* muted = red */
  background: #ef4444 !important;
  color: white !important;
  border-color: rgba(239,68,68,0.8) !important;
}
.mute-btn:not(.muted) {
  /* unmuted = green */
  background: #16a34a !important;
  color: white !important;
  border-color: rgba(22,163,74,0.8) !important;
}

/* Buy MTK button */
.btn-buy-mtk {
  background: #22c55e !important;
  padding: 0.35rem 0.9rem;
  font-size: 0.85rem;
}

/* Token balance */
.token-balance-row {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 0;
}
.token-balance-label {
  font-size: 0.9rem;
  color: rgba(255, 255, 255, 0.6);
}
.token-balance-value {
  font-size: 1rem;
  font-weight: 700;
  color: #22c55e;
  font-family: monospace;
}
.btn-refresh {
  background: none;
  border: 1px solid rgba(128, 128, 128, 0.3);
  border-radius: 6px;
  color: rgba(255, 255, 255, 0.7);
  cursor: pointer;
  font-size: 0.9rem;
  padding: 0.15rem 0.5rem;
}
.btn-refresh:hover {
  background: rgba(255, 255, 255, 0.1);
}
.btn-refresh:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

/* Modal */
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10000;
}
.modal-card {
  background: #1e1e1e;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 14px;
  width: 400px;
  max-width: 90vw;
  padding: 0;
}
.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1rem 1.25rem;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
}
.modal-title {
  font-weight: 600;
  font-size: 1.05rem;
}
.modal-close-btn {
  background: none;
  border: none;
  color: rgba(255, 255, 255, 0.5);
  font-size: 1.2rem;
  cursor: pointer;
  padding: 0.2rem;
}
.modal-close-btn:hover {
  color: rgba(255, 255, 255, 0.9);
}
.modal-body {
  padding: 1.25rem;
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}
.quote-row {
  font-size: 0.9rem;
  padding: 0.4rem 0;
}
.quote-ok {
  color: #22c55e;
}
.quote-error {
  color: #ef4444;
}
.btn-buy-confirm {
  background: #22c55e !important;
  width: 100%;
}
.btn-buy-confirm:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
.confirm-text {
  text-align: center;
  font-size: 1rem;
  padding: 0.5rem 0;
  line-height: 1.6;
}
.confirm-actions {
  display: flex;
  gap: 0.5rem;
}
.confirm-actions .btn {
  flex: 1;
}
.btn-cancel {
  background: #555 !important;
}

@media (prefers-color-scheme: light) {
  .modal-card {
    background: #ffffff;
    border-color: rgba(0, 0, 0, 0.1);
  }
  .modal-close-btn { color: rgba(0, 0, 0, 0.4); }
  .modal-close-btn:hover { color: rgba(0, 0, 0, 0.8); }
}

/* Wallet bar */
.wallet-bar {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-bottom: 1rem;
  padding: 0.75rem;
  background: rgba(255, 255, 255, 0.02);
  border-radius: 8px;
  flex-wrap: wrap;
}
.wallet-badge {
  padding: 0.25rem 0.75rem;
  border-radius: 12px;
  font-size: 0.85rem;
  font-weight: 600;
}
.wallet-connected {
  background: rgba(34, 197, 94, 0.2);
  color: #22c55e;
  font-family: monospace;
}
.wallet-disconnected {
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
}
.wallet-error {
  font-size: 0.8rem;
  color: #ef4444;
  width: 100%;
}
.wallet-btn {
  margin-left: auto;
}
.btn-wallet-connect {
  background: #f7931a !important;
}
.btn-wallet-disconnect {
  background: #555 !important;
}
.wallet-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}

@media (prefers-color-scheme: light) {
  .remote-user-id { color: rgba(0, 0, 0, 0.5); }
  .pusher-badge { background: rgba(59, 92, 255, 0.15); color: #3b5cff; }
  .btn-wallet-disconnect { background: #888 !important; }
}
</style>
