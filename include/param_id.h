#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.

enum class ParamID : uint8_t // Define an enumeration class named ParamID with a fixed underlying type of uint8_t to represent unique identifiers for various parameters in the system.
{
    AIR_TEMP, // Identifier for the air temperature parameter.
    HUMIDITY, // Identifier for the humidity parameter.
    PRESSURE, // Identifier for the pressure parameter.
    CO2, // Identifier for the carbon dioxide (CO2) parameter.
    O2, // Identifier for the oxygen (O2) parameter.
    LIGHT, // Identifier for the light intensity parameter.
    WATER_TEMP, // Identifier for the water temperature parameter.
    WATER_LEVEL, // Identifier for the water level parameter.
    PH, // Identifier for the pH level parameter.
    ELECTRONICS_TEMP, // Identifier for the electronics temperature parameter.
    BATT_SOC, // Identifier for the battery state of charge (SOC) parameter.
    COUNT // Identifier for the total count of parameters (used for iteration or validation).
};