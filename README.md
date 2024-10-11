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

After applying power to the M5Dial device, the sketch will sequentially display data of six pre-programmed timezones.


For each of the six timezones, in four steps, the following data will be displayed:
   1) Time zone continent and city, for example: "Europe" and "Lisbon"; 
   2) the word "Zone" and the Timezone in letters, for example "CEST", and the offset to UTC, for example "+0100";
   3) date info, for example "Monday September 30 2024"; 
   4) time info, for example: "20:52:28 in: Lisbon".

Each time zone sequence of four displays is repeated for 25 seconds. This repeat time is defined in function ```loop()```:

```
1045 unsigned long const zone_chg_interval_t = 25 * 1000L; // 25 seconds
```

M5Dial Display:

2024-10-11 Added functionality to switch the display off and On by display touches. This new function is "work-in-progress". The response of the sketch to a display touch to set the display asleep is not as sensitive as is to the opposite: touch to awake the display.

M5Dial sound:

The M5Dial has a built-in speaker, however my experience is that the sound is very weak, even with the volume set maximum (10).
I also experienced that the audibility of the speaker sound depends on the frequency of the tone played. Another thing I noticed is that when using the speaker, the NTP Time Synchronization moment is delayed each time by 2 seconds. When the speaker is not used, there is no delay in the NTP Time Synchronization moment.
For this reason I decided not to use the M5Dial speaker. As an alternative I added a text in the toprow (see below under ```M5Dial Reset:```),
and in this repo, I added functionality to "use" the ability of the M5Atom Echo device to produce nice sounds, also louder than the speaker of the M5Dial device can produce.
The function ```send_cmd_to_AtomEcho()``` is called at the moment of NTP Time Synchronization, however only when the display is awake. When the display is asleep (off), because the user touched the display to put it asleep, for example to have the display asleep during night time, no beep commands will be send to the Atom Echo device. We don't want sounds during the night or other moments of silence. To control the sound of the M5Dial itself, there is the global variable spkr_on. Default state of this flag: false, because we use the external M5Atom Echo device:

```
79 bool spkr_on = false;
```

M5Dial reset:

Pressing the M5Dial button (of the display) will cause a software reset.

On reset the Arduino Sketch will try to connect to the WiFi Access Point of your choice (set in secret.h). 
The sketch will connect to a NTP server of your choice. In this version the sketch uses a ```NTP polling system```. 
The following define sets the NTP polling interval time:

```
53 #define CONFIG_LWIP_SNTP_UPDATE_DELAY  15 * 60 * 1000 // = 15 minutes
```

At the moment of a NTP Time Synchronization, the text "TS" will be shown in the middle of the toprow of the display.
The sketch will also send a digital impulse via GROVE PORT B of the M5Dial, pin 1 (GROVE white wire).
The internal RTC of the M5Dial device will be set to the NTP datetime stamp with the local time for the current Timezone.
Next the sketch will display time zone name, timezone offset from UTC, date and time of the current Timezone.

In the M5Dial sketch is pre-programmed a map (dictionary), name ```zones_map```. This map contains six timezones:

```
    zones_map[0] = std::make_tuple("Asia/Tokyo", "JST-9");
    zones_map[1] = std::make_tuple("America/Kentucky/Louisville", "EST5EDT,M3.2.0,M11.1.0");
    zones_map[2] = std::make_tuple("America/New_York", "EST5EDT,M3.2.0,M11.1.0");
    zones_map[3] = std::make_tuple("America/Sao_Paulo", "<-03>3");
    zones_map[4] = std::make_tuple("Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3");
    zones_map[5] = std::make_tuple("Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3");
```

 After reset of the M5Dial the sketch will load from the file ```secret.h``` the values of ```SECRET_NTP_TIMEZONE``` and ```SECRET_NTP_TIMEZONE_CODE```, 
 and replaces the first record in the map ```zones_map``` with these values from secret.h.

M5Dial Debug output:

In the sketch file of the M5Dial, added a global variable ```my_debug```. The majority of monitor output I made conditionally controlled by this new ```my_debug```.
See the difference in monitor output in the two monitor_output.txt files.

File secret.h:

Update the file secret.h as far as needed:
```
 a) your WiFi SSID in SECRET_SSID;
 b) your WiFi PASSWORD in SECRET_PASS;
 c) your timezone in SECRET_NTP_TIMEZONE, for example: Europe/Lisbon;
 d) your timezone code in SECRET_NTP_TIMEZONE_CODE, for example: WET0WEST,M3.5.0/1,M10.5.0;
 e) the name of the NTP server of your choice in SECRET_NTP_SERVER_1, for example: 2.pt.pool.ntp.org.
```

The M5Atom Echo part:

After applying power to the M5 Echo Atom device, the sketch will wait for a "beep command" impulse on the GROVE PORT pin1, sent by the M5Dial device, or a button press of the button on top of the M5Atom Echo device. Upon pressing the button or receiving a "beep command", a double tone sound will be produced and the RGB Led wil be set to GREEN at the start of the beeps. After the beeps have finished, the RGB Led will be set to RED.

The sketch ```M5Atom_EchoSPKR_beep_on_command_M5Dial.ino``` uses two files: ```AntomEchoSPKR.h``` and ```AntomEchoSPKR.cpp```,
which last two files define the ```class ATOMECHOSPKR```, used by the sketch.
The sketch has functionality to make the M5Atom Echo device to "listen" on Pin1 of the GROVE PORT for a digital impulse (read: beep command)
sent by the M5Dial at moment of a NTP Time Synchronization.

Update: 2024-10-09: added functionality to switch sound ON/off by double press of the button. If sound is off, the RGB Led will show BLUE color.
If sound is ON (default), the RGB Led will show RED color.


Docs:

```
M5Dial_Monitor_output.txt
M5AtomEcho_Monitor_output.txt
```

Images: 

Images taken during the sketch was running are in the folder ```images```.

Links to product pages of the hardware used:

- M5Stack M5Dial [info](https://docs.m5stack.com/en/core/M5Dial);
- M5Stack M5Atom Echo [info](https://docs.m5stack.com/en/atom/atomecho);
- M5Stack GROVE Cable [info](https://docs.m5stack.com/en/accessory/cable/grove_cable)

If you want a 3D Print design of a stand for the M5Dial, see the post of Cyril Ed on the Printables website:
- [Stand for M5Dial](https://www.printables.com/model/614079-m5stactk-dial-stand).
  I successfully downloaded the files and sent them to a local electronics shop that has a 3D printing service.
  See the images.
  
