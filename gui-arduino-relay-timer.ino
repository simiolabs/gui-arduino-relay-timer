/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Simio Labs
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
 
#include <genieArduino.h>
#include <SimpleTimer.h>
#include <Wire.h>
#include <EEPROMex.h>

#define DEBUG
#ifdef DEBUG
#include <SoftwareSerial.h>
#endif

// Visi-Genie objects
#define FORM0         0x00
#define FORM1         0x01
#define FORM2         0x02
#define FORM3         0x03
#define LED_DIG0      0x00
#define LED_DIG1      0x01
#define LED_DIG2      0x02
#define USER_BUTTON0  0x00
#define USER_BUTTON1  0x01
#define USER_BUTTON2  0x02
#define USER_BUTTON3  0x03
#define USER_BUTTON4  0x04
#define USER_BUTTON5  0x05
#define USER_BUTTON6  0x06
#define USER_BUTTON7  0x07
#define USER_BUTTON8  0x08
#define USER_BUTTON9  0x09

#define DS1307_ADDRESS 0x68  // RTC
#define LCD_RESET 2          // LCD reset line
#define RELAY     13         // output to relay

// states
enum TimerState {
  idle,
  manualRun,
  schRun
};

// global variables
int startMinValue = 0;
int startHourValue = 0;
int endMinValue = 0;
int endHourValue = 0;
int elapsedMinValue = 0;
int elapsedHourValue = 0;
int timeDateBuf[7];

TimerState state;
Genie genie;
SimpleTimer increaseCounterTimer;
SimpleTimer checkStartScheduleTimer;
SimpleTimer checkEndScheduleTimer;
#ifdef DEBUG
SoftwareSerial DisplaySerial(10, 11);
#endif

void setup()
{ 
  // set up LCD
#ifdef DEBUG
  Serial.begin(9600);
  DisplaySerial.begin(31250); // max for software serial according to LCD docs
  genie.Begin(DisplaySerial);
#else
  Serial.begin(31250); // keep it as above if you don't want to change the Visi Genie code
  genie.Begin(Serial);
#endif
  genie.AttachEventHandler(myGenieEventHandler); // Attach the user function Event Handler for processing events
  // set up RTC
  Wire.begin();
  //set up counters
  increaseCounterTimer.setInterval(1000, increaseCounterTimerFunction);
  increaseCounterTimer.disable(0);
  checkStartScheduleTimer.setInterval(1000, checkStartScheduleFunction);
  checkEndScheduleTimer.setInterval(1000, checkEndScheduleFunction);
  checkEndScheduleTimer.disable(0);
  // set up output
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);
  // reset LCD
  pinMode(LCD_RESET, OUTPUT);
  digitalWrite(LCD_RESET, 0);
  delay(100);
  digitalWrite(LCD_RESET, 1);
  delay (3500); //let the display start up after the reset
  // start up in idle mode
  state = idle;
}

void loop()
{
  genie.DoEvents(); // this calls the library each loop to process the queued responses from the display
  increaseCounterTimer.run(); // update the counter when on
  checkStartScheduleTimer.run(); // check for start time
  checkEndScheduleTimer.run(); // check for end time
}

// this function increases any time on an LED digits object
void increaseClock(byte index, int *hourValue, int *minValue, int value) 
{  
  *minValue += value;
  if (*minValue == 60) {
    *minValue = 0;
    *hourValue += 100;
  }
  if (*hourValue == 2400) {
    *hourValue = 0;
  }
  genie.WriteObject(GENIE_OBJ_LED_DIGITS, index, *hourValue + *minValue);
}

// this function decreases any time on an LED digits object
void decreaseClock(byte index, int *hourValue, int *minValue, int value) 
{
  *minValue -= value;
  if (*minValue < 0) {
    *minValue = 45;
    *hourValue -= 100;
  }
  if (*hourValue < 0) {
    *hourValue = 2300;
  }
  genie.WriteObject(GENIE_OBJ_LED_DIGITS, index, *hourValue + *minValue);
}

// this function increases a time counter on an LED digits object
void increaseCounterTimerFunction() {
  increaseClock(LED_DIG2, &elapsedHourValue, &elapsedMinValue, 1);
}

// switch to run state
void goToRunState() {
  digitalWrite(RELAY, HIGH); // turn on relay
#ifdef DEBUG
  Serial.println("relay on");
#endif
  increaseCounterTimer.enable(0);
  checkStartScheduleTimer.disable(0);
  if (state == schRun)
    checkEndScheduleTimer.enable(0);
  genie.WriteObject(GENIE_OBJ_FORM, FORM3, 0);
}

// switch to idle state
void goToIdleState() {
  state = idle;
  digitalWrite(RELAY, LOW); // turn off relay
#ifdef DEBUG
  Serial.println("relay off");
#endif
  increaseCounterTimer.disable(0);
  checkStartScheduleTimer.enable(0);
  checkEndScheduleTimer.disable(0);
  genie.WriteObject(GENIE_OBJ_FORM, FORM0, 0);
  // reset elapsed time on LED digits
  elapsedHourValue = 0;
  elapsedMinValue = 0;
  genie.WriteObject(GENIE_OBJ_LED_DIGITS, LED_DIG2, 0);
}

