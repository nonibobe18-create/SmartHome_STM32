#ifndef __ESP8266_H
#define __ESP8266_H

void ESP8266_Init(void);
void ESP8266_SendByte(unsigned char byte);
void ESP8266_SendString(const char *string);
unsigned char ESP8266_TestConnection(unsigned long timeout_ms);
unsigned char ESP8266_ConnectWiFi(void);

#endif
