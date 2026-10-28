#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.

enum class ParamID : uint8_t // Define an enumeration class named ParamID with a fixed underlying type of uint8_t to represent unique identifiers for various parameters in the system.
{
    AIR_TEMP, // 0; Identifier for the air temperature parameter.
    HUMIDITY, // 1; Identifier for the humidity parameter.
    PRESSURE, // 2; Identifier for the pressure parameter.
    CO2, // 3; Identifier for the carbon dioxide (CO2) parameter.
    O2, // 4; Identifier for the oxygen (O2) parameter.
    LIGHT, // 5; Identifier for the light intensity parameter.
    WATER_TEMP, // 6; Identifier for the water temperature parameter.
    WATER_LEVEL, // 7; Identifier for the water level parameter.
    PH, // 8; Identifier for the pH level parameter.
    ELECTRONICS_TEMP, // 9; Identifier for the electronics temperature parameter.
    BATT_SOC, // 10; Identifier for the battery state of charge (SOC) parameter.
    BATT_VOLTAGE, // 11; Identifier for the battery voltage parameter.
    COUNT // 12; Identifier for the total count of parameters (used for iteration or validation).
};