/*
###################################################################
Title:       Arduino MPPT Solar controller
Purpose:     Use a 126 x 64 I2C OLED screen and an Arduino Leonardo
Created by:  MacTester57
Note:        Please reuse, repurpose, and redistribute this code.
Note:        This code uses the Adafruit GFX library
###################################################################
*/
#include <SPI.h>
#include <Wire.h>
#include <SimpleTimer.h>
#include <Adafruit_GFX.h>  // display general library
#include <Adafruit_SSD1306.h> // display hardware-specific library
#include "RunningMedian.h"
#include "setPrescaler.h"; // Custom .h file allows the adjustment of the PWM frequencies on the AtMega 32u4


//
// ========================================
// PIN ASSIGNMENTS & GLOBAL VARIABLES
// ========================================
//

// display pins
#define OLED_RESET 11
Adafruit_SSD1306 display(99);

// output pins
#define pwmPin 10
#define chargePump 6

// buttons & debouncing parameters
#define menuButton 9
#define selButton 8  // NOTE! do not use interrupt pins for buttons!
#define plusButton 4
#define minusButton 5

#define DEBOUNCE 5  // button debouncer, how many ms to debounce, 5+ ms is usually plenty
byte buttons[] = {menuButton, selButton, plusButton, minusButton};
#define NUMBUTTONS sizeof(buttons)
boolean pressed[NUMBUTTONS], justpressed[NUMBUTTONS], justreleased[NUMBUTTONS];

// analog inputs
#define panelAmpSensor A0
#define panelVoltSensor A1
#define batteryAmpSensor A2
#define batteryVoltSensor A3
#define potentiometer A4 // A11 = pin 12!

// current and voltage sensors
float panelAmps = 0.0;
float panelVolts = 0.0;
float panelWatts = 0.0;
float batteryAmps = 0.0;
float batteryVolts = 0.0;
float batteryWatts = 0.0;

float efficiency = 0.0;

// RunningMedian classes & variables
RunningMedian panelAmpsMedian(5); // 5    -> you must MANUALLY set median max size = 5 in library!
RunningMedian panelVoltsMedian(5); // 5
RunningMedian batteryAmpsMedian(5); // 5
RunningMedian batteryVoltsMedian(5); // 5

float panelAmpsRaw = 0.0;
float panelVoltsRaw = 0.0;
float batteryAmpsRaw = 0.0;
float batteryVoltsRaw = 0.0;

// current sensor calibration
short panelAmpCal;
short batteryAmpCal;

// PWM & MPPT variables
#define batteryProtection 1
#define MPPT 2
#define manualMode 3

short potValue;
short mpptValue = 220;
short pwmDuty;
short pwmPercentage;
boolean trackingDirection = true; // true = upwards
short operationMode = MPPT;
short trackingIncrement = 5;  // 5 OK
float lastBatteryWatts = 0.0;
float batteryWattsProtection = 0.0;
float lastPanelWatts = 0.0;
float trackingWattsHysteresis = 0.1;  // 0.1 OK
float batteryVoltsMin = 11.8;
float batteryVoltsCharge = 13.8; // lead acid gel @ 20°C: 13.8 longtime, 14.4 shorttime
float batteryVoltsMax = 14.0;
float batteryVoltsAbsMax = 14.4;

// menu
byte menu = 1;          // the current menu item

// simple timer
SimpleTimer timer;

//
// ========================================
// MAIN ARDUINO SETUP (1x during startup)
// ========================================
//

