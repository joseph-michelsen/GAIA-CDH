#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "i2c_bus.h" // Include the header file that defines the I2CBus class for I2C communication.
#include "sensor.h" // Include the header file that defines the Sensor class for sensor management.

// Driver for the Sensirion SCD30 NDIR CO2 sensor (Adafruit SCD-30 board, I2C address 0x61).
// Each read() produces 1 sample: out[0] = ParamID::CO2 in parts per million (ppm).
// The SCD30 also measures temperature and humidity, but those are NOT reported: the BME680 already
// provides AIR_TEMP and HUMIDITY, and two sources for the same ParamID would conflict.

class SCD30 : public Sensor // Define the SCD30 sensor class that inherits from the Sensor base class.
{
    public:
    using DelayFn = void (*)(uint32_t ms); // A "wait this many milliseconds" function: Arduino's delay() on the board, a fake in tests.

    static constexpr uint8_t kAddress = 0x61; // The SCD30's fixed I2C address.
    static constexpr uint8_t kSampleCount = 1; // The number of samples read() writes, so callers can size their arrays.

    SCD30(I2CBus& bus, DelayFn delayMs) : bus_(bus), delayMs_(delayMs) {} // Constructor: the bus to talk through and the wait function.

    const char* name() const override { return "SCD30"; } // Override the name function to return the name of the sensor as a string.
    Status begin() override; // Check the sensor answers, apply our settings, and start continuous measurement.
    size_t read(Sample* out, size_t capacity, uint32_t MET_s) override; // Wait for the next measurement and write 1 CO2 sample.

    Status setAmbientPressure(float pressurePa); // Tell the sensor the air pressure (e.g. from the BME680) so its CO2 reading is correct.

    static uint8_t crc8(const uint8_t* data, size_t len); // Sensirion's checksum: CRC-8, polynomial 0x31, start value 0xFF.

    private:
    Status sendCommand(uint16_t command); // Send a 2-byte command with no argument.
    Status sendCommand(uint16_t command, uint16_t argument); // Send a 2-byte command followed by a 2-byte argument and its checksum.
    Status query(uint16_t command, uint16_t* words, size_t count); // Send a command, wait, then read back 'count' 16-bit words (checksums verified).
    Status startMeasuring(); // (Re)start continuous measurement with the current pressure setting.

    I2CBus& bus_; // Reference to the I2C bus used to communicate with the SCD30.
    DelayFn delayMs_; // Function used to wait between a command and its reply.
    bool present_ = false; // True once begin() has found, configured and started the sensor.
    uint16_t pressureMbar_ = 0; // Ambient pressure sent to the sensor, in millibar (0 = compensation off).
};