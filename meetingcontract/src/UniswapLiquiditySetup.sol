// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/access/Ownable.sol";
import "@openzeppelin/token/ERC20/IERC20.sol";
import "@openzeppelin/utils/ReentrancyGuard.sol";
import "@uniswap/v2-periphery/interfaces/IUniswapV2Router02.sol";
import "@uniswap/v2-core/interfaces/IUniswapV2Factory.sol";
import "@uniswap/v2-core/interfaces/IUniswapV2Pair.sol";

contract UniswapLiquiditySetup is Ownable, ReentrancyGuard {
    IUniswapV2Router02 public immutable router;
    IUniswapV2Factory public immutable factory;
    address public immutable WETH;

    event LiquidityAdded(address indexed token, uint256 tokenAmount, uint256 ethAmount, uint256 liquidity);
    event LiquidityRemoved(address indexed token, uint256 liquidity, uint256 tokenAmount, uint256 ethAmount);
    event TokenSwappedETH(address indexed token, uint256 ethSpent, uint256 tokenReceived);
    event TokenSwappedForETH(address indexed token, uint256 tokenSpent, uint256 ethReceived);
    event ETHWithdrawn(address indexed to, uint256 amount);
    event ERC20Swept(address indexed token, address indexed to, uint256 amount);

    constructor(address _router) Ownable(msg.sender) {
        require(_router != address(0), "Router must be a valid address");
        router = IUniswapV2Router02(_router);
        factory = IUniswapV2Factory(router.factory());
        WETH = router.WETH();
    }

    function createPoolAndAddLiquidity(
        address tokenA,
        uint256 amount
    ) external payable onlyOwner nonReentrant {
        require(tokenA != address(0), "TokenA must be a valid address");
        require(msg.value > 0, "Must send ETH to add liquidity");
        require(amount > 0, "tokenA amount must not be zero");

        (address token0, address token1) = _sortTokens(tokenA, WETH);
        address pair = factory.getPair(token0, token1);
        if (pair == address(0)) {
            factory.createPair(token0, token1);
        }

        IERC20(tokenA).transferFrom(msg.sender, address(this), amount);
        IERC20(tokenA).approve(address(router), 0);
        IERC20(tokenA).approve(address(router), amount);

        uint256 minTokenAmount = amount * 950 / 1000;
        uint256 minEthAmount = msg.value * 950 / 1000;

        (uint256 amountToken, uint256 amountEth, uint256 liquidity) = router.addLiquidityETH{value: msg.value}(
            tokenA,
            amount,
            minTokenAmount,
            minEthAmount,
            owner(),
            block.timestamp + 600
        );

        emit LiquidityAdded(tokenA, amountToken, amountEth, liquidity);
    }

    function removeLiquidity(
        address tokenA,
        uint256 liquidity
    ) external onlyOwner nonReentrant {
        require(tokenA != address(0), "TokenA must be a valid address");
        require(liquidity > 0, "Liquidity amount must be greater than zero");

        (address token0, address token1) = _sortTokens(tokenA, WETH);
        address pair = factory.getPair(token0, token1);
        require(pair != address(0), "Pool does not exist");

        IERC20(pair).transferFrom(msg.sender, address(this), liquidity);
        IERC20(pair).approve(address(router), 0);
        IERC20(pair).approve(address(router), liquidity);

        uint256 totalSupply = IERC20(pair).totalSupply();
        (uint256 reserveToken, uint256 reserveWeth, ) = IUniswapV2Pair(pair).getReserves();
        if (reserveToken == 0 || reserveWeth == 0) {
            revert("Pool reserves are empty");
        }
        if (token0 != tokenA) {
            (reserveToken, reserveWeth) = (reserveWeth, reserveToken);
        }
        uint256 minTokenAmount = liquidity * reserveToken / totalSupply * 950 / 1000;
        uint256 minEthAmount = liquidity * reserveWeth / totalSupply * 950 / 1000;

        (uint256 amountToken, uint256 amountEth) = router.removeLiquidityETH(
            tokenA,
            liquidity,
            minTokenAmount,
            minEthAmount,
            owner(),
            block.timestamp + 600
        );

        emit LiquidityRemoved(tokenA, liquidity, amountToken, amountEth);
    }

    function swapExactETHForTokens(
        address tokenA,
        uint256 amountOutMin
    ) external payable nonReentrant {
        require(tokenA != address(0), "TokenA must be a valid address");
        require(msg.value > 0, "Must send ETH to swap");

        address[] memory path = new address[](2);
        path[0] = WETH;
        path[1] = tokenA;

        uint256[] memory amounts = router.swapExactETHForTokens{value: msg.value}(
            amountOutMin,
            path,
            msg.sender,
            block.timestamp + 15 minutes
        );

        emit TokenSwappedETH(tokenA, amounts[0], amounts[1]);
    }

    function swapExactTokensForETH(
        address tokenA,
        uint256 amountIn,
        uint256 amountOutMin
    ) external onlyOwner nonReentrant {
        require(tokenA != address(0), "TokenA must be a valid address");
        require(amountIn > 0, "AmountIn must be greater than 0");

        IERC20(tokenA).transferFrom(msg.sender, address(this), amountIn);
        IERC20(tokenA).approve(address(router), amountIn);

        address[] memory path = new address[](2);
        path[0] = tokenA;
        path[1] = WETH;

        uint256[] memory amounts = router.swapExactTokensForETH(
            amountIn,
            amountOutMin,
            path,
            msg.sender,
            block.timestamp + 15 minutes
        );

        emit TokenSwappedForETH(tokenA, amounts[0], amounts[1]);
    }

    function withdrawETH() external onlyOwner nonReentrant {
        uint256 balance = address(this).balance;
        require(balance > 0, "No ETH to withdraw");

        (bool sent, ) = msg.sender.call{value: balance}("");
        require(sent, "ETH transfer failed");

        emit ETHWithdrawn(msg.sender, balance);
    }

    function sweepERC20(address token) external onlyOwner nonReentrant {
        require(token != address(0), "Token must be a valid address");

        uint256 balance = IERC20(token).balanceOf(address(this));
        require(balance > 0, "No tokens to sweep");

        IERC20(token).transfer(msg.sender, balance);

        emit ERC20Swept(token, msg.sender, balance);
    }

    function _sortTokens(
        address tokenA,
        address tokenB
    ) private pure returns (address token1, address token2) {
        require(tokenA != address(0), "TokenA must be a valid address");
        require(tokenB != address(0), "TokenB must be a valid address");

        if (tokenA < tokenB) {
            return (tokenA, tokenB);
        } else {
            return (tokenB, tokenA);
        }
    }

    receive() external payable {}
}
