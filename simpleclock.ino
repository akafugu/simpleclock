/*
 * simpleclock - clock backpack for TWIDisplay
 * (C) 2012 Akafugu Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR Aw
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 */
 
/* mods by William B. Phelps
 *
 * 07nov2012 - port Auto DST from VFD clock, also show Alarm time when switch changed
 * more distinct startup tones
 *
 * 06nov2012 - new menu code, 12 hour time display no leading zero
 *
 */

//#define FEATURE_AUTO_DST

#include <EEPROM.h>
#include <Wire.h>
#include <TWIDisplay.h>
#include <WireRtcLib.h>

#include "onewire.h"
#include "button.h"
#include "pitches.h"
#ifdef FEATURE_AUTO_DST
#include "adst.h"
#endif // FEATURE_AUTO_DST 

WireRtcLib rtc;

#define SLAVE_ADDR 18
TWIDisplay disp(SLAVE_ADDR);

struct BUTTON_STATE buttons;
uint8_t g_alarming = false; // alarm is going off
uint8_t g_alarm_switch;
bool g_update_rtc = true;
bool g_update_scroll = false;

// Cached settings
uint8_t g_24h_clock = false;  // wbp temp ???
uint8_t g_show_temp = false;
uint8_t g_brightness = 10;
uint8_t g_show_special_cnt = 10;  // show alarm time for 1 second

uint8_t g_dateyear = 12;
uint8_t g_datemonth = 1;
uint8_t g_dateday = 1;
uint8_t g_autodate = true;

#ifdef FEATURE_AUTO_DST
uint8_t g_DST_mode;  // DST off, on, auto?
uint8_t g_DST_offset;  // DST offset in Hours
uint8_t g_DST_updated = false;  // DST update flag = allow update only once per day
//DST_Rules dst_rules = {{10,1,1,2},{4,1,1,2},1};   // DST Rules for parts of OZ including NSW (for JG)
DST_Rules dst_rules = {{3,1,2,2},{11,1,1,2},1};   // initial values from US DST rules as of 2011
// DST Rules: Start(month, dotw, week, hour), End(month, dotw, week, hour), Offset
// DOTW is Day of the Week, 1=Sunday, 7=Saturday
// N is which occurrence of DOTW
// Current US Rules:  March, Sunday, 2nd, 2am, November, Sunday, 1st, 2 am, 1 hour
const DST_Rules dst_rules_lo = {{1,1,1,0},{1,1,1,0},0};  // low limit
const DST_Rules dst_rules_hi = {{12,7,5,23},{12,7,5,23},1};  // high limit
#endif // FEATURE_AUTO_DST 

#define S24H_MODE_POS 0
#define SHOW_TEMP_POS 1
#define BRIGHTNESS_POS 2
#define DATE_YEAR_POS 3
#define DATE_MONTH_POS 4
#define DATE_DAY_POS 5

#ifdef FEATURE_AUTO_DST
#define DST_MODE_POS 6
#define DST_OFFSET_POS 7
#define DST_UPDATED_POS 8
#endif // FEATURE_AUTO_DST 

#define AUTODATE_POS 9
#define REGION_POS 10

#define SQW_PIN 2
#define BUTTON_1_PIN 3
#define BUTTON_2_PIN 4
#define ALARM_SWITCH_PIN 5 

#define PIEZO_1 9
#define PIEZO_2 10

// menu states
typedef enum {
  // basic states
  STATE_CLOCK = 0,
  STATE_SET_CLOCK,
  STATE_SET_ALARM,
  // menu
  STATE_MENU_BRIGHTNESS,
  STATE_MENU_24H,
  STATE_MENU_YEAR,
  STATE_MENU_MONTH,
  STATE_MENU_DAY,
  STATE_MENU_AUTODATE,
  STATE_MENU_REGION,
#ifdef FEATURE_AUTO_DST
  STATE_MENU_DST,
#endif // FEATURE_AUTO_DST  
  STATE_MENU_TEMP,
  STATE_MENU_LAST,
} state_t;

state_t clock_state = STATE_CLOCK;
bool menu_b1_first = false;

// display modes
typedef enum {
  MODE_NORMAL = 0, // HH.MM
  MODE_SECONDS,    //  SS 
  MODE_TEMP,       // XX.XC
  MODE_LAST,  // end of display modes for right button pushes
  MODE_ALARM_TEXT,  // "ALRM"
  MODE_ALARM_TIME,  // HH.MM
  MODE_AUTO_DATE,   // scroll date across the screen
} display_mode_t;

