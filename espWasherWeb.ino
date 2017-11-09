/*
- Based on a Esp-12 (Compile for Nodemcu)
-Read temp and humidity from a DHT11.
-Read current from a current sensor (HWCT004).
-Present results on a graphical LCD (Nokia 5110 type, PCD8544).
-Also presents results via http. 
-Get time from a Node Red server and show on the display.
  Node red sends time in the request (http://myip?time=1633) for time keeping.

Serial baud rate: 115200

 https://github.com/RandyPatterson/ESP8266_5110/blob/master/examples/HelloWorld/HelloWorld.ino
 http://playground.arduino.cc/Main/DHT11Lib
 https://github.com/bblanchon/ArduinoJson
 Patrik Hermansson 2016-17
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <Time.h>
#include <ArduinoJson.h>

// For state machine
long lastMsg = 0;

String temp, hum, curHour, curMinute;
double current;

#include "ESP8266_Nokia5110.h"
#define PIN_SCE   5  //CE
#define PIN_RESET 4  //RST
#define PIN_DC    12  //DC
#define PIN_SDIN  2  //Din
#define PIN_SCLK  14 //Clk
ESP8266_Nokia5110 lcd = ESP8266_Nokia5110(PIN_SCLK,PIN_SDIN,PIN_DC,PIN_SCE,PIN_RESET);

// Emoncms power lib
#include "EmonLiteESP.h"
EmonLiteESP power;
// Analog GPIO on the ESP8266
#define CURRENT_PIN             0

// If you are using a nude ESP8266 board it will be 1.0V, if using a NodeMCU there
// is a voltage divider in place, so use 3.3V instead.
#define REFERENCE_VOLTAGE       1.0
// Precision of the ADC measure in bits. Arduinos and ESP8266 use 10bits ADCs, but the
// ADS1115 is a 16bits ADC
#define ADC_BITS                10
// Number of decimal positions for the current output
//#define CURRENT_PRECISION       3
// This is basically the volts per amper ratio of your current measurement sensor.
// If your sensor has a voltage output it will be written in the sensor enclosure,
// something like "30V 1A", otherwise it will depend on the burden resistor you are
// using.

//#define CURRENT_RATIO           66    // based on 30ohm burden resistor and 2000 windings in SCT-013-000
// 2000/30=66,67
// Mine is 1000 windings and R is 100ohm. 1000/100=10
#define CURRENT_RATIO 10

// This version of the library only calculate aparent power, so it asumes a fixes
// mains voltage
#define MAINS_VOLTAGE           230
// Number of samples each time you measure
#define SAMPLES_X_MEASUREMENT   1000

String localip;

//For Json output
StaticJsonBuffer<200> jsonBuffer;
JsonObject& root = jsonBuffer.createObject();
char msg[100];

unsigned int currentCallback() {
  // If usingthe ADC GPIO in the ESP8266
  return analogRead(CURRENT_PIN);
}

char ssid[]="NETGEAR83";
const char* password = "..........";

ESP8266WebServer server(80);

#include <dht11.h>
dht11 DHT11;
#define DHT11PIN 13

// Mqtt
#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient client(espClient);
const char* mqtt_server = "192.168.1.79";

void handleRoot() {
  int chk = DHT11.read(DHT11PIN);    // READ DATA
  String temp = String((float)DHT11.temperature,2);
  String shortTemp=temp.substring(0,2);
  String hum = String((float)DHT11.humidity,2);
  String humShort = hum.substring(0,2);

  // Get power reading from Current Transformer
  double current = power.getCurrent(SAMPLES_X_MEASUREMENT);
  int cpower = current * MAINS_VOLTAGE;

  Serial.print(temp);
  Serial.println("C");
  Serial.print(hum);
  Serial.println("%");
  Serial.print(cpower);
  Serial.println("W");

  time_t t = now();

  
// Check if the request from the client contains current hours and minute
 if (server.hasArg("time")) {
  unsigned long iclienttime;
  String clienttime = server.arg("time");
  //Serial.print("Incoming data: ");
  //Serial.println(clienttime);

  iclienttime = clienttime.toInt();

  // Set system time
  time_t t = iclienttime;
  //Serial.print("t: ");
  //Serial.println(t);
  setTime(t);  
  // Show what you got
  Serial.print("Got time from server: ");
  Serial.print(hour());
  Serial.println();
 }


  // Create Json string
  // Set values in Json variable

  root["time"] = String(t);
  root["temp"] = temp;
  root["humidity"] = hum;
  root["power"] = cpower;
  root["test"] = 1;
  
  char buffer[256];
  root.printTo(buffer, sizeof(buffer));
  String servermess=buffer;
  // Respond to client
  server.send(200, "text/plain", buffer);
 
}

void setup() {
  char* bestWifi[15];
  Serial.begin(115200);
  Serial.println("Booting");

  // Start lcd
  lcd.begin();
  lcd.clear();
  lcd.setContrast(0x20);
  lcd.setCursor(0,0);
  lcd.print("Hello World");  

  // Setup wifi

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks(false,true);
  Serial.println("scan done");
  Serial.println(n);

  if (n == 0)
    Serial.println("no networks found");
  else
  {
    // sort by RSSI
    int indices[n];
    for (int i = 0; i < n; i++) {
      indices[i] = i;
    }
    for (int i = 0; i < n; i++) {
      for (int j = i + 1; j < n; j++) {
        if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
          std::swap(indices[i], indices[j]);
        }
      }
      Serial.print(WiFi.SSID(i));
      Serial.print("-");
      Serial.println(WiFi.RSSI(i));
    }
          Serial.print("Best Wifi found: ");
          Serial.println(WiFi.SSID(0));
          strcpy (ssid, WiFi.SSID(0).c_str());
          lcd.setCursor(0,1);
          lcd.print(WiFi.SSID(0));
  }
  
  WiFi.mode(WIFI_STA);
  // Connect to strongest network
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Mqtt
  client.setServer(mqtt_server, 1883);

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  
  localip = WiFi.localIP().toString();
  Serial.print("IP address: ");
  Serial.println(localip);

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  // Further server settings
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");

  // Init current meter
  power.initCurrent(currentCallback, ADC_BITS, REFERENCE_VOLTAGE, CURRENT_RATIO);  
}
void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  // "State machine", check sensor and send values when 2 minutes have passed. 
  long now = millis();
  if (now - lastMsg > 120000) {  // Every 2 minutes
  //if (now - lastMsg > 10000) {  // Every 10 seconds
    lastMsg = now;
    // Update lcd
    Serial.println("Update lcd");
    showOnLcd(localip);

    // Mqtt
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
    
    String c = String(current);
    /*
    String message = curHour + ":" + curMinute + ";" + temp + ";" + hum + ";" + c;
    char cmessage[100];
    message.toCharArray(cmessage, 100);
    bool res = client.publish("EspWasher", cmessage, 100);
    */
    root["time"] = curHour + "." + curMinute;
    root["temp"] = temp;
    root["hum"] = hum;
    root["current"] = c;
    
    root.printTo((char*)msg, root.measureLength() + 1);
    client.publish("EspWasher", msg);  // Wants a char
  }  
}

