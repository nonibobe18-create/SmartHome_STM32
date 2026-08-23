#include "stm32f10x.h"                  // Device header
#include "MQ2.h"
#include "Delay.h"

#define MQ2_GPIO GPIOA
#define MQ2_PIN  GPIO_Pin_4

/**
 * @brief 初始化MQ‑2模拟输入引脚PA4
 * @param 无
 * @retval 无
 * @note ADC1由光敏传感器初始化函数完成初始化
 */
void MQ2_Init(void)
{
	 GPIO_InitTypeDef GPIO_InitStructure;
	 
	 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	 
	 GPIO_InitStructure.GPIO_Pin = MQ2_PIN;
	 GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;     // 模拟输入模式
	 GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	 GPIO_Init(MQ2_GPIO,&GPIO_InitStructure);
}
 
 /**
 * @brief 读取MQ‑2多次采样后的ADC平均值
 * @param sampleCount 采样次数
 * @retval ADC平均值，范围 0 ~ 4095
 */
uint16_t MQ2_ReadAverage(uint8_t sampleCount)
{
	uint8_t sampleIndex;
	uint32_t sampleSum;
	uint16_t sampleValue;
	
	if(sampleCount == 0U)
	{
		return 0U;
	}
	
	sampleSum = 0U;
	
	// 切换ADC通道到通道4（PA4，MQ‑2）
	ADC_RegularChannelConfig(ADC1,
	                         ADC_Channel_4,
	                         1,
	                         ADC_SampleTime_55Cycles5);
	
	for(sampleIndex = 0U;
	    sampleIndex < sampleCount;
	    sampleIndex ++)
	{
		ADC_SoftwareStartConvCmd(ADC1,ENABLE);   //软件触发ADC转换
		
		while(ADC_GetFlagStatus(ADC1,ADC_FLAG_EOC) == RESET);  // 等待转换完成EOC标志
		
		sampleValue = ADC_GetConversionValue(ADC1);   //读取转换结果
		sampleSum += sampleValue;
		
		Delay_ms(2U);              //两次采样间隔2ms
		
	}
		
		/*
		 * MQ‑2采样结束后恢复回光敏传感器通道（ADC1通道1，PA1）
		 */
		ADC_RegularChannelConfig(ADC1,
		                         ADC_Channel_1,
		                         1,
		                         ADC_SampleTime_55Cycles5);
		
		return (uint16_t)(sampleSum / sampleCount); //返回平均ADc值
} 
