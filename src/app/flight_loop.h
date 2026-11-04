#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "sensor.h" // Include the Sensor contract (what gets read).
#include "status.h" // Include the Status enumeration class.
#include "services/mission_clock.h" // Include the mission clock (where MET_s comes from).
#include "services/data_logger.h" // Include the data logger (where samples get stored).

class Averager; // Declared here, defined in services/averager.h. A reference only needs the name, not the full class.
class StateTable; // Declared here, defined in services/state_table.h (same reason).

// One entry in the sensor table: which sensor, and how often to read it.
// The table itself lives in main.cpp, so adding a sensor never changes the flight loop.

// No default values on purpose: under C++11 (the ESP32 toolchain's default), a struct with default member values
// can't be written as {&sensor, 2}. Every table entry gives both values anyway.

struct SensorSlot
{
    Sensor* sensor; // The sensor to read (nullptr = empty slot).
    uint16_t everyNCycles; // Read it every N cycles: 2 = every 10 s, 12 = every 60 s (at a 5 s cycle); 0 = disabled. Not const, so it can be changed in flight later.
};

// The flight loop: runs one cycle of sense -> timestamp -> store at a time.
// It only knows the Sensor, MissionClock, DataLogger, Averager and StateTable contracts, never a specific part.
// Rules that connect specific sensors (e.g. pressure -> SCD30) run in main.cpp after each cycle, reading the state table.
// main.cpp decides WHEN a cycle runs (every kCyclePeriod_s); runCycle() decides WHAT happens in it.
//
// Retrying failed sensors is each driver's job, not the loop's: a driver's read() calls begin() again
// whenever its sensor isn't working, and always fills in its samples with a Status saying why.

class FlightLoop
{
    public:
    static constexpr uint32_t kCyclePeriod_s = 5; // Time between cycles. Every sensor period is a multiple of this.
    static constexpr size_t kMaxSensors = 12; // Most sensor table entries the loop accepts.
    static constexpr size_t kMaxSamples = 24; // Room for every sample from one cycle (all sensors due at once).

    FlightLoop(SensorSlot* slots, size_t slotCount, MissionClock& clock, DataLogger& logger, Averager& averager, StateTable& state)
        : slots_(slots), slotCount_(slotCount), clock_(clock), logger_(logger), averager_(averager), state_(state) {}

    Status begin(); // begin() every sensor once at boot. Keeps going if one fails; returns the first problem found (OK if none).
    Status runCycle(); // Read every sensor that is due, stamp with one MET, then log, update the state table and average. Returns the logger's Status.
    uint32_t cycles() const { return cycles_; } // Cycles run since boot (for health telemetry).
    uint32_t lastMet() const { return lastMet_; } // MET of the most recent cycle (what rules should use as "now").
    uint32_t readsDropped() const { return readsDropped_; } // Due sensors that produced no samples, e.g. because the buffer was full (for health telemetry).

    private:
    bool isDue(const SensorSlot& slot) const; // True if this slot should be read on the current cycle.

    SensorSlot* slots_; // The sensor table (owned by main.cpp).
    size_t slotCount_; // How many entries the table has.
    MissionClock& clock_; // Where MET_s comes from.
    DataLogger& logger_; // Where samples are stored (SD card).
    Averager& averager_; // Where samples are summarized (1-minute averages for the radio).
    StateTable& state_; // Where the latest value of each parameter is kept (for rules and control).
    Sample buffer_[kMaxSamples]; // One cycle's samples, filled in table order. Fixed size: no dynamic memory.
    uint32_t cycles_ = 0; // Cycles run since boot (also decides which slots are due).
    uint32_t lastMet_ = 0; // MET of the most recent cycle.
    uint32_t readsDropped_ = 0; // Health counter.
};