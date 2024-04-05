#include <stdlib.h>
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <ArduinoOTA.h>
#include <esp_system.h>

#include <PubSubClient.h>

#include "SPI.h"
#include <XPT2046_Touchscreen.h>

#include "TFT_eSPI.h"

#include "Free_Fonts.h" // Include the header file attached to this sketch

// ----------------------------
// Touch Screen pins
// ----------------------------

// The CYD touch uses some non default
// SPI pins

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// ----------------------------

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

//#define pdMS_TO_TICKS( xTimeInMs ) ( ( ( TickType_t ) ( xTimeInMs ) * configTICK_RATE_HZ ) / ( TickType_t ) 1000 )

#define CHIP "ESP32-CYD4"

const char* mqtt_broker = "192.168.100.232";
const int mqtt_port = 1883;

const char *clientId = CHIP;
const char* publish_topic = "tele/" CHIP "/status";
const char* subscribe_topic = "cmnd/" CHIP "/#";

WiFiClient espClient;
PubSubClient client(espClient);

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI();

const char* ssid = "TP-Link_CB58";
const char* password = "13157005";
const char* ntpServer = "pool.ntp.org";

const char compile_date[] = __DATE__ " " __TIME__;
const char filename[] = __FILE__;

time_t now;
struct tm *tmInfo;
struct tm *tmInfo2;
struct tm *tmInfo3;
struct tm info;
struct tm infoUTC;

//char UKTZ='TZ=GMT+00:00:BST+01:00:start=3/last,sunday/02:00:00,end=10/last,sunday/01:00:00'
//char UKTZ='TZ=GMT+00:00:BST+01:00:M3/last,sunday/02:00:00,end=10/last,sunday/01:00:00'
#define UKTZ "TZ=GMT0BST,M3.5.0/02:00:00,M10.5.0/02:00:00"
#define NYCTZ "TZ=EST5EDT4,M3.2.0/02:00:00,M10.5.0/02:00:00"
#define UTCTZ "TZ=UTC0"
#define CALTZ "TZ=PST8PDT,M3.2.0,M11.1.0"

enum TZSettings {
  UTC,
  NYC,
  GB
};

TZSettings tzpos1 = UTC;
TZSettings tzpos2 = NYC;

char metricData[100]; // Placeholder for your sensor data
char olddate[64];
char oldtemp[64];
//char tempbuffer[64];
volatile long current_temp = 44;

uint16_t txt_colour = TFT_YELLOW;

unsigned long lastPublishTime = 0;
//const unsigned long publishInterval = 300000; // milliseconds (5 minutes)
const unsigned long publishInterval = 60000; // milliseconds (5 minutes)

volatile int currentScreen = 1;

// Function prototypes (declared before setup)
void displayScreen1(void *pvParameters);
//void displayScreen2(void *pvParameters);
//void displayScreen3(void *pvParameters);
void reconnectMQTT();
void publishMetricData(void);
void receiveSubscribed(char* topic, byte* payload, unsigned int length);

//void receiveSubscribed();

/*
int ignore() {
  // Get local time for New York (replace with actual time zone string)
  struct tm* new_york_time = get_local_time("America/New_York");
  printf("New York Time: %s\n", asctime(new_york_time));

  // Get local time for California (replace with actual time zone string)
  struct tm* california_time = get_local_time("America/Los_Angeles");
  printf("California Time: %s\n", asctime(california_time));

  return 0;
}
*/

struct tm *get_local_time(const char* time_zone, struct tm *tm) {
  // Set time zone (choose one approach)
  // Option 1: Set TZ environment variable (system-wide)
  // setenv("TZ", time_zone, 1);

  // Option 2: Use tzset() (more controlled)
  //unsetenv("TZ");
  //setenv("TZ", time_zone, 1);
  putenv((char *)time_zone);
  //putenv("TZ",time_zone);
  tzset();

  time_t now = time(NULL);
  //static struct tm local_time;
  localtime_r(&now, tm);

  return tm;

}
void setupScreen1(void) {

  tft.begin();

  tft.setRotation(3);
  //tft.invertDisplay( true );
  tft.fillScreen(TFT_BLACK);

  
  tft.setFreeFont(TT1);
  tft.setCursor(0, 20);
  tft.setTextColor(TFT_GREEN);
  tft.print("Code: ");
  tft.println(filename);
  tft.println("");
  tft.println(compile_date);
  tft.setCursor(0,60);
  tft.setFreeFont(FSS9);
  tft.print("IP: ");
  tft.println(WiFi.localIP());

  tft.setTextColor(TFT_SKYBLUE);
  tft.setFreeFont(FSSB12);

  tft.setCursor(0,140);
  tft.print("UK ");
  tft.setCursor(0,195);
  tft.print("NYC");

}

