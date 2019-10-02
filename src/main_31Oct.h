/*
Connects to WiFi using MULTI to allow connection to either Kercem2 or workshop
Hardware debounce with interrupts on falling and rising to detect press and release.
Time pressed counted and short / long press decoded.
Reads INA219 each time round LOOP.
Modified 25th September 2019
    - completely restructured
ToDo:
   - 
   - 
Author: Robin Harris
Date: 25-09-2019
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <SSD1306.h>
#include <OLEDDisplayUi.h>
#include <Adafruit_INA219.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <FS.h>
#include "font.h"

ESP8266WiFiMulti wifiConnection;
ESP8266WebServer server(80);

// Initialize the OLED display using Wire library
SSD1306Wire display(0x3c, D3, D4);
OLEDDisplayUi ui(&display);
Adafruit_INA219 ina219;

// Global variables
unsigned long wifiTimeoutDelay = 3000;   // milliseconds to wait for a connection before aborting
volatile unsigned long buttonDownMillis; // start of buttonDown period
volatile unsigned long buttonUpMillis;   // end of buttonDown period
volatile bool buttonState = false;       // false = up, true = down
bool oldButtonState = false;
// global variables ready for display
float displayBusVoltage = 0; // voltage after the shunt resistor
float displayCurrent_mA = 0;
float displaySupplyVoltage = 0; // voltage of the supply
float displayPower_mW = 0;
float displayEnergy_mWH = 0;
char displayString[100]; // holds string ready to display
String logFile;
bool loggingActive = false;
const int loggingInterval[] = {500, 1000, 5000, 10000, 30000, 60000, 300000, 600000}; // mS between log updates
const int numberOfIntervals = sizeof(loggingInterval) / sizeof(loggingInterval[0]);
byte loggingIntervalIndex = 0;
unsigned long fileUpdateInterval = loggingInterval[0];
const unsigned long displayInterval = 500; // mS between OLED updates
// pin to monitor the button
const byte interruptPin = 5; //GPIO12 = D6 GPIO05 = D1
// end globals

//prototype function definitions
void handleDisplayData(float, float, float, float);
void handleFileData(float, float, float, float);
void printDirectory();
void deleteFile();
bool loadFromSpiffs(String path);
void handleOther();
String updateFileName();
void ICACHE_RAM_ATTR ReleaseButton();
// end prototype functions

// frame 1 on ui
void running(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_10);
    display->clear();
    display->drawString(0 + x, 0 + y, "Vin: " + String(displaySupplyVoltage, 3));
    display->drawString(0 + x, 12 + y, "Vload: " + String(displayBusVoltage, 3));
    display->drawString(0 + x, 24 + y, "current mA: " + String(displayCurrent_mA, 1));
    display->drawString(0 + x, 36 + y, "Power mW: " + String(displayPower_mW, 0));
    display->drawString(0 + x, 48 + y, "Energy mWH: " + String(displayEnergy_mWH, 3));
}

// frame 2 on ui
void menu(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->clear();
    display->setFont(Arimo_Bold_16);
    if (loggingInterval[loggingIntervalIndex] < 1000)
    {
        display->drawString(20 + x, 24 + y, String(loggingInterval[loggingIntervalIndex]) + " mS");
    }
    else
    {
        display->drawString(20 + x, 24 + y, String(loggingInterval[loggingIntervalIndex]) + " S");
    }
}

// indicator when logging is running
void overlayLogging(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->setFont(ArialMT_Plain_10);
    if (loggingActive)
    {
        display->drawString(128, 0, "Logging");
    }
    else
    {
        display->drawString(128, 0, "       ");
    }
}

FrameCallback frames[] = {running, menu};
OverlayCallback overlays[] = {overlayLogging};
int overlaysCount = 1;
int framesCount = 2;

// The ISR - notice the attribute ICACHE_RAM_ATTR must be used to it is held in IRAM
// This ISR deals with a button press
void ICACHE_RAM_ATTR PushButton()
{
    buttonState = true;
    buttonDownMillis = millis();
    detachInterrupt(digitalPinToInterrupt(interruptPin));
    attachInterrupt(digitalPinToInterrupt(interruptPin), ReleaseButton, RISING);
}

// The ISR - notice the attribure ICACHE_RAM_ATTR must be used to it is held in IRAM
// This ISR deals with a button release
void ICACHE_RAM_ATTR ReleaseButton()
{
    buttonState = false;
    buttonUpMillis = millis();
    detachInterrupt(digitalPinToInterrupt(interruptPin));
    attachInterrupt(digitalPinToInterrupt(interruptPin), PushButton, FALLING);
}

void setup()
{
    // start a serial monitor
    Serial.begin(115200);
    delay(10);
    // display and ui stuff
    ui.setTargetFPS(10);
    ui.disableAllIndicators();
    ui.setFrameAnimation(SLIDE_LEFT);
    ui.setFrames(frames, framesCount);
    ui.setOverlays(overlays, overlaysCount);
    ui.disableAutoTransition();

    display.init();
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 20, "Connecting to WiFi");
    display.display();

    wifiConnection.addAP("Kercem2", "E0E3106433F4");
    wifiConnection.addAP("workshop", "workshop");

    // try to make a wifi connection with a timeout in case we are out of range
    unsigned long startConnectionMillis = millis();
    while (wifiConnection.run() != WL_CONNECTED && (millis() < startConnectionMillis + wifiTimeoutDelay))
    {
        Serial.print(".");
        delay(500);
    }
    if (WiFi.status() != WL_CONNECTED)
    {
        display.drawString(0, 20, "No wifi connection found");
        display.display();
    }
    else
    {
        sprintf(displayString, "\nConnected to %s\n\n", WiFi.SSID().c_str());
        display.clear();
        display.drawString(0, 20, displayString);

        // Print the IP address
        sprintf(displayString, "\nIP address: %s\n\n", WiFi.localIP().toString().c_str());
        display.drawString(0, 40, displayString);
        display.display();
    }
    pinMode(interruptPin, INPUT);
    attachInterrupt(digitalPinToInterrupt(interruptPin), PushButton, FALLING);
    server.on("/list", HTTP_GET, printDirectory);
    server.on("/delete", HTTP_GET, deleteFile);
    server.on("/", HTTP_GET, printDirectory);
    server.onNotFound(handleOther);
    if (WiFi.status() == WL_CONNECTED)
    {
        MDNS.begin("cv");
        server.begin();
    }
    SPIFFS.begin();
    ina219.begin();
    ina219.setCalibration_16V_400mA();
    delay(2000); // time to read IP address
    ui.init();   // start the ui
    // now read the old filename and increment it
    logFile = updateFileName();
}

void loop()
{
    static float shuntVoltage = 0; // voltage across the shunt resistor
    static float busVoltage = 0;   // voltage after the shunt resistor
    static float current_mA = 0;
    static float energy_mWH = 0;
    static unsigned long previousLoopMillis = 0;
    static bool menuMode = false; // false = running
    static unsigned long pushDuration;
    static int totalShort = 0;
    static int totalLong = 0;
    ui.update();
    MDNS.update();
    // button goes from up to down
    if (oldButtonState == false && buttonState == true)
    {
        oldButtonState = true;
    }
    // button goes from down (true) to false take action
    if (oldButtonState == true && buttonState == false)
    {
        pushDuration = buttonUpMillis - buttonDownMillis;
        oldButtonState = false;

        // deal with short pushes
        if (pushDuration < 400 && !menuMode)
        {
            totalShort++;
            loggingActive = !loggingActive;
        }

        else if (pushDuration < 400 && menuMode)
        {
            loggingIntervalIndex = (loggingIntervalIndex + 1) % numberOfIntervals;
        }

        // deal with long pushes
        else if (pushDuration >= 400 && !menuMode)
        {
            menuMode = true;
            loggingActive = false;
            ui.switchToFrame(1);
            totalLong++;
        }
        else if (pushDuration >= 400 && menuMode)
        {
            ui.switchToFrame(0);
            menuMode = false;
            totalLong++;
            fileUpdateInterval = (loggingInterval[loggingIntervalIndex]);
        }
    }

    // handle client requests if we have a WiFi connection
    if (WiFi.status() == WL_CONNECTED)
    {
        server.handleClient();
    }
    // get the latest values from the ina219
    unsigned long millisNow = millis();
    shuntVoltage = ina219.getShuntVoltage_mV(); //the voltage ACROSS the shunt
    busVoltage = ina219.getBusVoltage_V();      //the voltage AFTER the shunt
    current_mA = ina219.getCurrent_mA();
    energy_mWH += busVoltage * current_mA * (millisNow - previousLoopMillis) / 3600000;
    previousLoopMillis = millisNow;
    handleDisplayData(shuntVoltage, busVoltage, current_mA, energy_mWH);
    handleFileData(shuntVoltage, busVoltage, current_mA, energy_mWH);
}

void handleDisplayData(float shuntVoltage, float busVoltage, float current, float energy)
{
    static float cumShuntVoltage = 0;
    static float cumBusVoltage = 0;
    static float cumCurrent = 0;
    static int numberOfReadings = 0;
    unsigned long millisNow;
    static unsigned long previousMillis = 0;

    millisNow = millis();
    previousMillis = millisNow;
    cumShuntVoltage += shuntVoltage;
    cumBusVoltage += busVoltage;
    cumCurrent += current;
    numberOfReadings++;

    // average for the interval is copied to global variables for display
    if ((millisNow - previousMillis) > displayInterval)
    {
        displaySupplyVoltage = (cumBusVoltage + (cumShuntVoltage / 100)) / numberOfReadings;
        displayCurrent_mA = cumCurrent / numberOfReadings;
        displayBusVoltage = cumBusVoltage / numberOfReadings;
        displayPower_mW = displayBusVoltage * displayCurrent_mA;
        displayEnergy_mWH = energy;
        previousMillis = 0;
        numberOfReadings = 0;
        cumBusVoltage = 0;
        cumCurrent = 0;
        cumShuntVoltage = 0;
    }
}

void handleFileData(float shuntVoltage, float busVoltage, float current, float energy)
{
    static float cumShuntVoltage = 0;
    static float cumBusVoltage = 0;
    static float cumCurrent = 0;
    static int numberOfReadings = 0;
    unsigned long millisNow;
    static unsigned long previousMillis = 0;

    millisNow = millis();
    cumShuntVoltage += shuntVoltage;
    cumBusVoltage += busVoltage;
    cumCurrent += current;
    numberOfReadings++;

    // average for the interval is written to the file
    if ((millisNow - previousMillis) > fileUpdateInterval)
    {
        File cvLogFile;
        if (!loggingActive)
        {
            return;
        }
        String lineToOutput = String(millisNow);
        lineToOutput += ",";
        lineToOutput += String((cumBusVoltage + (cumShuntVoltage / 100)) / numberOfReadings, 3);
        lineToOutput += ",";
        lineToOutput += String(cumBusVoltage / numberOfReadings, 3);
        lineToOutput += ",";
        lineToOutput += String(cumCurrent / numberOfReadings, 1);
        lineToOutput += ",";
        lineToOutput += String(cumBusVoltage * cumCurrent, 0);
        lineToOutput += ",";
        lineToOutput += String(energy, 3);
        cvLogFile = SPIFFS.open(logFile, "a");
        if (cvLogFile)
        {
            cvLogFile.println(lineToOutput);
            cvLogFile.close();
        }
        previousMillis = millisNow;
        numberOfReadings = 0;
        cumBusVoltage = 0;
        cumCurrent = 0;
        cumShuntVoltage = 0;
    }
}

void printDirectory()
{
    String content = "";
    String path = "";
    Dir dir = SPIFFS.openDir(path);
    while (dir.next())
    {
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

bool loadFromSpiffs(String path)
{
    String dataType = "text/plain";
    if (path.endsWith("/"))
    {
        printDirectory(); //this is where index.htm is created
        return true;
    }
    if (path.endsWith(".src"))
        path = path.substring(0, path.lastIndexOf("."));
    else if (path.endsWith(".htm"))
        dataType = "text/html";
    else if (path.endsWith(".css"))
        dataType = "text/css";
    else if (path.endsWith(".js"))
        dataType = "application/javascript";
    else if (path.endsWith(".png"))
        dataType = "image/png";
    else if (path.endsWith(".gif"))
        dataType = "image/gif";
    else if (path.endsWith(".jpg"))
        dataType = "image/jpeg";
    else if (path.endsWith(".ico"))
        dataType = "image/x-icon";
    else if (path.endsWith(".xml"))
        dataType = "text/xml";
    else if (path.endsWith(".pdf"))
        dataType = "application/pdf";
    else if (path.endsWith(".zip"))
        dataType = "application/zip";
    else if (path.endsWith(".csv"))
        dataType = "application/octet-stream";

    File dataFile = SPIFFS.open(path.c_str(), "r"); //open file to read
    if (!dataFile)                                  //unsuccesful open?
    {
        Serial.print("Don't know this as a command and it's not a file in SPIFFS : ");
        Serial.println(path);
        return false;
    }

    if (server.streamFile(dataFile, dataType) != dataFile.size())
    {
    } //a lot happening here...
    dataFile.close();
    return true;
}

void deleteFile()
{
    Serial.print("Argument received: ");
    String fileToDelete = server.arg(0);
    Serial.println(fileToDelete);

    if (SPIFFS.exists(fileToDelete))
    {
        Serial.println("Found it");
        SPIFFS.remove(fileToDelete);
    }
    else
    {
        Serial.println("Does not exist");
    }
    printDirectory();
}

void handleOther()
{
    Serial.println(server.uri()); // let's see what we are asked for
    if (loadFromSpiffs(server.uri().substring(1)))
        return; //gotcha - it's a file in SPIFFS

    String message = "Not Found\n\n"; //or not...
    message += "URI: ";               //make a 404 response
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
    printDirectory();
    // server.send(404, "text/plain", message);
}

String updateFileName()
{
    String oldFileName;
    File file = SPIFFS.open("nameForFile.txt", "r");
    oldFileName = file.readStringUntil('.');
    file.close();
    String newFileName = String((oldFileName.toInt()) + 1);
    // go back to 1 when 99 is reached
    if (newFileName.toInt() > 99)
    {
        newFileName = 1;
    }
    newFileName += ".csv";

    file = SPIFFS.open("nameForFile.txt", "w");
    file.println(newFileName);
    file.close();

    return newFileName;
}
