/*
Connects to WiFi using MULTI to allow connection to either Kercem2 or Workshop
Next step is to add interrupt detection of button press
Author: Robin Harris
Date: 30-08-2019
*/


#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

ESP8266WiFiMulti wifiConnection;
// Global variables
unsigned long wifiTimeoutDelay = 3000; // milliseconds to wait for a connection before aborting

// pin to monitor the button
const byte interruptPin = 13;

// used to keep track of the total number of interrrupts that have occurred
int total = 0;

// used to signal if a button press has been detected.  Volatile because it is used inside the ISR
volatile boolean buttonPushed = false;

// The ISR - notice the attribure ICACHE_RAM_ATTR must be used to it is held in IRAM
void ICACHE_RAM_ATTR PushButton() {
  // declared static so that it persists between one call and the next
  volatile static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();
  if (interrupt_time - last_interrupt_time > 500UL)  // ignores interrupts for n milliseconds
  {
    buttonPushed = true;
  }
  last_interrupt_time = interrupt_time;
}

void setup() {
  // start a serial monitor
  Serial.begin(115200);
  delay(10);

  wifiConnection.addAP("Kercem2", "E0E3106433F4");
  wifiConnection.addAP("workshop", "workshop");
  
  unsigned long startConnectionMillis = millis();
  while (wifiConnection.run() != WL_CONNECTED && (millis()<startConnectionMillis+wifiTimeoutDelay)){
    Serial.print(".");
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED){
    Serial.println("No wifi connection found");
  }
  else {
    Serial.println("");
    Serial.printf("Connected to %s\n\n", WiFi.SSID().c_str());
    // Print the IP address
    Serial.printf("IP address: %s\n\n", WiFi.localIP().toString().c_str());
  }
  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), PushButton, FALLING);
}

void loop() {
  if (buttonPushed){
    total++;
    Serial.print("An interrupt occured.  Total:\t");
    Serial.println(total);
    buttonPushed = false;
  }
}