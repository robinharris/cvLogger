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
}

void loop() {

}