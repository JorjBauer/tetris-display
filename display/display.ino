#include "ClockPrefs.h"
#include "TCPLogger.h"
#include "WebManager.h"
#include "WifiManager.h"
#include "templater.h"

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266httpUpdate.h>

#include "LEDAbstraction.h"
#include "RingPixels.h"
#include <RingBuffer.h>

#include "tetris.h"
#include "tetris-clock.h"
#include "snake.h"

#include <SunriseCalc.h> // from https://github.com/JorjBauer/SunriseCalc
#include <TimeLib.h>

#include <FS.h>

#include "font5x7.h"
#define CHAR_WIDTH 6
#define CHAR_HEIGHT 7

extern Templater templ;

LEDAbstraction ledPanel;

ClockPrefs myprefs;
TCPLogger tlog;
WebManager server(80);
WifiManager wifi;
WiFiUDP Udp;

#define NAME "tetris"

bool fsRunning;

int localPort = 8267;
byte packetBuffer[25];
WiFiServer tcpserver(8267); // tcp server
WiFiClient tcpclient;
bool udpRunStarted = false;

const int ESP_BUILTIN_LED = 1;

// Offscreen text buffering
#define BACKINGTEXTSIZE 100
RingBuffer backingText(BACKINGTEXTSIZE);

// Offscreen pixel buffering
#define BACKINGPIXELSIZE 24
RingPixels backingPixels(NUM_ROWS, BACKINGPIXELSIZE);

Tetris tetrisEngine;
Snake snakeEngine;
bool needsRefresh = true;
bool checkLines = false;
bool running = false;

uint32_t nextTick = 0;
uint32_t nextTimeUpdate = 0;
int32_t sunriseAt = -1; // minutes since midnight, or -1 for "none"
int32_t sunsetAt = -1;
uint8_t sunriseHours;
uint8_t sunriseMinutes;
uint8_t sunsetHours;
uint8_t sunsetMinutes;

SunriseCalc *location = NULL;

bool isDST = false; // always default to false; will set to true
		    // during calculations if autoSetDST is set

TetrisClock *clockDriver = NULL;
bool clockShowing = false;
bool clockRestarting = false;

bool colorWheelMode = false;

bool autoBrightness = true;

int32_t lastCorrection = 0; // When we NTP, save the # of seconds drifted

uint8_t curMon, curDay, curHour, curMinute, curSecond;

enum {
  mode_text     = 0,
  mode_tetris   = 1,
  mode_clock    = 2,
  mode_startup  = 3,
  mode_pickGame = 4,
  mode_snake    = 5,
  mode_tree     = 6
};

uint8_t currentMode = mode_startup;
uint8_t currentGameSelection = mode_tetris; // for the menu
uint32_t pickGameTimeout;

#define MENUTIMEOUT 120000

#define MAX_TREE_BLINKERS 8
uint8_t currentTreeBlinkers;
uint32_t treeBlinkerTime[MAX_TREE_BLINKERS];
uint8_t treeBlinkerState[MAX_TREE_BLINKERS];
uint8_t treeBlinkerMax[MAX_TREE_BLINKERS];
offset treeBlinkers[MAX_TREE_BLINKERS];
uint32_t treeCounter;

