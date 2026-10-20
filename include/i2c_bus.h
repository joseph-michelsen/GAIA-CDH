#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "status.h" // Include the header file that defines the Status enumeration class for sensor reading quality.

class I2CBus
{
    public:
    virtual ~I2CBus() {} // Declare a virtual destructor to allow proper cleanup of derived classes.
    virtual bool probe(uint8_t addr) = 0; // Declare a pure virtual function to probe the I2C bus for a device at the specified address, returning true if the device is present.
    virtual Status readRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) = 0; // Declare a pure virtual function to read multiple registers from a device at the specified address, returning the status of the operation.
    virtual Status writeRegs(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) = 0; // Declare a pure virtual function to write multiple registers to a device at the specified address, returning the status of the operation.
};