display_mode_t clock_mode = MODE_NORMAL;

// date format modes
typedef enum {
  FORMAT_YMD = 0,
  FORMAT_DMY,
  FORMAT_MDY,
} date_format_t;

date_format_t g_date_format = FORMAT_YMD;

bool g_blink; // flag to control when to blink the display
bool g_blank; // flag to control if the display is to blanked out or not

String g_date_string; // string holding the date to scroll across the screen in ADATE mode
uint8_t g_date_scroll_offset; // offset used when scrolling date across the screen


#define MENU_TIMEOUT 200  // (160?)

WireRtcLib::tm* t;
uint16_t temp;

void setup()
{
  //Serial.begin(9600);
  
  pinMode(PIEZO_1, OUTPUT);
  pinMode(PIEZO_2, OUTPUT);
  digitalWrite(PIEZO_2, LOW);
  
  pinMode(BUTTON_1_PIN, INPUT);
  pinMode(BUTTON_2_PIN, INPUT);
  pinMode(ALARM_SWITCH_PIN, INPUT);
  
  // enable pull-ups
  digitalWrite(BUTTON_1_PIN, HIGH);
  digitalWrite(BUTTON_2_PIN, HIGH);
  digitalWrite(ALARM_SWITCH_PIN, HIGH);
  
  Wire.begin();
  rtc.begin();
  rtc.runClock(true);
  
  disp.setRotateMode();
  disp.clear();
  //disp.begin(8);

  rtc.setDS1307();
  rtc.SQWSetFreq(WireRtcLib::FREQ_1);
  rtc.SQWEnable(true);
  
  start_meas();
  read_meas();
  
  // initialize timer
  // First disable the timer overflow interrupt while we're configuring
  //TIMSK2 &= ~(1<<TOIE2);  
  
  // Configure timer2 in normal mode (pure counting, no PWM etc.)
  TCCR2A &= ~((1<<WGM21) | (1<<WGM20));  
  //TCCR2B &= ~(1<<WGM22);  

  // Select clock source: internal I/O clock
  ASSR &= ~(1<<AS2);  
  
  // Disable Compare Match A interrupt enable (only want overflow)
  TIMSK2 &= ~(1<<OCIE2A);  
  
  // Now configure the prescaler to CPU clock divided by 128
  TCCR2B |= (1<<CS22)  | (1<<CS20); // Set bits  
  TCCR2B &= ~(1<<CS21);             // Clear bit  
  
  TIMSK2 |= (1<<TOIE2);  
  
  g_alarm_switch = !digitalRead(ALARM_SWITCH_PIN);

  // set up interrupt for alarm switch and RTC SQW
  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT18);
  PCMSK2 |= (1 << PCINT21);
  
  g_24h_clock = EEPROM.read(S24H_MODE_POS);	
  g_show_temp = EEPROM.read(SHOW_TEMP_POS);	
  g_brightness = EEPROM.read(BRIGHTNESS_POS);

  g_dateyear = EEPROM.read(DATE_YEAR_POS);
  if (g_dateyear>29)  g_dateyear = 12;
  g_datemonth = EEPROM.read(DATE_MONTH_POS);
  if (g_datemonth>12)  g_dateyear = 1;
  g_dateday = EEPROM.read(DATE_DAY_POS);
  if (g_dateday>31)  g_dateyear = 1;

#ifdef FEATURE_AUTO_DST
  g_DST_mode = EEPROM.read(DST_MODE_POS);
  g_DST_offset = EEPROM.read(DST_OFFSET_POS);
  if (g_DST_offset>1)  g_DST_offset = 0;
  g_DST_updated = EEPROM.read(DST_UPDATED_POS);
#endif // FEATURE_AUTO_DST 

  if (g_brightness > 10 || g_brightness < 0) g_brightness = 10;
  disp.setBrightness(g_brightness*10);

  disp.print("----");
  tone(9, NOTE_A4, 100);
  delay(200);  // 100 ms pause, tone() is asynchronous
  tone(9, NOTE_A6, 100);
  delay(200);
  tone(9, NOTE_A4, 100);
  delay(200);
  
  update_time();
}

