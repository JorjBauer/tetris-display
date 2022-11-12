#include "WebManager.h"
#include "xxtea.h"
#include "templater.h"
#include "Prefs.h"
#include "TCPLogger.h"
#include <base64.hpp>
#include <ArduinoOTA.h>
#include "templater.h"
#include <TimeLib.h>
#include <ESP8266TimerInterrupt.h>

extern Prefs myprefs;
extern TCPLogger tlog;
extern WebManager server; // needed for static functions :(

#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif

#define LOGIN_PERIOD_SECONDS (3600)

static const char texthtml[] PROGMEM = "text/html";
static const char textplain[] PROGMEM = "text/plain";
static const char ftrue[] PROGMEM = "true";
static const char ffalse[] PROGMEM = "false";

Templater templ;
ESP8266Timer iTimer;

WebManager::WebManager(int port) : ESP8266WebServer(port)
{
  // NTP is used to get the current time and is required for the AuthN
  // model used here -- we reversibly encrypt the current epoch
  // timestamp, so that we get a cookie for the browser that includes
  // a forced expiration date/time

  timeClient = new NTPClient(ntpUdp, "pool.ntp.org");
  lastTimeUpdate = 0;
}

WebManager::~WebManager()
{
  delete timeClient;
}

void TimerHandler()
{
  server.epochTime++;
}

void WebManager::begin()
{
  // The interval is in microseconds
  iTimer.attachInterruptInterval(1000*1000, TimerHandler);
  
  on("/", handleIndex);
  serveStatic("/style.css", SPIFFS, "/style.css");
  serveStatic("/main.js", SPIFFS, "/main.js");
  on("/login", HTTP_GET, handleLoginGet);
  on("/login", HTTP_POST, handleLoginPost);
  on("/status", handleStatus);
  on("/config", handleConfig);
  on("/submit", HTTP_POST, handleSubmit);
  on("/ls", handleLs);
  on("/download", handleDownload);
  on("/upload", HTTP_POST, []() {
    server.send(200, FPSTR(textplain), "{\"success\":1}");
    }, handleUpload);
  on("/rm", handleRm);
  on("/restart", handleRestart);
  
  collectHeaders("Cookie"); // we need Cookie headers for AuthN checks
  ESP8266WebServer::begin();
}

void WebManager::loop()
{
  handleClient();
  if (millis() > nextTimeUpdate) {
    if (timeClient->update()) {
      // success!
      epochTime = timeClient->getEpochTime();
      lastTimeUpdate = epochTime;
      nextTimeUpdate = millis() + 15 * 60 * 1000; // once every 15 minutes
    }
  }
}

bool WebManager::isAuthenticated() {
  if (hasHeader("Cookie")) {
    tlog.logmsg("has cookie");
    String cookie = header("Cookie");
    int idx = cookie.indexOf("ESPSESSIONID=");
    if (idx != -1) {
      tlog.logmsg("found substr");
      String value = cookie.substring(idx+strlen("ESPSESSIONID="));
      // check to see if there are other cookies after this one
      if (value.indexOf(";")>=0) {
        value = value.substring(0, value.indexOf(";")-1); // FIXME is that -1 correct?
      }
      // 'value' should be the hex value of our cookie now - check it
      // It's a Base64 encoded XXTEA-encrypted string containing the epoch
      // time when we issued the authentication. It should be within the
      // last LOGIN_PERIOD_SECONDS from whatever 'epochTime' is now.
      
      // FIXME: length checking before stuffing it in to encbuf
      uint8_t decbuf[256];
      memset(decbuf, 0, sizeof(decbuf));
      tlog.logmsg("decoding:");
      tlog.logmsg(value.c_str());
      uint16_t numBytes = decode_base64((unsigned char *)value.c_str(), value.length(), decbuf);
      int numBlocks = xxteaDecrypt((char *)decbuf, numBytes/16, myprefs.cookieKey);
      tlog.logmsg("decrypted to:");
      String dec = (const char *)decbuf;
      tlog.logmsg(dec);
      
      uint32_t decEpoch = dec.toInt();
      if (decEpoch &&
          server.epochTime &&
          decEpoch >= server.epochTime - LOGIN_PERIOD_SECONDS) {
        tlog.logmsg("success");
        return true;
      }
    }
  }
  
  // Failed to validate, so redirect to /login
  sendHeader("Location", "/login");
  sendHeader("Cache-Control", "no-cache");
  send(301);
  
  return false;
}

void WebManager::SendHeader()
{
  setContentLength(CONTENT_LENGTH_UNKNOWN);
  send(200, "text/html", "");
  fs::File f = SPIFFS.open("/header.html", "r");
  if (f) {
    repvars *r = templ.newRepvar(String("@COMMENT@"), String(myprefs.comment));
    templ.generateOutput(&server, f, r);
    f.close();
  }
}

void WebManager::SendFooter()
{
  fs::File f = SPIFFS.open("/footer.html", "r");
  if (f) {
    sendContent(f);
    f.close();
  }
}

// static method
void WebManager::handleIndex()
{
  server.SendHeader();
  fs::File f = SPIFFS.open("/index.html", "r");
  server.sendContent(f);
  f.close();
  server.SendFooter();
}

void WebManager::handleLoginGet()
{
  fs::File f = SPIFFS.open("/login.html", "r");
  // Looks like server.send(int, char *, Stream) is not defined;
  // instead, use the individual pieces so we can sendContent(Stream)
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(f);
  f.close();
}

