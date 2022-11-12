#ifndef __Prefs_H
#define __Prefs_H

#include <stdint.h>
#include <FS.h>

class Prefs {
 public:
  Prefs();
  ~Prefs();

  void begin(const char *baseName);
  
  void write();
  bool read();

  void extendedWrite(fs::File f) { }; // For virtual overloads to write more prefs
  
  void setDefaults();
  void set(const char *what, String newVal);
  void set(const char *what, const char *newVal);

  private:  
  char *prefsFileName;
  char *baseName;
  
 public:
  char ssid[50];
  char password[50];
  char mdnsName[50];
  char comment[250];
  char adminPassword[50];
  char otaPassword[50];
  char hashMaterial[50]; // A string used to construct the cookieKey

  uint32_t cookieKey[4];
};

#endif
