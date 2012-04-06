/*
 * simpleclock - clock backpack for the TWIDisplay
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

#include <Wire.h>
#include <TWIDisplay.h>
#include "WireRtcLib.h"

extern "C" {
  #include "onewire.h"
  #include "button.h"
}

WireRtcLib rtc;

#define SLAVE_ADDR 18
TWIDisplay disp(SLAVE_ADDR);

struct BUTTON_STATE buttons;

// menu states
typedef enum {
	// basic states
	STATE_CLOCK = 0,
	STATE_SET_CLOCK,
	STATE_SET_ALARM,
	// menu
	STATE_MENU_BRIGHTNESS,
	STATE_MENU_24H,
	STATE_MENU_VOL,
	STATE_MENU_TEMP,
	STATE_MENU_DOTS,
	STATE_MENU_LAST,
} state_t;

// display modes
typedef enum {
	MODE_NORMAL = 0, // HH.MM
	MODE_SECONDS,    //  SS 
	MODE_LAST,
} display_mode_t;

display_mode_t clock_mode = MODE_NORMAL;

void setup()
{
  pinMode(3, INPUT);
  pinMode(4, INPUT);
  pinMode(5, INPUT);
  
  // enable pull-ups
  digitalWrite(3, HIGH);
  digitalWrite(4, HIGH);
  digitalWrite(5, HIGH);
  
  Wire.begin();
  rtc.begin();
  
  //rtc.setTime_s(22, 49, 00);
  disp.setBrightness(255);
  disp.clear();

  rtc.setDS1307();
  
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
  
  /*
  g_alarm_switch = get_alarm_switch();

  // set up interrupt for alarm switch
  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT18);
  */
  
}

/*
// Alarm switch changed interrupt
ISR( PCINT2_vect )
{
	if ( (SWITCH_PIN & _BV(SWITCH_BIT)) == 0)
		g_alarm_switch = false;
	else
		g_alarm_switch = true;
}
*/

ISR(TIMER2_OVF_vect) {
  button_timer();
}  


void menu_loop()
{
  while (1) {
   
  } 
}

void loop()
{
  for (int i = 0; i < 40; i++) {
    WireRtcLib::tm* t = rtc.getTime();

    get_button_state(&buttons);


    if (buttons.both_held) {
      disp.clear();
      disp.print("----");
    }
    // Right button toggles display mode
    else if (/*clock_state == STATE_CLOCK &&*/ buttons.b1_keyup) {
      if (clock_mode == MODE_NORMAL) clock_mode = MODE_SECONDS;
      else if (clock_mode == MODE_SECONDS) clock_mode = MODE_NORMAL;
      buttons.b1_keyup = 0; // clear state
    }
    else if (i < 20) {
      if (clock_mode == MODE_NORMAL)
        disp.writeTime(t->hour, t->min, t->sec);
      else {
        disp.clear();
        disp.print(' ');
        if (t->sec < 10) disp.print('0');
        disp.print(t->sec);
      }
    }
    else {
      // Read temperature from 1-wire temperature sensor
      start_meas();
      uint16_t temp = read_meas();

      disp.clear();
      disp.print(temp/10, DEC);
      disp.print('C');
      disp.setDot(1, true);
    }
    
    //if (digitalRead(5)) disp.setDot(3, true);
    
    delay(200);
  }
}

