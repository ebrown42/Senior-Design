#include <Arduino.h>
#include "Pump.h"what

PumpControl pump;
Sensors sensors;
DataLogger dataLogger;
BluetoothComm bluetooth;

struct SystemState {
    float soilMoisture[3];
    float temp; //temperature
    float humidity;
    float batteryVolt;

    bool pumpActive;
    bool sdCardOK;
    bool systemError;
    unsigned long lastWateringTime;
    unsigned long lastSensorReadTime;
    unsigned int wateringCyclesToday;
} state;

void setup();
void loop();
void readAllSensors();
void evaluateIrrigation();
void startWateringCycle();
void logSensorData();
void checkSystemHealth();
void handleBluetoothCommands();
void enterSleepMode();
void calibrateSensors();

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=================================");
    Serial.println("SOLAR POWERED AUTOMATED IRRIGATION");
    Serial.println("ESP32 Irrigation Controller v1.0");
    Serial.println("=================================\n");

    Serial.print("Initializing pump... ");
    pump.begin(PUMP_RELAY_PIN);
    Serial.println("OK");
    
    Serial.print("Initializing sensors... ");
    sensors.begin(DHT_PIN, SOIL_MOISTURE_PINS);
    Serial.println("OK");
    
    Serial.print("Initializing SD card... ");
    state.sdCardOK = dataLogger.begin(SD_CS_PIN);
    if (state.sdCardOK) {
        Serial.println("OK");
        dataLogger.writeHeader();
    } else {
        Serial.println("FAILED - Logging to serial only");
    }
    
    Serial.print("Initializing Bluetooth... ");
    bluetooth.begin("SolarIrrigation");
    Serial.println("OK");

    Serial.print("Running pump self-test...");
    if (pump.selfTest()) {
        Serial.println("PASSED");
    } else {
        Serial.println("FAILED - Check relay wiring");
        state.systemError = true;
    }

    Serial.print("Priming pump...");
    pump.primePump();
    Serial.println("OK");

    calibrateSensors();

    state.lastSensorReadTime = millis();
    state.lastWateringTime = 0;
    state.wateringCyclesToday = 0;
    stat.systemError = false;

    Serial.println("\n=== SYSTEM READY ===\n");
}

//Loop
void loop() {
    unsigned long currentMillis = millis();

    //Read Sensors
    if (currentMillis - state.lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        readAllSensors();
        logSensorData();
        evaluateIrrigation();
        state.lastSensorReadTime = currentMillis;
    }

    state.pumpActive = pump.isActive(); //check pump status

    handleBluetoothCommands(); //Bluetooth

    //Battery
    static unsigned long lastBatteryCheck = 0;
    if (currentMillis - lastBatteryCheck >= BATTERY_CHECK_INTERVAL) {
        state.batteryVoltage = readBatteryVoltage();
        if (state.batteryVoltage < BATTERY_LOW_THRESHOLD) {
            Serial.println("WARNING: Low battery voltage!");
            enterSleepMode();  // Save power
        }
        lastBatteryCheck = currentMillis;
    }

    //health check
    static unsigned long lastHealthCheck = 0;
    if (currentMillis - lastHealthCheck >= 300000) {
        checkSystemHealth();
        lastHealthCheck = currentMillis;
    }

    //Reset Day Counter
    static unsigned long lastDayReset = 0;
    if (currentMillis - lastDayReset >= 86400000) {  // 24 hours
        state.wateringCyclesToday = 0;
        lastDayReset = currentMillis;
    }

    delay(100);
}

//read sensors
void readAllSensors() {
    Serial.println("\n--- Reading Sensors ---");
    
    // Read soil moisture sensors
    for (int i = 0; i < 3; i++) {
        state.soilMoisture[i] = sensors.readSoilMoisture(i);
        Serial.print("Soil ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(state.soilMoisture[i]);
        Serial.print("% ");
    }
    
    // Read DHT22
    state.temperature = sensors.readTemp();
    state.humidity = sensors.readHumidity();
    
    Serial.print(" | Temp: ");
    Serial.print(state.temp);
    Serial.print("°F");
    Serial.print(" | Humidity: ");
    Serial.print(state.humidity);
    Serial.println("%");
}

//check irrigation needs
void evaluateIrrigation() {
    // Calculate average soil moisture
    float avgMoisture = 0;
    for (int i = 0; i < 3; i++) {
        avgMoisture += state.soilMoisture[i];
    }
    avgMoisture /= 3.0;

    //check if needs water
    bool needsWater = (
        avgMoisture < MOISTURE_THRESHOLD &&           // Soil is dry
        state.temperature >= MIN_TEMP &&              // Not too cold
        state.temperature <= MAX_TEMP &&              // Not too hot
        !state.pumpActive &&                          // Pump not already running
        state.batteryVoltage > BATTERY_MIN_VOLTAGE && // Enough battery
        (millis() - state.lastWateringTime) > WATERING_COOLDOWN &&  // Not too frequent
        state.wateringCyclesToday < MAX_CYCLES_PER_DAY  // Daily limit
    );
    
    if (needsWater) {
        // Calculate water amount based on deficit
        float deficit = (MOISTURE_THRESHOLD - avgMoisture) / 10.0;
        unsigned int waterTime = BASE_WATER_TIME * deficit;
        waterTime = constrain(waterTime, MIN_WATER_TIME, MAX_WATER_TIME);
        
        startWateringCycle(waterTime);
    }
}

//Start Watering Cycle
void startWateringCycle(unsigned int duration) {
    Serial.println("\n=== STARTING WATERING CYCLE ===");
    Serial.print("Duration: ");
    Serial.print(duration);
    Serial.println(" seconds");
    Serial.print("Average soil moisture: ");
    Serial.print((state.soilMoisture[0] + state.soilMoisture[1] + state.soilMoisture[2]) / 3.0);
    Serial.println("%");
    
    // Activate pump
    pump.activate(duration);
    state.pumpActive = true;
    state.lastWateringTime = millis();
    state.wateringCyclesToday++;
    
    // Log event
    String event = "Watering started," + String(duration) + "s";
    if (state.sdCardOK) {
        dataLogger.logEvent(event);
    }
    
    // Send Bluetooth notification
    bluetooth.sendNotification("Watering started: " + String(duration) + "s");
    
    Serial.println("=== WATERING COMPLETE ===\n");
}

//Log Sensor Data
void logSensorData() {
    
}

