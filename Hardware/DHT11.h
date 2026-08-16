#ifndef __DHT11_H
#define __DHT11_H

#define DHT11_GPIO GPIOA
#define DHT11_PIN  GPIO_Pin_5

void DHT11_Init(void);
uint8_t DHT11_ReadData(uint8_t *temperature,uint8_t *humidity);


#endif 
