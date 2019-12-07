#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Bounce2.h>
#include <ESP8266WebServer.h>
#include <WiFiServer.h>
#include <ESP8266WebServer.h>

//#define DEBUGSERIAL

byte packetBuffer[25];
WiFiClient tcpclient;
WiFiUDP Udp;
IPAddress clientIP;
uint16_t clientPort;

const char* ssid = "your-wifi-ssid";
const char* password = "your-wifi-password";

// 4 GPIO inputs debouncing
Bounce *debouncer[4];

ESP8266WebServer server(80); //HTTP server on port 80

/* Settings in Arduino IDE:
   Generic ESP8266 module
   Flash mode: DOUT
   Flash size: 1M (64k SPIFFS)
   Debug port: disabled
   Debug level: none
   IwIP variant: v2 lower memory
   Reset method: ck
   Crystal frequency: 26 MHz
   VTables: Flash
   Flash frequency: 40 MHz
   CPU frequency: 80 MHz
   Builtin LED: 1
   Upload speed: 115200
   Erase flash: only sketch
   Port: <via wifi after first attempt>
*/

void handleRoot() {
  // Debugging: tell me what you see on MDNS
  String status = String("<html><pre>");

  status += String("\n\nClient IP address is ") + clientIP.toString() + String("\n");
  status += String("</html>");

  server.send(200, "text/html", status.c_str());
}

void setup() {
#ifdef DEBUGSERIAL
  Serial.begin(115200);
  Serial.println("startup");
#else
  // make GPIO 1 (TX) and 3 (RX) both GPIO (FUNCTION_3) instead of serial
  // (which is FUNCTION_0).
  pinMode(1, FUNCTION_3); 
  pinMode(3, FUNCTION_3); 
  pinMode(1, OUTPUT);
  pinMode(3, OUTPUT);
  digitalWrite(1, HIGH); // Turn on LED while we're waiting for wireless
#endif

  pinMode(0, INPUT);
  pinMode(2, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  // WiFi.setSleepMode(WIFI_NONE_SLEEP); // to avoid sleep timer issues (Debugging only)
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
#ifdef DEBUGSERIAL 
    Serial.println("Connection Failed! Rebooting...");
#endif
    delay(5000);
    ESP.restart();
  }

#ifndef DEBUGSERIAL
  pinMode(1, INPUT); // will also turn off LED (which we want after wireless is init'd)
  pinMode(3, INPUT);
#endif

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  String name = String("tetris-controller-") + ESP.getChipId();
  ArduinoOTA.setHostname(name.c_str());

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
#ifdef DEBUGSERIAL 
      Serial.println("Start");
#endif
  });
  ArduinoOTA.onEnd([]() {
#ifdef DEBUGSERIAL 
      Serial.println("\nEnd");
#endif
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
#ifdef DEBUGSERIAL 
      // Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
#endif
  });
  ArduinoOTA.onError([](ota_error_t error) {
#ifdef DEBUGSERIAL 
      /*
      Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
      */
#endif
  });
  ArduinoOTA.begin();

  Udp.begin(49152); // arbitrary ephemeral port

  server.on("/", handleRoot);
  server.begin();

  // 4 GPIO pins debounced
  for (int i=0; i<4; i++) {
    debouncer[i] = new Bounce();
    debouncer[i]->attach(i); // Don't try to use internal pullups (not even sure they exist)
    debouncer[i]->interval(25);
  }
}

bool reconnect() {
  // For some reason, the display isn't properly announcing its
  // "_tetris._udp" or "_tetris._tcp" services. So we'll piggyback on
  // one that *is* working: _http._tcp. And we'll hard-code the port. :shrug:
  int n = MDNS.queryService("http", "tcp");
  if (n == 0) {
    return false;
  } else {
    // FIXME: hard-coded port b/c MDNS isn't doing what we want on the display
    clientPort = 8267;
    // Find the one that begins with "tetris-display" and use its IP
    bool found = false;
    for (int i=0; i<n; i++) {
      String resolvedName = MDNS.hostname(i);
      if (resolvedName.startsWith("tetris-display")) {
	found = true;
	clientIP = MDNS.IP(i);
	break;
      }
    }
    if (!found) {
      // Guess it's using the static default?
      clientIP = (IPAddress)(192,168,4,1);
      clientPort = 8267;
    }
    return(tcpclient.connect(clientIP, clientPort));
  }
}

void handleButtonPress(int i)
{
  if (!tcpclient.connected()) {
    if (!reconnect()) {
      return;
    }
    tcpclient.setNoDelay(true);
  }

  if (tcpclient.available() > 0) {
    char c = tcpclient.read(); // read, drain, discard
  }

  switch (i) {
  case 0:
    packetBuffer[0] = 'a'; // left
    break;
  case 3:
    packetBuffer[0] = 'd'; // right
    break;
  case 1:
    packetBuffer[0] = ' '; // drop
    break;
  case 2:
    packetBuffer[0] = 'e'; // rotate right
    break;
  }
  // Use UDP for the game as much as possible

  Udp.beginPacket(clientIP, clientPort);
  Udp.write(packetBuffer, 1);
  Udp.endPacket();
/*
  // This works, but has more overhead
  tcpclient.write(packetBuffer, 1);
*/
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  for (int i=0; i<4; i++) {
    debouncer[i]->update();
    if (debouncer[i]->fell()) {
      handleButtonPress(i);
    }
  }
}