// static method
void WebManager::handleLoginPost()
{
  char outbuf[50];
  
  if (server.hasArg("user") && server.hasArg("pass") &&
      server.arg("user") == "admin" &&
      server.arg("pass") == myprefs.adminPassword) {
    
    sprintf(outbuf, "%u", server.epochTime);
    tlog.logmsg("storing cookie:");
    tlog.logmsg(outbuf);
    
    int numBlocks = xxteaEncrypt(outbuf, strlen(outbuf), myprefs.cookieKey);
    String epochStr = outbuf;
    
    encode_base64((unsigned char *)epochStr.c_str(), numBlocks*16, (unsigned char *)outbuf);
    tlog.logmsg("encoded:");
    tlog.logmsg(outbuf);
    
    epochStr = "ESPSESSIONID=";
    epochStr += outbuf;
    
    // Logged in successfully
    server.sendHeader("Location", "/");
    server.sendHeader("Cache-Control", "no-cache");
    server.sendHeader("Set-Cookie", epochStr.c_str());
    server.send(301);
  } else {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(301);
  }
}

// static method
void WebManager::handleStatus()
{
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

  fs::File f = SPIFFS.open("/status.html", "r");
  templ.generateOutput(&server, f, r);
  f.close();

  server.SendFooter();
}

// static method
void WebManager::handleConfig()
{
  if (!server.isAuthenticated()) {
    return;
  }

  server.SendHeader();
  
  String html =
    String(F("<form action='/submit' method='post'>"
             "<div><label for='ssid'>Connect to SSID:</label>"
             "<input type='text' id='ssid' name='ssid' value='")) +
    String(myprefs.ssid) +
    String(F("'/></div>"
             "<div><label for='password'>Network Password:</label>"
             "<input type='password' id='password' name='password' value='")) +
    String(myprefs.password) +
    String(F("'/></div>"
             "<div><label for='adminpw'>Admin Password:</label>"
             "<input type='password' id='adminpw' name='adminpw' value='")) +
    String(myprefs.adminPassword) +
    String(F("'/></div>"
             "<div><label for='otapw'>OTA Password:</label>"
             "<input type='password' id='otapw' name='otapw' value='")) +
    String(myprefs.otaPassword) +
    String(F("'/></div>"
             "<div><label for='hashmat'>Cookie encryption key:</label>"
             "<input type='password' id='hashmat' name='hashmat' value='")) +
    String(myprefs.hashMaterial) +
    String(F("'/></div>"
             "<div><label for='comment'>Comment:</label>"
             "<input type='text' id='comment' name='comment' value='")) +
    String(myprefs.comment) +
    String(F("'/></div><div><input type='submit' value='Save' /></div>"
             "</form>"));
  
  server.sendContent(html);
  
  server.SendFooter();
}

// static method
void WebManager::handleSubmit()
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

  myprefs.set("ssid", new_ssid);
  myprefs.set("password", new_password);
  myprefs.set("comment", new_comment);
  myprefs.set("adminPassword", new_adminpw);
  myprefs.set("otaPassword", new_otapw);
  myprefs.set("hashMaterial", new_hashmat);

  myprefs.write();
  myprefs.read();
  ArduinoOTA.setPassword(myprefs.otaPassword); // FIXME: does this change happen in real time or do we need a restart?

  // Redirect to /status to show the changes
  server.sendHeader(F("Location"), String("/status"), true);
  server.send(302, FPSTR(textplain), "");
}

// FIXME: upload needs authentication. But I use it from a curl
// command, so how to do that?
fs::File fsUploadFile;
// static method
void WebManager::handleUpload()
{
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/"))
      filename = "/" + filename;

    fsUploadFile = SPIFFS.open(filename, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile)
      fsUploadFile.close();
  }
}

// static method
void WebManager::handleDownload()
{
  if (!server.isAuthenticated()) {
    return;
  }
  
  String filename = server.arg("file");
  if (filename.isEmpty()) {
    server.send(200,
                FPSTR(texthtml),
                F("<html><h3>Error</h3><div>No file argument specified</div></html>"));
    return;
  }
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

  fs::File f = SPIFFS.open(filename, "r");
  if (!f) {
    server.send(200,
                FPSTR(texthtml),
                F("<html><h3>Error</h3><div>File not found</div></html>"));
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("application/octet-stream"), "");
  uint32_t fsize = f.size();
  char buf[128];
  while (fsize) {
    uint32_t count = f.readBytes(buf, MIN(sizeof(buf), fsize));
    fsize -= count;
    server.sendContent(buf, count);
  }
  f.close();
}

// static method
void WebManager::handleRm()
{
  if (!server.isAuthenticated()) {
    return;
  }

  server.SendHeader();
  
  String filename = server.arg("file");
  if (filename && !filename.isEmpty()) {
    if (SPIFFS.remove(filename)) {
      server.sendContent("Unlinked file ");
    } else {
      server.sendContent("Remember that SPIFFS needs leading slashes. Failed to unlink file ");
    }
    server.sendContent(filename);
  }

  server.SendFooter();
}

// static method
void WebManager::handleLs()
{
  if (!server.isAuthenticated()) {
    return;
  }
  
  server.SendHeader();

  File root = SPIFFS.open("/", "r");

  Dir dir = SPIFFS.openDir("/");
  server.sendContent(F("<pre>"));
  while (dir.next()) {
    server.sendContent(dir.fileName());
    server.sendContent(F("\n"));
  }
  server.sendContent(F("</pre>"));

  server.SendFooter();
}

// static method
void WebManager::handleRestart()
{
  if (!server.isAuthenticated()) {
    return;
  }
  
  server.SendHeader();
  server.sendContent("Restarting");
  server.SendFooter();

  ESP.restart();
}

