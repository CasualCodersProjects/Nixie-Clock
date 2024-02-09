#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Preferences.h>
#include <AsyncUDP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include "index.h"
#include <NTPClient.h>
#include <time.h>
#include <FastLED.h>

#define NUM_LEDS 4
#define DATA_PIN 1


// User Memory - WiFi SSID, PSK, Clock Configuration bits.
Preferences preferences;

// Static Constats. AP IDs, Default UserMem WiFi Value.
const char* APID = "NixeClock";
const char* APSK = "MinesBigger";
const char* GARBAGE_STRING = "C!pbujKY2#4HXbcm5dY!WJX#ns29ff#vEDWmbZ9^d!QfBW@o%Trfj&sPENuVe&sx";

// Global Variables
bool softAPActive = 0;

// Server Stuff
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
AsyncWebServer server(80);

// RGB LEDs
CRGB leds[NUM_LEDS];

// Pins used for segment displays
const int minutes_pin[8] = {18, 16, 15, 17, 9, 11, 12, 10};
const int hours_pin[8] = {38, 36, 35, 37, 13, 21, 33, 14};

void printTime(int hours, int minutes){
  // Serial.println("Printing: " + String(hours) + ":" + String(minutes));
  int hoursT = hours / 10;
  int hoursO = hours % 10;
  int minutesT = minutes / 10;
  int minutesO = minutes % 10;

  digitalWrite(18, (hoursT >> 0) & 1);  // A4
  digitalWrite(16, (hoursT >> 1) & 1);  // B4
  digitalWrite(15, (hoursT >> 2) & 1);  // C4
  digitalWrite(17, (hoursT >> 3) & 1);  // D4

  digitalWrite(9, (hoursO >> 0) & 1);   // A3
  digitalWrite(11, (hoursO >> 1) & 1);  // B3
  digitalWrite(12, (hoursO >> 2) & 1);  // C3
  digitalWrite(10, (hoursO >> 3) & 1);  // D3

  digitalWrite(38, (minutesT >> 0) & 1);  // A2
  digitalWrite(36, (minutesT >> 1) & 1);  // B2
  digitalWrite(35, (minutesT >> 2) & 1);  // C2
  digitalWrite(37, (minutesT >> 3) & 1);  // D2

  digitalWrite(13, (minutesO >> 0) & 1);  // A1
  digitalWrite(21, (minutesO >> 1) & 1);  // B1
  digitalWrite(33, (minutesO >> 2) & 1);  // C1
  digitalWrite(14, (minutesO >> 3) & 1);  // D1
}

