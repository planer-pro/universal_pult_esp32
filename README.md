# ESP32 Universal IR Remote with Telegram Control

This project turns an ESP32 into a universal IR remote control that can learn and send IR codes. It is controlled via a physical button and through a Telegram bot. The project is designed to be modular and uses both cores of the ESP32 for efficient operation.

## Features

- **Learn IR Codes**: Can capture and store IR codes from any remote control.
- **Send IR Codes**: Can transmit stored IR codes to control devices.
- **Telegram Control**: A Telegram bot interface to send commands.
- **SD Card Storage**: Saves learned IR codes to an SD card.
- **Multi-Core Operation**: Utilizes both ESP32 cores for parallel processing of UI and networking.
- **LCD/OLED Display Support**: Supports both I2C LCD 20x4 and OLED 128x64 displays (configurable).

## How it Works

The application leverages the dual-core architecture of the ESP32 for non-blocking performance.

- **Core 0** (`main.cpp`): This core runs the main application logic. It handles:
  - User interface (physical button and display).
  - Learning new IR codes.
  - Sending saved IR codes based on commands received from Core 1.
  - Reading from and writing to the SD card.
  - Sending status messages to the network task via a FreeRTOS queue (`telegramQueue`).

- **Core 1** (`wifi_telegram_core.cpp`): This core is dedicated to all networking tasks.
  - Connecting to the WiFi network.
  - Handling all communication with the Telegram Bot API.
  - Receiving commands from the user via Telegram and forwarding them to Core 0 via a queue (`commandQueue`).
  - Sending status messages from Core 0 to the user.

### Communication
- **Core 0 to Core 1**: A FreeRTOS queue (`telegramQueue`) is used for safe, thread-safe communication from the main logic to the network task. This allows Core 0 to send status updates (e.g., "Code learned," "File deleted") to the user via Telegram without dealing with network complexities.
- **Core 1 to Core 0**: Another FreeRTOS queue (`commandQueue`) is used to send commands received from Telegram (like a numeric ID to send a code) from the network core to the main core for execution.

## File Structure

- `src/main.cpp`: Main application logic running on Core 0.
- `src/wifi_telegram_core.cpp`: Networking logic for Core 1.
- `src/wifi_telegram_core.h`: Header file for the networking task.
- `src/config.h`: **Configuration file.** You must enter your WiFi SSID, password, Telegram Bot Token, and Chat ID here.
- `platformio.ini`: PlatformIO project configuration.

## Physical Button Controls

- **Single Click**: Toggles the display backlight on or off.
- **Double Click**: Activates "Learning Mode" to capture a new IR code.
- **Long Press (Hold)**: Deletes the `dataCodes.txt` file from the SD card, clearing all saved codes.

## Telegram Bot Commands

You can control the device by sending commands to your Telegram bot.

- **Send a number** (e.g., `1`, `25`): Transmits the IR code with the corresponding ID.
- `/help`: Displays the list of available commands.
- `/learn`: Activates "Learning Mode," the same as a double-click on the physical button.
- `/allclear`: Deletes all saved IR codes from the SD card, the same as a long press.
- `/list`: Displays the list of all saved IR codes with their IDs, protocols, and data.
- `/status`: Shows the current system status, including WiFi connection and IP address.
- `/memory`: Reports the amount of free memory (heap) on the ESP32.
- `/restart`: Restarts the device.

## How to Use

1.  **Configuration**: Open `src/config.h` and fill in your WiFi credentials and Telegram Bot details.
2.  **Learn a Code**:
    - Double-press the physical button or send the `/learn` command via Telegram.
    - The display will show "LEARNING MODE".
    - Point your existing IR remote at the IR receiver and press the button you want to learn.
    - The device will save the code to the SD card with a new ID, which will be shown on the display and sent to your Telegram chat.
3.  **Send a Code**:
    - Open your Telegram chat with the bot.
    - Send the ID of the code you want to transmit as a simple number (e.g., `1`).
    - The device will send the corresponding IR signal.
4.  **Delete All Codes**:
    - Press and hold the physical button or send the `/allclear` command via Telegram.
    - The `dataCodes.txt` file on the SD card will be deleted.

---

# ESP32 Универсальный ИК-пульт с управлением через Telegram

Этот проект превращает ESP32 в универсальный ИК-пульт, который может изучать и отправлять ИК-коды. Управление осуществляется с помощью физической кнопки и через Telegram-бота. Проект имеет модульную структуру и использует оба ядра ESP32 для эффективной работы.

## Возможности

