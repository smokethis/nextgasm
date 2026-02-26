// state.cpp — State machine implementation

#include "state.h"
#include "config.h"
#include "globals.h"
#include "modes.h"
#include "motor.h"
#include "leds.h"

#include <EEPROM.h>
#include "FastLED.h"
#include <list>

const uint8_t modeList[] = {STANDBY, MANUAL, AUTO, OPT_SPEED, OPT_PRES, OPT_USER_MODE};
const int modeCount = sizeof(modeList) / sizeof(modeList[0]);  // = 6

void run_state_machine(uint8_t state) {
    switch (state) {
        case MANUAL:      run_manual();           break;
        case AUTO:        run_auto();             break;
        case OPT_SPEED:   run_opt_speed();        break;
        case OPT_RAMPSPD: run_opt_rampspd();      break;
        case OPT_BEEP:    run_opt_beep();         break;
        case OPT_PRES:    run_opt_pres();         break;
        case OPT_USER_MODE: run_opt_userModeChange(); break;
        case STANDBY:     run_standby();          break;
        default:          run_standby();          break;
    }
}

uint8_t get_next_state(uint8_t state) {
    // Find current mode's index (like Python's list.index(value))
    int currentIndex = 0;
    for (int i = 0; i < modeCount; i++) {
        if (modeList[i] == state) {
            currentIndex = i;
            break;
        }
    }

    // Move to next
    int nextIndex = (currentIndex + 1) % modeCount;
    state = modeList[nextIndex];

    return state;
}

uint8_t get_previous_state(uint8_t state) {
    // Find current mode's index (like Python's list.index(value))
    int currentIndex = 0;
    for (int i = 0; i < modeCount; i++) {
        if (modeList[i] == state) {
            currentIndex = i;
            break;
        }
    }

    // Move to next
    int nextIndex = (currentIndex - 1) % modeCount;
    state = modeList[nextIndex];

    return state;
}

// uint8_t set_state(uint8_t state) {

//     if (btnState == BTN_NONE) {
//         return state;
//     }

//     // --- Very long press: power off ---
//     if (btnState == BTN_V_LONG) {
//         Serial.println("power off");
//         fill_gradient_RGB(leds, 0, CRGB::Black, NUM_LEDS - 1, CRGB::Black);
//         FastLED.show();
//         motor_write(0);
//         beep_motor(2093, 1396, 1047);
//         motor_write(0);
//         // Block until button is released, then wait for next press
//         while (!digitalRead(ENC_SW)) delay(1);
//         beep_motor(1047, 1396, 2093);
//         return MANUAL;
//     }

//     // --- Short press: cycle between primary modes ---
//     if (btnState == BTN_SHORT) {
//         switch (state) {
//             case MANUAL:
//                 myEnc.write(sensitivity);
//                 motorSpeed = 0;
//                 return AUTO;

//             case AUTO:
//                 myEnc.write(0);
//                 motorSpeed = 0;
//                 EEPROM.update(SENSITIVITY_ADDR, sensitivity);
//                 return MANUAL;

//             case OPT_SPEED:
//                 myEnc.write(0);
//                 EEPROM.update(MAX_SPEED_ADDR, maxMotorSpeed);
//                 motorSpeed = 0;
//                 motor_write(0);
//                 return OPT_PRES;

//             case OPT_BEEP:
//                 myEnc.write(0);
//                 return OPT_PRES;

//             case OPT_PRES:
//                 myEnc.write(map(maxMotorSpeed, 0, 255, 0, 4 * NUM_LEDS));
//                 return OPT_USER_MODE;

//             case OPT_USER_MODE:
//                 myEnc.write(map(userMode, 1, userModeTotal, 0, 4 * NUM_LEDS));
//                 return OPT_SPEED;
//         }
//     }

//     // --- Long press: enter/exit settings ---
//     if (btnState == BTN_LONG) {
//         switch (state) {
//             case MANUAL:
//             case AUTO:
//                 myEnc.write(map(maxMotorSpeed, 0, 255, 0, 4 * NUM_LEDS));
//                 return OPT_SPEED;

//             case OPT_SPEED:
//             case OPT_RAMPSPD:
//             case OPT_BEEP:
//             case OPT_PRES:
//                 myEnc.write(0);
//                 return MANUAL;

//             case OPT_USER_MODE:
//                 myEnc.write(map(userMode, 1, userModeTotal, 0, 4 * NUM_LEDS));
//                 return AUTO;
//         }
//     }

//     return MANUAL;
// }