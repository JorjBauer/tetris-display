Tetris Display
==============

Yet another semi-pointless project for fun!

The Tetris Display was born as a use for a flexible WS2812 LED panel I
had lying around. My 13-year-old son and I built a quick Tetris clone
for it; his friends played it with him (and insisted on Super Rotation
support) at his 14th birthday party. And then things got even more
interesting.

This display (and remote control) use a pair of ESP8266 ESP-01s on
their own network to play Tetris. But why stop there?

It's an ESP, so it has WiFi, so it also serves its own web
page... where you can configure it, or play Tetris without the remote
(sort of; it's not ideal).

And as long as we have a matrix of pixels, we might as well let it
serve as a WiFi-connected display for text messages. They're sideways,
but not awful.

And then, why would I build anything without also making it a clock?
(Why are clocks so fun to build? Sigh.) If you put this on your home
WiFi network, it will get NTP time updates off the Internet. Automatic
Daylight Savings time (US and Europe rules); sunrise and sunset
brightness shifts; NTP synchronization every 10 minutes (done
stupidly, so it's +/- 1 second accuracy).

As of this writing, this is still a bit of a work-in-progress. The
write-up is [going on
Hackaday](https://hackaday.io/project/166204-tetris-display), so take
a look there while I finish it all off. (It's fully functional but not
necessarily polished.)


Setup
=====

By default, the display will broadcast its own SSID
("tetris-display"). Connect to that network, and then browse to

  http://192.168.4.1/config

When you're done, press 'Save'; if the values look right, then go to

  http://192.168.4.1/reset

That's it! The display will reboot and come up in your set
configuration. If you gave it an SSID/password, it should associate
with your wireless network; find an NTP server; and automatically
start the clock.





