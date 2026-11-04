#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "sample.h" // Include the Sample struct (what gets stored).
#include "param_id.h" // Include the ParamID enumeration class (one entry per parameter).
#include "status.h" // Include the Status enumeration class.

// The state table: the latest Sample of every parameter, and how old it is.
// The flight loop updates it every cycle; rules, fault checks and (later) control loops read from it.
// It is the only place that sets Status::STALE (see status.h): drivers report what they measured,
// the state table reports whether that is still recent enough to act on.

class StateTable
{
    public:
    static constexpr size_t kParamCount = static_cast<size_t>(ParamID::COUNT); // One entry per parameter.

    void update(const Sample* samples, size_t count); // Store each sample as its parameter's latest, replacing the previous one (OK or not).

    // The latest sample of id, as seen at MET now_s:
    //   never received          -> status NOT_PRESENT, value 0
    //   older than maxAge_s     -> status STALE, value = the old value (so it can still be displayed)
    //   otherwise               -> the sample exactly as stored (which may itself be BUS_ERROR, OUT_OF_RANGE, ...)
    Sample latest(ParamID id, uint32_t now_s, uint32_t maxAge_s) const;

    private:
    Sample latest_[kParamCount]; // Newest sample of each parameter. A default Sample means "never received" (status NOT_PRESENT).
    bool received_[kParamCount] = {}; // True once a parameter has had at least one sample.
};
