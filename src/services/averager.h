#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "sample.h" // Include the Sample struct (what gets averaged).
#include "param_id.h" // Include the ParamID enumeration class (one running average per parameter).
#include "status.h" // Include the Status enumeration class.

// One parameter's summary over one window (what the radio will send).

struct Average
{
    ParamID id = ParamID::COUNT; // Which parameter.
    float mean = 0.0f; // Mean of the OK samples only (0 if there were none).
    Status status = Status::NOT_PRESENT; // OK if at least one sample was OK; otherwise the most recent problem.
    uint8_t okCount = 0; // How many samples went into the mean.
    uint8_t sampleCount = 0; // How many samples arrived in total (OK or not).
};

// The averager: turns a stream of Samples into one Average per parameter per window.
// Windows are lined up with MET: window 0 = MET 0-59 s, window 1 = MET 60-119 s, ...
// A window is complete as soon as a sample from a LATER window arrives; it is then kept until takeCompleted().
// Only parameters that had at least one sample in the window are reported (fresh samples only, never filled in).

class Averager
{
    public:
    static constexpr uint32_t kWindow_s = 60; // Length of one averaging window.
    static constexpr size_t kParamCount = static_cast<size_t>(ParamID::COUNT); // One running sum per parameter.

    void add(const Sample* samples, size_t count); // Add one cycle's samples. Closes the current window if they belong to a later one.
    bool hasCompleted() const { return hasCompleted_; } // True if a finished window is waiting to be taken.
    size_t takeCompleted(Average* out, size_t capacity, uint32_t& windowStart_s); // Copy out the finished window's averages; returns how many. Clears hasCompleted().
    uint32_t windowsDropped() const { return windowsDropped_; } // Finished windows replaced before anyone took them (for health telemetry).

    private:
    struct Accumulator // Running totals for one parameter in one window.
    {
        float sum = 0.0f; // Sum of the OK values.
        uint8_t ok = 0; // OK samples so far.
        uint8_t total = 0; // All samples so far.
        Status lastProblem = Status::NOT_PRESENT; // Most recent non-OK status (reported if nothing was OK).
    };

    void closeWindow(); // Move current_ into completed_ and start an empty window.

    Accumulator current_[kParamCount]; // The window being filled.
    Accumulator completed_[kParamCount]; // The last finished window, waiting for takeCompleted().
    uint32_t currentWindow_ = 0; // Window number being filled (MET_s / kWindow_s).
    uint32_t completedWindow_ = 0; // Window number of completed_.
    bool started_ = false; // True once the first sample has arrived (so currentWindow_ means something).
    bool hasCompleted_ = false; // True while completed_ holds a window nobody has taken yet.
    uint32_t windowsDropped_ = 0; // Health counter.
};