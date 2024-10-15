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
*  Update: 2024-10-14. To prevent 100% memory usage: deleted a few functions. Deleted a lot of if(my_debug) conditional prints with a lot of text.
*  Assigned more variables as static procexpr const PROGMEM.
*
*  Update: 2025-10-15: Moved all zone definitions to secret.h. Deleted function: void map_replace_first_zone(void).
*          Changed function create_maps(). This function now imports all zone and zone_code definitions into a map.
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
#include "secret.h"
// Following 8 includes needed for creating, changing and using map time_zones
#include <iostream>
#include <map>
#include <memory>
#include <array>
#include <string>
#include <tuple>
#include <iomanip> // For setFill and setW
#include <sstream>  // Used in ck_RFID()

//namespace {  // anonymous namespace (also known as an unnamed namespace)

#define WIFI_SSID     SECRET_SSID // "YOUR WIFI SSID NAME"
#define WIFI_PASSWORD SECRET_PASS //"YOUR WIFI PASSWORD"
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
struct tm timeinfo = {};
bool use_timeinfo = true;
std::tm* tm_local = {};
tm RTCdate;

int dw;
int dh;

// M5Dial screen 1.28 Inch 240x240px. Display device: GC9A01
static constexpr const int hori[] PROGMEM = {0, 60, 120, 180, 220};
static constexpr const int vert[] PROGMEM = {0, 60, 120, 180, 220};

static constexpr const char* wd[7] PROGMEM = {"Sun", "Mon", "Tue", "Wed",
                                            "Thu", "Fri", "Sat"};
// M5Dial touch driver: FT3267

unsigned long zone_chg_start_t = millis();
bool TimeToChangeZone = false;

int connect_try = 0;
const int max_connect_try = 10;

volatile bool buttonPressed = false;

int zone_idx; // Will be incremented in loop()
static constexpr const int nr_of_zones = SECRET_NTP_NR_OF_ZONES[0] - '0';  // Assuming SECRET_NTP_NR_OF_ZONES is defined as a string

std::map<int, std::tuple<std::string, std::string>> zones_map;

//} // end of namespace

void create_maps() 
{
  
  for (int i = 0; i < nr_of_zones; ++i)
  {
    // Building variable names dynamically isn't directly possible, so you might want to define arrays instead
    switch (i)
    {
      case 0:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE0, SECRET_NTP_TIMEZONE0_CODE);
        break;
      case 1:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE1, SECRET_NTP_TIMEZONE1_CODE);
        break;
      case 2:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE2, SECRET_NTP_TIMEZONE2_CODE);
        break;
      case 3:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE3, SECRET_NTP_TIMEZONE3_CODE);
        break;
      case 4:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE4, SECRET_NTP_TIMEZONE4_CODE);
        break;
      case 5:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE5, SECRET_NTP_TIMEZONE5_CODE);
        break;
      case 6:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE6, SECRET_NTP_TIMEZONE6_CODE);
        break;
      default:
        break;
    }             
  }
}

/* Show or remove NTP Time Sync notification on the middle of the top of the display */
void ntp_sync_notification_txt(bool show)
{
  if (show)
  {
    std::shared_ptr<std::string> TAG = std::make_shared<std::string>("mtp_sync_notification_txt(): ");
    static constexpr const char txt[] PROGMEM = "beep command to the M5 Atom Echo device";
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
    {
      static constexpr const char txt2[] PROGMEM = "Display is Off. Not sending ";
      std::cout << *TAG << txt2 << (txt) << std::endl;
    }
  }
  else
  {
    //M5Dial.Display.fillRect(dw/2-25, 15, 50, 25, BLACK);
    M5Dial.Display.fillRect(0, 0, dw-1, 55, BLACK);
  }
}

void time_sync_notification_cb(struct timeval *tv)
{
  static constexpr const char txt1[] PROGMEM = "time_sync_notification_cb(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);

  if (initTime())
  {
    time_t t = time(NULL);
    static constexpr const char txt5[] PROGMEM = "time synchronized at time (UTC): ";
    std::cout << *TAG << txt5 << asctime(gmtime(&t)) << std::flush;  // prevent a 2nd LF. Do not use std::endl
    ntp_sync_notification_txt(true);
  }
}

