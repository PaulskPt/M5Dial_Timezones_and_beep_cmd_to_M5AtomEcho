M5Stack M5Dial Timezones and beep cmd to M5AtomEcho

This is a combination of my [earlier repo1 ](https://github.com/PaulskPt/M5Stack_Atom_Matrix_Timezones)
and [earlier repo2](https://github.com/PaulskPt/M5Stack_M5Atom_EchoSPKR)

Important credit:

I was only able to create and successfully finish this project with the great help of Microsoft AI assistant CoPilot.
CoPilot helped me correcting wrong C++ code fragments. It suggested C++ code fragments. CoPilot gave me, in one Q&A session, a "workaround" 
for certain limitation that the Arduino IDE has with the standard C++ specification. And CoPilot gave it's answers at great speed of response.
There were no delays. The answers were instantaneous! Thank you CoPilot. Thank you Microsoft for this exciting new tool!

Hardware used:

    1) M5Stack M5Dial;
    2) M5Stack M5Atom Echo;
    3) GROVE 4-wire cable;

The software consists of two parts: a) for the M5Dial; b) for the M5Atom Echo.

The M5Dial part:

Since 2024-10-11 there are two versions of the M5Dial part: 
1. an initial version that used display touch to put the display asleep or awake;
2. an updated version with RFID TAG recognition. Now you can put the display asleep or awake with your RFID TAG.
   The ID number of the RFID TAG has to be copied into the file ```secret.h```, variable: ```SECRET_MY_RFID_TAG_NR_HEX```,
   for example: ```2b8e3942``` (letters in lower case). A global variable ```use_rfid``` (default: ```true```), controls if RFID recognition
   will be active or not (in that case display touch will be the way to put the display asleep or awake).

After applying power to the M5Dial device, the sketch will sequentially display data of seven pre-programmed timezones.


For each of the seven timezones, in four steps, the following data will be displayed:
   1) Time zone continent and city, for example: "Europe" and "Lisbon"; 
   2) the word "Zone" and the Timezone in letters, for example "CEST", and the offset to UTC, for example "+0100";
   3) date info, for example "Monday September 30 2024"; 
   4) time info, for example: "20:52:28 in: Lisbon".

Each time zone sequence of four displays is repeated for 25 seconds. This repeat time is defined in function ```loop()```:

```
845 unsigned long const zone_chg_interval_t = 25 * 1000L; // 25 seconds
```

M5Dial Display:

2024-10-11 Added functionality to switch the display off and On by display touches. This new function is "work-in-progress". The response of the sketch to a display touch to set the display asleep is not as sensitive as is to the opposite: touch to awake the display.

M5Dial sound:

The M5Dial has a built-in speaker, however my experience is that the sound is very weak, even with the volume set maximum (10).
I also experienced that the audibility of the speaker sound depends on the frequency of the tone played. Another thing I noticed is that when using the speaker, the NTP Time Synchronization moment is delayed each time by 2 seconds. When the speaker is not used, there is no delay in the NTP Time Synchronization moment.
For this reason I decided not to use the M5Dial speaker. As an alternative I added a text in the toprow (see below under ```M5Dial Reset:```),
and in this repo, I added functionality to "use" the ability of the M5Atom Echo device to produce nice sounds, also louder than the speaker of the M5Dial device can produce.
The function ```send_cmd_to_AtomEcho()``` is called at the moment of NTP Time Synchronization, however only when the display is awake. When the display is asleep (off), because the user touched the display to put it asleep, for example to have the display asleep during night time, no beep commands will be send to the Atom Echo device. We don't want sounds during the night or other moments of silence.

M5Dial reset:

Pressing the M5Dial button (of the display) will cause a software reset.

On reset the Arduino Sketch will try to connect to the WiFi Access Point of your choice (set in secret.h). 
The sketch will connect to a SNTP server of your choice. In this version the sketch uses a ```SNTP polling system```. 
The following define sets the SNTP polling interval time:

```
66 #define CONFIG_LWIP_SNTP_UPDATE_DELAY  15 * 60 * 1000 // = 15 minutes
```

At the moment of a SNTP Time Synchronization, the text "TS" will be shown in the middle of the toprow of the display.
The sketch will also send a digital impulse via GROVE PORT B of the M5Dial, pin 1 (GROVE white wire).
The internal RTC of the M5Dial device will be set to the SNTP datetime stamp with the local time for the current Timezone.
Next the sketch will display time zone name, timezone offset from UTC, date and time of the current Timezone.

In the M5Dial sketch is pre-programmed a map (dictionary), name ```zones_map```. At start, the function ```create_maps()```
will import all the timezone and timezone_code strings from the file ```secret.h``` into the map ```zones_map```, resulting
in the following map:

```
    zones_map[0] = std::make_tuple("America/Kentucky/Louisville", "EST5EDT,M3.2.0,M11.1.0");
    zones_map[1] = std::make_tuple("America/New_York", "EST5EDT,M3.2.0,M11.1.0");
    zones_map[2] = std::make_tuple("America/Sao_Paulo", "<-03>3");
    zones_map[3] = std::make_tuple("Europe/Lisbon","WET0WEST,M3.5.0/1,M10.5.0");
    zones_map[4] = std::make_tuple("Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3");
    zones_map[5] = std::make_tuple("Asia/Tokyo", "JST-9");
    zones_map{6] = std::make_tuple("Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3");
```

