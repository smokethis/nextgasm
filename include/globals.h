// globals.h — Central registry of shared mutable state

#pragma once

#include <Arduino.h>
#include <Encoder.h>
#include "config.h"

// --- Hardware objects ---
extern Encoder myEnc;

// --- Pressure state ---
extern int pressure;
extern int averagePressure;

// --- Motor state ---
extern float motorSpeed;
extern int maxMotorSpeed;

// --- Edging algorithm state ---
extern int sensitivity;
extern int pressureLimit;
extern int rampUp;
extern int userMode;
extern int userModeTotal;
extern int pressureStep;

// --- Cooldown state ---
extern int cooldown;
extern int cooldownStep;
extern int cooldownFlag;
extern int maxCooldown;
extern int minimumcooldown;