#ifndef __CLOCKPREFS_H
#define __CLOCKPREFS_H

#include <Arduino.h>

#include "Prefs.h"

enum {
  T_DST_NONE = 0,
  T_DST_USA  = 1,
  T_DST_EU   = 2
};

struct ClockPrefs : public Prefs {
 public:
  ClockPrefs();
  virtual ~ClockPrefs();

  virtual void extendedWrite(fs::File f);
  
  virtual void setDefaults();
  virtual void set(const char *what, String newVal);
  virtual void set(const char *what, const char *newVal);

 public:
  float lat;
  float lon;
  int8_t defaultTimeZone;
  int8_t autoSetDST;
};

#endif
