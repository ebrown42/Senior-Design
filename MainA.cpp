#include <Arduino.h>
#include <LiquidCrystal.h>
#include <DHT.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LoRa.h>

// ============ FUNCTION PROTOTYPES ============
void startPump(int seconds);
void stopPump();
void readSensors();
void printStatus();
void calibrateSensors();
void handleCommand();
void printHelp();
void updateLCD(int soil, float temp, float humid, bool pump);
// ============================================

// ============ PIN DEFINITIONS ============
#define SOIL_PIN 4
#define DHT_PIN 15
#define RELAY_PIN 12
#define LED_PIN 2
#define SD_CLK 17
#define SD_CS 16
#define SD_MOSI 18
#define SD_MISO 13

// LCD pins (RS, E, DB4, DB5, DB6, DB7)
LiquidCrystal lcd(5, 6, 7, 8, 9, 10);

// ============ BLE UUIDs ============
#define SERVICE_UUID "9366ae51-983c-473d-a47d-0735e0a752c7"
#define CHARACTERISTIC_UUID "9f567c97-4b41-42d5-9719-eab2af7fd6cf"

// ============ ESP-NOW ============
uint8_t slaveMacAddress[] = {0x3C, 0xDC, 0x75, 0x6E, 0x82, 0xD4};

typedef struct {
    int soilPercent;
    float tempF;
    float humidity;
    bool requestWater;
} slaveData_t;

slaveData_t slaveData;
bool waterRequestedBySlave = false;

// ============ CALIBRATION ============
int dryValue = 2597;
int wetValue = 1046;
int moistureThreshold = 80;

#define PUMP_RUN_TIME     5
#define READ_INTERVAL     2000
#define SERIAL_INTERVAL   5000

// ============ COOLDOWN VARIABLES ============
unsigned long lastPumpTrigger = 0;
const unsigned long pumpTriggerCooldown = 10000;  // 10 seconds between pump triggers
unsigned long lastPumpStartTime = 0;
const unsigned long pumpCooldown = 5000;  // 5 seconds minimum between pump starts
unsigned long lastWateringCheck = 0;
const unsigned long wateringCheckInterval = 10000;  // Check every 10 seconds

// ============ GLOBALS ============
DHT dht(DHT_PIN, DHT22);
bool pumpActive = false;
unsigned long pumpStopTime = 0;
unsigned long lastSensorRead = 0;
unsigned long lastSerialPrint = 0;

// BLE
BLEServer *pServer;
BLEService *pService;
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

bool sdCardOK = false;

// ============ BLE CALLBACKS ============
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Phone Connected");
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Phone Disconnected");
        BLEDevice::getAdvertising()->start();
    }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        String cmd = pCharacteristic->getValue().c_str();
        cmd.trim();
        
        if (cmd == "WATER" || cmd == "1") {
            Serial.println("BLE Command: WATER");
            startPump(5);
        } else if (cmd == "STOP" || cmd == "0") {
            Serial.println("BLE Command: STOP");
            stopPump();
        }
    }
};

// ============ SD CARD FUNCTIONS ============
void initSDCard() {
    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    
    if (!SD.begin(SD_CS)) {
        Serial.println("SD Card Mount Failed");
        sdCardOK = false;
        return;
    }
    
    sdCardOK = true;
    Serial.println("SD Card Mounted Successfully");
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return;
    }
    
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) Serial.println("MMC");
    else if (cardType == CARD_SD) Serial.println("SDSC");
    else if (cardType == CARD_SDHC) Serial.println("SDHC");
    else Serial.println("UNKNOWN");
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    
    if (!SD.exists("/irrigation.csv")) {
        File file = SD.open("/irrigation.csv", FILE_WRITE);
        if (file) {
            file.println("Timestamp,Soil_%,Temp_F,Humidity_%,Pump_Status");
            file.close();
            Serial.println("Created irrigation.csv");
        }
    }
}

