#include "stm32f10x.h"                  // Device header
#include "ESP8266.h"
#include "Delay.h"
#include <stdio.h>
#include "WiFiConfig.h"
#include <string.h>
#include "NetworkConfig.h"

static void ESP8266_ClearReceiveData(void);
static unsigned char ESP8266_waitResponse(const char *response,unsigned long timeout_ms);

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
 * @param timeout_ms 最大空闲等待时间，单位为毫秒
 * @retval 1U：收到OK；0U：超时未收到OK
 */
unsigned char ESP8266_TestConnection(unsigned long timeout_ms)
{
	ESP8266_ClearReceiveData();
	ESP8266_SendString("AT\r\n");
	
	return ESP8266_waitResponse("OK",timeout_ms);
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
 * @brief 阻塞等待ESP8266返回目标应答字符串
 * @param response 期望接收的应答字符串，以'\0'结尾
 * @param timeout_ms 最大等待时间，单位为毫秒
 * @retval 1U：完整匹配到目标字符串；0U：达到超时
 */
static unsigned char ESP8266_waitResponse(const char *response,unsigned long timeout_ms)
{
    unsigned long elapsed_us;
    unsigned char rxData;
    unsigned char responseIndex;

    elapsed_us = 0U;
    responseIndex = 0U;

    while(elapsed_us < (timeout_ms * 1000UL))
    {
        if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET)
        {
            rxData = (unsigned char)USART_ReceiveData(USART2);

            if(rxData == (unsigned char)response[responseIndex])
            {
                responseIndex++;

                if(response[responseIndex] == '\0')
                {
                    return 1U;
                }
            }
            else if(rxData == (unsigned char)response[0])
            {
                responseIndex = 1U;
            }
            else
            {
                responseIndex = 0U;
            }
        }

        /*
         * 100us轮询一次，远小于9600波特率的单字节传输时间，
         * 同时保证timeout_ms仍是实际毫秒数。
         */
        Delay_us(100U);
        elapsed_us += 100U;
    }

    return 0U;
}

/**
 * @brief 通过AT+RST复位ESP8266并等待模块启动完成
 * @param None
 * @retval 1：模块复位并启动成功；0：超时未收到ready
 */
unsigned char ESP8266_Reset(void)
{
	ESP8266_ClearReceiveData();
	
	ESP8266_SendString("AT+RST\r\n");
	
	if(ESP8266_waitResponse("ready",5000U) == 0U)
	{
		return 0U;
	}
	
	/* 模块输出ready后仍需要一点时间恢复串口和网络功能 */
	Delay_ms(5000U);
	
	return 1U;
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
	
	// WiFi连接耗时较长，超时设置40秒；未收到OK则返回3
	if(ESP8266_waitResponse("OK",40000U) == 0U)
	{
		return 3U;
	}
	
	return 1U;
}

/**
 * @brief 连接指定TCP服务器
 * @param server_ip TCP服务器IPv4地址
 * @param server_port TCP服务器端口
 * @retval 1：连接成功；0：连接失败
 */
unsigned char ESP8266_ConnectTcpServer(const char *server_ip,unsigned int server_port)
{
	char command[96];
	
	/*
	 * 关闭可能残留的旧TCP会话。
	 * 未连接时返回ERROR是正常现象，因此不检查该命令的回执。
	 */
	ESP8266_ClearReceiveData();
	ESP8266_SendString("AT+CIPCLOSE\r\n");
	Delay_ms(300U);
	ESP8266_ClearReceiveData();
	
	/* 设置普通TCP模式，并立即等待该命令的OK响应。 */
	ESP8266_SendString("AT+CIPMODE=0\r\n");
	
	if(ESP8266_waitResponse("OK",3000U) == 0U)
	{
		return 0U;
	}
	
	/* CIPMUX=0：单连接模式，只允许1路TCP连接 */
	ESP8266_SendString("AT+CIPMUX=0\r\n");
	if(ESP8266_waitResponse("OK",3000U) == 0U)
	{
		return 0U;
	}
	
	/* 发起TCP连接 AT+CIPSTART="TCP","ip",port */
	sprintf(command,"AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",server_ip,server_port);
	
	ESP8266_ClearReceiveData();
	ESP8266_SendString(command);
	
	/* 收到CONNECT表示TCP连接已经真正建立 */
	if(ESP8266_waitResponse("CONNECT",8000U) == 0U)
	{
		return 0U;
	}
	
	return 1U;
}

/**
 * @brief CIPMODE=0模式下通过TCP发送字符串数据
 * @param data 待发送的'\0'结尾字符串
 * @retval 1U：收到发送提示符并发送数据；0U：未收到发送提示符
 */
unsigned char ESP8266_SendTcpData(const char *data)
{
	char command[32];
	unsigned int dataLength;
	
	dataLength = (unsigned int)strlen(data);
		
	/*
	 * 当前测试报文必须包含完整的回车换行，
	 * 长度由strlen自动计算，不能手写固定长度。
	 */
	sprintf(command,"AT+CIPSEND=%u\r\n",dataLength);
	
	ESP8266_ClearReceiveData();
	ESP8266_SendString(command);
	
	/*
	 * 等待ESP8266返回发送提示符。
	 * 收到提示符后稍作等待，确保模块已经进入数据发送状态。
	 */
	if(ESP8266_waitResponse(">",5000U) == 0U)
	{
		return 0U;
	}
	
	Delay_ms(100U);
	
	/*
	 * ESP8266已经返回'>'提示符，
	 * 表示模块已经进入数据接收状态。
	 */
	ESP8266_SendString(data);
	
	/*
	 * 以收到'>'并发送完整数据作为本次发送成功条件。
	 * NetAssist收到数据用于验证端到端通信。
	 */
	return 1U;
}