void setup() {

  // PWM frequency
  setPWMPrescaler(pwmPin, 1 );  // 1 = 31.5kHz
  setPWMPrescaler(chargePump, 1); // 8 = 4kHz

  // setup ADC parameters
  setupADC();

  // input setup
  pinMode(menuButton, INPUT_PULLUP);
  pinMode(selButton, INPUT_PULLUP);
  pinMode(plusButton, INPUT_PULLUP);
  pinMode(minusButton, INPUT_PULLUP);

  // output setup
  pinMode(pwmPin, OUTPUT);
  pinMode(chargePump, OUTPUT);

  // start charge pump
  analogWrite(chargePump, 127);

  // current sensor calibration
  analogWrite(pwmPin, 255);  // 255 = buck converter off for calibration!
  delay(300);
  panelAmpCal = analogRead (panelAmpSensor);
  batteryAmpCal = analogRead (batteryAmpSensor);
  panelAmpCal = 510;  // 509 override!
  batteryAmpCal = 506;  // 505 override!
  delay(100);

  // serial port setup
  Serial.begin(9600);

  // averaging setup
  batteryVoltsMedian.clear();

  // display setup
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64 OLED from Ebay)
  display.clearDisplay();

  // show splash screen
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(43, 15);
  display.print("MPPT");
  display.setTextSize(1);
  display.setCursor(25, 35);
  display.print("Solar Charge");
  display.setCursor(18, 45);
  display.print("Controller v0.9");
  display.display();
  delay(3000);

  // draw display every 200ms
  timer.setInterval(200, drawDisplay);

  // read buttons every 15ms
  timer.setInterval(15, readButtonFunction);

  // read analog sensors every 5ms
  timer.setInterval(5, readSensors); // 5?

  // call PWM controller every 200ms
  timer.setInterval(200, pwmController); // 200 OK
}

//
// ========================================
// MAIN LOOP
// ========================================
//

void loop() {

  // Loop the timer
  timer.run();

  processButtons();

  //readSensors();
}

//
// ========================================
// READ BUTTONS
// ========================================
//

void readButtonFunction() {

  byte index;
  static boolean previousstate[NUMBUTTONS];
  static boolean currentstate[NUMBUTTONS];
  static long lasttime;

  if (millis() < lasttime) { // we wrapped around, lets just try again
    lasttime = millis();
  }

  if ((lasttime + DEBOUNCE) >= millis()) { // not enough time has passed to debounce
    return;
  }

  // ok we have waited DEBOUNCE milliseconds, lets reset the timer
  lasttime = millis();

  for (index = 0; index < NUMBUTTONS; index++) {

    currentstate[index] = digitalRead(buttons[index]);   // read the buttons

    /*Serial.print(index, DEC);

      Serial.print(": cstate=");
      Serial.print(currentstate[index], DEC);

      Serial.print(", pstate=");
      Serial.print(previousstate[index], DEC);

      Serial.print(", press=");*/

    if (currentstate[index] == previousstate[index]) {  // if the current state equals to the previous state (doesn't bounce anymore)
      if ((pressed[index] == false) && (currentstate[index] == false)) {
        // just pressed
        justpressed[index] = true;
      }
      else if ((pressed[index] == true) && (currentstate[index] == true)) {
        // just released
        justreleased[index] = true;
      }
      pressed[index] = !currentstate[index];  // remember, digital HIGH means NOT pressed
    }

    //Serial.println(pressed[index], DEC);
    previousstate[index] = currentstate[index];   // keep a running tally of the buttons
  }
}

void processButtons() {

  // select (button no. 1)
  if (justpressed[1]) {
    menu++;
    justpressed[1] = false;
  }
  
  if (menu > 3) menu = 1; // define the max. number of menus

  // + (button no. 2)
  if (justpressed[2]) {
    if (menu == 1) {
      //   triggerLevel += (adcMax / 10);
    }
    else if (menu == 2) {
      // timeBase += 10;
    }
    justpressed[2] = false;
  }

  // - (button no. 3)
  if (justpressed[3]) {
    if (menu == 1) {
      //  triggerLevel -= (adcMax / 10);
    }
    else if (menu == 2) {
      // timeBase -= 10;
    }
    justpressed[3] = false;
  }

  // + and - buttons pressed
  if (pressed[2] && pressed[3]) {
    if (menu == 1) {
      //    triggerLevel = arrayAverage;  // auto-adjust the trigger level
    }
  }

  // timeBase = constrain (timeBase, 0, 110);

}

//
// ========================================
// READ SENSORS
// ========================================
//

