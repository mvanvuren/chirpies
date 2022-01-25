#include <Arduino.h>
#include <I2CSoilMoistureSensor.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <WiFi-Credentials.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <SPI.h>
#include <StopWatch.h>

#define DEEP_SLEEP_MODE
#ifdef DEEP_SLEEP_MODE
  #define SLEEP_TIME 300e6
  #define SLEEP_RESOLUTION StopWatch::MICROS
  #define SLEEP(...) { ESP.deepSleep(__VA_ARGS__); }
#else
  #define SLEEP_TIME 300e3
  #define SLEEP_RESOLUTION StopWatch::MILLIS  
  #define SLEEP(...) { delay(__VA_ARGS__); }
#endif

#define DBG_CHIRPIES
#ifdef DBG_CHIRPIES
  #define DBG_PRINTER Serial
  #define DBG_PRINT(...) { DBG_PRINTER.print(__VA_ARGS__); }
  #define DBG_PRINTLN(...) { DBG_PRINTER.println(__VA_ARGS__); }
  #define DBG_PRINTF(...) { DBG_PRINTER.printf(__VA_ARGS__); }
#else
  #define DBG_PRINT(...) {}
  #define DBG_PRINTLN(...) {}
  #define DBG_PRINTF(...) {}
#endif

//#define LED_DBG
#ifdef LED_DBG
  #define DBG_LED(x) { flash(x); }
  void flash(uint8_t x) {
    pinMode(LED_BUILTIN, OUTPUT);
    while(x-- > 0) {
      digitalWrite(LED_BUILTIN, HIGH); delay(500);
      digitalWrite(LED_BUILTIN, LOW); delay(500);
    }
  } 
#else
  #define DBG_LED(x) {}
#endif

I2CSoilMoistureSensor sensor20(0x20);
I2CSoilMoistureSensor sensor21(0x21);
WiFiClient wifiClient;
StopWatch stopWatch(SLEEP_RESOLUTION);

#define IDX_SOILMOISTURE_20 560
#define IDX_SOILMOISTURE_21 561
#define IDX_LIGHT_20        562
#define IDX_LIGHT_21        563

#define MAX_WIFI_RETRY 25

const char* topic = "domoticz/in";
IPAddress hostIp(192, 168, 0, 162);
IPAddress gatewayIp(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress domoticzpiIp(192, 168, 0, 40);
const char *mqttServer = "192.168.0.40";
const uint16_t mqttPort = 1883;
const uint16_t httpPort = 80;

Adafruit_MQTT_Client mqttClient(&wifiClient, mqttServer, mqttPort);

unsigned int soilMoisture20;
unsigned int light20;
unsigned int soilMoisture21;
unsigned int light21;


void worker();
void getData();
void sendData();

void setup() 
{
  delay(10);

#ifdef DBG_CHIRPIES
  DBG_PRINTER.begin(9600);
  DBG_PRINTER.setDebugOutput(true);
#endif

  DEBUG_PRINTLN(domoticzpiIp.toString().c_str());
  
  Wire.begin();

  sensor20.begin(true);
  sensor21.begin(true);

#ifdef DEEP_SLEEP_MODE  
  worker();
#endif 
}


void loop() {
    worker();
}


void worker()
{
    stopWatch.reset();
    stopWatch.start();

    getData();
    sendData();

  DBG_PRINTLN(F("Sleeping..."));
  
  SLEEP(SLEEP_TIME - stopWatch.elapsed());  // delay or deep sleep, depending on DEEP_SLEEP_MODE
}


void getData()
{
    soilMoisture20 = sensor20.getCapacitance();
    soilMoisture21 = sensor21.getCapacitance();
    delay(1000);
    light20 = sensor20.getLight(true);
    light21 = sensor21.getLight(true);
    
    DBG_PRINTLN(String(F("Soil Moisture 20: ")) + soilMoisture20);
    DBG_PRINTLN(String(F("Light Reading 20: ")) + light20);
    DBG_PRINTLN(String(F("Soil Moisture 21: ")) + soilMoisture21);
    DBG_PRINTLN(String(F("Light Reading 21: ")) + light21);
}

bool wifiConnect() {

  DBG_PRINTLN(F("WiFi connect..."));

  WiFi.forceSleepWake();
  delay(1);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // TOOD: WiFi.config(hostIp, gatewayIp, subnet); // speed things up
  WiFi.begin(ssid, password);

  uint8_t retryCount = MAX_WIFI_RETRY;
  while (retryCount-- > 0) {
    if (WiFi.status() == WL_CONNECTED) {
      DBG_PRINTLN(WiFi.localIP()); 
      return true; 
    }
    delay(500);
    DBG_PRINTLN(".");
  }

  DBG_PRINTLN(F("ERROR: wifiConnect"));

  return false;
}

void wifiDisconnect() {
  
  DBG_PRINTLN(F("wifiDisconnect"));

  wifiClient.flush();
  wifiClient.stop();

  WiFi.disconnect();
  WiFi.forceSleepBegin();
  delay(1);  
}

void mqttDisconnect() {
    mqttClient.disconnect();
    delay(PUBLISH_TIMEOUT_MS); // TODO: looking for waitUntil ? wifiClient done with sending ?    
}


bool mqttConnect() {
    DBG_PRINTLN(F("MQTT connect..."));

    int8_t code = -1;
    uint8_t retryCount = MAX_WIFI_RETRY;
    while (retryCount-- > 0) 
    {
        code = mqttClient.connect();
        if (code == 0)
        {
            DBG_PRINTLN(F("MQTT: connected"));
            return true;
        }
        mqttDisconnect();
        DBG_PRINTLN(".");
    }

    DBG_PRINTLN(F("ERROR: mqttConnect"));
    DBG_PRINTLN(mqttClient.connectErrorString(code));

    return false;  
}


void sendPayload(uint16_t idx, unsigned int sValue)
{
    String payload = String("{") +
                                F("\"idx\":") + idx +
                                F(",\"nvalue\":0") +
                                F(",\"svalue\":\"") + sValue + "\"" +
                            "}";

    DBG_PRINTLN(payload);

    if (mqttClient.publish(topic, payload.c_str())) {
        DBG_PRINTLN(F("Payload published"));
        delay(PUBLISH_TIMEOUT_MS); // TODO: looking for waitUntil ? wifiClient done with sending ?
    } else {
        DBG_PRINTLN(F("ERROR: Payload NOT published"));
    }
}

void sendData()
{
    if (!wifiConnect()) return;

#ifdef DBG_CHIRPIES
    WiFi.printDiag(DBG_PRINTER);
#endif

    DBG_PRINTLN(F("Client connect..."));
    if (!wifiClient.connect(domoticzpiIp, httpPort)) // TODO: needed?
    {
        DBG_PRINTLN(F("ERROR: Client connection"));
        return;
    }

    if (mqttConnect()) {
        sendPayload(IDX_SOILMOISTURE_20, soilMoisture20);
        sendPayload(IDX_LIGHT_20, light20);
        sendPayload(IDX_SOILMOISTURE_21, soilMoisture21);
        sendPayload(IDX_LIGHT_21, light21);
    }

    mqttDisconnect();
    wifiDisconnect();
}