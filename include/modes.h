// modes.h — Operating mode implementations
//
// Each mode corresponds to a color on the LED ring:
//   Red:   Manual vibrator control
//   Blue:  Automatic edging
//   Green: Max speed setting  
//   White: Pressure debug display
//   Red cursor: User mode selection

#pragma once

#include <Arduino.h>

void run_manual();
void run_auto();
void run_opt_speed();
void run_opt_rampspd();
void run_opt_beep();
void run_opt_pres();
void run_opt_userModeChange();
void run_standby();