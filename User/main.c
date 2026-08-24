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
#include "ESP8266.h"
#include "NetworkConfig.h"
#include "stm32f10x_tim.h"
#include "misc.h"
#include "MQ2.h"

/*温度报警阈值*/
#define TEMPERATURE_ALARM_THRESHOLD 30U

/* 主循环执行间隔 */
#define MAIN_LOOP_INTERVAL_MS 10U

/* 连续5秒未收到有效数据，则判定节点离线 */
#define NODE_OFFLINE_TIMEOUT_MS 5000U

/* 本地DHT11读取周期 */
#define LOCAL_DHT11_READ_INTERVAL_MS 2000U

/* DHT11单次读取失败后的重试配置 */
#define LOCAL_DHT11_RETRY_COUNT 3U
#define LOCAL_DHT11_RETRY_INTERVAL_MS 100U
#define LOCAL_DHT11_FAIL_LIMIT 3U

/* 本地环境数据TCP上报周期 */
#define TCP_ENVIRONMENT_REPORT_INTERVAL_MS 5000U

/* 光照自动窗帘的滞回阈值 */
#define CURTAIN_LIGHT_OPEN_THRESHOLD  30U
#define CURTAIN_LIGHT_CLOSE_THRESHOLD 70U

#define CURTAIN_AUTO_UNKNOWN 0U
#define CURTAIN_AUTO_OPEN    1U
#define CURTAIN_AUTO_CLOSE   2U

/* TCP服务器连接失败后的最大重试次数 */
#define ESP8266_TCP_RETRY_COUNT 3U

/* 两次TCP连接尝试之间的等待时间 */
#define ESP8266_TCP_RETRY_INTERVAL_MS 1000U

/* 网络断线后的下一次重连间隔 */
#define ESP8266_RECONNECT_INTERVAL_MS 10000U

/* MQ-2 sampling and alarm parameters */
#define MQ2_READ_INTERVAL_MS       500U    // MQ‑2采样周期，500ms读取一次ADC
#define MQ2_SAMPLE_COUNT           8U      // 滑动采样点数，取平均，抑制ADC噪声
#define MQ2_ALARM_ON_THRESHOLD     1600U   // 报警开启阈值；ADC＞1600，满足报警条件
#define MQ2_ALARM_OFF_THRESHOLD    1200U   // 报警关闭阈值；ADC＜1200，解除报警
#define MQ2_CONFIRM_COUNT          3U      // 确认计数；连续N次满足条件才切换报警状态，防误触发

static volatile uint32_t EnvironmentSystemTimeMs;

static uint8_t RemoteTemperatureAlarmActive;
static uint8_t SmokeAlarmActive;

/**
 * @brief 初始化TIM2，产生1ms系统时间基准
 * @param None
 * @retval None
 */
static void Environment_TimeBaseInit(void)
{
	TIM_TimeBaseInitTypeDef timerInit;
	NVIC_InitTypeDef nvicInit;

	/* APB1定时器时钟为72MHz，配置为1ms中断一次 */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	TIM_TimeBaseStructInit(&timerInit);
	timerInit.TIM_Prescaler = 72U - 1U;
	timerInit.TIM_Period = 1000U - 1U;
	timerInit.TIM_CounterMode = TIM_CounterMode_Up;
	timerInit.TIM_ClockDivision = TIM_CKD_DIV1;

	TIM_TimeBaseInit(TIM2, &timerInit);
	TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

	nvicInit.NVIC_IRQChannel = TIM2_IRQn;
	nvicInit.NVIC_IRQChannelPreemptionPriority = 2;
	nvicInit.NVIC_IRQChannelSubPriority = 0;
	nvicInit.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvicInit);

	EnvironmentSystemTimeMs = 0U;
	TIM_Cmd(TIM2, ENABLE);
}

/**
 * @brief TIM2更新中断处理函数
 * @param None
 * @retval None
 */
void TIM2_IRQHandler(void)
{
	if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
	{
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
		EnvironmentSystemTimeMs++;
	}
}

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
 * @brief 根据所有报警源状态更新本地LED与蜂鸣器输出
 * @param 无
 * @retval 无
 */
