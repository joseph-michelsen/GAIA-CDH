#include "data_logger.h" // Include the header file that defines the DataLogger class.
#include <stdio.h> // Include snprintf, used to format rows as text.
#include <string.h> // Include strlen.

namespace
{
constexpr uint32_t kSecondsPerDay = 86400; // 24 * 60 * 60.
constexpr size_t kMaxRowLength = 64; // Longest possible row, e.g. "4294967295,ELECTRONICS_TEMP,-1.234568e+38,OUT_OF_RANGE\n" is 56.
}

void DataLogger::dayFileName(uint32_t MET_s, char* out, size_t capacity)
{
  snprintf(out, capacity, "/day_%03lu.csv", static_cast<unsigned long>(MET_s / kSecondsPerDay)); // Day 0 = first 24 h of the mission.
}

Status DataLogger::flush(uint32_t day, const char* text, size_t len, uint32_t rows)
{
  if (rows == 0)
  {
    return Status::OK; // Nothing collected.
  }

  char name[kFileNameLength];
  dayFileName(day * kSecondsPerDay, name, sizeof(name));

  Status s = Status::OK;
  if (readyDay_ != day && !storage_.exists(name))
  {
    s = storage_.append(name, reinterpret_cast<const uint8_t*>(kHeader), strlen(kHeader)); // New file: header first.
  }
  if (s == Status::OK)
  {
    s = storage_.append(name, reinterpret_cast<const uint8_t*>(text), len); // All the collected rows in one write.
  }

  if (s == Status::OK)
  {
    readyDay_ = day; // This file exists and has its header: no need to check again.
    rowsWritten_ += rows;
  }
  else
  {
    readyDay_ = kNoDay; // Something went wrong (card removed?): check the file again next time.
    rowsDropped_ += rows;
  }
  return s;
}

Status DataLogger::log(const Sample* samples, size_t count)
{
  if (samples == nullptr)
  {
    return count == 0 ? Status::OK : Status::OUT_OF_RANGE;
  }

  char buffer[kBufferSize]; // Rows waiting to be written.
  size_t used = 0; // Bytes in buffer.
  uint32_t rows = 0; // Rows in buffer.
  uint32_t day = 0; // Mission day of the rows in buffer.
  Status worst = Status::OK; // First problem seen.

  for (size_t i = 0; i < count; ++i)
  {
    const Sample& s = samples[i];
    if (s.id >= ParamID::COUNT)
    {
      continue; // Never filled in: nothing to log.
    }

    uint32_t sampleDay = s.MET_s / kSecondsPerDay;
    if (rows > 0 && (sampleDay != day || used + kMaxRowLength > kBufferSize))
    {
      Status f = flush(day, buffer, used, rows); // Day changed, or buffer nearly full: write what we have.
      if (f != Status::OK && worst == Status::OK)
      {
        worst = f;
      }
      used = 0;
      rows = 0;
    }
    day = sampleDay;

    int n = snprintf(buffer + used, kBufferSize - used, "%lu,%s,%.7g,%s\n", // %.7g: all the digits a float really has, no padding.
                     static_cast<unsigned long>(s.MET_s), paramName(s.id), static_cast<double>(s.value), statusName(s.status));
    if (n > 0 && static_cast<size_t>(n) < kBufferSize - used)
    {
      used += static_cast<size_t>(n);
      rows++;
    }
    else
    {
      rowsDropped_++; // Can't happen with real values (rows are at most ~56 characters), but never overflow the buffer.
    }
  }

  Status f = flush(day, buffer, used, rows); // Write whatever is left.
  if (f != Status::OK && worst == Status::OK)
  {
    worst = f;
  }
  return worst;
}