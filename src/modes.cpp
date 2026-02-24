// modes.cpp — Operating mode implementations

#include "modes.h"
#include "config.h"
#include "globals.h"
#include "leds.h"
#include "motor.h"
#include "buttons.h"    // for encLimitRead
#include "pressure.h"

// --- Manual mode (Red) ---
// User directly controls motor speed with the knob.
// LED ring shows pressure level in the background as a bar graph.
void run_manual()
{
    int knob = encLimitRead(0, NUM_LEDS - 1);
    motorSpeed = map(knob, 0, NUM_LEDS - 1, 0., (float)MOT_MAX);
    motor_write((int)motorSpeed);

    // Draw pressure-above-average as a green->yellow->red bar
    int presDraw = map(
        constrain(pressure - averagePressure, 0, pressureLimit),
        0, pressureLimit, 0, NUM_LEDS * 3
    );
    draw_bars_3(presDraw, CRGB::Green, CRGB::Yellow, CRGB::Red);
    draw_cursor(knob, CRGB::Red);
}

// --- Automatic edging mode (Blue) ---
// Motor ramps up linearly. If pressure spike detected (approaching 
// orgasm), motor cuts immediately and waits through a cooldown 
// before ramping again. Knob adjusts detection sensitivity.
void run_auto()
{
    // 'static' here means these persist between calls — like 
    // instance variables on a Python class. They keep their value 
    // from one 60Hz tick to the next.
    static float motorIncrement = 0.0;
    static float LimitSpeed = 0.0;

    // Calculate how much to increment motor speed each tick to 
    // reach maxMotorSpeed over rampUp seconds.
    // e.g. 255 / (60Hz * 10s) = ~0.425 per tick
    motorIncrement = ((float)maxMotorSpeed / ((float)FREQUENCY * (float)rampUp));

    // Knob controls sensitivity. Higher knob = lower pressureLimit = more sensitive.
    // The 3-revolution range (0 to 71) gives fine-grained control.
    int knob = encLimitRead(0, (3 * NUM_LEDS) - 1);
    sensitivity = knob * 4;
    pressureLimit = map(knob, 0, 3 * (NUM_LEDS - 1), MAX_PRESSURE_LIMIT, 1);

    // --- EDGE DETECTED: pressure spike exceeds threshold ---
    if (pressure - averagePressure > pressureLimit)
    {
        motor_write(0);  // Kill motor immediately

        // Each userMode handles cooldown differently by setting motorSpeed 
        // to a negative value. Since the motor only turns on when motorSpeed 
        // exceeds MOT_MIN (~20), a negative motorSpeed means the ramp-up 
        // has to "climb back from below zero" before the motor restarts.
        // The more negative, the longer the effective cooldown.
        //
        // This is a clever trick: rather than using a separate timer, the 
        // cooldown duration emerges naturally from how deep negative we go 
        // and how fast motorIncrement brings us back up.
        switch (userMode)
        {
            case 1:  // Half ramp-up time as cooldown
                motorSpeed = -.5 * (float)rampUp * ((float)FREQUENCY * motorIncrement);
                break;

            case 2:  // Double ramp-up time as cooldown
                motorSpeed = -2 * (float)rampUp * ((float)FREQUENCY * motorIncrement);
                break;

            case 3:  // Fixed cooldown (in seconds)
                motorSpeed = -1 * (float)cooldown * ((float)FREQUENCY * motorIncrement);
                break;

            case 4:  // Slow creep — cooldown increases each edge
                motorSpeed = -1 * (float)minimumcooldown * ((float)FREQUENCY * motorIncrement);
                if (cooldownFlag == 1)
                {
                    cooldownFlag = 0;
                    if (minimumcooldown <= maxCooldown)
                        minimumcooldown += cooldownStep;
                }
                break;

            case 5:  // More sensitive — lowers threshold each edge
                motorSpeed = -1 * (float)cooldown * ((float)FREQUENCY * motorIncrement);
                if (cooldownFlag == 1)
                {
                    cooldownFlag = 0;
                    if (cooldown <= maxCooldown)
                        pressureLimit = max(pressureLimit - pressureStep, 10);
                }
                break;

            case 6:  // Clench-responsive — motor inversely tracks pressure
                motorSpeed = -(.5 * (float)rampUp * ((float)FREQUENCY * motorIncrement) + 10);
                break;
        }
    }
    // --- NO EDGE: ramp up toward target speed ---
    else
    {
        if (userMode == 6)
        {
            // Mode 6 continuously adjusts the speed ceiling based on 
            // how close pressure is to the threshold. As you clench 
            // harder (pressure rises), the motor slows down — creating 
            // a feedback loop where the user's arousal response directly 
            // modulates stimulation intensity.
            //
            // The 1.15 multiplier makes the ceiling drop slightly faster 
            // than a pure linear relationship, adding a safety margin.
            LimitSpeed = (float)maxMotorSpeed 
                - (1.15 * (pressure - averagePressure) / pressureLimit * (float)maxMotorSpeed);
            LimitSpeed = constrain(LimitSpeed, 0, (float)maxMotorSpeed);

            if (motorSpeed < LimitSpeed)
                motorSpeed += motorIncrement;           // Ramp up toward ceiling
            else if (motorSpeed > LimitSpeed)
                motorSpeed -= 3.5 * motorIncrement;    // Back off quickly if ceiling dropped
        }
        else if (motorSpeed < (float)maxMotorSpeed)
        {
            motorSpeed += motorIncrement;  // Standard linear ramp for modes 1-5
        }

        // Apply motor output
        if (motorSpeed > MOT_MIN)
        {
            motor_write((int)motorSpeed);
        }
        else
        {
            motor_write(0);
            cooldownFlag = 1;  // Signal that cooldown period has elapsed
        }

        // Draw pressure bar and sensitivity cursor
        int presDraw = map(
            constrain(pressure - averagePressure, 0, pressureLimit),
            0, pressureLimit, 0, NUM_LEDS * 3
        );
        draw_bars_3(presDraw, CRGB::Green, CRGB::Yellow, CRGB::Red);
        draw_cursor_3(knob, CRGB(50, 50, 200), CRGB::Blue, CRGB::Purple);
    }
}

