
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <SPI.h>
#include <Wire.h>
#include "SparkFun_AS3935.h"
#include <ArduinoJson.h>
#include <EEPROM.h>


// 0x03 is default, but the address can also be 0x02, 0x01.
// Adjust the address jumpers on the underside of the product.
#define AS3935_ADDR 0x03
#define INDOOR 0x12
#define OUTDOOR 0xE
#define LIGHTNING_INT 0x08
#define DISTURBER_INT 0x04
#define NOISE_INT 0x01
#define EP_SIZE 512

SparkFun_AS3935 lightning(AS3935_ADDR);


#define DBG_OUTPUT_PORT Serial


ESP8266WebServer server(80);

// Interrupt pin for lightning detection
const int lightningInt = 14;

// This variable holds the number representing the lightning or non-lightning
// event issued by the lightning detector.
int intVal = 0;
int noise = 2; // Value between 1-7
int disturber = 2; // Value between 1-10

String endpoint = "";

const size_t capacity = JSON_ARRAY_SIZE(7) + JSON_OBJECT_SIZE(4) + 140;
DynamicJsonDocument doc(capacity);



String getContentType(String filename) {
  if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

bool handleFileRead(String path) {
  DBG_OUTPUT_PORT.println("handleFileRead: " + path);
  if (path.endsWith("/")) {
    path += "index.html";
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz)) {
      path += ".gz";
    }
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void GET_A(String msg) {
  String Link = endpoint + "/json.htm?type=command&param=udevice&idx=168&nvalue=0&svalue=" + msg;
  HTTPClient http;
  http.begin(Link);
  http.end();
}

void GET_B() {
  String Link = endpoint + "/json.htm?type=command&param=udevice&idx=168&nvalue=0&svalue=1";
  HTTPClient http;
  http.begin(Link);
  http.end();
}

void serveData() {
  String j;
  serializeJson(doc, j);
  server.send(200, "application/json", j);
}

void doSave() {
  eraseEP();
  String j;
  serializeJson(doc, j);
  DBG_OUTPUT_PORT.println(j);
  for (int i = 0; i < j.length() ; i++) {
    EEPROM.write(i, j[i]);
  }
  EEPROM.commit();
}

void saveData() {
  if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("ip")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String ip = server.arg("ip");

    doc["ssid"] = ssid;
    doc["pass"] = pass;
    doc["ip"] = ip;

    doSave();
    server.send(200, "application/json", "{\"status\":true}");

    delay(1000);
    ESP.restart();
  }
  server.send(500, "text/plain", "bad request!");
}

bool checkSaved() {

  char buf[EP_SIZE];
  for (int i = 0; i < EP_SIZE; i ++) {
    buf[i] = EEPROM.read(i);
  }

  DeserializationError error = deserializeJson(doc, buf);

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }
  return true;

}

void eraseEP() {
  for (int i = 0; i < EP_SIZE; i ++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}


void setup(void) {

  EEPROM.begin(EP_SIZE);

  pinMode(lightningInt, INPUT);
  pinMode(0, INPUT_PULLUP);

  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.println("AS3935 Franklin Lightning Detector");
  DBG_OUTPUT_PORT.print("\n");
  DBG_OUTPUT_PORT.print("\n");
  DBG_OUTPUT_PORT.print("\n");
  DBG_OUTPUT_PORT.print("\n");



  if (!SPIFFS.begin()) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }



  if (checkSaved()) {

    String ssid = doc["ssid"];
    String password = doc["pass"];

    //WIFI INIT
    DBG_OUTPUT_PORT.print("Connecting to :");
    DBG_OUTPUT_PORT.println(ssid);
    DBG_OUTPUT_PORT.println(password);
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    int c = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      DBG_OUTPUT_PORT.print(".");
      if (digitalRead(0) == LOW) {
        eraseEP();
        ESP.restart();
      }

    }
    DBG_OUTPUT_PORT.println("");
    DBG_OUTPUT_PORT.print("Connected! IP address: ");
    DBG_OUTPUT_PORT.println(WiFi.localIP());
  } else {

    const char* defult = "{\"ssid\":\"\",\"pass\":\"\",\"ip\":\"\",\"chart\":[]}";
    deserializeJson(doc, defult);

    DBG_OUTPUT_PORT.print("Setting AP (Access Point)…");
    WiFi.softAP("esp8266w");

    IPAddress IP = WiFi.softAPIP();
    DBG_OUTPUT_PORT.print("AP IP address: ");
    DBG_OUTPUT_PORT.println(IP);

    // Print ESP8266 Local IP Address
    DBG_OUTPUT_PORT.println(WiFi.localIP());


  }

  MDNS.begin("esplan");


  DBG_OUTPUT_PORT.print("Open http://");
  DBG_OUTPUT_PORT.print("esplan");
  DBG_OUTPUT_PORT.println(".local");

  server.on("/get", HTTP_GET, serveData);
  server.on("/save", HTTP_GET, saveData);

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "FileNotFound");
    }
  });

  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");


  Wire.begin(); // Begin Wire before lightning sensor.
  if ( !lightning.begin() ) { // Initialize the sensor.
    DBG_OUTPUT_PORT.println ("Lightning Detector did not start up!");
  }
  else {
    DBG_OUTPUT_PORT.println("Schmow-ZoW, Lightning Detector Ready!");
  }

  String t = doc["ip"];

  endpoint = t;

}

void loop(void) {
  server.handleClient();
  if (digitalRead(0) == LOW) {
    eraseEP();
    ESP.restart();
  }

    if (digitalRead(lightningInt) == HIGH) {
      // Hardware has alerted us to an event, now we read the interrupt register
      // to see exactly what it is.
      intVal = lightning.readInterruptReg();
      if (intVal == NOISE_INT) {
        DBG_OUTPUT_PORT.println("Noise.");
        // Too much noise? Uncomment the code below, a higher number means better
        // noise rejection.
        //lightning.setNoiseLevel(setNoiseLevel);
      }
      else if (intVal == DISTURBER_INT) {
        DBG_OUTPUT_PORT.println("Disturber.");
        // Too many disturbers? Uncomment the code below, a higher number means better
        // disturber rejection.
        //lightning.watchdogThreshold(threshVal);
      }
      else if (intVal == LIGHTNING_INT) {
        DBG_OUTPUT_PORT.println("Lightning Strike Detected!");
        // Lightning! Now how far away is it? Distance estimation takes into
        // account any previously seen events in the last 15 seconds.
        byte distance = lightning.distanceToStorm();
        DBG_OUTPUT_PORT.print("Approximately: ");
        DBG_OUTPUT_PORT.print(distance);
        DBG_OUTPUT_PORT.println("km away!");
        GET_A("Lightning%20Strike%20Detected!Approximately:%20" + String(distance) + "km%20away!");
        delay(1000);
        GET_B();
      }
      delay(100);
    }
  
}
