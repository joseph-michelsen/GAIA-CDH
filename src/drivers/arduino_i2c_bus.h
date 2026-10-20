#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <Wire.h> // Include the Wire library for I2C communication.
#include "i2c_bus.h" // Include the header file that defines the I2CBus class for I2C communication.

class ArduinoI2CBus : public I2CBus // Define a class named ArduinoI2CBus that inherits from the I2CBus class to implement I2C communication using the Arduino Wire library.
{
    public:
    explicit ArduinoI2CBus(TwoWire& wire) : wire_(wire) {} // Constructor that takes a reference to a TwoWire object and initializes the wire_ member variable.
    bool begin(uint8_t sda, uint8_t scl, uint32_t hz, uint16_t timeout_ms); // Declare a function to initialize the I2C bus with specified SDA and SCL pins, clock frequency, and timeout in milliseconds.
    bool probe(uint8_t addr) override; // Override the probe function to check for the presence of a device at the specified I2C address.
    Status readRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) override; // Override the readRegs function to read multiple registers from a device at the specified I2C address, returning the status of the operation.
    Status writeRegs(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) override; // Override the writeRegs function to write multiple registers to a device at the specified I2C address, returning the status of the operation.
    
    private:
    static constexpr int kMaxAttempts = 3; // Define a constant for the maximum number of attempts to communicate with a device on the I2C bus.
    TwoWire& wire_; // Declare a reference to a TwoWire object for I2C communication.
};