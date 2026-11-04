#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include "services/state_table.h" // Include the state table (where rules get their inputs).

// Cross-sensor rules: decisions that connect one parameter to another part of the system.
// Each rule only DECIDES, from the state table; main.cpp carries out the decision (e.g. calls the driver).
// That keeps every rule pure logic, testable on the laptop with no hardware or driver involved.

namespace rules
{

constexpr uint32_t kPressureMaxAge_s = 30; // 3x the BME680's 10 s period: tolerates one or two missed reads.

// The pressure to give the SCD30, if there is a fresh, valid one.
// Returns true and sets pressurePa if the latest PRESSURE is OK and no older than kPressureMaxAge_s.
// Returns false otherwise; the caller then does nothing, so the SCD30 keeps its last pressure.
bool co2PressureCompensation(const StateTable& state, uint32_t now_s, float& pressurePa);

}