void showOnLcd(String localip){
    lcd.clear();
    // Print IP address
    lcd.setCursor(0,0);   // (Col, Row)
    String ipshort = String(localip);
    ipshort.remove(0,9);
    lcd.print("IP: ");
    lcd.print(ipshort);

    // Print time
    time_t t = now();
    if (timeStatus()!= timeNotSet) {
      lcd.setCursor(0,1);   // (Col, Row)
      lcd.print("Tid: ");
      curHour = String (hour(t));
      curMinute = String(minute(t));
      if (minute(t)<10) {
        curMinute="0"+curMinute;
      }
      Serial.print(curHour);
      Serial.print(":");
      Serial.println(curMinute);
      lcd.print(curHour);
      lcd.print(":");
      lcd.print(curMinute);
    }
    else {
      Serial.println("Time not set");
    }
    int chk = DHT11.read(DHT11PIN);    // READ DATA, temp and humidity
    temp = String((float)DHT11.temperature,1);
    hum = String((float)DHT11.humidity, 0);
    lcd.setCursor(2,2);
    lcd.print("Temp: " + temp+"C");
    lcd.setCursor(2,3);
    lcd.print("Fukt: " + hum+"%");

    // Read power and display
    current = power.getCurrent(SAMPLES_X_MEASUREMENT);
    lcd.setCursor(1,4);
    lcd.print("Pwr: ");
    lcd.print(String(round(current * MAINS_VOLTAGE)));
    lcd.print("W");

    // Also print to serial port
    Serial.print(temp);
    Serial.println("C");
    Serial.print(hum);
    Serial.println("%");
    Serial.print(int(current * MAINS_VOLTAGE));
    Serial.println("W");
}

void readDht() {
      Serial.print("Read temp/humidity sensor: ");
    int chk = DHT11.read(DHT11PIN);
    switch (chk)
    {
      case DHTLIB_OK: 
      Serial.println("OK"); 
      break;
      case DHTLIB_ERROR_CHECKSUM: 
      Serial.println("Checksum error"); 
      break;
      case DHTLIB_ERROR_TIMEOUT: 
      Serial.println("Time out error"); 
      break;
      default: 
      Serial.println("Unknown error"); 
      break;
    }
}
void printDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("EspWasherWeb", "emonpi", "emonpimqtt2016")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      //client.publish("EspWasher","hello world from washer");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
