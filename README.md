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

