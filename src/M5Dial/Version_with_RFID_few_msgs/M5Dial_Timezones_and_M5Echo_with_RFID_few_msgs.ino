/*
*  
*  M5Dial_Timezones_and_M5Echo_with_RFID_few_msgs.ino
*  Test sketch for M5Stack M5Dial with 240 x 240 pixels
*  This sketch displays in sequence six different timezones.
*  This version uses SNTP automatic polling.
*  by @PaulskPt 2024-10-05. Last update; 2024-10-22.
*  License: MIT
*
*  Example:
*  https://randomnerdtutorials.com/esp32-ntp-timezones-daylight-saving/

*
*  Update 2024-10-11: added functionality to use RFID card to put asleep or awake the display.
*  This functionality depends on the state of the global variable: use_rfid.
*  The ID number of the card that will be accepted needs to be in hexadecimal format, lower case, in file secret.h. 
*  Name: SECRET_MY_RFID_TAG_NR_HEX.
*
*  Update: 2024-10-14. To prevent 100% memory usage: deleted a few functions. Deleted a lot of if(my_debug) conditional prints with a lot of text.
*  Assigned more variables as static procexpr const PROGMEM.
*
*  Update: 2024-10-15: Moved all zone definitions to secret.h. Deleted function: void map_replace_first_zone(void).
*          Changed function create_maps(). This function now imports all zone and zone_code definitions into a map.
*  Update: 2024-10-23: In this version various functions are, with help of MS CoPilot optimized for memory use and
*          to prevent memory leakages (which happened in setTimezone() and disp_data()).
*/
#include <Arduino.h>
#include <M5Dial.h>
#include <M5Unified.h>
#include <M5GFX.h>

#include <esp_sntp.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <stdlib.h>   // for putenv
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
//#include <time.h>
#include <DateTime.h> // See: /Arduino/libraries/ESPDateTime/src
#include "secret.h"
// Following 8 includes needed for creating, changing and using map time_zones
#include <map>
#include <memory>
#include <array>
#include <string>
#include <tuple>
// #include <iomanip>  // For setFill and setW
#include <sstream>  // Used in intToHex() (line 607)
//#include <ctime>    // Used in time_sync_notification_cb()

//namespace {  // anonymous namespace (also known as an unnamed namespace)

// SNTP Polling interval set to: 900000 mSec = 15 minutes in milliseconds (15 seconds is the minimum),
// See: https://github.com/espressif/esp-idf/blob/master/components/lwip/apps/sntp/sntp.c
// 300000 dec = 0x493E0 = 20 bits  ( 5 minutes)
// 900000 dec = 0xDBBA0 = 20 bits  (15 minutes)

#define NTP_SERVER1   SECRET_NTP_SERVER_1 // for example: "0.pool.ntp.org"

#ifdef CONFIG_LWIP_SNTP_UPDATE_DELAY   // Found in: Component config > LWIP > SNTP
#undef CONFIG_LWIP_SNTP_UPDATE_DELAY
#endif

// 15U * 60U * 1000U = 15 minutes in milliseconds
#define CONFIG_LWIP_SNTP_UPDATE_DELAY (15 * 60 * 1000)  // 15 minutes

// 4-PIN connector type HY2.0-4P
#define PORT_B_GROVE_OUT_PIN 2
#define PORT_B_GROVE_IN_PIN  1

bool lStart = true;
bool display_on = true;
bool sync_time = false;
time_t time_sync_epoch_at_start = 0;
time_t last_time_sync_epoch = 0; // see: time_sync_notification_cb()

// M5Dial screen 1.28 Inch 240x240px. Display device: GC9A01
static constexpr const int hori[] PROGMEM = {0, 60, 120, 180, 220};
static constexpr const int vert[] PROGMEM = {0, 60, 120, 180, 220};

// M5Dial touch driver: FT3267

bool TimeToChangeZone = false;

int zone_idx; // Will be incremented in loop()
static constexpr const int nr_of_zones = SECRET_NTP_NR_OF_ZONES[0] - '0';  // Assuming SECRET_NTP_NR_OF_ZONES is defined as a string

std::map<int, std::tuple<std::string, std::string>> zones_map;

//} // end of namespace