void logToSD(int soil, float tempF, float humidity, bool pumpActive) {
    if (!sdCardOK) return;
    
    File file = SD.open("/irrigation.csv", FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open file for appending");
        return;
    }
    
    file.print(millis());
    file.print(",");
    file.print(soil);
    file.print(",");
    file.print(tempF);
    file.print(",");
    file.print(humidity);
    file.print(",");
    file.println(pumpActive ? "ON" : "OFF");
    file.close();
}

// ============ ESP-NOW RECEIVE CALLBACK ============
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
    memcpy(&slaveData, incomingData, sizeof(slaveData));

    Serial.println("=================================");
    Serial.println("DATA FROM SLAVE");
    Serial.println("=================================");
    Serial.print("Slave Soil: ");
    Serial.print(slaveData.soilPercent);
    Serial.println("%");
    Serial.print("Slave Temp: ");
    Serial.print(slaveData.tempF);
    Serial.println("°F");
    Serial.print("Slave Humidity: ");
    Serial.print(slaveData.humidity);
    Serial.println("%");

    if (slaveData.requestWater) {
        Serial.println("Water request from slave");
        waterRequestedBySlave = true;
    }
    Serial.println("=================================\n");
}

// ============ SETUP ============
void setup() {
    pinMode(3, OUTPUT);
    analogWrite(3, 80);
    
    delay(5000);
    Serial.begin(115200);
    
    int timeout = 0;
    while (!Serial && timeout < 10) {
        delay(100);
        timeout++;
    }

    Serial.println("---Master System Starting---");

    // Initialize LCD
    lcd.begin(16, 2);
    lcd.print("Solar Irrigation");
    lcd.setCursor(0, 1);
    lcd.print("Starting...");

    // Initialize BLE
    Serial.println("Starting BLE Server");
    BLEDevice::init("Solar Powered Irrigation System");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_WRITE
    );
    pCharacteristic->setCallbacks(new CommandCallbacks());
    pService->start();
    pServer->getAdvertising()->start();

    // Initialize SD Card
    initSDCard();

    // Initialize ESP-NOW (RECEIVE ONLY)
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
    } else {
        esp_now_register_recv_cb(OnDataRecv);  
        Serial.println("ESP-NOW Ready - Listening for Slave");
    }

    // Initialize hardware
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    dht.begin();

    delay(2000);
    lcd.clear();
    
    Serial.println(F("System Ready"));
    printHelp();
}

// ============ LOOP ============
void loop() {
    unsigned long now = millis();
    
    if (now - lastSensorRead >= READ_INTERVAL) {
        readSensors();
        lastSensorRead = now;
    }

    // Handle water request from slave (with cooldown)
    if (waterRequestedBySlave && !pumpActive) {
        if (millis() - lastPumpTrigger >= pumpTriggerCooldown) {
            Serial.println("Processing slave water request...");
            startPump(PUMP_RUN_TIME);
            lastPumpTrigger = millis();
        } else {
            Serial.println("Slave request ignored (cooldown active)");
        }
        waterRequestedBySlave = false;
    }
    
    if (now - lastSerialPrint >= SERIAL_INTERVAL) {
        printStatus();
        lastSerialPrint = now;
    }
    
    if (pumpActive && now >= pumpStopTime) {
        stopPump();
    }
    
    if (Serial.available()) {
        handleCommand();
    }
    
    delay(10);
}

