// debug print control
//#define DEBUG_VERBOSE                // basic output for debugging
//#define DEBUG_EXTRA_VERBOSE          // extra output for debugging

// patterns saved to LITTLEFS in /patterns folder
// name
// led count
// frames
// frame delay
// r,g,b,bright for each led
// repeat for each frame
struct LED_Values
{
  uint8_t colorValues[4];
};
struct LED_Frame
{
  ulong frameDelay;
  LED_Values* ledValues;
};
struct LED_Patterns
{
  int patternID;
  char patternName[64];
  int ledCount;
  int frameCount;
  LED_Frame* frames;
};
LED_Patterns* patterns;
int patternCount = 0;

// FUNCTION PROTOTYPES
// required
void setup();
void loop();
// 
// hardcoded LED patterns
//
void CylonEyes(uint8_t r, uint8_t g, uint8_t b, uint8_t bright, int repeatCount);
void StripColor(uint8_t r, uint8_t g, uint8_t b, uint8_t bright);
void RainbowCycle(uint8_t wait);
uint32_t Wheel(byte WheelPos);
//
// OLED functions
//
void RefreshOLED(int fontSize);
//
// MQTT functions
//
void MQTT_Report();
bool MQTT_Reconnect();
void MQTT_Callback(char* topic, byte* payload, unsigned int length);
void MQTT_SubscribeTopics();
bool MQTT_PublishTopics();
bool MQTT_PublishTopic(int topicID);
//
// LITTLEFS functions
//
void LITTLEFS_Init();
String LITTLEFS_ReadFile(fs::FS &fs, const char * path);
void LITTLEFS_WriteFile(fs::FS &fs, const char * path, const char * message);
void LITTLEFS_ListDir(fs::FS &fs, const char * dirname, uint8_t levels);
void LITTLEFS_DeleteFile(fs::FS &fs, const char * path);
void LITTLEFS_AppendFile(fs::FS &fs, const char * path, const char * message);
//
// wifi functions
//
bool WiFi_Init();
//
// data and info files
//
void LoadCredentials();
void LoadPatterns(fs::FS &fs);
void GetCredentials();
void SaveCredentials();
void ClearCredentials();
void CreateHTML();
//
// time functions
//
void UpdateLocalTime();
//
// globals and defines
//
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
//
// global variables for ESP32 credentials and WiFi
// using #define instead of const saves some program space
#define WIFI_WAIT 10000    // interval between attempts to connect to wifi
// Search for parameter in HTTP POST request
#define PARAM_INPUT_1  "ssid"
#define PARAM_INPUT_2  "pass"
#define PARAM_INPUT_3  "clientname"
#define PARAM_INPUT_4  "timezone"
#define PARAM_INPUT_5  "dst"
//Variables to save values from HTML form
String ssid;
String pass;
String ip;
String wifiClientName;
String tz;
String dst;
// File paths to save input values permanently
#define ssidPath     "/ssid.txt"
#define passPath     "/pass.txt"
#define clientPath   "/client.txt"       // wifi client node name
#define tzPath       "/tz.txt"
#define dstPath      "/dst.txt"
// ESP32 IP address (use DNS if blank)
IPAddress localIP;
// local Gateway IP address and subnet mask 
IPAddress localGateway;
IPAddress subnet(255, 255, 0, 0);
// wifi state
char wifiState[256];
bool wifiConnected = false;
//
// global variables for time
//
// NTP Server Details
#define ntpServer "pool.ntp.org"
long  gmtOffset_sec = 0;
int   daylightOffset_sec = 0;
ESP32Time rtc(0);
int dayNum;
int monthNum;
int yearNum;
int hourNum;
int minNum;
int secondNum;

int lastDayNum = -1;
int lastHourNum = -1;
int lastMinNum = -1;

char weekDay[10];
char dayMonth[4];
char monthName[5];
char year[6];
char hour[4];
char minute[4];
char second[4];
char localTimeStr[256];
char connectTimeStr[256];
bool connectDateTimeSet = false;
bool rtcTimeSet = false;
// Timer variables
unsigned long previousMillis = 0;
unsigned long timeCheckInterval = 30000;
unsigned long lastTimeCheck = 0;
//
// MQTT info
//
#define PARAM_INPUT_6   "mqtt_serverIP"
#define PARAM_INPUT_7   "mqtt_port"
#define PARAM_INPUT_8   "mqtt_user"
#define PARAM_INPUT_9  "mqtt_password"

#define mqtt_serverPath    "/mqttIP.txt"
#define mqtt_portPath      "/mqttPrt.txt"
#define mqtt_userPath      "/mqttUse.txt"
#define mqtt_passwordPath  "/mqttPas.txt"

void MQTT_Callback(char* topic, byte* payload, unsigned int length);
String mqtt_server = "0.0.0.0";
String mqtt_port  = "0";
String mqtt_user;
String mqtt_password;
IPAddress mqttserverIP(0, 0, 0, 0);
int  mqtt_portVal = 0;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
String mqttClientID = "MQTTStairLights_";   // local name, must be unique on MQTT server so append a random string at the end
// subscribed (listening) topic(s)
// list of text strings for subscribed topics from server, if any
#define MAX_SUBSCRIBE 5
// use MAX_SUBSCRIBE if > 0
String subscribed_topic[MAX_SUBSCRIBE] =  { "Stair_Lights/RedVal",
                                            "Stair_Lights/GreenVal",
                                            "Stair_Lights/BlueVal",
                                            "Stair_Lights/Brightness",
                                            "Stair_Lights/Pattern" };
// published (send to server) topic(s)
#define MAX_PUBLISH 2
// use MAX_PUBLISH if > 0
String published_topic[MAX_PUBLISH] = { "Stair_Lights/AddPatternName",
                              "Stair_Lights/RequestSettings" };
String published_payload[MAX_PUBLISH];
char mqttState[256];
bool mqtt_Report = true;
int mqttAttemptCount = 0;
uint mqttAttemptedReports = 0;
uint mqttSuccessfulReports = 0;
unsigned long lastUpdate;
// device specific
// neopixel settings
#define NUMPIXELS    14
#define NEO_PIN      12  // NeoPixel control pin
#define MAX_BRIGHT  255 // max brightness of 0-255 range

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ400);

uint8_t redVal;
uint8_t greenVal;
uint8_t blueVal;
uint8_t brightVal;
uint8_t patternVal;

//
//
//
#define INTERVAL_MS 100  //  5  second interval to call mqttClient
unsigned long startDelay = 0;
//
// 128x64 OLED display with Adafruit library
//
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 oledDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

char tmpStr[256];
String oledString[4] = {"Line 1", "Line 2", "Line 3", "Line 4"};
