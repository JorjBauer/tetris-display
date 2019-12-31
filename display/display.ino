#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "LEDAbstraction.h"
#include "RingPixels.h"
#include <RingBuffer.h>

#include <ESP8266WebServer.h>
#include <WiFiServer.h>

#include "tetris.h"
#include "tetris-clock.h"
#include "snake.h"

#include <NTPClient.h>
#include <SunriseCalc.h> // from https://github.com/JorjBauer/SunriseCalc
#include <TimeLib.h>

#include <FS.h>

#include "font5x7.h"
#define CHAR_WIDTH 6
#define CHAR_HEIGHT 7

LEDAbstraction ledPanel;

ESP8266WebServer server(80); //HTTP server on port 80
WiFiUDP Udp; // UDP server
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

bool fsRunning = false; // set to true when SPIFFS is set up

uint32_t nextTick = 0;
uint32_t nextTimeUpdate = 0;
int32_t sunriseAt = -1; // minutes since midnight, or -1 for "none"
int32_t sunsetAt = -1;
uint8_t sunriseHours;
uint8_t sunriseMinutes;
uint8_t sunsetHours;
uint8_t sunsetMinutes;

enum {
  DST_NONE = 0,
  DST_USA  = 1,
  DST_EU   = 2
};

// Preferences, as stored in SPIFFS. Defaults overridden by whatever's
// read from the config.
char ssid[50] = "";
char password[50] = "";
float lat = 40;
float lon = -75;
int8_t defaultTimeZone = -5;
int8_t autoSetDST = DST_USA;
bool staMode = true;
// End preferences

SunriseCalc *location = NULL;

time_t lastNtpDate;
bool isDST = false; // always default to false; will set to true
		    // during calculations if autoSetDST is set
uint8_t lastUpdatePhase; // for debugging

TetrisClock *clockDriver = NULL;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
bool clockShowing = false;
bool clockRestarting = false;

bool colorWheelMode = false;

bool autoBrightness = true;

int32_t lastCorrection = 0; // When we NTP, save the # of seconds drifted

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

void handleRoot() {
 server.send(200, "text/html", 
"<!DOCTYPE html><html>"
"<h1>Welcome to the Tetris Display!</h1>"
"<p>Embedded pages:</p>"
"<ul>"
"<li>/l, /r, /rl, /rr, /d, /s: pages for tetris (left, right, rotate left/right, drop, step)</li>"
"<li><a href='/init'>/init</a>: pick which game to play</li>"
"<li><a href='/tetris'>/tetris</a>: play tetris using in-browser controls</li>"
"<li><a href='/status'>/status</a>: show current status</li>"
"<li><a href='/test'>/test</a>: LED test mode</li>"
"<li><a href='/text?s=hi'>/text</a>: GET with argument 's' to display text</li>"
"<li><a href='/reset'>/reset</a>: reboots the display</li>"
"<li><a href='/startclock'>/startclock</a>: start running a tetris-style clock</li>"
"<li><a href='/testclock?t=0123'>/testclock</a>: test clock display with argument 't'</li>"
"<li><a href='/starttree'>/starttree</a>: display holiday tree</li>"
"<li><a href='/color'>/color</a>: toggle color wheel mode on/off</li>"
"<li><a href='/brightness?b=40'>/brightness</a>: GET with argument 'b' to set brightness (1-255)</li>"
"<li><a href='/autobrightness'>/autobrightness</a>: toggle auto-brightness on or off</li>"
"<li><a href='/file?path=/tetris.cfg'>/file</a>: retrieve file from SPIFFS with name in argument 'path'</li>"
"<li><a href='/config'>/config</a>: change configuration (SSID, password, time zone)</li>"
"</ul>"
"</p>"
"<p>This also listens on port 8267 (udp and tcp). When a TCP session is active, then the game is really in 'play' mode; pieces drop automatically, increasing in speed over time. Commands over UDP do play the game but the player has to make time pass (with 's' for single steps or (space) for a drop) unless you first send a '!'. When the game is running, it keeps going unless the TCP session drops (there is no way to stop after '!').</p>"
"<p>Performing a GET at the URLs /l, /r, et al. works like the UDP connection - but the overhead of handling them as HTTP requests means they are very laggy. The in-game browser version must use these via AJAX since a browser can't open arbitrary TCP/UDP connections.</p>"
"<p>The actual game is best played with a controller that opens a TCP connection to start the game going, and then sends individual commands via UDP.</p>"
"</html>"
);
}

