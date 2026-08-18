#include "stm32f10x.h"                  // Device header
#include <string.h>
#include <stdio.h>
#include "delay.h"
#include "OLED.h"
#include "Serial.h"
#include "Key.h"
#include "LED.h"
#include "Buzzer.h"
#include "DHT11.h"
#include "LightSensor.h"

/*温度报警阈值*/
#define TEMPERATURE_ALARM_THRESHOLD 30U

/* 主循环执行间隔 */
#define MAIN_LOOP_INTERVAL_MS 10U

/* 连续5秒未收到有效数据，则判定节点离线 */
#define NODE_OFFLINE_TIMEOUT_MS 5000U

/* 本地DHT11读取周期 */
#define LOCAL_DHT11_READ_INTERVAL_MS 2000U

/**
 * @brief 计算载荷字符串的XOR校验和
 * @param text 不带帧头、校验字段的原始载荷字符串
 * @retval 1字节XOR校验结果
 */
static uint8_t Communication_CalculateChecksum(const char *text)
{
 uint8_t checksum;
 uint16_t index;
 
 checksum = 0;
 index = 0;
 
 // 遍历直到字符串结束符'\0'
 while(text[index] != '\0')
 {
	 checksum ^= (uint8_t)text[index];
	 index ++;
 }
 
 return checksum;
}

/**
* @brief 对接收报文计算校验：只计算 ,C= 之前的载荷部分
* @param packet 已剥离@、\r\n的完整接收报文（包含,C=xxx）
* @retval 载荷部分XOR校验和
*/
static uint8_t Communication_CalculatePacketChecksum(const char*packet)
{
 uint8_t checksum;
 uint16_t index;
 
 checksum = 0;
 index = 0;
 
 while(packet[index] != '\0')
 {
	 // 检测到 ",C=" 标记，停止计算，后面是校验域不再参与运算
	 if((packet[index] == ',' ) && (packet[index + 1] == 'C') && packet[index + 2] == '=')
	 {
		 break;
	 }
	 
	 checksum ^= (uint8_t)packet[index];
	 index ++;
 }
 
 return checksum;
}
 
/**
* @brief 输入原始载荷，自动添加帧头、校验、帧尾并串口发送
* @param payload 业务载荷字符串，不含@、,C=xx、\r\n
* @retval None
*/
static void Communication_SendPayloadwithChecksum(const char *payload)
{
 uint8_t checksum;
 char frame[32];
 
 checksum = Communication_CalculateChecksum(payload);
 
 // 组装完整帧：@载荷,C=校验\r\n
 sprintf(frame,"@%s,C=%u\r\n",payload,(unsigned int)checksum);
 
 Serial_SendString(frame);
}
 
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
	OLED_ShowString(2,12,"L:");
    OLED_ShowString(3,1,"State:WAIT NODE");
