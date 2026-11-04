#include "averager.h" // Include the header file that defines the Averager class.

namespace
{
constexpr uint8_t kCountMax = 255; // Largest count a uint8_t holds. At 5 s cycles a window has at most 12 samples, so this is only a safety limit.
}

void Averager::add(const Sample* samples, size_t count)
{
  if (samples == nullptr)
  {
    return; // Nothing to add.
  }

  for (size_t i = 0; i < count; ++i)
  {
    const Sample& s = samples[i];
    const size_t index = static_cast<size_t>(s.id); // ParamID doubles as the array index.
    if (index >= kParamCount)
    {
      continue; // Not a real parameter (e.g. ParamID::COUNT = never filled in): ignore it.
    }

    const uint32_t window = s.MET_s / kWindow_s; // Which window this sample belongs to (integer division rounds down).
    if (!started_)
    {
      currentWindow_ = window; // First sample ever: start its window.
      started_ = true;
    }
    else if (window > currentWindow_)
    {
      closeWindow(); // A sample from a later window: the current one is finished.
      currentWindow_ = window; // Skipped windows (a MET jump) simply have no averages.
    }
    // window < currentWindow_ can't happen: MissionClock never goes backwards. If it ever did, the sample joins the current window.

    Accumulator& a = current_[index];
    if (a.total == kCountMax)
    {
      continue; // Full (can't happen at normal rates): stop counting rather than wrap around to 0.
    }
    ++a.total;

    if (isUsable(s.status))
    {
      a.sum += s.value; // Only OK values go into the mean.
      ++a.ok;
    }
    else
    {
      a.lastProblem = s.status; // Remember the most recent problem, in case nothing in this window is OK.
    }
  }
}

void Averager::closeWindow()
{
  if (hasCompleted_)
  {
    ++windowsDropped_; // The previous finished window was never taken: it is about to be replaced.
  }

  for (size_t i = 0; i < kParamCount; ++i)
  {
    completed_[i] = current_[i]; // Keep the finished window...
    current_[i] = Accumulator(); // ...and start the next one empty.
  }
  completedWindow_ = currentWindow_;
  hasCompleted_ = true;
}

size_t Averager::takeCompleted(Average* out, size_t capacity, uint32_t& windowStart_s)
{
  if (!hasCompleted_ || out == nullptr)
  {
    return 0; // Nothing finished yet (or nowhere to put it). A finished window stays waiting.
  }

  windowStart_s = completedWindow_ * kWindow_s; // e.g. window 42 -> MET 2520 s.

  size_t n = 0; // Averages written so far.
  for (size_t i = 0; i < kParamCount && n < capacity; ++i)
  {
    const Accumulator& a = completed_[i];
    if (a.total == 0)
    {
      continue; // No samples for this parameter in the window: report nothing (fresh samples only).
    }

    Average& avg = out[n++];
    avg.id = static_cast<ParamID>(i);
    avg.okCount = a.ok;
    avg.sampleCount = a.total;
    if (a.ok > 0)
    {
      avg.mean = a.sum / static_cast<float>(a.ok); // Mean of the OK samples.
      avg.status = Status::OK;
    }
    else
    {
      avg.mean = 0.0f; // No valid data: the value means nothing, the status says why.
      avg.status = a.lastProblem;
    }
  }

  hasCompleted_ = false; // Taken: the next closeWindow() won't count a drop.
  return n;
}