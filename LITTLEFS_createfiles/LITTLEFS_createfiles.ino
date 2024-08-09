//
// create wifi config page and default wifi and mqtt login files for stair lighting controller
//
//
// To get time/date stamps by file.getLastWrite(), you need an 
// esp32 core on IDF 3.3 and comment a line in file esp_littlefs.c:
//
//#define CONFIG_LITTLEFS_FOR_IDF_3_2
//
// You only need to format LITTLEFS the first time you run a
// test or else use the LITTLEFS plugin to create a partition
// https://github.com/lorol/arduino-esp32littlefs-plugin 
#include <Arduino.h>
#include "FS.h"
#include <LittleFS.h>
#include <time.h> 
#include <WiFi.h>

#define FORMAT_LITTLEFS_IF_FAILED true
#define CREATE_FILES
#define ssidPath          "/ssid.txt"         // wifi server ip
#define passPath          "/pass.txt"         // wifi password
#define clientPath        "/client.txt"       // wifi client node name
#define tzPath            "/tz.txt"           // timezone offset
#define dstPath           "/dst.txt"          // daylight savings offset
#define mqtt_serverPath   "/mqttIP.txt"       // MQTT server IP
#define mqtt_portPath     "/mqttPrt.txt"      // MQTT server port
#define mqtt_userPath     "/mqttUse.txt"      // MQTT server username
#define mqtt_passwordPath "/mqttPas.txt"      // MQTT password
#define stylePath         "/style.css"        // style sheet
#define wifiPagePath      "/wifiManager.html" // web page

void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
void createDir(fs::FS &fs, const char * path);
void removeDir(fs::FS &fs, const char * path);
void readFile(fs::FS &fs, const char * path);
void writeFile(fs::FS &fs, const char * path, const char * message);
void appendFile(fs::FS &fs, const char * path, const char * message);
void renameFile(fs::FS &fs, const char * path1, const char * path2);
void deleteFile(fs::FS &fs, const char * path);
void CreateHTML();
void CreateCSS();
void CreateIP();
void CreatePassword();
void CreateTZ();
void CreateDSTOff();
void CreateMQTT_IP();
void CreateMQTT_Port();
void CreateMQTT_User();
void CreateMQTT_Pass();
const char* ssid     = "FamilyRoom";
const char* password = "ZoeyDora48375";

long timezone = 1; 
byte daysavetime = 1;
//
//
//
void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println();
  //
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) 
  {
      delay(500);
      Serial.print(".");
  }
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  //
  Serial.println("Contacting Time Server");
  configTime(3600*timezone, daysavetime*3600, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
  struct tm tmstruct ;
  delay(2000);
  tmstruct.tm_year = 0;
  getLocalTime(&tmstruct, 5000);
  Serial.printf("\r\nNow is : %d-%02d-%02d %02d:%02d:%02d\r\n",(tmstruct.tm_year)+1900,( tmstruct.tm_mon)+1, tmstruct.tm_mday,tmstruct.tm_hour , tmstruct.tm_min, tmstruct.tm_sec);
  Serial.println("");
  //
  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED))
  {
    Serial.println("LITTLEFS Mount Failed");
    return;
  }
  //
  Serial.println("Original files");
  listDir(LittleFS, "/", 1);

  Serial.println("Remove existing files");
  deleteFile(LittleFS, stylePath);        //"/style.css" style sheet
  deleteFile(LittleFS, wifiPagePath);  //"/wifiManager.html" web page
  deleteFile(LittleFS, ssidPath);        // wifi server ip
  deleteFile(LittleFS, passPath);        // wifi password
  deleteFile(LittleFS, clientPath);      // wifi client node name
  deleteFile(LittleFS, tzPath);       // timezone offset
  deleteFile(LittleFS, dstPath);      // daylight savings offset
  deleteFile(LittleFS, mqtt_serverPath);    // MQTT server IP
  deleteFile(LittleFS, mqtt_portPath);   // MQTT server port
  deleteFile(LittleFS, mqtt_userPath);   // MQTT server username
  deleteFile(LittleFS, mqtt_passwordPath);   // MQTT password
  
  listDir(LittleFS, "/", 1);

  Serial.println("----Create setup files----");
  CreateHTML();
  CreateCSS();
  #ifdef CREATE_FILES
  CreateIP();
  CreatePassword();
  CreateClientName();
  CreateTZ();
  CreateDSTOff();
  CreateMQTT_IP();
  CreateMQTT_Port();
  CreateMQTT_User();
  CreateMQTT_Pass();
  #endif
  
  Serial.println( "Setup complete" );
  Serial.println("---------------");
  listDir(LittleFS, "/", 1);
  readFile(LittleFS, ssidPath);
  readFile(LittleFS, passPath);
  readFile(LittleFS, clientPath);
  readFile(LittleFS, tzPath);
  readFile(LittleFS, dstPath);
  readFile(LittleFS, mqtt_serverPath);
  readFile(LittleFS, mqtt_portPath);
  readFile(LittleFS, mqtt_userPath);
  readFile(LittleFS, mqtt_passwordPath);
}

