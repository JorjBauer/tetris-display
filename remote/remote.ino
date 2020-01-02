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

char ssid[50] = "";
char password[50] = "";
bool staMode = false;
bool fsRunning = false; // set to true when SPIFFS is set up

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

void handleSubmit() {
  String new_ssid = server.arg("ssid");
  String new_password = server.arg("password");
  strncpy(ssid, new_ssid.c_str(), 50);
  strncpy(password, new_password.c_str(), 50);
  writePrefs();

  // Redirect to /restart to apply changs
  server.sendHeader("Location", String("/restart"), true);
  server.send(302, "text/plain", "");
}

void handleConfig()
{
  server.send(200, "text/html",
              "<!DOCTYPE html><html>"
              "<head>"
              "</head>"
              "<body>"
              "<form action='/submit' method='post'>"
              "<div><label for='ssid'>Connect to SSID:</label>"
              "<input type='text' id='ssid' name='ssid' /></div>"
              "<div><label for='password'>Network Password:</label>"
              "<input type='password' id='password' name='password' /></div>"
              "<div><input type='submit' value='Save' /></div>"
              "</form>"
              "</body></html");
}

void handleRestart() {
  server.send(200, "text/html", "Okay, restarting");
  delay(1000);
  ESP.restart();
}

void handleRoot() {
  // Debugging: tell me what you see on MDNS
  String status = String("<html><h1>Welcome to the Tetris controller!</h1>"
			 "<p>Embedded pages:</p>"
			 "<ul>"
			 "<li><a href='/config'>/config</a>: change configuration (SSID, password)</li>"
			 "<li><a href='/restart'>/restart</a>: reboot the controller</li>"
			 "</ul></p>");
  status += String("<p>Current configuration:</p><pre>ssid: ") + 
    String(ssid) + String("\nconnecting to IP: ") + clientIP.toString() + String("\n</pre></html>");

  server.send(200, "text/html", status.c_str());
}

void processConfig(const char *lhs, const char *rhs)
{
  if (!strcmp(lhs, "ssid")) {
    strncpy(ssid, (char *)rhs, 50);
    staMode = true;
  } else if (!strcmp(lhs, "password")) {
    strncpy(password, (char *)rhs, 50);
  }
}

void writePrefs()
{
  fs::File f = SPIFFS.open("/controller.cfg", "w");
  f.println("# Submitted config");
  f.print("ssid=");
  f.println(ssid);
  f.print("password=");
  f.println(password);
  f.close();
}

void readPrefs(fs::File f)
{
  bool readingVar = true;
  int8_t slen = 0;
  char lhs[50] = {'\0'};
  char *lp = lhs;
  char rhs[50] = {'\0'};
  char *rp = rhs;
  for(uint8_t i=0; i<f.size(); i++) {
    char c = f.read();
    // Skip commented out and blank lines
    if (slen == 0 && (c == '#' || c == '\n' || c == '\r')) {
      continue;
    }

    if (slen >= 49) {
      // safety: reset
      slen = 0;
      readingVar = true;
      lhs[0] = rhs[0] = '\0';
    }
    if (readingVar) {
      // Keep reading a variable name until we hit an '='
      if (c == '\n' || c == '\r') {
        // Abort - got a return before the '='
        slen = 0;
        lhs[0] = '\0';
      }
      else if (c == '=') {
        readingVar = false;
        slen = 0;
        rhs[0] = '\0';
      } else {
        lhs[slen++] = c;
        lhs[slen] = '\0';
      }
    } else {
      // Keep reading a variable value until we hit a newline
      if (c == '\n' || c == '\r') {
        processConfig(lhs,rhs);
        readingVar = true;
        slen = 0;
        lhs[0] = rhs[0] = '\0';
      } else {
        rhs[slen++] = c;
        rhs[slen] = '\0';
      }
    }
  }
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

  if (SPIFFS.begin()) {
    fsRunning = true;
  } else {
    fsRunning = false;
  }

  if (fsRunning) {
    // try to load the config file.
    fs::File f = SPIFFS.open("/controller.cfg", "r");
    if (!f) {
      // No config file found; re-format the SPIFFS and create a default config.
      if (!SPIFFS.format()) {
	// Not sure what happened, but we can't use the filesystem.
	fsRunning = false;
      } else {
	f = SPIFFS.open("/controller.cfg", "w");
	f.println("# Blank auto-generated config");
	f.close();
	f = SPIFFS.open("/controller.cfg", "r");
	if (!f) {
	  fsRunning = false;
	}
      }
    }

    if (f) {
      readPrefs(f);
    }
  }

  if (!ssid[0]) {
    staMode = false;
  } else {
    staMode = true;
  }

  if (staMode) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    // WiFi.setSleepMode(WIFI_NONE_SLEEP); // to avoid sleep timer issues (Debugging only)
    uint8_t count=0;
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      count++;
#ifdef DEBUGSERIAL 
      Serial.println("Connection Failed! Delaying...");
#endif
      delay(5000);
    }
    if (count >= 5) {
      staMode = false;
    }
  }

  if (!staMode) {
    // Try to connect to the "TetrisDisplay" network.
    WiFi.mode(WIFI_STA);
    WiFi.begin("TetrisDisplay", "");

    // Explicit IP, b/c no DHCP exists
    IPAddress local_IP(192,168,4,2);
    IPAddress gateway(192,168,4,1);
    IPAddress subnet(255,255,255,0);

    uint8_t count=0;
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      count++;
#ifdef DEBUGSERIAL 
      Serial.println("Connection Failed! Delaying...");
#endif
      delay(5000);
    }
    if (count >= 5) {
#ifdef DEBUGSERIAL 
      Serial.println("Connection Failed! Rebooting...");
#endif
      ESP.restart();
    }
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
  server.on("/restart", handleRestart);
  server.on("/submit", handleSubmit);
  server.on("/config", handleConfig);

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
