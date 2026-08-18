#include "stm32f10x.h"                  // Device header
#include "LightSensor.h"

/**
 * @brief 光照传感器初始化，配置PA1为ADC1通道1输入
 * @param None
 * @retval None
 */
void LightSensor_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	ADC_InitTypeDef ADC_InitStructure;
	
	// 开启GPIOA与ADC1外设时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | 
	                       RCC_APB2Periph_ADC1,
	                       ENABLE);
	
	// ADC时钟配置：PCLK2 6分频，F1系列ADC最大时钟不能超过14MHz
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);
	
	// PA1设置为模拟输入模式(AIN)，模拟输入不需要配置速率
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	
	//复位ADC1寄存器
	ADC_DeInit(ADC1);
	
	// ADC基础配置：独立模式、非扫描、单次转换、软件触发、数据右对齐、1个转换通道
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;       // 非扫描模式，单通道
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE; // 单次转换模式，每次需要软件触发
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; // 软件触发
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right; // 数据右对齐
	ADC_InitStructure.ADC_NbrOfChannel = 1;             // 规则通道数量1
	ADC_Init(ADC1,&ADC_InitStructure);
	
	// 配置ADC1规则通道1，排序1，采样时间55.5周期
	ADC_RegularChannelConfig(ADC1,ADC_Channel_1,1,ADC_SampleTime_55Cycles5);
	
	// 使能ADC外设
	ADC_Cmd(ADC1,ENABLE);
	
	// ADC复位校准，等待复位校准完成
	ADC_ResetCalibration(ADC1);
	while(ADC_GetResetCalibrationStatus(ADC1) == SET);
	
	// ADC启动自校准，等待校准完成，提高采样精度
	ADC_StartCalibration(ADC1);
	while(ADC_GetCalibrationStatus(ADC1) == SET);
}

/**
 * @brief 读取光照传感器ADC原始采样值
 * @param None
 * @retval ADC原始值，范围0‑4095（12位ADC）
 */
uint16_t LightSensor_Read(void)
{
	ADC_SoftwareStartConvCmd(ADC1,ENABLE);     // 软件触发一次ADC转换
	
	while(ADC_GetFlagStatus(ADC1,ADC_FLAG_EOC) == RESET);// 等待转换结束EOC标志
	
	return ADC_GetConversionValue(ADC1);       // 返回12位采样结果
}

/**
 * @brief 将ADC原始值换算为光照百分比 0~100%
 * @param None
 * @retval 光照百分比 0‑100
 */
uint8_t LightSensor_ReadPercent(void)
{
	uint16_t rawValue;
	
	rawValue = LightSensor_Read();
	
	// 强转uint32_t防止乘法溢出： rawValue * 100 / 4095
	return (uint8_t)(((uint32_t)rawValue * 100U) / 4095U);
}