void sntp_initialize() {
  static constexpr const char txt1[] PROGMEM = "sntp_initialize(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, NTP_SERVER1);
  sntp_set_sync_interval(CONFIG_LWIP_SNTP_UPDATE_DELAY);
  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  sntp_init();
  
  static constexpr const char txt4[] PROGMEM = "sntp polling interval: ";
  static constexpr const char txt5[] PROGMEM = " Minute(s)";
  std::cout << *TAG << txt4 << std::to_string(CONFIG_LWIP_SNTP_UPDATE_DELAY/60000) << txt5 << std::endl;
}

void setTimezone(void)
{
  static constexpr const char txt1[] PROGMEM = "setTimezone(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  elem_zone = std::get<0>(zones_map[zone_idx]);
  elem_zone_code = std::get<1>(zones_map[zone_idx]);
  if (elem_zone_code != elem_zone_code_old)
  {
    elem_zone_code_old = elem_zone_code;
    const char s1[] PROGMEM = "has changed to: ";
    zone_has_changed = true;
  }
  setenv("TZ",elem_zone_code.c_str(),1);
  delay(500);
  tzset();
  delay(500);
}

bool initTime(void)
{
  bool ret = false;
  static constexpr const char txt1[] PROGMEM = "initTime(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  elem_zone = std::get<0>(zones_map[zone_idx]);

#ifndef ESP32
#define ESP32 (1)
#endif

std::string my_tz_code = getenv("TZ");

// See: /Arduino/libraries/ESPDateTime/src/DateTime.cpp, lines 76-80
#if defined(ESP8266)
  configTzTime(elem_zone_code.c_str(), NTP_SERVER1, NTP_SERVER2, NTP_SERVER3); 
#elif defined(ESP32)
static constexpr const char txt7[] PROGMEM = "Setting configTzTime to: \"";
  std::cout << *TAG << txt7 << my_tz_code.c_str() << "\"" << std::endl;
  //configTime(0, 3600, NTP_SERVER1);
  configTzTime(my_tz_code.c_str(), NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);  // This one is use for the M5Stack Atom Matrix
#endif

  while (!getLocalTime(&timeinfo, 1000))
  {
    std::cout << "." << std::flush;
    delay(1000);
  };
  static constexpr const char txt8[] PROGMEM = "NTP Connected. ";
  std::cout << *TAG << txt8 << std::endl;

  if (timeinfo.tm_sec == 0 && timeinfo.tm_min  == 0 && timeinfo.tm_hour  == 0 &&
      timeinfo.tm_mday == 0 && timeinfo.tm_mon  == 0 && timeinfo.tm_year  == 0 &&
      timeinfo.tm_wday == 0 && timeinfo.tm_yday == 0 && timeinfo.tm_isdst == 0)
  {
    static constexpr const char txt9[] PROGMEM = "Failed to obtain datetime from NTP";
    std::cout << *TAG << txt9 << std::endl;
  }
  else
  {
    setTimezone();
    ret = true;
  }
  return ret;
}

bool set_RTC(void)
{
  bool ret = false;
  static constexpr const char txt1[] PROGMEM = "set_RTC(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  struct tm my_timeinfo;
  if(!getLocalTime(&my_timeinfo))
  {
    static constexpr const char txt2[] PROGMEM = "Failed to obtain time";
    std::cout << *TAG << txt2 << std::endl;
    return ret;
  }

  if (my_timeinfo.tm_year + 1900 > 1900)
  {
    //                            YYYY  MM  DD      hh  mm  ss
    //M5Dial.Rtc.setDateTime( { { 2021, 12, 31 }, { 12, 34, 56 } } );
    M5Dial.Rtc.setDateTime( {{my_timeinfo.tm_year + 1900, my_timeinfo.tm_mon + 1, 
        my_timeinfo.tm_mday}, {my_timeinfo.tm_hour, my_timeinfo.tm_min, my_timeinfo.tm_sec}} );
    ret = true;
  }
  return ret;
}

void printLocalTime()  // "Local" of the current selected timezone!
{ 
  static constexpr const char txt1[] PROGMEM = "printLocalTime(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  if(!getLocalTime(&timeinfo)){
    static constexpr const char txt2[] PROGMEM = "Failed to obtain time";
    std::cout << txt2 << std::endl;
    return;
  }
  static constexpr const char txt3[] PROGMEM = "Timezone: ";
  static constexpr const char txt4[] PROGMEM = ", datetime: ";
  std::cout << *TAG << txt3 << elem_zone.c_str() << txt4 << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << std::endl;

}

