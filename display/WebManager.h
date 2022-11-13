#ifndef __WEBMANAGER_H
#define __WEBMANAGER_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "Prefs.h"

class WebManager : virtual public ESP8266WebServer {
 public:
  WebManager(int port);
  ~WebManager();

  void begin(const Prefs *p);
  void loop();

  bool isAuthenticated();

  void sendFileHandle(fs::File f);

  void SendHeader();
  void SendFooter();

  static void handleIndex();
  static void handleLoginGet();
  static void handleLoginPost();
  static void handleStatus();
  static void handleConfig();
  static void handleSubmit();
  static void handleUpload();
  static void handleDownload();
  static void handleRm();
  static void handleLs();
  static void handleRestart();

public:
  volatile uint32_t epochTime;
  uint32_t lastTimeUpdate;
private:
  WiFiUDP ntpUdp;
  NTPClient *timeClient;
  uint32_t nextTimeUpdate;
};

#endif
