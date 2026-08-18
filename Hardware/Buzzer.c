#include "stm32f10x.h"                  // Device header
#include "Buzzer.h"

/* 本地蜂鸣器连接PB0，默认低电平有效 */
#define BUZZER_GPIO GPIOB
#define BUZZER_PIN  GPIO_Pin_0

/**
 * @brief 初始化本地蜂鸣器GPIO
 * @param None
 * @retval None
 */
void Buzzer_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	
	GPIO_InitStructure.GPIO_Pin = BUZZER_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(BUZZER_GPIO,&GPIO_InitStructure);
	
	/* 低电平有效，初始化时先关闭蜂鸣器 */
	GPIO_SetBits(BUZZER_GPIO,BUZZER_PIN);
}

/**
 * @brief 打开本地蜂鸣器
 * @param None
 * @retval None
 */
void Buzzer_On(void)
{
	GPIO_ResetBits(BUZZER_GPIO,BUZZER_PIN);
}

/**
 * @brief 关闭本地蜂鸣器
 * @param None
 * @retval None
 */
void Buzzer_Off(void)
{
	GPIO_SetBits(BUZZER_GPIO,BUZZER_PIN);
}
