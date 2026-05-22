// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "forge-std/Test.sol";
import "../src/MeetingToken.sol";

contract MeetingTokenTest is Test {
    MeetingToken public token;
    address public constant OWNER = address(0x1000);
    address public constant USER_A = address(0x2000);
    address public constant USER_B = address(0x3000);

    uint256 public constant INITIAL_SUPPLY = 1_000_000 * 10 ** 18;

    function setUp() public {
        vm.prank(OWNER);
        token = new MeetingToken(INITIAL_SUPPLY, OWNER);
    }

    function test_Constructor_InitialSupply() public view {
        assertEq(token.totalSupply(), INITIAL_SUPPLY);
        assertEq(token.balanceOf(OWNER), INITIAL_SUPPLY);
    }

    function test_Constructor_OwnerSet() public view {
        assertEq(token.owner(), OWNER);
    }

    function test_Transfer_BetweenUsers() public {
        uint256 amount = 100 * 10 ** 18;
        vm.prank(OWNER);
        token.transfer(USER_A, amount);
        assertEq(token.balanceOf(USER_A), amount);
        assertEq(token.balanceOf(OWNER), INITIAL_SUPPLY - amount);
    }

    function test_Mint_OnlyOwner() public {
        uint256 mintAmount = 500 * 10 ** 18;
        vm.prank(OWNER);
        token.mint(USER_A, mintAmount);
        assertEq(token.totalSupply(), INITIAL_SUPPLY + mintAmount);
        assertEq(token.balanceOf(USER_A), mintAmount);
    }

    function test_Revert_Mint_NotOwner() public {
        vm.prank(USER_A);
        vm.expectRevert();
        token.mint(USER_A, 100 * 10 ** 18);
    }

    function test_Burn_OwnTokens() public {
        uint256 burnAmount = 200 * 10 ** 18;
        vm.prank(OWNER);
        token.burn(burnAmount);
        assertEq(token.totalSupply(), INITIAL_SUPPLY - burnAmount);
        assertEq(token.balanceOf(OWNER), INITIAL_SUPPLY - burnAmount);
    }

    function test_Revert_Burn_ExceedsBalance() public {
        vm.prank(USER_A);
        vm.expectRevert();
        token.burn(100 * 10 ** 18);
    }

    function test_BurnFrom_WithApproval() public {
        uint256 burnAmount = 300 * 10 ** 18;
        vm.prank(OWNER);
        token.approve(USER_A, burnAmount);
        vm.prank(USER_A);
        token.burnFrom(OWNER, burnAmount);
        assertEq(token.totalSupply(), INITIAL_SUPPLY - burnAmount);
        assertEq(token.balanceOf(OWNER), INITIAL_SUPPLY - burnAmount);
    }

    function test_Decimals() public view {
        assertEq(token.decimals(), 18);
    }

    function test_Name_Symbol() public view {
        assertEq(token.name(), "MeetingToken");
        assertEq(token.symbol(), "MTK");
    }
}
