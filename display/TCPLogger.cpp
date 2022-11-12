#include "TCPLogger.h"

TCPLogger::TCPLogger()
{
  tcpserver = new WiFiServer(9001);
}

TCPLogger::~TCPLogger()
{
  delete tcpserver;
}

void TCPLogger::begin()
{
  tcpserver->begin();
}

void TCPLogger::logmsg(String s)
{
  logmsg((const char *)s.c_str());
}

void TCPLogger::logmsg(const char *msg)
{
  if (tcpclient && tcpclient.connected()) {
    tcpclient.println(msg);
    tcpclient.flush();
  }
}

void TCPLogger::loop()
{
  if (tcpserver->hasClient()) {
    if (tcpclient.connected()) {
      tcpclient.stop();
    }
    tcpclient = tcpserver->available();
    tcpclient.println("Hello");
    tcpclient.flush();
  }
}
