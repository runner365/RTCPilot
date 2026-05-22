// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/token/ERC20/ERC20.sol";
import "@openzeppelin/token/ERC20/extensions/ERC20Burnable.sol";
import "@openzeppelin/access/Ownable.sol";

contract MeetingToken is ERC20, ERC20Burnable, Ownable {
    constructor(
        uint256 totalSupply_,
        address owner_
    ) ERC20("MeetingToken", "MTK") Ownable(owner_) {
        _mint(owner_, totalSupply_);
    }

    function mint(address to, uint256 amount) external onlyOwner {
        _mint(to, amount);
    }

    function burn(uint256 amount) public override {
        super.burn(amount);
    }

    function burnFrom(address account, uint256 amount) public override {
        super.burnFrom(account, amount);
    }
}
