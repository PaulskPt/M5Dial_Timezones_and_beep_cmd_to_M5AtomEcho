/*
*  
*  M5Dial_Timezones.ino  Version 4.0
*  Test sketch for M5Stack M5Dial with 240 x 240 pixels
*  This sketch displays in sequence six different timezones.
*  Version 4.0 uses NTP automatic polling.
*  by @PaulskPt 2024-10-05
*  License: MIT
*
*  Example:
*  https://randomnerdtutorials.com/esp32-ntp-timezones-daylight-saving/
   #:~:text=configTzTime(%E2%80%9CCET-1CEST,M3.5.0/03,M10.5.0/03%E2%80%9D,%20ntp.c_str());%20then%20it%20works%20perfect
*/

#include <M5Dial.h>
#include <esp_sntp.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <stdlib.h>   // for putenv
#include <time.h>
#include <DateTime.h> // See: /Arduino/libraries/ESPDateTime/src
#include <M5GFX.h>
//#include <Free_Fonts.h>
#include <driver/adc.h>
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

//namespace {  // anonymous namespace (also known as an unnamed namespace)

#define WIFI_SSID     SECRET_SSID // "YOUR WIFI SSID NAME"
#define WIFI_PASSWORD SECRET_PASS //"YOUR WIFI PASSWORD"
#define NTP_TIMEZONE  SECRET_NTP_TIMEZONE // for example: "Europe/Lisbon"
#define NTP_TIMEZONE_CODE  SECRET_NTP_TIMEZONE_CODE // for example: "WET0WEST,M3.5.0/1,M10.5.0"
#define NTP_SERVER1   SECRET_NTP_SERVER_1 // for example: "0.pool.ntp.org"
#define NTP_SERVER2   "1.pool.ntp.org"
#define NTP_SERVER3   "2.pool.ntp.org"

#ifdef CONFIG_LWIP_SNTP_UPDATE_DELAY   // Found in: Component config > LWIP > SNTP
#undef CONFIG_LWIP_SNTP_UPDATE_DELAY
#endif

#define CONFIG_LWIP_SNTP_UPDATE_DELAY  15 * 60 * 1000 // = 15 minutes (15 seconds is the minimum). Original setting: 3600000  // 1 hour

// 4-PIN connector type HY2.0-4P
#define PORT_B_GROVE_OUT_PIN 2
#define PORT_B_GROVE_IN_PIN  1

std::string elem_zone;
std::string elem_zone_code;
std::string elem_zone_code_old;
bool zone_has_changed = false;

bool my_debug = false;
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
    M5Dial.Display.setCursor(dw/2-25, 20);
    M5Dial.Display.setTextColor(GREEN, BLACK);
    M5Dial.Display.print("TS");
    delay(500);
    std::cout << *TAG << "Sending cmd to beep to Atom Echo" << std::endl;
    send_cmd_to_AtomEcho(); // Send a digital signal to the Atom Echo to produce a beep
  }
  else
  {
    M5Dial.Display.fillRect(dw/2-25, 15, 50, 25, BLACK);
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
    clr_scrn_partly();
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
  // Serial.printf("Setting Timezone to \"%s\"\n",elem_zone_code.c_str());
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
  if (ck_Btn())
    return;

  clr_scrn_partly();
  M5Dial.Display.setTextColor(YELLOW, BLACK);
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
  delay(disp_data_delay);
  if (TimeToChangeZone)
    return;
  // =========== 2nd view =================
  if (ck_Btn())
    return;

  clr_scrn_partly();
  M5Dial.Display.setTextColor(YELLOW, BLACK);
  M5Dial.Display.setCursor(hori[1], vert[1]+5);
  M5Dial.Display.print("Zone");
  M5Dial.Display.setCursor(hori[1], vert[2]);
  M5Dial.Display.print(&my_timeinfo, "%Z %z");
  delay(disp_data_delay);
  if (TimeToChangeZone)
    return;
  // =========== 3rd view =================
  if (ck_Btn())
    return;

  clr_scrn_partly();
  M5Dial.Display.setTextColor(YELLOW, BLACK);
  M5Dial.Display.setCursor(hori[1], vert[1]+5);
  M5Dial.Display.print(&my_timeinfo, "%A");  // Day of the week
  M5Dial.Display.setCursor(hori[1], vert[2]-2);
  M5Dial.Display.print(&my_timeinfo, "%B %d");
  M5Dial.Display.setCursor(hori[1], vert[3]-10);
  M5Dial.Display.print(&my_timeinfo, "%Y");
  delay(disp_data_delay);
  if (TimeToChangeZone)
    return;
   // =========== 4th view =================
  if (ck_Btn())
    return;

  clr_scrn_partly();
  M5Dial.Display.setTextColor(YELLOW, BLACK);
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
  
  delay(disp_data_delay);
  if (TimeToChangeZone)
    return;
}

