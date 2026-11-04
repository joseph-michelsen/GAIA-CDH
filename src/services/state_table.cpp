#include "state_table.h" // Include the header file that defines the StateTable class.

void StateTable::update(const Sample* samples, size_t count)
{
  if (samples == nullptr)
  {
    return; // Nothing to store.
  }

  for (size_t i = 0; i < count; ++i)
  {
    const size_t index = static_cast<size_t>(samples[i].id); // ParamID doubles as the array index.
    if (index >= kParamCount)
    {
      continue; // Not a real parameter (e.g. ParamID::COUNT = never filled in): ignore it.
    }
    latest_[index] = samples[i]; // Newest wins, whatever its status: the table reports the sensor's current state.
    received_[index] = true;
  }
}

Sample StateTable::latest(ParamID id, uint32_t now_s, uint32_t maxAge_s) const
{
  const size_t index = static_cast<size_t>(id);
  if (index >= kParamCount || !received_[index])
  {
    return makeSample(id, 0.0f, Status::NOT_PRESENT, now_s); // Never received (or not a real parameter).
  }

  Sample s = latest_[index]; // A copy: the caller can't change the table.
  const uint32_t age = (now_s >= s.MET_s) ? (now_s - s.MET_s) : 0; // A sample "from the future" can't happen (MET never goes backwards); treat it as brand new.
  if (age > maxAge_s)
  {
    s.status = Status::STALE; // Too old to act on. The value stays, so it can still be displayed.
  }
  return s;
}
