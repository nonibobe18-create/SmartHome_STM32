#include "stm32f10x.h"                  // Device header
#include "ESP8266.h"
#include "Delay.h"
#include <stdio.h>
#include "WiFiConfig.h"

/**
 * @brief 初始化ESP8266使用的USART2
 * @param None
 * @retval None
 */
void ESP8266_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStruture;
	
	/* 开启GPIOA外设时钟；USART2挂载在APB1总线上 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,ENABLE);
	
	/* PA2 → USART2_TX 复用推挽输出 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	
	/* PA3 → USART2_RX 上拉输入模式 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	
	/* ESP8266波特率9600，8N1：8数据位，无校验，1停止位，无硬件流控，收发全开 */
	USART_InitStruture.USART_BaudRate = 9600;
	USART_InitStruture.USART_WordLength = USART_WordLength_8b;
	USART_InitStruture.USART_StopBits = USART_StopBits_1;
	USART_InitStruture.USART_Parity = USART_Parity_No;
	USART_InitStruture.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStruture.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	
	USART_Init(USART2,&USART_InitStruture);
	USART_Cmd(USART2,ENABLE);
}

/**
 * @brief USART2发送单个字节，等待发送寄存器为空
 * @param byte 需要发送的字节数据
 * @retval None
 */
void ESP8266_SendByte(unsigned char byte)
{
	USART_SendData(USART2,byte);
	while(USART_GetFlagStatus(USART2,USART_FLAG_TXE) == RESET);// 等待发送数据寄存器空
}

/**
 * @brief 发送以'\0'结尾的字符串给ESP8266
 * @param string 待发送字符串指针
 * @retval None
 */
void ESP8266_SendString(const char *string)
{
	while(*string != '\0')
	{
		ESP8266_SendByte((unsigned char) *string);
		string ++;
	}
}

/**
 * @brief 发送AT命令并等待ESP8266返回OK
 * @param timeout_ms 最大等待时间，单位为毫秒
 * @retval 1：收到OK；0：超时未收到OK
 */
unsigned char ESP8266_TestConnection(unsigned long timeout_ms)
{
	unsigned long elapsed_ms;
	unsigned char rxData;
	unsigned char receiveState;
	
	elapsed_ms = 0U;
	receiveState = 0U;
	
	/* 清除发送测试命令前残留的接收数据 */
	while(USART_GetFlagStatus(USART2,USART_FLAG_RXNE) == SET)
	{
		USART_ReceiveData(USART2);
	}
	
	/* 发送AT测试指令 AT\r\n */
	ESP8266_SendString("AT\r\n");
	
	/* 阻塞循环，在超时时间内轮询接收寄存器 */
	while(elapsed_ms < timeout_ms)
	{
		if(USART_GetFlagStatus(USART2,USART_FLAG_RXNE) == SET)
		{
			rxData = (unsigned char)USART_ReceiveData(USART2);
			
			/* 状态机匹配 "OK" */
			if((receiveState == 0U) && (rxData == 'O'))
			{
				receiveState = 1U;    // 收到'O'，等待下一个'K'
			}
			else if((receiveState == 1U) && (rxData == 'K'))
			{
				return 1U;            // O后面紧跟K，匹配OK，连接正常
			}
			else if(rxData == 'O')
			{
				receiveState = 1U;   // 中途乱码，重新捕获O
			}
			else
			{
				receiveState = 0U;   // 收到其他字符，状态重置
			}
		}
		
		Delay_ms(1U);  // 1ms延时
		elapsed_ms ++; // 计时累加
	}
	
	return 0U;         // 超时退出，未收到OK
}

/**
 * @brief 清空USART2接收寄存器，丢弃未读取的残留字节
 * @param None
 * @retval None
 */
static void ESP8266_ClearReceiveData(void)
{
	while(USART_GetFlagStatus(USART2,USART_FLAG_RXNE) == SET)
	{
		USART_ReceiveData(USART2);
	}
}

/**
 * @brief 阻塞等待ESP8266返回目标应答字符串，状态机逐字符匹配
 * @param response 期望接收的应答字符串(以'\0'结尾)
 * @param timeout_ms 最大等待时间，单位为毫秒
 * @retval 1U：完整匹配到目标字符串；0U：超时未匹配
 */
static unsigned char ESP8266_waitResponse(const char *response,unsigned long timeout_ms)
{
	unsigned long elapsed_ms;
	unsigned char rxData;
	unsigned char responseIndex;
	
	elapsed_ms = 0U;
	responseIndex = 0U;
	
	while(elapsed_ms < timeout_ms)
	{
		if(USART_GetFlagStatus(USART2,USART_FLAG_RXNE) == SET)
		{
			rxData = (unsigned char)USART_ReceiveData(USART2);
			
			// 当前字符和期待字符匹配，索引后移
			if(rxData == (unsigned char)response[responseIndex])
			{
				responseIndex ++;
				// 读到字符串结束符，代表完整匹配成功
				if(response[responseIndex] == '\0')
				{
					return 1U;
				}
			}
			// 收到目标串首字符，重置索引从第2位开始匹配（处理中途乱码）
			else if(rxData == (unsigned char)response[0])
			{
				responseIndex = 1U;
			}
			// 字符不匹配，匹配状态清零，从头开始
			else
			{
				responseIndex = 0U;
			}
		}
		
		Delay_ms(1U);
		elapsed_ms ++;
	}
	
	return 0U;// 超时退出，未收到目标应答
}

/**
 * @brief 配置ESP8266为Station模式，执行连接WiFi操作
 * @param None
 * @retval 1U：WiFi连接全部成功
 * @retval 2U：Station模式设置失败
 * @retval 3U：WiFi账号密码连接失败/超时
 */
unsigned char ESP8266_ConnectWiFi(void)
{
	char command[96];
	
	// AT+CWMODE=1 设置为STA模式；返回OK或者no change（已经是STA模式）都算成功
	ESP8266_ClearReceiveData();
	ESP8266_SendString("AT+CWMODE=1\r\n");
	
	// OK / no change 任意一个应答出现即判定模式配置成功，两个都没收到返回2
	if((ESP8266_waitResponse("OK",3000U) == 0U) && (ESP8266_waitResponse("no change",1000U) == 0U))
	{
		return 2U;
	}
	
	// 拼接AT+CWJAP连接WiFi指令，填入SSID和密码宏
	sprintf(command,"AT+CWJAP=\"%s\",\"%s\"\r\n",WIFI_SSID,WIFI_PASSWORD);
	
	ESP8266_ClearReceiveData();
	ESP8266_SendString(command);
	
	// WiFi连接耗时较长，超时设置20秒；未收到OK则返回3
	if(ESP8266_waitResponse("OK",20000U) == 0U)
	{
		return 3U;
	}
	
	return 1U;
}

