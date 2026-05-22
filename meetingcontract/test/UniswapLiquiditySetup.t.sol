// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "forge-std/Test.sol";
import "../src/UniswapLiquiditySetup.sol";
import "../src/MeetingToken.sol";

contract UniswapLiquiditySetupTest is Test {
    address public constant UNISWAP_ROUTER = 0x7a250d5630B4cF539739dF2C5dAcb4c659F2488D;
    address public constant WETH = 0xC02aaA39b223FE8D0A0e5C4F27eAD9083C756Cc2;

    UniswapLiquiditySetup public setup;
    MeetingToken public token;
    address public owner = address(0x1000);
    address public user = address(0x2000);

    uint256 public constant INITIAL_SUPPLY = 1_000_000 * 10 ** 18;

    function setUp() public {
        uint256 forkId = vm.createFork(vm.envString("ETH_RPC_URL"));
        vm.selectFork(forkId);

        vm.startPrank(owner);
        vm.deal(owner, 100 ether);
        setup = new UniswapLiquiditySetup(UNISWAP_ROUTER);
        token = new MeetingToken(INITIAL_SUPPLY, owner);
        token.approve(address(setup), INITIAL_SUPPLY);
        vm.stopPrank();

        vm.deal(user, 100 ether);
    }

    function test_Constructor() public view {
        assertEq(setup.owner(), owner);
        assertEq(address(setup.router()), UNISWAP_ROUTER);
        assertEq(address(setup.factory()), setup.router().factory());
        assertEq(setup.WETH(), WETH);
    }

    function test_Revert_ZeroRouter() public {
        vm.expectRevert("Router must be a valid address");
        new UniswapLiquiditySetup(address(0));
    }

    function test_CreatePoolAndAddLiquidity() public {
        uint256 tokenAmount = 1000 * 10 ** 18;
        uint256 ethAmount = 1 ether;

        vm.startPrank(owner);
        token.approve(address(setup), tokenAmount);

        setup.createPoolAndAddLiquidity{value: ethAmount}(address(token), tokenAmount);
        vm.stopPrank();

        address pair = setup.factory().getPair(address(token), WETH);
        assertTrue(pair != address(0), "Pair should be created");
        assertGt(IERC20(pair).balanceOf(owner), 0, "Owner should hold LP tokens");
    }

    function test_Revert_CreatePool_ZeroAmount() public {
        vm.startPrank(owner);
        vm.expectRevert("tokenA amount must not be zero");
        setup.createPoolAndAddLiquidity{value: 1 ether}(address(token), 0);
        vm.stopPrank();
    }

    function test_Revert_CreatePool_NoETH() public {
        vm.startPrank(owner);
        vm.expectRevert("Must send ETH to add liquidity");
        setup.createPoolAndAddLiquidity{value: 0}(address(token), 1000 ether);
        vm.stopPrank();
    }

    function test_SwapExactETHForTokens() public {
        uint256 tokenAmount = 10_000 * 10 ** 18;
        uint256 ethAmount = 10 ether;

        vm.prank(owner);
        setup.createPoolAndAddLiquidity{value: ethAmount}(address(token), tokenAmount);

        uint256 swapEth = 0.1 ether;
        uint256 balanceBefore = token.balanceOf(user);

        vm.prank(user);
        setup.swapExactETHForTokens{value: swapEth}(address(token), 0);

        uint256 balanceAfter = token.balanceOf(user);
        assertGt(balanceAfter, balanceBefore, "Should receive tokens from swap");
    }

    function test_SwapExactTokensForETH() public {
        uint256 tokenAmount = 10_000 * 10 ** 18;
        uint256 ethAmount = 10 ether;

        vm.prank(owner);
        setup.createPoolAndAddLiquidity{value: ethAmount}(address(token), tokenAmount);

        uint256 swapTokens = 100 * 10 ** 18;
        uint256 balanceBefore = address(owner).balance;

        vm.startPrank(owner);
        token.approve(address(setup), swapTokens);
        setup.swapExactTokensForETH(address(token), swapTokens, 0);
        vm.stopPrank();

        uint256 balanceAfter = address(owner).balance;
        assertGt(balanceAfter, balanceBefore, "Should receive ETH from swap");
    }

    function test_RemoveLiquidity() public {
        uint256 tokenAmount = 10_000 * 10 ** 18;
        uint256 ethAmount = 10 ether;

        vm.prank(owner);
        setup.createPoolAndAddLiquidity{value: ethAmount}(address(token), tokenAmount);

        address pair = setup.factory().getPair(address(token), WETH);
        uint256 lpBalance = IERC20(pair).balanceOf(owner);

        vm.startPrank(owner);
        IERC20(pair).approve(address(setup), lpBalance);
        setup.removeLiquidity(address(token), lpBalance);
        vm.stopPrank();
    }

    function test_Revert_RemoveLiquidity_NotOwner() public {
        vm.prank(user);
        vm.expectRevert();
        setup.removeLiquidity(address(token), 1);
    }

    function test_WithdrawETH() public {
        // Send ETH directly to the contract
        vm.prank(user);
        (bool sent, ) = address(setup).call{value: 1 ether}("");
        require(sent, "ETH transfer failed");

        uint256 balanceBefore = address(owner).balance;
        vm.prank(owner);
        setup.withdrawETH();
        uint256 balanceAfter = address(owner).balance;

        assertGt(balanceAfter, balanceBefore, "Should receive ETH");
    }

    function test_Revert_WithdrawETH_ZeroBalance() public {
        vm.prank(owner);
        vm.expectRevert("No ETH to withdraw");
        setup.withdrawETH();
    }

    function test_SweepERC20() public {
        uint256 sweepAmount = 500 * 10 ** 18;
        vm.prank(owner);
        token.transfer(address(setup), sweepAmount);

        vm.prank(owner);
        setup.sweepERC20(address(token));

        assertEq(token.balanceOf(owner), INITIAL_SUPPLY);
    }

    function test_Revert_SweepERC20_ZeroBalance() public {
        vm.prank(owner);
        vm.expectRevert("No tokens to sweep");
        setup.sweepERC20(address(token));
    }

    function test_ReceiveETH() public {
        uint256 sendAmount = 0.5 ether;
        vm.prank(user);
        (bool sent, ) = address(setup).call{value: sendAmount}("");
        assertTrue(sent);
        assertEq(address(setup).balance, sendAmount);
    }
}