void readSensors() {

  // Current sensors
  panelAmpsRaw = (panelAmpCal - analogRead(panelAmpSensor)) * 27.03 / 1023;
  batteryAmpsRaw = (batteryAmpCal - analogRead(batteryAmpSensor)) * 27.03 / 1023;

  // Voltage sensors
  panelVoltsRaw = analogRead (panelVoltSensor) * 29.1 / 1023;  // theroretically 29.26, depends on resistor values!
  batteryVoltsRaw = analogRead (batteryVoltSensor) * 29.6 / 1023;

  // smoothing
  panelAmpsMedian.add(panelAmpsRaw);
  panelAmps = panelAmpsMedian.getMedian();
  //panelAmps = panelAmpsRaw;

  panelVoltsMedian.add(panelVoltsRaw);
  panelVolts = panelVoltsMedian.getMedian();
  //panelVolts = panelVoltsRaw;

  batteryAmpsMedian.add(batteryAmpsRaw);
  batteryAmps = batteryAmpsMedian.getMedian();

  batteryVoltsMedian.add(batteryVoltsRaw);
  batteryVolts = batteryVoltsMedian.getMedian();

  // efficiency
  efficiency = 100.0 * (batteryAmps * batteryVolts) / (panelAmps * panelVolts);
  if (efficiency > 100.0) efficiency = 100.0;

  // calculate tracking increment
  trackingIncrement = (float)((20 * batteryVoltsCharge) - (20 * batteryVolts)); // 20 *
  trackingIncrement = constrain(trackingIncrement, 1, 5);
  //Serial.println(trackingIncrement);
}

//
// ========================================
// PWM MPPT CONTROLLER
// ========================================
//

void pwmController() {

  // this function is called every 200ms

  // ---- read potentiometer ----

  potValue = analogRead(potentiometer);

  // ---- mode detection ----

  if (potValue > 20) {
    pwmDuty = map(potValue, 0, 1023, 0, 255);  // manual mode with potentiometer
    operationMode = manualMode;
    mpptValue = 220;
  }
  else if (batteryVolts > batteryVoltsCharge && operationMode == MPPT) {
    operationMode = batteryProtection; // batteryProtection mode
    batteryWattsProtection = batteryWatts; // save last battery watts
  }
  else if (batteryWatts < (batteryWattsProtection - 0.5) && operationMode == batteryProtection) {
    operationMode = MPPT; // go from batteryProtection mode back to MPPT mode, if battery watts decreased > 0.5A
  }
  else if (batteryVolts < (batteryVoltsCharge - 0.05) && operationMode == batteryProtection) {
    operationMode = MPPT; // go from batteryProtection mode back to MPPT mode, if battery volts decreased > 0.05V
  }
  else if (operationMode != batteryProtection) {
    operationMode = MPPT; // initialise MPPT mode
  }

  // ---- calculate power for MPPT algorythm ----

  // calculate battery watts
  batteryWatts = batteryAmps * batteryVolts;

  // calculate panel watts
  panelWatts = panelAmps * panelVolts;


  // ---- MPPT mode ----

  if (operationMode == MPPT) {
    // change tracking direction if wattage is less than before
    if ((batteryWatts + trackingWattsHysteresis) < lastBatteryWatts) {
      trackingDirection = !trackingDirection;
    }

    // set tracking direction upwards, before PWM is zero
    if (mpptValue <= 5) {
      trackingDirection = true;
    }

    // set tracking direction upwards, before panel voltage is too high
    if (panelVolts > 18.0) {
      trackingDirection = true;
    }

    // solar panel minimum voltage
    if (panelVolts <= batteryVolts && (mpptValue - trackingIncrement) > 0) {
      trackingDirection = false;
    }

    // increase PWM duty cycle
    if (trackingDirection == true) {
      if (batteryVolts < batteryVoltsCharge && (mpptValue + trackingIncrement) < 255) {
        mpptValue = mpptValue + trackingIncrement;
      }
    }
    else
    { // decrease PWM duty cycle
      if (mpptValue - trackingIncrement > 0) {
        mpptValue = mpptValue - trackingIncrement;
      }
    }

    pwmDuty = mpptValue;
  }

  // ---- battery protection mode ----

  if (operationMode == batteryProtection) {
    if (batteryVolts > (batteryVoltsCharge + 0.1)) {
      mpptValue = mpptValue - trackingIncrement; // decrease MPPT value
    } else {
      mpptValue = mpptValue; // freeze MPPT value
    }

    if (batteryVolts >= batteryVoltsMax) {  // maximum battery voltage limit
      mpptValue = mpptValue - 10; // decrease MPPT value fast
    }

    pwmDuty = mpptValue;
  }


  // range limits (only for safety)
  if (mpptValue > 255) mpptValue = 255;
  if (mpptValue < 0) mpptValue = 0;



  // ---- adjust PWM duty cycle (for all operation modes) ----

  if (batteryVolts >= batteryVoltsAbsMax) {  // absolute maximum battery voltage limit for every operation mode!
    pwmDuty = 0;
  }

  lastBatteryWatts = batteryWatts;
  pwmPercentage = map(pwmDuty, 0, 255, 0, 100); //percentage for display
  pwmDuty = map(pwmDuty, 0, 255, 255, 0);  // invert pwmDuty for MOSFET driver!
  analogWrite(pwmPin, pwmDuty);
}