void chg_display_clr(void)
{
  switch (FSM) 
  {
    case 0:
        M5Dial.Display.fillScreen(GREEN); // Change GREEN to any color you want
        break;
    case 1:
        M5Dial.Display.fillScreen(RED);
        break;
    case 2:
        M5Dial.Display.fillScreen(BLUE);
        break;
    case 3:
        M5Dial.Display.fillScreen(WHITE);
        break;
    case 4:
        M5Dial.Display.fillScreen(MAGENTA);
        break;
    case 5:
        M5Dial.Display.fillScreen(ORANGE);
        break;
    case 6:
        M5Dial.Display.fillScreen(BLACK);
        break;
    default:
        break;
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

  clr_scrn_partly();
  M5Dial.Display.setTextColor(RED, BLACK);
  //M5Dial.Display.setFont(&fonts::FreeSans18pt7b);

  for (int i = 0; i < 4; ++i) {
    x = calc_x_offset(txt[i], char_width_in_pixels[i]);
    // Serial.printf("start_scrn(): x = %d\n", x);
    M5Dial.Display.setCursor(x, vert2[i + 1]);
    M5Dial.Display.println(txt[i]);
  }

  //delay(5000);
  M5Dial.Display.setTextColor(YELLOW, BLACK);
  //M5Dial.Display.setFont(&fonts::FreeSans12pt7b); // was: efontCN_14);
}

void clr_scrn_partly(void)
{
  M5Dial.Display.fillRect(hori[0],vert[1]-5, dw-1, dh-1, BLACK);  // clear display except the upper row
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
  M5.begin();  
  auto cfg = M5.config();

  M5Dial.begin(cfg, true, false);

  /*
  * A workaround to prevent some problems regarding 
  * M5Dial.BtnA.IsPressed(), M5Dial.BtnA.IsPressedPressed(), 
  * M5Dial.BtnA,wasPressed() or M5Dial.BtnA.pressedFor(ms).
  * See: https://community.m5stack.com/topic/3955/atom-button-at-gpio39-without-pullup/5
  */
  adc_power_acquire(); // ADC Power ON

  //attachInterrupt(digitalPinToInterrupt(ck_Btn()), handleButtonPress, FALLING);

  // Pin settings for communication with M5Dial to receive commands from M5Dial
  // commands to start a beep.
  pinMode(PORT_B_GROVE_OUT_PIN, OUTPUT); // Set PORT_B_GROVE_OUT_PIN as an output
  digitalWrite(PORT_B_GROVE_OUT_PIN, LOW); // Turn Idle the output pin
  pinMode(PORT_B_GROVE_IN_PIN, INPUT); // Set PORT_B_GROVE_IN_PIN as an input
  digitalWrite(PORT_B_GROVE_IN_PIN, LOW); // Turn Idle the output pin

  M5Dial.Display.init();
   dw = M5Dial.Display.width();
   dh = M5Dial.Display.height();
  M5Dial.Display.setRotation(0);
  M5Dial.Display.setTextColor(YELLOW, BLACK);

  M5Dial.Display.setColorDepth(1); // mono color
  M5Dial.Display.setFont(&fonts::FreeSans12pt7b); // was: efontCN_14);
  M5Dial.Display.setTextWrap(false);
  M5Dial.Display.setTextSize(1);

  Serial.begin(115200);

  if (my_debug)
    std::cout << "M5Stack M5Dial display width = " << std::to_string(dw) << ", height = " << std::to_string(dh) << std::endl;

  start_scrn();

  if (my_debug)
    getID();

  std::cout << "\n\nM5Stack M5Dial Timezones test." << std::endl;

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
    
    std::cout << "setup(): sntp_sync_status = " << txt.c_str() << std::endl;

    zone_idx = 0;
    setTimezone();

    if (true) // (initTime())
    {
      printLocalTime();
      if (set_RTC())
      {
        poll_RTC();  // Update RTCtimeinfo
        disp_data();
      }
    }
  }
  else
    connect_try++;

  clr_scrn_partly();
}

