#include <Arduino.h>                    // Базовая библиотека Arduino
#include <DHT.h>                        // Библиотека для работы с датчиком DHT11
#include <NimBLEDevice.h>               // Лёгкая и современная библиотека BLE

#define DHTPIN 23                       // Пин, к которому подключён датчик DHT11
#define DHTTYPE DHT11                   // Тип датчика — DHT11
DHT dht(DHTPIN, DHTTYPE);               // Создаём объект датчика

// ==================== Пины и калибровка датчика влажности почвы YL-69 (FC-28) ====================
// ВАЖНО: Используйте ТОЛЬКО пины ADC1 (GPIO 32, 33, 34, 35, 36, 39), когда работает BLE!
// GPIO 34 — отличный выбор (ADC1_CH6), безопасен с NimBLE.
#define SOIL_MOISTURE_PIN 34

// === КАЛИБРОВКА (ОБЯЗАТЕЛЬНО СДЕЛАЙТЕ ПОД СЕБЯ!) ===
// Датчик YL-69 имеет ИНВЕРТИРОВАННЫЙ выход: 
//   высокое значение analogRead = сухая почва
//   низкое значение analogRead  = влажная почва / вода
// 
// Ваши указанные диапазоны (0-300 сухая, 700+ мокрая) для 10-бит ADC, скорее всего, имеют противоположную полярность.
// Стандартное поведение YL-69 (подтверждено множеством гайдов): 
//   сухая почва → ~3000-4095 (на ESP32 12 бит при питании 3.3V)
//   очень влажная/вода → ~800-2000
//
// Как откалибровать:
// 1. Загрузите код, откройте Serial Monitor.
// 2. Поместите датчик в СУХУЮ почву → запишите значение analogRead → поставьте в SOIL_DRY
// 3. Поместите датчик в ВОДУ или очень влажную почву → запишите значение → SOIL_WET (должно быть МЕНЬШЕ dry)
// 4. Перезагрузите и проверьте проценты влажности.
#define SOIL_MIN 0      // значение raw ADC = 0% влажности
#define SOIL_MAX 1000   // значение raw ADC = 100% влажности

// ===========================================================================================

// ==================== Структура данных ====================
struct SensorData {                     // Структура для хранения данных (теперь 12 байт)
    float temperature;                  // Температура (4 байта)
    float humidity;                     // Влажность (4 байта)
    float soilMoisture;                 // Влажность почвы в % (0-100). 0 = сухо, 100 = очень влажно
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

    // Настройка ADC для точного 12-битного чтения (0-4095)
    analogReadResolution(12);

    // ==================== ПОДКЛЮЧЕНИЕ ДАТЧИКА YL-69 ====================
    // РЕКОМЕНДУЕТСЯ: Питание датчика от пина 3.3V ESP32 (безопасно, нет риска >3.3V на ADC)
    //   VCC  датчика → 3.3V ESP32
    //   GND  датчика → GND ESP32
    //   AOUT (аналоговый выход) → GPIO 34 (SOIL_MOISTURE_PIN)
    //
    // АЛЬТЕРНАТИВА (если нужно 5V): 
    //   Питайте VCC от 5V, но ОБЯЗАТЕЛЬНО поставьте делитель напряжения на AOUT!
    //   Пример: 10kΩ между AOUT и GPIO34 + 10kΩ между GPIO34 и GND (или рассчитайте под 4.2V → ~3.0V)
    //   Иначе можно повредить ESP32!
    //
    // Не используйте цифровой выход D0 — мы используем аналоговый для точности.
    // =====================================================================

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
    Serial.println("Отправка бинарных данных (Temp + Hum + SoilMoisture %)");
    Serial.println(">>> Не забудьте откалибровать SOIL_DRY и SOIL_WET под вашу почву!");
}

void loop() {
    delay(2000);

    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int soilRaw = analogRead(SOIL_MOISTURE_PIN);

    if (isnan(t) || isnan(h)) {
        Serial.println("Ошибка чтения DHT11!");
        return;
    }

// Вычисление влажности почвы (простая линейная шкала 0–1000 → 0–100%)
    float soilMoisture = 0.0;

    if (soilRaw <= SOIL_MIN) {
        soilMoisture = 0.0;
    } 
    else if (soilRaw >= SOIL_MAX) {
        soilMoisture = 100.0;
    } 
    else {
        soilMoisture = (float)soilRaw / (float)SOIL_MAX * 100.0;
    }

    SensorData data = {t, h, soilMoisture};

    // Отправляем данные только если есть подключение
    if (deviceConnected) {
        pDataCharacteristic->setValue((uint8_t*)&data, sizeof(data));  // Записываем данные в уже созданный "файл"
        pDataCharacteristic->notify();                                 // Говорим: "Эй, клиент, новые данные пришли!"
        Serial.printf("Отправлено -> Temp: %.1f°C | Hum: %.1f%% | Soil: %.0f%%\n", t, h, soilMoisture);
        Serial.printf("  (Soil raw ADC = %d | min=%d, max=%d)\n", soilRaw, SOIL_MIN, SOIL_MAX);
    } 
}