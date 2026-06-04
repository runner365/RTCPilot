# Web3 Development

This project is an open-source WebRTC SFU server supporting WebRTC video conferencing.

We use the Ethereum blockchain as the platform for video conferencing, covering:

* **Create a meeting** — meeting creators (e.g., performers, instructors)
* **Join a meeting** — meeting participants (e.g., audience, students)
* **ERC20 token** — used for video conferencing payments. Meeting creators pay a small amount of meeting tokens to create a conference; participants pay meeting tokens to join.
* **UniswapV3** — used for swapping ERC20 tokens to facilitate conference payments: provides Eth-to-ERC20 token exchange, ERC20 token-to-Eth exchange, adding liquidity for the meeting token with Eth, and removing token liquidity.

## Create a meeting
* Meeting creators need to purchase meeting tokens with Eth (via UniswapV3 swap: Eth → meeting token).
* Meeting creators pay a small amount of meeting tokens to create a video conference.
* Meeting creators specify the meeting name, description, start time, end time, and other parameters.
* Meeting creators can set a meeting fee in meeting tokens. Each participant pays meeting tokens when joining.

## Join a meeting
* Meeting participants need to purchase meeting tokens with Eth (via UniswapV3 swap: Eth → meeting token).
* Meeting participants pay meeting tokens to join the video conference.

## ERC20 token
We implement the meeting token as an ERC20 token named **MeetingToken**, with the following characteristics:

* MeetingToken is a fungible token that can be traded on UniswapV3.
* The total supply of MeetingToken is initialized via the ERC20 contract with a configured `totalSupply`.
* MeetingToken holders can transfer tokens and query balances through the ERC20 contract.

## UniswapV3 swap
We use UniswapV3 as the exchange platform for MeetingToken, providing the following functionality:

* Provide Eth-to-MeetingToken exchange.
* Provide MeetingToken-to-Eth exchange.
* Provide liquidity for the MeetingToken/Eth pair.
* Remove liquidity for the MeetingToken.
