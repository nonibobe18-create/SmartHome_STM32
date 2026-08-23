#ifndef __MQ2_H
#define __MQ2_H

#include "stm32f10x.h"

void MQ2_Init(void);
uint16_t MQ2_ReadAverage(uint8_t sampleCount);

#endif
