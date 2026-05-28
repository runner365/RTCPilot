#!/bin/bash

RPC_URL="${RPC_URL:-https://ethereum-sepolia.publicnode.com}"
GAS_LIMIT="${GAS_LIMIT:-6000000}"

case "$1" in
    MeetingToken)
        forge script script/MeetingToken.s.sol:MeetingTokenScript \
            --rpc-url "$RPC_URL" \
            --gas-limit "$GAS_LIMIT" \
            --broadcast \
            -vvvvv
        ;;
    UniswapLiquiditySetup)
        forge script script/UniswapLiquiditySetup.s.sol:UniswapLiquiditySetupScript \
            --rpc-url "$RPC_URL" \
            --gas-limit "$GAS_LIMIT" \
            --broadcast \
            -vvvvv
        ;;
    MeetingManager)
        forge script script/MeetingManager.s.sol:MeetingManagerScript \
            --rpc-url "$RPC_URL" \
            --gas-limit "$GAS_LIMIT" \
            --broadcast \
            -vvvvv
        ;;
    AddLiquidity)
        forge script script/AddLiquidity.s.sol:AddLiquidityScript \
            --rpc-url "$RPC_URL" \
            --gas-limit "$GAS_LIMIT" \
            --broadcast \
            -vvvvv
        ;;
    *)
        echo "Usage: $0 <ContractName>"
        echo "Available contracts: MeetingToken UniswapLiquiditySetup MeetingManager AddLiquidity"
        exit 1
        ;;
esac