void create_maps() {
  
  for (int i = 0; i < nr_of_zones; ++i) {
    // Building variable names dynamically isn't directly possible, so you might want to define arrays instead
    switch (i) {
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
void ntp_sync_notification_txt(bool show) {
  int dw = M5Dial.Display.width();
  if (show) {
    M5Dial.Display.setCursor(dw/2-25, 20);
    M5Dial.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Dial.Display.print("TS");
    delay(500);
    M5Dial.Display.setCursor(dw/2-25, 20);      // Try to overwrite in black instead of wiping the whole top area
    M5Dial.Display.setTextColor(TFT_BLACK, TFT_BLACK);
    M5Dial.Display.print("TS");
    M5Dial.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    /* Only send command to make sound by the M5 Atom Echo when the display is on,
       because when the user has put the display off, he/she probably wants to go to bed/sleep.
       In that case we don't want nightly sounds!
    */
    if (display_on) {
      send_cmd_to_AtomEcho(); // Send a digital signal to the Atom Echo to produce a beep
    }
  } else {
    //M5Dial.Display.fillRect(dw/2-25, 15, 50, 25, TFT_BLACK);
    M5Dial.Display.fillRect(0, 0, dw-1, 55, TFT_BLACK);
  }
}

/* Code suggested by MS CoPilot */
// The sntp callback function
void time_sync_notification_cb(struct timeval *tv) {
  // Get the current time  (very important!)
  time_t currentTime = time(nullptr);
  // Convert time_t to GMT struct tm
  struct tm* gmtTime = gmtime(&currentTime);
  uint16_t diff_t;
  // Set the starting epoch time if not set, only when lStart is true
  if (lStart && (time_sync_epoch_at_start == 0) && (currentTime > 0)) {
    time_sync_epoch_at_start = currentTime;  // Set only once!
  }
  // Set the last sync epoch time if not set
  if ((last_time_sync_epoch == 0) && (currentTime > 0)) {
    last_time_sync_epoch = currentTime;
  }
  if (currentTime > 0) {   
    diff_t = currentTime - last_time_sync_epoch;
    last_time_sync_epoch = currentTime;
    #define CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_SECONDS   CONFIG_LWIP_SNTP_UPDATE_DELAY / 1000

    if ((diff_t >= CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_SECONDS) || lStart) {  // 900 or more seconds = 15 or more minutes
      sync_time = true; // See loop initTime
      ntp_sync_notification_txt(true);
    }
  }
}
/* End of code suggested by MS CoPilot */

void esp_sntp_initialize() {
   if (esp_sntp_enabled()) { 
    esp_sntp_stop();  // prevent initialization error
  }
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, NTP_SERVER1);
  esp_sntp_set_sync_interval(CONFIG_LWIP_SNTP_UPDATE_DELAY);
  esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb); // Set the notification callback function
  esp_sntp_init();
  // check the set sync_interval
  uint32_t rcvd_sync_interval_secs = esp_sntp_get_sync_interval();
}

void setTimezone(void) {
  char elem_zone_code[50];
  strcpy(elem_zone_code, std::get<1>(zones_map[zone_idx]).c_str());
  setenv("TZ", elem_zone_code, 1);
  tzset();
}

bool initTime(void) {
  char elem_zone[50];
  char my_tz_code[50];
  bool ret = false;
  static constexpr const char NTP_SERVER2[] PROGMEM = "1.pool.ntp.org";
  static constexpr const char NTP_SERVER3[] PROGMEM = "2.pool.ntp.org";

  strcpy(elem_zone, std::get<0>(zones_map[zone_idx]).c_str());
  strcpy(my_tz_code, getenv("TZ"));

  configTzTime(my_tz_code, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

  struct tm my_timeinfo;
  while (!getLocalTime(&my_timeinfo, 1000)) {
      delay(1000);
  }

  if (my_timeinfo.tm_sec != 0 || my_timeinfo.tm_min  != 0 || my_timeinfo.tm_hour  != 0 || 
      my_timeinfo.tm_mday != 0 || my_timeinfo.tm_mon  != 0 || my_timeinfo.tm_year  != 0 || 
      my_timeinfo.tm_wday != 0 || my_timeinfo.tm_yday != 0 || my_timeinfo.tm_isdst != 0) {
      setTimezone();
      ret = true;
  }
  return ret;
}

bool set_RTC(void) {
  bool ret = false;
  struct tm my_timeinfo;
  if(!getLocalTime(&my_timeinfo)) return ret;
  if (my_timeinfo.tm_year + 1900 > 1900) {
    //                            YYYY  MM  DD      hh  mm  ss
    //M5Dial.Rtc.setDateTime( { { 2021, 12, 31 }, { 12, 34, 56 } } );
    M5Dial.Rtc.setDateTime( {{my_timeinfo.tm_year + 1900, my_timeinfo.tm_mon + 1, 
        my_timeinfo.tm_mday}, {my_timeinfo.tm_hour, my_timeinfo.tm_min, my_timeinfo.tm_sec}} );
    ret = true;
  }
  return ret;
}

/* This function uses local var my_timeinfo to display date and time data.
   The function also displays my_timezone info.
   It also calls functions ck_touch() and ck_Btn() four times 
   to increase a "catch" a display touch or a BtnA keypress.
*/
void disp_data(void) {
    struct tm my_timeinfo;
    if (!getLocalTime(&my_timeinfo)) return;

    M5.Display.clear(TFT_BLACK);
    M5Dial.Display.setTextColor(TFT_ORANGE, TFT_BLACK);

    char elem_zone[50];
    strncpy(elem_zone, std::get<0>(zones_map[zone_idx]).c_str(), sizeof(elem_zone) - 1);
    elem_zone[sizeof(elem_zone) - 1] = '\0'; // Ensure null termination

    char part1[20], part2[20], part3[20], part4[20];
    char copiedString[50], copiedString2[50];

    memset(part1, 0, sizeof(part1));
    memset(part2, 0, sizeof(part2));
    memset(part3, 0, sizeof(part3));
    memset(part4, 0, sizeof(part4));
    memset(copiedString, 0, sizeof(copiedString));
    memset(copiedString2, 0, sizeof(copiedString2));
    
    char *index1 = strchr(elem_zone, '/');  // index to the 1st occurrance of a forward slash (e.g.: Europe/Lisbon)
    char *index2 = nullptr; 
    char *index3 = strchr(elem_zone, '_'); // index to the occurrance of an underscore character (e.g.: Sao_Paulo)
    int disp_data_view_delay = 1000;

    strncpy(copiedString, elem_zone, sizeof(copiedString) - 1);
    copiedString[sizeof(copiedString) - 1] = '\0'; // Ensure null termination
    // Check if index1 is valid and within bounds
    if (index1 != nullptr) {
      size_t idx1_pos = index1 - elem_zone;
      if (idx1_pos < sizeof(copiedString)) {
        strncpy(part1, copiedString, idx1_pos);
        part1[idx1_pos] = '\0';
      }
      strncpy(copiedString2, index1 + 1, sizeof(copiedString2) - 1);
      copiedString2[sizeof(copiedString2) - 1] = '\0'; // Ensure null termination
      if (index3 != nullptr) {
        // Replace underscores with spaces in copiedString
        for (int i = 0; i < sizeof(copiedString2); i++) {
          if (copiedString2[i] == '_') {
            copiedString2[i] = ' ';
          }
        }
      }
      index2 = strchr(copiedString2, '/'); // index to the 2nd occurrance of a forward slahs (e.g.: America/Kentucky/Louisville)
      if (index2 != nullptr) {
        size_t idx2_pos = index2 - copiedString2;
        if (idx2_pos < sizeof(copiedString2)) {
            strncpy(part3, copiedString2, idx2_pos);  // part3, e.g.: "Kentucky"
            part3[idx2_pos] = '\0';
        }
        strncpy(part4, index2 + 1, sizeof(part4) - 1);  // part4, e.g.: "Louisville"
        part4[sizeof(part4) - 1] = '\0'; // Ensure null termination
      } else {
        strncpy(part2, copiedString2, sizeof(part2) - 1);
        part2[sizeof(part2) - 1] = '\0'; // Ensure null termination
      }
    }
    if (ck_touch() > 0) return;
    if (ck_BtnA()) return;

    if (index1 != nullptr && index2 != nullptr) {
        M5Dial.Display.setCursor(50, 65);
        M5Dial.Display.print(part1);
        M5Dial.Display.setCursor(50, 118);
        M5Dial.Display.print(part3);
        M5Dial.Display.setCursor(50, 170);
        M5Dial.Display.print(part4);
    } else if (index1 != nullptr) {
        M5Dial.Display.setCursor(50, 65);
        M5Dial.Display.print(part1);
        M5Dial.Display.setCursor(50, 120);
        M5Dial.Display.print(part2);
    } else {
        M5Dial.Display.setCursor(50, 65);
        M5Dial.Display.print(copiedString);
    }
    delay(disp_data_view_delay);

    if (ck_touch() > 0) return;
    if (ck_BtnA()) return;

    M5Dial.Display.clear();
    M5Dial.Display.setCursor(50, 65);
    M5Dial.Display.print("Zone");
    M5Dial.Display.setCursor(50, 120);
    M5Dial.Display.print(&my_timeinfo, "%Z %z");

    delay(disp_data_view_delay);
    if (ck_touch() > 0) return;
    if (ck_BtnA()) return;

    M5Dial.Display.clear();
    M5Dial.Display.setCursor(50, 65);
    M5Dial.Display.print(&my_timeinfo, "%A");
    M5Dial.Display.setCursor(50, 118);
    M5Dial.Display.print(&my_timeinfo, "%B %d");
    M5Dial.Display.setCursor(50, 170);
    M5Dial.Display.print(&my_timeinfo, "%Y");

    delay(disp_data_view_delay);
    if (ck_touch() > 0) return;
    if (ck_BtnA()) return;

    M5Dial.Display.clear();
    M5Dial.Display.setCursor(40, 65);
    M5Dial.Display.print(&my_timeinfo, "%H:%M:%S local");
    M5Dial.Display.setCursor(50, 120);

    if (index2 != nullptr) {
        M5Dial.Display.printf("in %s\n", part4);
    } else if (index1 != nullptr) {
        M5Dial.Display.printf("in %s\n", part2);
    }
    delay(disp_data_view_delay);
}

void disp_msg(String str) {
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setBrightness(200);  // Make more brightness than normal
  M5Dial.Display.clear();
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setTextColor(TFT_NAVY); // (BLUE);
  M5Dial.Display.drawString(str, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
  //M5Dial.Display.drawString(str2, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
  delay(6000);
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setBrightness(50); // Restore brightness to normal
  M5Dial.Display.clear();
}

bool connect_WiFi(void) {
  #define WIFI_SSID     SECRET_SSID // "YOUR WIFI SSID NAME"
  #define WIFI_PASSWORD SECRET_PASS //"YOUR WIFI PASSWORD"
  bool ret = false;
  WiFi.begin( WIFI_SSID, WIFI_PASSWORD );

  for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
    delay(500);
  
  if (WiFi.status() == WL_CONNECTED) {
    ret = true;
    static constexpr const char txt3[] PROGMEM = "\r\nWiFi Connected";
    Serial.println(txt3);
  }
  else {
    static constexpr const char txt6[] PROGMEM = "WiFi connection failed.";
    Serial.printf("\r\n %s\n", txt6);
  }
  return ret;
}

bool ck_BtnA() {
  bool buttonPressed = false;
  M5Dial.update();
  //if (M5Dial.BtnA.isPressed())
  if (M5Dial.BtnA.wasPressed()) { // 100 mSecs
    buttonPressed = true;
  }
  return buttonPressed;
}

bool lastTouchState = false;
unsigned long lastDebounceTime = 0;

unsigned int ck_touch(void) {
  unsigned long debounceDelay = 50; // 50 milliseconds debounce delay
  bool touchState = false;
  unsigned int ck_touch_cnt = 0;

  M5Dial.update();

  auto t = M5Dial.Touch.getDetail();
  bool currentTouchState = t.state; // M5.Touch.ispressed();

  if (currentTouchState != lastTouchState)
    lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentTouchState != touchState)
    {
      touchState = currentTouchState;
      if (touchState)
      {
        ck_touch_cnt++;  // increase global var
        // The maximum value of an unsigned int is 4294967295
        if (ck_touch_cnt >= (4294967295 - 1))
          ck_touch_cnt = 4294967295 - 1; // keep it below the maximum. Prevent an overflow
      }
    }
  }
  lastTouchState = currentTouchState;
  return ck_touch_cnt;
}

std::string intToHex(int value) {
    std::stringstream ss;
    ss << std::hex << value;
    return ss.str();
}

bool ck_RFID(void) {
  #define MY_RFID_TAG_HEX SECRET_MY_RFID_TAG_NR_HEX
  bool lCardIsOK = false;
  M5Dial.Rfid.begin();
  delay(1000);
  if (M5Dial.Rfid.PICC_IsNewCardPresent() &&
      M5Dial.Rfid.PICC_ReadCardSerial()) {
    M5Dial.Display.clear();
    uint8_t piccType = M5Dial.Rfid.PICC_GetType(M5Dial.Rfid.uid.sak);
    // Check is the PICC of Classic MIFARE type
    if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
          piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
          piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
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

    for (byte i = 0; i < M5Dial.Rfid.uid.size; i++) {    // Output the stored UID data.
      std::string hexString1 = intToHex(M5Dial.Rfid.uid.uidByte[i]);
      uid += hexString1;
    }
    bool lCkEq = true;

    /* Check if the presented RFID card has the same ID as that of the ID in secret.h */
    for (int i = 0; i < le; i++) {
      if (uid[i] != sMyRFID[i]) {
        lCkEq = false; // RFID Card not OK
        break;
      }
    }
    lCardIsOK = lCkEq;
  }
  M5Dial.Rfid.PICC_HaltA();
  M5Dial.Rfid.PCD_StopCrypto1();
  return lCardIsOK;
}

int calc_x_offset(const char* t, int ch_width_in_px) {
  int le = strlen(t);
  int char_space = 1;
  int ret = (M5Dial.Display.width() - ((le * ch_width_in_px) + ((le -1) * char_space) )) / 2;
  return (ret < 0) ? 0 : ret;
}

void start_scrn(void) {
  static constexpr const char* txt[] PROGMEM = {"TIMEZONES", "by Paulus", "Github", "@PaulskPt"};
  static constexpr int char_width_in_pixels[] PROGMEM = {16, 12, 12, 14};
  static constexpr const int vert2[] PROGMEM = {0, 60, 90, 120, 150}; 
  int x = 0;

  M5.Display.clear(TFT_BLACK);
  M5Dial.Display.setTextColor(TFT_RED, TFT_BLACK);
  //M5Dial.Display.setFont(&fonts::FreeSans18pt7b);
  for (int i = 0; i < 4; ++i) {
    x = calc_x_offset(txt[i], char_width_in_pixels[i]);
    // Serial.printf("start_scrn(): x = %d\n", x);
    M5Dial.Display.setCursor(x, vert2[i + 1]);
    M5Dial.Display.println(txt[i]);
  }
  //delay(5000);
  M5Dial.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
  //M5Dial.Display.setFont(&fonts::FreeSans12pt7b); // was: efontCN_14);
}

void send_cmd_to_AtomEcho(void) {
    digitalWrite(PORT_B_GROVE_IN_PIN, LOW);
    digitalWrite(PORT_B_GROVE_OUT_PIN, HIGH);
    delay(100);
    digitalWrite(PORT_B_GROVE_OUT_PIN, LOW);
    delay(100); 
}

void setup(void) {
  M5.begin();  
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;

  // Set the log level to debug
  /* Using esp_log_level_set("*", ESP_LOG_DEBUG) ensures all logging levels are included. */
  // esp_log_level_set("*", ESP_LOG_DEBUG);   
  // LOGD appears not to function! See in loop(). I thinkg the reason was I did not use "#include <esp_log.h>"
  // void M5_DIAL::begin(bool enableEncoder, bool enableRFID)
  // void M5_DIAL::begin(m5::M5Unified::config_t cfg, bool enableEncoder, bool enableRFID) {
  M5Dial.begin(cfg, false, true);

  M5Dial.Power.begin();

  // Pin settings for communication with M5Dial to receive commands from M5Dial
  // commands to start a beep.
  pinMode(PORT_B_GROVE_OUT_PIN, OUTPUT);
  digitalWrite(PORT_B_GROVE_OUT_PIN, LOW); // Turn Idle the output pin
  pinMode(PORT_B_GROVE_IN_PIN, INPUT);
  digitalWrite(PORT_B_GROVE_IN_PIN, LOW); // Turn Idle the input pin

  Serial.begin(115200);

  static constexpr const char txt2[] PROGMEM = "M5Stack M5Dial Timezones and M5Atom Echo with RFID test.";
  Serial.print("\n\n");
  Serial.println(txt2);
  M5Dial.Display.init();
  M5Dial.Display.setBrightness(50);  // 0-255
  M5Dial.Display.setRotation(0);
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
  M5Dial.Display.setColorDepth(1); // mono color
  M5Dial.Display.setFont(&fonts::FreeSans12pt7b);
  M5Dial.Display.setTextWrap(false);
  M5Dial.Display.setTextSize(1);

  start_scrn();

  create_maps();  // creeate zones_map

  delay(1000);

  /* Try to establish WiFi connection. If so, Initialize NTP, */
  if (connect_WiFi()) {
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

    esp_sntp_initialize();  // name sntp_init() results in compilor error "multiple definitions sntp_init()"
    //sntp_sync_status_t sntp_sync_status = sntp_get_sync_status();
    int status = esp_sntp_get_sync_status();
    zone_idx = 0; // needed to set here. Is needed in setTimezone()
    setTimezone();
  }
  M5.Display.clear(TFT_BLACK);
}

void loop() {
  static constexpr const char txt0[] PROGMEM = "loop() running on core ";
  Serial.print(txt0);
  Serial.println(xPortGetCoreID());

  const char* txts[] PROGMEM = {    // longest string 14 (incl \0)
  "Waking up!",     //  0
  "asleep!",        //  1
  "Reset...",       //  2
  "Bye...",         //  3
  "Going ",         //  4
  "display state",  //  5
  " changed to: ",  //  6
  "RTC updated ",   //  7
  "with SNTP ",     //  8 
  "datetime",       //  9
  "Free heap: "     // 10
  };

  const unsigned long zone_chg_interval_t PROGMEM = 25 * 1000L; // 25 seconds
  unsigned long zone_chg_curr_t = 0L;
  unsigned long zone_chg_elapsed_t = 0L;
  unsigned long zone_chg_start_t = millis();
  unsigned int touch_cnt = 0;
  int connect_try = 0;
  bool i_am_asleep = false;
  bool display_state = display_on;
  
  /* use_rfid flag:
  *  if true: use RFID card to put asleep/awake the display. 
  *  if false: use touches  to put asleep/awake the display.
  */
  bool use_rfid = true; 

  while (true) {
    if (use_rfid) {
      if (ck_RFID()) {
        touch_cnt++;
      }
    } else {
        touch_cnt = ck_touch();
    }
  
    if (touch_cnt > 0) {
      touch_cnt = 0; // reset

      display_on = (display_on == true) ? false : true; // flip the display_on flag
      if (display_on != display_state)
      {
        display_state = display_on;
        Serial.print(txts[5]);
        Serial.print(txts[6]);
        if (display_state == true)
          Serial.println(F("On"));
        else
          Serial.println(F("Off"));
      }
      if (display_on) {
        if (i_am_asleep) {
          // See: https://github.com/m5stack/m5-docs/blob/master/docs/en/api/lcd.md
          M5.Display.wakeup();
          i_am_asleep = false;
          M5.Display.setBrightness(50);  // 0 - 255
          disp_msg(txts[0]);
          M5Dial.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
          M5.Display.clear(TFT_BLACK);
        }
      } else {
        if (!i_am_asleep) {
          // See: https://github.com/m5stack/m5-docs/blob/master/docs/en/api/lcd.md
          // M5Dial.Power.powerOff(); // shutdown
          char* result = new char[15];
          strcpy(result, txts[4]); // Copy the first string into result
          strcat(result, txts[1]);
          disp_msg(result);
          delete[] result;  // free memory
          M5.Display.sleep();
          i_am_asleep = true;
          M5.Display.setBrightness(0);
          M5Dial.Display.fillScreen(TFT_BLACK); 
        }
      }
    }

    if (ck_BtnA()) {
      // We have a button press so do a software reset
      disp_msg(txts[2]); // there is already a wait of 6000 in disp_msg()
      //delay(3000);
      esp_restart();
    } else {
      if (WiFi.status() != WL_CONNECTED) { // Check if we're still connected to WiFi
        if (connect_WiFi())
          connect_try = 0;  // reset count
        else
          connect_try++;

        if (connect_try >= 10) {
          break;
        }
      }

      if (sync_time || lStart) { 
        if (sync_time) {
          if (initTime()) {
            if (set_RTC()) {
                sync_time = false;
                Serial.printf("%s%s%s\n", txts[7], txts[8], txts[9]);  // was: txt[16]
                Serial.printf("%s%u\n", txts[10], ESP.getFreeHeap()); 
            }
          }
        }
      }
      
      zone_chg_curr_t = millis();
      zone_chg_elapsed_t = zone_chg_curr_t - zone_chg_start_t;

      /* Do a zone change */
      if ( lStart || (zone_chg_elapsed_t >= zone_chg_interval_t) ) {
        if (lStart) {
          zone_idx = -1; // will be increased in code below
          //lStart = false;
        }
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
        setTimezone();
        TimeToChangeZone = false;
        if (display_on) {
          disp_data();
        }
      }
    }
    if (display_on) {
        disp_data();
        //delay(1000);  // Wait 1 second
    }
    lStart = false;
    M5Dial.update(); // read btn state etc.
  }
  disp_msg(txts[3]);
  //M5Dial.update();
  /* Go into an endless loop after WiFi doesn't work */
  do {
    delay(5000);
  } while (true);
}