/* Settings in Arduino IDE:
   Generic ESP8266 module
   Flash mode: DIO
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

void handleStatus() {

  if (!server.isAuthenticated()) {
    return;
  }

  server.SendHeader();
  repvars *r = templ.newRepvar(String("@SSID@"), String(myprefs.ssid));
  r = templ.addRepvar(r, String("@PASS@"), String(myprefs.password));
  r = templ.addRepvar(r, String("@UPTIME@"), String(millis()));
  r = templ.addRepvar(r, String("@HEAP@"), String(ESP.getFreeHeap()));
  r = templ.addRepvar(r, String("@ID@"), String(ESP.getChipId()));
  r = templ.addRepvar(r, String("@MDNS@"), String(myprefs.mdnsName));
  r = templ.addRepvar(r, String("@COMMENT@"), String(myprefs.comment));
  r = templ.addRepvar(r, String("@ADMINPW@"), String(myprefs.adminPassword));
  r = templ.addRepvar(r, String("@OTAPW@"), String(myprefs.otaPassword));
  r = templ.addRepvar(r, String("@UPDATESVRIP@"), String(myprefs.updateServerHost));
  r = templ.addRepvar(r, String("@UPDATESVRPORT@"), String(myprefs.updateServerPort));
  r = templ.addRepvar(r, String("@UPDATESVRPATH@"), String(myprefs.updateServerPath));
  r = templ.addRepvar(r, String("@HASHMAT@"), String(myprefs.hashMaterial));
  r = templ.addRepvar(r, String("@EPOCH@"), String(server.epochTime));
  r = templ.addRepvar(r, String("@NTPSYNC@"), String(server.lastTimeUpdate));

  tmElements_t tm;
  breakTime(server.epochTime, tm);
  r = templ.addRepvar(r, String("@HH@"), String(tm.Hour));
  r = templ.addRepvar(r, String("@MM@"), String(tm.Minute));
  r = templ.addRepvar(r, String("@SS@"), String(tm.Second));
  r = templ.addRepvar(r, String("@M@"), String(tm.Month));
  r = templ.addRepvar(r, String("@D@"), String(tm.Day));
  r = templ.addRepvar(r, String("@Y@"), String(tm.Year+1970));

  r = templ.addRepvar(r, String("@LAT@"), String(myprefs.lat));
  r = templ.addRepvar(r, String("@LON@"), String(myprefs.lon));
  r = templ.addRepvar(r, String("@TZ@"), String(myprefs.defaultTimeZone));
  r = templ.addRepvar(r, String("@DST@"), String(myprefs.autoSetDST));
  r = templ.addRepvar(r, String("@TCPCLIENT@"), String((tcpclient && tcpclient.connected()) ? "Connected" : "Not connected"));
  r = templ.addRepvar(r, String("@MODE@"),
                      String( (currentMode == mode_text) ? "mode_text" : 
                              (currentMode == mode_tetris) ? "mode_tetris" :
                              (currentMode == mode_clock) ? "mode_clock" :
                              (currentMode == mode_startup) ? "mode_startup" : 
                              (currentMode == mode_pickGame) ? "mode_pickGame":
                              (currentMode == mode_snake) ? "mode_snake" :
                              (currentMode == mode_tree) ? "mode_tree" :
                              "unknown mode"));
  r = templ.addRepvar(r, String("@SCORE@"),
                      String( (currentMode == mode_tetris) ? tetrisEngine.score() :
                              (currentMode == mode_snake) ? snakeEngine.score() :
                              0));
  r = templ.addRepvar(r, String("@CLOCKSHOWING@"),
                      String( clockShowing ? "true" : "false" ));
  r = templ.addRepvar(r, String("@LASTCORR@"), String(lastCorrection));

  char sunrisebuf[6];
  sprintf(sunrisebuf, "%.2d:%.2d", sunriseHours, sunriseMinutes);
  char sunsetbuf[6];
  sprintf(sunsetbuf, "%.2d:%.2d", sunsetHours, sunsetMinutes);

  r = templ.addRepvar(r, String("@SUNRISE@"), sunrisebuf);
  r = templ.addRepvar(r, String("@SUNSET@"), sunsetbuf);
  r = templ.addRepvar(r, String("@AUTOBRIGHTNESS@"),
                      String(autoBrightness ? "true" : "false"));
  r = templ.addRepvar(r, String("@ISDST@"),
                      String(isDST ? "yes" : "no"));
  
  fs::File f = SPIFFS.open("/status.html", "r");
  templ.generateOutput(&server, f, r);
  f.close();

  server.SendFooter();
}

void handleTetris() {
  fs::File f = SPIFFS.open("/tetris.html", "r");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(f);
  f.close();
}

void handleLeft() {
  if (currentMode == mode_tetris)
    tetrisEngine.MoveLeft();
  else if (currentMode == mode_snake)
    snakeEngine.TurnLeft();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleRight() {
  if (currentMode == mode_tetris)
    tetrisEngine.MoveRight();
  else if (currentMode == mode_snake)
    snakeEngine.TurnRight();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleRotateLeft() {
  if (currentMode == mode_tetris)
    tetrisEngine.RotateLeft();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleRotateRight() {
  if (currentMode == mode_tetris)
    tetrisEngine.RotateRight();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleStep() {
  if (currentMode == mode_tetris)
    tetrisEngine.Step();
  else if (currentMode == mode_snake)
    snakeEngine.Step();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleDrop() {
  if (currentMode == mode_tetris) {
    if (!tetrisEngine.Drop()) {
      gameOver();
    }
  }
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleInit() {
  if (currentMode == mode_tetris) {
    tetrisEngine.Init();
  } else if (currentMode == mode_snake) {
    snakeEngine.Init();
  }
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;
  nextTick = 0;

  running = false;
  udpRunStarted = false;
  ledPanel.setFadeMode(false);
}

void handleTest() {
  String s = "ok";
  server.send(200, "text/html", s);
  running = false;
  udpRunStarted = false;
  needsRefresh = false;

  CRGB colors[8] = { CRGB::White,
		     CHSV(HUE_ORANGE,255,255),
		     CHSV(HUE_YELLOW,255,255),
		     CHSV(HUE_AQUA,255,255),
		     CHSV(HUE_BLUE,255,255),
		     CHSV(HUE_PURPLE,255,255),
		     CHSV(HUE_PINK,255,255),
		     CHSV(HUE_GREEN,255,255) };


  for (int y=0; y<YSIZE; y++) {
    for (int x=0; x<XSIZE; x++) {
      ledPanel.SetLED(x,y,colors[random(104)%8]);
    }
  }
}

void addTextToBackingStore(String s)
{
  for (int i=0; i<s.length(); i++) {
    if (!backingText.isFull()) {
      backingText.addByte(s[i]);
    }
  }
}

bool updateTime()
{
  ledPanel.clear();

  // Pull the time out of WebManager's NTP clock
  tmElements_t tm;
  breakTime(server.epochTime + myprefs.defaultTimeZone * 3600 + (isDST ? 3600 : 0), tm);

  curMinute = tm.Minute;
  curHour = tm.Hour;
  curSecond = tm.Second;

  // Deal with the time first: DST calculation, then set the time!
  uint32_t prevTime = clockDriver->setTime(curHour, curMinute, curSecond, 0, 0);

  curDay = tm.Day;
  curMon = tm.Month;
  if (myprefs.autoSetDST != T_DST_NONE) {

    // dayOfWeek() returns 1 == Sunday
    bool potentialDST = (myprefs.autoSetDST == T_DST_USA) ?
      usIsTodayDST(curDay, curMon, dayOfWeek(server.epochTime)) :
      europeIsTodayDST(curDay, curMon, dayOfWeek(server.epochTime));

    if (potentialDST != isDST) {
      if (curHour >= 2) {
	isDST = !isDST;
        breakTime(server.epochTime + myprefs.defaultTimeZone * 3600 + (isDST ? 3600 : 0), tm);
        
        curMinute = tm.Minute;
        curHour = tm.Hour;
        curSecond = tm.Second;
	prevTime = clockDriver->setTime(curHour, curMinute, curSecond, curMon, curDay);
    
      }
    }
  }

  // Calculate and save the time correction for the /status display
  uint8_t hh = (prevTime >> 16);
  uint8_t mm = (prevTime >> 8) & 0xFF;
  uint8_t ss = (prevTime & 0xFF);

  int32_t oldSeconds = ss + mm*60 + hh*3600;
  int32_t newSeconds = curSecond + curMinute*60 + curHour*3600;

  lastCorrection = newSeconds - oldSeconds;

  // Handle auto-brightness
  if (autoBrightness) {
    // recalculate today's sunrise/sunset times
    if (!location) {
      location = new SunriseCalc(myprefs.lat, myprefs.lon, myprefs.defaultTimeZone);
    }
    location->date(1970+tm.Year, tm.Month, tm.Day, isDST);

    sunriseAt = location->sunrise();
    sunsetAt = location->sunset();    
    
    sunriseHours = sunriseAt / 60;
    sunriseMinutes = sunriseAt % 60;
    sunsetHours = sunsetAt / 60;
    sunsetMinutes = sunsetAt % 60;

    if (sunriseAt == -1) {
      // No sunrise?
      ledPanel.setBrightness(1);
    } else if (sunsetAt == -1) {
      // No sunset?
      ledPanel.setBrightness(40);
    } else if ( ((curHour > sunriseHours) || 
		 ((curHour == sunriseHours) && (curMinute >= sunriseMinutes))) &&
		((curHour < sunsetHours) ||
		 ((curHour == sunsetHours) && (curMinute < sunsetMinutes))) ) {
      // FIXME: could fade in slower than this when near sunrise/sunset ***
      ledPanel.setBrightness(40);
    } else {
      if (curHour > 12 && curHour < 21) {
	// A little generous: find an in-between brightness for "if we have the lights on" hours
	ledPanel.setBrightness(10);
      } else {
	ledPanel.setBrightness(1);
      }
    }
  }

  return true;
}

void startTreeMode()
{
  treeCounter = 10 * 30; // FIXME: 30 second constant
  currentMode = mode_tree;
  ledPanel.setFadeMode(false);
  ledPanel.clear();
  ledPanel.Update();

  currentTreeBlinkers = 0;

  drawTree();

  nextTick = millis();
}

void handleStartTree()
{
  server.send(200, "text/html", "ok");
  startTreeMode();
}

void startClockMode()
{
  currentMode = mode_clock;
  ledPanel.setFadeMode(false);
  ledPanel.clear();
  ledPanel.Update();

  clockRestarting = true;
  nextTick = millis();
}

void handleStartClock()
{
  server.send(200, "text/html", "ok");

  startClockMode();
}

void handleTestClock()
{
  server.send(200, "text/html", "ok, testing clock");

  uint16_t v = 0;
  String a = server.arg("t");
  for (int i=0; i<a.length(); i++) {
    v *= 10;
    uint8_t c = a[i] - '0';
    if (c < 0) c = 0;
    if (c > 9) c = 0;
    v += c;
  }

  uint16_t mm = v % 100;
  uint16_t hh = v / 100;
  clockDriver->setTime(hh, mm, 0, 0, 0);

  clockShowing = true;
  clockRestarting = false;
  nextTick = millis();
}

void handleText() {
  String s = "ok: ";
  running = false;
  udpRunStarted = false;
  needsRefresh = false;

  currentMode = mode_text;
  ledPanel.setFadeMode(true);

  s = s + server.arg("s");
  server.send(200, "text/html", s);

  addTextToBackingStore(server.arg("s"));
}

void handleBrightness() {
  String s = "ok: brightness is now ";

  uint8_t b = 0;
  String a = server.arg("b");
  for (int i=0; i<a.length(); i++) {
    b *= 10;
    uint8_t c = a[i] - '0';
    if (c < 0) c = 0; 
    if (c > 9) c = 0;
    b += c;
  }

  if (b < 1) b = 1;
  if (b > 255) b = 255;

  s += b;
  server.send(200, "text/html", s);
  ledPanel.setBrightness(b);
  autoBrightness = false;
}

void handleAutoBrightness() {
  autoBrightness = !autoBrightness;
  char buf[50];
  sprintf(buf, "autoBrightness is %s", autoBrightness ? "on" : "off");
  server.send(200, "text/html", buf);
}

void handleColorWheel() {
  colorWheelMode = !colorWheelMode;
  char buf[50];
  sprintf(buf, "Color wheel mode is %s", colorWheelMode ? "on" : "off");
  server.send(200, "text/html", buf);
}

void handleUpdate() {
  nextTimeUpdate = 0;
  char buf[50];
  server.send(200, "text/html", "Ok, forcing NTP update");
}

String getContentType(String filename) 
{
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

void handleConfig()
{
  if (!server.isAuthenticated()) {
    return;
  }

  server.SendHeader();

  repvars *r = templ.newRepvar(String("@SSID@"), String(myprefs.ssid));
  r = templ.addRepvar(r, String("@PASS@"), String(myprefs.password));
  r = templ.addRepvar(r, String("@ADMINPW@"), String(myprefs.adminPassword));
  r = templ.addRepvar(r, String("@OTAPW@"), String(myprefs.otaPassword));
  r = templ.addRepvar(r, String("@UPDATESVRIP@"), String(myprefs.updateServerHost));
  r = templ.addRepvar(r, String("@UPDATESVRPORT@"), String(myprefs.updateServerPort));
  r = templ.addRepvar(r, String("@UPDATESVRPATH@"), String(myprefs.updateServerPath));
  r = templ.addRepvar(r, String("@HASHMAT@"), String(myprefs.hashMaterial));
  r = templ.addRepvar(r, String("@COMMENT@"), String(myprefs.comment));
  r = templ.addRepvar(r, String("@LAT@"), String(myprefs.lat));
  r = templ.addRepvar(r, String("@LON@"), String(myprefs.lon));
  r = templ.addRepvar(r, String("@TZ@"), String(myprefs.defaultTimeZone));
  r = templ.addRepvar(r, String("@USDST@"),
                      String(myprefs.autoSetDST == T_DST_USA ? "checked" : ""));
  r = templ.addRepvar(r, String("@EUDST@"),
                      String(myprefs.autoSetDST == T_DST_EU ? "checked" : ""));
  r = templ.addRepvar(r, String("@NODST@"),
                      String(myprefs.autoSetDST == T_DST_NONE ? "checked" : ""));

  fs::File f = SPIFFS.open("/config.html", "r");
  templ.generateOutput(&server, f, r);
  f.close();

  server.SendFooter();
}

void handleSubmit()
{
  if (!server.isAuthenticated()) {
    return;
  }
  
  String new_ssid = server.arg("ssid");
  String new_password = server.arg("password");
  String new_comment = server.arg("comment");
  String new_adminpw = server.arg("adminpw");
  String new_otapw = server.arg("otapw");
  String new_hashmat = server.arg("hashmat");
  
  String new_timezone = server.arg("timezone");
  String new_latitude = server.arg("latitude");
  String new_longitude = server.arg("longitude");
  String new_autodst = server.arg("autodst");
  String new_updateserverhost = server.arg("updateserverhost");
  String new_updateserverport = server.arg("updateserverport");
  String new_updateserverpath = server.arg("updateserverpath");

  bool ssidChanged = strcmp(myprefs.ssid, new_ssid.c_str()) ||
    strcmp(myprefs.password, new_password.c_str());
  
  myprefs.set("ssid", new_ssid);
  myprefs.set("password", new_password);
  myprefs.set("comment", new_comment);
  myprefs.set("adminPassword", new_adminpw);
  myprefs.set("otaPassword", new_otapw);
  myprefs.set("hashMaterial", new_hashmat);

  myprefs.set("lat", new_latitude);
  myprefs.set("lon", new_longitude);
  myprefs.set("defaultTimeZone", new_timezone);
  myprefs.set("autoSetDST", new_autodst);

  myprefs.set("updateServerHost", new_updateserverhost);
  myprefs.set("updateServerPort", new_updateserverport);
  myprefs.set("updateServerPath", new_updateserverpath);
  
  myprefs.write();
  myprefs.read();
  ArduinoOTA.setPassword(myprefs.otaPassword);

  if (ssidChanged) {
    wifi.JoinNetwork();
  }
  
  // Redirect to /status to show the changes
  server.sendHeader("Location", String("/status"), true);
  server.send(302, "text/plain", "");
}

void setup()
{
  randomSeed(analogRead(A0));

  if(SPIFFS.begin())
    fsRunning = true;
  else
    fsRunning = false;

  myprefs.begin(NAME);
  myprefs.setDefaults();

  bool prefsOk = false;
  
  if (fsRunning) {
    prefsOk = myprefs.read();
  }

  wifi.begin(&myprefs, NAME);

  server.begin();
  tlog.begin();

  location = NULL;
  
  ledPanel.Init();

  ledPanel.setFadeMode(true);

  addTextToBackingStore("TetrisDisplay! IP ");
  addTextToBackingStore(WiFi.localIP().toString());
  addTextToBackingStore("      ");

  tetrisEngine.Init();
  snakeEngine.Init();

  server.on("/l", handleLeft);
  server.on("/r", handleRight);
  server.on("/rl", handleRotateLeft);
  server.on("/rr", handleRotateRight);
  server.on("/d", handleDrop);
  server.on("/s", handleStep);
  server.on("/init", handleInit);
  server.on("/tetris", handleTetris);
  server.on("/test", handleTest);
  server.on("/text", handleText);
  server.on("/status2", handleStatus); // override default behavior FIXME wound up using a new URI b/c I can't override default...
  server.on("/startclock", handleStartClock);
  server.on("/testclock", handleTestClock);
  server.on("/brightness", handleBrightness);
  server.on("/autobrightness", handleAutoBrightness);
  server.on("/color", handleColorWheel);
  server.on("/update", handleUpdate);
  server.on("/config2", handleConfig); // override default behavior FIXME
  server.on("/submit2", handleSubmit); // override default behavior FIXME
  server.on("/starttree", handleStartTree);
  server.on("/checkDownload", handleCheckDownload);

  Udp.begin(localPort);
  tcpserver.begin();

  MDNS.begin(myprefs.mdnsName);
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("tetris", "udp", localPort);
  MDNS.addService("tetris", "tcp", localPort);

  clockDriver = new TetrisClock(&ledPanel);
}

void handleChar(char c)
{
  switch (c) {
  case 'a': 
    if (currentMode == mode_tetris) 
      tetrisEngine.MoveLeft();
    else if (currentMode == mode_snake)
      snakeEngine.TurnLeft();
    else if (currentMode == mode_pickGame) {
      if (currentGameSelection == mode_tetris) {
	currentGameSelection = mode_snake;
      } else {
	currentGameSelection = mode_tetris;
      }
      pickGameTimeout = millis() + MENUTIMEOUT;
    }
    break;
  case 'd':
    if (currentMode == mode_tetris)
      tetrisEngine.MoveRight();
    else if (currentMode == mode_snake)
      snakeEngine.TurnRight();
    else if (currentMode = mode_pickGame) {
      currentMode = currentGameSelection;
      handleInit();
    }
    break;
  case 'q': 
    if (currentMode == mode_tetris)
      tetrisEngine.RotateLeft();
    break;
  case 'e':
    if (currentMode == mode_tetris)
      tetrisEngine.RotateRight();
    break;
  case '!':
    udpRunStarted = true; // We're playing tetris in real-time w/o a TCP connection
    break;
  case 's':
    if (currentMode == mode_tetris) {
      if (!tetrisEngine.Step()) {
	gameOver();
      }
      checkLines = true;
    } else if (currentMode == mode_snake){
      if (!snakeEngine.Step()) {
	gameOver();
      }
    }
    break;
  case ' ':
    if ((currentMode == mode_tetris) && !tetrisEngine.Drop()) {
      // restart
      gameOver();
      checkLines = true;
    }
    break;
  }
}

void handleUdp(int byteCount)
{
  if (byteCount < sizeof(packetBuffer)) {
    Udp.read(packetBuffer, byteCount);

    handleChar(packetBuffer[0]);
  }
}

void textLoop()
{
  static uint32_t nextMillis = 0;

  if (millis() >= nextMillis) {
    // Shift display 1 pixel
    for (int y=0; y<YSIZE-1; y++) {
      for (int x=0; x<XSIZE; x++) {
	ledPanel.SetLED(x, y, 
			ledPanel.GetLED(x,y+1));
      }
    }

    // Shift in 1 pixel of what's offscreen
    if (backingPixels.hasData()) {
      byte *p = backingPixels.consumeLine();
      for (int i=0; i<8; i++) {
	ledPanel.SetLED(i, 31, p[i] ? CRGB::White : CRGB::Black);
      }
    } else if (!backingText.hasData() && currentMode == mode_startup) {
      startClockMode();
    }

    // If there's text to be placed in the backing pixels buffer and
    // there's space, then do it
    if (backingText.hasData() && backingPixels.freeSpace() > CHAR_WIDTH+1) {
      addCharToBackingStore(backingText.consumeByte());
    }

    nextMillis = millis() + 100; // 250 was a little bit unbearably slow
  }
}

void addCharToBackingStore(char c)
{
  // Construct each column of pixels from the XPM data & push them on the backing pixel store
  for (uint8_t x=0; x<CHAR_WIDTH; x++) {
    uint8_t columnData = 0;
    for (uint8_t y=0; y<CHAR_HEIGHT; y++) {
      uint8_t d = pgm_read_byte(&font5x7_xpm[3+y][((c-' ')*CHAR_WIDTH)+x]);
      if (d == '.') {
	columnData |= 1<<y;
      }
    }
    addColumnToBackingStore(columnData);
  }
}

void addColumnToBackingStore(uint8_t data)
{
  // Don't allow overflow; just drop the excess data
  if (backingPixels.isFull())
    return;

  // If there's no data in the backing pixel buffer, then we want to
  // insert at index 0.
  byte storeData[NUM_ROWS];

 for (int y=0; y<NUM_ROWS; y++) {
   if (data & (1 << ((NUM_ROWS-1)-y))) {
     storeData[y] = 1;
   } else {
     storeData[y] = 0;
   }
 }

 backingPixels.addLine(storeData);
}

void loop() {
  MDNS.update();

  server.loop();
  wifi.loop();
  tlog.loop();

  int noBytes = Udp.parsePacket();
  if (noBytes) {
    handleUdp(noBytes);
    needsRefresh = true;
  }

  // cf https://forum.arduino.cc/index.php?topic=535898.0 for multiple
  // connections. As written here, a new connection will force the old
  // one to drop.
  //
  // Also, this won't accept a new connection while text is scrolling.
  if (tcpserver.hasClient() &&
      ( ( currentMode != mode_text ) ||
	( (!backingText.hasData()) &&
	  (!backingPixels.hasData()) ) )
      ) {
    tcpclient = tcpserver.available();
    tcpclient.print("Hello");
    tcpclient.flush();
    currentMode = mode_pickGame;
    pickGameTimeout = millis() + MENUTIMEOUT;
  }
  if (tcpclient && tcpclient.available() > 0) {
    char c = tcpclient.read();
    handleChar(c);
    needsRefresh = true;
  }

  clockDriver->loop();

  if (currentMode == mode_clock) {
    if (millis() >= nextTick) {	 
      if (clockRestarting) {
	if (millis() >= nextTimeUpdate) {
	  if (updateTime()) {
	    nextTimeUpdate = millis() + 60 * 10 * 1000; // Update the clock from NTP every 10 minutes
	  } // else it failed, and we'll try again immediately
	} else {
 	  clockDriver->startDisplay();
	}
	clockShowing = true;
	clockRestarting = false;
      }
	
      if (clockShowing) {
	unsigned long thisDelay = clockDriver->step();
	if (thisDelay == 0) {
	  // Done with the drawing; delay, then clear the screen
	  nextTick = millis() + 15 * 1000;
	  clockRestarting = false;
	  clockShowing = false;
	  colorWheelMode = true;
	} else if (thisDelay == 99999) { // FIXME: terrible constant
	  startTreeMode();
	} else {
	  nextTick = millis() + thisDelay;
	}
      } else {
	// We've finished showing the time. Blank for a while and then start over.
	ledPanel.clearByScrolling();
	colorWheelMode = false;
	nextTick = millis() + 45 * 1000;
	clockShowing = false;
	clockRestarting = true;
      }
    }
  }

  if ((currentMode == mode_tetris || currentMode == mode_snake) && 
      ((tcpclient && tcpclient.connected()) ||
       udpRunStarted)) {
    if (millis() >= nextTick) {
      if (currentMode == mode_tetris) {
	if (!tetrisEngine.Step()) {
	  gameOver();
	}
      } else {
	if (!snakeEngine.Step()) {
	  gameOver();
	}
      }
      if (currentMode == mode_tetris)
	checkLines = true;
      needsRefresh = true;
      int32_t nextDelay = 500;
      uint32_t curScore = (currentMode == mode_tetris) ? tetrisEngine.score() : snakeEngine.score();
      if (currentMode == mode_tetris) {
	nextDelay = 500 - ((curScore > 49 ? 49 : curScore)*10);
      } else {
	nextDelay -= curScore*5;
	if (nextDelay < 10) nextDelay = 10;
      }
      nextTick = millis() + nextDelay;
    }
  }

  if (currentMode == mode_tetris && needsRefresh) {
    if (tetrisEngine.changedPieceThisTurn()) {
      nextTick = millis() + 750;
    }

    if (checkLines) {
      uint8_t f = tetrisEngine.numFilledLines();
      if (f) {
	for (int c=0; c<4; c++) { // flash off-on off-on
	  for (int i=0; i<f; i++) { // for each solved line
	    // This line needs to flash
	    uint8_t l = tetrisEngine.lastFilledLineIndex(i);

	    for (int x=0; x<XSIZE; x++) {
	      ledPanel.SetLED(x, l, (c & 1) ? CRGB::White : CRGB::Black);
	    }
	  }
	  ledPanel.Update();
	  delay(150);
	}
      }
      // have to reset the drop timer to compensate for time that just elapsed
      if (tetrisEngine.changedPieceThisTurn()) {
	nextTick = millis() + 750;
      }
      checkLines = false;
    }
  }

  if ((currentMode == mode_tetris || currentMode == mode_snake) &&
      needsRefresh) {
    for (int y=0; y<YSIZE; y++) {
      for (int x=0; x<XSIZE; x++) {
	uint8_t sq = (currentMode == mode_tetris) ? tetrisEngine.GetSquare(x,y) : snakeEngine.GetSquare(x,y);
	CRGB outColor;
	switch (sq) {
	case 0:
	case ' ':
	  outColor = CHSV(0,0,0);
	  break;

	case '|':
	  outColor = CRGB::White;
	  break;
	case '*':
	  outColor = CHSV(HUE_ORANGE,255,255);
	  break;
	case 'z':
	  outColor = CHSV(HUE_YELLOW,255,255);
	  break;
	case 'Z':
	  outColor = CHSV(HUE_AQUA,255,255);
	  break;
	case 'L':
	case '.': // food in snake game
	  outColor = CHSV(HUE_BLUE,255,255);
	  break;
	case 'J':
	  outColor = CHSV(HUE_PURPLE,255,255);
	  break;
	case 'T':
	  outColor = CHSV(HUE_PINK,255,255);
	  break;
	default:
	  outColor = CHSV(HUE_GREEN,255,255);
	  break;
	}
	ledPanel.SetLED(x, y, outColor);
      }
    }
    needsRefresh = false;
  }

  if (currentMode == mode_text || currentMode == mode_startup) {
    textLoop();
  }

  if (currentMode == mode_pickGame) {
    // Draw the basic menu
    ledPanel.clear(true);

    // Tetris mode
    ledPanel.SetLED(2, 10, CHSV(HUE_BLUE,255,255));
    ledPanel.SetLED(3, 10, CHSV(HUE_BLUE,255,255));
    ledPanel.SetLED(4, 10, CHSV(HUE_BLUE,255,255));
    ledPanel.SetLED(3, 9, CHSV(HUE_BLUE,255,255));
    ledPanel.SetLED(4, 9, CHSV(HUE_YELLOW,255,255));
    ledPanel.SetLED(5, 9, CHSV(HUE_YELLOW,255,255));
    ledPanel.SetLED(5, 10, CHSV(HUE_YELLOW,255,255));
    ledPanel.SetLED(4, 8, CHSV(HUE_YELLOW,255,255));

    // Snake mode
    ledPanel.SetLED(2, 22, CHSV(HUE_GREEN,255,255));
    ledPanel.SetLED(3, 22, CHSV(HUE_GREEN,255,255));
    ledPanel.SetLED(4, 22, CHSV(HUE_GREEN,255,255));
    ledPanel.SetLED(6, 22, CHSV(HUE_BLUE,255,255));

    // Draw the selection
    uint8_t boxStartY = 0;
    if (currentGameSelection == mode_tetris) {
      boxStartY = 6;
    } else if (currentGameSelection == mode_snake) {
      boxStartY = 19;
    }
    for (int x=0; x<=7; x++) {
      for (int y=boxStartY; y<=boxStartY + 6; y++) {
	if (x==0 || x==7 || y==boxStartY || y==boxStartY+6)
	  ledPanel.SetLED(x,y,CHSV(HUE_RED,255,255));
      }
    }

    if (millis() >= pickGameTimeout) {
      // Timed out waiting for input; go back to the clock
      startClockMode();
    }
  }

  if (currentMode == mode_tree) {
    EVERY_N_MILLISECONDS(100) {
      handleTreeBlinkers();
    }
  }

  EVERY_N_MILLISECONDS(35) {
    if (colorWheelMode) {
      ledPanel.stepColorWheel();
    } else {
      ledPanel.Update();
    }
  }

#if 0
  static uint32_t nextAt = 0;
  static uint16_t pix = 0;
  if (millis() > nextAt) {
    if (pix > 0) {
      leds[pix-1] = CHSV(0,0,0);
    }
    leds[pix] = CHSV(180, 255, 255);
    pix++;

    nextAt = millis() + 500;
  }
#endif

#if 0
  static uint8_t hue = 0;

  for(int i = 0; i < NUM_LEDS; i++) {
    // Set the i'th led to red 
    leds[i] = CHSV(hue++, 255, 255);
    // Show the leds
  }
  hue++; // to make it off-by-one so it moves...

#endif
}

void gameOver() {
  currentMode = mode_text;

  backingText.clear();
  backingPixels.clear();

  addTextToBackingStore("  Game Over  ");
  if (tcpclient && tcpclient.connected()) {
    tcpclient.flush();
    tcpclient.stop();
  }
  char buf[25];
  sprintf(buf, "Score: %d     ", (currentMode == mode_tetris) ? tetrisEngine.score() : snakeEngine.score());
  addTextToBackingStore(buf);
}

bool usIsTodayDST(int8_t day, int8_t month, int8_t dow)
{
  if (month < 3 || month > 11) { return false; }
  if (month > 3 && month < 11) { return true; }
  int8_t previousSunday = day - dow;

  // March: DST is true on and after the second Sunday. Calculate when the first Sunday of this month way
  if (month == 3) {
    if (day <= 7) { return false; } // Obviously the first week is a miss
    int8_t firstSunday = ((day - dow)+1) % 7;
    int8_t secondSunday = firstSunday + 7;
    return (day >= secondSunday);
  }

  // November: we're only DST if we're before the first Sunday
  return previousSunday <= 0;
}

bool europeIsTodayDST(int8_t day, int8_t month, int8_t dow)
{
  if (month < 3 || month > 10)  return false; 
  if (month > 3 && month < 10)  return true; 

  int8_t previousSunday = day - dow;

  if (month == 3) return previousSunday >= 25;
  if (month == 10) return previousSunday < 25;

  return false; // notreached
}

void createNewTreeBlinker()
{
  int newY, newX;
  do {    
    newY = random(110)%11; // 0-10, inclusive
    if (newY < 2) {
      newX = (random(100)%2) + 3;
    } else if (newY < 6) {
      newX = (random(100)%4) + 2;
    } else if (newY < 9) {
      newX = (random(102)%6) + 1;
    } else {
      newX = random(104)%8;
    }
    newY += 16; // offset to the top of the tree
  } while ((CRGB) ledPanel.GetLED(newX, newY) != (CRGB) CRGB::Green);
  
  treeBlinkers[currentTreeBlinkers].x = newX;
  treeBlinkers[currentTreeBlinkers].y = newY;
  treeBlinkerTime[currentTreeBlinkers] = millis();
  treeBlinkerState[currentTreeBlinkers] = 0;
  treeBlinkerMax[currentTreeBlinkers] = 8 + random(4);
  currentTreeBlinkers++;
  ledPanel.SetLED(newX, newY, CRGB::Black);
}

void handleTreeBlinkers()
{
  if (--treeCounter == 0) {
    // Done blinking - go back to clock mode

    ledPanel.clearByScrolling();

    startClockMode();

    colorWheelMode = false;
    nextTick = millis() + 45 * 1000;
    clockShowing = false;
    clockRestarting = true;

    return;
  }

  if (currentTreeBlinkers < MAX_TREE_BLINKERS) {
    createNewTreeBlinker();
  }

  // Loop backwards so we can easily prune the blinker arrays
  for (int i=currentTreeBlinkers-1; i>=0; i--) {
    int x = treeBlinkers[i].x;
    int y = treeBlinkers[i].y;
    if (millis() >= treeBlinkerTime[i]) {
      treeBlinkerTime[i] = millis() + 250 + random(100);
      treeBlinkerState[i]++;
      if (treeBlinkerState[i] != treeBlinkerMax[i]) {
	ledPanel.SetLED(x, y, (random(100) >= 50) ? CRGB::Blue : CRGB::White);
      } else {
	ledPanel.SetLED(x, y, CRGB::Green);
	// Remove it from the arrays. Move everything down to overwrite this.
	for (int j=i; j<MAX_TREE_BLINKERS-1; j++) {
	  treeBlinkers[j] = treeBlinkers[j+1];
	  treeBlinkerTime[j] = treeBlinkerTime[j+1];
	  treeBlinkerState[j] = treeBlinkerState[j+1];
	  treeBlinkerMax[i] = treeBlinkerMax[i+1];
	}
	currentTreeBlinkers--;
      }
    }
  }
}

void drawTree()
{
  // trunk: y=28..30 x=3..4
  // tree: 
  //   y=25..27 x=0..7
  //   y=22..24 x=1..6
  //   y=18..21 x=2..5
  //   y=16..17 x=3..4

  for (int y=28;y<=30;y++) {
    for (int x=3;x<=4;x++) {
      ledPanel.SetLED(x, y, CRGB::Brown);
    }
  }
  for (int y=25; y<=27; y++) {
    for (int x=0; x<=7; x++) {
      ledPanel.SetLED(x, y, CRGB::Green);
    }
  }

  for (int y=22; y<=24; y++) {
    for (int x=1; x<=6; x++) {
      ledPanel.SetLED(x, y, CRGB::Green);
    }
  }

  for (int y=18; y<=21; y++) {
    for (int x=2; x<=5; x++) {
      ledPanel.SetLED(x, y, CRGB::Green);
    }
  }

  for (int y=16; y<=17; y++) {
    for (int x=3; x<=4; x++) {
      ledPanel.SetLED(x, y, CRGB::Green);
    }
  }
}

void checkForUpdate(ESP8266WebServer *server)
{
  ESPhttpUpdate.rebootOnUpdate(true);
  WiFiClient client;
  // This can take a URL instead of broken out pieces. Use that instead maybe?
  t_httpUpdate_return ret = ESPhttpUpdate.update(client,
                                                 myprefs.updateServerHost,
                                                 myprefs.updateServerPort,
                                                 myprefs.updateServerPath);
  
  switch (ret) {
  case HTTP_UPDATE_FAILED:
    server->sendContent("Update failed: error ");
    server->sendContent(String(ESPhttpUpdate.getLastError()));
    server->sendContent(": ");
    server->sendContent(ESPhttpUpdate.getLastErrorString());
    break;
  case HTTP_UPDATE_NO_UPDATES:
    server->sendContent("No update needed.");
    break;
  case HTTP_UPDATE_OK:
    server->sendContent("Update ok.");
    // FIXME: should we force a reboot here? Or does that already happen?
    break;
  }
}

void handleCheckDownload()
{
  if (!server.isAuthenticated()) {
    return;
  }

  server.SendHeader();
  checkForUpdate(&server);
  server.SendFooter();
}
