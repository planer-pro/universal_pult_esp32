#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <EncButton.h>
#include <SD.h>
#include <SPI.h>
#include <LiquidCrystal_I2C.h>
#include <GyverOLED.h>
#include "config.h"
#include "wifi_telegram_core.h"

// --- Выбор типа дисплея ---
#define USE_LCD_DISPLAY // Использовать LCD 20x4
// #define USE_OLED_DISPLAY // Использовать OLED 128x64 SSH1106

// --- Пины для ESP32 WROWER ---
#define IR_RECEIVE_PIN 15 // GPIO15 для ИК-приемника
#define IR_SEND_PIN 2     // GPIO2 для ИК-передатчика
#define BUTTON_PIN 25     // GPIO25 для кнопки
#define SD_CS_PIN 4       // GPIO4 для CS SD-карты

// Пины для OLED дисплея
#define OLED_SDA_PIN 21 // GPIO21 для SDA (I2C)
#define OLED_SCL_PIN 22 // GPIO22 для SCL (I2C)
#define OLED_RST_PIN -1 // Не используется, -1 если не подключен
#define OLED_ADDR 0x3C  // I2C адрес OLED дисплея
#define OLED_WIDTH 128  // Ширина OLED
#define OLED_HEIGHT 64  // Высота OLED

#define LCD_COLS 20 // Ширина LCD
#define LCD_ROWS 4  // Высота LCD

// --- Переменные ---
volatile bool btnPressed = false;    // Флаг нажатия кнопки
volatile bool clearAllCodes = false; // Флаг для очистки кодов по команде

// Флаг готовности сетевого подключения на втором ядре
volatile bool networkInitialized = false;

#include "freertos/queue.h"
QueueHandle_t telegramQueue;
QueueHandle_t commandQueue;

// Кэш данных с SD-карты
String *codesData = NULL;
int codesCount = 0;

// Мьютекс для синхронизации доступа к общим ресурсам
SemaphoreHandle_t xMutex;

// Объекты
LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS);
GyverOLED<SSH1106_128x64> oled;
Button btn(BUTTON_PIN, INPUT_PULLUP, LOW);
IRrecv irrecv(IR_RECEIVE_PIN);
IRsend irsend(IR_SEND_PIN);
decode_results results;

// Функции
void initDisplay();
void clearDisplay();
void displayInfo(int posY, String nfo, unsigned long displayTime = 0, bool clearScreen = true);
void displayMainMenu();
void DisplayLcdInfoCenter(int posY, String nfo, unsigned long displayTime = 0, bool clearScreen = true);
void BackToLcdMainMenu();
void sendAnswer(String text);
int getNextID();
String getProtocolName(decode_type_t protocol);
void lcdBacklightControl();
void resetBacklightTimer();

// Переменные для управления подсветкой
unsigned long lastActivityTime;
bool lcdBacklightOn = true;

void setup()
{
    Serial.begin(115200);

    // Инициализация выбранного дисплея
    initDisplay();

    displayInfo(1, F("IR Remote Control System"));
    displayInfo(2, F("Version 1.0"), 2000, false);

    // Инициализация SD-карты
    displayInfo(1, F("Init SD card..."), 1000);

    if (!SD.begin(SD_CS_PIN))
    {
        displayInfo(1, F("SD failed!"));
        displayInfo(2, F("Check your SD card or it's wiring."), 0, false);

        while (1)
            ;
    }
    else
        displayInfo(1, F("SD passed"), 1000);

    // Создаем задачу для WiFi и Telegram на втором ядре
    xTaskCreatePinnedToCore(
        wifiTelegramTask,   // Функция задачи
        "WifiTelegramTask", // Имя задачи
        8192,               // Размер стека (увеличим для WiFi/TLS)
        NULL,               // Параметры задачи
        1,                  // Приоритет
        NULL,               // Дескриптор задачи (не нужен)
        1                   // Ядро 1
    );

    // Ждем, пока задача на втором ядре подключится к сети
    while (!networkInitialized)
    {
        delay(10);
    }

    // Создание очереди для сообщений в Telegram
    // Очередь будет содержать указатели на строки (String*)
    telegramQueue = xQueueCreate(10, sizeof(String *));

    // Создание очереди для ID команд
    commandQueue = xQueueCreate(10, sizeof(int));

    // Создание мьютекса для синхронизации
    xMutex = xSemaphoreCreateMutex();

    // Кэширование данных с SD-карты
    displayInfo(1, F("Loading codes from SD..."), 1000);

    File file = SD.open("/dataCodes.txt", FILE_READ);

    if (file)
    {
        int lines = 0;

        while (file.available())
        {
            file.readStringUntil('\n');
            lines++;
        }

        file.close();

        codesData = new String[lines];
        codesCount = lines;

        file = SD.open("/dataCodes.txt", FILE_READ);

        for (int i = 0; i < lines && file.available(); i++)
        {
            codesData[i] = file.readStringUntil('\n');
        }

        file.close();

        sendAnswer("Codes loaded " + String(codesCount));
        displayInfo(1, String("Codes loaded ") + String(codesCount), 1000);
    }
    else
    {
        sendAnswer(F("No codes file found!"));
        displayInfo(1, F("No codes file found!"), 1000);

        codesData = NULL;
        codesCount = 0;
    }

    sendAnswer(F("READY"));

    // Инициализация ИК-приемника и передатчика
    irrecv.enableIRIn();
    irsend.begin();

    displayMainMenu();

    // Инициализация таймера подсветки
    lastActivityTime = millis();
}

