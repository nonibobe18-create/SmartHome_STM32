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

/* 主循环执行间隔 */
#define MAIN_LOOP_INTERVAL_MS 10U

/* 连续5秒未收到有效数据，则判定节点离线 */
#define NODE_OFFLINE_TIMEOUT_MS 5000U

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
		OLED_ShowString(3,1,"State:TEMP HIGH ");
		alarmActive = 1;
	}
	else
	{
		LED1_OFF();
		OLED_ShowString(3,1,"State:NORMAL    ");
	}
	
	return alarmActive;                  //返回当前报警状态给调用者
}

/**
 * @brief 根据温度报警状态向51节点发送控制命令。
 * @param alarmActive 1表示开启报警，0表示关闭报警。
 * @retval None
 */
static void Communication_SendAlarmCommand(uint8_t alarmActive)
{
	if(alarmActive == 1)
	{
		/* 温度报警时，通知51点亮本地报警LED。 */
		Serial_SendString("@G1,ALARM=1\r\n");
	}
	else
	{
		/* 温度正常时，通知51关闭本地报警LED。 */
		Serial_SendString("@G1,ALARM=0\r\n");
	}
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
 * @brief 解析来自N1节点的DHT11错误上报数据包
 * @param errorCode 指针，用于接收解析得到的DHT11错误码
 * @retval 1 数据包解析有效；0 数据包格式错误/数值非法
 */
static uint8_t Communication_ParseNodeErrorPacket(uint8_t *errorCode)
{
	unsigned int parsedErrorCode;
	int parseResult;
	
	//按照格式“N1,ERROR=%u”从接收缓存区提取错误码
	parseResult = sscanf(Serial_RxPacket,"N1,ERROR=%u",&parsedErrorCode);
	
	/*
     * 判断：sscanf成功解析出1个数据；并且错误码范围1~6
     * 不满足任意一个，代表报文无效，返回0
     */
	if((parseResult != 1) || (parsedErrorCode < 1U) || (parsedErrorCode > 6U))
	{
		return 0;
	}
	
	// 将解析出的错误码赋值给外部传入的指针变量
	*errorCode = (uint8_t)parsedErrorCode;
	
	return 1;
}

/**
 * @brief 处理来自51节点的一整包接收数据
 * @param None
 * @retval 收到有效数据包返回1，否则返回0
 */
static uint8_t Communication_ProcessReceivePacket(void)
{
	uint8_t temperature;
	uint8_t humidity;
	uint8_t alarmActive;
	uint8_t sensorErrorCode;
	
	if(Serial_RxFlag == 0)
	{
		return 0;
	}
	
	if(Communication_ParseNodePacket(&temperature, &humidity) == 1)
	{
		/* 刷新显示，并取得当前温度报警状态。 */
		alarmActive = Environment_Update(temperature, humidity);
		
		/* 将报警状态回传给51节点。 */
		Communication_SendAlarmCommand(alarmActive);
		
		OLED_ShowString(4,1,"NODE:N1 OK      ");
		
		/* 允许串口驱动接收下一包数据 */
		Serial_RxFlag = 0;
		
		return 1;
	}
	else if(Communication_ParseNodeErrorPacket(&sensorErrorCode) == 1)
	{
		/* 节点在线，但DHT11传感器读取失败 */
		LED1_ON();
		
		/* 清除远端节点上原有的温度报警 */
		Communication_SendAlarmCommand(0);
		
		OLED_ShowString(3,1,"State:DHT ERR   ");
		OLED_ShowNum(3,15,sensorErrorCode,1);
		OLED_ShowString(4,1,"NODE:N1 DHT ERR");
		
		
		Serial_RxFlag = 0;
		
		return 1;
	}
	else
	{
		/* 接收到无效数据包，显示错误状态 */
		LED1_ON();
		OLED_ShowString(3,1,"State:PACKET ERR");
		OLED_ShowString(4,1,"NODE:N1 ERROR   ");
		
		Serial_RxFlag = 0;
		
		return 0;
	}
}

/**
 * @brief  STM32网关主函数入口
 * @param  None
 * @retval 不会返回
 */
int main(void)
{
	
	uint32_t nodeOfflineTimeMs;
    uint8_t packetValid;
	
	OLED_Init();
	Serial_Init();
	LED_Init();
	
	Environment_DispalyStaticText();
	
	/* 初始为离线状态，直到接收到节点有效数据包 */
	nodeOfflineTimeMs = NODE_OFFLINE_TIMEOUT_MS;

	
	while(1)
	{
		/* 处理来自51节点的一整包接收数据 */
    packetValid = Communication_ProcessReceivePacket();

    if (packetValid == 1)
    {
        /* 接收到有效数据包后重置计时 */
        nodeOfflineTimeMs = 0;
    }
    else if (nodeOfflineTimeMs < NODE_OFFLINE_TIMEOUT_MS)
    {
        /* 未收到有效数据包时，累加计时 */
        nodeOfflineTimeMs += MAIN_LOOP_INTERVAL_MS;
    }

    if (nodeOfflineTimeMs >= NODE_OFFLINE_TIMEOUT_MS)
    {
        /* 节点离线，关闭报警LED */
        LED1_OFF();

        OLED_ShowString(3, 1, "State:OFFLINE   ");
        OLED_ShowString(4, 1, "NODE:N1 OFFLINE ");
    }

    Delay_ms(MAIN_LOOP_INTERVAL_MS);
	}
}
