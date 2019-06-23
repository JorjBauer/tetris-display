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

As of this writing, this is still a bit of a work-in-progress. I
expect to put the project up on Hackaday.io shortly, and write up the
tale in excrutiating detail there while I finish it all off...
