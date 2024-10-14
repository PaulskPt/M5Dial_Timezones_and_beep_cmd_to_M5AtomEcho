/*
*  
*  M5Dial_Timezones_and_M5Echo_with_RFID.ino  Version 4.0
*  Test sketch for M5Stack M5Dial with 240 x 240 pixels
*  This sketch displays in sequence six different timezones.
*  Version 4.0 uses NTP automatic polling.
*  by @PaulskPt 2024-10-05
*  License: MIT
*
*  Example:
*  https://randomnerdtutorials.com/esp32-ntp-timezones-daylight-saving/
   #:~:text=configTzTime(%E2%80%9CCET-1CEST,M3.5.0/03,M10.5.0/03%E2%80%9D,%20ntp.c_str());%20then%20it%20works%20perfect
*
*  Update 2024-10-11: added functionality to use RFID card to put asleep or awake the display.
*  This functionality depends on the state of the global variable: use_rfid.
*  The ID number of the card that will be accepted needs to be in hexadecimal format, lower case, in file secret.h. 
*  Name: SECRET_MY_RFID_TAG_NR_HEX.
*
*/

#include <M5Dial.h>
#include <M5Unified.h>
#include <M5GFX.h>

#include <esp_sntp.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <stdlib.h>   // for putenv
#include <time.h>
#include <DateTime.h> // See: /Arduino/libraries/ESPDateTime/src
//#include <Free_Fonts.h>
//#include <driver/adc.h> // Obscelete? Not found in Arduino_esp32
//#include <FastLED.h>
#include "secret.h"
// Following 8 includes needed for creating, changing and using map time_zones
#include <iostream>
#include <map>
#include <memory>
#include <array>
#include <string>
#include <tuple>
#include <iomanip> // For setFill and setW
#include <cstring> // For strcpy
#include <sstream>  // Used in ck_RFID()

//namespace {  // anonymous namespace (also known as an unnamed namespace)

#define WIFI_SSID     SECRET_SSID // "YOUR WIFI SSID NAME"
#define WIFI_PASSWORD SECRET_PASS //"YOUR WIFI PASSWORD"
#define NTP_TIMEZONE  SECRET_NTP_TIMEZONE // for example: "Europe/Lisbon"
#define NTP_TIMEZONE_CODE  SECRET_NTP_TIMEZONE_CODE // for example: "WET0WEST,M3.5.0/1,M10.5.0"
#define NTP_SERVER1   SECRET_NTP_SERVER_1 // for example: "0.pool.ntp.org"
#define NTP_SERVER2   "1.pool.ntp.org"
#define NTP_SERVER3   "2.pool.ntp.org"
#define MY_RFID_TAG_HEX SECRET_MY_RFID_TAG_NR_HEX

#ifdef CONFIG_LWIP_SNTP_UPDATE_DELAY   // Found in: Component config > LWIP > SNTP
#undef CONFIG_LWIP_SNTP_UPDATE_DELAY
#endif

#define CONFIG_LWIP_SNTP_UPDATE_DELAY  15 * 60 * 1000 // = 15 minutes (15 seconds is the minimum). Original setting: 3600000  // 1 hour

// 4-PIN connector type HY2.0-4P
#define PORT_B_GROVE_OUT_PIN 2
#define PORT_B_GROVE_IN_PIN  1

#define MY_WHITE 3
#define MY_BLACK 5

int DISP_FG = TFT_ORANGE;
int DISP_BG = TFT_BLACK;
int disp_brightness = 50;

std::string elem_zone;
std::string elem_zone_code;
std::string elem_zone_code_old;
bool zone_has_changed = false;

bool my_debug = false;
/* use_rfid flag:
*  if true: use RFID card to put asleep/awake the display. 
*  if false: use touches  to put asleep/awake the display.
*/
bool use_rfid = true;  
bool i_am_asleep = false;
unsigned int touch_cnt = 0;
unsigned long touch_start_t = 0L;

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50; // 50 milliseconds debounce delay
bool lastTouchState = false;
bool touchState = false;

bool display_on = true;
bool spkr_on = false;
struct tm timeinfo = {};
bool use_timeinfo = true;
std::tm* tm_local = {};
tm RTCdate;

int dw;
int dh;

// M5Dial screen 1.28 Inch 240x240px. Display device: GC9A01
static constexpr const int hori[] = {0, 60, 120, 180, 220};
static constexpr const int vert[] = {0, 60, 120, 180, 220};  // was: {30, 60, 90}

static constexpr const char* const wd[7] = {"Sun", "Mon", "Tue", "Wed",
                                            "Thu", "Fri", "Sat"};
// M5Dial touch driver: FT3267

unsigned long zone_chg_start_t = millis();
bool TimeToChangeZone = false;
int Done = 0;

uint8_t FSM = 0;  // Store the number of key presses
int connect_try = 0;
int max_connect_try = 10;

volatile bool buttonPressed = false;

int zone_idx = 0;
const int zone_max_idx = 6;
// I know Strings are less memory efficient than char arrays, 
// however I have less "headache" by using Strings. E.g. the string.indexOf()
// and string.substring() functions make work much easier!

//} // end of namespace

/* For button presses: 
*  See:      https://github.com/m5stack/M5Unified/blob/master/src/utility/Button_Class.hpp 
*  See also: https://community.m5stack.com/topic/1731/detect-longpress-button-but-not-only-when-released-button
*/

void spkr(void)
{
  // Speaker test
  // Play two tones alternating
  // Use M5Dial.Speaker.beep or M5Dial.Speaker.tone
  M5Dial.Speaker.setVolume(8); // (range is 0 to 10)
  for (int j=0; j < 2; j++)
  {
    M5Dial.Speaker.tone(10000, 100);
    delay(1000);
    M5Dial.Speaker.tone(4000, 20);
    delay(1000);
  }
}

std::map<int, std::tuple<std::string, std::string>> zones_map;