// Функция для подключения к WiFi с повторными попытками

void loop()
{
    lcdBacklightControl(); // Управление подсветкой
    btn.tick();

    if (btn.hasClicks())
    {
        resetBacklightTimer(); // Сбрасываем таймер при активности
        static bool enDisplay = true;
        uint8_t btnCnt = btn.getClicks();

        switch (btnCnt)
        {
        case 2:
            btnPressed = true;
            break;
        default:
            displayInfo(0, F("Other clicks not implemented."));
            displayInfo(2, F("Press BTN twice to add IR code."), 3000, false);
            displayMainMenu();
            break;
        }
    }

    if (btn.hold() || clearAllCodes)
    {
        resetBacklightTimer(); // Сбрасываем таймер при активности
        displayInfo(1, F("Deleting dataCodes.txt..."), 1000);
        sendAnswer(F("Deleting dataCodes.txt..."));
        if (SD.exists("/dataCodes.txt"))
        {
            SD.remove("/dataCodes.txt");

            // Очищаем кэш
            if (codesData != NULL && xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
            {
                delete[] codesData;
                codesData = NULL;
                codesCount = 0;
                xSemaphoreGive(xMutex);
            }

            displayInfo(2, F("File deleted."), 1000, false);
            sendAnswer(F("IR codes file deleted. Cache cleared."));
        }
        else
        {
            displayInfo(1, F("File does not exist now."), 1000);
            sendAnswer(F("IR codes file does no exist now."));
        }

        displayMainMenu();

        if (clearAllCodes)
            clearAllCodes = false;
    }

    // Режим обучения: активируется двойным нажатием
    if (btnPressed)
    {
        static uint8_t part = 0;

        switch (part)
        {
        case 0:
            sendAnswer(F("LEARNING MODE:\nPoint your remote and press a button..."));

            displayInfo(0, F("LEARNING MODE:"));
            displayInfo(1, F("Point your remote and press a button..."), 0, false);

            irrecv.resume();

            part++;
            break;
        case 1:
            // Запись ИК-кода при получении
            if (irrecv.decode(&results))
            {
                resetBacklightTimer(); // Сбрасываем таймер при активности
                // Проверяем, не слишком ли длинный код
                if (results.bits > 0 && results.bits <= 64)
                {
                    displayInfo(1, F("Code received!"), 1000);
                    sendAnswer(F("Code received!"));

                    int newID = getNextID();

                    // ИСПРАВЛЕНО: Режим открытия файла для ESP32
                    File dataFile = SD.open("/dataCodes.txt", FILE_APPEND);

                    if (dataFile)
                    {
                        decode_type_t protocol = results.decode_type;
                        uint32_t address = results.address;
                        uint32_t command = results.command;

                        dataFile.print(newID);
                        dataFile.print(" ");
                        dataFile.print((int)protocol);
                        dataFile.print(" ");
                        dataFile.print(address);
                        dataFile.print(" ");
                        dataFile.println(command);
                        dataFile.close();

                        // Обновляем кэш
                        // Обновляем кэш
                        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
                        {
                            // Создаем новый кэш с увеличенным размером
                            String *newCache = new String[codesCount + 1];

                            // Копируем старые данные, если они есть
                            for (int i = 0; i < codesCount; i++)
                            {
                                newCache[i] = codesData[i];
                            }

                            // Добавляем новую запись
                            newCache[codesCount] = String(newID) + " " + String((int)protocol) + " " + String(address) + " " + String(command);

                            // Удаляем старый кэш, если он был
                            if (codesData != NULL)
                            {
                                delete[] codesData;
                            }

                            // Обновляем указатель на кэш и счетчик
                            codesData = newCache;
                            codesCount++;

                            xSemaphoreGive(xMutex);
                        }

                        // Отправляем данные в Telegram
                        String codeData = "CODE DATA:\nID: " + String(newID) +
                                          "\nProtocol: " + getProtocolName(protocol) +
                                          "\nAddr: " + String(address, HEX) +
                                          "\nCmd: " + String(command, HEX);
                        sendAnswer(codeData);

                        displayInfo(0, F("CODE DATA:"));
                        displayInfo(1, "ID: " + String(newID), 0, false);
                        displayInfo(2, "Protocol: " + getProtocolName(protocol), 0, false);
                        displayInfo(3, "Addr:" + String(address, HEX) + "   Cmd:" + String(command, HEX), 2000, false);
                    }
                    else
                    {
                        sendAnswer(F("Error: Could not save IR code to SD card"));
                        displayInfo(1, F("Error opening file, or file is absent!"), 2000);
                    }
                }
                else
                {
                    sendAnswer(F("Invalid IR code length!"));
                    displayInfo(1, F("Invalid IR code length!"), 2000);
                }

                irrecv.resume();

                displayMainMenu();

                btnPressed = false;
                part = 0;
            }
            break;
        }
    }

    // Режим воспроизведения: активируется по данным из очереди
    int commandID = 0;
    if (xQueueReceive(commandQueue, &commandID, 0) == pdTRUE)
    {
        if (commandID > 0)
        {
            resetBacklightTimer(); // Сбрасываем таймер при активности
            // Используем кэшированные данные вместо чтения с SD-карты
            bool found = false;
            decode_type_t protocol = NEC;
            uint32_t address = 0;
            uint32_t command = 0;

            // Защищаем доступ к кэшу мьютексом
            if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
            {
                for (int i = 0; i < codesCount && !found; i++)
                {
                    String line = codesData[i];
                    if (line.startsWith(String(commandID) + " "))
                    {
                        int firstSpace = line.indexOf(' ');
                        int secondSpace = line.indexOf(' ', firstSpace + 1);
                        int thirdSpace = line.indexOf(' ', secondSpace + 1);

                        String protocolStr = line.substring(firstSpace + 1, secondSpace);
                        String addressStr = line.substring(secondSpace + 1, thirdSpace);
                        String commandStr = line.substring(thirdSpace + 1);

                        protocol = (decode_type_t)protocolStr.toInt();
                        address = (uint32_t)addressStr.toInt();
                        command = (uint32_t)commandStr.toInt();

                        found = true;
                    }
                }
                xSemaphoreGive(xMutex);
            }

            if (found)
            {
                char buffer[128];
                snprintf(buffer, sizeof(buffer), "Sending code ID: %d\nProtocol: %s\nAddr: %s\nCmd: %s",
                         commandID, getProtocolName(protocol).c_str(),
                         String(address, HEX).c_str(), String(command, HEX).c_str());
                sendAnswer(String(buffer));

                displayInfo(0, F("Sending code ID:"));
                displayInfo(1, String(commandID), 0, false);
                displayInfo(2, "Protocol:" + getProtocolName(protocol), 0, false);
                displayInfo(3, "Addr:" + String(address, HEX) + "   Cmd:" + String(command, HEX), 0, false);

                switch (protocol)
                {
                case NEC:
                    irsend.sendNEC(address, command);
                    break;
                case SONY:
                    irsend.sendSony(address, command, 12);
                    break;
                case SAMSUNG:
                    irsend.sendSAMSUNG(address, command);
                    break;
                case RC5:
                    irsend.sendRC5(address, command);
                    break;
                case RC6:
                    irsend.sendRC6(address, command);
                    break;
                default:
                    displayInfo(1, F("Unsupported protocol"), 1000);
                    break;
                }
            }
            else
            {
                sendAnswer("Code ID " + String(commandID) + " not found.");
                displayInfo(1, "Code ID " + String(commandID) + " not found.", 1000);
            }

            displayMainMenu();
        }
    }
}

int getNextID()
{
    if (!SD.exists("/dataCodes.txt"))
        return 1;

    File file = SD.open("/dataCodes.txt", FILE_READ);

    if (!file)
        return 1; // Запасной вариант при ошибке

    int maxID = 0;

    while (file.available())
    {
        String line = file.readStringUntil('\n');

        if (line.length() > 0)
        {
            int spaceIndex = line.indexOf(' ');

            if (spaceIndex > 0)
            {
                int id = line.substring(0, spaceIndex).toInt();

                if (id > maxID)
                    maxID = id;
            }
        }
    }

    file.close();

    return maxID + 1;
}

String getProtocolName(decode_type_t protocol)
{
    switch (protocol)
    {
    case NEC:
        return "NEC";
    case SONY:
        return "SONY";
    case SAMSUNG:
        return "SAMSUNG";
    case RC5:
        return "RC5";
    case RC6:
        return "RC6";
    case DISH:
        return "DISH";
    case SHARP:
        return "SHARP";
    case JVC:
        return "JVC";
    case PANASONIC:
        return "PANASONIC";
    case LG:
        return "LG";
    default:
        return "UNKNOWN";
    }
}

void DisplayLcdInfoCenter(int posY, String nfo, unsigned long displayTime, bool clearScreen)
{
    if (posY < 0 || posY >= LCD_ROWS)
        return;

    if (clearScreen)
    {
        for (int row = 0; row < LCD_ROWS; row++)
        {
            lcd.setCursor(0, row);

            for (int col = 0; col < LCD_COLS; col++)
                lcd.print(' ');
        }
    }
    else
    {
        for (int row = posY; row < LCD_ROWS; row++)
        {
            lcd.setCursor(0, row);

            for (int col = 0; col < LCD_COLS; col++)
                lcd.print(' ');
        }
    }

    int currentRow = posY;
    int startIndex = 0;
    int totalLen = nfo.length();

    while (startIndex < totalLen && currentRow < LCD_ROWS)
    {
        int maxFragmentLen = min(LCD_COLS, totalLen - startIndex);
        String fragment = nfo.substring(startIndex, startIndex + maxFragmentLen);
        int lastSpace = fragment.lastIndexOf(' ');
        int fragmentLen = maxFragmentLen;

        if ((startIndex + maxFragmentLen < totalLen) && (lastSpace > 0))
        {
            fragmentLen = lastSpace + 1;
            fragment = nfo.substring(startIndex, startIndex + fragmentLen);
        }

        int padLen = (LCD_COLS - fragmentLen) / 2;

        lcd.setCursor(padLen, currentRow);
        lcd.print(fragment);

        startIndex += fragmentLen;
        currentRow++;
    }

    if (displayTime > 0)
        delay(displayTime);
}

void BackToLcdMainMenu()
{
    displayInfo(0, F("Press BTN to add new IR code."));
    displayInfo(3, F("READY FOR CONTROL"), 0, false);
}

// Универсальные функции для работы с дисплеем
void initDisplay()
{
#ifdef USE_LCD_DISPLAY
    // Инициализация LCD
    lcd.init();
    lcd.backlight();
    lcd.clear();
#endif

#ifdef USE_OLED_DISPLAY
    // Инициализация OLED
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    oled.init();
    oled.clear();
    oled.update();
    oled.setScale(1);
    oled.autoPrintln(true);
#endif
}

void clearDisplay()
{
#ifdef USE_LCD_DISPLAY
    lcd.clear();
#endif

#ifdef USE_OLED_DISPLAY
    oled.clear();
    oled.update();
#endif
}

void displayInfo(int posY, String nfo, unsigned long displayTime, bool clearScreen)
{
#ifdef USE_LCD_DISPLAY
    DisplayLcdInfoCenter(posY, nfo, displayTime, clearScreen);
#endif

#ifdef USE_OLED_DISPLAY
    // Для OLED реализуем аналогичную функцию
    if (clearScreen)
        oled.clear();

    // Определяем количество строк на OLED (примерно 8 строк при размере текста 1)
    int oledRows = 8;

    if (posY < 0 || posY >= oledRows)
        return;

    // Устанаваем курсор в начало строки posY
    oled.setCursor(0, posY);

    // Выводим текст
    oled.println(nfo);

    // Отображаем на экране
    oled.update();

    // Задержка, если нужно
    if (displayTime > 0)
        delay(displayTime);
#endif
}

void displayMainMenu()
{
#ifdef USE_LCD_DISPLAY
    BackToLcdMainMenu();
#endif

#ifdef USE_OLED_DISPLAY
    clearDisplay();

    oled.setCursor(0, 0);
    oled.println(F("Press BTN to add"));
    oled.setCursor(0, 1);
    oled.println(F("new IR code."));
    oled.setCursor(0, 7);
    oled.println(F("WAITING FOR CONTROL"));
    oled.update();
#endif
}

void sendAnswer(String text)
{
    // Отправляем указатель на копию строки в очередь
    String *pText = new String(text);

    BaseType_t xStatus = xQueueSend(telegramQueue, &pText, portMAX_DELAY);

    if (xStatus != pdPASS)
    {
        // Ошибка отправки, освобождаем память
        delete pText;
        // Логирование ошибки
        Serial.println("Error: Failed to send message to queue");
    }
}

void lcdBacklightControl()
{
#ifdef USE_LCD_DISPLAY
    if (LCD_BACKLIGHT_TIMEOUT_S > 0 && lcdBacklightOn && (millis() - lastActivityTime > (LCD_BACKLIGHT_TIMEOUT_S * 1000)))
    {
        lcd.noBacklight();
        lcdBacklightOn = false;
    }
#endif
}

void resetBacklightTimer()
{
#ifdef USE_LCD_DISPLAY
    if (!lcdBacklightOn)
    {
        lcd.backlight();
        lcdBacklightOn = true;
    }
#endif
    lastActivityTime = millis();
}