void loop(void)
{
  unsigned long zone_chg_interval_t = 25000L; // 25 seconds
  unsigned long zone_chg_curr_t = 0L;
  unsigned long zone_chg_elapsed_t = 0L;

  bool dummy = false;
  bool zone_change = false;
  bool lStart = true;

  while (true)
  {
    if (!ck_Btn())
    {
      if (WiFi.status() != WL_CONNECTED) // Check if we're still connected to WiFi
      {
        // Serial.print(F("loop(): WiFi connection lost. Trying to reconnect..."));
        std::cout << "loop(): WiFi connection lost. Trying to reconnect..." << std::endl;
        if (!connect_WiFi())  // Try to connect WiFi
          connect_try++;

        if (connect_try >= max_connect_try)
        {
          clr_scrn_partly();
          M5Dial.Display.setCursor(hori[1], vert[1]+5);
          M5Dial.Display.print("WiFi fail!");
          M5Dial.Display.setCursor(hori[1], vert[2]-2);
          M5Dial.Display.print("Exit into");
          M5Dial.Display.setCursor(hori[1], vert[3]-10);
          M5Dial.Display.print("infinite loop");

          std::cout << "\nWiFi connect try failed " << connect_try << "times. Going into infinite loop...\n" << std::flush;
          break; // exit while() and go into an endless loop
        }
      }

      zone_chg_curr_t = millis();

      zone_chg_elapsed_t = zone_chg_curr_t - zone_chg_start_t;

      /* Do a zone change */
      if (lStart || zone_chg_elapsed_t >= zone_chg_interval_t)
      {
        if (lStart) 
          zone_idx = -1; // will be increased in code below
        
        lStart = false;
        TimeToChangeZone = true;
        zone_chg_start_t = zone_chg_curr_t;
        /*
          Increases the Display color index.
        */
        FSM++;
        if (FSM >= 6)
          FSM = 0;
        // chg_display_clr();
        /*
        Increase the zone_index, so that the sketch
        will display data from a next timezone in the map: time_zones.
        */
        zone_idx++;
        if (zone_idx >= zone_max_idx) 
          zone_idx = 0;
        setTimezone();
        TimeToChangeZone = false;
        poll_RTC();
        printLocalTime();
        disp_data();

        // Poll NTP and set RTC to synchronize it
        Done = 0; // Reset this count also
      }
    }
    if (buttonPressed)
    {
      buttonPressed = false;
      std::cout << "\nButton pressed" << std::endl;
      std::cout << "Going to do a software reset..." << std::endl;
      M5Dial.Display.clearDisplay();
      M5Dial.Display.setCursor(hori[1]-20, vert[2]-5);
      M5Dial.Display.print("Going to reset...");
      delay(3000);
      esp_restart();
    }
    // printLocalTime();
    disp_data();
    //delay(1000);  // Wait 1 second
  }
  
  clr_scrn_partly();
  M5Dial.Display.setCursor(hori[1], vert[2]-2);
  M5Dial.Display.print("Bye...");
  M5Dial.update();
  /* Go into an endless loop after WiFi doesn't work */
  do
  {
    delay(5000);
  } while (true);
}
