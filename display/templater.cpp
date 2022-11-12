#include <Arduino.h>
#include "templater.h"

Templater::Templater() {
}

Templater::~Templater() {
}

repvars *Templater::newRepvar(String name, String value)
{
  repvars *r = new repvars {name, value, NULL};
  return r;
}

repvars *Templater::newRepvar(const char *name, const char *value)
{
  String n(name);
  String v(value);
  return newRepvar(n, v);
}

repvars *Templater::addRepvar(repvars *r, String name, String value)
{
  repvars *n = newRepvar(name, value);
  n->next = r;
  return n;
}

repvars *Templater::addRepvar(repvars *r, const char *name, const char *value)
{
  String n(name);
  String v(value);
  repvars *n1 = newRepvar(n, v);
  n1->next = r;
  return n1;
}

void Templater::deleteRepvar(repvars *r)
{
  while (r) {
    repvars *n = r->next;
    delete r;
    r = n;
  }
}

void Templater::generateOutput(ESP8266WebServer *server,
                               fs::File f,
                               repvars *r)
{
  /* read a line of the file
   * flip through all the r->name and replace with r->value
   *  until r->next is NULL
   */
  while (f.available()) {
    String aLine = f.readStringUntil('\n');
    repvars *t = r;
    while (t) {
      aLine.replace(t->name, t->value);
      t = t->next;
    }
    // If we send the String object with an empty string, 'server' will
    // interpret that as "done". But we want to honor blank lines in <pre>
    // blocks - and for that matter, we want <pre> blocks to include
    // linebreaks - so we add the end-of-line here.
    aLine += "\n";
    server->sendContent(aLine);
  }
  deleteRepvar(r);
}
