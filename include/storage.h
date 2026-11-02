#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "status.h" // Include the header file that defines the Status enumeration class.

class Storage
{
    public:
    static constexpr size_t kMaxPathLength = 31; // Longest allowed path, e.g. "/log_2026-11-01.csv" is 19.

    virtual ~Storage() {} // Declare a virtual destructor to allow proper cleanup of derived classes.

    virtual Status begin() = 0; // Get the medium ready (on the board: mount the SD card). NOT_PRESENT if there is no card.
    virtual bool exists(const char* path) = 0; // True if the file exists.
    virtual Status append(const char* path, const uint8_t* data, size_t len) = 0; // Add bytes to the end of a file, creating it if needed.
    virtual Status writeFile(const char* path, const uint8_t* data, size_t len) = 0; // Replace a whole (small) file with these bytes.
    virtual Status readFile(const char* path, uint8_t* out, size_t capacity, size_t& got) = 0; // Read a file into out; got = bytes read.
    virtual Status freeBytes(uint64_t& bytes) = 0; // Space left on the medium, for health telemetry.
};

inline bool isValidPath(const char* path) // Shared check for the path rules above, so every implementation agrees.
{
  if (path == nullptr || path[0] != '/')
  {
    return false; // Must start with "/".
  }
  size_t n = 1;
  for (; path[n] != '\0'; ++n)
  {
    if (path[n] == '/' || n >= Storage::kMaxPathLength)
    {
      return false; // No sub-folders, and not too long.
    }
  }
  return n > 1; // "/" on its own is not a file.
}