// Alarm switch changed / SQW interrupt
ISR( PCINT2_vect )
{
  uint8_t sw = !digitalRead(ALARM_SWITCH_PIN);
  if (sw != g_alarm_switch) {  // switch changed?
    g_alarm_switch = sw;
    clock_mode = MODE_ALARM_TEXT;
    g_show_special_cnt = 10;  // show alarm text for 1 second
  }
  g_update_rtc = true;
  g_update_scroll = true;
  g_blank = digitalRead(SQW_PIN);
}

ISR(TIMER2_OVF_vect) {
  button_timer();
}

void printRightJustified(int num)
{
  disp.setPosition(0);
  
  if (num < 10)
    disp.print(' ');
  if (num < 100)
    disp.print(' ');
  if (num < 1000)
    disp.print(' ');

  disp.print(num);  
}

void printRightJustified(const char* str)
{
  size_t len = strlen(str);
  
  if (len < 2)
    disp.print(' ');
  if (len < 3)
    disp.print(' ');
  if (len < 4)
    disp.print(' ');
  
  disp.print(str);
}

void print2(int num)
{
  if (num < 10)
    disp.print('0');
   disp.print(num); 
}

void print_temp()
{
    disp.setPosition(0);
    disp.print(temp/10, DEC);
    disp.print('C');
    disp.setDot(1, true);
}

void alarm()
{
  tone(9, NOTE_A4, 500);
  delay(100);
  tone(9, NOTE_C3, 750);
  delay(100);
  tone(9, NOTE_A4, 500);
  delay(500);
}

void update_display(uint8_t mode)
{
  uint8_t hour = 0, min = 0, sec = 0;
  disp.setDot(3, g_alarm_switch);

  if (clock_mode == MODE_ALARM_TEXT) {
    disp.clear();
    disp.print("ALRM");
  }
  else if (clock_mode == MODE_ALARM_TIME) {
    disp.setDot(1, false);
    if (g_alarm_switch) {
      rtc.getAlarm_s(&hour, &min, &sec);
      disp.writeTime(hour, min, sec);
    }  
    else {
      disp.print("OFF ");
    }
  }
  // show temperature for a few seconds each minute (depending on TEMP setting)
  else if (g_show_temp && 
           (clock_mode == MODE_NORMAL || clock_mode == MODE_SECONDS) && 
           t->sec >= 45 &&t->sec <= 49) {
    print_temp();
  }
  // scroll date across the screen once a minute (depending on ADTE setting)
  else if (g_autodate && clock_mode == MODE_NORMAL &&t->sec == 5) {
    disp.setDot(1, false);
    disp.clear();
    disp.setScrollMode();
    clock_mode = MODE_AUTO_DATE;
  }
  else if (clock_mode == MODE_NORMAL) {
      if (g_24h_clock)
        disp.writeTime(t->hour, t->min, t->sec);
      else
        disp.writeTime12h(t->twelveHour, t->min, t->sec);
  }
  else if (clock_mode == MODE_SECONDS) {
    disp.setPosition(0);
    if (g_24h_clock) {
      disp.print(' ');
      print2(t->sec);
      disp.print(' ');
    }
    else {
      disp.print(t->am ? "AM" : "PM");
      print2(t->sec);
    }
    disp.setDot(1, false);    
  }
  else if (clock_mode == MODE_TEMP) {
    print_temp();    
  }
  else if (clock_mode == MODE_AUTO_DATE) {
    if (g_date_scroll_offset == 14) { // finished scrolling, go back to normal
      g_date_scroll_offset = 0;
      disp.setRotateMode();
      clock_mode = MODE_NORMAL;
    }
    else if (g_update_scroll) {
      disp.print(g_date_string[g_date_scroll_offset]);
      g_date_scroll_offset++;
      g_update_scroll = false;
    }
  }
}

void set_blink(bool on)
{
  g_blink = on;
}

void show_setting(const char* str, int value, bool show_setting)
{
  disp.clear();
  if (show_setting)
    printRightJustified(value);
  else
    disp.print(str);
}

void show_setting(const char* str, const char* value, bool show_setting)
{
  disp.clear();
  if (show_setting)
    printRightJustified(value);
  else
    disp.print(str);
}

