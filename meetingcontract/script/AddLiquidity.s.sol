// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "forge-std/Script.sol";
import "../src/UniswapLiquiditySetup.sol";
import "@openzeppelin/token/ERC20/IERC20.sol";

contract AddLiquidityScript is Script {
    function run() external {
        uint256 deployerPrivateKey = vm.envUint("PRIVATE_KEY");

        address payable setupAddr = payable(address(0xAeE55f3E2927587144f1238C5F802EAd30b6689A));
        address tokenAddr = address(0x10f2594b8B8c166AC5C7F2B3cD0b5E92d74e2B4d);

        uint256 tokenAmount = vm.envOr("TOKEN_AMOUNT", uint256(100_000 ether));
        uint256 ethAmount = vm.envOr("ETH_AMOUNT", uint256(0.1 ether));

        vm.startBroadcast(deployerPrivateKey);

        // Approve the setup contract to pull tokens
        IERC20(tokenAddr).approve(setupAddr, tokenAmount);

        // Create pool and add liquidity
        UniswapLiquiditySetup(setupAddr).createPoolAndAddLiquidity{value: ethAmount}(
            tokenAddr,
            tokenAmount
        );

        vm.stopBroadcast();

        console.log("Liquidity added:");
        console.log("  Token: %s", tokenAddr);
        console.log("  Token amount: %d", tokenAmount);
        console.log("  ETH amount: %d", ethAmount);
    }
}
