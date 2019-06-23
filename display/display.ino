#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "LEDAbstraction.h"
#include "font_data.h"
#include "RingPixels.h"
#include <RingBuffer.h>

#include <ESP8266WebServer.h>
#include <WiFiServer.h>

#include "tetris.h"
#include "tetris-clock.h"

#include <NTPClient.h>
#include <Dusk2Dawn.h> // FIXME: This is a bad library, replace it
#include <TimeLib.h>

#include <FS.h>

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

Tetris engine;
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

// This SSID/password is read in from the config file in SPIFFS.
char ssid[50] = "";
char password[50] = "";

// *** FIXME: these are all prefs
int8_t defaultTimeZone = -5; // FIXME: how to set this? ***
bool autoSetDST = true;      // FIXME: another user pref to set ***
Dusk2Dawn wyncote(40.091983, -75.146454, defaultTimeZone); // *** FIXME - pref for location?
bool staMode = true; // station, or infrastructure?
// *** end prefs

time_t lastNtpDate;
bool isDST = false; // always default to false; will set to true
		    // during calculations if autoSetDST is true

TetrisClock *clockDriver = NULL;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
bool clockShowing = false;
bool clockRestarting = false;

bool colorWheelMode = false;

bool autoBrightness = true;

int32_t lastCorrection = 0; // When we NTP, save the # of seconds drifted

enum {
  mode_text    = 0,
  mode_tetris  = 1,
  mode_clock   = 2,
  mode_startup = 3
};

uint8_t currentMode = mode_startup;

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
"<li><a href='/init'>/init</a>: restart Tetris</li>"
"<li><a href='/tetris'>/tetris</a>: play tetris using in-browser controls</li>"
"<li><a href='/status'>/status</a>: show game status</li>"
"<li><a href='/test'>/test</a>: LED test mode</li>"
"<li><a href='/text?s=hi'>/text</a>: GET with argument 's' to display text</li>"
"<li><a href='/reset'>/reset</a>: reboots the display</li>"
"<li><a href='/startclock'>/startclock</a>: start running a tetris-style clock</li>"
"<li><a href='/testclock?t=0123'>/testclock</a>: test clock display with argument 't'</li>"
"<li><a href='/color'>/color</a>: toggle color wheel mode on/off</li>"
"<li><a href='/brightness?b=40'>/brightness</a>: GET with argument 'b' to set brightness (1-255)</li>"
"<li><a href='/autobrightness'>/autobrightness</a>: toggle auto-brightness on or off</li>"
"<li><a href='/file?path=/tetris.cfg'>/file</a>: retrieve file from SPIFFS with name in argument 'path'</li>"
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
    String("</div><div>TCP client: ") +
    String( (tcpclient && tcpclient.connected()) ? 
	    "Connected" : 
	    "Not connected" ) +
    String("</div><div>Score: ") + 
    String(engine.score()) + 
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
    String("</div></html>");
    
  server.send(200, "text/html", status.c_str());
}


void handleTetris() {
 server.send(200, "text/html", 
"<!DOCTYPE html><html>"
"<head>"
"<script src='http://ajax.googleapis.com/ajax/libs/jquery/1.3.1/jquery.min.js' type='text/javascript'>\</script>"
"<script type='text/javascript'>"
"function keyDownHandler(event) { var keyPressed =String.fromCharCode(event.keyCode);if (keyPressed=='A') {$.ajax({ url: '/l', async: false });}if (keyPressed=='D') {$.ajax({ url: '/r', async: false });}if (keyPressed=='Q') {$.ajax({ url: '/rl', async: false });}if (keyPressed=='E') {$.ajax({ url: '/rr', async: false });}if (keyPressed=='S') {$.ajax({ url: '/s', async: false });}if (keyPressed==' ') {$.ajax({ url: '/d', async: false });}}"
"document.addEventListener('keydown',keyDownHandler, false);"
"document.addEventListener('keyup',keyUpHandler, false);"
"</script></head><body><center><h1>Tetris</h1></center><p>Keys: <ul><li>Left and right: <b>a</b> and <b>d</b></li><li>Rotate: <b>q</b> and <b>e</b></li><li>Step: <b>s</b></li><li>Drop: <b>space</b></li></ul></p></body></html>"
);
}

