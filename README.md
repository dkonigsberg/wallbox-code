# Wallbox Interface Firmware

## Introduction
This repository provides all the code for the firmware that runs on the
ESP8266 (ESP-12S) module that is part of my Wall-O-Matic Interface Board.

The interface board is a device designed to take a Seeburg Wall-O-Matic
jukebox remote, a common sight on a sterotypical 1950's diner tabletop,
and interface it to a Sonos system via Wi-Fi.

The main repository for this project is:
https://github.com/dkonigsberg/wallbox-code

The schematics for the board itself can be found here:
https://github.com/dkonigsberg/wallbox-board

A good overview of the project can be found in these blog posts:
* Part 1 - [Introduction](http://hecgeek.blogspot.com/2017/10/wall-o-matic-interface-1.html)
* Part 2 - [Procuring a Functional Wallbox](http://hecgeek.blogspot.com/2017/10/wall-o-matic-interface-2.html)
* Part 3 - [Decoding the Pulses](http://hecgeek.blogspot.com/2017/10/wall-o-matic-interface-3.html)
* Part 4 - [Inserting Coins](http://hecgeek.blogspot.com/2017/10/wall-o-matic-interface-4.html)
* Part 5 - [Designing the Circuit](http://hecgeek.blogspot.com/2017/10/wall-o-matic-interface-5.html)
* Part 6 - [Developing the Software](http://hecgeek.blogspot.com/2017/10/wall-o-matic-interface-6.html)

You can see a video about the project here:
* [Wallbox Interface Assembly and Testing](https://www.youtube.com/watch?v=2aR7-YdtxFc)


## Building the toolchain
```sh
$ git clone git@github.com:pfalcon/esp-open-sdk.git
$ cd esp-open-sdk
$ make STANDALONE=y VENDOR_SDK=2.1.0-18-g61248df
```
For more detailed information, please see the ```README.md``` file that
accompanies the esp-open-sdk project.

Once the toolchain is built, I strongly recommend preparing a little shell
script to set all the necessary environment variables for using it:

```sh
export PATH=$HOME/esp-open-sdk/xtensa-lx106-elf/bin:$PATH
export SDK_BASE=$HOME/esp-open-sdk/ESP8266_NONOS_SDK-2.1.0-18-g61248df
export XTENSA_TOOLS_ROOT=$HOME/esp-open-sdk/xtensa-lx106-elf/bin
export ESPTOOL=$HOME/esp-open-sdk/esptool/esptool.py
export ESPPORT=/dev/ttyUSB0
```

Obviously adjust these variables to suit your setup. Also, if you've installed
a more recent version of `esptool.py` than what comes with the SDK, you can
adjust the `ESPTOOL` variable to point to that as well. Newer versions of the
tool do provide more features and detailed output.


## Flashing the base firmware

This step will prepare your ESP-12S for use, and allow for some preliminary
testing of the device. It writes the basic startup firmware, initial device
data, and a program that implements an AT-style command set that can be
used to operate the module. It should only need to be done once.

### 32Mbit (512KB+512KB)
```sh
$ cd ~/esp-open-sdk/ESP8266_NONOS_SDK-2.1.0-18-g61248df
$ $ESPTOOL --port $ESPPORT \
    write_flash 0x00000 bin/boot_v1.7.bin \
    0x01000 bin/at/512+512/user1.1024.new.2.bin \
    0x3fc000 bin/esp_init_data_default.bin \
    0x7e000 bin/blank.bin 0x3fe000 bin/blank.bin
```

### 8Mbit (512KB+512KB)
```sh
$ cd ~/esp-open-sdk/ESP8266_NONOS_SDK-2.1.0-18-g61248df
$ $ESPTOOL --port $ESPPORT \
    write_flash 0x00000 bin/boot_v1.7.bin \
    0x01000 bin/at/512+512/user1.1024.new.2.bin \
    0xfc000 bin/esp_init_data_default.bin \
    0x7e000 blank.bin 0xfe000 bin/blank.bin
```

### Testing with the base firmware
After flashing the base firmware, connect to the device using a serial
terminal program. At startup the device will spew its bootloader messages
at a baud rate of 76,800, and then it will switch to a more normal baud rate
of 115,200.

If you are connected with a terminal program that is set to the normal baud
rate, and reset the device, this startup process may fill your screen with
junk. Just restart the program (or reset its emulation state), and you will
be fine. (Another option is to use a program like CuteCom, whose emulation is
too basic to get corrupted by line noise.)

Make sure your terminal program is set to send CR+LF as its line break,
and type `AT+GMR`. This should generate output similar to the following:

```
AT+GMR
AT version:1.4.0.0(May  5 2017 16:10:59)
SDK version:2.1.0(116b762)
compile time:May  5 2017 16:37:48
OK
```


## Building the Wallbox Interface Code
If you've configured everything correctly, and have all the environment
variables set, it should be as simple as this:

```sh
$ cd ~/path/to/wallbox-code
$ make
```


## Flashing the Wallbox Interface Code
First, make sure that you aren't actively connected to the USB serial port
the device is connected to. If you are, then `esptool.py` will spew a
screenful of confusing error messages when you try downloading code.

Second, make sure the device is in "UART" boot mode. If you are using the
real wallbox interface board, then install a jumper across the "Program"
pins and press the "Reset" button. (If you are on a development rig using
an Adafruit Feather HUZZAH, then you don't need to do anything.)

At this point, just run the following command:

```sh
$ make flash
```

Once complete, remove the jumper and press "Reset" again. If you have a
serial terminal app connected at this time, you should see some startup
messages. Take note of the MAC address, as it will be useful if you want
to configure your DHCP server to make it easier to find the device
on your network.

## Configuring Wi-Fi
The first thing you are going to want to do after loading the firmware,
is to configure the Wi-Fi interface on the ESP-12S to connect to your
access point. To do this, you need to tell this firmware to start in a
special configuration mode.

While holding down the "Config" button, press the "Reset" button.
A second or two later, you can release the "Config" button.
The device will now start in its configuration mode, and will have
created a wireless access point with an SSID like: "Wallbox 1B2C3D"
Connect a computer to this SSID, and point a web browser at:
http://192.168.4.1/

Use this page to configure the device to connect to your real Wi-Fi,
network. When finished, it will restart in its normal run mode.
You can then connect to it at an IP address on your real network,
and configure it to talk to your own wallbox and Sonos system.