void handleStatus() {
  char buf[100];
  sprintf(buf, "%d milliseconds", nextTimeUpdate - millis());

  uint32_t ct = clockDriver->curTime();
  uint8_t hh = (ct >> 16);
  uint8_t mm = (ct >> 8) & 0xFF;
  uint8_t ss = (ct & 0xFF);

  String status = String("<html><div>SSID: ") +
    String(ssid) + 
    String("</div><div>Password: ") + 
    String(password) + 
    String("</div><div>Lat/lon: ") +
    String(lat) +
    String(",") +
    String(lon) +
    String("</div><div>Time zone: ")+
    String(defaultTimeZone) +
    String("</div><div>Auto-set DST: ") +
    String(autoSetDST) + 
    String("</div><div>STA mode: ") +
    String(staMode ? "true" : "false") + 
    String("</div><div>TCP client: ") +
    String( (tcpclient && tcpclient.connected()) ? 
	    "Connected" : 
	    "Not connected" ) +
    String("</div><div>Mode: ") + 
    String( (currentMode == mode_text) ? "mode_text" : 
	    (currentMode == mode_tetris) ? "mode_tetris" :
	    (currentMode == mode_clock) ? "mode_clock" :
	    (currentMode == mode_startup) ? "mode_startup" : 
	    (currentMode == mode_pickGame) ? "mode_pickGame" :
	    (currentMode == mode_snake) ? "mode_snake" :
	    (currentMode == mode_tree) ? "mode_tree" :
	    "unknown mode") +
    String("</div><div>Score: ") + 
    String(currentMode == mode_tetris ? tetrisEngine.score() : snakeEngine.score()) +  // FIXME: might not be either mode
    String("</div><div>Clock showing: ") + 
    String(clockShowing ? "true" : "false") + 
    String("</div><div>Clock restarting: ") +
    String(clockRestarting ? "true" : "false") +
    String("</div><div>Next NTP update: ") +
    String(buf) + 
    String("</div><div>CurTime: ") +
    String(hh) + String(":") + String(mm) + String(":") + String(ss) +
    String("</div><div>Last correction: ") + 
    String(lastCorrection) +
    String("</div><div>Auto-brightness: ") +
    String(autoBrightness ? "true" : "false") +
    String("</div><div>Sunrise at: ") +
    String(sunriseHours) + String(":") + String(sunriseMinutes) +
    String("</div><div>Sunset at: ") +
    String(sunsetHours) + String(":") + String(sunsetMinutes) +
    String("</div><div>Is today DST: ") + 
    String(isDST ? "yes" : "no") +
    String("</div><div>Last Update Phase: ") + 
    String(lastUpdatePhase) +
    String("</div></html>");
    
  server.send(200, "text/html", status.c_str());
}


