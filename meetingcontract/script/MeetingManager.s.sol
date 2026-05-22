// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "forge-std/Script.sol";
import "../src/MeetingManager.sol";

contract MeetingManagerScript is Script {
    uint256 public constant CREATION_FEE = 1 * 10 ** 18; // 1 MTK

    function run() external {
        uint256 deployerPrivateKey = vm.envUint("PRIVATE_KEY");
        address meetingTokenAddr = vm.envAddress("MEETING_TOKEN_ADDRESS");

        vm.startBroadcast(deployerPrivateKey);
        new MeetingManager(meetingTokenAddr, CREATION_FEE);
        vm.stopBroadcast();
    }
}