// ============ READ SENSORS ============
void readSensors() {
    int rawSoil = analogRead(SOIL_PIN);
    int soilPercent = map(rawSoil, dryValue, wetValue, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);
    
    float tempC = dht.readTemperature();
    float tempF = (tempC * 9.0/5.0) + 32.0;
    float humidity = dht.readHumidity();
    
    if (isnan(tempF) || isnan(humidity)) {
        Serial.println("DHT read failed");
        return;
    }

    logToSD(soilPercent, tempF, humidity, pumpActive);
    updateLCD(soilPercent, tempF, humidity, pumpActive);

    if (deviceConnected) {
        String data = "Soil: " + String(soilPercent) + "%, Temp: " + String(tempF) + "°F, Humidity: " + String(humidity) + "%";
        pCharacteristic->setValue(data.c_str());
        pCharacteristic->notify();
    }
    
    // DEBUG: Print sensor changes
    static int lastSoil = 0;
    static float lastHumidity = 0;
    
    if (soilPercent != lastSoil || humidity != lastHumidity) {
        Serial.print("Sensor changed - Soil: ");
        Serial.print(soilPercent);
        Serial.print(" (was ");
        Serial.print(lastSoil);
        Serial.print(") | Humidity: ");
        Serial.print(humidity);
        Serial.print(" (was ");
        Serial.print(lastHumidity);
        Serial.println(")");
        lastSoil = soilPercent;
        lastHumidity = humidity;
    }
    
    // ========== AUTO WATERING WITH COOLDOWN ==========
    // Only check watering conditions every 10 seconds
    if (millis() - lastWateringCheck >= wateringCheckInterval) {
        lastWateringCheck = millis();
        
        bool soilDry = (soilPercent > moistureThreshold);
        bool highHumidity = (humidity > moistureThreshold);
        
        Serial.print("DEBUG - Soil: ");
        Serial.print(soilPercent);
        Serial.print(" | Humidity: ");
        Serial.print(humidity);
        Serial.print(" | Threshold: ");
        Serial.print(moistureThreshold);
        Serial.print(" | PumpActive: ");
        Serial.println(pumpActive);
        
        if (!pumpActive && (soilDry || highHumidity)) {
            // Check cooldown before triggering
            if (millis() - lastPumpTrigger >= pumpTriggerCooldown) {
                Serial.print(F("\n Condition met - "));
                if (soilDry) Serial.print(F("SOIL DRY"));
                if (soilDry && highHumidity) Serial.print(F(" and "));
                if (highHumidity) Serial.print(F("HIGH HUMIDITY"));
                Serial.println(F(" - Starting pump"));
                lastPumpTrigger = millis();
                startPump(PUMP_RUN_TIME);
            } else {
                Serial.println("Water condition met but cooldown active - skipping");
            }
        } else if (!pumpActive) {
            // Print why it's NOT starting
            Serial.print("Not watering - SoilDry:");
            Serial.print(soilDry);
            Serial.print(" HighHumidity:");
            Serial.println(highHumidity);
        }
    }
}

// ============ UPDATE LCD ============
void updateLCD(int soil, float temp, float humid, bool pump) {
    lcd.setCursor(0, 0);
    lcd.print("Soil:");
    lcd.print(soil);
    lcd.print("%  ");
    
    lcd.setCursor(0, 1);
    lcd.print("T:");
    lcd.print(temp, 0);
    lcd.print("F H:");
    lcd.print(humid, 0);
    lcd.print("%");
    
    lcd.setCursor(12, 0);
    if (pump) {
        lcd.print("ON ");
    } else {
        lcd.print("   ");
    }
}

// ============ PUMP CONTROL ============
void startPump(int seconds) {
    // Prevent multiple rapid calls
    if (millis() - lastPumpStartTime < pumpCooldown) {
        Serial.println("DEBUG - Pump start blocked (cooldown active)");
        return;
    }
    lastPumpStartTime = millis();
    
    Serial.println("DEBUG - startPump() called");
    Serial.print(F("→ Pump ON for "));
    Serial.print(seconds);
    Serial.println(F(" seconds"));
    
    digitalWrite(RELAY_PIN, HIGH);
    pumpActive = true;
    pumpStopTime = millis() + (seconds * 1000);
    digitalWrite(LED_PIN, LOW);

    if (deviceConnected) {
        pCharacteristic->setValue("Pump ON");
        pCharacteristic->notify();
    }
}

void stopPump() {
    Serial.println("DEBUG - stopPump() called");
    digitalWrite(RELAY_PIN, LOW);
    pumpActive = false;
    digitalWrite(LED_PIN, HIGH);
    Serial.println(F("→ Pump OFF"));

    if (deviceConnected) {
        pCharacteristic->setValue("Pump OFF");
        pCharacteristic->notify();
    }
}