// this is the user's event handler. It is called by genieDoEvents()
// when the following conditions are true: the link is in an IDLE state, 
// and there is an event to handle.
// this function handles every action from the LCD in this case buttons 
// being pressed.
void myGenieEventHandler(void)
{
  genieFrame Event;
  genie.DequeueEvent(&Event);

  // if the cmd received is from a reported event
  if (Event.reportObject.cmd == GENIE_REPORT_EVENT) {
    // if the event was from a user button object
    if (Event.reportObject.object == GENIE_OBJ_USERBUTTON) {
      if (Event.reportObject.index == USER_BUTTON0) {
        state = manualRun,
        goToRunState();
      }
      if (Event.reportObject.index == USER_BUTTON2) {
        checkStartScheduleTimer.disable(0);
        genie.WriteObject(GENIE_OBJ_FORM, FORM1, 0);
      }
      if (Event.reportObject.index == USER_BUTTON3) {
        increaseClock(LED_DIG0, &startHourValue, &startMinValue, 15);
      }
      if (Event.reportObject.index == USER_BUTTON4) {                     
        decreaseClock(LED_DIG0, &startHourValue, &startMinValue, 15);
      }
      if (Event.reportObject.index == USER_BUTTON5) {        
        genie.WriteObject(GENIE_OBJ_FORM, FORM2, 0);
#ifdef DEBUG
        Serial.print("start time: ");
        Serial.print(startHourValue / 100);
        Serial.print(":");
        Serial.println(startMinValue);
#endif
      }
      if (Event.reportObject.index == USER_BUTTON6) {
        increaseClock(LED_DIG1, &endHourValue, &endMinValue, 15);
      }
      if (Event.reportObject.index == USER_BUTTON7) {                     
        decreaseClock(LED_DIG1, &endHourValue, &endMinValue, 15);
      }
      if (Event.reportObject.index == USER_BUTTON8) {
        checkStartScheduleTimer.enable(0);        
        genie.WriteObject(GENIE_OBJ_FORM, FORM0, 0);
#ifdef DEBUG
        Serial.print("end time: ");
        Serial.print(endHourValue / 100);
        Serial.print(":");
        Serial.println(endMinValue);
#endif
      }
      if (Event.reportObject.index == USER_BUTTON9) {
        goToIdleState();
      }
    }
  }
}

byte bcdToDec(byte val) {
// Convert binary coded decimal to normal decimal numbers
  return ( (val/16*10) + (val%16) );
}

// read date from RTC
void readDate(int *buf) {
  // Reset the register pointer
  Wire.beginTransmission(DS1307_ADDRESS);

  byte zero = 0x00;
  Wire.write(zero);
  Wire.endTransmission();

  Wire.requestFrom(DS1307_ADDRESS, 7);

  buf[0] = bcdToDec(Wire.read()); //seconds
  buf[1] = bcdToDec(Wire.read()); //minutes
  buf[2] = bcdToDec(Wire.read() & 0b111111); //24 hour time
  buf[3] = bcdToDec(Wire.read()); //0-6 -> sunday - saturday
  buf[4] = bcdToDec(Wire.read()); //day
  buf[5] = bcdToDec(Wire.read()); //month
  buf[6] = bcdToDec(Wire.read()); //year
}

// print date
void printDate(int *buf) {
  //print the date EG   3/1/11 23:59:59
  Serial.print(buf[4]);
  Serial.print("/");
  Serial.print(buf[5]);
  Serial.print("/");
  Serial.print(buf[6]);
  Serial.print(" ");
  Serial.print(buf[2]);
  Serial.print(":");
  Serial.print(buf[1]);
  Serial.print(":");
  Serial.println(buf[0]);
}

// check if schedule should start
void checkStartScheduleFunction() {
  readDate(timeDateBuf);
  int currentMin = timeDateBuf[2] * 100 + timeDateBuf[1]; // hours * 60 + minutes
  int startMin = startHourValue + startMinValue;
  int endMin = endHourValue + endMinValue;
#ifdef DEBUG
  printDate(timeDateBuf);
  Serial.print("currentMin: ");
  Serial.println(currentMin);
  Serial.print("start: ");
  Serial.println(startMin);
#endif
  // check if relay should be turned on
  if(currentMin >= startMin && currentMin < endMin) {
    state = schRun;
    goToRunState();
  }
}

// check if schedule should end
void checkEndScheduleFunction() {
  readDate(timeDateBuf);
  int currentMin = timeDateBuf[2] * 100 + timeDateBuf[1]; //hours * 60 + minutes
  int startMin = startHourValue + startMinValue;
  int endMin = endHourValue + endMinValue;
#ifdef DEBUG
  printDate(timeDateBuf);
  Serial.print("currentMin: ");
  Serial.println(currentMin);
  Serial.print("end: ");
  Serial.println(endMin);
#endif
  // check if relay should be turned off
  if(currentMin >= endMin)
    goToIdleState();
}

// save start time to EEPROM
bool saveStartTime(int startTime) {
  return EEPROM.writeInt(0, startTime);
}

// save end time to EEPROM
bool saveEndTime(int endTime) {
  return EEPROM.writeInt(1, endTime);
}

// recover start and end times from EEPROM
void getStartEndTime(int *startTime, int *endTime) {
  *startTime = EEPROM.readInt(0);
  *endTime = EEPROM.readInt(1);
#ifdef DEBUG
  Serial.print("recovered start time: ");
  Serial.print(*startTime);
  Serial.print("recovered end time: ");
  Serial.print(*endTime);
#endif
}
