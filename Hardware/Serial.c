#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <stdarg.h>

char Serial_RxPacket[100];
volatile uint8_t Serial_RxFlag;

void Serial_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;//复用推挽输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;//TX引脚
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;//上拉输入模式
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;//RX引脚
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 9600;//波特率
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//硬件流控制
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_InitStructure.USART_Parity = USART_Parity_No;//校验模式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//停止位
	USART_InitStructure.USART_WordLength =USART_WordLength_8b;//字长，无校验模式选8b
	USART_Init(USART1,&USART_InitStructure);
	
	USART_ITConfig(USART1,USART_IT_RXNE,ENABLE);//中断方式
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);//分组
	
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);
	
	USART_Cmd(USART1,ENABLE);
}

void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART1,Byte);
	while(USART_GetFlagStatus(USART1,USART_FLAG_TXE) == RESET);//等待发送寄存器空置位
}

void Serial_SendArray(uint8_t *Array,uint16_t Length)
{
	uint16_t i;
	for(i = 0; i< Length; i++)
	{
		Serial_SendByte(Array[i]);
	}
}

void Serial_SendString(char *String)
{
	uint8_t i;
	for(i = 0;String[i] != '\0';i++)
	{
		Serial_SendByte(String[i]);
	}
}

uint32_t Serial_Pow(uint32_t X,uint32_t Y)
{
	uint32_t Result = 1;
	while(Y --)
	{
		Result *=X;
	}
	return Result;
}

void Serial_SendNumber(uint32_t Number,uint8_t Length)
{
	uint8_t i;
	for(i = 0;i < Length;i ++)
	{
		Serial_SendByte(Number / Serial_Pow(10,Length - i -1)% 10 + 0x30);//显示字符，0x30为‘0’，加上偏移量
	}
}

int fputc(int ch,FILE *f)//fputc是prinf函数底层，fputc重定向到串口
{
	Serial_SendByte(ch);
	return ch;
}

void Serial_Printf(char *format,...)//format用来接收格式化字符串,...用来接收可变参数列表
{
	char String[100]; // 定义字符数组缓冲区，存放格式化之后完整字符串，最大100字节
	va_list arg;// 定义可变参数列表变量arg，用来获取...传入的各个参数
	va_start(arg,format);// 初始化可变参数列表，arg绑定到format之后的参数
	vsprintf(String,format,arg);// 根据format格式，把可变参数格式化，输出到String缓冲区
	va_end(arg); // 结束可变参数的读取，做清理工作，必须配对va_start
	Serial_SendString(String); // 把组装完成的字符串通过串口发送出去
}

/**
 * @brief USART1接收中断服务函数
 * @param None
 * @retval None
 */
void USART1_IRQHandler(void)
{
	static uint8_t rxState = 0;    //接收状态机状态
	static uint8_t packetIndex = 0;//接收缓冲区索引
	uint8_t rxData;
	
	
	// 判断是否为接收寄存器非空RXNE中断（收到1字节数据）
	if(USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		//读取接收数据寄存器，获取收到的字节
		rxData = USART_ReceiveData(USART1);
		
		if(rxState == 0)
		{
			/*
             * 状态0：空闲等待状态，等待帧起始字符 '@'
             * Serial_RxFlag == 0：上一帧数据包已经被主循环处理完毕，才允许开启新帧接收
             * 防止旧数据包还未处理就被新数据覆盖
             */
			if((rxData == '@') && (Serial_RxFlag == 0))
			{
				rxState = 1;         //切换状态：进入报文内容接收
				packetIndex = 0;     //缓冲区下标清零，准备存储新报文
			}
		}
		else if(rxState == 1)
		{
			/* 状态1：接收报文主体有效数据 */
			if(rxData == '\r')
			{
				// 收到回车\r：代表报文有效内容接收完毕，切换状态等待换行结束符\n
				rxState = 2;
			}
			else if(packetIndex < 99U)
			{
				/*
                 * 将字节存入接收缓冲区Serial_RxPacket
                 * 缓冲区最大有效长度99，预留1字节位置给字符串结束符'\0'，避免数组越界
                 */
				Serial_RxPacket[packetIndex] = rxData;
				packetIndex ++;
			}
		    else
			{
				// 数据包超长，丢弃当前整帧，复位状态机，等待下一次帧头
				rxState = 0;
				packetIndex = 0;
			}
		}
		else
		{
			/* 状态2：已经收到\r，等待帧结束换行符 \n */
			if(rxData == '\n')
			{
				Serial_RxPacket[packetIndex] = '\0';  // 添加C字符串结束符，供sscanf解析使用
				Serial_RxFlag = 1;                    // 置接收完成标志，通知主循环有完整数据包待处理
			}
			
			// 无论是否正常收到\n，强制复位接收状态机，防止状态机卡死，准备接收下一帧
			rxState = 0;
			packetIndex = 0;
		}
	
	// 手动清除RXNE中断挂起标志位
	USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}