// ============ STATUS ============
void printStatus() {
    Serial.println(F("\n--- SYSTEM STATUS ---"));
    
    int rawSoil = analogRead(SOIL_PIN);
    int soilPercent = map(rawSoil, dryValue, wetValue, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);
    
    float tempC = dht.readTemperature();
    float tempF = (tempC * 9.0/5.0) + 32.0;
    float humidity = dht.readHumidity();
    
    Serial.print(F("Soil: "));
    Serial.print(soilPercent);
    Serial.print(F("% (raw: "));
    Serial.print(rawSoil);
    Serial.println(F(")"));
    
    if (!isnan(tempF)) {
        Serial.print(F("Temp: "));
        Serial.print(tempF);
        Serial.println(F("°F"));
        Serial.print(F("Humidity: "));
        Serial.print(humidity);
        Serial.println(F("%"));
    }
    
    Serial.print(F("Pump: "));
    Serial.println(pumpActive ? F("ON") : F("OFF"));
    
    if (pumpActive) {
        int secLeft = (pumpStopTime - millis()) / 1000;
        Serial.print(F("Time left: "));
        Serial.print(secLeft);
        Serial.println(F("s"));
    }

    Serial.print(F("Bluetooth: "));
    Serial.println(deviceConnected ? F("Connected") : F("Waiting"));
    Serial.println(F("----------------------"));
}

// ============ CALIBRATION ============
void calibrateSensors() {
    Serial.println(F("\n=== CALIBRATION MODE ==="));
    Serial.println(F("Step 1: Place sensor in DRY air"));
    Serial.println(F("Press any key when ready..."));
    while (!Serial.available());
    Serial.read();
    delay(2000);
    
    int dryTotal = 0;
    for(int i = 0; i < 10; i++) {
        dryTotal += analogRead(SOIL_PIN);
        delay(100);
        Serial.print(F("."));
    }
    dryValue = dryTotal / 10;
    Serial.print(F(" DRY = "));
    Serial.println(dryValue);
    
    Serial.println(F("\nStep 2: Place sensor in WATER"));
    Serial.println(F("Press any key when ready..."));
    while (!Serial.available());
    Serial.read();
    delay(2000);
    
    int wetTotal = 0;
    for(int i = 0; i < 10; i++) {
        wetTotal += analogRead(SOIL_PIN);
        delay(100);
        Serial.print(F("."));
    }
    wetValue = wetTotal / 10;
    Serial.print(F(" WET = "));
    Serial.println(wetValue);
    
    Serial.println(F("\n Calibration Complete"));
    Serial.print(F("dryValue = "));
    Serial.println(dryValue);
    Serial.print(F("wetValue = "));
    Serial.println(wetValue);
}

// ============ COMMANDS ============
void handleCommand() {
    if (Serial.available()) {
        char cmd = Serial.read();
        Serial.print("You typed: ");
        Serial.print(cmd);
        
        switch(cmd) {
            case '1':
                Serial.println(F("\n Manual pump test"));
                startPump(3);
                break;
            case '0':
                if (pumpActive) {
                    Serial.println(F("\n Manual stop"));
                    stopPump();
                }
                break;
            case 'c':
            case 'C':
                calibrateSensors();
                break;
            case 's':
            case 'S':
                printStatus();
                break;
            case 'h':
            case 'H':
                printHelp();
                break;
        }
    }
}

void printHelp() {
    Serial.println(F("\n=== AVAILABLE COMMANDS ==="));
    Serial.println(F("  '1' - Manual pump (3 seconds)"));
    Serial.println(F("  '0' - Stop pump immediately"));
    Serial.println(F("  'c' - Calibrate soil sensor"));
    Serial.println(F("  's' - Show status now"));
    Serial.println(F("  'h' - Show this help"));
    Serial.println(F("===========================\n"));
}
