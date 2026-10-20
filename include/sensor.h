#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "sample.h" // Include the header file that defines the Sample struct for sensor readings.
#include "status.h" // Include the header file that defines the Status enumeration class for sensor reading quality.

class Sensor // Define a base class named Sensor to represent a generic sensor with common attributes and behaviors.
{
    public:
    virtual ~Sensor() {} // Declare a virtual destructor to allow proper cleanup of derived classes.
    virtual const char* name() const = 0; // Declare a pure virtual function to return the name of the sensor as a string.
    virtual Status begin() = 0; // Declare a pure virtual function to initialize the sensor and return its status.
    virtual size_t read(Sample* out, size_t capacity, uint32_t MET_s) = 0; // Declare a pure virtual function to read sensor data into an array of Sample structs, returning the number of samples read.
};