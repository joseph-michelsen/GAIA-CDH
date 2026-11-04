#include "flight_loop.h" // Include the header file that defines the FlightLoop class.
#include "services/averager.h" // Include the full Averager class (the header only needed its name).
#include "services/state_table.h" // Include the full StateTable class (same reason).

Status FlightLoop::begin()
{
  if (slotCount_ > kMaxSensors)
  {
    return Status::OUT_OF_RANGE; // Programming mistake: the table is bigger than the loop supports. runCycle() only uses the first kMaxSensors.
  }

  Status first = Status::OK; // The first problem found (stays OK if every sensor starts).
  for (size_t i = 0; i < slotCount_; ++i)
  {
    if (slots_[i].sensor == nullptr)
    {
      continue; // Empty slot: nothing to start.
    }

    Status s = slots_[i].sensor->begin(); // Start this sensor. If it fails, its read() will retry later.
    if (s != Status::OK && first == Status::OK)
    {
      first = s; // Remember the first problem, but keep starting the other sensors.
    }
  }
  return first;
}

bool FlightLoop::isDue(const SensorSlot& slot) const
{
  if (slot.everyNCycles == 0)
  {
    return false; // 0 = disabled (and avoids dividing by zero below).
  }
  return cycles_ % slot.everyNCycles == 0; // Cycle 0 reads every sensor, so the first log rows after boot are a full snapshot.
}

Status FlightLoop::runCycle()
{
  const uint32_t MET_s = clock_.now(); // ONE timestamp for the whole cycle, read before any sensor.
  lastMet_ = MET_s; // Rules run after the cycle use this as "now", without reading the RTC again.
  const size_t count = (slotCount_ < kMaxSensors) ? slotCount_ : kMaxSensors; // Never index past the supported table size.

  size_t n = 0; // Samples collected so far this cycle.
  for (size_t i = 0; i < count; ++i)
  {
    const SensorSlot& slot = slots_[i];
    if (slot.sensor == nullptr || !isDue(slot))
    {
      continue; // Empty slot, or not this sensor's turn.
    }

    const size_t got = slot.sensor->read(buffer_ + n, kMaxSamples - n, MET_s); // Fills the next free part of the buffer. A driver writes nothing if there isn't room.
    if (got == 0)
    {
      ++readsDropped_; // A due sensor always reports something (even a failure), so 0 means its data had nowhere to go.
    }
    n += got;
  }

  Status s = logger_.log(buffer_, n); // Store first: the SD card is the complete record. (Logging 0 samples is fine.)
  state_.update(buffer_, n); // Then keep the latest value of each parameter, for rules and control.
  averager_.add(buffer_, n); // Then summarize for the radio. Both are done even if logging failed: they don't depend on the card.

  ++cycles_; // Advance the schedule last, so isDue() above saw this cycle's number.
  return s;
}
