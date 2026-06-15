#include <Arduino.h>                    // Базовая библиотека Arduino
#include <DHT.h>                        // Библиотека для работы с датчиком DHT11
#include <NimBLEDevice.h>               // Лёгкая и современная библиотека BLE

#define DHTPIN 23                       // Пин, к которому подключён датчик DHT11
#define DHTTYPE DHT11                   // Тип датчика — DHT11
DHT dht(DHTPIN, DHTTYPE);               // Создаём объект датчика

// ==================== Структура данных ====================
struct SensorData {                     // Структура для хранения данных (8 байт всего)
    float temperature;                  // Температура (4 байта)
    float humidity;                     // Влажность (4 байта)
};
// ========================================================

NimBLEServer* pServer = nullptr;                    // Указатель на BLE-сервер
NimBLECharacteristic* pDataCharacteristic = nullptr; // Указатель на характеристику (канал данных)

bool deviceConnected = false;                       // Флаг: подключено ли устройство

// Класс для обработки событий подключения/отключения
class MyServerCallbacks : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* pServer) { 
        deviceConnected = true;                     // Устанавливаем флаг подключения
        Serial.println("Connected");                // Выводим сообщение
    }
    void onDisconnect(NimBLEServer* pServer) { 
        deviceConnected = false;                    // Сбрасываем флаг
        Serial.println("Disconnected");             // Выводим сообщение
    }
};

void setup() {
    Serial.begin(115200);
    dht.begin();

    NimBLEDevice::init("ESP32_Temp_Hum");
    
    pServer = NimBLEDevice::createServer();                    // Сначала создаём сервер
    
    static MyServerCallbacks callbacks;
    pServer->setCallbacks(&callbacks);                         // Говорим: "если кто-то подключится — сообщи"

    NimBLEService* pService = pServer->createService("12345678-1234-5678-1234-56789abcdef0");           // Создаём "папку" для данных

    pDataCharacteristic = pService->createCharacteristic(       // Создаём "файл" внутри папки
        "12345678-1234-5678-1234-56789abcdef1",
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    pService->start();
    NimBLEDevice::getAdvertising()->start();

    Serial.println("BLE запущен! Ищи 'ESP32_Temp_Hum'");
    Serial.println("Отправка бинарных данных (Temp + Hum)");
}

void loop() {
    delay(2000);

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
        Serial.println("Ошибка чтения DHT11!");
        return;
    }

    SensorData data = {t, h};

    // Отправляем данные только если есть подключение
    if (deviceConnected) {
        pDataCharacteristic->setValue((uint8_t*)&data, sizeof(data));  // Записываем данные в уже созданный "файл"
        pDataCharacteristic->notify();                                 // Говорим: "Эй, клиент, новые данные пришли!"
        Serial.printf("Отправлено -> Temp: %.1f°C | Hum: %.1f%%\n", t, h);
    } 
}