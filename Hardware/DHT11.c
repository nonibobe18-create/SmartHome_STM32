#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "DHT11.h"

static void DHT11_SetOutput(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	GPIO_InitStructure.GPIO_Pin = DHT11_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DHT11_GPIO,&GPIO_InitStructure);
}

static void DHT11_SetInput(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	GPIO_InitStructure.GPIO_Pin = DHT11_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DHT11_GPIO,&GPIO_InitStructure);
}

static uint8_t DHT11_WaitForLevel(uint8_t level, uint8_t timeout_us)//超时等待引脚电平
{
	while(GPIO_ReadInputDataBit(DHT11_GPIO,DHT11_PIN) != level)
	{
		if(timeout_us == 0)
		{
			return 1;
		}
		
		timeout_us --;
		Delay_us(1);
	}
	return 0;
}

void DHT11_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	
	DHT11_SetInput();
	GPIO_SetBits(DHT11_GPIO,DHT11_PIN);
}

static uint8_t DHT11_Start(void)
{
	DHT11_SetOutput();
	
	GPIO_SetBits(DHT11_GPIO,DHT11_PIN);
	Delay_us(10);
	
	GPIO_ResetBits(DHT11_GPIO,DHT11_PIN);
	Delay_ms(20);
	
	GPIO_SetBits(DHT11_GPIO,DHT11_PIN);
	Delay_us(30);
	
	DHT11_SetInput();
	
	if(DHT11_WaitForLevel(0,100))//收到应答信号
	{
		return 1;
	}
	
	if(DHT11_WaitForLevel(1,100))
	{
		return 1;
	}
	
	if(DHT11_WaitForLevel(0,100))
	{
		return 1;
	}
	
	return 0;
}

uint8_t DHT11_ReadData(uint8_t *temperature,uint8_t *humidity)
{
	uint8_t data[5] = {0};
	uint8_t i,j;
	
	if(DHT11_Start())
	{
		return 1;
	}
	
	for(i = 0;i < 5;i ++)
	{
		data[i] = 0;
		
		for(j = 0;j < 8;j ++)
		{
			if(DHT11_WaitForLevel(1,100))
			{
				return 1;
			}
			
			Delay_us(40);
			
			data[i] <<= 1;
			
			if(GPIO_ReadInputDataBit(DHT11_GPIO,DHT11_PIN))
			{
				data[i] |= 1;
			}
			
			if(DHT11_WaitForLevel(0,100))
			{
				return 1;
			}
		}
	}
	
	if((uint8_t)(data[0] +  data[1] + data[2] + data[3]) != data[4])
	{
		return 1;
	}
	
	*humidity = data[0];
    *temperature = data[2];
	
	return 0;
}