void create_maps(void) 
{
  zones_map[0] = std::make_tuple("Asia/Tokyo", "JST-9");
  zones_map[1] = std::make_tuple("America/Kentucky/Louisville", "EST5EDT,M3.2.0,M11.1.0");
  zones_map[2] = std::make_tuple("America/New_York", "EST5EDT,M3.2.0,M11.1.0");
  zones_map[3] = std::make_tuple("America/Sao_Paulo", "<-03>3");
  zones_map[4] = std::make_tuple("Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3");
  zones_map[5] = std::make_tuple("Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3");

  // Iterate and print the elements
  /*
  std::cout << "create_maps(): " << std::endl;
  for (const auto& pair : zones_map)
  {
    std::cout << "Key: " << pair.first << ". Values: ";
    std::cout << std::get<0>(pair.second) << ", ";
    std::cout << std::get<1>(pair.second) << ", ";
    std::cout << std::endl;
  }
  */
}

void map_replace_first_zone(void)
{
  bool ret = false;
  int tmp_zone_idx = 0;
  std::string elem_zone_original;
  std::string elem_zone_code_original;
  elem_zone  = std::get<0>(zones_map[tmp_zone_idx]);
  elem_zone_code  = std::get<1>(zones_map[tmp_zone_idx]);
  std::string elem_zone_check;
  std::string elem_zone_code_check;
  
  elem_zone_original = elem_zone; // make a copy
  elem_zone_code_original = elem_zone_code;
  elem_zone = NTP_TIMEZONE;
  elem_zone_code = NTP_TIMEZONE_CODE;
  zones_map[0] = std::make_tuple(elem_zone, elem_zone_code);
  // Check:
  elem_zone_check  = std::get<0>(zones_map[tmp_zone_idx]);
  elem_zone_code_check  = std::get<1>(zones_map[tmp_zone_idx]);

  /*
  std::cout << "Map size before erase: " << myMap.size() << std::endl;

  for (int i = 0; i < 2; i++
  {
    // Get iterator to the element with key i
    auto it = zones_map.find(i);
    if (it != zones_map.end())
    {
        zones_map.erase(it);
    }
  std::cout << "Map size after erase: " << myMap.size() << std::endl;
  */
 
  if (my_debug)
  {
    std::cout << "map_replace_first_zone(): successful replaced the first record of the zone_map:" << std::endl;
    
    std::cout << "zone original: \"" << elem_zone_original.c_str() << "\""
      << ", replaced by zone: \"" << elem_zone_check.c_str()  << "\""
      << " (from file secrets.h)" 
      << std::endl;
    
    std::cout << "zone code original: \"" <<  elem_zone_code_original.c_str() << "\""
      << ", replaced by zone code: \"" << elem_zone_code_check.c_str() << "\""
      << std::endl;
  }
}

/* Show or remove NTP Time Sync notification on the middle of the top of the display */
void ntp_sync_notification_txt(bool show)
{
  if (show)
  {
    std::shared_ptr<std::string> TAG = std::make_shared<std::string>("mtp_sync_notification_txt(): ");
    const char txt[] = "beep command to the M5 Atom Echo device";
    M5Dial.Display.setCursor(dw/2-25, 20);
    M5Dial.Display.setTextColor(GREEN, BLACK);
    M5Dial.Display.print("TS");
    delay(500);
    M5Dial.Display.setCursor(dw/2-25, 20);      // Try to overwrite in black instead of wiping the whole top area
    M5Dial.Display.setTextColor(BLACK, BLACK);
    M5Dial.Display.print("TS");
    M5Dial.Display.setTextColor(DISP_FG, DISP_BG);

    
    /* Only send command to make sound by the M5 Atom Echo when the display is on,
       because when the user has put the display off, he/she probably wants to go to bed/sleep.
       In that case we don't want nightly sounds!
    */

    if (display_on)
    {
      std::cout << *TAG << "Sending " << (txt) << std::endl;
      send_cmd_to_AtomEcho(); // Send a digital signal to the Atom Echo to produce a beep
    }
    else
      std::cout << *TAG << "Display is Off. Not sending " << (txt) << std::endl;
  }
  else
  {
    //M5Dial.Display.fillRect(dw/2-25, 15, 50, 25, BLACK);
    M5Dial.Display.fillRect(0, 0, dw-1, 55, BLACK);
  }
}
/*
  The getLocalTime() function is often used in microcontroller projects, such as with the ESP32, 
  to retrieve the current local time from an NTP (Network Time Protocol) server. 
  Here’s a simple example of how you can use this function with the ESP32:
*/
bool poll_NTP(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("poll_NTP(): ");
  bool ret = false;
  
  if(getLocalTime(&timeinfo))
  {
    std::cout << "getLocalTime(&timeinfo): timeinfo = " << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << std::endl;
    ret = true;
  }
  else
  {
    std::cout << *TAG << "Failed to obtain time " << std::endl;
    M5.Display.clear(BLACK);
    M5Dial.Display.setCursor(hori[1], vert[2]);
    M5Dial.Display.print(F("Failed to obtain time"));
    //display.waitDisplay();
    //M5Dial.Display.pushSprite(&display, 0, (display.height() - M5Dial.Display.height()) >> 1);
    delay(3000);
  }
  return ret;
}

bool is_tm_empty(const std::tm& timeinfo)
{
  return timeinfo.tm_sec == 0 && timeinfo.tm_min  == 0 && timeinfo.tm_hour  == 0 &&
        timeinfo.tm_mday == 0 && timeinfo.tm_mon  == 0 && timeinfo.tm_year  == 0 &&
        timeinfo.tm_wday == 0 && timeinfo.tm_yday == 0 && timeinfo.tm_isdst == 0;
}

void time_sync_notification_cb(struct timeval *tv)
{
    std::shared_ptr<std::string> TAG = std::make_shared<std::string>("time_sync_notification_cb(): ");
    if (my_debug)
    {
      if (tv != nullptr) {
          std::cout << *TAG << "Parameter *tv, tv-<tv_sec (Seconds): " << tv->tv_sec << ", tv->tv_usec (microSeconds): " << tv->tv_usec << std::endl;
      } else {
          std::cerr << *TAG << "Invalid timeval pointer" << std::endl;
      }
    }

    if (spkr_on == true)
    {
      spkr();
    }
  
    time_t t = time(NULL);
    std::cout << *TAG << "time synchronized at time (UTC): " << asctime(gmtime(&t)) << std::flush;  // prevent a 2nd LF. Do not use std::endl
    ntp_sync_notification_txt(true);
}

