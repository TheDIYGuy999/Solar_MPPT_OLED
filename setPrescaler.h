#include <Arduino.h>
#include <compat/deprecated.h>
//
// -----------------------
// PWM FREQUENCY PRESCALER
// -----------------------
//

/**
 * Arduino Leonardo AtMega 32u4 specific
 * Sets the Prescaler (Divisor) for a given PWM pin. The resulting frequency
 * is equal to the base frequency divided by the given divisor:
 *   - Base frequencies:
 *      o The base frequency for pins 3, and 11 is 64,500 Hz.
 *      o The base frequency for pins 5, 9, and 10 is 31,250 Hz.
 *      o The base frequency for pins 6 and 13 is 125,000 Hz.
 *   - Divisors:
 *      o The divisors available on pins 3, 5, 9, 10 and 11 are: 1, 8, 64, 256, and 1024.
 *      o The divisors available on pins 6 and 13 are all powers of two between 1 and 16384
 *
 * PWM frequencies are tied together in pairs of pins. If one in a
 * pair is changed, the other is also changed to match:
 *   - Pins 3 and 11 are paired on timer0 (8bit)
 *   - Pins 9 and 10 are paired on timer1 (16bit)
 *   - Pins 6 and 13 are paired on timer4 (10bit)
 *   - Pins 5 is exclusivly on timer3 (16bit)
 *
 * Note: Pins 3 and 11 operate on Timer 0 changes on this pins will
 * affect the user of the main time millis() functions
 */
void setPWMPrescaler(uint8_t pin, uint8_t prescale) {
  if (pin == 3 || pin == 11) {  // Pin3 tested, OK, but millis() is affected!!
    byte mode;
    switch (prescale) {
      case 1: mode = 0x01; break;
      case 8: mode = 0x02; break;
      case 64: mode = 0x03; break;
      case 256: mode = 0x04; break;
      case 1024: mode = 0x05; break;
      default: return;
    }
    TCCR0B = TCCR0B & 0b11111000 | mode;  // timer0
  }

  if (pin == 5 || pin == 9 || pin == 10) {
    byte mode;
    switch (prescale) {
      case 1: mode = 0x01; break;
      case 8: mode = 0x02; break;
      case 64: mode = 0x03; break;
      case 256: mode = 0x04; break;
      case 1024: mode = 0x05; break;
      default: return;
    }
    if (pin == 9 || pin == 10) {  // Pin 5, 9 & 10 tested, OK
      TCCR1B = TCCR1B & 0b11111000 | mode;  // timer1
    } else {
      TCCR3B = TCCR3B & 0b11111000 | mode;  // timer3
    }
  }

  if (pin == 6 || pin == 13) {  // Pin 6 tested, OK
    byte mode;
    switch (prescale) {
      case 1: mode = 0x01; break;
      case 2: mode = 0x02; break;
      case 4: mode = 0x03; break;
      case 8: mode = 0x04; break;
      case 16: mode = 0x05; break;
      case 32: mode = 0x06; break;
      case 64: mode = 0x07; break;
      case 128: mode = 0x08; break;
      case 256: mode = 0x09; break;
      case 512: mode = 0x0A; break;
      case 1024: mode = 0x0B; break;
      case 2048: mode = 0x0C; break;
      case 4096: mode = 0x0D; break;
      case 8192: mode = 0x0E; break;
      case 16384: mode = 0x0F; break;
      default: return;
    }
    TCCR4B = TCCR4B & 0b11110000 | mode;  // timer4
  }
}

//
// ---------------------------
// ADC SETUP
// ---------------------------
//

void setupADC() {
// configure the ADC
  DIDR0 = 0b11110011; // disable digital functionality of analog ports (saves energy)
}
