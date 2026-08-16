#include "stm32f10x.h"                  // Device header
#include "delay.h"
#include "OLED.h"
#include "Serial.h"
#include "Key.h"
#include <string.h>
#include "LED.h"
#include "DHT11.h"

/*温度报警阈值*/
#define TEMPERATURE_ALARM_THRESHOLD 30U

/**
*@brief  OLED初始化完成后，显示固定不变的文字标签
*@param  None
*@retval None
*/
static void Environment_DispalyStaticText(void)
{
	OLED_Clear();                         //清空OLED
	
	OLED_ShowString(1,1,"Temp:");         
	OLED_ShowString(2,1,"Humi:");
    OLED_ShowString(3,1,"State:WAIT NODE");
    OLED_ShowString(4,1,"NODE:N1 WAIT");	
}

/**
*@brief  刷新OLED环境数据显示，同时执行温度报警逻辑
*@param  temperature：采集得到的温度
*@param  humidity：采集得到的相对湿度
*@retval 1：温度报警触发，0：温度处于正常范围
*/
static uint8_t Environment_Update(uint8_t temperature, uint8_t humidity)
{
	uint8_t alarmActive = 0;              //报警状态标志，初始为0（无警报）
	
	OLED_ShowNum(1,7,temperature,2);     
    OLED_ShowString(1,10,"C");
	
	OLED_ShowNum(2,7,humidity,2);
	OLED_ShowString(2,10,"%");
	
	//判断温度是否大于等于报警阈值
	if(temperature >= TEMPERATURE_ALARM_THRESHOLD)
	{
		LED1_ON();
		OLED_ShowString(3,1,"State:TEMP HIGH");
		alarmActive = 1;
	}
	else
	{
		LED1_OFF();
		OLED_ShowString(3,1,"State:NORMAL");
	}
	
	return alarmActive;                  //返回当前报警状态给调用者
}

/**
 * @brief  解析来自N1节点的环境数据包
 * @param  temperature：解析出的温度输出指针
 * @param  humidity：解析出的湿度输出指针
 * @retval 1数据包合法；0数据包解析失败
 */
static uint8_t Communication_ParseNodePacket(uint8_t *temperature,
	                                          uint8_t *humidity)
{
	unsigned int parsedTemperature;
	unsigned int parsedHumidity;
	int parseResult;
	
	/*
     * Serial_RxPacket已经由串口驱动过滤掉@、\r、\n
     * 收到有效报文格式： N1,T=31,H=45
     */
	parseResult = sscanf(Serial_RxPacket, "N1,T=%u,H=%u", &parsedTemperature, &parsedHumidity);
	
	if(parseResult != 2)
	{
		return 0;
	}
	
	/* 数值范围校验，过滤非法数据，再转为uint8_t */
	if((parsedTemperature > 99U) || (parsedHumidity > 100U))
	{
		return 0;
	}
	
	*temperature = (uint8_t)parsedTemperature;
	*humidity = (uint8_t)parsedHumidity;
	
	return 1;
}

/**
 * @brief  处理51节点上传的一整包接收数据
 * @param  None
 * @retval None
 */
static void Communication_ProcessReceivePacket(void)
{
	uint8_t temperature;
	uint8_t huminity;
	
	if(Serial_RxFlag == 0)
	{
		return;
	}
	
	if(Communication_ParseNodePacket(&temperature, &huminity) == 1)
	{
		Environment_Update(temperature, huminity);
		OLED_ShowString(4,1,"NODE:N1 OK      ");
	}
	else
	{
		LED1_ON();
		OLED_ShowString(3,1,"State:PACKET ERR");
		OLED_ShowString(4,1,"NODE:N1 OK      ");
	}
	
	/*
     * 处理完当前数据包之后再清零接收标志
     * 串口中断可以继续接收下一帧数据
     */
	Serial_RxFlag = 0;
}

/**
 * @brief  STM32网关主函数入口
 * @param  None
 * @retval 不会返回
 */
int main(void)
{
	OLED_Init();
	Serial_Init();
	LED_Init();
	
	Environment_DispalyStaticText();
	
	while(1)
	{
		Communication_ProcessReceivePacket();
		
		/*
         * 主循环短延时保证循环稳定；
         * 串口接收在中断内独立运行，不受此处延时阻塞。
         */
		Delay_ms(10);
	}
}
