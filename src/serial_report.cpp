// serial_report.cpp — USB serial data reporting

#include "serial_report.h"
#include "globals.h"

// These two functions have the same name but different parameter 
// types. C++ calls this "function overloading" — the compiler 
// picks the right version based on what you pass in.
void debug_print(const char* label, int value)
{
    Serial.print(label);
    Serial.print(": ");
    Serial.println(value);
}

void debug_print(const char* label, float value)
{
    Serial.print(label);
    Serial.print(": ");
    Serial.println(value, 2);  // 2 decimal places
}

void report_serial()
{
    if (DEBUG_MODE) return; // Only output when not in debug mode
    // Format: motor:NNN,pres:NNN,avg:NNN,delta:NNN,limit:NNN,cooldown:NNN
    Serial.print("motor:");  Serial.print(motorSpeed);
    Serial.print(",");
    Serial.print("pres:");   Serial.print(pressure);
    Serial.print(",");
    Serial.print("avg:");    Serial.print(averagePressure);
    Serial.print(",");
    Serial.print("delta:");  Serial.print(pressure - averagePressure);
    Serial.print(",");
    Serial.print("limit:");  Serial.print(pressureLimit);
    Serial.print(",");
    Serial.print("cooldown:"); Serial.println(minimumcooldown);
}