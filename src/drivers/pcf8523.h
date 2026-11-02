#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include "i2c_bus.h" // Include the header file that defines the I2CBus class for I2C communication.
#include "status.h" // Include the header file that defines the Status enumeration class.

// A calendar date and time of day, as kept by the real-time clock (24-hour clock, years 2000-2099).

struct RtcTime
{
    uint16_t year = 2000; // Full year, e.g. 2026.
    uint8_t month = 1; // 1-12.
    uint8_t day = 1; // 1-31 (depending on the month).
    uint8_t hour = 0; // 0-23.
    uint8_t minute = 0; // 0-59.
    uint8_t second = 0; // 0-59.
};

bool isValidTime(const RtcTime& t); // True if every field is in range, including the real number of days in that month (leap years included).
uint32_t toSeconds2000(const RtcTime& t); // Seconds since 2000-01-01 00:00:00. Two of these can be subtracted to get elapsed time (e.g. MET).
bool parseBuildTime(const char* date, const char* time, RtcTime& out); // Turn the compiler's __DATE__ ("Nov  1 2026") and __TIME__ ("20:54:05") into an RtcTime.

// Driver for the NXP PCF8523 real-time clock on the Adafruit Adalogger FeatherWing (I2C address 0x68).
// It is not a Sensor (it doesn't produce Samples); it keeps the date and time, running from a CR1220 coin cell when main power is off.

class PCF8523
{
    public:
    static constexpr uint8_t kAddress = 0x68; // The PCF8523's fixed I2C address.

    explicit PCF8523(I2CBus& bus) : bus_(bus) {} // Constructor that takes the I2C bus the clock is on.

    const char* name() const { return "PCF8523"; } // Name for logs and debugging.
    Status begin(); // Check the clock answers, make sure it is running, and enable the backup coin cell.
    Status readTime(RtcTime& out, bool& trustworthy); // Read the date and time. trustworthy = false if the clock stopped at some point (time was lost).
    Status setTime(const RtcTime& t); // Set the date and time (also marks the time as trustworthy again).

    private:
    I2CBus& bus_; // Reference to the I2C bus used to communicate with the clock.
};