void handleLeft() {
  engine.MoveLeft();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleRight() {
  engine.MoveRight();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleRotateLeft() {
  engine.RotateLeft();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleRotateRight() {
  engine.RotateRight();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleStep() {
  engine.Step();
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleDrop() {
  if (!engine.Drop()) {
    gameOver();
  }
  String s = "ok";
  server.send(200, "text/html", s);
  needsRefresh = true;

  running = true;
}

void handleRestart() {
  currentMode = mode_tetris;
  engine.Init();
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
  ledPanel.clear();

  // necessary to set this before running timeClient.update()
  timeClient.setTimeOffset(defaultTimeZone * 3600 + (isDST ? 3600 : 0));

  if (!timeClient.update()) {
    // The NTP update failed; we'll try again next cycle
    return false;
  }

  uint8_t curMinute = timeClient.getMinutes();
  uint8_t curHour = timeClient.getHours();
  uint8_t curSecond = timeClient.getSeconds();

  // Deal with the time first: DST calculation, then set the time!
  uint32_t prevTime = clockDriver->setTime(curHour, curMinute, curSecond);

  uint32_t epochTime = timeClient.getEpochTime();
  lastNtpDate = epochTime;
  if (autoSetDST) {
    if (usIsTodayDST(day(lastNtpDate), month(lastNtpDate), dayOfWeek(lastNtpDate)) != isDST) {
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
    sunriseAt = wyncote.sunrise(year(lastNtpDate), month(lastNtpDate), day(lastNtpDate), isDST);
    sunsetAt = wyncote.sunset(year(lastNtpDate), month(lastNtpDate), day(lastNtpDate), isDST);
    
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
      ledPanel.setBrightness(1);
    }
  }

  return true;
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

void processConfig(const char *lhs, const char *rhs)
{
  if (!strcmp(lhs, "ssid")) {
    strncpy(ssid, (char *)rhs, 50);
  }
  else if (!strcmp(lhs, "password")) {
    strncpy(password, (char *)rhs, 50);
  }
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
	f.println("# Config version 1");
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

  // We *also* start up in SoftAP mode, which the controller
  // uses. This gives us a permanent future-proof version where the
  // controller and clock will work, regardless of whether or not
  // there's an Internet connection available.

  WiFi.softAP("TetrisDisplay");
  // Set the IP address and info for SoftAP mode. Note this is also
  // the default IP (192.168.4.1), but better to be explicit...
  IPAddress local_IP(192,168,4,1);
  IPAddress gateway(192,168,4,1);
  IPAddress subnet(255,255,255,0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  
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

  engine.Init();

  server.on("/", handleRoot);
  server.on("/l", handleLeft);
  server.on("/r", handleRight);
  server.on("/rl", handleRotateLeft);
  server.on("/rr", handleRotateRight);
  server.on("/d", handleDrop);
  server.on("/s", handleStep);
  server.on("/init", handleRestart);
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

  server.begin();

  Udp.begin(localPort);
  tcpserver.begin();

  MDNS.addService("tetris", "tcp", localPort);
  MDNS.addService("tetris", "udp", localPort);
  MDNS.addService("http", "tcp", 80);

  timeClient.begin();

  clockDriver = new TetrisClock(&ledPanel);
}

// x should be a multiple of 8
void displayCharAt(unsigned char c, int x)
{
  // The display pixels are in a SNAKE PATTERN on the display: 0 is
  // top-left, down to #7; then over to #8, up to #15; over to #16,
  // down to #23... and so on. So in EVEN columns, it's top-to-bottom
  // and in ODD columns it's bottom-to-top.

  // The font data is in columns, where the high bit is the bottom of
  // each column.

  for (int col=0; col<CHAR_WIDTH; col++) {

    // overflow prevention
    if (x+col >= NUM_COLS)
      continue;
    
    uint8_t colOfPixels = pgm_read_byte(&charData[c-32][col]);
    for (int pix=0; pix<8; pix++) {
      uint8_t bit = colOfPixels & (1 << pix); // top-to-bottom

      ledPanel.SetLED(7-pix, x+col, bit ? CHSV(0,255,255) : CHSV(0,0,0));
    }
  }
}

void handleChar(char c)
{
  switch (c) {
  case 'a': 
    engine.MoveLeft();
    break;
  case 'd':
    engine.MoveRight();
    break;
  case 'q': 
    engine.RotateLeft();
    break;
  case 'e':
    engine.RotateRight();
    break;
  case '!':
    udpRunStarted = true; // We're playing tetris in real-time w/o a TCP connection
    break;
  case 's':
    if (!engine.Step()) {
      gameOver();
    }
    checkLines = true;
    break;
  case ' ':
    if (!engine.Drop()) {
      // restart
      gameOver();
    }
    checkLines = true;
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


void displayText(const char *s)
{
  int x=0;
  while (*s) {
    displayCharAt(*s, x);
    x += CHAR_WIDTH+1;
    s++;
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
      // ***
      if (staMode) {
	startClockMode();
      } else {
	// We're not on the Internet, so don't go right in to clock mode
	currentMode = mode_text;

	ledPanel.setFadeMode(true);
	addTextToBackingStore("SoftAP mode");
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
  for (int i=0; i<CHAR_WIDTH; i++) {
    uint8_t d = pgm_read_byte(&(charData[c-' '][i]));
    addColumnToBackingStore(d);
  }
  addColumnToBackingStore(0);
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
    handleRestart();
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

  if (currentMode == mode_tetris && 
      ((tcpclient && tcpclient.connected()) ||
       udpRunStarted)) {
    if (millis() >= nextTick) {
      if (!engine.Step()) {
	gameOver();
      }
      checkLines = true;
      needsRefresh = true;
      int32_t nextDelay = 500;
      uint32_t curScore = engine.score();
      nextDelay = 500 - ((curScore > 49 ? 49 : curScore)*10);
      nextTick = millis() + nextDelay;
    }
  }

  if (currentMode == mode_tetris && needsRefresh) {
    if (engine.changedPieceThisTurn()) {
      nextTick = millis() + 750;
    }

    if (checkLines) {
      uint8_t f = engine.numFilledLines();
      if (f) {
	for (int c=0; c<4; c++) { // flash off-on off-on
	  for (int i=0; i<f; i++) { // for each solved line
	    // This line needs to flash
	    uint8_t l = engine.lastFilledLineIndex(i);

	    for (int x=0; x<XSIZE; x++) {
	      ledPanel.SetLED(x, l, (c & 1) ? CRGB::White : CRGB::Black);
	    }
	  }
	  ledPanel.Update();
	  delay(150);
	}
      }
      // have to reset the drop timer to compensate for time that just elapsed
      if (engine.changedPieceThisTurn()) {
	nextTick = millis() + 750;
      }
      checkLines = false;
    }

    for (int y=0; y<YSIZE; y++) {
      for (int x=0; x<XSIZE; x++) {
	uint8_t sq = engine.GetSquare(x,y);
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
  sprintf(buf, "Score: %d     ", engine.score());
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