void update_date_string(WireRtcLib::tm* t)
{
  if (!t) return;
  
  String temp;
  
  switch (g_date_format) {
  case FORMAT_YMD:
    temp.concat(t->year+2000);
    temp.concat('-');
    if (t->mon < 10) temp.concat('0');
    temp.concat(t->mon);
    temp.concat('-');
    if (t->mday < 10) temp.concat('0');    
    temp.concat(t->mday);
    break;
  case FORMAT_DMY:
    if (t->mday < 10) temp.concat('0');
    temp.concat(t->mday);
    temp.concat('-');
    if (t->mon < 10) temp.concat('0');
    temp.concat(t->mon);
    temp.concat('-');
    temp.concat(t->year+2000);
    break;
  case FORMAT_MDY:
    if (t->mon < 10) temp.concat('0');
    temp.concat(t->mon);
    temp.concat('-');
    if (t->mday < 10) temp.concat('0');
    temp.concat(t->mday);
    temp.concat('-');
    temp.concat(t->year+2000);
    break;
  }

  temp.concat("    ");

  g_date_string = temp;
  
  // Serial.println(g_date_string);
}

void update_time()
{
  // update time
  t = rtc.getTime();
  
  // update date string
  update_date_string(t);
      
  // Read temperature from 1-wire temperature sensor
  start_meas();
  temp = read_meas();

  g_dateyear = t->year;  // save year for Menu
  g_datemonth = t->mon;  // save month for Menu
  g_dateday = t->mday;  // save day for Menu

#ifdef FEATURE_AUTO_DST
  if (t->sec%10 == 0)  // check DST Offset every 10 seconds
    setDSToffset(g_DST_mode); 
  if ((t->hour == 0) && (t->min == 0) && (t->sec == 0)) {  // MIDNIGHT!
    g_DST_updated = false;
    DSTinit(t, &dst_rules);  // re-compute DST start, end
  }
#endif // FEATURE_AUTO_DST 

  g_update_rtc = false;  
}

#ifdef FEATURE_AUTO_DST
void setDSToffset(uint8_t mode) {
  int8_t adjOffset;
  uint8_t newOffset;
  if (mode == 2) {  // Auto DST
    if (g_DST_updated) return;  // already done it once today
    if (t == NULL) return;  // safet check
    newOffset = getDSToffset(t, &dst_rules);  // get current DST offset based on DST Rules
  }
  else
    newOffset = mode;  // 0 or 1
  adjOffset = newOffset - g_DST_offset;  // offset delta
  if (adjOffset == 0)  return;  // nothing to do
  if (adjOffset > 0)
    tone(9, NOTE_A5, 100);  // spring ahead
  else
    tone(9, NOTE_A4, 100);  // fall back
  t = rtc.getTime();  // refresh current time;
  t->year = y2kYearToTm(t->year);
  unsigned long tNow = makeTime(t);  // fetch current time from RTC as time_t
  tNow += adjOffset * SECS_PER_HOUR;  // add or subtract new DST offset
  breakTime(tNow, t);
  t->year = tmYearToY2k(t->year);  // remove 1970 offset
  rtc.setTime(t);  // adjust RTC
  g_DST_offset = newOffset;
  EEPROM.write(DST_OFFSET_POS, g_DST_offset);
  g_DST_updated = true;
}
#endif // FEATURE_AUTO_DST

void set_date(uint8_t yy, uint8_t mm, uint8_t dd) {
  t = rtc.getTime();
  t->year = yy;
  t->mon = mm;
  t->mday = dd;
  rtc.setTime(t);
  
#ifdef FEATURE_AUTO_DST
  DSTinit(t, &dst_rules);  // re-compute DST start, end for new date
  g_DST_updated = false;  // allow automatic DST adjustment again
  setDSToffset(g_DST_mode);  // set DSToffset based on new date
#endif // FEATURE_AUTO_DST 
}