/* This function uses global var timeinfo to display date and time data.
   The function also displays timezone info.
   It also calls ck_Btn() four times to increase a "catch" of BtnA keypress.
*/
void disp_data(void)
{
  static constexpr const char txt1[] PROGMEM = "disp_data(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  int disp_data_delay = 1000;
  // For unitOLED
  int scrollstep = 2;

  elem_zone  = std::get<0>(zones_map[zone_idx]);
  std::string copiedString, copiedString2;
  std::string part1, part2, part3, part4;
  std::string partUS1, partUS2;
  int index1, index2, index3 = -1;
  copiedString =  elem_zone;
  // Search for a first forward slash (e.g.: "Europe/Lisbon")
  index1 = copiedString.find('/');
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
  if (index1 >= 0)
  {
    part1 = copiedString.substr(0, index1);
    part2 = copiedString.substr(index1+1);

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
    static constexpr const char txt2[] PROGMEM = "Failed to obtain time";
    std::cout << txt2 << std::endl;
    return;
  }
  // =========== 1st view =================
if (ck_touch())
    return;
 if (ck_Btn())
    return;

  M5.Display.clear(BLACK);
  M5Dial.Display.setTextColor(DISP_FG, DISP_BG);
  if (index1 >= 0 && index2 >= 0)
  {
    M5Dial.Display.setCursor(hori[1], vert[1]+5);
    M5Dial.Display.print(part1.c_str());
    M5Dial.Display.setCursor(hori[1], vert[2]-2);
    M5Dial.Display.print(part3.c_str());
    M5Dial.Display.setCursor(hori[1], vert[3]-10);
    M5Dial.Display.print(part4.c_str());
  }
  else if (index1 >= 0)
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
  else if (index1 >= 0)
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

bool connect_WiFi(void)
{
  static constexpr const char txt1[] PROGMEM = "connect_WiFi(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  bool ret = false;
  WiFi.begin( WIFI_SSID, WIFI_PASSWORD );

  for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
    delay(500);
  
  if (WiFi.status() == WL_CONNECTED) 
  {
    ret = true;
    static constexpr const char txt3[] PROGMEM = "\r\nWiFi Connected";
    std::cout << txt3 << std::endl;
  }
  else
  {
    static constexpr const char txt6[] PROGMEM = "WiFi connection failed.";
    std::cout << "\r\n" << txt6 << std::endl;
  }
  return ret;
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

  M5Dial.update();

  auto t = M5Dial.Touch.getDetail();
  bool currentTouchState = t.state; // M5.Touch.ispressed();

  if (currentTouchState != lastTouchState)
    lastDebounceTime = millis();

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
  static constexpr const char txt1[] PROGMEM = "ck_RFID(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  bool lCardIsOK = false;

  M5Dial.Rfid.begin();
  delay(1000);
  if (M5Dial.Rfid.PICC_IsNewCardPresent() &&
      M5Dial.Rfid.PICC_ReadCardSerial()) 
  {
    M5Dial.Display.clear();
    uint8_t piccType = M5Dial.Rfid.PICC_GetType(M5Dial.Rfid.uid.sak);
    // Check is the PICC of Classic MIFARE type
    if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
          piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
          piccType != MFRC522::PICC_TYPE_MIFARE_4K)
    {
      static constexpr const char txt5[] PROGMEM = "Your tag is not of type MIFARE Classic.";
      std::cout << *TAG << txt5 << std::endl;
      M5Dial.Display.fillScreen(TFT_BLACK);
      M5Dial.Display.clear();
      M5Dial.Display.setTextDatum(middle_center);
      M5Dial.Display.setTextColor(TFT_RED);
      static constexpr const char txt6[] PROGMEM = "card not";
      static constexpr const char txt7[] PROGMEM = "supported";
      M5Dial.Display.drawString(txt6, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 - 50);
      M5Dial.Display.drawString(txt7, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
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

    static constexpr const char txt2[] PROGMEM = "Hi there, your RFID TAG has ";
    static constexpr const char txt3[] PROGMEM = "been recognized. ";
    static constexpr const char txt4[] PROGMEM = "Check your card!";

    if (lCardIsOK)
      std::cout << *TAG << (txt2) << (txt3) << std::endl;
    else
      std::cout << *TAG << (txt2) << "not " << (txt3) << (txt4) << std::endl;
 
  }
  M5Dial.Rfid.PICC_HaltA();
  M5Dial.Rfid.PCD_StopCrypto1();
  return lCardIsOK;
}

int calc_x_offset(const char* t, int ch_width_in_px) {
  int le = strlen(t);
  int char_space = 1;
  int ret = ( dw - ((le * ch_width_in_px) + ((le -1) * char_space) )) / 2;
  return (ret < 0) ? 0 : ret;
}

void start_scrn(void) {
  static constexpr const char* txt[] PROGMEM = {"TIMEZONES", "by Paulus", "Github", "@PaulskPt"};
  static constexpr int char_width_in_pixels[] PROGMEM = {16, 12, 12, 14};
  static constexpr const int vert2[] PROGMEM = {0, 60, 90, 120, 150}; 
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
    digitalWrite(PORT_B_GROVE_IN_PIN, LOW);
    digitalWrite(PORT_B_GROVE_OUT_PIN, HIGH);
    delay(100);
    digitalWrite(PORT_B_GROVE_OUT_PIN, LOW);
    delay(100); 
}

void setup(void) 
{
  static constexpr const char txt1[] PROGMEM = "setup(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  M5.begin();  
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;

  M5Dial.begin(cfg); //, true, false);

  M5Dial.Power.begin();

  // Pin settings for communication with M5Dial to receive commands from M5Dial
  // commands to start a beep.
  pinMode(PORT_B_GROVE_OUT_PIN, OUTPUT);
  digitalWrite(PORT_B_GROVE_OUT_PIN, LOW); // Turn Idle the output pin
  pinMode(PORT_B_GROVE_IN_PIN, INPUT);
  digitalWrite(PORT_B_GROVE_IN_PIN, LOW); // Turn Idle the input pin

  Serial.begin(115200);
  static constexpr const char txt2[] PROGMEM = "M5Stack M5Dial Timezones and M5Atom Echo with RFID test.";
  std::cout << "\n\n" << *TAG << txt2 << std::endl;

  M5Dial.Display.init();
  static constexpr const char txt3[] PROGMEM = "setting display brightness to ";
  static constexpr const char txt4[] PROGMEM = " (range 0-255)";
  std::cout << *TAG << txt3 << (disp_brightness) << txt4 << std::endl;
  M5Dial.Display.setBrightness(disp_brightness);  // 0-255
   dw = M5Dial.Display.width();
   dh = M5Dial.Display.height();
  M5Dial.Display.setRotation(0);
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setTextColor(DISP_FG, DISP_BG);
  M5Dial.Display.setColorDepth(1); // mono color
  M5Dial.Display.setFont(&fonts::FreeSans12pt7b);
  M5Dial.Display.setTextWrap(false);
  M5Dial.Display.setTextSize(1);

  start_scrn();

  create_maps();  // creeate zones_map

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
    static constexpr const char txt6[] PROGMEM = "sntp sync status = ";
    std::cout << *TAG << txt6 << std::to_string(status) << std::endl;

    zone_idx = 0; // needed to set here. Is needed in setTimezone()
    setTimezone();
  }
  else
  {
    connect_try++;
  }
  M5.Display.clear(BLACK);
}

void loop(void)
{
  static constexpr const char txt1[] PROGMEM = "loop(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  unsigned long const zone_chg_interval_t = 25 * 1000L; // 25 seconds
  unsigned long zone_chg_curr_t = 0L;
  unsigned long zone_chg_elapsed_t = 0L;
  time_t t;
  
  static constexpr const char t_txt [] PROGMEM = "True";
  static constexpr const char f_txt [] PROGMEM = "False";
  static constexpr const char on_txt [] PROGMEM = "On";
  static constexpr const char off_txt [] PROGMEM = "Off";

  int btn_press_cnt = 0;
  static constexpr const char times_lst[3][7] = {"dummy", "first", "second"};
  bool dummy = false;
  bool zone_change = false;
  bool lStart = true;
  bool msgAsleep_shown = false;
  bool msgWakeup_shown = false;
  static constexpr const char disp_off_txt[] PROGMEM = "Switching display off.";
  bool lCkRFID = false;

  static constexpr const char txt2[] PROGMEM = "global flag use_rfid = ";
  std::cout << *TAG << txt2 << ((use_rfid == true) ? t_txt : f_txt) << std::endl;
  while (true)
  {
    if (use_rfid)
    { 
      lCkRFID = ck_RFID();
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
        static constexpr const char txt4[] PROGMEM = "Switching display ";
        std::cout << *TAG << txt4 << ((display_on == true) ? on_txt : off_txt) << std::endl;
      }
      else
      {
        static constexpr const char txt5[] PROGMEM = "touch_cnt = ";
        static constexpr const char txt6[] PROGMEM = "Display touched. Switching display ";
        std::cout << std::endl << *TAG << txt5 << std::to_string(touch_cnt) << std::endl;
        std::cout << *TAG << txt6 << ((display_on == true) ? on_txt : off_txt) << std::endl;
      }

      if (display_on)
      {
        if (i_am_asleep)
        {
          // See: https://github.com/m5stack/m5-docs/blob/master/docs/en/api/lcd.md
          M5.Display.wakeup();
          i_am_asleep = false;
          M5.Display.setBrightness(disp_brightness);  // 0 - 255
          static constexpr const char txt7[] PROGMEM = "Waking up!";
          static constexpr const char txt8[] PROGMEM = " At (UTC): ";
          disp_msg(txt7);
          t = time(NULL);

          std::cout << *TAG << txt7 << txt8 << asctime(gmtime(&t)) << std::endl;
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
          static constexpr const char txt9[] PROGMEM = "Going asleep!";
          static constexpr const char txt10[] PROGMEM = " At (UTC): ";
          disp_msg(txt9);
          t = time(NULL);
          std::cout << *TAG << txt9 << txt10 << asctime(gmtime(&t)) << std::endl;
          M5.Display.sleep();
          i_am_asleep = true;
          M5.Display.setBrightness(0);
          M5Dial.Display.fillScreen(TFT_BLACK); 
        }
      }
    }

    if (!ck_Btn())
    {
      if (WiFi.status() != WL_CONNECTED) // Check if we're still connected to WiFi
      {
        static constexpr const char txt11[] PROGMEM = "WiFi connection lost. Trying to reconnect...";
        std::cout << *TAG << txt11 << std::endl;
        if (!connect_WiFi())  // Try to connect WiFi
          connect_try++;
        
        if (connect_try >= max_connect_try)
        {
          M5Dial.Display.fillScreen(TFT_BLACK);
          M5Dial.Display.clear();
          M5Dial.Display.setTextDatum(middle_center);
          M5Dial.Display.setTextColor(TFT_RED);
          static constexpr const char txt12[] PROGMEM = "WiFi fail";
          static constexpr const char txt13[] PROGMEM = "Exit into";
          static constexpr const char txt14[] PROGMEM = "infinite loop";
          static constexpr const char txt15[] PROGMEM = "\nWiFi connect try failed ";
          static constexpr const char txt16[] PROGMEM = " times. Going into infinite loop...\n";
          M5Dial.Display.drawString(txt12, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 - 50);
          M5Dial.Display.drawString(txt13, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
          M5Dial.Display.drawString(txt14, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 + 50);
          std::cout << txt15 << (connect_try) << txt16 << std::endl;
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
        Increase the zone_index, so that the sketch
        will display data from a next timezone in the map: time_zones.
        */
        if (zone_idx < (nr_of_zones-1))
          zone_idx++;
        else
          zone_idx = 0;
        if (zone_idx == 0)
          std::cout << std::endl; // blank line
        std::cout << *TAG << "new zone_idx = " << zone_idx << std::endl;

        setTimezone();
        TimeToChangeZone = false;
        printLocalTime();
        if (display_on)
        {
          disp_data();
        }
      }
    }
    if (buttonPressed)
    {
      // We have a button press so do a software reset
      static constexpr const char txt17[] PROGMEM = "Button was pressed.\n";
      static constexpr const char txt18[] PROGMEM = "Going to do a software reset...\n";
      static constexpr const char txt19[] PROGMEM = "Reset...";
      std::cout << *TAG << txt17 << 
        txt18 << std::endl;
      disp_msg(txt19); // there is already a wait of 6000 in disp_msg()
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
  static constexpr const char txt20[] PROGMEM = "Bye...";
  disp_msg(txt20);
  std::cout << *TAG << txt20 << std::endl << std::endl;
  //M5Dial.update();
  /* Go into an endless loop after WiFi doesn't work */
  do
  {
    delay(5000);
  } while (true);
}
