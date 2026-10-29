#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "i2c_bus.h" // Include the header file that defines the I2CBus class for I2C communication.
#include "sensor.h" // Include the header file that defines the Sensor class for sensor management.

class MAX17048 : public Sensor // Define a class named MAX17048 that inherits from the Sensor class to represent a specific sensor model (MAX17048) with its own attributes and behaviors.
{
    public:
    explicit MAX17048(I2CBus& bus) : bus_(bus) {} // Constructor that takes a reference to an I2CBus object and initializes the bus_ member variable for I2C communication with the sensor.
   
    const char* name() const override { return "MAX17048"; } // Override the name function to return the name of the sensor as a string.
    Status begin() override; // Override the begin function to initialize the sensor and return its status.
    size_t read(Sample* out, size_t capacity, uint32_t MET_s) override; // Override the read function to read sensor data into an array of Sample structs, returning the number of samples read.

    private:
    Status readWord(uint8_t reg, uint16_t& value); // Declare a private function to read a 16-bit word from the specified register of the sensor, returning the status of the operation and storing the value in the provided reference.
    I2CBus& bus_; // Declare a reference to an I2CBus object for I2C communication with the sensor.
    bool present_ = false; // Initialize a boolean member variable to track whether the sensor is present and detected on the I2C bus.
};