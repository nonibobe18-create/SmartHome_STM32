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

“最终引脚规划”表
PA1       光敏电阻 ADC
PA2/PA3   ESP8266 USART2
PA4       MQ-2 ADC
PA5       STM32 本地 DHT11
PA9/PA10  当前 51 直连 UART
PB0       STM32 蜂鸣器
PB5       外接 LED1
PB8/PB9   当前 OLED
PB10/PB11 ZigBee USART3