void menu(bool update, bool show)
{
  switch (clock_state) {
    case STATE_MENU_BRIGHTNESS:
      if (update) {
        g_brightness++;
      
        if (g_brightness > 10) g_brightness = 1;

        EEPROM.write(BRIGHTNESS_POS, g_brightness);
        disp.setBrightness(g_brightness*10);
      }
      show_setting("BRIT", g_brightness, show);
      break;
    case STATE_MENU_24H:
      if (update) {
        g_24h_clock = !g_24h_clock;
					
        EEPROM.write(S24H_MODE_POS, g_24h_clock);	
      }
      show_setting("24H", g_24h_clock ? " on" : " off", show);
      break;
    case STATE_MENU_YEAR:
      if (update) {
        g_dateyear++;
        if (g_dateyear > 29) g_dateyear = 10;
        EEPROM.write(DATE_YEAR_POS, g_dateyear);
        set_date(g_dateyear, g_datemonth, g_dateday);
      }
      show_setting("YEAR", g_dateyear, show);
      break;
    case STATE_MENU_MONTH:
      if (update) {
        g_datemonth++;
        if (g_datemonth > 12) g_datemonth = 1;
        EEPROM.write(DATE_MONTH_POS, g_datemonth);
        set_date(g_dateyear, g_datemonth, g_dateday);
      }
      show_setting("MNTH", g_datemonth, show);
      break;
    case STATE_MENU_DAY:
      if (update) {
        g_dateday++;
        if (g_dateday > 31) g_dateday = 1;
        EEPROM.write(DATE_DAY_POS, g_dateday);
        set_date(g_dateyear, g_datemonth, g_dateday);
      }
      show_setting("DAY", g_dateday, show);
      break;
    case STATE_MENU_AUTODATE:
      if (update) {
        g_autodate = !g_autodate;
        
        EEPROM.write(AUTODATE_POS, g_autodate);
      }
    
      show_setting("ADTE", g_autodate ? " on" : " off", show);
      break;
    case STATE_MENU_REGION:
      if (update) {
        if (g_date_format == FORMAT_YMD)
          g_date_format = FORMAT_MDY;
        else if (g_date_format == FORMAT_MDY)
          g_date_format = FORMAT_DMY;
        else if (g_date_format == FORMAT_DMY)
          g_date_format = FORMAT_YMD;
        
        EEPROM.write(REGION_POS, g_date_format);
      }
    
      if (g_date_format == FORMAT_YMD)
        show_setting("REGN", "YMD", show);
      else if (g_date_format == FORMAT_MDY)
        show_setting("REGN", "MDY", show);
      else if (g_date_format == FORMAT_DMY)
        show_setting("REGN", "DMY", show);
      
      break;
#ifdef FEATURE_AUTO_DST
    case STATE_MENU_DST:
      if (update) {	
        g_DST_mode = (g_DST_mode+1)%3;  //  0: off, 1: on, 2: auto
        EEPROM.write(DST_MODE_POS, g_DST_mode);
        g_DST_updated = false;  // allow automatic DST adjustment again
        setDSToffset(g_DST_mode);
      }
      show_setting("DST", dst_setting(g_DST_mode), show);
      break;
#endif // FEATURE_AUTO_DST 
    case STATE_MENU_TEMP:
      if (update) {
        g_show_temp = !g_show_temp;
        
        EEPROM.write(SHOW_TEMP_POS, g_show_temp);
      }
      show_setting("TEMP", g_show_temp ? " on" : " off", show);
      break;
    default:
      break; // do nothing
  }
}

