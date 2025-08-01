#ifndef WIFI_TELEGRAM_CORE_H
#define WIFI_TELEGRAM_CORE_H

#include <Arduino.h>

// Functions to be executed on the second core
void wifiTelegramTask(void *pvParameters);
bool checkWiFiConnection();
void saveLastMessageId(long id);
long loadLastMessageId();

#endif // WIFI_TELEGRAM_CORE_H