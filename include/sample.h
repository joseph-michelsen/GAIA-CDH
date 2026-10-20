#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include "status.h" // Include the header file that defines the Status enumeration class for sensor reading quality.
#include "param_id.h" // Include the header file that defines the ParamID enumeration class for parameter identifiers.

struct Sample // Each Sample struct represents a single sensor reading, including its parameter identifier, value, status, and the time at which it was recorded.
{
    ParamID id = ParamID::COUNT; // Initialize the parameter identifier to COUNT indicating an invalid or uninitialized state.
    float value = 0.0; // Initialize the parameter value to 0.0 as a default.
    Status status = Status::NOT_PRESENT; // Initialize the status to NOT_PRESENT indicating that the sensor or driver is not detected or no data is received yet.
    uint32_t MET_s = 0; // Initialize the Mission Elapsed Time (MET) in seconds to 0 as default. 
};

inline Sample makeSample(ParamID id, float value, Status status, uint32_t MET_s)
{
    Sample s; // Create a new Sample instance called "s".
    s.id = id; // Set the parameter identifier of the sample to the provided id.
    s.value = value; // Set the parameter value of the sample to the provided value.
    s.status = status; // Set the status of the sample to the provided status.
    s.MET_s = MET_s; // Set the Mission Elapsed Time (MET) of the sample to the provided MET_s.
    return s; // Return the constructed Sample instance.
}