// leds.cpp — LED drawing functions implementation

#include "leds.h"

// This is the actual DEFINITION of the array (allocates memory).
// Other files see the 'extern' declaration in leds.h and know 
// to look here for the real thing. 
// 
// In Python terms: leds.h says "there's a variable called leds", 
// and this line says "here it is, with actual memory allocated."
CRGB leds[NUM_LEDS];

void draw_cursor(int pos, CRGB C1)
{
    pos = constrain(pos, 0, NUM_LEDS - 1);
    leds[pos] = C1;
}

void draw_cursor_3(int pos, CRGB C1, CRGB C2, CRGB C3)
{
    pos = constrain(pos, 0, NUM_LEDS * 3 - 1);
    int colorNum = pos / NUM_LEDS;
    int cursorPos = pos % NUM_LEDS;
    switch(colorNum)
    {
        case 0: leds[cursorPos] = C1; break;
        case 1: leds[cursorPos] = C2; break;
        case 2: leds[cursorPos] = C3; break;
    }
}

void draw_bars_3(int pos, CRGB C1, CRGB C2, CRGB C3)
{
    pos = constrain(pos, 0, NUM_LEDS * 3 - 1);
    int colorNum = pos / NUM_LEDS;
    int barPos = pos % NUM_LEDS;
    switch(colorNum)
    {
        case 0:
            fill_gradient_RGB(leds, 0, C1, barPos, C1);
            break;
        case 1:
            fill_gradient_RGB(leds, 0, C1, barPos, C2);
            break;
        case 2:
            fill_gradient_RGB(leds, 0, C2, barPos, C3);
            break;
    }
}