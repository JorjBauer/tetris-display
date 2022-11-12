#ifndef __TCPLOGGER_H
#define __TCPLOGGER_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiServer.h>

class TCPLogger {
 public:
  TCPLogger();
  ~TCPLogger();

  void begin();
  void logmsg(String s);
  void logmsg(const char *msg);

  void loop();
  
 private:
  WiFiServer *tcpserver;
  WiFiClient tcpclient;
};

#endif
