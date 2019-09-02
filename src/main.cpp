/*
Connects to WiFi using MULTI to allow connection to either Kercem2 or Workshop
Testing hardware debounce and long / short press detection
Author: Robin Harris
Date: 02-09-2019
*/


#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

ESP8266WiFiMulti wifiConnection;

// Global variables
unsigned long wifiTimeoutDelay = 3000; // milliseconds to wait for a connection before aborting
volatile unsigned long buttonDownMillis; // start of buttonDown period
volatile unsigned long buttonUpMillis; // end of buttonDown period
volatile bool buttonState = false; // false = up, true = down
bool oldButtonState = false;
int total = 0; // used to keep track of the total number of interrrupts that have occurred

// pin to monitor the button
const byte interruptPin = 13;

void ICACHE_RAM_ATTR ReleaseButton();

// The ISR - notice the attribute ICACHE_RAM_ATTR must be used to it is held in IRAM
// This ISR deals with a button press
void ICACHE_RAM_ATTR PushButton() {
    buttonState = true;
    buttonDownMillis = millis();
    detachInterrupt(digitalPinToInterrupt(interruptPin));
    attachInterrupt(digitalPinToInterrupt(interruptPin), ReleaseButton, RISING);
}

// The ISR - notice the attribure ICACHE_RAM_ATTR must be used to it is held in IRAM
// This ISR deals with a button release
void ICACHE_RAM_ATTR ReleaseButton() {
    buttonState = false;
    buttonUpMillis = millis();
    detachInterrupt(digitalPinToInterrupt(interruptPin));
    attachInterrupt(digitalPinToInterrupt(interruptPin), PushButton, FALLING);
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
  pinMode(interruptPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(interruptPin), PushButton, FALLING);
}

void loop() {
  static unsigned long pushDuration;
  static int totalShort = 0;
  static int totalLong = 0;
  // button goes from up to down 
  if (oldButtonState == false && buttonState == true){
      oldButtonState = true;
  }
  // button goes from down (true) to false take action
  if (oldButtonState == true && buttonState == false){
    pushDuration = buttonUpMillis - buttonDownMillis;
    oldButtonState = false; 

    // deal with short pushes
    if (pushDuration < 400){
        totalShort++;
    }

    // deal with long pushes
    else if (pushDuration >= 400){
        totalLong++;
        }
        
    Serial.printf("Duration of button press: %d\n", pushDuration);
    Serial.printf("Total short pushes %d\t, Total long pushes %d\n", totalShort, totalLong);
  }
}