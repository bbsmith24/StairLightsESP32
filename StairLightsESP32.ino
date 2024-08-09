/*
  Brian Smith
  November 2022
  Copyright (c) 2022 Brian B Smith. All rights reserved.
  Stair lighting controller
  uses MQTT to update OpenHAB (or any other MQTT server)
  
based on ESP32_Credentials
  Brian B Smith
  November 2022
  brianbsmith.com
  info@brianbsmith.com
  
  This is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  This does nothing interesting on its own, except to get credentials and timezone offset (if needed) then connect to 
  WiFi and output the time every minute. Use as a basis for other applications that need WiFi
  
  'ESP32_Credentials' is based on "ESP32: Create a Wi-Fi Manager" tutorial by Rui Santos at Random Nerds Tutorials
  found here: https://randomnerdtutorials.com/esp32-wi-fi-manager-asyncwebserver/
  They have lots of useful tutorials here, well worth a visit!

  changes from original:
    just get wifi credentials, no web server page after connection
    refactored for clarity/ease of use (at least it's more clear to me...)
    added 'clear credentials' to allow for changing between wifi networks (reset, reboots to credentials page, reboots to connection)
    added 'define VERBOSE' for debug statements
    use defines for constant values
    allow undefined local ip, allows router to set ip from DNS
    get time from NTP server and set internal RTC
    added local time offset from GMT and daylight savings time offset to credentials page - hours now, could be enhanced with a combo box for timezone

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.
*/
//#define DEBUG_VERBOSE                // basic output for debugging
//#define DEBUG_EXTRA_VERBOSE          // extra output for debugging
// wait between wifi and MQTT server connect attempts
#define RECONNECT_DELAY    10000
#define FORMAT_LITTLEFS_IF_FAILED true
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1

#include <WiFi.h>               //
#include <AsyncTCP.h>           // https://github.com/mathieucarbou/AsyncTCP#v3.2.3
#include <ESPAsyncWebServer.h>  // https://github.com/mathieucarbou/ESPAsyncWebServer#v3.1.1
#include <ElegantOTA.h>         //
#include "FS.h"                 // 
#include <LittleFS.h>           // LittleFS_esp32 by loral from Arduino library manager
#include <time.h>               // for NTP time
#include <ESP32Time.h>          // for RTC time  https://github.com/fbiego/ESP32Time
#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>       //
#include <Wire.h>               //
#include <Adafruit_GFX.h>       //
#include <Adafruit_SSD1306.h>   //
#include <Adafruit_NeoPixel.h>  //
// specific to this code
#include "StairLightsESP32.h"

