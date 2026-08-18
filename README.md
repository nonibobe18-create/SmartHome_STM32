\# SmartHome STM32 Gateway



\## Project Description



This project uses STM32F103C8T6 as the smart-home gateway.



The STC89C52 collects DHT11 temperature and humidity data and sends it to STM32 through UART. STM32 parses the packet, displays the data on OLED, and controls the temperature alarm LED.



\## Hardware



\- STM32F103C8T6

\- STC89C52

\- DHT11

\- OLED

\- USB to TTL

\- ST-Link



\## UART Connection



```text

STC89C52 P3.1/TXD -> STM32 PA10/USART1\_RX

STC89C52 GND      -> STM32 GND

## STM32 Final Pin Map


Pin	Function	Status
PA1	Light sensor ADC (光照传感器 ADC 采集)	Planned 待开发
PA2/PA3	ESP8266 USART2(TX/RX)	Planned 待开发
PA4	MQ‑2 ADC (烟雾传感器 ADC 采集)	Planned 待开发
PA5	Local DHT11 (本地 DHT11 温湿度单总线)	Planned 待开发
PA9/PA10	Current 51 direct UART USART1 (TX/RX，和 51 节点直连通信)	In use 正在使用
PB0	Local buzzer (本地蜂鸣器)	Planned 待开发
PB5	External alarm LED1 (外部报警指示灯)	In use 正在使用
PB8/PB9	OLED I2C(SCL/SDA)	In use 正在使用
PB10/PB11	ZigBee USART3 (TX/RX，ZigBee 模块串口)	Planned 待开发
