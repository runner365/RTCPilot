// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "forge-std/Script.sol";
import "../src/UniswapLiquiditySetup.sol";

contract UniswapLiquiditySetupScript is Script {
    function run() external {
        // Deploy UniswapV2Router02 first, then set ROUTER_ADDRESS to its address.
        // Mainnet Uniswap V2 Router: 0x7a250d5630B4cF539739dF2C5dAcb4c659F2488D
        // Sepolia official Uniswap V2 Router
        uint256 deployerPrivateKey = vm.envUint("PRIVATE_KEY");
        address router = vm.envOr("ROUTER_ADDRESS", address(0xeE567Fe1712Faf6149d80dA1E6934E354124CfE3));

        vm.startBroadcast(deployerPrivateKey);
        new UniswapLiquiditySetup(router);
        vm.stopBroadcast();
    }
}
