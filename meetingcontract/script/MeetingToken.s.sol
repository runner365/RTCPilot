// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "forge-std/Script.sol";
import "../src/MeetingToken.sol";

contract MeetingTokenScript is Script {
    uint256 public constant INITIAL_SUPPLY = 1_000_000 * 10 ** 18;

    function run() external {
        uint256 deployerPrivateKey = vm.envUint("PRIVATE_KEY");
        address deployer = vm.addr(deployerPrivateKey);

        vm.startBroadcast(deployerPrivateKey);
        new MeetingToken(INITIAL_SUPPLY, deployer);
        vm.stopBroadcast();
    }
}