void sntp_initialize() {
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("sntp_initialize(): ");
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, NTP_SERVER1);
  sntp_set_sync_interval(CONFIG_LWIP_SNTP_UPDATE_DELAY);
  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  sntp_init();
  
  if (my_debug)
  {
    std::cout << *TAG << "sntp initialized" << std::endl;
    std::cout << *TAG << "sntp set to polling mode" << std::endl;
  }
  std::cout << *TAG << "sntp polling interval: " << std::to_string(CONFIG_LWIP_SNTP_UPDATE_DELAY/60000) << " Minute(s)" << std::endl;
}

void setTimezone(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("setTimezone(): ");
  elem_zone = std::get<0>(zones_map[zone_idx]);
  elem_zone_code = std::get<1>(zones_map[zone_idx]);
  if (elem_zone_code != elem_zone_code_old)
  {
    elem_zone_code_old = elem_zone_code;
    const char s1[] = "has changed to: ";
    zone_has_changed = true;
    //std::cout << *TAG << "Sending cmd to beep to Atom Echo" << std::endl;
    //send_cmd_to_AtomEcho(); // Send a digital signal to the Atom Echo to produce a beep
    if (my_debug)
    {
      std::cout << *TAG << "Timezone " << s1 << "\"" << elem_zone.c_str() << "\"" << std::endl;
      std::cout << *TAG << "Timezone code " << s1 << "\"" << elem_zone_code.c_str() << "\"" << std::endl;
    }
  }

  /*
    See: https://docs.espressif.com/projects/esp-idf/en/v5.0.2/esp32/api-reference/system/system_time.html#sntp-time-synchronization
    Call setenv() to set the TZ environment variable to the correct value based on the device location. 
    The format of the time string is the same as described in the GNU libc documentation (although the implementation is different).
    Call tzset() to update C library runtime data for the new timezone.
  */
  // std::cout << *TAG << "Setting Timezone to \"" << (elem_zone_code.c_str()) << "\"\n" << std::endl;
  setenv("TZ",elem_zone_code.c_str(),1);
  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  delay(500);
  tzset();
  delay(500);

  if (my_debug)
  {
    // Check:
    std::cout << *TAG << "check environment variable TZ = \"" << getenv("TZ") << "\"" << std::endl;
  }
}

bool initTime(void)
{
  bool ret = false;
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("initTime(): ");
  elem_zone = std::get<0>(zones_map[zone_idx]);
  elem_zone_code = std::get<1>(zones_map[zone_idx]);

  if (my_debug)
  {
    std::cout << *TAG << "Setting up time" << std::endl;
    std::cout << "zone       = \"" << elem_zone.c_str() << "\"" << std::endl;
    std::cout << "zone code  = \"" << elem_zone_code.c_str() << "\"" << std::endl;
    std::cout 
      << "NTP_SERVER1 = \"" << NTP_SERVER1 << "\", " 
      << "NTP_SERVER2 = \"" << NTP_SERVER2 << "\", "
      << "NTP_SERVER3 = \"" << NTP_SERVER3 << "\""
      << std::endl;
  }

  /*
  * See answer from: bperrybap (March 2021, post #6)
  * on: https://forum.arduino.cc/t/getting-time-from-ntp-service-using-nodemcu-1-0-esp-12e/702333/5
  */

#ifndef ESP32
#define ESP32 (1)
#endif

/*
char* my_env = getenv("TZ");

// See: /Arduino/libraries/ESPDateTime/src/DateTime.cpp, lines 76-80
#if defined(ESP8266)
  configTzTime(elem_zone_code.c_str(), NTP_SERVER1, NTP_SERVER2, NTP_SERVER3); 
#elif defined(ESP32)
  //configTzTime(elem_zone_code.c_str(), NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);  // This one is use for the M5Stack Atom Matrix
  std::cout << "initTime(). Setting configTzTime to: \"" << std::to_string(my_env).c_str() << "\"" << std::endl;
  configTzTime(, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);  // This one is use for the M5Stack Atom Matrix
#endif

  configTime(0, 3600, NTP_SERVER1);
*/
  while (!getLocalTime(&timeinfo, 1000))
  {
    std::cout << "." << std::flush;
    delay(1000);
  };

  std::cout << "\nNTP Connected. " << std::endl;

  if (is_tm_empty(timeinfo))
  {
    std::cout << *TAG << "Failed to obtain datetime from NTP" << std::endl;
  }
  else
  {
    if (my_debug)
    {
      std::cout << *TAG << "Got this datetime from NTP: " << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << std::endl;
    }
    // Now we can set the real timezone
    setTimezone();

    ret = true;
  }
  return ret;
}

bool set_RTC(void)
{
  bool ret = false;
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("set_RTC(): ");
  struct tm my_timeinfo;
  if(!getLocalTime(&my_timeinfo))
  {
    std::cout << *TAG << "Failed to obtain time" << std::endl;
    return ret;
  }

  if (my_timeinfo.tm_year + 1900 > 1900)
  {
    //                            YYYY  MM  DD      hh  mm  ss
    //M5Dial.Rtc.setDateTime( { { 2021, 12, 31 }, { 12, 34, 56 } } );
    M5Dial.Rtc.setDateTime( {{my_timeinfo.tm_year + 1900, my_timeinfo.tm_mon + 1, my_timeinfo.tm_mday}, {my_timeinfo.tm_hour, my_timeinfo.tm_min, my_timeinfo.tm_sec}} );

    if (my_debug)
    {
      std::cout << *TAG << "internal RTC has been set to: " 
      << std::to_string(RTCdate.tm_year) << "-" 
      << std::setfill('0') << std::setw(2) << std::to_string(my_timeinfo.tm_mon) << "-"
      << std::setfill('0') << std::setw(2) << std::to_string(my_timeinfo.tm_mday) << " ("
      << wd[my_timeinfo.tm_wday] << ") "
      << std::setfill('0') << std::setw(2) << std::to_string(my_timeinfo.tm_hour) << ":"
      << std::setfill('0') << std::setw(2) << std::to_string(my_timeinfo.tm_min) << ":"
      << std::setfill('0') << std::setw(2) << std::to_string(my_timeinfo.tm_sec) << std::endl;
    }
    ret = true;
  }
  return ret;
}

