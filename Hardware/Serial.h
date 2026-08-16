#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>


extern char Serial_RxPacket[];
extern volatile uint8_t Serial_RxFlag;//易变变量，告诉编译器不要做寄存器优化，每次都必须从内存读取真实值，不要缓存到 CPU 寄存器**。

void Serial_Init(void);
void Serial_SendByte(uint8_t Byte);
void Serial_SendArray(uint8_t *Array,uint16_t Length);
void Serial_SendString(char *String);
void Serial_SendNumber(uint32_t Number,uint8_t Length);
void Serial_Printf(char *format,...);


#endif