// Initial Setup Function
void setup() {
  // Serial for debug. Preferences for UserMem.
  Serial.begin(9600);
  Serial.println("Entered setup");
  preferences.begin("usermem");
  Serial.println("Begin EEPROM");
  SPIFFS.begin();

  setenv("TZ", preferences.getString("timezone", "UTC").c_str(), 1);
  tzset();

  // Set the hours and minutes pins to output mode
  for (int i = 0; i < 8; i++) {
    pinMode(hours_pin[i], OUTPUT);
    pinMode(minutes_pin[i], OUTPUT);
  }
  
  // ---------- WiFi Section ----------
  // Access Point init
  Serial.println("Start Access Point");
  WiFi.softAPsetHostname("nixeclock");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(APID, APSK);
  softAPActive = true;

  // Skip WiFi configuration if no credentials exist.
  if (preferences.getString("WiFiSSID", GARBAGE_STRING) == GARBAGE_STRING || preferences.getString("WiFiPSK", GARBAGE_STRING) == GARBAGE_STRING) {
    Serial.println("WiFi not configured. Skipping network connection.");
  }

  // WiFi Credentials exist. Configure WiFi and attempt to connect.
  else {
    Serial.println("WiFi Configured. Attempting Connection.");
   
    // Begin WiFi with the stored credentials.
    WiFi.setHostname("niceclock");
    WiFi.begin(preferences.getString("WiFiSSID", GARBAGE_STRING).c_str(), preferences.getString("WiFiPSK", GARBAGE_STRING).c_str());
    
    // Attempt to connect for ~5 seconds before continuing.
    while(!WiFi.isConnected() && millis() < 5000) {
      Serial.print(". ");
      delay(100);
    }
    Serial.println();

    // Connection Successful. Teardown and disable AP.
    if (WiFi.isConnected()){
      Serial.println("\nConnection Success. Tearing down AP.");
      WiFi.softAPdisconnect();
      WiFi.mode(WIFI_STA);
      softAPActive = false;
    }

    // Connection unsuccessful. Continue to main loop.
    // Your WiFi is slow, down, or you incorrectly configured your credentials if you're at this point.
    else{
      Serial.println("\nConnection Failed. Please connect to the webportal and enter valid information.");
    }
  }

  // Begin Time Keeping
  timeClient.begin();
  timeClient.update();

  // ---------- Webpage Configuration Section ----------
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false);
  });

  // Route to load style.css file
  server.on("/milligram.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Loading BS CSS");
    request->send(SPIFFS, "/milligram.min.css", "text/css");
  });

  // Route to load style.css file
  server.on("/bootstrap.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Loading BS Script");
    request->send(SPIFFS, "/bootstrap.min.js", "application/javascript");
  });

  // Route to load style.css file
  server.on("/moment.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Loading moment");
    request->send(SPIFFS, "/moment.min.js", "application/javascript");
  });

  // Route to load style.css file
  server.on("/moment-timezone.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Loading moment timezone");
    request->send(SPIFFS, "/moment-timezone.min.js", "application/javascript");
  });

  // Handle HTTP POST requests for the root page
  server.on("/updateWiFi", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("Request Received");

    // Handling input data from the webpage. ALL EEPROM Values updated
    if (request->hasParam("ssid", true) && request->hasParam("psk", true)) {
      String ssid = request->getParam("ssid", true)->value();
      String psk = request->getParam("psk", true)->value();
      if(ssid != ""){
        Serial.println("Updating WiFi Credentials to SSID: " + ssid);
        preferences.putString("WiFiSSID", ssid);
        preferences.putString("WiFiPSK", psk);

        // Begin WiFi with the stored credentials.
        WiFi.setHostname("niceclock");
        WiFi.begin(preferences.getString("WiFiSSID", GARBAGE_STRING).c_str(), preferences.getString("WiFiPSK", GARBAGE_STRING).c_str());
      }
    }

    // Done
    request->send(200);
  });

  // Handle HTTP POST requests for the root page
  server.on("/updateBrightness", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("Request Received");

    // Handling input data from the webpage. ALL EEPROM Values updated
    if (request->hasParam("minBrightnessSlider", true) && request->hasParam("maxBrightnessSlider", true)) {
      int min = request->getParam("minBrightnessSlider", true)->value().toInt();
      int max = request->getParam("maxBrightnessSlider", true)->value().toInt();

      preferences.putInt("minBrightness", min);
      preferences.putInt("maxBrightness", max);
    }

    // Done
    request->send(200);
  });

  // Handle HTTP POST requests for the root page
  server.on("/setTZ", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("Request Received");

    // Handling input data from the webpage
    if (request->hasArg("timezone")) {
      String timezone = request->arg("timezone");
      preferences.putString("timezone", timezone);
      configTzTime(timezone.c_str(), "pool.ntp.org");
      timeClient.update();
    }

    // Done
    request->send(200);
  });

  // Start the server
  server.begin();

  // Begin LEDs
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  fill_solid(leds, NUM_LEDS, CRGB::Purple);
  // leds[0] = CRGB::Purple;
  FastLED.show();
}

void loop() {
  // Connection Successful. Teardown and disable AP.
  if(softAPActive && WiFi.isConnected()){
    Serial.println("Internet Connected. Tearing down AP.");
    WiFi.softAPdisconnect();
    WiFi.mode(WIFI_STA);
    softAPActive = false;
    timeClient.update();
  }

  // Lost connection to the internet. Re-Enable the AP to avoid getting stuck.
  if(!softAPActive && !WiFi.isConnected()) {
    Serial.println("Lost Internet. Restarting AP.");
    WiFi.softAPsetHostname("nixeclock");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(APID, APSK);
    softAPActive = true;
  }

  // Debug logging
  // Reconnect to NTP To update time at midnight every night
  if(timeClient.getMinutes() == 0 && timeClient.getSeconds() == 0){
    timeClient.update();
    delay(1000);
  }
  // Base the time on epoch. This allows DST To work automatically.
  time_t now = timeClient.getEpochTime();
  struct tm *timeinfo = localtime(&now);

  // Print the timezone info to SSDs
  printTime(timeinfo->tm_hour, timeinfo->tm_min);
}
