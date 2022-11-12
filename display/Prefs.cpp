#include <Arduino.h>
#include <FS.h>
#include <CRC32.h>

#include "Prefs.h"

Prefs::Prefs()
{
  prefsFileName = NULL;
  cookieKey[0] = cookieKey[1] = cookieKey[2] = cookieKey[3] = 0;

  ssid[0] = password[0] = mdnsName[0] = comment[0] = adminPassword[0] = otaPassword[0] = hashMaterial[0] = 0;

}

Prefs::~Prefs()
{
  if (this->prefsFileName)
    delete[] this->prefsFileName;
  if (this->baseName)
    delete[] this->baseName;
}



void Prefs::begin(const char *baseName)
{
  if (this->prefsFileName)
    delete[] this->prefsFileName;
  if (this->baseName)
    delete[] this->baseName;

  this->prefsFileName = new char[strlen(baseName)+1+5];
  sprintf(this->prefsFileName, "/%s.cfg", baseName);
  this->baseName = new char[strlen(baseName)+1];
  strcpy(this->baseName, baseName);

  setDefaults();
}

void Prefs::write()
{
  fs::File f = SPIFFS.open(prefsFileName, "w");

  f.print("# Configuration for ");
  f.println(baseName);
  f.print("ssid=");
  f.println(ssid);
  f.print("password=");
  f.println(password);
  f.print("comment=");
  f.println(comment);
  f.print("adminpw=");
  f.println(adminPassword);
  f.print("otapw=");
  f.println(otaPassword);
  f.print("hashmat=");
  f.println(hashMaterial);

  extendedWrite(f);
  
  f.close();
}

void Prefs::setDefaults()
{
  strncpy(ssid, "", sizeof(ssid));
  strncpy(password, "", sizeof(password));
  strncpy(mdnsName, baseName, sizeof(mdnsName));
  strncat(mdnsName, "-", sizeof(mdnsName)-strlen(mdnsName));
  strncat(mdnsName, String(ESP.getChipId()).c_str(), sizeof(mdnsName)-strlen(mdnsName));
  strncpy(comment, "undefined", sizeof(comment));
  strncpy(adminPassword, "admin", sizeof(adminPassword));
  strncpy(otaPassword, "admin", sizeof(otaPassword));
  strncpy(hashMaterial, "defaultAppPw", sizeof(hashMaterial));

  cookieKey[0] = cookieKey[1] = cookieKey[2] = cookieKey[3] = 0;
}

void Prefs::set(const char *what, String newVal)
{
  set(what, (const char *)newVal.c_str());
}

void Prefs::set(const char *what, const char *newVal)
{
  if (!strcmp(what, "ssid")) {
    strncpy(ssid, newVal, sizeof(ssid));
  }
  else if (!strcmp(what, "password")) {
    strncpy(password, newVal, sizeof(password));
  }
  else if (!strcmp(what, "comment")) {
    strncpy(comment, newVal, sizeof(comment));
  }
  else if (!strcmp(what, "adminpw")) {
    strncpy(adminPassword, newVal, sizeof(adminPassword));
  }
  else if (!strcmp(what, "otapw")) {
    strncpy(otaPassword, newVal, sizeof(otaPassword));
  }
  else if (!strcmp(what, "hashmat")) {
    strncpy(hashMaterial, newVal, sizeof(hashMaterial));

    // Turn hashMaterial in to cookieKey[4]. This is pretty awful from
    // a crypto standpoint - but it keeps the code small. I wouldn't
    // use this for anything particularly important/sensitive...
    uint32_t checksum = CRC32::calculate(hashMaterial,
                                         strlen(hashMaterial));
    cookieKey[0] = checksum;
    cookieKey[1] = CRC32::calculate(&cookieKey[0], 4);
    cookieKey[2] = CRC32::calculate(&cookieKey[1], 4);
    cookieKey[3] = CRC32::calculate(&cookieKey[2], 4);
  }
}

bool Prefs::read()
{
  fs::File f = SPIFFS.open(prefsFileName, "r");
  if (!f)
    return false;

  bool readingVar = true;
  int8_t slen = 0;
  char lhs[50] = {'\0'};
  char *lp = lhs;
  char rhs[50] = {'\0'};
  char *rp = rhs;
  for(uint8_t i=0; i<f.size(); i++) {
    char c = f.read();
    // Simple safety for binary garbage
    if (c > 126 || (c<32 && c != 10 && c != 13))
      return false;

    // Skip commented out and blank lines
    if (slen == 0 && (c == '#' || c == '\n' || c == '\r')) {
      continue;
    }

    if (slen >= 49) {
      // safety: reset
      slen = 0;
      readingVar = true;
      lhs[0] = rhs[0] = '\0';
    }

    if (readingVar) {
      // Keep reading a variable name until we hit an '='
      if (c == '\n' || c == '\r') {
        // Abort - got a return before the '='
        slen = 0;
        lhs[0] = '\0';
      }
      else if (c == '=') {
        readingVar = false;
        slen = 0;
        rhs[0] = '\0';
      } else {
        lhs[slen++] = c;
        lhs[slen] = '\0';
      }
    } else {
      // Keep reading a variable value until we hit a newline
      if (c == '\n' || c == '\r') {
        set(lhs, String(rhs));
        readingVar = true;
        slen = 0;
        lhs[0] = rhs[0] = '\0';
      } else {
        rhs[slen++] = c;
        rhs[slen] = '\0';
      }
    }
  }

  return true;
}

