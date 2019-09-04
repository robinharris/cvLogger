#include <Wire.h>
#include <SSD1306.h>
#include <OLEDDisplayUi.h>
#include <SPI.h>
#include <Adafruit_INA219.h>
#include "font.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>


// Initialize the OLED display using Wire library
SSD1306  display(0x3c, D1, D2);
Adafruit_INA219 ina219;

// char ssid[] = "C4Di_Management";
// char pass[] = "c4d1manag3m3nt4747";
char ssid[] = "workshop";
char pass[] = "workshop";

ESP8266WebServer server(80);

File cvLogFile;

unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "0.pool.ntp.org"; //"time.nist.gov";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

unsigned long previousMillis = 0;
unsigned long previousDisplayMillis = 0;
//set the sample interval here (ms)
unsigned long interval = 900000;

//debounce variables
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 30; //mS
int lastButtonPressed = HIGH; //used to identify HIGH to LOW button transition
int buttonPressed = HIGH; //current button state
int lastButtonReading = HIGH;
int status = LOW;  //LOW - not running  HIGH - running and recording
const int buttonPin = D1; //connect button with pull-up

unsigned long timestamp; // holds the Unix time
String fileName; //given a value of timestamp + .csv

float shuntVoltage = 0; // voltage across the shunt resistor
float wemosVoltage = 0; // voltage after the shunt resistor 
float current_mA = 0;
float supplyVoltage = 0; // voltage of the supply
// File cvLogFile;
int counter = 0;

//prototype function definitions
void displaydata();
void ina219values();
void checkButton();
unsigned long getNTPtime();
unsigned long sendNTPpacket(IPAddress& address);
void printDirectory();
bool loadFromSpiffs(String path);
void handleOther();


void setup() {
  pinMode (buttonPin, INPUT);
  Serial.begin(9600);
  WiFi.begin(ssid, pass);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  MDNS.begin("currentVoltageLogger");
  MDNS.addService("http", "tcp", 80);

  server.on("/list", HTTP_GET, printDirectory);
  server.on("/", HTTP_GET, printDirectory);
  server.onNotFound(handleOther);
  server.begin();
  udp.begin(localPort);
  SPIFFS.begin();
  display.init();
  ina219.begin();
}

void loop() {
  checkButton();
  server.handleClient();
  if (status == LOW) { // if status = LOW go back to start of loop
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(DejaVu_LGC_Sans_Mono_Bold_16);
    display.drawString(0,20,"READY");
    display.display();
    return;
  }
  unsigned long currentMillis = millis();

  //get new values and update display every 500mS
  if (currentMillis - previousDisplayMillis >= 500){
    ina219values();
    displaydata();
    previousDisplayMillis = currentMillis;
  }
  // when the interval has passed write the latest values to file
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    // write the reading to the file
    cvLogFile = SPIFFS.open(fileName, "a");
    if (cvLogFile) {
      String lineToOutput = String(currentMillis);
      lineToOutput += ",";
      lineToOutput += String(supplyVoltage);
      lineToOutput += ",";
      lineToOutput += String(wemosVoltage);
      lineToOutput += ",";
      lineToOutput += String(current_mA);
      cvLogFile.println(lineToOutput);
      cvLogFile.close();
    }
  }
}

void displaydata() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(DejaVu_LGC_Sans_Mono_Bold_16);
  display.drawString(0,0,"Vbat: " + String(supplyVoltage));
  display.drawString(0,20,"Vload: " + String(wemosVoltage));
  display.drawString(0,40,"mA: " + String(current_mA));
  display.display();

}

void ina219values() {
  shuntVoltage = ina219.getShuntVoltage_mV(); //the voltage ACROSS the shunt
  wemosVoltage = ina219.getBusVoltage_V(); //the voltage AFTER the shunt 
  current_mA =  ina219.getCurrent_mA(); 
  supplyVoltage = wemosVoltage + (shuntVoltage / 1000);  //derived voltage of the supply (V+)
}

void checkButton() {
  //read state of button
  int buttonReading = digitalRead(buttonPin);

  // if button has changed start timing by making lastDebounceTime
  // equal to millis()
  if (buttonReading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  //when the debounce delay has been exceeded and the buttonPressed is different
  //to lastButtonReading set buttonPressed equal to new state
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (buttonReading != buttonPressed){
      buttonPressed = !buttonPressed;
      if (lastButtonPressed == HIGH) {
        status = ! status;
        if (status == HIGH){ 
          //just starting to sample so create a new file name
          timestamp = getNTPtime();
          fileName = String(timestamp);
          fileName.remove(8); //shorten the file name to 8 chars
          fileName += ".csv";
        }

        lastButtonPressed = !lastButtonPressed; //toggle last button state
      }
      else lastButtonPressed = !lastButtonPressed; //toggle last button state but 
      //do not change status on LOW to HIGH transitions
    }
  }
lastButtonReading = buttonReading;
}

unsigned long getNTPtime() {
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP); 

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);
  
  int cb = udp.parsePacket();
  while (!cb) {
    delay(500);
  }
  // We've received a packet, read the data from it
  udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

  //the timestamp starts at byte 40 of the received packet and is four bytes,
  // or two words, long. First, esxtract the two words:
  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long secsSince1900 = highWord << 16 | lowWord;

  // now convert NTP time into everyday time:
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  unsigned long epoch = secsSince1900 - seventyYears;
  // print Unix time:
  return (epoch);
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void printDirectory() {
  String content = "";
  String path = "";
  Dir dir = SPIFFS.openDir(path);
  while (dir.next()) {
    content += (dir.fileName());
    content += "    ";
    File f = dir.openFile("r");
    content += (f.size());
    content += "\n";
    f.close();
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain", "");
  server.sendContent(content);
}

bool loadFromSpiffs(String path){
  String dataType = "text/plain";
  if (path.endsWith("/")) {
    printDirectory(); //this is where index.htm is created
    return true;
  }

  if (path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if (path.endsWith(".htm")) dataType = "text/html";
  else if (path.endsWith(".css")) dataType = "text/css";
  else if (path.endsWith(".js")) dataType = "application/javascript";
  else if (path.endsWith(".png")) dataType = "image/png";
  else if (path.endsWith(".gif")) dataType = "image/gif";
  else if (path.endsWith(".jpg")) dataType = "image/jpeg";
  else if (path.endsWith(".ico")) dataType = "image/x-icon";
  else if (path.endsWith(".xml")) dataType = "text/xml";
  else if (path.endsWith(".pdf")) dataType = "application/pdf";
  else if (path.endsWith(".zip")) dataType = "application/zip";
  else if (path.endsWith(".csv")) dataType = "application/octet-stream";

  File dataFile = SPIFFS.open(path.c_str(), "r");   //open file to read
  if (!dataFile)  //unsuccesful open?
  {
    Serial.print("Don't know this as a command and it's not a file in SPIFFS : ");
    Serial.println(path);
    return false;
  }

  if (server.streamFile(dataFile, dataType) != dataFile.size())
  {}    //a lot happening here...
  
  dataFile.close();
  return true;
}

void handleOther(){
  Serial.println(server.uri());   // let's see what we are asked for
  if (loadFromSpiffs(server.uri().substring(1)))
    return;   //gotcha - it's a file in SPIFFS

  String message = "Not Found\n\n";           //or not...
  message += "URI: ";     //make a 404 response
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " NAME:" + server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  Serial.println(message);
}
