#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.

enum class Status : uint8_t // Define an enumeration class named Status with a fixed underlying type of uint8_t to define the quality of a sensor reading.
{
    OK, // Set if the value is within the expected range and is considered valid.
    STALE, // Set if the value is outdated (set by the state table, never by a driver).
    OUT_OF_RANGE, // Set if the value is physically impossible or outside the expected range (set by the driver).
    BUS_ERROR, // Set if the sensor or device did not respond or timed out (set by the driver).
    NOT_PRESENT // Set if the sensor or driver is not detected or no data is received yet.
};

inline bool isUsable(Status s) { return s == Status::OK; } // Returns true if the status is OK, indicating that the sensor reading is usable and valid.