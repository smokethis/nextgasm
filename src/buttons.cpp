// buttons.cpp — Encoder and button implementation

#include "buttons.h"
#include "config.h"
#include "globals.h"   // for myEnc
#include "serial_report.h"

void button_init()
{
    pinMode(ENC_SW, INPUT);
    digitalWrite(ENC_SW, HIGH);  // Enable internal pull-up resistor
}

uint8_t check_button()
{
    static bool lastBtn = ENC_SW_DOWN;
    static unsigned long keyDownTime = 0;
    uint8_t btnState = BTN_NONE;
    bool thisBtn = digitalRead(ENC_SW);

    // Detect the moment the button is pressed down
    if (thisBtn == ENC_SW_DOWN && lastBtn == ENC_SW_UP) {
        keyDownTime = millis();
        if (DEBUG_BUTTONS) debug_print("buttonPush:", thisBtn); // Print to serial if enabled
    }

    // Detect the moment the button is released — classify press length
    if (thisBtn == ENC_SW_UP && lastBtn == ENC_SW_DOWN) {
        unsigned long held = millis() - keyDownTime;
        if (held >= V_LONG_PRESS_MS) {
            btnState = BTN_V_LONG;
        } else if (held >= LONG_PRESS_MS) {
            btnState = BTN_LONG;
        } else {
            btnState = BTN_SHORT;
        }
    }

    if (DEBUG_BUTTONS) debug_print("buttonState:", btnState); // Print to serial if enabled
    lastBtn = thisBtn;
    return btnState;
}

int encLimitRead(int minVal, int maxVal)
{
    // Encoder library counts in raw pulses. Most encoders give 
    // 4 pulses per physical detent/click. We divide by 4 and 
    // clamp to the requested range so each click = 1 step.
    if (myEnc.read() > maxVal * 4) myEnc.write(maxVal * 4);
    else if (myEnc.read() < minVal * 4) myEnc.write(minVal * 4);
    return constrain(myEnc.read() / 4, minVal, maxVal);
}