void poll_RTC(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("poll_RTC(): ");
  time_t t = time(NULL);
  delay(500);

  /// ESP32 internal timer
  // struct tm timeinfo;
  t = std::time(nullptr);

  if (my_debug)
  {
    std::tm* tm = std::gmtime(&t);  // for UTC.
    std::cout << std::dec << *TAG << "ESP32 UTC : " 
      << std::setw(4) << (tm->tm_year+1900) << "-"
      << std::setfill('0') << std::setw(2) << (tm->tm_mon+1) << "-"
      << std::setfill('0') << std::setw(2) << (tm->tm_mday) << " ("
      << wd[tm->tm_wday] << ") "
      << std::setfill('0') << std::setw(2) << (tm->tm_hour) << ":"
      << std::setfill('0') << std::setw(2) << (tm->tm_min)  << ":"
      << std::setfill('0') << std::setw(2) << (tm->tm_sec)  << std::endl;
  }

  if (my_debug)
  {
    // std::tm* tm_local  Global var!
    //tm_local = std::localtime(&t);  // for local timezone.
    tm_local = localtime(&t);
    elem_zone = std::get<0>(zones_map[zone_idx]);
    std::cout << std::dec << *TAG << "ESP32 " << elem_zone             << ": " 
      << std::setw(4)                      << (tm_local->tm_year+1900) << "-"
      << std::setfill('0') << std::setw(2) << (tm_local->tm_mon+1)     << "-"
      << std::setfill('0') << std::setw(2) << (tm_local->tm_mday)      << " ("
      << wd[tm_local->tm_wday] << ") "
      << std::setfill('0') << std::setw(2) << (tm_local->tm_hour)      << ":"
      << std::setfill('0') << std::setw(2) << (tm_local->tm_min)       << ":"
      << std::setfill('0') << std::setw(2) << (tm_local->tm_sec) << std::endl;
  }
}

void printLocalTime()  // "Local" of the current selected timezone!
{ 
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("printLocalTime(): ");
  if(!getLocalTime(&timeinfo)){
    std::cout << "Failed to obtain time" << std::endl;
    return;
  }
  if (my_debug)
    std::cout << *TAG << "Timezone: " << elem_zone.c_str() << ", datetime: " << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << std::endl;
}

/* This function uses global var timeinfo to display date and time data.
   The function also displays timezone info.
   It also calls ck_Btn() four times to increase a "catch" of BtnA keypress.
*/
void disp_data(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("disp_data(): ");
  int disp_data_delay = 1000;
  // For unitOLED
  int scrollstep = 2;

  elem_zone  = std::get<0>(zones_map[zone_idx]);
  std::string copiedString, copiedString2;
  std::string part1, part2, part3, part4;
  std::string partUS1, partUS2;
  int index, index2, index3 = -1;
  copiedString =  elem_zone;
  // Search for a first forward slash (e.g.: "Europe/Lisbon")
  index = copiedString.find('/');
  index3 = copiedString.find('_'); // e.g. in "New_York" or "Sao_Paulo"

  if (ck_touch())
    return;
  if (ck_Btn())
    return;

  if (index3 >= 0)
  {
    partUS1 = copiedString.substr(0, index3);
    partUS2 = copiedString.substr(index3+1);
    copiedString = partUS1 + " " + partUS2;  // replaces the found "_" by a space " "
  }
  if (index >= 0)
  {
    part1 = copiedString.substr(0, index);
    part2 = copiedString.substr(index+1);

    copiedString2 = part2.c_str();

    // Search for a second forward slash  (e.g.: "America/Kentucky/Louisville")
    index2 = copiedString2.find('/'); 
    if (index2 >= 0)
    {
      part3 = copiedString2.substr(0, index2);
      part4 = copiedString2.substr(index2+1);
    }
  }
  struct tm my_timeinfo;
  if(!getLocalTime(&my_timeinfo))
  {
    std::cout << "Failed to obtain time" << std::endl;
    return;
  }
  // =========== 1st view =================
if (ck_touch())
    return;
 if (ck_Btn())
    return;

  M5.Display.clear(BLACK);
  M5Dial.Display.setTextColor(DISP_FG, DISP_BG);
  if (index >= 0 && index2 >= 0)
  {
    M5Dial.Display.setCursor(hori[1], vert[1]+5);
    M5Dial.Display.print(part1.c_str());
    M5Dial.Display.setCursor(hori[1], vert[2]-2);
    M5Dial.Display.print(part3.c_str());
    M5Dial.Display.setCursor(hori[1], vert[3]-10);
    M5Dial.Display.print(part4.c_str());
  }
  else if (index >= 0)
  {
    M5Dial.Display.setCursor(hori[1], vert[1]+5);
    M5Dial.Display.print(part1.c_str());
    M5Dial.Display.setCursor(hori[1], vert[2]);
    M5Dial.Display.print(part2.c_str());
  }
  else
  {
    M5Dial.Display.setCursor(hori[1], vert[1]+5);
    M5Dial.Display.print(copiedString.c_str());
  }
  if (ck_touch()) return;
  delay(disp_data_delay);
  if (TimeToChangeZone)
    return;
  // =========== 2nd view =================
 if (ck_Btn())
    return;

  M5.Display.clear(BLACK);
  M5Dial.Display.setTextColor(DISP_FG, DISP_BG);
  M5Dial.Display.setCursor(hori[1], vert[1]+5);
  M5Dial.Display.print("Zone");
  M5Dial.Display.setCursor(hori[1], vert[2]);
  M5Dial.Display.print(&my_timeinfo, "%Z %z");
  if (ck_touch()) return;
  delay(disp_data_delay);
  if (TimeToChangeZone)
    return;
  // =========== 3rd view =================
 if (ck_Btn())
    return;

  M5.Display.clear(BLACK);
  M5Dial.Display.setTextColor(DISP_FG, DISP_BG);
  M5Dial.Display.setCursor(hori[1], vert[1]+5);
  M5Dial.Display.print(&my_timeinfo, "%A");  // Day of the week
  M5Dial.Display.setCursor(hori[1], vert[2]-2);
  M5Dial.Display.print(&my_timeinfo, "%B %d");
  M5Dial.Display.setCursor(hori[1], vert[3]-10);
  M5Dial.Display.print(&my_timeinfo, "%Y");
  if (ck_touch()) return;
  delay(disp_data_delay);
  if (TimeToChangeZone)
    return;
   // =========== 4th view =================
   if (ck_Btn())
    return;


  M5.Display.clear(BLACK);
  M5Dial.Display.setTextColor(DISP_FG, DISP_BG);
  M5Dial.Display.setCursor(hori[1], vert[1]+5);
  M5Dial.Display.print(&my_timeinfo, "%H:%M:%S");
  M5Dial.Display.setCursor(hori[1], vert[2]);
  
  ntp_sync_notification_txt(false);

  if (index2 >= 0)
  {
    M5Dial.Display.printf("in %s\n", part4.c_str());
    // std::cout << *TAG << "part4 = " << part4.c_str() << ", index2 = " << index2 << std::endl;
  }
  else
    M5Dial.Display.printf("in %s\n", part2.c_str());
  if (ck_touch()) return;
  delay(disp_data_delay);
}