void loop()
{
  uint8_t hour = 0, min = 0, sec = 0;

  // Counters used when setting time
  int16_t time_to_set = 0;
  uint16_t button_released_timer = 0;
  uint16_t menu_timer = 0;
  uint16_t button_speed = 25;

  while (1) {
    get_button_state(&buttons);

    if (g_update_rtc) {
      update_time();
    }

    // When alarming:
    // any button press cancels alarm
    if (g_alarming) {
      update_display(clock_mode);

      if (buttons.b1_keydown || buttons.b1_keyup || buttons.b2_keydown || buttons.b2_keyup) {
        buttons.b1_keyup = 0; // clear state
        buttons.b2_keyup = 0; // clear state
        g_alarming = false;
        continue;
      }
      else {
        alarm();
      }
    }
    // If both buttons are held:
    //  * If the ALARM BUTTON SWITCH is on the BOTTOM, go into set time mode
    //  * If the ALARM BUTTON SWITCH is on the TOP, go into set alarm mode
    else if (clock_state == STATE_CLOCK && buttons.both_held) {
      if (g_alarm_switch) {
        clock_state = STATE_SET_ALARM;
        disp.clear();
        disp.print("ALRM");
        rtc.getAlarm_s(&hour, &min, &sec);
        time_to_set = hour*60 + min;
      }
      else {
        clock_state = STATE_SET_CLOCK;
        disp.clear();
        disp.print("TIME");
        rtc.getTime_s(&hour, &min, &sec);
        time_to_set = hour*60 + min;
      }
      
      set_blink(true);
			
      // wait until both buttons are released
      while (1) {
        delay(50);
        get_button_state(&buttons);
        if (buttons.none_held)
          break;
      }
    }
    // Set time or alarm
    else if (clock_state == STATE_SET_CLOCK || clock_state == STATE_SET_ALARM) {
      // Check if we should exit STATE_SET_CLOCK or STATE_SET_ALARM
      if (buttons.none_held) {
        set_blink(true);
        button_released_timer++;
        button_speed = 25;
      }
      else {
        set_blink(false);
        button_released_timer = 0;
        button_speed++;
      }
      // exit mode after no button has been touched for a while
      if (button_released_timer >= MENU_TIMEOUT) {
        set_blink(false);
        button_released_timer = 0;
        button_speed = 1;
				
        if (clock_state == STATE_SET_CLOCK)
          rtc.setTime_s(time_to_set / 60, time_to_set % 60, 0);
	else
          rtc.setAlarm_s(time_to_set / 60, time_to_set % 60, 0);

        clock_state = STATE_CLOCK;
      }

      // Increase / Decrease time counter
      if (buttons.b1_repeat) time_to_set+=(button_speed/50);
      if (buttons.b1_keyup)  time_to_set++;
      if (buttons.b2_repeat) time_to_set-=(button_speed/50);
      if (buttons.b2_keyup)  time_to_set--;

      if (time_to_set  >= 1440) time_to_set = 0;
      if (time_to_set  < 0) time_to_set = 1439;

      if (g_blink && g_blank) {
        disp.clear();
      }
      else {
        disp.setPosition(0);
        print2(time_to_set/60);
        print2(time_to_set%60);
      
        disp.setDot(1, true);
      }
    }
    // Left button enters menu
    else if (clock_state == STATE_CLOCK && buttons.b2_keyup) {
      // make sure to force-exit auto date mode when entering menu
      if (clock_mode == MODE_AUTO_DATE)
        clock_mode = MODE_NORMAL;
      
      clock_state = STATE_MENU_BRIGHTNESS;
      menu(false, false);  // show first item in menu
      buttons.b2_keyup = 0; // clear state
    }
    // Right button toggles display mode
    else if (clock_state == STATE_CLOCK && buttons.b1_keyup) {
      // special handling for auto date mode: exit back to normal mode
      if (clock_mode == MODE_AUTO_DATE) {
        clock_mode = MODE_NORMAL;
      }
      else {
        clock_mode = (display_mode_t)(clock_mode + 1);
        if (clock_mode == MODE_LAST) clock_mode = MODE_NORMAL;
          buttons.b1_keyup = 0; // clear state
      }
    }
    else if (clock_state >= STATE_MENU_BRIGHTNESS) {
      if (buttons.none_held)
        button_released_timer++;
      else
        button_released_timer = 0;

      if (button_released_timer >= MENU_TIMEOUT) {
        button_released_timer = 0;
        clock_state = STATE_CLOCK;
      }
    
      if (buttons.b1_keyup) {  // right button
        menu(!menu_b1_first, true);
        buttons.b1_keyup = false;
        menu_b1_first = false;  // b1 not first time now
      }  // if (buttons.b1_keyup) 
      if (buttons.b2_keyup) {  // left button
        menu_b1_first = true;  // reset first time flag
        clock_state = (state_t)(clock_state + 1);
      
//        if (clock_state == STATE_MENU_LAST) clock_state = STATE_MENU_BRIGHTNESS;
        if (clock_state == STATE_MENU_LAST) clock_state = STATE_CLOCK;  // 07nov12/wbp
      
        menu(false, false);
        buttons.b2_keyup = 0; // clear state
      }
    }
    else {
      if (g_show_special_cnt>0) {
	g_show_special_cnt--;
	if (g_show_special_cnt == 0)
          switch (clock_mode) {
            case MODE_ALARM_TEXT:
              clock_mode = MODE_ALARM_TIME;
              g_show_special_cnt = 10;  // now show time for 1 second
              break;
            case MODE_ALARM_TIME:
              clock_mode = MODE_NORMAL;
              break;
            default:
              clock_mode = MODE_NORMAL;
          }
      }
      update_display(clock_mode);
      delay(100);
    }
    
    if (g_alarm_switch && rtc.checkAlarm())
      g_alarming = true;
      
    delay(10);
  }
}

