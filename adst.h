/*
 * Auto DST support for VFD Modular Clock
 * (C) 2012 William B Phelps
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

#ifndef ADST_H_
#define ADST_H_

//convenience macros to convert to and from tm years 
#define  tmYearToCalendar(Y) ((Y) + 1970)  // full four digit year 
#define  CalendarYrToTm(Y)   ((Y) - 1970)
#define  tmYearToY2k(Y)      ((Y) - 30)    // offset is from 2000
#define  y2kYearToTm(Y)      ((Y) + 30)   

#define LEAP_YEAR(Y)     ( ((1970+Y)>0) && !((1970+Y)%4) && ( ((1970+Y)%100) || !((1970+Y)%400) ) )
#define SECS_PER_MIN  (60UL)
#define SECS_PER_HOUR (3600UL)
#define SECS_PER_DAY  (SECS_PER_HOUR * 24UL)

typedef struct {
  uint8_t Month; 
  uint8_t DOTW; 
  uint8_t Week; 
  uint8_t Hour;
} DST_Rule;
typedef struct { 
  DST_Rule Start;
  DST_Rule End;
  uint8_t Offset; 
} DST_Rules;

void breakTime(unsigned long time, WireRtcLib::tm* tm);
unsigned long makeTime(WireRtcLib::tm* tm);
char* dst_setting(uint8_t dst);
uint8_t dotw(uint16_t year, uint8_t month, uint8_t day);
void DSTinit(WireRtcLib::tm* te, DST_Rules* rules);
uint8_t getDSToffset(WireRtcLib::tm* te, DST_Rules* rules);

#endif