- **Изучение ИК-кодов**: Может захватывать и сохранять ИК-коды с любого пульта.
- **Отправка ИК-кодов**: Может передавать сохраненные ИК-коды для управления устройствами.
- **Управление через Telegram**: Интерфейс с Telegram-ботом для отправки команд.
- **Хранение на SD-карте**: Сохраняет изученные ИК-коды на SD-карту.
- **Многоядерная работа**: Использует оба ядра ESP32 для параллельной обработки пользовательского интерфейса и сетевых задач.
- **Поддержка LCD/OLED дисплеев**: Поддерживает как I2C LCD 20x4, так и OLED 128x64 дисплеи (настраивается в коде).

## Как это работает

Приложение использует двухъядерную архитектуру ESP32 для неблокирующей производительности.

- **Ядро 0** (`main.cpp`): На этом ядре выполняется основная логика приложения. Оно отвечает за:
  - Пользовательский интерфейс (физическая кнопка и дисплей).
  - Изучение новых ИК-кодов.
  - Отправку сохраненных кодов на основе команд, полученных от Ядра 1.
  - Чтение и запись на SD-карту.
  - Отправку статусных сообщений сетевой задаче через очередь FreeRTOS (`telegramQueue`).

- **Ядро 1** (`wifi_telegram_core.cpp`): Это ядро выделено для всех сетевых задач.
  - Подключение к сети WiFi.
  - Обработка всего взаимодействия с Telegram Bot API.
  - Получение команд от пользователя через Telegram и пересылка их на Ядро 0 через очередь (`commandQueue`).
  - Отправку статусных сообщений от Ядра 0 пользователю.

### Взаимодействие между ядрами
- **Ядро 0 -> Ядро 1**: Очередь FreeRTOS (`telegramQueue`) используется для безопасной передачи сообщений от основной логики к сетевой задаче. Это позволяет Ядру 0 отправлять статусные обновления (например, "Код изучен," "Файл удален") пользователю через Telegram, не вникая в сложности работы с сетью.
- **Ядро 1 -> Ядро 0**: Другая очередь FreeRTOS (`commandQueue`) используется для отправки команд, полученных из Telegram (например, числовой ID для отправки кода), от сетевого ядра к основному ядру для исполнения.

## Структура файлов

- `src/main.cpp`: Основная логика приложения, работающая на Ядре 0.
- `src/wifi_telegram_core.cpp`: Сетевая логика для Ядра 1.
- `src/wifi_telegram_core.h`: Заголовочный файл для сетевой задачи.
- `src/config.h`: **Файл конфигурации.** Здесь вы должны указать ваш SSID и пароль от WiFi, а также токен Telegram-бота и ваш Chat ID.
- `platformio.ini`: Файл конфигурации проекта PlatformIO.

## Управление физической кнопкой

- **Одиночное нажатие**: Включает или выключает подсветку дисплея.
- **Двойное нажатие**: Активирует "Режим обучения" для захвата нового ИК-кода.
- **Долгое нажатие (удержание)**: Удаляет файл `dataCodes.txt` с SD-карты, стирая все сохраненные коды.

## Команды Telegram-бота

Вы можете управлять устройством, отправляя команды вашему Telegram-боту.

- **Отправка числа** (например, `1`, `25`): Передает ИК-код с соответствующим ID.
- `/help`: Отображает список доступных команд.
- `/learn`: Активирует "Режим обучения", аналогично двойному нажатию физической кнопки.
- `/allclear`: Удаляет все сохраненные ИК-коды с SD-карты, аналогично долгому нажатию.
- `/list`: Выводит список всех сохраненных ИК-кодов с их ID, протоколами и данными.
- `/status`: Показывает текущий статус системы, включая подключение к WiFi и IP-адрес.
- `/memory`: Сообщает о количестве свободной памяти (heap) на ESP32.
- `/restart`: Перезагружает устройство.

## Как использовать

1.  **Настройка**: Откройте файл `src/config.h` и впишите свои данные для WiFi и Telegram-бота.
2.  **Изучение кода**:
    - Дважды нажмите физическую кнопку или отправьте команду `/learn` через Telegram.
    - На дисплее появится "LEARNING MODE".
    - Направьте ваш ИК-пульт на ИК-приемник и нажмите кнопку, которую хотите сохранить.
    - Устройство сохранит код на SD-карту с новым ID, который отобразится на дисплее и будет отправлен в ваш чат в Telegram.
3.  **Отправка кода**:
    - Откройте чат с вашим ботом в Telegram.
    - Отправьте ID кода, который вы хотите передать, в виде простого числа (например, `1`).
    - Устройство отправит соответствующий ИК-сигнал.
4.  **Удаление всех кодов**:
    - Нажмите и удерживайте физическую кнопку или отправьте команду `/allclear` через Telegram.
    - Файл `dataCodes.txt` на SD-карте будет удален.