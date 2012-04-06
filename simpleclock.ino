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
}

WireRtcLib rtc;

#define SLAVE_ADDR 18
TWIDisplay disp(SLAVE_ADDR);

void setup()
{
  Wire.begin();
  rtc.begin();
  
  //rtc.setTime_s(22, 49, 00);
  disp.setBrightness(255);
  disp.clear();

  rtc.setDS1307();
  
  start_meas();
  read_meas();
}

void loop()
{
  for (int i = 0; i < 40; i++) {
    WireRtcLib::tm* t = rtc.getTime();

    if (i < 20) {
      disp.writeTime(t->hour, t->min, t->sec);
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
    
    delay(200);
  }
}