M5Dial Debug output:

Because of memory limitations all of the if (my_debug) {...} blocks were removed.
Only in function time_sync_notification_cb() there is used a preprocessor directive ```DEBUG_OUTPUT```. (In M5Dial version 2),
defined in line 52.

File secret.h:

Update the file secret.h as far as needed:
```
 a) your WiFi SSID in SECRET_SSID;
 b) your WiFi PASSWORD in SECRET_PASS;
 c) the name of the NTP server of your choice in SECRET_NTP_SERVER_1, for example: 2.pt.pool.ntp.org;
 d) the SECRET_NTP_NR_OF_ZONES as a string, e.g.: "7";
 e) the TIMEZONE and TIMEZONE_CODE texts for each of the zones you want to be displayed.

 At this moment file secret.h has the following timezones and timezone_codes defined:
    #define SECRET_NTP_TIMEZONE0 "America/Kentucky/Louisville"
    #define SECRET_NTP_TIMEZONE0_CODE "EST5EDT,M3.2.0,M11.1.0"
    #define SECRET_NTP_TIMEZONE1 "America/New_York"
    #define SECRET_NTP_TIMEZONE1_CODE "EST5EDT,M3.2.0,M11.1.0"
    #define SECRET_NTP_TIMEZONE2 "America/Sao_Paulo"
    #define SECRET_NTP_TIMEZONE2_CODE "<-03>3"
    #define SECRET_NTP_TIMEZONE3 "Europe/Lisbon"
    #define SECRET_NTP_TIMEZONE3_CODE "WET0WEST,M3.5.0/1,M10.5.0"
    #define SECRET_NTP_TIMEZONE4 "Europe/Amsterdam"
    #define SECRET_NTP_TIMEZONE4_CODE "CET-1CEST,M3.5.0,M10.5.0/3"
    #define SECRET_NTP_TIMEZONE5 "Asia/Tokyo"
    #define SECRET_NTP_TIMEZONE5_CODE "JST-9"
    #define SECRET_NTP_TIMEZONE6 "Australia/Sydney"
    #define SECRET_NTP_TIMEZONE6_CODE "AEST-10AEDT,M10.1.0,M4.1.0/3"

As you can see, I have the timezones ordered in offset to UTC, however this is not a must.

```

The M5Atom Echo part:

After applying power to the M5 Echo Atom device, the sketch will wait for a "beep command" impulse on the GROVE PORT pin1, sent by the M5Dial device, or a button press of the button on top of the M5Atom Echo device. Upon pressing the button or receiving a "beep command", a double tone sound will be produced and the RGB Led wil be set to GREEN at the start of the beeps. After the beeps have finished, the RGB Led will be set to RED.

The sketch ```M5Atom_EchoSPKR_beep_on_command_M5Dial.ino``` uses two files: ```AntomEchoSPKR.h``` and ```AntomEchoSPKR.cpp```,
which last two files define the ```class ATOMECHOSPKR```, used by the sketch.
The sketch has functionality to make the M5Atom Echo device to "listen" on Pin1 of the GROVE PORT for a digital impulse (read: beep command)
sent by the M5Dial at moment of a NTP Time Synchronization.

Updates:

2024-10-09: Version 1 for the M5Dial: added functionality to switch sound ON/off by double press of the button. If sound is off, the RGB Led will show BLUE color.
If sound is ON (default), the RGB Led will show RED color.

2025-10-14: Version 2 for M5Dial: I had to delete a lot of ```if (my_debug)``` blocks and use other measures regarding definitions of certain variables containing texts to get rid of a ```memory full``` error while compiling the sketch. After these measures the memory is occupied for 97 percent. The sketch compiles OK.

2024-10-17: Version 2 for M5Dial: in function ```time_sync_notification_cb()``` changed the code a lot to make certain that the function initTime() gets called only once at the moment of a SNTP synchronization.

2024-10-22: created a second version M5Dial with RFID however with few messages to the Serial Monitor to reduce the use of memory.
Name of this sketch: ```M5Dial_Timezones_and_M5Echo_with_RFID_few_msgs.ino```.

2024-10-23: totally rebuild function disp_data(), to eliminate memory leaks.

Docs:

```
M5Dial_Monitor_output.txt
M5AtomEcho_Monitor_output.txt
```

Images: 

Images taken during the sketch was running are in the folder ```images```.

Video:

Here is a link to a post on X: [Post](https://x.com/PSchulinck/status/1847631815902625945?t=Y801U7cWpaVQeQWlq13S_A&s=19)

Links to product pages of the hardware used:

- M5Stack M5Dial [info](https://docs.m5stack.com/en/core/M5Dial);
- M5Stack M5Atom Echo [info](https://docs.m5stack.com/en/atom/atomecho);
- M5Stack GROVE Cable [info](https://docs.m5stack.com/en/accessory/cable/grove_cable)

If you want a 3D Print design of a stand for the M5Dial, see the post of Cyril Ed on the Printables website:
- [Stand for M5Dial](https://www.printables.com/model/614079-m5stactk-dial-stand).
  I successfully downloaded the files and sent them to a local electronics shop that has a 3D printing service.
  See the images.
  