//
// ========================================
// DRAW display
// ========================================
//

void drawDisplay() {

  Serial.println(menu);

  // clear old display content
  display.clearDisplay();


  switch (menu) {
    case 1: // main page

      // readings
      display.setTextSize(1);
      display.setTextColor(WHITE);

      display.setCursor(3, 11); // show panel Voltage on display
      display.print(panelVolts, 1);
      display.print("V");

      display.setCursor(90, 11);  // show battery Voltage on display
      display.print(batteryVolts, 1);
      display.print("V");

      if (batteryAmps >= 0.07 || batteryVolts >= 13.2 || operationMode == manualMode) {
        display.setCursor(3, 1); // show panel Amps on display
        display.print(panelAmps);
        display.print("A");

        display.setCursor(3, 21); // show panel Watts on display
        display.print(panelWatts, 1);
        display.print("W");

        display.setCursor(90, 1); // show battery Amps on display
        display.print(batteryAmps);
        display.print("A");

        display.setCursor(90, 21);  // show battery Watts on display
        display.print(batteryWatts, 1);
        display.print("W");

        // show battery Watts bargraph on display
        display.drawRect(0, 50, 128, 14, WHITE); // show bargraph frame
        display.drawRect(3, 52, (batteryWatts * 10), 10, WHITE); // draw new watts bargraph

        display.setCursor(44, 11);  // show tracking direction on display
        if (trackingDirection == true) {
          display.print("Tr. +");
        } else {
          display.print("Tr. -");
        }

        display.setCursor(44, 21);  // show PWM on display
        display.print("PWM");
        display.print(pwmPercentage);
        display.print("%");

        display.setCursor(44, 1);  // show efficiency on display
        display.print("E.");
        display.print(efficiency, 0);
        display.print("%");

        display.drawFastVLine(41, 0, 31, WHITE);  // show divider lines
        display.drawFastVLine(87, 0, 31, WHITE);
        display.drawFastHLine(0, 31, 128, WHITE);
      }

      // display operation mode
      //display.drawRoundRect( 0, 34, 128, 13, 5, WHITE);
      // display.setTextSize(2);
      display.setTextColor(WHITE);
      if (operationMode == manualMode) {
        display.setCursor(30, 37);
        display.print("Manual Mode");
      }
      if (operationMode == MPPT) {
        display.setCursor(40, 37);
        display.print("MPPT Mode");
      }
      if (operationMode == batteryProtection) {
        display.setCursor(10, 37);
        display.print("Battery Prot. Mode");
      }
      break;

    case 2: // main page
      display.print("2");
      break;

    case 3: // main page
      display.print("3");
      break;
  }

  // menus
  /*display.fillRect(0, 119, 44, 9, WHITE);  // refresh menu background
  display.setTextColor(BLACK);
  display.setCursor(1, 120);  // show menu on display
  switch (menu) {
    case 1: // trigger menu
      display.print("Tr. Off"); // trigger off
      break;

    case 2: // time base
      display.print("T. Base"); // time base
      break;

    case 3: // zoom menu x
      display.print("Zoom x");
      //  display.print(displayScaleX);
      break;

    case 4: // zoom menu y
      display.print("Zoom y");
      break;

    case 5: // offset menu y
      display.print("Offs y");
      break;

    case 6: // test signal prescaler
      display.print("TPr ");
      // display.print(pwmPrescaler);
      break;

    case 7: // test signal duty cycle
      display.print("TDu ");
      //  display.print(pwmDutyBase);
      break;
  }*/
  display.display();
}