void disp_msg(String str)
{
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setBrightness(200);  // Make more brightness than normal
  M5Dial.Display.clear();
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setTextColor(TFT_NAVY); // (BLUE);
  M5Dial.Display.drawString(str, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
  //M5Dial.Display.drawString(str2, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
  delay(6000);
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setBrightness(disp_brightness); // Restore brightness to normal

  M5Dial.Display.clear();
}

/* See: https://github.com/m5stack/m5-docs/blob/master/docs/en/api/lcd.md */
void chg_display_clr(int color)
{
  if (color >= 0 && color <= 6)
    M5Dial.Display.fillScreen(color);  // a kindof override 
  else
  {
    switch (FSM) 
    {
      case 0:
          M5Dial.Display.fillScreen(TFT_GREEN); // Change GREEN to any color you want
          break;
      case 1:
          M5Dial.Display.fillScreen(TFT_RED);
          break;
      case 2:
          M5Dial.Display.fillScreen(TFT_NAVY); // (BLUE);
          break;
      case 3:
          M5Dial.Display.fillScreen(TFT_WHITE);
          break;
      case 4:
          M5Dial.Display.fillScreen(TFT_MAGENTA);
          break;
      case 5:
          M5Dial.Display.fillScreen(TFT_ORANGE);
          break;
      case 6:
          M5Dial.Display.fillScreen(TFT_BLACK);
          break;
      default:
          break;
    }
  }
}

bool connect_WiFi(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("connect_WiFi(): ");
  bool ret = false;
  if (my_debug)
  {
    std::cout << std::endl << "WiFi: " << std::flush;
  }
  WiFi.begin( WIFI_SSID, WIFI_PASSWORD );

  for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
  {
    if (my_debug)
      std::cout << "." << std::flush;
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) 
  {
    ret = true;
    if (my_debug)
      std::cout << "\r\nWiFi Connected to: " << WIFI_SSID << std::endl;
    else
      std::cout << "\r\nWiFi Connected" << std::endl;

    if (my_debug)
    {
      IPAddress ip;
      ip = WiFi.localIP();
      // Convert IPAddress to string
      String ipStr = ip.toString();
      std::cout << "IP address: " << ipStr.c_str() << std::endl;

      byte mac[6];
      WiFi.macAddress(mac);

      // Allocate a buffer of 18 characters (12 for MAC + 5 colons + 1 null terminator)
      char* mac_buff = new char[18];

      // Create a shared_ptr to manage the buffer with a custom deleter
      std::shared_ptr<char> bufferPtr(mac_buff, customDeleter);

      std::cout << *TAG << std::endl;

      std::cout << "MAC: ";
      for (int i = 0; i < 6; ++i)
      {
        if (i > 0) std::cout << ":";
        std::cout << std::hex << (int)mac[i];
      }
      std::cout << std::dec << std::endl;
    }
  }
  else
  {
    std::cout << "\r\n" << "WiFi connection failed." << std::endl;
  }
  return ret;
}

void customDeleter(char* buffer) {
    // std::cout << "\nCustom deleter called\n" << std::endl;
    delete[] buffer;
}

void getID(void)
{
  uint64_t chipid_EfM = ESP.getEfuseMac(); // The chip ID is essentially the MAC address 
  char chipid[13] = {0};
  sprintf( chipid,"%04X%08X", (uint16_t)(chipid_EfM>>32), (uint32_t)chipid_EfM );
  std::cout << "\nESP32 Chip ID = " << chipid << std::endl;
  std::cout << "chipid mirrored (same as M5Burner MAC): " << std::flush;
  // Mirror MAC address:
  for (uint8_t i = 10; i >= 0; i-=2)  // 10, 8. 6. 4. 2, 0
  {
    // bytes 10, 8, 6, 4, 2, 0
    // bytes 11, 9, 7. 5, 3, 1
    std::cout << chipid[i] << chipid[i+1] << std::flush;
    if (i > 0)
      std::cout << ":" << std::flush;
    if (i == 0)  // Note: this needs to be here. Yes, it is strange but without it the loop keeps on running.
      break;     // idem.
  }
}

bool ck_Btn()
{
  M5Dial.update();
  //if (M5Dial.BtnA.isPressed())
  if (M5Dial.BtnA.wasPressed())  // 100 mSecs
  {
    buttonPressed = true;
    return true;
  }
  return false;
}

bool ck_touch(void)
{
  // std::shared_ptr<std::string> TAG = std::make_shared<std::string>("ck_touch(): ");
  bool ret = false;

  /* MS CoPilot: 
     In the context of the M5Stack touch screen, 
     the state “flick” refers to a quick, 
     swiping motion detected on the touch screen. 
     This is typically recognized when the touch point 
     moves a certain distance within a short period, 
     meeting the predefined flick threshold
  
  static constexpr const char* state_name[16] = {
    "none", "touch", "touch_end", "touch_begin",
    "___",  "hold",  "hold_end",  "hold_begin",
    "___",  "flick", "flick_end", "flick_begin",
    "___",  "drag",  "drag_end",  "drag_begin"};
  */

  M5Dial.update();

  auto t = M5Dial.Touch.getDetail();

  bool currentTouchState = t.state; // M5.Touch.ispressed();

  if (currentTouchState != lastTouchState)
  {
      lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    if (currentTouchState != touchState)
    {
      touchState = currentTouchState;

      if (touchState)
      {
        ret = true;
        touch_cnt++;  // increase global var
        // The maximum value of an unsigned int is 4294967295
        if (touch_cnt >= (4294967295 - 1))
          touch_cnt = 4294967295 - 1; // keep it below the maximum. Prevent an overflow
        if (touch_start_t == 0)
          touch_start_t = millis();
      }
    }
  }
  lastTouchState = currentTouchState;
  return ret;
}

std::string intToHex(int value) {
    std::stringstream ss;
    ss << std::hex << value;
    return ss.str();
}

bool ck_RFID(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("ck_RFID(): ");
  bool lCardIsOK = false;
  char txt1[] = "Hi there, your RFID TAG has ";
  char txt2[] = "been recognized. ";

  M5Dial.Rfid.begin();
  delay(1000);
  if (M5Dial.Rfid.PICC_IsNewCardPresent() &&
      M5Dial.Rfid.PICC_ReadCardSerial()) 
  {
    M5Dial.Display.clear();
    uint8_t piccType = M5Dial.Rfid.PICC_GetType(M5Dial.Rfid.uid.sak);
    if (my_debug)
    {
      std::cout << *TAG << "PICC type: " << (M5Dial.Rfid.PICC_GetTypeName(piccType)) << std::endl;
    }
    // Check is the PICC of Classic MIFARE type
    if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
          piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
          piccType != MFRC522::PICC_TYPE_MIFARE_4K)
    {
      std::cout << *TAG << "Your tag is not of type MIFARE Classic." << std::endl;
      M5Dial.Display.fillScreen(TFT_BLACK);
      M5Dial.Display.clear();
      M5Dial.Display.setTextDatum(middle_center);
      M5Dial.Display.setTextColor(TFT_RED);
      M5Dial.Display.drawString("card not", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 - 50);
      M5Dial.Display.drawString("supported", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
      return false;
    }
    
    std::string uid = "";
    std::string sMyRFID(MY_RFID_TAG_HEX);
    int le = sMyRFID.length();

    for (byte i = 0; i < M5Dial.Rfid.uid.size; i++) // Output the stored UID data.
    {  
      std::string hexString1 = intToHex(M5Dial.Rfid.uid.uidByte[i]);
      uid += hexString1;
    }

    if (my_debug)
      std::cout << *TAG << "uid     = 0x" << (uid) << std::endl;

    /*
    M5Dial.Display.drawString(M5Dial.Rfid.PICC_GetTypeName(piccType), M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 - 30);
    M5Dial.Display.drawString("card id:", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
    M5Dial.Display.drawString(uid, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 + 30);
    */

    if (my_debug)
    {
      std::cout << std::endl << *TAG << "uid     = 0x" << (uid.c_str())     << std::endl;
      std::cout <<              *TAG << "sMyRFID = 0x" << (sMyRFID.c_str()) << std::endl;
      std::cout << *TAG << "length of sMyRFID = " << (le) << std::endl;
    }
    bool lCkEq = true;

    /* Check if the presented RFID card has the same ID as that of the ID in secret.h */
    for (int i = 0; i < le; i++)
    {
      if (uid[i] != sMyRFID[i])
      {
        lCkEq = false; // RFID Card not OK
        break;
      }
    }
    
    lCardIsOK = lCkEq;

    if (lCardIsOK)
    {
      std::cout << *TAG << (txt1) << (txt2) << std::endl;
      if (spkr_on)
      {
        M5Dial.Speaker.tone(8000, 20);
      }
    }
    else
    {
      std::cout << *TAG << (txt1) << "not " << (txt2) << "Check your card!" << std::endl;
    }
  }
  else
  {
    if (my_debug)
    {
      std::cout << *TAG << "No RFID card presented!" << std::endl;
    }
  }
  M5Dial.Rfid.PICC_HaltA();
  M5Dial.Rfid.PCD_StopCrypto1();

  return lCardIsOK;
}

/* According to MS CoPilot:
   The width of a character in the FreeSans12pt7b font on the M5Stack 
   can vary slightly depending on the specific character. 
   However, on average, 
   each character in this font occupies about 12 pixels horizontally.
   (Source of the CoPilot answer: https://learn.adafruit.com/adafruit-gfx-graphics-library/using-fonts)
  
  Function calc_x_offset calculates the horizontal offset from the left edge of the display,
  based on the received parameter text and the value of the character-width-in-pixels for the given text

  Parameters: const char* t (text), 
              int   ch_width_in_px (the width of an average character in pixels)
*/
int calc_x_offset(const char* t, int ch_width_in_px) {
  int le = strlen(t);
  int char_space = 1;
  int ret = ( dw - ((le * ch_width_in_px) + ((le -1) * char_space) )) / 2;
  /*
  std::cout 
    << "calc_x_offset(\"" 
    << (t) 
    << "\", "
    << (pw)
    << "): ret = (" 
    << (dw) 
    << " - ((" 
    << (le) 
    << " * " 
    << (pw) 
    << ") + (("
    << (le-1) 
    << " * "
    << (char_space) 
    << ") )) / 2 = "
    << (ret) 
    << std::endl;
    */
  return (ret < 0) ? 0 : ret;
}

void start_scrn(void) {
  static const char* txt[] = {"TIMEZONES", "by Paulus", "Github", "@PaulskPt"};
  static const int char_width_in_pixels[] = {16, 12, 12, 14};
  int vert2[] = {0, 60, 90, 120, 150}; 
  int x = 0;

  M5.Display.clear(BLACK);
  M5Dial.Display.setTextColor(RED, BLACK);
  //M5Dial.Display.setFont(&fonts::FreeSans18pt7b);

  for (int i = 0; i < 4; ++i) {
    x = calc_x_offset(txt[i], char_width_in_pixels[i]);
    // Serial.printf("start_scrn(): x = %d\n", x);
    M5Dial.Display.setCursor(x, vert2[i + 1]);
    M5Dial.Display.println(txt[i]);
  }

  //delay(5000);
  M5Dial.Display.setTextColor(DISP_FG, DISP_BG);
  //M5Dial.Display.setFont(&fonts::FreeSans12pt7b); // was: efontCN_14);
}

void send_cmd_to_AtomEcho(void)
{
    digitalWrite(PORT_B_GROVE_IN_PIN, LOW); // Send a HIGH signal
    digitalWrite(PORT_B_GROVE_OUT_PIN, HIGH); // Send a HIGH signal
    delay(100); // Wait for 100 Msec
    digitalWrite(PORT_B_GROVE_OUT_PIN, LOW); // Send a LOW signal
    delay(100); // Wait for 100 Msec
}

void setup(void) 
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("setup(): ");
  M5.begin();  
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;

  M5Dial.begin(cfg); //, true, false);

  M5Dial.Power.begin();

  /*
  * A workaround to prevent some problems regarding 
  * M5Dial.BtnA.IsPressed(), M5Dial.BtnA.IsPressedPressed(), 
  * M5Dial.BtnA,wasPressed() or M5Dial.BtnA.pressedFor(ms).
  * See: https://community.m5stack.com/topic/3955/atom-button-at-gpio39-without-pullup/5
  */
  // adc_power_acquire(); // ADC Power ON

  //attachInterrupt(digitalPinToInterrupt(ck_Btn()), handleButtonPress, FALLING);

  // Pin settings for communication with M5Dial to receive commands from M5Dial
  // commands to start a beep.
  pinMode(PORT_B_GROVE_OUT_PIN, OUTPUT); // Set PORT_B_GROVE_OUT_PIN as an output
  digitalWrite(PORT_B_GROVE_OUT_PIN, LOW); // Turn Idle the output pin
  pinMode(PORT_B_GROVE_IN_PIN, INPUT); // Set PORT_B_GROVE_IN_PIN as an inputf
  digitalWrite(PORT_B_GROVE_IN_PIN, LOW); // Turn Idle the output pin

  Serial.begin(115200);

  std::cout << "\n\n" << *TAG << "M5Stack M5Dial Timezones and M5Atom Echo with RFID test." << std::endl;

  M5Dial.Display.init();
  std::cout << *TAG << "setting display brightness to " << (disp_brightness) << " (range 0-255)" << std::endl;
  M5Dial.Display.setBrightness(disp_brightness);  // 0-255
   dw = M5Dial.Display.width();
   dh = M5Dial.Display.height();
  M5Dial.Display.setRotation(0);

  chg_display_clr(MY_BLACK);
  M5Dial.Display.setTextColor(DISP_FG, DISP_BG);
  

  M5Dial.Display.setColorDepth(1); // mono color
  M5Dial.Display.setFont(&fonts::FreeSans12pt7b); // was: efontCN_14);
  M5Dial.Display.setTextWrap(false);
  M5Dial.Display.setTextSize(1);

  if (my_debug)
    std::cout << *TAG << "M5Stack M5Dial display width = " << std::to_string(dw) << ", height = " << std::to_string(dh) << std::endl;

  start_scrn();

  if (my_debug)
    getID();

  create_maps();  // creeate zones_map

  map_replace_first_zone();

  zone_idx = 0;

  delay(1000);

  /* Try to establish WiFi connection. If so, Initialize NTP, */
  if (connect_WiFi())
  {
    /*
    * See: https://docs.espressif.com/projects/esp-idf/en/v5.0.2/esp32/api-reference/system/system_time.html#sntp-time-synchronization
      See also: https://docs.espressif.com/projects/esp-idf/en/v5.0.2/esp32/api-reference/kconfig.html#config-lwip-sntp-update-delay

      CONFIG_LWIP_SNTP_UPDATE_DELAY
      This option allows you to set the time update period via SNTP. Default is 1 hour.
      Must not be below 15 seconds by specification. (SNTPv4 RFC 4330 enforces a minimum update time of 15 seconds).
      Range:
      from 15000 to 4294967295

      Default value:
      3600000
    
      See: https://github.com/espressif/esp-idf/blob/v5.0.2/components/lwip/include/apps/esp_sntp.h
      SNTP sync status
          typedef enum {
            SNTP_SYNC_STATUS_RESET,         // Reset status.
            SNTP_SYNC_STATUS_COMPLETED,     // Time is synchronized.
            SNTP_SYNC_STATUS_IN_PROGRESS,   // Smooth time sync in progress.
          } sntp_sync_status_t;
    */

    sntp_initialize();  // name sntp_init() results in compilor error "multiple definitions sntp_init()"
    //sntp_sync_status_t sntp_sync_status = sntp_get_sync_status();
    int status = sntp_get_sync_status();
    String txt = "";
    if (status == SNTP_SYNC_STATUS_RESET) // SNTP_SYNC_STATUS_RESET
      txt = "RESET";
    else if (status == SNTP_SYNC_STATUS_COMPLETED)
      txt = "COMPLETED";
    else if (status == SNTP_SYNC_STATUS_IN_PROGRESS)
      txt = "IN PROGRESS";
    else
      txt = "UNKNOWN";
    
    std::cout << *TAG << "sntp_sync_status = " << txt.c_str() << std::endl;

    zone_idx = 0;
    setTimezone();

    if (true) // (initTime())
    {
      printLocalTime();
      if (set_RTC())
      {
        poll_RTC();  // Update RTCtimeinfo
        if (display_on)
          disp_data();
      }
    }
  }
  else
    connect_try++;

  M5.Display.clear(BLACK);
}

