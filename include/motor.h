// motor.h — Motor output and beep interface

#pragma once

#include <Arduino.h>

// Play a three-tone beep sequence through the motor.
// Uses tone() so the motor itself acts as a crude speaker.
void beep_motor(int f1, int f2, int f3);

// Safe wrapper for writing motor speed. Keeps the PWM write 
// in one place so it's easy to add logging or safety checks later.
void motor_write(int speed);

// Set up motor pin and PWM prescaler. Called from setup().
void motor_init();