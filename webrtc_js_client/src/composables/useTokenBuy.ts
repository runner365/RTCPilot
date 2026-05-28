import { ref, computed } from 'vue'
import {
  createPublicClient,
  http,
  formatEther,
  formatUnits,
  parseUnits,
  encodeFunctionData,
  type Address,
} from 'viem'
import { sepolia } from 'viem/chains'

// ---- Minimal ABIs ----

const uniswapLiquiditySetupAbi = [
  {
    inputs: [
      { name: 'tokenA', type: 'address' },
      { name: 'amountOutMin', type: 'uint256' },
    ],
    name: 'swapExactETHForTokens',
    outputs: [],
    stateMutability: 'payable',
    type: 'function',
  },
  {
    inputs: [],
    name: 'WETH',
    outputs: [{ name: '', type: 'address' }],
    stateMutability: 'view',
    type: 'function',
  },
  {
    inputs: [],
    name: 'router',
    outputs: [{ name: '', type: 'address' }],
    stateMutability: 'view',
    type: 'function',
  },
] as const

const uniswapV2RouterAbi = [
  {
    inputs: [
      { name: 'amountOut', type: 'uint256' },
      { name: 'path', type: 'address[]' },
    ],
    name: 'getAmountsIn',
    outputs: [{ name: 'amounts', type: 'uint256[]' }],
    stateMutability: 'view',
    type: 'function',
  },
  {
    inputs: [],
    name: 'factory',
    outputs: [{ name: '', type: 'address' }],
    stateMutability: 'view',
    type: 'function',
  },
] as const

const uniswapV2FactoryAbi = [
  {
    inputs: [
      { name: 'tokenA', type: 'address' },
      { name: 'tokenB', type: 'address' },
    ],
    name: 'getPair',
    outputs: [{ name: 'pair', type: 'address' }],
    stateMutability: 'view',
    type: 'function',
  },
] as const

const uniswapV2PairAbi = [
  {
    inputs: [],
    name: 'getReserves',
    outputs: [
      { name: 'reserve0', type: 'uint112' },
      { name: 'reserve1', type: 'uint112' },
      { name: 'blockTimestampLast', type: 'uint32' },
    ],
    stateMutability: 'view',
    type: 'function',
  },
] as const

const erc20Abi = [
  {
    inputs: [],
    name: 'decimals',
    outputs: [{ name: '', type: 'uint8' }],
    stateMutability: 'view',
    type: 'function',
  },
] as const

const SEPOLIA_RPC = 'https://ethereum-sepolia.publicnode.com'

export interface TokenBuyContracts {
  meeting_token: string
  uniswap_liquidity_setup: string
}

function encodeSwap(tokenAddr: Address, minOut: bigint): `0x${string}` {
  return encodeFunctionData({
    abi: uniswapLiquiditySetupAbi,
    functionName: 'swapExactETHForTokens',
    args: [tokenAddr, minOut],
  })
}

export type BuyStep = 'input' | 'quoting' | 'confirm' | 'buying' | 'done'

