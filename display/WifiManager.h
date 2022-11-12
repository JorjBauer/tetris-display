#ifndef __WIFIMANAGER_H
#define __WIFIMANAGER_H

#include <Arduino.h>
#include <ESP8266WiFi.h>

class WifiManager {
 public:
  WifiManager();
  ~WifiManager();

  void begin(const char *templateName);
  void loop();

 private:
  void StartSoftAP();
  void JoinNetwork();
  
 private:
  String baseName;
  
  bool softAP;
  bool scanUnderway;
  uint32_t nextWifiScan;
};

#endif
