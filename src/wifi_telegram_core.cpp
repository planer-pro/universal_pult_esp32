#include "wifi_telegram_core.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <SD.h>
#include "freertos/queue.h"

// Макрос для отладки
#define DEBUG_TELEGRAM true

uint16_t GetNewMessagesDelay = 200; // Задержка при получении новых сообщений в Telegram

// External variables and functions from main.cpp
extern void displayInfo(int posY, String nfo, unsigned long displayTime = 0, bool clearScreen = true);
extern void displayMainMenu();
extern volatile bool networkInitialized;
extern QueueHandle_t telegramQueue;
extern QueueHandle_t commandQueue;
extern volatile bool btnPressed;    // Флаг для режима обучения
extern volatile bool clearAllCodes; // Флаг для очистки кодов

// WiFi and Telegram objects
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// Forward declarations
bool connectToWiFi();
void GetNewMessages(int numNewMessages);
void parseCommand(String text);
void saveLastMessageId(long id);
long loadLastMessageId();
void internalSendAnswer(String text); // Renamed to avoid conflicts

void wifiTelegramTask(void *pvParameters)
{
    // Initialize WiFi
    displayInfo(1, F("Connecting to WiFi..."));
    displayInfo(2, WIFI_SSID, 1000, false);

    secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org

    if (!connectToWiFi())
    {
        displayInfo(1, F("WiFi connection failed!"));
        displayInfo(2, F("Check credentials or signal"), 0, false);
        displayInfo(3, F("Restarting..."), 1000, false);
        ESP.restart();
    }

    displayInfo(1, F("WiFi connected! "));
    displayInfo(2, "IP:" + WiFi.localIP().toString(), 2000, false);

    // Загружаем ID последнего сообщения и устанавливаем его для бота
    long last_id = loadLastMessageId();
    if (last_id > 0)
    {
        bot.last_message_received = last_id;
    }

    internalSendAnswer(F("IR Remote Control System\nVersion 1.0"));
    parseCommand(F("/help"));

    // Signal to the main core that the network is ready
    networkInitialized = true;

    // Main loop for this core
    const unsigned long wifiCheckInterval = 1000; // 1 second
    unsigned long lastWifiCheckTime = millis() + wifiCheckInterval;

    for (;;)
    {
        // 1. Проверка подключения к WiFi
        if (millis() - lastWifiCheckTime > wifiCheckInterval)
        {
            lastWifiCheckTime = millis();
            if (!checkWiFiConnection())
            {
                continue; // Пропускаем остальную часть цикла
            }
        }

        // 2. Работа с Telegram, только если есть WiFi
        if (WiFi.status() == WL_CONNECTED)
        {
            // Check for incoming commands from Telegram
            static uint32_t tmTeleg = millis() + GetNewMessagesDelay;

            if (millis() - tmTeleg > GetNewMessagesDelay)
            {
                tmTeleg = millis();
                int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

                while (numNewMessages)
                {
                    GetNewMessages(numNewMessages);
                    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
                }
            }

            // Check for outgoing messages from the main core
            String *pText = NULL;
            if (xQueueReceive(telegramQueue, &pText, 0) == pdTRUE)
            {
                if (pText != NULL)
                {
                    internalSendAnswer(*pText);
                    delete pText; // Free the memory
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Задержка для экономии ресурсов
    }
}

bool connectToWiFi()
{
    int maxAttempts = 5;
    int attemptTimeout = 20;

    for (int attempt = 1; attempt <= maxAttempts; attempt++)
    {
        String attemptMsg = "WiFi attempt " + String(attempt) + "/" + String(maxAttempts);
        displayInfo(1, attemptMsg, 0);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        int timeoutCounter = 0;
        while (WiFi.status() != WL_CONNECTED && timeoutCounter < attemptTimeout * 2)
        {
            delay(500);
            timeoutCounter++;

            if (timeoutCounter % 4 == 0)
            {
                String progress = "";
                for (int i = 0; i < (timeoutCounter / 4); i++)
                    progress += ".";
                displayInfo(2, progress, 0, false);
            }
        }

        if (WiFi.status() == WL_CONNECTED)
            return true;

        if (maxAttempts != attempt)
            displayInfo(2, F("Retrying in 5 seconds..."), 5000, false);
    }

    return false; // Эта строка никогда не выполнится из-за перезапуска
}

void GetNewMessages(int numNewMessages)
{
    for (int i = 0; i < numNewMessages; i++)
    {
        if (i >= 0 && i < numNewMessages)
        {
            telegramMessage &msg = bot.messages[i];
            parseCommand(msg.text);
        }
    }
}

void parseCommand(String text)
{
    // Проверяем, является ли команда числом
    int commandID = text.toInt();
    if (commandID > 0)
    {
        // Отправляем ID в очередь
        if (xQueueSend(commandQueue, &commandID, portMAX_DELAY) != pdPASS)
        {
            internalSendAnswer(F("Error: Command queue is full"));
        }
    }
    else
    {
        // Обработка текстовых команд
        if (text.equalsIgnoreCase(F("/help")))
        {
            internalSendAnswer(F("Available commands:\n- Send a number to execute IR code\n- /help - Show this help\n- /learn - Start IR code learning mode\n- /allclear - Delete all saved codes\n- /status - Show system status\n- /restart - Restart device\n- /memory - Show free memory"));
        }
        else if (text.equalsIgnoreCase("/status"))
        {
            internalSendAnswer("System status:\n- WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") +
                               "\n- IP: " + WiFi.localIP().toString());
        }
        else if (text.equalsIgnoreCase("/restart"))
        {
            internalSendAnswer(F("Restarting device..."));
            saveLastMessageId(bot.last_message_received); // Сохраняем ID последнего сообщения
            vTaskDelay(pdMS_TO_TICKS(1000));              // Добавляем задержку в 1 секунду, чтобы сообщение успело отправиться
            ESP.restart();
        }
        else if (text.equalsIgnoreCase(F("/memory")))
        {
            int freeHeap = ESP.getFreeHeap();
            internalSendAnswer("Free memory: " + String(freeHeap) + " bytes");
        }
        else if (text.equalsIgnoreCase(F("/learn")))
        {
            btnPressed = true;
        }
        else if (text.equalsIgnoreCase(F("/allclear")))
        {
            clearAllCodes = true;
            internalSendAnswer(F("Command to delete all codes received. The file will be deleted shortly."));
        }
        else
        {
            internalSendAnswer(F("Unknown command. Send a number to execute IR code or /help for help."));
        }
    }
}

void internalSendAnswer(String text)
{
    int maxRetries = 3;

    for (int i = 0; i < maxRetries; i++)
    {
        if (bot.sendMessage(CHAT_ID, text, "Markdown"))
        {
            if (DEBUG_TELEGRAM)
            {
                Serial.print(F("Message sent successfully: "));
                Serial.println(text);
            }

            return; // Успешная отправка
        }

        if (DEBUG_TELEGRAM)
        {
            Serial.print(F("Failed to send message (attempt "));
            Serial.print(i + 1);
            Serial.println(F(")"));
        }

        vTaskDelay(pdMS_TO_TICKS(250)); // Небольшая неблокирующая задержка 250 мс
    }

    // Если все попытки неудачны, можно отправить сообщение об ошибке
    if (DEBUG_TELEGRAM)
    {
        Serial.println("Error: Failed to send message after " + String(maxRetries) + " attempts");
    }
}
void saveLastMessageId(long id)
{
    File file = SD.open("/last_msg_id.txt", FILE_WRITE);
    if (file)
    {
        file.print(id);
        file.close();
        if (DEBUG_TELEGRAM)
            Serial.println("Saved last message ID: " + String(id));
    }
    else
    {
        if (DEBUG_TELEGRAM)
            Serial.println("Failed to open last_msg_id.txt for writing");
    }
}

long loadLastMessageId()
{
    if (SD.exists("/last_msg_id.txt"))
    {
        File file = SD.open("/last_msg_id.txt", FILE_READ);
        if (file)
        {
            String id_str = file.readString();
            file.close();
            long id = atol(id_str.c_str()); // Используем atol для преобразования в long
            if (DEBUG_TELEGRAM)
                Serial.println("Loaded last message ID: " + String(id));
            return id;
        }
    }
    if (DEBUG_TELEGRAM)
        Serial.println("No last message ID file found.");
    return 0; // Возвращаем 0, если файл не найден
}

bool checkWiFiConnection()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        // internalSendAnswer(F("WiFi disconnected. Reconnecting..."));

        displayInfo(1, F("WiFi disconnected!"));
        displayInfo(2, F("Reconnecting..."), 1000, false);

        if (connectToWiFi())
        {
            internalSendAnswer("WiFi reconnected. IP: " + WiFi.localIP().toString());

            displayInfo(1, F("WiFi reconnected!"));
            displayInfo(2, "IP: " + WiFi.localIP().toString(), 2000, false);
            displayMainMenu();
            return true;
        }
        else
        {
            // displayInfo(1, F("Reconnect failed!"), 2000);
            // return false;
            // Если все попытки неудачны, перезапускаем устройство
            // internalSendAnswer(F("WiFi connection failed. Restarting device..."));

            displayInfo(1, F("WiFi connection failed!"));
            displayInfo(2, F("Restarting device..."), 1000, false);
            ESP.restart();
        }
    }
    return true;
}
