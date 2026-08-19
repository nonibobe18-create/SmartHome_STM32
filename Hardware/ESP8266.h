#ifndef __ESP8266_H
#define __ESP8266_H

void ESP8266_Init(void);
void ESP8266_SendByte(unsigned char byte);
void ESP8266_SendString(const char *string);
unsigned char ESP8266_TestConnection(unsigned long timeout_ms);
unsigned char ESP8266_ConnectWiFi(void);
unsigned char ESP8266_ConnectTcpServer(const char *server_ip,unsigned int server_port);
unsigned char ESP8266_SendTcpData(const char *data);
unsigned char ESP8266_Reset(void);

#endif
