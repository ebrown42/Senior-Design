#include <Arduino.h>
#include <LiquidCrystal.h>
#include <DHT.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// ============ FUNCTION PROTOTYPES (ADDED TO FIX ERRORS) ============
void startPump(int seconds);
void stopPump();
void readSensors();
void printStatus();
void calibrateSensors();
void handleCommand();
void printHelp();
void updateLCD(int soil, float temp, float humid, bool pump);
// ================================================================

// ============ PIN DEFINITIONS ============
#define SOIL_PIN 4
#define DHT_PIN 15
#define RELAY_PIN 5
#define LED_PIN 2

// LCD pins (RS, E, DB4, DB5, DB6, DB7)
LiquidCrystal lcd(5, 6, 7, 8, 9, 10);

// ============ BLE UUIDs ============
#define SERVICE_UUID "9366ae51-983c-473d-a47d-0735e0a752c7"
#define  CHARACTERISTIC_UUID "9f567c97-4b41-42d5-9719-eab2af7fd6cf"

// ============ CALIBRATION ============
int dryValue = 2597;     // for dry air
int wetValue = 1046;     // for water
int moistureThreshold = 40;  // water if soil below 40%

#define PUMP_RUN_TIME     5       // Seconds to run pump
#define READ_INTERVAL     2000    // Read sensors every 2 seconds
#define SERIAL_INTERVAL   5000    // Print status every 5 seconds

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

// ============ BLE COMMAND CALLBACKS ============
class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        String cmd = pCharacteristic->getValue().c_str();
        cmd.trim();
       
        if (cmd == "WATER" || cmd == "1") {
            startPump(5);
        } else if (cmd == "STOP" || cmd == "0") {
            stopPump();
        } else if (cmd == "STATUS") {
            // Status will be sent in next update
        }
    }
};

// ============ SETUP ============
void setup() {
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

    pCharacteristic -> setCallbacks(new CommandCallbacks);

    pService->start();
    pServer->getAdvertising()->start();

    // Initialize hardware
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);  // Pump off initially
   
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);   // LED on = system ready
   
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
    // read soil moisture
    int rawSoil = analogRead(SOIL_PIN);
    int soilPercent = map(rawSoil, dryValue, wetValue, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);
   
    // read temperature (F)
    float tempC = dht.readTemperature();
    float tempF = (tempC * 9.0/5.0) + 32.0;
    float humidity = dht.readHumidity();
   
    // Handle DHT errors
    if (isnan(tempF) || isnan(humidity)) {
        return;
    }

    // Update LCD display
    updateLCD(soilPercent, tempF, humidity, pumpActive);

    // Send to Bluetooth
    if (deviceConnected) {
        String data = "Soil: " + String(soilPercent) + "%, Temp: " + String(tempF) + "°F, Humidity: " + String(humidity) + "%";
        pCharacteristic->setValue(data.c_str());
        pCharacteristic->notify();
        Serial.println("Sending: " + data);
    }
   
    // AUTO WATERING DECISION
    if (!pumpActive && soilPercent < moistureThreshold) {
        Serial.println(F("\n SOIL DRY - Starting pump"));
        startPump(PUMP_RUN_TIME);
    }
}

// ============ UPDATE LCD DISPLAY ============
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

// ============ PUMP CONTROL FUNCTIONS ============
void startPump(int seconds) {
    Serial.print(F("→ Pump ON for "));
    Serial.print(seconds);
    Serial.println(F(" seconds"));
   
    digitalWrite(RELAY_PIN, HIGH);  // Turn pump on
    pumpActive = true;
    pumpStopTime = millis() + (seconds * 1000);
    digitalWrite(LED_PIN, LOW);     // LED off = pump running

    if (deviceConnected) {
        pCharacteristic -> setValue ("Pump ON");
        pCharacteristic -> notify();
    }
}

void stopPump() {
    digitalWrite(RELAY_PIN, LOW);   // Turn pump off
    pumpActive = false;
    digitalWrite(LED_PIN, HIGH);    // LED on = system ready
    Serial.println(F("→ Pump OFF"));

    if (deviceConnected) {
        pCharacteristic -> setValue ("Pump OFF");
        pCharacteristic -> notify();
    }
}

// ============ PRINT STATUS TO MONITOR ============
void printStatus() {
    Serial.println(F("\n--- SYSTEM STATUS ---"));
   
    // Read current values
    int rawSoil = analogRead(SOIL_PIN);
    int soilPercent = map(rawSoil, dryValue, wetValue, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);
   
    float tempC = dht.readTemperature();
    float tempF = (tempC * 9.0/5.0) + 32.0;
    float humidity = dht.readHumidity();
   
    // Print soil
    Serial.print(F("Soil: "));
    Serial.print(soilPercent);
    Serial.print(F("% (raw: "));
    Serial.print(rawSoil);
    Serial.println(F(")"));
   
    // Print temperature/humidity
    if (!isnan(tempF)) {
        Serial.print(F("Temp: "));
        Serial.print(tempF);
        Serial.println(F("°F"));
       
        Serial.print(F("Humidity: "));
        Serial.print(humidity);
        Serial.println(F("%"));
    }
   
    // Print pump status
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

// ============ CALIBRATION STUFF ============
void calibrateSensors() {
    Serial.println(F("\n=== CALIBRATION MODE ==="));
    Serial.println(F("Step 1: Place sensor in DRY air"));
    Serial.println(F("Press any key when ready..."));
    while (!Serial.available());
    Serial.read();
    delay(2000);
   
    // Read dry value
    Serial.print(F("Reading DRY..."));
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
   
    // Read wet value
    Serial.print(F("Reading WET..."));
    int wetTotal = 0;
    for(int i = 0; i < 10; i++) {
        wetTotal += analogRead(SOIL_PIN);
        delay(100);
        Serial.print(F("."));
    }
    wetValue = wetTotal / 10;
    Serial.print(F(" WET = "));
    Serial.println(wetValue);
   
    // Show results
    Serial.println(F("\n Calibration Complete"));
    Serial.print(F("dryValue = "));
    Serial.println(dryValue);
    Serial.print(F("wetValue = "));
    Serial.println(wetValue);
    Serial.println();
}

// ============ KEYBOARD STUFF ============
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
        case '?':
            printHelp();
            break;
    }
  }
}

// ============ HELP MENU ============
void printHelp() {
    Serial.println(F("\n=== AVAILABLE COMMANDS ==="));
    Serial.println(F("  '1' - Manual pump (3 seconds)"));
    Serial.println(F("  '0' - Stop pump immediately"));
    Serial.println(F("  'c' - Calibrate soil sensor"));
    Serial.println(F("  's' - Show status now"));
    Serial.println(F("  'h' - Show this help"));
    Serial.println(F("===========================\n"));
}
