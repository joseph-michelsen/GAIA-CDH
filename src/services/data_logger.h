#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "sample.h" // Include the Sample struct (what gets logged).
#include "storage.h" // Include the Storage contract (where it gets logged).

// The data logger: turns Samples into CSV rows and appends them to one file per mission day.
//   /day_000.csv, /day_001.csv, ...      (day = MET_s / 86400)
//   met_s,param,value,status             <- header, written once at the top of each file
//   120,CO2,812.5,OK
// It only knows the Storage contract, never the SD card itself.

class DataLogger
{
    public:
    static constexpr const char* kHeader = "met_s,param,value,status\n"; // First line of every day file.
    static constexpr size_t kFileNameLength = 13; // "/day_000.csv" is 12 characters + terminating zero.

    explicit DataLogger(Storage& storage) : storage_(storage) {} // Constructor that takes where to write.

    Status log(const Sample* samples, size_t count); // Append one row per sample. OK, or the first storage problem (those rows are lost).
    uint32_t rowsWritten() const { return rowsWritten_; } // Rows safely on storage since boot (for health telemetry).
    uint32_t rowsDropped() const { return rowsDropped_; } // Rows lost to storage problems since boot (for health telemetry).

    static void dayFileName(uint32_t MET_s, char* out, size_t capacity); // e.g. MET 90 000 s -> "/day_001.csv".

    private:
    static constexpr size_t kBufferSize = 512; // Rows are collected here and written with ONE append (fewer slow SD file opens).
    static constexpr uint32_t kNoDay = 0xFFFFFFFFu; // "No day file known to be ready yet."

    Status flush(uint32_t day, const char* text, size_t len, uint32_t rows); // Write collected rows (plus the header if the file is new).

    Storage& storage_; // Where the day files live.
    uint32_t readyDay_ = kNoDay; // The day whose file is known to exist with its header (skips an exists() check per write).
    uint32_t rowsWritten_ = 0; // Health counter.
    uint32_t rowsDropped_ = 0; // Health counter.
};