//
// set up display, sensors, wifi, mqtt
//
void setup() 
{
  // Serial port for debugging purposes
  Serial.begin(115200);
  delay(5000);
  Serial.println("");
  Serial.println("BBS 2024");
  Serial.println("ESP32-MQTT Stair Lighting");
  Serial.println("=========================");
  startDelay = millis();
  while((millis() - startDelay) < 1000)
  {}
  
  if(!oledDisplay.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) 
  {
    #ifdef DEBUG_VERBOSE
      Serial.println(F("SSD1306 allocation failed"));
      for(;;); // Don't proceed, loop forever
    #endif
  }
  oledString[0] = "---";
  oledString[1] = "---";
  oledString[2] = "---";
  oledString[3] = "Stairs V1";
  RefreshOLED(1);

  // set CPU freq to 80MHz, disable bluetooth  to save power
  int freq = getCpuFrequencyMhz();
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.printf("Default Freq\n");
    Serial.printf("CPU Freq = %dMhz\n", freq);
    freq = getXtalFrequencyMhz();
    Serial.printf("XTAL Freq = %dMhz\n", freq);
    freq = getApbFrequency();
    Serial.printf("APB Freq = %dHz\n", freq);
  #endif

  setCpuFrequencyMhz(80);
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.printf("Updated Freq\n");
    freq = getCpuFrequencyMhz();
    Serial.printf("CPU Freq = %dMhz\n", freq);
    freq = getXtalFrequencyMhz();
    Serial.printf("XTAL Freq = %dMhz\n", freq);
    freq = getApbFrequency();
    Serial.printf("APB Freq = %dHz\n", freq);
  #endif
  // stop bluetooth
  btStop();
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.printf("Bluetooth disabled\n");
  #endif  

  sprintf(wifiState, "Not connected");
  sprintf(mqttState, "No attempts");

  // initialize LITTLEFS for reading credentials
  LITTLEFS_Init();

  #ifdef DEBUG_EXTRA_VERBOSE
  // list files in LITTLEFS
  LITTLEFS_ListDir(LittleFS, "/", 10);
  LITTLEFS_ListDir(LittleFS, "/patterns/", 10);
  #endif
  // uncomment to clear saved credentials 
  //ClearCredentials();
  // Load values saved in LITTLEFS if any
  LoadCredentials();
  //
  LoadPatterns(LittleFS);
  //
  // try to initalize wifi with stored credentials
  // if getting credentials from config page, reboot after to connect with new credentials
  //
  if(!WiFi_Init()) 
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("No credentials stored - get from locally hosted page");
    #endif
    GetCredentials();
  }
  // generate a randomized MQTT client name
  // YOU CANNOT DUPLICATE CLIENT NAMES!!!! (and expect them both to work anyway...)
  randomSeed(analogRead(0));
  mqttClientID += String(random(0xffff), HEX);
  
  #ifdef DEBUG_VERBOSE
    Serial.print("MQTT Server: ");
    Serial.println(mqtt_server);
    Serial.print("Port: ");
    Serial.println(mqtt_portVal);
    Serial.print("User: ");
    Serial.println(mqtt_user);
    Serial.print("Password: ");
    Serial.println(mqtt_password);
  #endif
  mqttClient.setServer(mqttserverIP, mqtt_portVal);
  mqttClient.setCallback(MQTT_Callback);
  // OTA update server
  // main page is credentials update
  // updates page shows Elegant OTA upload page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("Request from webserver, send page");
    #endif
    request->send(LittleFS, "/wifimanager.html", "text/html");
  });
  server.serveStatic("/", LittleFS, "/");
  //
  // display web page and get credentials from user
  //  
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) 
  {
    int params = request->params();
    for(int i=0;i<params;i++)
    {
      yield();
      const AsyncWebParameter* p = request->getParam(i);
      if(p->isPost())
      {
        // HTTP POST ssid value
        if (p->name() == PARAM_INPUT_1) 
        {
          ssid = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_1, ssid);
          #endif
        }
        // HTTP POST password value
        if (p->name() == PARAM_INPUT_2) 
        {
          pass = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_2, pass);
          #endif
        }
        // HTTP POST local ip value
        if (p->name() == PARAM_INPUT_3) 
        {
          wifiClientName = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_3, ip);
          #endif
        }
        // HTTP POST time zone offset value
        if (p->name() == PARAM_INPUT_4) 
        {
          tz = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_4, tz);
          #endif
        }
        // HTTP POST dst offset value
        if (p->name() == PARAM_INPUT_5) 
        {
          dst = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_5, dst);
          #endif
        }
        // HTTP POST mqtt server ip value
        if (p->name() == PARAM_INPUT_6) 
        {
          mqtt_server = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_6, mqtt_server);
          #endif
        }
        // HTTP POST mqtt port value
        if (p->name() == PARAM_INPUT_7) 
        {
          mqtt_port = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_7, mqtt_port);
          #endif
        }
        // HTTP POST mqtt username value
        if (p->name() == PARAM_INPUT_8) 
        {
          mqtt_user = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_8, mqtt_user);
          #endif
        }
        // HTTP POST mqtt password value
        if (p->name() == PARAM_INPUT_9) 
        {
          mqtt_password = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_9, mqtt_password);
          #endif
        }
      }
    } 
    request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("Store credentials in LITTLEFS and reboot");
    #endif
    SaveCredentials();
  });
  ElegantOTA.begin(&server);    // Start ElegantOTA
  server.begin();

  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Started OTA update page server");
  #endif

  // get time
  UpdateLocalTime();
  // initial MQTT connect - sets up subscriptions so MQTT on/off from server works
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("MQTT_Reconnect in setup()");
  #endif

  redVal = 0;
  greenVal = 0;
  blueVal = 0;
  brightVal = 0;
  patternVal = 0;
  strip.begin();
  strip.setBrightness(255);
  strip.show(); // Initialize all pixels to 'off'
  StripColor(255,   0,   0, 32);
  delay(1000);
  StripColor(  0, 255,   0, 32);
  delay(1000);
  StripColor(  0,   0, 255, 32);
  delay(1000);
  StripColor(255, 255, 255, 32);
  delay(1000);
  StripColor(  255, 255, 255,   5);
  delay(1000);
  StripColor(  0, 0, 0,   0);
  lastUpdate = millis();
  MQTT_Reconnect();
}
//
// main loop
// check for pattern/color changes
//
void loop()
{
  bool colorChange = true;
  bool patternChange = true;
  // only continue if connected to wifi
  if(!wifiConnected)
  {
    oledString[0] = "No WiFi";
    oledString[1] = "No MQTT";
    oledString[2] = "---";
    oledString[3] = "---";
    RefreshOLED(1);
    delay(1000);
    return;
  }
  // let mqtt and client stuff happen to keep watchdog on leash
  // all action happens in the MQTT subscribed message handler
  unsigned long now = millis();
  if((now - lastUpdate) >= INTERVAL_MS)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("call mqttClient.loop in loop()");
    #endif
    lastUpdate = now;
    mqttClient.loop();
  }
  if((patternVal < 0) ||
     (patternVal > (patternCount + 1)))
  {
     patternVal = 0;
  }
  strip.setBrightness(brightVal);
  // user color settings from OpenHAB
  if(patternVal == patternCount)
  {
    StripColor(redVal, greenVal, blueVal, brightVal);
    oledString[2] = "From OpenHAB";
    RefreshOLED(1);
  }
  // color wheel 
  else if(patternVal == (patternCount + 1))
  {
    RainbowCycle(10);
    oledString[2] = "Rainbows";
    RefreshOLED(1);
  }
  // play selected pattern
  else
  {
    PlayPattern(patternVal);
  }
}
//
// ================================ begin device specific functions ========================================
//
void StripColor(uint8_t r, uint8_t g, uint8_t b, uint8_t bright)
{
  redVal = r;
  greenVal = g;
  blueVal = b;
  brightVal = bright;
  strip.setBrightness(bright);
  for(int pixelIdx = 0; pixelIdx < NUMPIXELS; pixelIdx++)
  {
    strip.setPixelColor(pixelIdx, r, g, b);
  }
  strip.show();
}
void RainbowCycle(uint8_t wait) 
{
  #ifdef DEBUG_VERBOSE
  Serial.println("Begin RainbowCycle");
  #endif
  uint16_t i, j;
   // 5 cycles of all colors on wheel
  //for(j=0; j<256*5; j++) 
  unsigned long lastTime = millis();
  for(j=0; j<256; j++) 
  {
    for(i=0; i< strip.numPixels(); i++) 
    {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    lastTime = millis();
    while(millis() - lastTime < wait)
    { }
  }
  #ifdef DEBUG_VERBOSE
  Serial.println("End of RainbowCycle");
  #endif

}
// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) 
{
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}
//
// ================================ begin OLED display functions ========================================
//
void RefreshOLED(int fontSize)
{
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("RefreshOLED");
    for(int idx = 0; idx < 4; idx++)
    {
      Serial.println(oledString[idx]);
    }
  #endif
  oledDisplay.clearDisplay();
  oledDisplay.setTextSize(fontSize);             // Draw 2X-scale text
  oledDisplay.setTextColor(SSD1306_WHITE);
  int y = 0;
  for(int idx = 0; idx < 4; idx++)
  {
    oledDisplay.setCursor(0,y);             // Start at top-left corner
    oledDisplay.print(oledString[idx].c_str());
    y+= 16;
  }
  oledDisplay.display();
}
//
// ================================ begin MQTT functions ========================================
//
// report to MQTT Host
//
void MQTT_Report()
{    
  char deviceStatusStr[256];
  mqttAttemptedReports++;
  if(MQTT_Reconnect())
  {
    mqttSuccessfulReports++;
    //
    // MQTT update
    //    
    sprintf(deviceStatusStr,"WiFi: %s %s | MQTT: %s, %s, Reports %d/%d", wifiState, 
                                                                           connectTimeStr, 
                                                                           (mqtt_Report ? "Reporting" : "Not reporting"),
                                                                           mqttState,
                                                                           mqttSuccessfulReports,
                                                                           mqttAttemptedReports);
    #ifdef DEBUG_VERBOSE
      Serial.println("calling MQTT_PublishTopics()");
    #endif
    delay(100);
    MQTT_PublishTopics();    
    #ifdef DEBUG_VERBOSE
      Serial.println("returned from MQTT_PublishTopics()");
    #endif
    delay(100);
  }
  else
  {
    #ifdef DEBUG_VERBOSE
      Serial.println(">>>>>MQTT update ON, failed to connect<<<<<");    
      Serial.println(deviceStatusStr);
    #endif
  }      
}
//****************************************************************************
//
// (re)connect to MQTT server
//
//****************************************************************************
bool MQTT_Reconnect() 
{
  // Loop until we're reconnected
  mqttAttemptCount = 0;
  bool mqttConnect = mqttClient.connected();
  while (!mqttConnect)// && (mqttAttemptCount < 10)) 
  {
    sprintf(mqttState, "Not connected");
    #ifdef DEBUG_VERBOSE
      Serial.printf("MQTT connect attempt %d ", (mqttAttemptCount + 1));
      Serial.print(" Server IP: >");
      Serial.print(mqttserverIP.toString());
      Serial.print("< Port: >");
      Serial.print(mqtt_portVal);
      Serial.print("< mqttClientID: >");
      Serial.print(mqttClientID);
      Serial.print("< mqtt_user: >");
      Serial.print(mqtt_user);
      Serial.print("< mqtt_password: >");
      Serial.print(mqtt_password);
      Serial.println("<");
    #endif
    // Wait before retrying
    startDelay = millis();
    while((millis() - startDelay) < RECONNECT_DELAY)
    {}
    mqttConnect = mqttClient.connect(mqttClientID.c_str(), mqtt_user.c_str(), mqtt_password.c_str()); 
    mqttAttemptCount++;

    oledString[0] = "No WiFi";
    oledString[1] = "No MQTT";
    oledString[2] = "---";
    oledString[3] = "---";
    RefreshOLED(1);
  }
  if(mqttConnect)
  {
    // we're connected
    #ifdef DEBUG_VERBOSE
      Serial.println("MQTT connected!");
    #endif
    sprintf(mqttState, "Connected");
    startDelay = millis();
    while (millis() - startDelay < RECONNECT_DELAY) 
    {}
    // set up listeners for server updates  
    MQTT_SubscribeTopics();

    oledString[0] = "WiFi & MQTT OK";
    sprintf(tmpStr, "%d LEDs", NUMPIXELS);
    oledString[1] = tmpStr;
    oledString[2] = "---";
    oledString[3] = "---";
    RefreshOLED(1);
    // send pattern names
    for(int patternIdx = 0; patternIdx < patternCount; patternIdx++)
    {
      published_payload[0] = patterns[patternIdx].patternName;
      MQTT_PublishTopic(0);
    }
    // request current settings after reconnect
    published_payload[1] = "Request";
    MQTT_PublishTopic(1);
  }
  else
  {
    #ifdef DEBUG_VERBOSE
      Serial.print("MQTT not connected!");
      Serial.print(" Server IP: ");
      Serial.print(mqttserverIP.toString());
      Serial.print(" Port: ");
      Serial.print(mqtt_portVal);
      Serial.print(" mqttClientID: ");
      Serial.print(mqttClientID.c_str());
      Serial.print(" mqtt_user: ");
      Serial.print(mqtt_user.c_str());
      Serial.print(" mqtt_password: ");
      Serial.println(mqtt_password.c_str());
    #endif
    sprintf(mqttState, "Connect failed");
    
    oledString[1] = "No MQTT";
    oledString[2] = "---";
    oledString[3] = "---";
    RefreshOLED(1);
  }
  return mqttConnect;
}
//****************************************************************************
//
// handle subscribed MQTT message received
// determine which message was received and what needs to be done
//
//****************************************************************************
void MQTT_Callback(char* topic, byte* payload, unsigned int length) 
{
  #ifdef DEBUG_VERBOSE
    Serial.print("MQTT_Callback ");
    Serial.print(topic);
    Serial.print(" ");
    Serial.print((char*)payload);
    Serial.print(" (");
    Serial.print(length);
    Serial.println(")");
  #endif
  String topicStr;
  String payloadStr;
  topicStr = topic;
  for (int i = 0; i < length; i++) 
  {
    payloadStr += (char)payload[i];
  }
  #ifdef DEBUG_VERBOSE
    Serial.print("MQTT_Callback ");
    Serial.print(topicStr);
    Serial.print(" payload  ");
    Serial.println(payloadStr);
  #endif
  if(topicStr == subscribed_topic[0])
  {
    redVal = atoi(payloadStr.c_str());
    redVal = map(redVal, 0, 100, 0, 255);
  }
  if(topicStr == subscribed_topic[1])
  {
    greenVal = atoi(payloadStr.c_str());
    greenVal = map(greenVal, 0, 100, 0, 255);
  }
  if(topicStr == subscribed_topic[2])
  {
    blueVal =  atoi(payloadStr.c_str());
    blueVal = map(blueVal, 0, 100, 0, 255);
  }
  if(topicStr == subscribed_topic[3])
  {
    brightVal =  atoi(payloadStr.c_str());
    #ifdef DEBUG_VERBOSE
    Serial.println("set brightness");
    Serial.print(brightVal);
    Serial.println("%");
    #endif
    brightVal = map(brightVal, 0, 100, 0, MAX_BRIGHT);
    #ifdef DEBUG_VERBOSE
    Serial.print(brightVal);
    Serial.println(" (0-255)");
    #endif
  }
  if(topicStr == subscribed_topic[4])
  {
    patternVal =  atoi(payloadStr.c_str());
  }
  #ifdef DEBUG_VERBOSE
    Serial.print("New pattern: ");
    Serial.println(patternVal);
    Serial.print("New color: 0x");
    Serial.print(redVal, HEX);
    Serial.print(" 0x");
    Serial.print(greenVal, HEX);
    Serial.print(" 0x");
    Serial.print(blueVal, HEX);
    Serial.print(" 0x");
    Serial.println(brightVal, HEX);
  #endif
}
//
// subscribed MQTT topics (get updates from MQTT server)
//
void MQTT_SubscribeTopics()
{ 
  bool subscribed = false;
  for(int idx = 0; idx < MAX_SUBSCRIBE; idx++)
  {
    subscribed = mqttClient.subscribe(subscribed_topic[idx].c_str());
    #ifdef DEBUG_VERBOSE
      Serial.printf("subscribing topic %s %s\n", subscribed_topic[idx].c_str(), (subscribed ? "success!" : "failed"));
    #endif
  }
}
bool MQTT_PublishTopic(int topicID)
{
    bool connected = mqttClient.connected();
  if (!connected)
  {
    #ifdef DEBUG_VERBOSE
      Serial.println("MQTT not connected in MQTT_PublishTopics()");
    #endif
    return connected;
  }
  int pubSubResult = 0;
  //#ifdef DEBUG_VERBOSE
    Serial.print("publishing ");
    Serial.println(published_topic[topicID]);
  //#endif
  pubSubResult = mqttClient.publish(published_topic[topicID].c_str(), published_payload[topicID].c_str());
  //#ifdef DEBUG_VERBOSE
    Serial.print((pubSubResult == 0 ? "FAIL TO PUBLISH Topic: " : "PUBLISHED Topic: "));
    Serial.print(published_topic[topicID]);
    Serial.print(" ");
    Serial.println(published_payload[topicID]);
  //#endif
  return connected;
}
//
// published MQTT topics (send updates to MQTT server)
// if multiple topics could be published, it might be good to either pass in the specific name
// or make separate publish functions
//
bool MQTT_PublishTopics()
{
  bool connected = mqttClient.connected();
  if (!connected)
  {
    #ifdef DEBUG_VERBOSE
      Serial.println("MQTT not connected in MQTT_PublishTopics()");
    #endif
    return connected;
  }
  int pubSubResult = 0;
  for(int idx = 0; idx < MAX_PUBLISH; idx++)
  {
    #ifdef DEBUG_VERBOSE
      Serial.print("publishing ");
      Serial.println(published_topic[idx]);
    #endif
    pubSubResult = mqttClient.publish(published_topic[idx].c_str(), published_payload[idx].c_str());
    #ifdef DEBUG_VERBOSE
      Serial.print((pubSubResult == 0 ? "FAIL TO PUBLISH Topic: " : "PUBLISHED Topic: "));
      Serial.print(published_topic[idx]);
      Serial.print(" ");
      Serial.println(published_payload[idx]);
    #endif
  }
  return connected;
}
//
// ================================ end MQTT functions ========================================
//
// ================================ begin LITTLEFS functions ================================
//
// Initialize LITTLEFS
//
void LITTLEFS_Init() 
{
  if (!LittleFS.begin(true)) 
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("An error has occurred while mounting LITTLEFS");
    #endif
    return;
  }
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("LITTLEFS mounted successfully");
  #endif
}
//
// Read File from LITTLEFS
//
String LITTLEFS_ReadFile(fs::FS &fs, const char * path)
{
  String fileContent = " ";
  char c = ' ';
  #ifdef DEBUG_VERBOSE
  Serial.println("=====================================================");
  Serial.printf("Reading file: %s\n", path);
  #endif
  File file = fs.open(path);
  if (!file)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("- failed to open file for reading");
    Serial.println("=====================================================");
    #endif
    return fileContent;
  }
  if(file.isDirectory())
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("- path is a directory");
    Serial.println("=====================================================");
    #endif
    return  fileContent;
  }
  while (file.available()) 
  {
    c = file.read();
    #ifdef DEBUG_VERBOSE
    Serial.write(c);
    #endif
    fileContent += c;
  }
  file.close();
  #ifdef DEBUG_VERBOSE
  Serial.println();
  Serial.println("=====================================================");
  #endif
  return fileContent;
}
//
// Write file to LITTLEFS
//
void LITTLEFS_WriteFile(fs::FS &fs, const char * path, const char * message)
{
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.printf("Writing >>%s<< to file: %s ", message, path);
  #endif
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("- failed to open file for writing");
    #endif
    return;
  }
  if(file.print(message))
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("- file written");
    #endif
  }
   else 
   {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("- file write failed");
    #endif
  }
}
//
// list LITTLEFS files
//
void LITTLEFS_ListDir(fs::FS &fs, const char * dirname, uint8_t levels)
{
  File root = fs.open(dirname);
  if(!root)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("- failed to open directory");
    #endif
    return;
  }
  if(!root.isDirectory())
  {
    #ifdef DEBUG_VERBOSE
    Serial.println(" - not a directory");
    #endif
    return;
  }
  File file = root.openNextFile();
  while(file)
  {
    if(file.isDirectory())
    {
      #ifdef DEBUG_VERBOSE
      Serial.print("  DIR : ");
      Serial.println(file.name());
      #endif
      sprintf(tmpStr, "/%s/",file.name());
      if(levels)
      {
        LITTLEFS_ListDir(fs, tmpStr, levels -1);
      }
    }
    #ifdef DEBUG_VERBOSE
    else 
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    #endif
    file = root.openNextFile();
  }
}
//
// delete named file from LITTLEFS
//
void LITTLEFS_DeleteFile(fs::FS &fs, const char * path)
{
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.printf("Deleting file: %s ", path);
  #endif
  if(fs.remove(path))
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("- file deleted");
    #endif
  }
  else 
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("- delete failed");
    #endif
  }
}
void LITTLEFS_AppendFile(fs::FS &fs, const char * path, const char * message)
{
  File file = fs.open(path, FILE_APPEND);
  if(!file)
  {
    Serial.println("Failed to open file for appending");
    return;
  }
  if(!file.print(message))
  {
    Serial.println("Append failed");
  }
  file.close();
}
// ================================ end LITTLEFS functions ================================
// ================================ begin WiFi initialize/credentials functions ================================
//
// Initialize WiFi
//
bool WiFi_Init() 
{
  // cant connect if there's no WiFi SSID defined
  if(ssid=="")
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("Undefined SSID");
    #endif    
    wifiConnected = false;
    return false;
  }

  WiFi.setHostname(wifiClientName.c_str());
  WiFi.mode(WIFI_STA);

  #ifdef DEBUG_EXTRA_VERBOSE  
    Serial.println("Connect to wifi with DNS assigned IP");
  #endif
  // set up and connect to wifi
  WiFi.config(INADDR_NONE,INADDR_NONE,INADDR_NONE,INADDR_NONE);
  WiFi.begin(ssid.c_str(), pass.c_str());
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.printf("Connecting to WiFi SSID: %s PWD: %s...", ssid.c_str(), pass.c_str());
  #endif
  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  int retryCount = 0;
  previousMillis = millis();
  wifiConnected = true;
  while((WiFi.status() != WL_CONNECTED) && (retryCount < 10))
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= WIFI_WAIT) 
    {
      #ifdef DEBUG_EXTRA_VERBOSE
        Serial.printf("Failed to connect on try %d of 10.", retryCount+1);
      #endif
      wifiConnected = false;
      retryCount++;
      previousMillis = currentMillis;
    }
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if(!wifiConnected)
  {
      #ifdef DEBUG_EXTRA_VERBOSE
        Serial.printf("Failed to connect after 10 attempts - reset credentials");
      #endif
      ClearCredentials();
      oledString[0] = "No WiFi";
      oledString[1] = "---";
      oledString[2] = "---";
      oledString[3] = "---";
      RefreshOLED(1);
  }
  ip = WiFi.localIP().toString().c_str();
  sprintf(wifiState, "%s (%s) Connected ",WiFi.localIP().toString().c_str(), WiFi.getHostname());
  oledString[0] = "WiFi OK";
  oledString[1] = "No MQTT";
  oledString[2] = "---";
  oledString[3] = "---";
  RefreshOLED(1);
 
  #ifdef DEBUG_VERBOSE
    Serial.println(wifiState);
  #endif
  return wifiConnected;
}
//
// load wifi credentials from LITTLEFS
//
void LoadCredentials()
{
  String tmp;
  ssid = LITTLEFS_ReadFile(LittleFS, ssidPath);
  ssid.trim();
  pass = LITTLEFS_ReadFile(LittleFS, passPath);
  pass.trim();
  wifiClientName = LITTLEFS_ReadFile(LittleFS, clientPath);
  wifiClientName.trim();
  tz = LITTLEFS_ReadFile (LittleFS, tzPath);
  tz.trim();
  dst = LITTLEFS_ReadFile (LittleFS, dstPath);
  dst.trim();
  
  gmtOffset_sec = atoi(tz.c_str()) * 3600; // convert hours to seconds
  daylightOffset_sec = atoi(dst.c_str()) * 3600; // convert hours to seconds

  mqtt_server = LITTLEFS_ReadFile(LittleFS, mqtt_serverPath);
  mqtt_server.trim();
  mqttserverIP.fromString(mqtt_server);
  mqtt_port = LITTLEFS_ReadFile(LittleFS, mqtt_portPath);
  mqtt_port.trim();
  mqtt_portVal = atoi(mqtt_port.c_str());
  mqtt_user = LITTLEFS_ReadFile(LittleFS, mqtt_userPath);
  mqtt_user.trim();
  mqtt_user = "openhabian";
  mqtt_password = LITTLEFS_ReadFile(LittleFS, mqtt_passwordPath);
  mqtt_password.trim();
  mqtt_Report = true;
  LITTLEFS_ReadFile(LittleFS, "/wifimanager.html");
  //#ifdef DEBUG_VERBOSE
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("PASSWORD: ");
  Serial.println(pass);
  Serial.print("Timezone offset: ");
  Serial.print(tz);
  Serial.print(" ");
  Serial.println(gmtOffset_sec);
  Serial.print("DST offset: ");
  Serial.print(dst);
  Serial.print(" ");
  Serial.println(daylightOffset_sec);

  Serial.println("MQTT credentials");
  Serial.print("Server: ");
  Serial.println(mqtt_server);
  Serial.print("Port: ");
  Serial.println(mqtt_port);
  Serial.print("User: ");
  Serial.println(mqtt_user);
  Serial.print("Password: ");
  Serial.println(mqtt_password);

  Serial.println("Update credentials HTML page");
  CreateHTML();
}
//
// load LED pattern files
//
void LoadPatterns(fs::FS &fs)
{
  char buf[512]; 
  int ledIdx = 0;
  int colorIdx = 0;
  int lineIdx = 0;
  // count the files in /patterns
  patternCount = 0;
  File root = fs.open("/patterns/");
  if(!root)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("failed to open /patterns folder");
    #endif
    return;
  }
  File file = root.openNextFile();
  while(file)
  {
    if(!file.isDirectory())
    {
      patternCount++;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  if(patternCount == 0)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("no pattern files found in /patterns");
    #endif
    return;
  }
  // create the pattern structure array
  patterns = (LED_Patterns*)calloc(patternCount, sizeof(LED_Patterns));

  root = fs.open("/patterns/");
  if(!root)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("failed to re-open /patterns folder");
    #endif
    return;
  }
  file = root.openNextFile();
  String pattern;
  for(int patternIdx = 0; patternIdx < patternCount; patternIdx++)
  {
    if(file.isDirectory())
    {
      continue;
    }
    patterns[patternIdx].patternID = patternIdx;

    sprintf(tmpStr, "/patterns/%s", file.name());
    pattern = LITTLEFS_ReadFile(LittleFS, tmpStr);
    // name
    ReadLine(file, buf);
    strcpy(patterns[patternIdx].patternName, buf);
    // LED count
    ReadLine(file, buf);
    patterns[patternIdx].ledCount =  atoi(buf);
    // frame count
    ReadLine(file, buf);
    patterns[patternIdx].frameCount = atoi(buf);
    // create the frame array
    patterns[patternIdx].frames = (LED_Frame*)calloc(patterns[patternIdx].frameCount, sizeof(LED_Frame));
    // frames
    for(int frameIdx = 0; frameIdx < patterns[patternIdx].frameCount; frameIdx++)
    {
      // values array
      patterns[patternIdx].frames[frameIdx].ledValues = (LED_Values*)calloc(patterns[patternIdx].ledCount, sizeof(LED_Values));
      // frame delay
      ReadLine(file, buf);
      patterns[patternIdx].frames[frameIdx].frameDelay = (ulong)atoi(buf);
      // led color and brightness values
      for(int ledIdx = 0; ledIdx < patterns[patternIdx].ledCount; ledIdx++)
      {
        ReadLine(file, buf);
        char* token = strtok(buf, ",");
        colorIdx = 0;
        while(token != NULL)
        {
          patterns[patternIdx].frames[frameIdx].ledValues[ledIdx].colorValues[colorIdx] = (uint8_t)atoi(token);
          token = strtok(NULL, ",");
          colorIdx++;
        }
      }
    }
    #ifdef DEBUG_VERBOSE
    Serial.println("=============================pattern=============================");
    Serial.print("pattern ID ");
    Serial.println(patterns[patternIdx].patternID);
    Serial.print("pattern name ");
    Serial.println(patterns[patternIdx].patternName);
    Serial.print("pattern LEDs ");
    Serial.println(patterns[patternIdx].ledCount);
    Serial.print("pattern frames ");
    Serial.println(patterns[patternIdx].frameCount);
    for(int frameIdx = 0; frameIdx < patterns[patternIdx].frameCount; frameIdx++)
    {
      Serial.print("  frame ");
      Serial.println(frameIdx);
      for(int pixelIdx = 0; pixelIdx < patterns[patternIdx].ledCount; pixelIdx++)
      {
        Serial.print("    pixel ");
        Serial.print(pixelIdx);
        Serial.print(": ");
        for(int colorIdx = 0; colorIdx < 4; colorIdx++)
        {
          Serial.print(patterns[patternIdx].frames[frameIdx].ledValues[pixelIdx].colorValues[colorIdx]);
          Serial.print(" ");
        }
        Serial.println();
      }
    }
    Serial.println("=================================================================");
    #endif
    file = root.openNextFile();
  }
}
//
//
//
void PlayPattern(int patternID)
{
  oledString[2] = patterns[patternID].patternName;  
  RefreshOLED(1);
  #ifdef DEBUG_VERBOSE
  Serial.print("Pattern ");
  Serial.println(patterns[patternID].patternName);
  #endif
  for(int frameIdx = 0; frameIdx < patterns[patternID].frameCount; frameIdx++)
  {
    #ifdef DEBUG_VERBOSE
    Serial.print("Frame ");
    Serial.println(frameIdx);
    Serial.print("Delay ");
    Serial.println(patterns[patternID].frames[frameIdx].frameDelay);
    #endif
    for(uint16_t ledIdx = 0; ledIdx < patterns[patternID].ledCount; ledIdx++)
    {
      #ifdef DEBUG_VERBOSE
      Serial.print("\tPixel\t");
      Serial.print(ledIdx);
      Serial.print("\t");
      for(int colorIdx = 0; colorIdx < 4; colorIdx++)
      {
        Serial.print(patterns[patternID].frames[frameIdx].ledValues[ledIdx].colorValues[colorIdx]);
        Serial.print("\t");
      }
      Serial.println();
      #endif
      strip.setPixelColor(ledIdx, (uint8_t)((float)patterns[patternID].frames[frameIdx].ledValues[ledIdx].colorValues[0] * ((float)patterns[patternID].frames[frameIdx].ledValues[ledIdx].colorValues[3]/255.0)),
                                  (uint8_t)((float)patterns[patternID].frames[frameIdx].ledValues[ledIdx].colorValues[1] * ((float)patterns[patternID].frames[frameIdx].ledValues[ledIdx].colorValues[3]/255.0)),
                                  (uint8_t)((float)patterns[patternID].frames[frameIdx].ledValues[ledIdx].colorValues[2] * ((float)patterns[patternID].frames[frameIdx].ledValues[ledIdx].colorValues[3]/255.0)));
    }
    strip.show();
    delay(patterns[patternID].frames[frameIdx].frameDelay);
  }
}
//
//
//
void ReadLine(File file, char* buf)
{  
  char c;
  int bufIdx = 0;
  buf[0] = '\0';
  do
  {
    if(!file.available())
    {
      return;
    }
    c = file.read();
    if(c < 0x20)
    {
      if(c != 0x0A)
      {
        continue;
      }
      break;
    }
    buf[bufIdx] = c;
    bufIdx++;
    buf[bufIdx] = '\0';
  } while (c != 0x10);
  return;
}
//
// get new credentials from user from web page in access point mode
//
void GetCredentials()
{
  disableCore0WDT();
  disableCore1WDT();
  // Connect to Wi-Fi network with SSID and password
  #ifdef DEBUG_VERBOSE
    Serial.print("Setting AP (Access Point) ");
  #endif
  // NULL sets an open Access Point
  WiFi.softAP("STAIRS-SETUP", NULL);

  IPAddress IP = WiFi.softAPIP();
  #ifdef DEBUG_VERBOSE
    Serial.print(" address: ");
    Serial.println(IP); 
  #endif
  oledString[0] = "Access Point";
  oledString[1] = "   ";
  oledString[2] = IP.toString();
  oledString[3] = "---";
  RefreshOLED(1);  

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("Request from webserver, send page");
    #endif
    request->send(LittleFS, "/wifimanager.html", "text/html");
  });
    
  server.serveStatic("/", LittleFS, "/");
  //
  // display web page and get credentials from user
  //  
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) 
  {
    int params = request->params();
    for(int i=0;i<params;i++)
    {
      yield();
      const AsyncWebParameter* p = request->getParam(i);
      if(p->isPost())
      {
        // HTTP POST ssid value
        if (p->name() == PARAM_INPUT_1) 
        {
          ssid = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_1, ssid);
          #endif
        }
        // HTTP POST password value
        if (p->name() == PARAM_INPUT_2) 
        {
          pass = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_2, pass);
          #endif
        }
        // HTTP POST local ip value
        if (p->name() == PARAM_INPUT_3) 
        {
          wifiClientName = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_3, ip);
          #endif
        }
        // HTTP POST time zone offset value
        if (p->name() == PARAM_INPUT_4) 
        {
          tz = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_4, tz);
          #endif
        }
        // HTTP POST dst offset value
        if (p->name() == PARAM_INPUT_5) 
        {
          dst = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_5, dst);
          #endif
        }
        // HTTP POST mqtt server ip value
        if (p->name() == PARAM_INPUT_6) 
        {
          mqtt_server = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_6, mqtt_server);
          #endif
        }
        // HTTP POST mqtt port value
        if (p->name() == PARAM_INPUT_7) 
        {
          mqtt_port = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_7, mqtt_port);
          #endif
        }
        // HTTP POST mqtt username value
        if (p->name() == PARAM_INPUT_8) 
        {
          mqtt_user = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_8, mqtt_user);
          #endif
        }
        // HTTP POST mqtt password value
        if (p->name() == PARAM_INPUT_9) 
        {
          mqtt_password = p->value().c_str();
          #ifdef DEBUG_EXTRA_VERBOSE
            Serial.printf("%s %s\n", PARAM_INPUT_9, mqtt_password);
          #endif
        }
      }
    } 
    request->send(200, "text/plain", "Credentials saved. ESP will restart.");
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("Store credentials in LITTLEFS and reboot");
    #endif
    SaveCredentials();
  });
  server.begin();
}
//
// save credentials to files
//
void SaveCredentials()
{
  LITTLEFS_WriteFile(LittleFS, ssidPath, ssid.c_str());
  LITTLEFS_WriteFile(LittleFS, passPath, pass.c_str());
  LITTLEFS_WriteFile(LittleFS, clientPath, wifiClientName.c_str());
  LITTLEFS_WriteFile(LittleFS, tzPath, tz.c_str());
  LITTLEFS_WriteFile(LittleFS, dstPath, dst.c_str());
  LITTLEFS_WriteFile(LittleFS, mqtt_serverPath, mqtt_server.c_str());
  LITTLEFS_WriteFile(LittleFS, mqtt_portPath, mqtt_port.c_str());
  LITTLEFS_WriteFile(LittleFS, mqtt_userPath, mqtt_user.c_str());
  LITTLEFS_WriteFile(LittleFS, mqtt_passwordPath, mqtt_password.c_str());
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print("SSID set to: ");
    Serial.println(ssid);
    Serial.print("Password set to: ");
    Serial.println(pass);
    Serial.print("IP Address: ");
    Serial.println(ip);
    Serial.print("Time zone offset set to: ");
    Serial.println(tz);
    Serial.print("DST offset set to: ");
    Serial.println(dst);
    Serial.print("MQTT server IP: ");
    Serial.println(mqtt_server);
    Serial.print("MQTT port: ");
    Serial.println(mqtt_port);
    Serial.print("MQTT username: ");
    Serial.println(mqtt_user);
    Serial.print("MQTT password: ");
    Serial.println(mqtt_password);
  #endif
  ESP.restart();
}
//
// clear credentials and restart
// allows user to change wifi SSIDs easily
//
void ClearCredentials()
{
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Clear WiFi credentials");
  #endif
  LITTLEFS_DeleteFile(LittleFS, ssidPath);
  LITTLEFS_DeleteFile(LittleFS, passPath);
  //LITTLEFS_DeleteFile(LittleFS, ipPath);
  //LITTLEFS_DeleteFile(LittleFS, gatewayPath);
  LITTLEFS_DeleteFile(LittleFS, tzPath);
  LITTLEFS_DeleteFile(LittleFS, dstPath);
  // MQTT
  LITTLEFS_DeleteFile(LittleFS, mqtt_serverPath);
  LITTLEFS_DeleteFile(LittleFS, mqtt_portPath);
  LITTLEFS_DeleteFile(LittleFS, mqtt_userPath);
  LITTLEFS_DeleteFile(LittleFS, mqtt_passwordPath);
  
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Restart...");
  #endif
  startDelay = millis();
  while((millis() - startDelay) < WIFI_WAIT)
  {}
  ESP.restart();
}
//
// update credentials page
//
void CreateHTML()
{
  Serial.println("Create /wifimanager.html");
  LITTLEFS_WriteFile(LittleFS, "/wifimanager.html", "<!DOCTYPE html>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "<html>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "<head>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "  <title>Stair Lighting Credentials</title>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "  <link rel=\"icon\" href=\"data:,\">\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "  <link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "</head>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "<body>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "  <div class=\"topnav\">\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "    <h1>Stair Lighting Credentials</h1>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "  </div>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "  <div class=\"content\">\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "    <div class=\"card-grid\">\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "      <div class=\"card\">\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "        <form action=\"/\" method=\"POST\">\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "          <p>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "             <div>WiFi Credentials</div>\n");

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <label for=\"ssid\">SSID</label>\n");
  sprintf(tmpStr, "            <input type=\"text\" id =\"ssid\" name=\"ssid\" value = \"%s\"><br>\n", ssid);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", tmpStr);

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <label for=\"pass\">Password</label>\n");
  sprintf(tmpStr, "            <input type=\"text\" id =\"pass\" name=\"pass\" value = \"%s\"><br>\n", pass);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", tmpStr);

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <label for=\"ssid\">Client Name</label>");
  sprintf(tmpStr, "            <input type=\"text\" id =\"clientname\" name=\"clientname\" value = \"%s\"><br>\n", wifiClientName);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", tmpStr);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <hr/>\n");

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <div>Time zone and DST offsets (hours)</div>\n");

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <label for=\"tz\">Time Zone offset from GST</label>\n");
  sprintf(tmpStr, "            <input type=\"text\" id =\"timezone\" name=\"timezone\" value = \"%s\"><br>\n", tz);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", tmpStr);
  
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <label for=\"dst\">DST offset</label>\n");
  sprintf(tmpStr, "            <input type=\"text\" id =\"dst\" name=\"dst\" value = \"%s\"><br>\n", dst);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", tmpStr);

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <hr/>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <div>MQTT setup</div>\n");

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <label for=\"mqtt_serverIP\">MQTT server IP</label>\n");
  sprintf(tmpStr, "            <input type=\"text\" id =\"mqtt_serverIP\" name=\"mqtt_serverIP\" value = \"%s\"><br>\n", mqtt_server);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", tmpStr);

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <label for=\"mqtt_port\">MQTT port</label>\n");
  sprintf(tmpStr, "            <input type=\"text\" id =\"mqtt_port\" name=\"mqtt_port\" value=\"%s\"><br>\n", mqtt_port);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", tmpStr);

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <label for=\"mqtt_user\">MQTT user name</label>\n");
  sprintf(tmpStr, "            <input type=\"text\" id =\"mqtt_user\" name=\"mqtt_user\" value = \"%s\"><br>\n", mqtt_user);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", tmpStr);

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <label for=\"mqtt_password\">MQTT password</label>\n");
  sprintf(tmpStr, "            <input type=\"text\" id =\"mqtt_password\" name=\"mqtt_password\" value = \"%s\"><br>\n", mqtt_password);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", tmpStr);
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <hr/>\n");

  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "            <input type =\"submit\" value =\"Submit\">\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "          </p>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "        </form>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "      </div>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "    </div>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "  </div>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "</body>\n");
  LITTLEFS_AppendFile(LittleFS, "/wifimanager.html", "</html>\n");
}
// ================================ end WiFi initialize/credentials functions ================================
// ================================ begin NTP/RTC time functions ================================
//
// get local time (initially set via NTP server)
//
void UpdateLocalTime()
{
  rtcTimeSet = false;
  if((!wifiConnected) && (!rtcTimeSet))
  {
    sprintf(localTimeStr, "Time not set, wifi not connected");
    return;
  }
  // if not set from NTP, get time and set RTC
  if(!rtcTimeSet)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print("Time from NTP server ");
    #endif
    struct tm timeinfo;
    //configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    configTime(gmtOffset_sec, daylightOffset_sec, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
    // Init timeserver
    unsigned long lastTimeCheck = millis();    
    while(!getLocalTime(&timeinfo))
    {
      #ifdef DEBUG_EXTRA_VERBOSE
        Serial.println("failed to get time from NTP");
      #endif
      while(millis() - lastTimeCheck < RECONNECT_DELAY)
      {}
      lastTimeCheck = millis();
    }
    //GET DATE
    strftime(dayMonth, sizeof(dayMonth), "%d", &timeinfo);
    strftime(monthName, sizeof(monthName), "%b", &timeinfo);
    strftime(year, sizeof(year), "%Y", &timeinfo);
    dayNum = atoi(dayMonth);
    monthNum = timeinfo.tm_mon+1;
    yearNum = atoi(year);

    //GET TIME
    strftime(hour, sizeof(hour), "%H", &timeinfo);
    strftime(minute, sizeof(minute), "%M", &timeinfo);
    strftime(second, sizeof(second), "%S", &timeinfo);
    hourNum = atoi(hour);
    minNum = atoi(minute);
    secondNum = atoi(second);

    //rtc.setTime(secondNum, minNum, hourNum, dayNum, monthNum, yearNum);
    rtc.setTimeStruct(timeinfo);
    rtcTimeSet = true;

    lastMinNum = minNum;
    lastHourNum = hourNum;
    lastDayNum = dayNum;
  }
  // use RTC for time
  else
  {
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print("get time from local RTC...");
    #endif
    dayNum = rtc.getDay();
    monthNum = rtc.getMonth() + 1;
    yearNum = rtc.getYear();
    hourNum = rtc.getHour();
    minNum = rtc.getMinute();
    secondNum = rtc.getSecond();
    #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print("...time retrieved from local RTC ");
    #endif
  }
  // set last time values to current
  if(lastMinNum == -1)
  {
    lastMinNum = minNum;
    lastHourNum = hourNum;
    lastDayNum = dayNum;
  }
  sprintf(localTimeStr, "%02d/%02d/%04d %02d:%02d:%02d", monthNum, dayNum, yearNum, hourNum, minNum, secondNum);
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print("localTimeStr = ");
    Serial.println(localTimeStr);
  #endif
  if(!connectDateTimeSet)
  {
    strcpy(connectTimeStr, localTimeStr);
    connectDateTimeSet = true;
  }
}
// ================================ end NTP/RTC time functions ================================