static void Environment_UpdateAlarmOutput(void)
{
    // 远程温度报警 或者 烟雾报警任意一个激活，触发声光报警
    if ((RemoteTemperatureAlarmActive != 0U) ||
        (SmokeAlarmActive != 0U))
    {
        LED1_ON();        // 报警LED点亮
        Buzzer_On();      // 蜂鸣器开启
    }
    else
    {
        LED1_OFF();       // 全部报警清除，LED熄灭
        Buzzer_Off();     // 关闭蜂鸣器
    }
}


/**
*@brief  刷新OLED环境数据显示，同时执行温度报警逻辑
*@param  temperature：采集得到的温度
*@param  humidity：采集得到的相对湿度
*@retval 1：温度报警触发，0：温度处于正常范围
*/
static uint8_t Environment_Update(uint8_t temperature, uint8_t humidity)
{
	uint8_t alarmActive = 0U;              //报警状态标志，初始为0（无警报）
	
	OLED_ShowNum(1,7,temperature,2);     
    OLED_ShowString(1,10,"C");
	
	OLED_ShowNum(2,7,humidity,2);
	OLED_ShowString(2,10,"%");
	
	//判断温度是否大于等于报警阈值
	if(temperature >= TEMPERATURE_ALARM_THRESHOLD)
	{
		RemoteTemperatureAlarmActive = 1U;
		alarmActive = 1U;
		OLED_ShowString(3,1,"State:TEMP HIGH ");
	}
	else
	{
		RemoteTemperatureAlarmActive = 0U;
		OLED_ShowString(3,1,"State:NORMAL    ");
	}
	
	Environment_UpdateAlarmOutput();
	
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
 * @brief 向51节点发送窗帘控制命令
 * @param command 命令字符串：OPEN（打开）、CLOSE（关闭）、STOP（停止）
 * @retval 无
 */
static void Communication_SendCurtainCommand(const char *command)
{
	char payload[24];    //定义数组，存放待发送不带校验和的命令载荷
	
	//格式化组装协议内容，例如：G1,CURTAIN=OPEN
	sprintf(payload,
	        "G1,CURTAIN=%s",
	        command);
	
	//追加校验和，完成组帧并通过串口发送出去
	Communication_SendPayloadwithChecksum(payload);
}

/**
 * @brief 生成本地环境数据TCP报文
 * @param dataValid 本地DHT11数据是否有效
 * @param temperature 本地温度
 * @param humidity 本地湿度
 * @param lightPercent 光照百分比
 * @param alarmActive 本地温度报警状态
 * @param errorCode DHT11错误码
 * @param packet 输出的完整TCP报文
 * @retval None
 */
static void Communication_BuildEnvironmentPacket(
	uint8_t dataValid,
	uint8_t temperature,
	uint8_t humidity,
	uint8_t lightPercent,
	uint8_t alarmActive,
	uint8_t errorCode,
	char *packet)
{
	char payload[48];
	uint8_t checksum;

	if(dataValid == 1U)
	{
		sprintf(payload,
		        "STM32,T=%u,H=%u,L=%u,ALARM=%u",
		        (unsigned int)temperature,
		        (unsigned int)humidity,
		        (unsigned int)lightPercent,
		        (unsigned int)alarmActive);
	}
	else
	{
		sprintf(payload,
		        "STM32,DHT_ERR=%u",
		        (unsigned int)errorCode);
	}

	checksum = Communication_CalculateChecksum(payload);

	sprintf(packet,
	        "@%s,C=%u\r\n",
	        payload,
	        (unsigned int)checksum);
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
+- * 合法性校验条件，任意一条满足则报文无效，返回0
+- * 1.parseResult !=2：sscanf未能成功读出2个字段，报文格式错误
+- * 2.parsedErrorCode <1U：错误码小于1，不在定义范围
+- * 3.parsedErrorCode >6U：错误码大于6，超出业务规定的错误编号(1~6)
+- * 4.parsedChecksum >255U：XOR校验是8位，不能超过0‑255
+- * 5.本地重新计算报文,C=之前载荷的XOR校验，和接收校验值比对不相等，说明传输出错
+- */
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
	uint8_t sensorErrorCode;
	
	if(Serial_RxFlag == 0)
	{
		return 0;
	}
	
	if(Communication_ParseNodePacket(&temperature, &humidity) == 1)
	{
		/* 刷新显示，并更新温度报警状态。 */
		Environment_Update(temperature, humidity);
		
		/* 将报警状态回传给51节点。 */
		Communication_SendAlarmCommand((RemoteTemperatureAlarmActive != 0U) ||
                                       (SmokeAlarmActive != 0U));
		
//		OLED_ShowString(4,1,"NODE:N1 OK      ");
		
		/* 允许串口驱动接收下一包数据 */
		Serial_RxFlag = 0;
		
		return 1;
	}
	else if(Communication_ParseNodeErrorPacket(&sensorErrorCode) == 1)
	{
		/*
		 * @brief 清除远程温度报警，但保留本地烟雾报警状态
		 */
		RemoteTemperatureAlarmActive = 0U;
		Environment_UpdateAlarmOutput();

		/* 将本地MQ-2报警状态同步给51节点 */
		Communication_SendAlarmCommand((SmokeAlarmActive != 0U));
		
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
 * @brief 带有限重试次数的本地DHT11读取
 * @param temperature 输出参数，温度（摄氏度）
 * @param humidity    输出参数，相对湿度（百分比）
 * @retval 0 读取成功；否则返回最后一次DHT11的错误码
 */
static uint8_t App_ReadLocalDht11WithRetry(uint8_t *temperature,
                                           uint8_t *humidity)
{
    uint8_t retryIndex;        // 重试计数器
    uint8_t readStatus;       // DHT11读取状态

    readStatus = 1U;          // 初始化为错误状态

    // 循环：最多重试 LOCAL_DHT11_RETRY_COUNT 次
    for (retryIndex = 0U;
         retryIndex < LOCAL_DHT11_RETRY_COUNT;
         retryIndex++)
    {
        // 调用底层接口读取温湿度
        readStatus = DHT11_ReadData(temperature, humidity);

        if (readStatus == 0U)
        {
            return 0U;         // 读取成功，直接返回0，结束函数
        }

        // 不是最后一次重试，则延时后再尝试
        if ((retryIndex + 1U) < LOCAL_DHT11_RETRY_COUNT)
        {
            Delay_ms(LOCAL_DHT11_RETRY_INTERVAL_MS);
        }
    }

    // 全部重试都失败，返回最后一次的错误码
    return readStatus;
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
	
	uint8_t keyNum;
	
	uint8_t curtainAutoState;
	
	uint32_t lightDisplayTimeMs;
    uint8_t lightPercent;
	
	/* 本地DHT11采集变量 */
	uint32_t localDhtReadTimeMs;
	uint8_t localTemperature;
	uint8_t localHumidity;
	uint8_t localDhtErrorCode;
	uint8_t localDhtDataValid;
	uint8_t localDhtFailCount;

	uint32_t tcpReportTimeMs;
	uint8_t tcpAlarmStatus;
	char tcpEnvironmentPacket[64];

	uint8_t esp8266Status;
	uint8_t esp8266ResetStatus;
	uint8_t wifiConnectStatus;
	uint8_t tcpConnectStatus;
	uint8_t tcpSendStatus;
	uint8_t tcpRetryCount;
	uint32_t esp8266ReconnectTimeMs;
	uint32_t mq2ReadTimeMs;
	uint16_t mq2AverageRaw;
	uint8_t smokeAlarmOnCount;
	uint8_t smokeAlarmOffCount;
	
	LightSensor_Init();
	MQ2_Init();
	OLED_Init();
	Serial_Init();
	ESP8266_Init();
	LED_Init();
	Buzzer_Init();
	DHT11_Init();
	Key_Init();
	Environment_TimeBaseInit();
	
	lightDisplayTimeMs = 100U;
	
	/*
	 * WiFi启动状态需要先保持显示，
	 * 本地DHT11在进入主循环约2秒后首次读取。
	 */
	localDhtReadTimeMs = 0U;
	localTemperature = 0U;
	localHumidity = 0U;
	localDhtErrorCode = 0U;
	localDhtDataValid = 0U;
	localDhtFailCount = 0U;
	
	mq2ReadTimeMs = MQ2_READ_INTERVAL_MS;
	mq2AverageRaw = 0U;
	SmokeAlarmActive = 0U;
	smokeAlarmOnCount = 0U;
	smokeAlarmOffCount = 0U;

	tcpReportTimeMs = 0U;
	tcpAlarmStatus = 0U;
	tcpEnvironmentPacket[0] = '\0';
	
	RemoteTemperatureAlarmActive = 0U;
	
	curtainAutoState = CURTAIN_AUTO_UNKNOWN;

	Environment_DispalyStaticText();
	
	/*
	 * ESP8266只执行一次复位、AT检测和WiFi入网。
	 * WiFi成功后，只对TCP服务器连接进行有限次数重试。
	 */
	esp8266ResetStatus = 0U;
	esp8266Status = 0U;
	wifiConnectStatus = 0U;
	tcpConnectStatus = 0U;
	tcpSendStatus = 0U;
	/* 允许主循环启动后按周期检查网络重连 */
	esp8266ReconnectTimeMs = EnvironmentSystemTimeMs;
	Delay_ms(2000U);
	
	esp8266ResetStatus = ESP8266_Reset();
	
	if(esp8266ResetStatus == 1U)
	{
		esp8266Status = ESP8266_TestConnection(5000U);

		if(esp8266Status == 1U)
		{
			wifiConnectStatus = ESP8266_ConnectWiFi();

			if(wifiConnectStatus == 1U)
			{
				for(tcpRetryCount = 0U;
					tcpRetryCount < ESP8266_TCP_RETRY_COUNT;
					tcpRetryCount++)
				{
					tcpConnectStatus = ESP8266_ConnectTcpServer(
						TCP_SERVER_IP,
						TCP_SERVER_PORT);

					if(tcpConnectStatus == 1U)
					{
						tcpSendStatus = ESP8266_SendTcpData(
							"@STM32,TCP=OK\r\n");

						if(tcpSendStatus == 1U)
						{
							break;
						}
					}

					Delay_ms(ESP8266_TCP_RETRY_INTERVAL_MS);
				}
			}
		}
	}
	 
	 /* 根据最终结果显示启动网络状态 */
	 if(tcpSendStatus == 1U)
	 {
		 OLED_ShowString(4,1,"TCP SEND OK     ");
	 }
	 else if(wifiConnectStatus == 2U)
	{
		OLED_ShowString(4, 1, "WIFI MODE ERR   ");
	}
	else if(wifiConnectStatus == 3U)
	{
		OLED_ShowString(4, 1, "WIFI JOIN ERR   ");
	}
	else if(esp8266ResetStatus == 0U)
	{
		OLED_ShowString(4, 1, "WIFI RST ERR    ");
	}
	else if(esp8266Status == 0U)
	{
		OLED_ShowString(4, 1, "WIFI AT ERR     ");
	}
	 else if(tcpConnectStatus == 0U)
	 {
		 OLED_ShowString(4,1,"TCP CONN ERR    ");
	 }
	 else
	 {
		 OLED_ShowString(4,1,"TCP SEND ERR    ");
	 }
	
	 /* 从网络初始化完成后开始计算环境数据上报周期 */
	tcpReportTimeMs = EnvironmentSystemTimeMs;
	 
	/* 初始为离线状态，直到接收到节点有效数据包 */
	nodeOfflineTimeMs = NODE_OFFLINE_TIMEOUT_MS;
	
	while(1)
	{
		/*
		 * @brief 读取STM32本地按键，并发送窗帘控制命令
		 * @param 无
		 * @retval 无
		 */
		keyNum = Key_GetNum();                   // 获取按键编号，1代表按键1按下，2代表按键2按下
		
		if(keyNum == 1U)
		{
			Communication_SendCurtainCommand("OPEN");
			curtainAutoState = CURTAIN_AUTO_OPEN;
			OLED_ShowString(4,1,"CURTAIN OPEN   ");
		}
		else if(keyNum == 2U)
		{
			Communication_SendCurtainCommand("CLOSE");
			curtainAutoState = CURTAIN_AUTO_CLOSE;
			OLED_ShowString(4,1,"CURTAIN CLOSE  ");
		}
		
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
			/*
			 * @brief 节点离线时清除远程温度报警
			 *        但不能影响本地MQ-2烟雾报警。
			 */
			RemoteTemperatureAlarmActive = 0U;
			Environment_UpdateAlarmOutput();

			OLED_ShowString(3, 1, "State:OFFLINE   ");
//			OLED_ShowString(4, 1, "NODE:N1 OFFLINE ");
		}
		
		/* 定时读取本地DHT11，并临时显示在OLED第4行 */
		if(localDhtReadTimeMs >= LOCAL_DHT11_READ_INTERVAL_MS)
		{
			localDhtReadTimeMs = 0U;
			
			// 读取本地DHT11，保存错误码，0=成功，非0代表不同类型故障
			localDhtErrorCode =App_ReadLocalDht11WithRetry(&localTemperature,&localHumidity);
			
			if(localDhtErrorCode == 0U)
			{
				localDhtDataValid = 1U;
				
				localDhtFailCount = 0U;

				if(localTemperature >= TEMPERATURE_ALARM_THRESHOLD)
				{
					tcpAlarmStatus = 1U;
				}
				else
				{
					tcpAlarmStatus = 0U;
				}
				// 读取成功：第4行显示本地温湿度
				OLED_ShowString(4, 1, "LOCAL T:");
				OLED_ShowNum(4, 9, localTemperature, 2);
				OLED_ShowString(4, 11, " H:");
				OLED_ShowNum(4, 14, localHumidity, 2);
			}
			else
			{
				if (localDhtFailCount < LOCAL_DHT11_FAIL_LIMIT)
				{
					localDhtFailCount++;
				}

				/*
				 * A single failed read is treated as a transient error.
				 * Only consecutive failures invalidate the local data.
				 */
				if (localDhtFailCount < LOCAL_DHT11_FAIL_LIMIT)
				{
					OLED_ShowString(4, 1,
									"LOCAL DHT RETRY ");
				}
				else
				{
					localDhtDataValid = 0U;

					OLED_ShowString(4, 1,
									"                ");

					OLED_ShowString(4, 1,
									"LOCAL ERR:");

					OLED_ShowNum(4, 12,
								 localDhtErrorCode,
								 1);
				}
			}
		}
		else
		{
			localDhtReadTimeMs += MAIN_LOOP_INTERVAL_MS;
		}
		
		/*
		 * @brief 检测TCP断线并周期性恢复WiFi和TCP连接
		 * @param 无
		 * @retval 无
		 * @note 每10秒最多执行一次重连尝试，避免阻塞主循环。
		 */
		if ((tcpConnectStatus == 0U) &&
			((uint32_t)(EnvironmentSystemTimeMs - esp8266ReconnectTimeMs)
			 >= ESP8266_RECONNECT_INTERVAL_MS))
		{
			esp8266ReconnectTimeMs = EnvironmentSystemTimeMs;

			/*
			 * WiFi状态无效时重新执行入网。
			 * ESP8266_ConnectWiFi内部具有最大超时时间。
			 */
			if (wifiConnectStatus != 1U)
			{
				wifiConnectStatus = ESP8266_ConnectWiFi();
			}

			if (wifiConnectStatus == 1U)
			{
				tcpConnectStatus = ESP8266_ConnectTcpServer(
					TCP_SERVER_IP,
					TCP_SERVER_PORT);

				if (tcpConnectStatus == 1U)
				{
					tcpSendStatus = ESP8266_SendTcpData(
						"@STM32,TCP=RECONNECT\r\n");

					if (tcpSendStatus != 1U)
					{
						tcpConnectStatus = 0U;
					}
				}
				else
				{
					/*
					 * TCP连接失败，下一次重连周期重新检查WiFi。
					 */
					wifiConnectStatus = 0U;
				}
			}
		}
		if((uint32_t)(EnvironmentSystemTimeMs - tcpReportTimeMs)
			>= TCP_ENVIRONMENT_REPORT_INTERVAL_MS)
		{
			tcpReportTimeMs = EnvironmentSystemTimeMs;

			if(tcpConnectStatus == 1U)
			{
				Communication_BuildEnvironmentPacket(
					localDhtDataValid,
					localTemperature,
					localHumidity,
					lightPercent,
					tcpAlarmStatus,
					localDhtErrorCode,
					tcpEnvironmentPacket);

				tcpSendStatus = ESP8266_SendTcpData(
					tcpEnvironmentPacket);
				if (tcpSendStatus != 1U)
				{
					/*
					 * 发送失败表示当前TCP连接不可用，
					 * 下一次重连周期重新建立连接。
					 */
					tcpConnectStatus = 0U;
				}
			}
		}
		if(lightDisplayTimeMs >= 100U)
		{
			// 读取光照传感器，得到光照百分比 0~100
			lightPercent = LightSensor_ReadPercent();
			
			// OLED显示光照百分比数值，3位数字
			OLED_ShowNum(2,14,lightPercent,3);
			
			/*
			 * @brief 根据光照强度执行窗帘自动控制
			 * @param 无
			 * @retval 无
			 * @note 强光关闭窗帘；弱光打开窗帘。
			 *       传感器输出为反向特性：L数值越小，光照越强。
			 */
			// 光照百分比 ≤开窗帘阈值，且当前自动状态不是关闭
			if ((lightPercent <= CURTAIN_LIGHT_OPEN_THRESHOLD) &&
				(curtainAutoState != CURTAIN_AUTO_CLOSE))
			{
				/*
				 * L<=30 代表光照很强
				 * 关闭窗帘，减少强光直射与热量
				 */
				Communication_SendCurtainCommand("CLOSE");      // 发送关闭窗帘命令
				curtainAutoState = CURTAIN_AUTO_CLOSE;          // 更新自动状态：自动关闭

				OLED_ShowString(4, 1, "AUTO CURTAIN CL ");      // OLED显示自动关闭提示
			}
			// 光照百分比 ≥关窗帘阈值，且当前自动状态不是打开
			else if ((lightPercent >= CURTAIN_LIGHT_CLOSE_THRESHOLD) &&
					 (curtainAutoState != CURTAIN_AUTO_OPEN))
			{
				/*
				 * L>=70 代表光照很弱
				 * 打开窗帘，充分利用自然光
				 */
				Communication_SendCurtainCommand("OPEN");      // 发送打开窗帘命令
				curtainAutoState = CURTAIN_AUTO_OPEN;           // 更新自动状态：自动打开

				OLED_ShowString(4, 1, "AUTO CURTAIN OP ");      // OLED显示自动打开提示
			}


			
			lightDisplayTimeMs = 0U;        // 计时清零，开启下一轮100ms计时
		}
		else
		{
			//主循环周期累加时间
			lightDisplayTimeMs += MAIN_LOOP_INTERVAL_MS;
		}
		
		/*
		 * @brief 读取MQ‑2并更新烟雾报警状态
		 * @param 无
		 * @retval 无
		 */
		if(mq2ReadTimeMs >= MQ2_READ_INTERVAL_MS)
		{
			mq2ReadTimeMs = 0U;
			
			mq2AverageRaw = MQ2_ReadAverage(MQ2_SAMPLE_COUNT);
			
			if(SmokeAlarmActive == 0U)          // 当前状态：正常
			{
				smokeAlarmOffCount = 0U;
				
				if(mq2AverageRaw >= MQ2_ALARM_ON_THRESHOLD)
				{
					if(smokeAlarmOnCount < MQ2_CONFIRM_COUNT)
					{
						smokeAlarmOnCount ++;    // 超标，累加报警确认计数
					}
					
					if(smokeAlarmOnCount >= MQ2_CONFIRM_COUNT)
					{
						SmokeAlarmActive = 1U;        // 触发烟雾报警
						smokeAlarmOnCount = 0U;
						
						Environment_UpdateAlarmOutput();

                        Communication_SendAlarmCommand(1U);
					}
				}
				else
				{
					smokeAlarmOnCount = 0U;      // 未超标，计数器清零
					
				}
			}
			else                           // 当前状态：已经报警
			{
				smokeAlarmOnCount = 0U;
				
				if(mq2AverageRaw <= MQ2_ALARM_OFF_THRESHOLD)
				{
					if(smokeAlarmOffCount < MQ2_CONFIRM_COUNT)
					{
						smokeAlarmOffCount ++;        // 低于解除阈值，累加解除计数
					}
					
					if(smokeAlarmOffCount >= MQ2_CONFIRM_COUNT)
					{
						SmokeAlarmActive = 0U;             // 解除报警
						smokeAlarmOffCount = 0U;
						
						Environment_UpdateAlarmOutput();

						Communication_SendAlarmCommand((RemoteTemperatureAlarmActive != 0U));
					}
				}
				
				// else：仍然高于关闭阈值，保持报警状态，不做处理
			}
		}
		else
		{
			mq2ReadTimeMs += MAIN_LOOP_INTERVAL_MS;   //计时累加
		}
		
		/*
		 * @brief 显示当前网络状态
		 * @param 无
		 * @retval 无
		 * @note TCP断开时优先显示TCP OFFLINE，
		 *       不把TCP服务器断开误判为WiFi断开。
		 */
		if (tcpConnectStatus == 0U)
		{
			OLED_ShowString(4, 1, "TCP OFFLINE     ");
		}
		Delay_ms(MAIN_LOOP_INTERVAL_MS);
	}
}
