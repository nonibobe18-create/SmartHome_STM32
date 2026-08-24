# SmartHome STM32 Gateway

## 项目简介

STM32F103C8T6 作为智能家居网关，负责：

- 接收 51 节点温湿度数据；
- 读取本地 DHT11；
- 读取光照传感器和 MQ-2 烟雾传感器；
- 控制 OLED、LED、蜂鸣器；
- 通过 ESP8266 连接 WiFi 和 TCP 服务器；
- 控制 51 节点窗帘；
- 根据光照自动控制窗帘。

项目只使用一个 STC89C52 节点。

## 已完成主要功能

- 51 节点温湿度接收与校验；
- 本地 DHT11 温湿度显示；
- 光照百分比采集；
- MQ-2 烟雾检测和报警；
- 温度、烟雾综合报警；
- 节点离线检测；
- ESP8266 WiFi 连接；
- TCP 环境数据周期上报；
- 窗帘 OPEN、CLOSE、STOP 控制；
- 光照强时自动 CLOSE；
- 光照弱时自动 OPEN；
- 51 节点限位保护。

## STM32 引脚连接

| 引脚 | 功能 | 状态 |
|---|---|---|
| PA1 | 光照传感器 ADC | 已使用 |
| PA2 | ESP8266 USART2 TX | 已使用 |
| PA3 | ESP8266 USART2 RX | 已使用 |
| PA4 | MQ-2 ADC | 已使用 |
| PA5 | 本地 DHT11 | 已使用 |
| PA9 | USART1 TX，连接 51 P3.0 | 已使用 |
| PA10 | USART1 RX，连接 51 P3.1 | 已使用 |
| PB0 | 蜂鸣器 | 已使用 |
| PB1 | 手动 OPEN 按键 | 已使用 |
| PB5 | 报警 LED | 已使用 |
| PB8 | OLED SCL | 已使用 |
| PB9 | OLED SDA | 已使用 |
| PB11 | 手动 CLOSE 按键 | 已使用 |

## 51 与 STM32 串口连接

```text
STM32 PA9/TX  -> 51 P3.0/RXD
STM32 PA10/RX <- 51 P3.1/TXD
STM32 GND     -> 51 GND

```

## 通信报文示例

```text
@N1,T=31,H=51,C=校验值\r\n
@G1,ALARM=1,C=校验值\r\n
@G1,CURTAIN=OPEN,C=校验值\r\n
@G1,CURTAIN=CLOSE,C=校验值\r\n
@G1,CURTAIN=STOP,C=41\r\n
```

## MQ-2 接线

```text
MQ-2 AO -> 2K 分压 -> PA4
PA4     -> 2K       -> GND
```

当前程序使用 ADC 平均采样和滞回阈值，避免传感器波动导致报警反复切换。

## 注意事项

- 不要将 WiFi 名称和密码写入公开 README；
- STM32 与 51 串口必须交叉连接并共地；
- USB-TTL 测试 51 时，不能与 STM32 TX 同时连接到 51 RX。