void handleTetris() {
 server.send(200, "text/html", 
"<!DOCTYPE html><html>"
"<head>"
"<script src='http://ajax.googleapis.com/ajax/libs/jquery/1.3.1/jquery.min.js' type='text/javascript'></script>"
"<script type='text/javascript'>"
"function keyDownHandler(event) { var keyPressed =String.fromCharCode(event.keyCode);if (keyPressed=='A') {$.ajax({ url: '/l', async: false });}if (keyPressed=='D') {$.ajax({ url: '/r', async: false });}if (keyPressed=='Q') {$.ajax({ url: '/rl', async: false });}if (keyPressed=='E') {$.ajax({ url: '/rr', async: false });}if (keyPressed=='S') {$.ajax({ url: '/s', async: false });}if (keyPressed==' ') {$.ajax({ url: '/d', async: false });}}"
"document.addEventListener('keydown',keyDownHandler, false);"
"document.addEventListener('keyup',keyUpHandler, false);"
"</script></head><body><center><h1>Tetris</h1></center><p>Keys: <ul><li>Left and right: <b>a</b> and <b>d</b></li><li>Rotate: <b>q</b> and <b>e</b></li><li>Step: <b>s</b></li><li>Drop: <b>space</b></li></ul></p></body></html>"
);
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
      ledPanel.SetLED(x,y,colors[random(7)]);
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
  lastUpdatePhase = 0;
  ledPanel.clear();

  // necessary to set this before running timeClient.update()
  timeClient.setTimeOffset(defaultTimeZone * 3600 + (isDST ? 3600 : 0));

  if (!timeClient.update()) {
    // The NTP update failed; we'll try again next cycle
    lastUpdatePhase = 1;
    return false;
  }

  lastUpdatePhase = 2;
  uint8_t curMinute = timeClient.getMinutes();
  uint8_t curHour = timeClient.getHours();
  uint8_t curSecond = timeClient.getSeconds();

  // Deal with the time first: DST calculation, then set the time!
  uint32_t prevTime = clockDriver->setTime(curHour, curMinute, curSecond);

  uint32_t epochTime = timeClient.getEpochTime();
  lastNtpDate = epochTime;
  if (autoSetDST != DST_NONE) {
    lastUpdatePhase = 3;
    bool potentialDST = (autoSetDST == DST_USA) ?
      usIsTodayDST(day(lastNtpDate), month(lastNtpDate), dayOfWeek(lastNtpDate)) :
      europeIsTodayDST(day(lastNtpDate), month(lastNtpDate), dayOfWeek(lastNtpDate));

    if (potentialDST != isDST) {
      // DST flag changed. We can offset what timeClient returned, or we can take 
      // the lazy way out and just re-poll after changing the zone info...
      if (curHour >= 2) {
	isDST = !isDST;
	timeClient.setTimeOffset(defaultTimeZone * 3600 + (isDST ? 3600 : 0));
	
	timeClient.update(); // ... Lazy!
	curMinute = timeClient.getMinutes();
	curHour = timeClient.getHours();
	curSecond = timeClient.getSeconds();
	prevTime = clockDriver->setTime(curHour, curMinute, curSecond);
	epochTime = timeClient.getEpochTime();
	lastNtpDate = epochTime;
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
    lastUpdatePhase = 4;
    if (!location) {
      location = new SunriseCalc(lat, lon, defaultTimeZone);
    }
    location->date(year(lastNtpDate), month(lastNtpDate), day(lastNtpDate), isDST);

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
  clockDriver->setTime(hh, mm, 0);

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

void handleReset() {
  server.send(200, "text/html", "Okay, restarting");

  ESP.restart();
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

void handleFile() {
  String path = server.arg("path");
  String contentType = getContentType(path);
  if (SPIFFS.exists(path)) {
    fs::File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
  } else {
    server.send(404, "text/html", "File not found");
  }
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
	      "<div><label for='lat'>Latitude:</label>"
	      "<input type='text' id='lat' name='latitude' /></div>"
	      "<div><label for='lon'>Longitude:</label>"
	      "<input type='text' id='lon' name='longitude' /></div>"
	      "<div><label for='tz'>Time zone:</label>"
	      "<input type='text' id='tz' name='timezone' /></div>"
	      "<div><p>Auto-set DST:</p>"
	      "<p><input type='radio' name='autodst' value='US' checked='checked'><label for='US'>Yes, US DST rules</label><br />"
	      "<input type='radio' name='autodst' value='EU'><label for='EU'>Yes, Europe DST rules</label><br />"
	      "<input type='radio' name='autodst' value='NO'><label for='NO'>No, disable DST</label></p></div>"
	      "<div><input type='submit' value='Save' /></div>"
	      "</form>"
	      "</body></html");
}

void handleSubmit()
{
  String new_ssid = server.arg("ssid");
  String new_password = server.arg("password");
  String new_timezone = server.arg("timezone");
  String new_latitude = server.arg("latitude");
  String new_longitude = server.arg("longitude");
  String new_autodst = server.arg("autodst");

  strncpy(ssid, new_ssid.c_str(), 50);
  strncpy(password, new_password.c_str(), 50);
  lat = new_latitude.toFloat();
  lon = new_longitude.toFloat();
  defaultTimeZone = new_timezone.toInt();

  if (new_autodst.startsWith("E")) {
    autoSetDST = DST_EU;
  } else if (new_autodst.startsWith("U")) {
    autoSetDST = DST_USA;
  } else {
    autoSetDST = DST_NONE;
  }

  if (ssid[0])
    staMode = true;
  else
    staMode = false;

  writePrefs();

  // Redirect to /status to show the changes
  server.sendHeader("Location", String("/status"), true);
  server.send(302, "text/plain", "");
}

void processConfig(const char *lhs, const char *rhs)
{
  if (!strcmp(lhs, "ssid")) {
    strncpy(ssid, (char *)rhs, 50);
  }
  else if (!strcmp(lhs, "password")) {
    strncpy(password, (char *)rhs, 50);
  }
  else if (!strcmp(lhs, "timezone")) {
    defaultTimeZone = ((String)rhs).toInt();
  }
  else if (!strcmp(lhs, "autodst")) {
    autoSetDST = 
      (rhs[0] == 'u' || rhs[0] == 'U') ? DST_USA :
      (rhs[0] == 'e' || rhs[0] == 'E') ? DST_EU :
      DST_NONE;
  }
  else if (!strcmp(lhs, "stamode")) {
    staMode = (rhs[0] == 't' || rhs[0] == 'y') ? true : false;
  }
  else if (!strcmp(lhs, "lat")) {
    lat = ((String)rhs).toFloat();
  }
  else if (!strcmp(lhs, "lon")) {
    lon = ((String)rhs).toFloat();
  }
}

void writePrefs()
{
  fs::File f = SPIFFS.open("/tetris.cfg", "w");
  f.println("# Submitted config");
  f.print("ssid=");
  f.println(ssid);
  f.print("password=");
  f.println(password);
  f.print("timezone=");
  f.println(defaultTimeZone);
  f.print("autodst=");
  f.println((autoSetDST == DST_NONE) ? "n" :
	    (autoSetDST == DST_USA) ? "u" :
	    "e");
  f.print("stamode=");
  f.println(staMode ? "t" : "f");
  f.print("lat=");
  f.println(lat);
  f.print("lon=");
  f.println(lon);
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

void setup()
{
  randomSeed(analogRead(A0));

  if(SPIFFS.begin())
    fsRunning = true;
  else
    fsRunning = false;

  if (fsRunning) {
    // Try to load the config file.
    fs::File f = SPIFFS.open("/tetris.cfg", "r");
    if (!f) {
      // No config found; re-format SPIFFS and create it.
      if (!SPIFFS.format()) {
	// Not sure what happened, but we can't use the filesystem.
	fsRunning = false;
      } else {
	f = SPIFFS.open("/tetris.cfg", "w");
	f.println("# Blank auto-generated config");
	f.close();
	f = SPIFFS.open("/tetris.cfg", "r");
	if (!f) {
	  fsRunning = false;
	}
      }
    }

    if (f) { // re-testing for 'f' because we might have fallen through the format above
      readPrefs(f);
    }
  }

  if (!ssid[0]) {
    staMode = false; // No SSID, so force reconfiguration of STA mode
  }

  // We start up in SoftAP mode, which the controller uses. This gives
  // us a permanent future-proof version where the controller and
  // clock will work, regardless of whether or not there's an Internet
  // connection available.

  WiFi.softAP("TetrisDisplay");
  // Set the IP address and info for SoftAP mode. Note this is also
  // the default IP (192.168.4.1), but better to be explicit...
  IPAddress local_IP(192,168,4,1);
  IPAddress gateway(192,168,4,1);
  IPAddress subnet(255,255,255,0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  // If we have an SSID/password configured, we'll also try to connect
  // to the internet for NTP updates.

  if (staMode) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    uint8_t count=0;
    while (count < 5 && WiFi.waitForConnectResult() != WL_CONNECTED) {
      count++;
      delay(5000);
      ESP.restart();
    }
    if (count >= 5) {
      // Failed to connect to WiFi; force re-configuration of STA mode
      staMode = false;
    }
  }

  location = NULL;
  
#if 0
  // Debugging: dump all the SSIDs we see to the SPIFFS file /ssids.txt
  fs::File tmpf = SPIFFS.open("/ssids.txt", "w");
  int n = WiFi.scanNetworks();
  
  if (n == 0)
    tmpf.println("No networks found");
  else {
    tmpf.print(n);
    tmpf.println(" networks found");
    for (int i = 0; i < n; ++i)	{
      // Print SSID and RSSI for each network found
      tmpf.print(i + 1);  //Sr. No
      tmpf.print(": ");
      tmpf.print(WiFi.SSID(i)); //SSID
      tmpf.print(" (");
      tmpf.print(WiFi.RSSI(i)); //Signal Strength
      tmpf.print(") MAC:");
      tmpf.print(WiFi.BSSIDstr(i));
      tmpf.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" Unsecured":" Secured");
    }
  }
  tmpf.close();
#endif

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  String name = String("tetris-display-") + ESP.getChipId();
  ArduinoOTA.setHostname(name.c_str());

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.begin();

  ledPanel.Init();

  ledPanel.setFadeMode(true);

  addTextToBackingStore("TetrisDisplay! IP ");
  addTextToBackingStore(WiFi.localIP().toString());
  addTextToBackingStore("      ");

#if 0
  uint32_t free = system_get_free_heap_size();
  char buf[25];
  sprintf(buf, " - RAM: %ld", free);
  addTextToBackingStore(buf);
#endif

  tetrisEngine.Init();
  snakeEngine.Init();

  server.on("/", handleRoot);
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
  server.on("/status", handleStatus);
  server.on("/startclock", handleStartClock);
  server.on("/testclock", handleTestClock);
  server.on("/brightness", handleBrightness);
  server.on("/autobrightness", handleAutoBrightness);
  server.on("/reset", handleReset);
  server.on("/color", handleColorWheel);
  server.on("/update", handleUpdate);
  server.on("/file", handleFile);
  server.on("/config", handleConfig);
  server.on("/submit", handleSubmit);
  server.on("/starttree", handleStartTree);

  server.begin();

  Udp.begin(localPort);
  tcpserver.begin();

  MDNS.begin(name.c_str());
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("tetris", "udp", localPort);
  MDNS.addService("tetris", "tcp", localPort);

  timeClient.begin();

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
      if (staMode) {
	startClockMode();
      } else {
	// We're not on the Internet, so don't go right in to clock mode
	currentMode = mode_text;

	ledPanel.setFadeMode(true);
	addTextToBackingStore("Configure at http://192.168.4.1/config");
      }
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
  ArduinoOTA.handle();
  MDNS.update();

  server.handleClient();

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
    handleTreeBlinkers();
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

  // March: DST is true if the previous Sunday is on/after the 8th
  if (month == 3) { return previousSunday >= 8; }

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
    newY = random(10); // 0-10, inclusive
    if (newY < 2) {
      newX = random(1) + 3;
    } else if (newY < 6) {
      newX = random(3) + 2;
    } else if (newY < 9) {
      newX = random(5) + 1;
    } else {
      newX = random(7);
    }
    newY += 16; // offset to the top of the tree
  } while ((CRGB) ledPanel.GetLED(newX, newY) != (CRGB) CRGB::Green);
  
  treeBlinkers[currentTreeBlinkers].x = newX;
  treeBlinkers[currentTreeBlinkers].y = newY;
  treeBlinkerTime[currentTreeBlinkers] = millis();
  treeBlinkerState[currentTreeBlinkers] = 0;
  treeBlinkerMax[currentTreeBlinkers] = 10 + random(4);
  currentTreeBlinkers++;
  ledPanel.SetLED(newX, newY, CRGB::Black);
}

void handleTreeBlinkers()
{
  if (currentTreeBlinkers < MAX_TREE_BLINKERS) {
    createNewTreeBlinker();
  }

  // Loop backwards so we can easily prune the blinker arrays
  for (int i=currentTreeBlinkers-1; i>=0; i--) {
    int x = treeBlinkers[i].x;
    int y = treeBlinkers[i].y;
    if (millis() >= treeBlinkerTime[i]) {
      treeBlinkerTime[i] = millis() + 250;
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
