// motor.cpp — Motor output and beep implementation

#include "motor.h"
#include "config.h"
#include "globals.h"

void motor_init()
{
    // Set PWM frequency to 31kHz — above human hearing so the 
    // motor doesn't whine. 
    //
    // On AVR Arduinos this required directly poking timer prescaler 
    // registers (TCCR1B etc). The Teensy framework provides a clean 
    // API for it instead. Much nicer!
    //
    // analogWriteFrequency() is Teensy-specific — it's not part of 
    // standard Arduino. It sets the PWM frequency for the given pin.
    // The second argument is in Hz.
    analogWriteFrequency(MOTPIN, 31372);

    pinMode(MOTPIN, OUTPUT);
    digitalWrite(MOTPIN, LOW);
}

void beep_motor(int f1, int f2, int f3)
{
    analogWrite(MOTPIN, 0);
    tone(MOTPIN, f1);
    delay(250);
    tone(MOTPIN, f2);
    delay(250);
    tone(MOTPIN, f3);
    delay(250);
    noTone(MOTPIN);
    // Restore whatever speed the motor was at before the beep
    analogWrite(MOTPIN, (int)motorSpeed);
}

void motor_write(int speed)
{
    analogWrite(MOTPIN, speed);
}