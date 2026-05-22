# Web3 开发
本工程是一个WebRTC SFU服务端的开源，用于支持WebRTC的视频会议功能。

我们将Web3 etherum 作为视频会议的区块链平台，对于：
* 开启视频会议--会议创建人(如表演者，教学者等角色)
* 加入视频会议--会议加入人(如观众，学生等角色)
* ERC20 代币--用于支付视频会议费用, 会议创建人支付少量的会议token用于创建会议；与会人支付会议token用于参见视频会议费用。
* UniswapV3 用于交换ERC20代币，实现视频会议费用的支付: 提供Eth到ERC20代币的兑换服务; 提供ERC20代币到Eth的兑换服务; 当然提供加入Token与Eth的流动性; 消除Token的流动性；

## 会议创建
* 会议创建人需要用Eth购买会议token。(通过UniswapV3交换: Eth -> 会议token。)
* 会议创建人需要支付少量的会议token，用于创建视频会议。
* 会议创建人需要指定会议的名称、描述、开始时间、结束时间等参数。
* 会议创建人可以设定会议的费用，单位为会议token。每一个参加会议的听众，入会时需要支付会议token。


## 会议加入
* 会议与会人需要用Eth购买会议token。(通过UniswapV3交换: Eth -> 会议token。)
* 会议加入人需要支付会议token用于参见视频会议费用。

## ERC20 代币
我们将会议token实现为一个ERC20代币，命名为MeetingToken。会议token具有以下特点：
* 会议token是一个可替代的代币，可以在UniswapV3上进行交易。
* 会议token的总供应量是通过ERC20合约进行初始化，设定totalSupply。
* 会议token的持有者可以通过ERC20合约进行转账、查询余额等操作。

## uniswapV3 交换
我们将UniswapV3作为会议token的交换平台，提供以下功能：
* 提供Eth到会议token的兑换服务。
* 提供会议token到Eth的兑换服务。
* 提供加入会议token与Eth的流动性。
* 消除会议token的流动性。

