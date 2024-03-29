#include "ClockPrefs.h"

ClockPrefs::ClockPrefs() : Prefs()
{
}

ClockPrefs::~ClockPrefs()
{
}

void ClockPrefs::extendedWrite(fs::File f)
{
  f.print("lat=");
  f.println(lat);
  f.print("lon=");
  f.println(lon);
  f.print("defaultTimeZone=");
  f.println(defaultTimeZone);
  f.print("autoSetDST=");
  switch (autoSetDST) {
  case T_DST_EU:
    f.println("E");
    break;
  case T_DST_USA:
    f.println("U");
    break;
  default:
    f.println("N");
    break;
  }
}

void ClockPrefs::setDefaults()
{
  Prefs::setDefaults();
  lat = 40;
  lon = -75;
  defaultTimeZone = -5;
  autoSetDST = T_DST_USA;
}

void ClockPrefs::set(const char *what, String newVal)
{
  set(what, (const char *)newVal.c_str());
}

void ClockPrefs::set(const char *what, const char *newVal)
{
  if (!strcmp(what, "lat")) {
    lat = String(newVal).toFloat();
  } else if (!strcmp(what, "lon")) {
    lon = String(newVal).toFloat();
  } else if (!strcmp(what, "defaultTimeZone")) {
    defaultTimeZone = String(newVal).toInt();
  } else if (!strcmp(what, "autoSetDST")) {
    switch (newVal[0]) {
    case 'E':
      autoSetDST = T_DST_EU;
      break;
    case 'U':
      autoSetDST = T_DST_USA;
      break;
    default:
      autoSetDST = T_DST_NONE;
      break;
    }
  }
  else
    Prefs::set(what, newVal);
}