void CreateIP()
{
  Serial.print("Create ");
  Serial.println(ssidPath);
  writeFile(LittleFS, ssidPath, "FamilyRoom");
}
void CreatePassword()
{
  Serial.print("Create ");
  Serial.println(passPath);
  writeFile(LittleFS, passPath, "ZoeyDora48375");
}
void CreateClientName()
{
  Serial.print("Create ");
  Serial.println(clientPath);
  writeFile(LittleFS, clientPath, "STAIR-LIGHTS");
}
void CreateTZ()
{
  Serial.print("Create ");
  Serial.println(tzPath);
  writeFile(LittleFS, tzPath, "-5");
}
void CreateDSTOff()
{
  Serial.print("Create ");
  Serial.println(dstPath);
  writeFile(LittleFS, dstPath, "1");
}
void CreateMQTT_IP()
{
  Serial.print("Create ");
  Serial.println(mqtt_serverPath);
  writeFile(LittleFS, mqtt_serverPath, "192.168.1.140");
}
void CreateMQTT_Port()
{
  Serial.print("Create ");
  Serial.println(mqtt_portPath);
  writeFile(LittleFS, mqtt_portPath, "1883");
}
void CreateMQTT_User()
{
  Serial.print("Create ");
  Serial.println(mqtt_userPath);
  writeFile(LittleFS, mqtt_userPath, "openhabian");
}
void CreateMQTT_Pass()
{
  Serial.print("Create ");
  Serial.println(mqtt_passwordPath);
  writeFile(LittleFS, mqtt_passwordPath, "SJnu12HMo");
}
void CreateHTML()
{
  Serial.print("Create ");
  Serial.println(wifiPagePath);
  writeFile(LittleFS, "/wifimanager.html", "<!DOCTYPE html>\n");
  appendFile(LittleFS, "/wifimanager.html", "<html>\n");
  appendFile(LittleFS, "/wifimanager.html", "<head>\n");
  appendFile(LittleFS, "/wifimanager.html", "  <title>Stair Lighting Credentials</title>\n");
  appendFile(LittleFS, "/wifimanager.html", "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n");
  appendFile(LittleFS, "/wifimanager.html", "  <link rel=\"icon\" href=\"data:,\">\n");
  appendFile(LittleFS, "/wifimanager.html", "  <link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">\n");
  appendFile(LittleFS, "/wifimanager.html", "</head>\n");
  appendFile(LittleFS, "/wifimanager.html", "<body>\n");
  appendFile(LittleFS, "/wifimanager.html", "  <div class=\"topnav\">\n");
  appendFile(LittleFS, "/wifimanager.html", "    <h1>Stair Lighting Credentials</h1>\n");
  appendFile(LittleFS, "/wifimanager.html", "  </div>\n");
  appendFile(LittleFS, "/wifimanager.html", "  <div class=\"content\">\n");
  appendFile(LittleFS, "/wifimanager.html", "    <div class=\"card-grid\">\n");
  appendFile(LittleFS, "/wifimanager.html", "      <div class=\"card\">\n");
  appendFile(LittleFS, "/wifimanager.html", "        <form action=\"/\" method=\"POST\">\n");
  appendFile(LittleFS, "/wifimanager.html", "          <p>\n");
  appendFile(LittleFS, "/wifimanager.html", "             <div>WiFi Credentials</div>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <label for=\"ssid\">SSID</label>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <input type=\"text\" id =\"ssid\" name=\"ssid\"><br>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <label for=\"pass\">Password</label>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <input type=\"text\" id =\"pass\" name=\"pass\"><br>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <label for=\"ssid\">Client Name</label>");
  appendFile(LittleFS, "/wifimanager.html", "            <input type=\"text\" id =\"clientname\" name=\"clientname\"><br>");
  appendFile(LittleFS, "/wifimanager.html", "            <hr/>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <div>Time zone and DST offsets (hours)</div>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <label for=\"tz\">Time Zone offset from GST</label>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <input type=\"text\" id =\"timezone\" name=\"timezone\"><br>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <label for=\"dst\">DST offset</label>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <input type=\"text\" id =\"dst\" name=\"dst\"><br>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <hr/>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <div>MQTT setup</div>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <label for=\"mqtt_serverIP\">MQTT server IP</label>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <input type=\"text\" id =\"mqtt_serverIP\" name=\"mqtt_serverIP\"><br>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <label for=\"mqtt_port\">MQTT port</label>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <input type=\"text\" id =\"mqtt_port\" name=\"mqtt_port\" value=\"1883\"><br>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <label for=\"mqtt_user\">MQTT user name</label>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <input type=\"text\" id =\"mqtt_user\" name=\"mqtt_user\"><br>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <label for=\"mqtt_password\">MQTT password</label>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <input type=\"text\" id =\"mqtt_password\" name=\"mqtt_password\"><br>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <hr/>\n");
  appendFile(LittleFS, "/wifimanager.html", "            <input type =\"submit\" value =\"Submit\">\n");
  appendFile(LittleFS, "/wifimanager.html", "          </p>\n");
  appendFile(LittleFS, "/wifimanager.html", "        </form>\n");
  appendFile(LittleFS, "/wifimanager.html", "      </div>\n");
  appendFile(LittleFS, "/wifimanager.html", "    </div>\n");
  appendFile(LittleFS, "/wifimanager.html", "  </div>\n");
  appendFile(LittleFS, "/wifimanager.html", "</body>\n");
  appendFile(LittleFS, "/wifimanager.html", "</html>\n");
}
void CreateCSS()
{
  Serial.print("Create ");
  Serial.println(stylePath);
  writeFile(LittleFS, "/style.css", "html {\n");
  appendFile(LittleFS, "/style.css", "  font-family: Arial, Helvetica, sans-serif; \n");
  appendFile(LittleFS, "/style.css", "  display: inline-block; \n");
  appendFile(LittleFS, "/style.css", "  text-align: center;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", "h1 {\n");
  appendFile(LittleFS, "/style.css", "  font-size: 1.8rem; \n");
  appendFile(LittleFS, "/style.css", "  color: white;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", "p { \n");
  appendFile(LittleFS, "/style.css", "  font-size: 1.4rem;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", ".topnav { \n");
  appendFile(LittleFS, "/style.css", "  overflow: hidden; \n");
  appendFile(LittleFS, "/style.css", "  background-color: #0A1128;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", "body {  \n");
  appendFile(LittleFS, "/style.css", "  margin: 0;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", ".content { \n");
  appendFile(LittleFS, "/style.css", "  padding: 5%;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", ".card-grid { \n");
  appendFile(LittleFS, "/style.css", "  max-width: 800px; \n");
  appendFile(LittleFS, "/style.css", "  margin: 0 auto; \n");
  appendFile(LittleFS, "/style.css", "  display: grid; \n");
  appendFile(LittleFS, "/style.css", "  grid-gap: 2rem; \n");
  appendFile(LittleFS, "/style.css", "  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", ".card { \n");
  appendFile(LittleFS, "/style.css", "  background-color: white; \n");
  appendFile(LittleFS, "/style.css", "  box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", ".card-title { \n");
  appendFile(LittleFS, "/style.css", "  font-size: 1.2rem;\n");
  appendFile(LittleFS, "/style.css", "  font-weight: bold;\n");
  appendFile(LittleFS, "/style.css", "  color: #034078\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", "input[type=submit] {\n");
  appendFile(LittleFS, "/style.css", "  border: none;\n");
  appendFile(LittleFS, "/style.css", "  color: #FEFCFB;\n");
  appendFile(LittleFS, "/style.css", "  background-color: #034078;\n");
  appendFile(LittleFS, "/style.css", "  padding: 15px 15px;\n");
  appendFile(LittleFS, "/style.css", "  text-align: center;\n");
  appendFile(LittleFS, "/style.css", "  text-decoration: none;\n");
  appendFile(LittleFS, "/style.css", "  display: inline-block;\n");
  appendFile(LittleFS, "/style.css", "  font-size: 16px;\n");
  appendFile(LittleFS, "/style.css", "  width: 100px;\n");
  appendFile(LittleFS, "/style.css", "  margin-right: 10px;\n");
  appendFile(LittleFS, "/style.css", "  border-radius: 4px;\n");
  appendFile(LittleFS, "/style.css", "  transition-duration: 0.4s;\n");
  appendFile(LittleFS, "/style.css", "  }\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", "input[type=submit]:hover {\n");
  appendFile(LittleFS, "/style.css", "  background-color: #1282A2;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", "input[type=text], input[type=number], select {\n");
  appendFile(LittleFS, "/style.css", "  width: 50%;\n");
  appendFile(LittleFS, "/style.css", "  padding: 12px 20px;\n");
  appendFile(LittleFS, "/style.css", "  margin: 18px;\n");
  appendFile(LittleFS, "/style.css", "  display: inline-block;\n");
  appendFile(LittleFS, "/style.css", "  border: 1px solid #ccc;\n");
  appendFile(LittleFS, "/style.css", "  border-radius: 4px;\n");
  appendFile(LittleFS, "/style.css", "  box-sizing: border-box;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "\n");
  appendFile(LittleFS, "/style.css", "label {\n");
  appendFile(LittleFS, "/style.css", "  font-size: 1.2rem; \n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", ".value{\n");
  appendFile(LittleFS, "/style.css", "  font-size: 1.2rem;\n");
  appendFile(LittleFS, "/style.css", "  color: #1282A2;  \n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", ".state {\n");
  appendFile(LittleFS, "/style.css", "  font-size: 1.2rem;\n");
  appendFile(LittleFS, "/style.css", "  color: #1282A2;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", "button {\n");
  appendFile(LittleFS, "/style.css", "  border: none;\n");
  appendFile(LittleFS, "/style.css", "  color: #FEFCFB;\n");
  appendFile(LittleFS, "/style.css", "  padding: 15px 32px;\n");
  appendFile(LittleFS, "/style.css", "  text-align: center;\n");
  appendFile(LittleFS, "/style.css", "  font-size: 16px;\n");
  appendFile(LittleFS, "/style.css", "  width: 100px;\n");
  appendFile(LittleFS, "/style.css", "  border-radius: 4px;\n");
  appendFile(LittleFS, "/style.css", "  transition-duration: 0.4s;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", ".button-on {\n");
  appendFile(LittleFS, "/style.css", "  background-color: #034078;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", ".button-on:hover {\n");
  appendFile(LittleFS, "/style.css", "  background-color: #1282A2;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", ".button-off {\n");
  appendFile(LittleFS, "/style.css", "  background-color: #858585;\n");
  appendFile(LittleFS, "/style.css", "}\n");
  appendFile(LittleFS, "/style.css", ".button-off:hover {\n");
  appendFile(LittleFS, "/style.css", "  background-color: #252524;\n");
  appendFile(LittleFS, "/style.css", "} \n");
}
void loop()
{
}
void listDir(fs::FS &fs, const char * dirname, uint8_t levels)
{
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if(!root)
  {
    Serial.println("Failed to open directory");
    return;
  }
  if(!root.isDirectory())
  {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while(file)
  {
    if(file.isDirectory())
    {
      Serial.print("  DIR : ");
      Serial.print (file.name());
      time_t t= file.getLastWrite();
      struct tm * tmstruct = localtime(&t);
      Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\r\n",(tmstruct->tm_year)+1900,( tmstruct->tm_mon)+1, tmstruct->tm_mday,tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);
      if(levels)
      {
        listDir(fs, file.name(), levels -1);
      }
    }
    else 
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.print(file.size());
      time_t t= file.getLastWrite();
      struct tm * tmstruct = localtime(&t);
      Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\r\n",(tmstruct->tm_year)+1900,( tmstruct->tm_mon)+1, tmstruct->tm_mday,tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);
    }
    file = root.openNextFile();
  }
}
void createDir(fs::FS &fs, const char * path)
{
  Serial.printf("Creating Dir: %s", path);
  if(!fs.mkdir(path))
  {
    Serial.println(" - failed to create dir");
  }
  Serial.println();
}
void removeDir(fs::FS &fs, const char * path)
{
  Serial.printf("Removing Dir: %s\r\n", path);
  if(!fs.rmdir(path))
  {
    Serial.println("rmdir failed");
  }
}
void readFile(fs::FS &fs, const char * path)
{
  //Serial.printf("Read file: %s\r\n", path);
  //File file = fs.open(path);
  //if(!file)
  //{
  //  Serial.println("Failed to open file for reading");
  //  return;
  //}
  //
  //  while(file.available())
  //  {
  //    Serial.write(file.read());
  //  }
  //  file.close();
  //  Serial.println();
  while(true)
  {
    ReadLine(file, buf);
    if(strlen(buf) == 0)
    {
      break;
    }
    token = strtok(buf, ";");
    char outStr[128];
    sprintf(outStr, "%s %s", cars[selectedCar].carName, token);
    carsMenu[menuCnt].description = outStr;
    carsMenu[menuCnt].result = menuCnt;
    menuCnt++;
  }
  file.close();
}
void writeFile(fs::FS &fs, const char * path, const char * message)
{
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }
  if(!file.print(message))
  {
    Serial.println("Write failed");
  }
  file.close();
}
void appendFile(fs::FS &fs, const char * path, const char * message)
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
void renameFile(fs::FS &fs, const char * path1, const char * path2)
{
  Serial.printf("Renaming file %s to %s", path1, path2);
  if (!fs.rename(path1, path2)) 
  {
    Serial.print(" - rename failed");
  }
  Serial.println();
}
void deleteFile(fs::FS &fs, const char * path)
{
  Serial.printf("Deleting file: %s", path);
  if(!fs.remove(path))
  {
    Serial.print(" - delete failed");
  }
  Serial.println();
}