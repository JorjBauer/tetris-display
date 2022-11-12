#include "WifiManager.h"
#include "Prefs.h"
#include <ArduinoOTA.h>

extern Prefs myprefs;

WifiManager::WifiManager()
{
  softAP = false;
  scanUnderway = false;
  nextWifiScan = 0;
}

WifiManager::~WifiManager()
{
}

void WifiManager::begin(const char *templateName)
{
  baseName = templateName;
  
  if (!strlen(myprefs.ssid)) {
    StartSoftAP();
  } else {
    JoinNetwork();
  }
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  
  ArduinoOTA.setHostname(myprefs.mdnsName);
  if (myprefs.otaPassword[0]) {
    ArduinoOTA.setPassword(myprefs.otaPassword);
  } else {
    ArduinoOTA.setPassword("admin");
  }
  ArduinoOTA.begin();
}
  
void WifiManager::loop()
{
  ArduinoOTA.handle();
  
  bool rejoinSSID = false;
  // If we're running a SoftAP, periodically scan to see if our prefs.ssid
  // appears. If so, then try to join it.
  if (softAP && myprefs.ssid[0] && (millis() > nextWifiScan)) {
    WiFi.scanNetworks(true, false); // async, and don't show hidden
                                    // nets
    scanUnderway = true;
    nextWifiScan = (10 * 60 * 1000) + millis(); // 10 minutes from now
  }
  if (scanUnderway) {
    int numberOfNetworks = WiFi.scanComplete();
    if (numberOfNetworks >= 0) {
      // >=0 indicates success
      if (numberOfNetworks > 0) {
        // >0 means there are results to inspect
        for (int i=0; i<numberOfNetworks; i++) {
          if (!strcmp(WiFi.SSID(i).c_str(), myprefs.ssid)) {
            // The SSID we want to be connected to exists - try to
            // join it
            rejoinSSID = true;
          }
        }
      }
      scanUnderway = false;
    }
    WiFi.scanDelete(); // free the memory from the scan
  }
  if (rejoinSSID) {
    JoinNetwork();
  }
}

void WifiManager::StartSoftAP()
{
  WiFi.mode(WIFI_AP);
  WiFi.setPhyMode((WiFiPhyMode_t)PHY_MODE_11N);
  String networkName = baseName + String("-") + ESP.getChipId();
  WiFi.softAP(networkName);
  // Set the IP address and info for SoftAP mode. Note this is also
  // the default IP (192.168.4.1), but better to be explicit...
  IPAddress local_IP(192,168,4,1);
  IPAddress gateway(192,168,4,1);
  IPAddress subnet(255,255,255,0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  softAP = true;
  scanUnderway = false;
  nextWifiScan = millis() + 10 * 60 * 1000; // 10 minutes from now
}

void WifiManager::JoinNetwork()
{
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode((WiFiPhyMode_t)PHY_MODE_11N);
  WiFi.begin(myprefs.ssid, myprefs.password);
  uint8_t count=0;
  while (count < 10 && WiFi.waitForConnectResult() != WL_CONNECTED) {
    count++;
    delay(5000);
  }
  if (count >= 10) {
    // Failed to connect to wifi. Fallback to SoftAP.
    StartSoftAP();
  } else {
    // connected successfully - stop any softap functionality in
    // progress
    softAP = false;
    scanUnderway = false;
  }
}
