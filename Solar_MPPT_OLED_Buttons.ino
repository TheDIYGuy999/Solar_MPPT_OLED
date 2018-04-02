/*
  ###################################################################
  Title:       Arduino MPPT solar charge controller
  Purpose:     Use a 126 x 64 I2C OLED screen and an Arduino Leonardo
  Created by:  TheDIYGuy999
  Note:        Please reuse, repurpose, and redistribute this code.
  Note:        This code uses the Adafruit GFX library
  ###################################################################
*/
#include <SPI.h>
#include <Wire.h>
#include <SimpleTimer.h>
//#include <EEPROMex.h>
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
#define OLED_RESET 11 // not connected, because the used display does not have a reset pin!
Adafruit_SSD1306 display(OLED_RESET);

// ********** CAUTION! manually uncomment the following line in Adafruit_SSD1306.h !! ************
// #define SSD1306_128_64

// output pins
#define pwmPin 10
#define chargePump 6
#define loadPin 12

// buttons & debouncing variables
#define menuButton 9
#define selButton 8  // NOTE! do not use interrupt pins for buttons!
#define plusButton 4
#define minusButton 5

byte menuButtonState;
byte selButtonState;
byte plusButtonState;
byte minusButtonState;

// macro for detection of raising edge and debouncing
/*the state argument (which must be a variable) records the current and the last 3 reads
  by shifting one bit to the left at each read and bitwise anding with 15 (=0b1111).
  If the value is 7(=0b0111) we have one raising edge followed by 3 consecutive 1's.
  That would qualify as a debounced raising edge*/
#define DRE(signal, state) (state=(state<<1)|(signal&1)&15)==7

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
RunningMedian panelAmpsMedian(5); // 5    -> you must MANUALLY set median max size = 5 in your "RunningMedian.h" library!
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
float batteryVoltsMin = 11.7; // 11.7
float batteryVoltsCharge = 13.8; // lead acid gel @ 20°C: 13.8 longterm, 14.4 shortterm
float batteryVoltsMax = 14.4;
float batteryCurrentMax = 2.0;

boolean loadState = true;

// menu
byte page = 1;          // the current display page
byte activeItem = 1;    // the selected menu item

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
  pinMode(loadPin, OUTPUT);

  // start charge pump
  analogWrite(chargePump, 127);

  // current sensor calibration
  analogWrite(pwmPin, 255);  // 255 = buck converter off for calibration!
  delay(300);
  panelAmpCal = analogRead (panelAmpSensor);
  batteryAmpCal = analogRead (batteryAmpSensor);
  panelAmpCal = 510;  // 509 override!
  batteryAmpCal = 505;  // 505 override!
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

  // read buttons every 5ms
  timer.setInterval(5, readButtonFunction);

  // read analog sensors every 5ms
  timer.setInterval(40, readSensors); // 5?

  // call PWM controller every 200ms
  timer.setInterval(200, pwmController); // 200 OK

  // switch load every 200ms
  timer.setInterval(200, loadSwitch);

  // show page 1
  page = 2;
}

//
// ========================================
// MAIN LOOP
// ========================================
//

void loop() {

  // Loop the timer
  timer.run();

}

//
// ========================================
// READ BUTTONS
// ========================================
//

void readButtonFunction() {

  // menu button
  if (DRE(digitalRead(menuButton), menuButtonState)) {
    page++;
  }
  // Select button
  if (DRE(digitalRead(selButton), selButtonState)) {
    activeItem++;
  }
  // + button
  if (DRE(digitalRead(plusButton), plusButtonState)) {
    //page++;
  }
  // - button
  if (DRE(digitalRead(minusButton), minusButtonState)) {
    loadState = !loadState;
  }
  if (page > 2) page = 1;
  if (activeItem > 4) activeItem = 1;
}


//
// ========================================
// READ SENSORS
// ========================================
//

void readSensors() {

  // Current sensors
  panelAmpsRaw = (panelAmpCal - analogRead(panelAmpSensor)) * 27.03 / 1023;
  if (panelAmpsRaw < 0.0) panelAmpsRaw = 0.0;

  batteryAmpsRaw = (batteryAmpCal - analogRead(batteryAmpSensor)) * 27.03 / 1023;
  if (batteryAmpsRaw < 0.0) batteryAmpsRaw = 0.0;

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
  //trackingIncrement = (float)((batteryVoltsCharge - batteryVolts) * 5.0);
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
    if (panelVolts > 17.0) { // 18.0
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
      mpptValue = mpptValue - trackingIncrement; // decrease MPPT value (trackingIncrement = 5)
    } else {
      mpptValue = mpptValue; // freeze MPPT value
    }

    if (batteryVolts >= (batteryVoltsCharge + batteryVoltsMax / 2)) {  // decrease MPPT value fast, if voltage >14.1V
      mpptValue = mpptValue - 10; // decrease MPPT value fast
    }

    if (batteryVolts >= batteryVoltsMax) {  // decrease MPPT value very fast, if voltage >14.4V
      mpptValue = mpptValue - 20; // decrease MPPT value very fast
    }

    pwmDuty = mpptValue;
  }


  // range limits (only for safety)
  if (mpptValue > 255) mpptValue = 255;
  if (mpptValue < 0) mpptValue = 0;



  // ---- adjust PWM duty cycle (for all operation modes) ----

  if (batteryVolts >= batteryVoltsMax) {  // maximum battery voltage limit for every operation mode!
    pwmDuty = 0;
  }

  lastBatteryWatts = batteryWatts;
  pwmPercentage = map(pwmDuty, 0, 255, 0, 100); //percentage for display
  pwmDuty = map(pwmDuty, 0, 255, 255, 0);  // invert pwmDuty for MOSFET driver!
  analogWrite(pwmPin, pwmDuty);
}

//
// ========================================
// DRAW DISPLAY
// ========================================
//

void drawDisplay() {

  //Serial.println(page);

  // clear old display content
  display.clearDisplay();


  switch (page) {
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

      if (batteryAmps >= 0.07 || batteryVolts >= 12.6 || operationMode == manualMode) {
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

    case 2: // menu page
      display.setCursor(50, 1);
      display.print("Menu");

      menuItem(0, 1, "Batt. Min.:", batteryVoltsMin, " V");
      menuItem(0, 2, "Batt. Float.:", batteryVoltsCharge, " V");
      menuItem(0, 3, "Batt. Max.:", batteryVoltsMax, " V");
      menuItem(0, 4, "Current Max.:", batteryCurrentMax, " A");

      break;
  }
  display.display();
}

//
// ========================================
// LOAD SWITCH
// ========================================
//

static unsigned long lastProtection = 0;

void loadSwitch() {

  if (operationMode != batteryProtection) lastProtection = millis(); // reset timer, if not in protection mode

  if (millis() - lastProtection > 5000) loadState = true; // switch load on, if more than 5s in protection mode

  if (batteryVolts < batteryVoltsMin) loadState = false; // switch load off, if battery voltage is too low!

  if (loadState) digitalWrite(loadPin, HIGH);
  else digitalWrite(loadPin, LOW);
}

//
// ========================================
// MENU FUNCTION
// ========================================
//

byte menuItem(short x, short item, char text[], float variable, char unit[]) {

  display.setCursor(x, 11 + (10 * item));
  display.print(text);
  display.setCursor(x + 90, 11 + (10 * item));

  if (activeItem == item) {
    display.setTextColor(BLACK, WHITE);
  } else {
    display.setTextColor(WHITE);
  }
  display.print(variable, 1);

  display.setTextColor(WHITE);
  display.print(unit);
}