//    OLED_ShowString(4,1,"NODE:N1 WAIT");	
	OLED_ShowString(4,1,"LOCAL DHT WAIT  ");
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
		Buzzer_On();
		OLED_ShowString(3,1,"State:TEMP HIGH ");
		alarmActive = 1U;
	}
	else
	{
		LED1_OFF();
		Buzzer_Off();
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
	char payload[16];
	
	sprintf(payload,"G1,ALARM=%u",(unsigned int)alarmActive);
	
	Communication_SendPayloadwithChecksum(payload);
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
	unsigned int parsedChecksum;
	int parseResult;
	
	/*
     * Serial_RxPacket已经由串口驱动过滤掉@、\r、\n
     * 收到有效报文格式： N1,T=31,H=45
     */
	parseResult = sscanf(Serial_RxPacket, "N1,T=%u,H=%u,C=%u", &parsedTemperature, &parsedHumidity, &parsedChecksum);
	
	if((parseResult != 3) ||
		(parsedChecksum > 255U) ||
	    (Communication_CalculatePacketChecksum(Serial_RxPacket) !=
	     (uint8_t)parsedChecksum))
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
	unsigned int parsedChecksum;
	int parseResult;
	
	//按照格式“N1,ERROR=%u”从接收缓存区提取错误码
	parseResult = sscanf(Serial_RxPacket,"N1,ERROR=%u,C=%u",&parsedErrorCode,&parsedChecksum);
	
	/*
 * 合法性校验条件，任意一条满足则报文无效，返回0
 * 1.parseResult !=2：sscanf未能成功读出2个字段，报文格式错误
 * 2.parsedErrorCode <1U：错误码小于1，不在定义范围
 * 3.parsedErrorCode >6U：错误码大于6，超出业务规定的错误编号(1~6)
 * 4.parsedChecksum >255U：XOR校验是8位，不能超过0‑255
 * 5.本地重新计算报文,C=之前载荷的XOR校验，和接收校验值比对不相等，说明传输出错
 */
	if((parseResult != 2) ||
		(parsedErrorCode < 1U) ||
     	(parsedErrorCode > 6U) ||
	    (parsedChecksum > 255U) ||
	    (Communication_CalculatePacketChecksum(Serial_RxPacket) !=
	    (uint8_t)parsedChecksum))
	{
		return 0;// 报文校验失败，返回0代表处理失败
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
		
//		OLED_ShowString(4,1,"NODE:N1 OK      ");
		
		/* 允许串口驱动接收下一包数据 */
		Serial_RxFlag = 0;
		
		return 1;
	}
	else if(Communication_ParseNodeErrorPacket(&sensorErrorCode) == 1)
	{
		/* 节点在线，但DHT11传感器读取失败 */
		LED1_ON();
		Buzzer_On();
		
		/* 清除远端节点上原有的温度报警 */
		Communication_SendAlarmCommand(0);
		
		OLED_ShowString(3,1,"State:DHT ERR   ");
		OLED_ShowNum(3,15,sensorErrorCode,1);
//		OLED_ShowString(4,1,"NODE:N1 DHT ERR");
		
		
		Serial_RxFlag = 0;
		
		return 1;
	}
	else
	{
		/* 接收到无效数据包，显示错误状态 */
		LED1_ON();
		Buzzer_On();
		OLED_ShowString(3,1,"State:PACKET ERR");
//		OLED_ShowString(4,1,"NODE:N1 ERROR   ");
		
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
	
	uint32_t lightDisplayTimeMs;
    uint8_t lightPercent;
	
	/* 本地DHT11采集变量 */
	uint32_t localDhtReadTimeMs;
	uint8_t localTemperature;
	uint8_t localHumidity;
	uint8_t localDhtErrorCode;
	
	LightSensor_Init();
	OLED_Init();
	Serial_Init();
	LED_Init();
	Buzzer_Init();
	DHT11_Init();
	
	lightDisplayTimeMs = 100U;
	
	/* 让主循环启动后立即读取一次本地DHT11 */
	localDhtReadTimeMs = LOCAL_DHT11_READ_INTERVAL_MS;
	localTemperature = 0U;
	localHumidity = 0U;
	localDhtErrorCode = 0U;
	
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
			Buzzer_Off();

			OLED_ShowString(3, 1, "State:OFFLINE   ");
//			OLED_ShowString(4, 1, "NODE:N1 OFFLINE ");
		}
		
		/* 定时读取本地DHT11，并临时显示在OLED第4行 */
		if(localDhtReadTimeMs >= LOCAL_DHT11_READ_INTERVAL_MS)
		{
			localDhtReadTimeMs = 0U;
			
			// 读取本地DHT11，保存错误码，0=成功，非0代表不同类型故障
			localDhtErrorCode = DHT11_ReadData(&localTemperature, &localHumidity);
			
			if(localDhtErrorCode == 0U)
			{
				// 读取成功：第4行显示本地温湿度
				OLED_ShowString(4, 1, "LOCAL T:");
				OLED_ShowNum(4, 9, localTemperature, 2);
				OLED_ShowString(4, 11, " H:");
				OLED_ShowNum(4, 14, localHumidity, 2);
			}
			else
			{
				/* 先清空整行，避免残留字符覆盖错误码 */
				OLED_ShowString(4, 1, "                ");
				
				// 读取失败：显示错误代号，同时用空格清除右侧旧湿度数字残留
				OLED_ShowString(4, 1, "LOCAL ERR:");
				OLED_ShowNum(4, 12, localDhtErrorCode, 1);
			}
		}
		else
		{
			localDhtReadTimeMs += MAIN_LOOP_INTERVAL_MS;
		}
		
		if(lightDisplayTimeMs >= 100U)// 定时100ms更新一次光照百分比显示，避免频繁读取ADC和刷屏
		{
			// 读取光照传感器换算后的百分比(0‑100)
			lightPercent = LightSensor_ReadPercent();
			// OLED第2行，第14列，显示3位数字光照百分比
			OLED_ShowNum(2,14,lightPercent,3);
			//计时清零，重新开始计时
			lightDisplayTimeMs = 0;
		}
		else
		{
			//主循环周期累加时间
			lightDisplayTimeMs += MAIN_LOOP_INTERVAL_MS;
		}
		
		Delay_ms(MAIN_LOOP_INTERVAL_MS);
	}
}
