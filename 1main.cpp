#include <Arduino.h>

#define SOIL_PIN  // need number maybe 32
#define RELAY_PIN  // need number maybe 4
#define DRY_VALUE 3000  // Adjust this after testing

bool pumpActive = false;
unsigned long pumpStopTime = 0;

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("Irrigation Ready");
}

void loop() {
    // Auto shutoff
    if (pumpActive && millis() >= pumpStopTime) {
        digitalWrite(RELAY_PIN, LOW);
        pumpActive = false;
        Serial.println("Pump OFF");
    }
    
    // Read sensor every 2 seconds
    static unsigned long lastRead = 0;
    if (millis() - lastRead >= 2000) {
        int raw = analogRead(SOIL_PIN);
        Serial.print("Soil: ");
        Serial.println(raw);
        
        // If soil is dry and pump not running
        if (!pumpActive && raw > DRY_VALUE) {
            Serial.println("WATERING");
            digitalWrite(RELAY_PIN, HIGH);
            pumpActive = true;
            pumpStopTime = millis() + 5000;  // 5 seconds
        }
        
        lastRead = millis();
    }
}