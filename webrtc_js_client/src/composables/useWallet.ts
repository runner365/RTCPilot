import { ref, computed, onMounted, onUnmounted } from 'vue'
import { logger } from '../logger'

// ---- EIP-1193 minimal type declarations ----
interface RequestArguments {
  readonly method: string
  readonly params?: readonly unknown[]
}

interface EIP1193Provider {
  request(args: RequestArguments): Promise<unknown>
  on(event: string, handler: (...args: any[]) => void): void
  removeListener(event: string, handler: (...args: any[]) => void): void
}

declare global {
  interface Window {
    ethereum?: EIP1193Provider
    phantom?: { ethereum?: EIP1193Provider }
  }
}

export function useWallet() {
  const walletAddress = ref<string>('')
  const isConnecting = ref(false)
  const connectionError = ref<string>('')
  const isWalletInstalled = ref(!!(window.ethereum || window.phantom?.ethereum))

  const truncatedAddress = computed(() => {
    const addr = walletAddress.value
    if (!addr) return ''
    return addr.length >= 10
      ? `${addr.slice(0, 6)}...${addr.slice(-4)}`
      : addr
  })

  const provider = computed(() => window.phantom?.ethereum ?? window.ethereum ?? null)

  let _accountsChangedHandler: ((accounts: string[]) => void) | null = null
  let _chainChangedHandler: ((chainId: string) => void) | null = null
  let _disconnectHandler: ((error: unknown) => void) | null = null

  function setupListeners() {
    const p = provider.value
    if (!p) return

    _accountsChangedHandler = (accounts: string[]) => {
      if (accounts.length === 0) {
        walletAddress.value = ''
      } else {
        walletAddress.value = accounts[0].toLowerCase()
      }
    }

    _chainChangedHandler = (_chainId: string) => {
      logger.info('Ethereum chain changed:', _chainId)
    }

    _disconnectHandler = (_error: unknown) => {
      logger.info('Provider disconnect:', _error)
      walletAddress.value = ''
      connectionError.value = 'Wallet disconnected'
    }

    p.on('accountsChanged', _accountsChangedHandler)
    p.on('chainChanged', _chainChangedHandler)
    p.on('disconnect', _disconnectHandler)
  }

  function removeListeners() {
    const p = provider.value
    if (!p) return
    if (_accountsChangedHandler) {
      p.removeListener('accountsChanged', _accountsChangedHandler)
      _accountsChangedHandler = null
    }
    if (_chainChangedHandler) {
      p.removeListener('chainChanged', _chainChangedHandler)
      _chainChangedHandler = null
    }
    if (_disconnectHandler) {
      p.removeListener('disconnect', _disconnectHandler)
      _disconnectHandler = null
    }
  }

  async function connectWallet(): Promise<string> {
    const p = provider.value
    if (!p) {
      throw new Error('Phantom wallet not detected. Please install Phantom and enable Ethereum support.')
    }

    isConnecting.value = true
    connectionError.value = ''

    try {
      const accounts = (await p.request({
        method: 'eth_requestAccounts',
      })) as string[]

      if (!Array.isArray(accounts) || accounts.length === 0) {
        throw new Error('No accounts returned from wallet')
      }

      walletAddress.value = accounts[0].toLowerCase()
      logger.info('Wallet connected:', walletAddress.value)
      return walletAddress.value
    } catch (e: any) {
      const msg =
        e?.code === 4001
          ? 'Connection rejected. Please approve the wallet connection.'
          : e?.message || 'Failed to connect wallet'
      connectionError.value = msg
      throw e
    } finally {
      isConnecting.value = false
    }
  }

  function disconnectWallet(): void {
    walletAddress.value = ''
    connectionError.value = ''
  }

  async function checkPriorAuthorization(): Promise<void> {
    const p = provider.value
    if (!p) return
    try {
      const accounts = (await p.request({
        method: 'eth_accounts',
      })) as string[]
      if (Array.isArray(accounts) && accounts.length > 0) {
        walletAddress.value = accounts[0].toLowerCase()
      }
    } catch {
      // silently fail — user must click Connect
    }
  }

  onMounted(() => {
    checkPriorAuthorization()
    setupListeners()
  })

  onUnmounted(() => {
    removeListeners()
  })

  return {
    walletAddress,
    isConnecting,
    connectionError,
    isWalletInstalled,
    truncatedAddress,
    provider,
    connectWallet,
    disconnectWallet,
  }
}
