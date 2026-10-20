#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(2000);                 // give the USB serial port time to connect
  Serial.println("CDH boot");
}

void loop() {
  Serial.println("alive");
  delay(1000);
}