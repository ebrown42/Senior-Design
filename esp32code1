#include <WiFi.h>
BluetoothSerial SerialBT;
const char* ssid = "UWyo Guest"
const char* password = ""

void setup() {

  Serial.begin(N16R8);
  SerialBT.begin("ESP32test");
  delay{1000};

  WiFi.begin(ssid, password);
  Serial.println("\nconnecting");

  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }

  // put your setup code here, to run once:

}

void loop() {

  if (Serial.available()) {
    SerialBT.write(Serial.read());
  }
  if(SerialBT.available()) {
    Serial.write(SerialBT.read());
  }
  delay(20);
  // put your main code here, to run repeatedly:

}