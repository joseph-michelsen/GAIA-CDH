#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.

enum class Status : uint8_t // Define an enumeration class named Status with a fixed underlying type of uint8_t to define the quality of a sensor reading.
{
    OK, // 0; Set if the value is within the expected range and is considered valid.
    STALE, // 1; Set if the value is outdated (set by the state table, never by a driver).
    OUT_OF_RANGE, // 2; Set if the value is physically impossible or outside the expected range (set by the driver).
    BUS_ERROR, // 3; Set if the sensor or device did not respond or timed out (set by the driver).
    NOT_PRESENT // 4; Set if the sensor or driver is not detected or no data is received yet.
};

inline bool isUsable(Status s) { return s == Status::OK; } // Returns true if the status is OK, indicating that the sensor reading is usable and valid.

inline const char* statusName(Status s) // Returns a string representation of the Status enumeration value for debugging and logging purposes.
{
    switch (s)
    {
        case Status::OK: return "OK";
        case Status::STALE: return "STALE";
        case Status::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case Status::BUS_ERROR: return "BUS_ERROR"; 
        case Status::NOT_PRESENT: return "NOT_PRESENT"; 
    }

    return "UNKNOWN_STATUS"; // Return a default string for unknown status values (this line is unreachable but included for completeness).
}