// --- Max speed setting (Green) ---
void run_opt_speed()
{
    int knob = encLimitRead(0, NUM_LEDS - 1);
    motorSpeed = map(knob, 0, NUM_LEDS - 1, 0., (float)MOT_MAX);
    motor_write((int)motorSpeed);
    maxMotorSpeed = motorSpeed;

    // Animated green bar to visualize the ramp-up rate
    static int visRamp = 0;
    if (visRamp <= FREQUENCY * NUM_LEDS - 1) visRamp += 16;
    else visRamp = 0;
    draw_bars_3(
        map(visRamp, 0, (NUM_LEDS - 1) * FREQUENCY, 0, knob),
        CRGB::Green, CRGB::Green, CRGB::Green
    );
}

// --- Ramp speed setting (not yet implemented) ---
void run_opt_rampspd()
{
    Serial.println("rampSpeed");
}

// --- Beep/brightness setting (not yet implemented) ---
void run_opt_beep()
{
    Serial.println("Brightness Settings");
}

// --- Pressure debug display (White) ---
// Shows raw ADC reading as a single white cursor on the ring.
// Useful for adjusting the analog gain trimpot.
void run_opt_pres()
{
    int p = map(analogRead(BUTTPIN), 0, ADC_MAX, 0, NUM_LEDS - 1);
    draw_cursor(p, CRGB::White);
}

// --- User mode selection ---
void run_opt_userModeChange()
{
    int position = encLimitRead(1, userModeTotal);
    draw_cursor(position, CRGB::Red);
    userMode = position;
}