void setup() {
  Serial.begin(115200);
  delay(10);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  //ts.begin(mySpi);
  ts.begin();
  ts.setRotation(3);

  ArduinoOTA.setHostname(CHIP);

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  //configTzTime("EST5EDT4,M3.2.0/02:00:00,M10.5.0/02:00:00",ntpServer);
  //configTzTime(NYCTZ,ntpServer);
  configTzTime(UTCTZ,ntpServer);
   
  Serial.println("Waiting for time to be set...");
  while (!time(nullptr)) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("Time set!");
  Serial.println(CHIP);
  Serial.println(publish_topic);
  Serial.println(subscribe_topic);

  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(receiveSubscribed);

  setupScreen1();
  xTaskCreate(displayScreen1, "Display Screen 1", 10240, NULL, 1, NULL);

}

void displayScreen1(void *pvParameters) {
  (void)pvParameters;

  char tbuffer[64];
  struct tm time1;
  struct tm time2;
  struct tm *tm1;
  struct tm *tm2;

  int xpos =  10;
  int ypos = 50;
  TFT_eSprite img=TFT_eSprite(&tft);
  TFT_eSprite dateSprite=TFT_eSprite(&tft);
  TFT_eSprite tempSprite=TFT_eSprite(&tft);
  TFT_eSprite tzSprite1=TFT_eSprite(&tft);
  TFT_eSprite tzSprite2=TFT_eSprite(&tft);

  int sprites_created = 0;

Serial.println("132");

  if (!sprites_created) {
     img.createSprite(260,120);
     img.fillSprite(TFT_BLACK);
     dateSprite.createSprite(160,40);
     dateSprite.fillSprite(TFT_BLACK);
     tempSprite.createSprite(60,40);
     tempSprite.fillSprite(TFT_BLACK);
     tzSprite1.createSprite(48,20);
     tzSprite2.createSprite(48,20);
     tzSprite1.fillSprite(TFT_BLACK);
     tzSprite2.fillSprite(TFT_BLACK);

     sprites_created = 1;
  }

  while(true) {
    
    tft.setCursor(0,140);
    tft.print("UK ");
    tft.setCursor(0,195);
    tft.print("NYC");
/*
    tzSprite1.setCursor(0,16);
    tzSprite2.setCursor(0,16);
    if ( tzpos1 == GB ){
      tzSprite1.print("UK ");
    }
    if ( tzpos2 == NYC ){
      tzSprite2.print("NYC");
    }

*/

     img.setTextColor(txt_colour);
     img.setCursor(xpos, ypos);  
     //tft.setFreeFont(FSSB24);       // Select Free Serif 24 point font
     img.setFreeFont(NEW48);       // Select Free Serif 24 point font  
     img.setTextDatum(TL_DATUM);
     img.fillSprite(TFT_BLACK);
     //img.drawRect(0,0,260,120,TFT_RED);
   
     //time(&now);
     //tmInfo = gmtime_r(&now, &info);
   
     tm1 = get_local_time(UKTZ, &time1);

     if (tm1 == NULL) {
       img.println("Failed to obtain time from NTP server");
       return;
     }
   
     strftime(tbuffer,63,"%H:%M:%S", &time1);

     
     //tft.print(asctime(&timeInfo));
     img.print(tbuffer);
     //strftime(tbuffer,63,"timezone is %Z",tmInfo);
     //Serial.println(tbuffer);
   
     //tmInfo = localtime_r(&now, &infoUTC);
     tm2 = get_local_time(NYCTZ,&time2);
     strftime(tbuffer,63,"%H:%M:%S",&time2);
     //tft.print(asctime(&timeInfo));
     img.setCursor(xpos, ypos+56);  
   
     img.print(tbuffer);

   //strftime(tbuffer,63,"timezone is %Z",tmInfo2);
     //Serial.println(tbuffer);

     img.pushSprite(60,100,TFT_TRANSPARENT);
     //tzSprite1.pushSprite(1,110);
     //tzSprite2.pushSprite(1,130);

  
     strftime(tbuffer,63,"%a %b %d",&time2);
//     tmInfo = get_local_time(UTCTZ,);
//    setenv("TZ", "UTC0", 1);
//    tzset();
     if ( strcmp(olddate,tbuffer)){  //If we have new date, update the sprite
        dateSprite.fillSprite(TFT_BLACK);
        dateSprite.setTextColor(TFT_SKYBLUE);
        dateSprite.setFreeFont(FSSB12);
        dateSprite.setCursor(2,30);
        dateSprite.print(tbuffer);
        dateSprite.pushSprite(100,68,TFT_TRANSPARENT);
        strcpy(olddate,tbuffer);
     }
   
     snprintf(tbuffer,63,"%ld",current_temp);
   
     if ( strcmp(oldtemp,tbuffer)){  //If we have new date, update the sprite
        tempSprite.fillSprite(TFT_BLACK);
        if ( current_temp < 32) {
           tempSprite.setTextColor(TFT_SKYBLUE);
        }
        else if ( current_temp >= 32 && current_temp < 80) {
           tempSprite.setTextColor(TFT_OLIVE);
        }
        else {
           tempSprite.setTextColor(TFT_ORANGE);
        }
        tempSprite.setFreeFont(FSSB24);
        tempSprite.setCursor(0,34);
        tempSprite.print(tbuffer);
        tempSprite.pushSprite(240,30,TFT_TRANSPARENT);
        strcpy(oldtemp,tbuffer);
     }
     
     vTaskDelay(1000 / portTICK_PERIOD_MS);
     

  }
}