void loop(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("loop(): ");
  unsigned long const zone_chg_interval_t = 25 * 1000L; // 25 seconds
  unsigned long zone_chg_curr_t = 0L;
  unsigned long zone_chg_elapsed_t = 0L;
  time_t t;
  
  int btn_press_cnt = 0;
  char times_lst[3][7] = {"dummy", "first", "second"};
  bool dummy = false;
  bool zone_change = false;
  bool lStart = true;
  bool msgAsleep_shown = false;
  bool msgWakeup_shown = false;
  const char disp_off_txt[] = "Switching display off. Just press the button to switch display on again.";
  bool lCkRFID = false;

  std::cout << *TAG << "state of the global flag use_rfid = " << ((use_rfid == true) ? "True" : "False") << std::endl;
  while (true)
  {
    if (use_rfid)
    { 
      lCkRFID = ck_RFID();
      if (my_debug)
      {
        std::cout << *TAG << "result ck_RFID() = " << ((lCkRFID == true) ? "True" : "False") << std::endl;
      }
      if (lCkRFID)
      {
        touch_cnt++;
      }
    }
    else
    {
      dummy = ck_touch();
    }
  
    if (touch_cnt > 0)
    {
      touch_cnt = 0; // reset

      display_on = (display_on == true) ? false : true; // flip the display_on flag

      if (use_rfid)
      {
        std::cout << *TAG << "Switching display " << ((display_on == true) ? "On" : "Off") << std::endl;
      }
      else
      {
        std::cout << std::endl << *TAG << "touch_cnt = " << std::to_string(touch_cnt) << std::endl;
        std::cout << *TAG << "Display touched. Switching display " << ((display_on == true) ? "On" : "Off") << std::endl;
      }

      if (display_on)
      {
        if (i_am_asleep)
        {
          // See: https://github.com/m5stack/m5-docs/blob/master/docs/en/api/lcd.md
          M5.Display.wakeup();
          i_am_asleep = false;
          M5.Display.setBrightness(disp_brightness);  // 0 - 255
          disp_msg("Waking up!");
          t = time(NULL);
          std::cout << *TAG << "Waking up! At (UTC): " << asctime(gmtime(&t)) << std::endl;
          M5Dial.Display.setTextColor(DISP_FG, DISP_BG);
          M5.Display.clear(BLACK);
        }
      }
      else
      {
        if (!i_am_asleep)
        {
          // See: https://github.com/m5stack/m5-docs/blob/master/docs/en/api/lcd.md
          // M5Dial.Power.powerOff(); // shutdown
          disp_msg("Going asleep!");
          t = time(NULL);
          std::cout << *TAG << "Going asleep! At (UTC): " << asctime(gmtime(&t)) << std::endl;
          M5.Display.sleep();
          i_am_asleep = true;
          M5.Display.setBrightness(0);
          chg_display_clr(MY_BLACK); // Switch background to black
        }
      }
    }

    if (!ck_Btn())
    {
      if (WiFi.status() != WL_CONNECTED) // Check if we're still connected to WiFi
      {
        std::cout << "loop(): WiFi connection lost. Trying to reconnect..." << std::endl;
        if (!connect_WiFi())  // Try to connect WiFi
          connect_try++;
        
        if (connect_try >= max_connect_try)
        {
          M5Dial.Display.fillScreen(TFT_BLACK);
          M5Dial.Display.clear();
          M5Dial.Display.setTextDatum(middle_center);
          M5Dial.Display.setTextColor(TFT_RED);
          M5Dial.Display.drawString("WiFi fail", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 - 50);
          M5Dial.Display.drawString("Exit into", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
          M5Dial.Display.drawString("infinite loop", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 + 50);
          std::cout << "\nWiFi connect try failed " << (connect_try) << " times. Going into infinite loop...\n" << std::endl;
          delay(6000);
          break; // exit and go into an endless loop
        }
      }

      zone_chg_curr_t = millis();

      zone_chg_elapsed_t = zone_chg_curr_t - zone_chg_start_t;

      /* Do a zone change */
      if (lStart || zone_chg_elapsed_t >= zone_chg_interval_t)
      {
        if (lStart)
        {
          zone_idx = -1; // will be increased in code below
        }
        lStart = false;
        TimeToChangeZone = true;
        zone_chg_start_t = zone_chg_curr_t;
        /*
          Increases the Display color index.
        */
        FSM++;
        if (FSM >= 6)
        {
          FSM = 0;
        }
        // chg_display_clr(-1);
        /*
        Increase the zone_index, so that the sketch
        will display data from a next timezone in the map: time_zones.
        */
        zone_idx++;
        if (zone_idx >= zone_max_idx)
        {
          zone_idx = 0;
        }
        setTimezone();
        TimeToChangeZone = false;
        poll_RTC();
        printLocalTime();
        if (display_on)
        {
          disp_data();
        }
        // Poll NTP and set RTC to synchronize it
        Done = 0; // Reset this count also
      }
    }
    if (buttonPressed)
    {
      // We have a button press so do a software reset
      std::cout << *TAG << "Button was pressed.\n" << 
        "Going to do a software reset...\n" << std::endl;
      disp_msg("Reset..."); // there is already a wait of 6000 in disp_msg()
      //delay(3000);
      esp_restart();
    }
    if (display_on)
    {
      // printLocalTime();
      disp_data();
      //delay(1000);  // Wait 1 second
    }
    M5Dial.update(); // read btn state etc.
  }
  
  disp_msg("Bye...");
  std::cout << *TAG << "Bye...\n" << std::endl;
  //M5Dial.update();
  /* Go into an endless loop after WiFi doesn't work */
  do
  {
    delay(5000);
  } while (true);
}
