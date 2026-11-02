#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include "status.h" // Include the header file that defines the Status enumeration class.
#include "storage.h" // Include the Storage contract (where the mission start time is saved).
#include "drivers/pcf8523.h" // Include the real-time clock driver.

// Where the current MET came from.

enum class ClockSource : uint8_t
{
    RTC, // Calculated from the real-time clock: exact, and survives reboots.
    ESTIMATED // RTC unavailable or untrusted: last saved MET + time counted by the processor since then (may run short after a power loss).
};

// The mission clock: the ONE place the rest of the software gets Mission Elapsed Time (MET_s) from.
//   MET_s = (RTC time now) - (mission start time saved on storage)
// so MET keeps counting correctly across reboots. If the RTC can't be trusted, it falls back to an estimate
// instead of stopping, and says so via source().

class MissionClock
{
    public:
    using MillisFn = uint32_t (*)(); // "Milliseconds since this boot": Arduino's millis() on the board, a fake in tests.

    static constexpr const char* kMissionFile = "/mission.txt"; // Where the mission start time and last MET are saved.
    static constexpr uint32_t kCheckpointS = 60; // Save the latest MET to storage at most once a minute.

    MissionClock(PCF8523& rtc, Storage& storage, MillisFn millisFn) : rtc_(rtc), storage_(storage), millis_(millisFn) {}

    Status begin(); // Load the saved mission (or start a new one). The clock always runs afterwards; the Status reports the worst problem found.
    uint32_t now(); // Current MET in seconds. Never goes backwards.
    ClockSource source() const { return source_; } // Whether the last now() was exact (RTC) or estimated.
    Status resetMission(); // Start a new mission at MET 0 (bench testing only).

    private:
    struct Reading // One attempt to read the RTC.
    {
        bool ok = false; // True if the read worked AND the clock never stopped.
        uint32_t s2000 = 0; // The time, as seconds since 2000-01-01.
    };

    Reading readRtc(); // Read the RTC and convert it to seconds since 2000.
    Status save(uint32_t met); // Write the mission start and this MET to storage.
    void load(); // Read the mission file into start_/savedMet_.

    PCF8523& rtc_; // The real-time clock.
    Storage& storage_; // Where the mission file lives.
    MillisFn millis_; // Processor time since boot.

    bool haveStart_ = false; // True once we know when the mission started (in RTC time).
    uint32_t start_ = 0; // Mission start, seconds since 2000.
    uint32_t savedMet_ = 0; // MET found in the mission file at boot.
    uint32_t anchorMet_ = 0; // For estimates: MET at the moment anchorMillis_ was taken.
    uint32_t anchorMillis_ = 0; // For estimates: millis() at that moment.
    uint32_t lastMet_ = 0; // Last value returned by now() (so MET never goes backwards).
    uint32_t lastSavedMet_ = 0; // Last MET written to storage.
    ClockSource source_ = ClockSource::ESTIMATED; // Where the last MET came from.
};