void loop() {

  ArduinoOTA.handle();

  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
/*
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
    if ( txt_colour == TFT_YELLOW) {
      txt_colour = TFT_GREEN;
    }
    else {
      txt_colour = TFT_YELLOW;
    }
    //printTouchToSerial(p);
    //printTouchToDisplay(p);
    //delay(500);
  }
*/
  if (millis() - lastPublishTime >= publishInterval) {
    char tbuffer[64];
    struct tm time3;
    tmInfo3 = get_local_time(NYCTZ,&time3);
    //time(&now);
    //tmInfo = localtime_r(&now, &info);
  
    strftime(tbuffer,63,"{\"payload\": \"loop %Y%m%d %H:%M:%S\"}",tmInfo3);
    
    snprintf(metricData,sizeof(metricData),"%s",tbuffer);
    // Prepare your sensor data (e.g., temperature reading) in metricData
    // Example:
    // float temperature = readSensorData();
    // snprintf(metricData, sizeof(metricData), "{\"temperature\": %.2f}", temperature);
    publishMetricData();
    lastPublishTime = millis();
  }
}

void reconnectMQTT() {
  int reconnectCount;
  Serial.print("Reconnecting to MQTT Broker: ");
  Serial.println(mqtt_broker);
  Serial.printf("Is client connected? %d\n",client.connected());
   reconnectCount = 0;

  while (!client.connected()) {
    if (client.connect(clientId)) {
      /*
      char tbuffer[64];
        time(&now);
        tmInfo = localtime_r(&now, &info);
  
      strftime(tbuffer,63,"Up and running at %H:%M:%S",&info);
      */
      Serial.println("Connected to MQTT Broker");
      client.subscribe(subscribe_topic);
      //client.publish(publish_topic,tbuffer);
      reconnectCount = 0;
    } else {
      Serial.print("Failed with state: ");
      Serial.println(client.state());
      reconnectCount++;
      if ( reconnectCount > 20) {
         esp_restart();
      }
      delay(1000);

    }
  }
}

void publishMetricData() {
  client.publish(publish_topic, metricData);
  Serial.print("Published data: ");
  Serial.println(metricData);
}

void receiveSubscribed(char* topic, byte* payload, unsigned int length) {
  char *end;
  char tempbuffer[64];

  Serial.print("Message arrived, topic = \"");
  Serial.print(topic);
  Serial.println("\"");
  Serial.print("payload=");
  for (int i = 0; i < length; i++) {
    tempbuffer[i] = payload[i];
    Serial.print((char)payload[i]);
  }
  tempbuffer[length] = '\0';
  current_temp = strtol(tempbuffer,&end,10);
  Serial.println();

  // Handle received messages based on the topic

  // Example: control LED based on command
  if (strcmp(topic, subscribe_topic) == 0) {
    String message = "";
    for (int i = 0; i < length; i++) {
      message += (char)payload[i];
    }
    Serial.print("Received: ");
    Serial.print(message);
/*
    if (message == "ON") {
      // Turn on LED
    } else if (message == "OFF") {
      // Turn off LED
    }
*/
  }
}

