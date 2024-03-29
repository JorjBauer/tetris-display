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
("tetris-#####"). Connect to that network, configure your machine with
a static IP address (I use 192.168.4.100) and netmask 255.255.255.0,
and then you should be able to configure it at the address

  http://192.168.4.1/config

When you're done, press 'Save'; it should pick up the network you
selected immediately, and use DHCP to get on-net. It will find an
NTP server and automatically start the clock.

Visual Glitches
===============

If you're seeing a sort of visual "stutter" while the display is
drawing, then you're likely being bitten by a problem with the
FastLED library and the ESP8266 core starting with version 3.0.0
(confirmed up to at least 3.0.2):

    https://github.com/FastLED/FastLED/commit/ae4c696d53981bd24cc6932593204b906b82d786

That patch fixed most of the issue (so that the display works some of
the time), but in the middle of the patch you'll find this comment:

	// even with interrupts disabled, the NMI interupt seems to cause
	// timing issues here. abort the frame if one bit took to long. if the
	// last of the 24 bits has been sent already, it is too late
	// this fixes the flickering first pixel that started to occur with
	// framework version 3.0.0

Go to the board manager and install the esp8266 platform version
2.7.4; as of this writing, there's no way to fix this problem with
version 3.0.0 and above.

OTA Updates
===========

While this does have OTA updates enabled, they probably won't work for
you. ArduinoOTA only works if the program you're installing is less
than 50% of the program space. On a typical ESP-01s, this is 57% of
the program space right now. If you're installing on a larger esp8266
variant then it should work just fine.

There's also an embedded HTTP updater that you can use, however. (Note
that this must be http, and not https.) If you use "export binary"
from the Arduino IDE, and then take the output .bin file it generates
to a web server, you can pass that web server URL to the clock like
this:

   http://<clock IP>/checkDownload?url=http://example.org/display.ino.generic.bin

It takes about 20 seconds to pull the binary and another 15 seconds to reboot.

Install errors on Big Sur and above
===================================

If you see the error "pyserial or esptool directories not found next
to this upload.py tool", you might need to patch up 2.7.4 so it will
install on your device. This worked for me:

* Remove the existing esptool and pyserial folders in
  ~/Library/Arduino15/packages/esp8266/hardware/esp8266/2.7.4/tools/
* Download https://github.com/espressif/esptool/archive/v3.0.zip
* Download https://github.com/pyserial/pyserial/archive/v3.4.zip
* Extract both of those archives in to the library tools directory
* Rename "pyserial-3.4" to just "pyserial"
* Rename "esptool-3.0" to just "esptool"
* Restart the Arduino IDE