export function useTokenBuy(
  contracts: () => TokenBuyContracts | null,
  getProvider: () => unknown,
  getWalletAddress: () => string,
) {
  const showModal = ref(false)
  const tokenAmountInput = ref('')
  const step = ref<BuyStep>('input')
  const ethRequired = ref(0n)
  const quoteError = ref('')
  const buyError = ref('')
  const buyTxHash = ref('')
  const tokenDecimals = ref(18)

  let _weth: Address | null = null
  let _routerAddr: Address | null = null
  let publicClient: ReturnType<typeof createPublicClient> | null = null

  function getPublicClient() {
    if (!publicClient) {
      publicClient = createPublicClient({
        chain: sepolia,
        transport: http(SEPOLIA_RPC),
      })
    }
    return publicClient
  }

  async function loadChainConstants(setupAddr: Address) {
    if (_weth && _routerAddr) return
    const pc = getPublicClient()
    _weth = (await pc.readContract({
      address: setupAddr,
      abi: uniswapLiquiditySetupAbi,
      functionName: 'WETH',
    })) as Address
    _routerAddr = (await pc.readContract({
      address: setupAddr,
      abi: uniswapLiquiditySetupAbi,
      functionName: 'router',
    })) as Address
  }

  const ethRequiredFormatted = computed(() => {
    if (ethRequired.value === 0n) return '0'
    return Number(formatEther(ethRequired.value)).toFixed(6)
  })

  function openBuyModal() {
    showModal.value = true
    tokenAmountInput.value = ''
    step.value = 'input'
    ethRequired.value = 0n
    quoteError.value = ''
    buyError.value = ''
    buyTxHash.value = ''
  }

  function closeModal() {
    showModal.value = false
  }

  /** Step 1: user clicks "Buy" — fetch quote, then show confirmation */
  async function requestBuy() {
    const c = contracts()
    if (!c) {
      quoteError.value = 'Contract addresses not loaded yet'
      return
    }

    const val = tokenAmountInput.value.trim()
    if (!val || isNaN(Number(val)) || Number(val) <= 0) {
      quoteError.value = 'Please enter a valid token amount'
      return
    }

    step.value = 'quoting'
    quoteError.value = ''

    try {
      const tokenAddr = c.meeting_token as Address
      const setupAddr = c.uniswap_liquidity_setup as Address

      if (tokenDecimals.value === 18) {
        try {
          tokenDecimals.value = (await getPublicClient().readContract({
            address: tokenAddr,
            abi: erc20Abi,
            functionName: 'decimals',
          })) as number
        } catch { /* keep default */ }
      }

      const amountOutWei = parseUnits(val, tokenDecimals.value)

      await loadChainConstants(setupAddr)

      const pc = getPublicClient()
      const factoryAddr = (await pc.readContract({
        address: _routerAddr!,
        abi: uniswapV2RouterAbi,
        functionName: 'factory',
      })) as Address

      const pairAddr = (await pc.readContract({
        address: factoryAddr,
        abi: uniswapV2FactoryAbi,
        functionName: 'getPair',
        args: [_weth!, tokenAddr],
      })) as Address

      if (!pairAddr || pairAddr === '0x0000000000000000000000000000000000000000') {
        quoteError.value = 'No Uniswap pool for MTK/WETH. Pool not created yet.'
        step.value = 'input'
        return
      }

      const reserves = (await pc.readContract({
        address: pairAddr,
        abi: uniswapV2PairAbi,
        functionName: 'getReserves',
      })) as [bigint, bigint, number]

      if (reserves[0] === 0n || reserves[1] === 0n) {
        quoteError.value = 'MTK/WETH pool has no liquidity. Add liquidity first.'
        step.value = 'input'
        return
      }

      const tokenReserve = _weth!.toLowerCase() < tokenAddr.toLowerCase() ? reserves[1] : reserves[0]

      if (amountOutWei >= tokenReserve) {
        const maxTokens = formatUnits(tokenReserve, tokenDecimals.value)
        quoteError.value = `Insufficient pool liquidity. Max available: ${Number(maxTokens).toFixed(2)} MTK`
        step.value = 'input'
        return
      }

      const amounts = (await pc.readContract({
        address: _routerAddr!,
        abi: uniswapV2RouterAbi,
        functionName: 'getAmountsIn',
        args: [amountOutWei, [_weth!, tokenAddr]],
      })) as readonly bigint[]

      ethRequired.value = amounts[0]
      step.value = 'confirm'
    } catch (e: any) {
      quoteError.value = e?.shortMessage || e?.message || 'Failed to get quote'
      step.value = 'input'
    }
  }

  /** Step 2: user confirms — send the transaction */
  async function confirmBuy() {
    const c = contracts()
    if (!c) return

    step.value = 'buying'
    buyError.value = ''

    try {
      const provider = getProvider() as any
      if (!provider) throw new Error('Wallet not connected')

      const account = getWalletAddress()
      if (!account) throw new Error('Wallet address not available')

      const amountOutWei = parseUnits(tokenAmountInput.value.trim(), tokenDecimals.value)
      const minOut = (amountOutWei * 950n) / 1000n

      const to = c.uniswap_liquidity_setup as Address
      const data = encodeSwap(c.meeting_token as Address, minOut)
      const valueHex = `0x${ethRequired.value.toString(16)}`

      const hash = await provider.request({
        method: 'eth_sendTransaction',
        params: [{
          from: account,
          to,
          value: valueHex,
          data,
        }],
      }) as string

      buyTxHash.value = hash

      // Wait for on-chain confirmation
      const pc = getPublicClient()
      const receipt = await pc.waitForTransactionReceipt({ hash: hash as `0x${string}` })

      if (receipt.status === 'success') {
        step.value = 'done'
      } else {
        buyError.value = 'Transaction reverted on-chain'
        step.value = 'confirm'
      }
    } catch (e: any) {
      buyError.value = e?.shortMessage || e?.message || 'Transaction failed'
      step.value = 'confirm'
    }
  }

  /** Go back from confirm to input */
  function cancelConfirm() {
    step.value = 'input'
    ethRequired.value = 0n
  }

  return {
    showModal,
    tokenAmountInput,
    step,
    ethRequired,
    ethRequiredFormatted,
    quoteError,
    buyError,
    buyTxHash,
    openBuyModal,
    closeModal,
    requestBuy,
    confirmBuy,
    cancelConfirm,
  }
}
