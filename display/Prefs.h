#ifndef __Prefs_H
#define __Prefs_H

#include <stdint.h>
#include <FS.h>

struct Prefs {
 public:
  Prefs();
  virtual ~Prefs();

  void begin(const char *baseName);
  
  void write();
  bool read();

  virtual void extendedWrite(fs::File f) { }; // For virtual overloads to write more prefs
  
  virtual void setDefaults();
  virtual void set(const char *what, String newVal);
  virtual void set(const char *what, const char *newVal);

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
