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
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 */

#include <EEPROM.h>
#include <Wire.h>
#include <TWIDisplay.h>
#include <WireRtcLib.h>

#include "onewire.h"
#include "button.h"
#include "pitches.h"

WireRtcLib rtc;

#define SLAVE_ADDR 18
TWIDisplay disp(SLAVE_ADDR);

struct BUTTON_STATE buttons;
uint8_t g_alarming = false; // alarm is going off
uint8_t g_alarm_switch;
bool g_update_rtc = true;

// Cached settings
uint8_t g_24h_clock = true;
uint8_t g_show_temp = false;
uint8_t g_brightness = 10;

#define CLOCK_MODE_POS 0
#define SHOW_TEMP_POS 1
#define BRIGHTNESS_POS 2

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
	STATE_MENU_TEMP,
	STATE_MENU_LAST,
} state_t;

state_t clock_state = STATE_CLOCK;

// display modes
typedef enum {
  MODE_NORMAL = 0, // HH.MM
  MODE_SECONDS,    //  SS 
  MODE_TEMP,       // XX.XC
  MODE_LAST,
} display_mode_t;

display_mode_t clock_mode = MODE_NORMAL;

bool g_blink;
bool g_blank;

#define MENU_TIMEOUT 160

WireRtcLib::tm* t;
uint16_t temp;

void setup()
{
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
  
  
  g_24h_clock = EEPROM.read(CLOCK_MODE_POS);	
  g_show_temp = EEPROM.read(SHOW_TEMP_POS);	
  g_brightness = EEPROM.read(BRIGHTNESS_POS);

  if (g_brightness > 10 || g_brightness < 0) g_brightness = 10;
  disp.setBrightness(g_brightness*10);

  /*
  disp.print("----");
  tone(9, NOTE_A4, 500);
  delay(100);
  tone(9, NOTE_C3, 750);
  delay(100);
  tone(9, NOTE_A4, 500);
  delay(500);
  */
  
  update_time();
}

// Alarm switch changed / SQW interrupt
ISR( PCINT2_vect )
{
  g_alarm_switch = !digitalRead(ALARM_SWITCH_PIN);
  g_update_rtc = true;

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
  static uint8_t counter = 0;
  
  disp.setDot(3, g_alarm_switch);
  
  if (!g_show_temp || counter < 25) {
    if (clock_mode == MODE_NORMAL) {
      if (g_24h_clock)
        disp.writeTime(t->hour, t->min, t->sec);
      else
        disp.writeTime(t->twelveHour, t->min, t->sec);
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
    else {
      print_temp();
    }
  }
  else {
    print_temp();
  }

  counter++;
  if (counter == 50) counter = 0;
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

void update_time()
{
  // update time
  t = rtc.getTime();
      
  // Read temperature from 1-wire temperature sensor
  start_meas();
  temp = read_meas();

  g_update_rtc = false;  
}

void loop()
{
  uint8_t hour = 0, min = 0, sec = 0;

  // Counters used when setting time
  int16_t time_to_set = 0;
  uint16_t button_released_timer = 0;
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
    //  * If the ALARM BUTTON SWITCH is on the LEFT, go into set time mode
    //  * If the ALARM BUTTON SWITCH is on the RIGHT, go into set alarm mode
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

      disp.setPosition(0);
      print2(time_to_set/60);
      print2(time_to_set%60);
      
      disp.setDot(1, true);
    }
    // Left button enters menu
    else if (clock_state == STATE_CLOCK && buttons.b2_keyup) {
      clock_state = STATE_MENU_BRIGHTNESS;
      show_setting("BRIT", g_brightness, false);
      buttons.b2_keyup = 0; // clear state
    }
    // Right button toggles display mode
    else if (clock_state == STATE_CLOCK && buttons.b1_keyup) {
      clock_mode = (display_mode_t)(clock_mode + 1);
      if (clock_mode == MODE_LAST) clock_mode = MODE_NORMAL;
        buttons.b1_keyup = 0; // clear state
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
    
      switch (clock_state) {
        case STATE_MENU_BRIGHTNESS:
          if (buttons.b1_keyup) {
            g_brightness++;
            buttons.b1_keyup = false;
          
            if (g_brightness > 10) g_brightness = 1;

            EEPROM.write(BRIGHTNESS_POS, g_brightness);
            show_setting("BRIT", g_brightness, true);
            disp.setBrightness(g_brightness*10);
          }
	  break;
        case STATE_MENU_24H:
          if (buttons.b1_keyup) {
            g_24h_clock = !g_24h_clock;
					
            EEPROM.write(CLOCK_MODE_POS, g_24h_clock);	
            show_setting("24H", g_24h_clock ? " on" : " off", true);
            buttons.b1_keyup = false;
          }
          break;
        case STATE_MENU_TEMP:
          if (buttons.b1_keyup) {
            g_show_temp = !g_show_temp;
            
            EEPROM.write(SHOW_TEMP_POS, g_show_temp);
            show_setting("TEMP", g_show_temp ? " on" : " off", true);
            buttons.b1_keyup = false;
          }
          break;
        default:
          break; // do nothing
      }
    
      if (buttons.b2_keyup) {
        clock_state = (state_t)(clock_state + 1);
      
        if (clock_state == STATE_MENU_LAST) clock_state = STATE_MENU_BRIGHTNESS;
      
        switch (clock_state) {
          case STATE_MENU_BRIGHTNESS:
            show_setting("BRIT", g_brightness, false);
            break;
          case STATE_MENU_24H:
            show_setting("24H", g_24h_clock ? " on" : " off", false);
            break;
            case STATE_MENU_TEMP:
              show_setting("TEMP", g_show_temp ? " on" : " off", false);
              break;
            default:
              break; // do nothing
        }

        buttons.b2_keyup = 0; // clear state
      }
    }
    else {
      update_display(clock_mode);
      delay(100);
    }
    
    if (g_alarm_switch && rtc.checkAlarm())
      g_alarming = true;
      
    delay(10);
  }
}

