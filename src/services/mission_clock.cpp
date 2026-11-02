#include "mission_clock.h" // Include the header file that defines the MissionClock class.
#include <stdio.h> // Include snprintf, used to write the mission file as text.

namespace
{
constexpr size_t kFileMax = 64; // The mission file is tiny: "start=817000000\nmet=1296000\n" is 28 bytes.

bool parseField(const char* text, const char* key, uint32_t& out) // Find "key=<digits>" in text and read the number.
{
  const char* p = text;
  size_t keyLen = 0;
  while (key[keyLen] != '\0')
  {
    ++keyLen;
  }
  for (; *p != '\0'; ++p)
  {
    bool match = true;
    for (size_t i = 0; i < keyLen; ++i)
    {
      if (p[i] != key[i])
      {
        match = false;
        break;
      }
    }
    if (match && p[keyLen] == '=')
    {
      p += keyLen + 1;
      if (*p < '0' || *p > '9')
      {
        return false; // "key=" with no number.
      }
      uint64_t value = 0;
      while (*p >= '0' && *p <= '9')
      {
        value = value * 10 + static_cast<uint64_t>(*p - '0');
        if (value > 0xFFFFFFFFull)
        {
          return false; // Too big for a uint32_t: corrupt.
        }
        ++p;
      }
      out = static_cast<uint32_t>(value);
      return true;
    }
  }
  return false; // Key not found.
}
}

MissionClock::Reading MissionClock::readRtc()
{
  Reading r;
  RtcTime t;
  bool trustworthy = false;
  if (rtc_.readTime(t, trustworthy) == Status::OK && trustworthy)
  {
    r.ok = true;
    r.s2000 = toSeconds2000(t);
  }
  return r;
}

void MissionClock::load()
{
  haveStart_ = false;
  savedMet_ = 0;

  uint8_t buf[kFileMax + 1] = {}; // +1 keeps a terminating zero so the text can be searched safely.
  size_t got = 0;
  if (storage_.readFile(kMissionFile, buf, kFileMax, got) != Status::OK)
  {
    return; // No file (first boot), no card, or unreadable: nothing to load.
  }
  buf[got] = '\0';
  const char* text = reinterpret_cast<const char*>(buf);

  uint32_t start = 0;
  uint32_t met = 0;
  if (parseField(text, "met", met))
  {
    savedMet_ = met;
  }
  if (parseField(text, "start", start) && start != 0) // start=0 means "unknown" (mission began without a trusted RTC).
  {
    haveStart_ = true;
    start_ = start;
  }
}

Status MissionClock::save(uint32_t met)
{
  char text[kFileMax];
  int n = snprintf(text, sizeof(text), "start=%lu\nmet=%lu\n", static_cast<unsigned long>(haveStart_ ? start_ : 0),
                   static_cast<unsigned long>(met));
  Status s = storage_.writeFile(kMissionFile, reinterpret_cast<const uint8_t*>(text), static_cast<size_t>(n));
  if (s == Status::OK)
  {
    lastSavedMet_ = met;
  }
  return s;
}

Status MissionClock::begin()
{
  Status worst = Status::OK; // Remember the first problem, but keep going: the clock must always run.

  Status rs = rtc_.begin(); // Make sure the RTC is running with its backup battery enabled.
  if (rs != Status::OK)
  {
    worst = rs;
  }

  Status ss = storage_.begin(); // Make sure the card is mounted.
  if (ss != Status::OK && worst == Status::OK)
  {
    worst = ss;
  }

  load(); // What does storage say about the mission so far?
  Reading r = readRtc();

  if (!haveStart_ && r.ok && savedMet_ == 0)
  {
    start_ = r.s2000; // First boot of the mission: it starts now.
    haveStart_ = true;
  }
  else if (!haveStart_ && r.ok)
  {
    start_ = r.s2000 - savedMet_; // Mission began without an RTC, but the RTC works now: anchor it so future reboots are exact.
    haveStart_ = true;
  }

  anchorMillis_ = millis_();
  if (haveStart_ && r.ok && r.s2000 >= start_)
  {
    source_ = ClockSource::RTC;
    anchorMet_ = r.s2000 - start_;
  }
  else
  {
    source_ = ClockSource::ESTIMATED; // RTC lost its time (or went backwards): continue from the last saved MET.
    anchorMet_ = savedMet_;
    if (worst == Status::OK)
    {
      worst = Status::OUT_OF_RANGE; // The clock runs, but MET may be short of the true value.
    }
  }

  lastMet_ = anchorMet_;
  Status vs = save(lastMet_); // Record the (possibly new) mission straight away.
  if (vs != Status::OK && worst == Status::OK)
  {
    worst = vs;
  }
  return worst;
}

uint32_t MissionClock::now()
{
  uint32_t nowMillis = millis_();
  uint32_t met = 0;

  Reading r = readRtc();
  if (haveStart_ && r.ok && r.s2000 >= start_)
  {
    met = r.s2000 - start_; // Exact.
    source_ = ClockSource::RTC;
    anchorMet_ = met; // Keep the estimate ready in case the RTC drops out next time.
    anchorMillis_ = nowMillis;
  }
  else
  {
    met = anchorMet_ + (nowMillis - anchorMillis_) / 1000u; // Estimate. (Unsigned subtraction also handles millis() wrapping after ~49 days.)
    source_ = ClockSource::ESTIMATED;
  }

  if (met < lastMet_)
  {
    met = lastMet_; // Never go backwards (e.g. if someone sets the RTC back).
  }
  lastMet_ = met;

  if (met - lastSavedMet_ >= kCheckpointS)
  {
    save(met); // Checkpoint, so an estimate after a future power loss starts from a recent value.
    lastSavedMet_ = met; // Even if the save failed, wait a full minute before trying again (don't hammer a missing card).
  }
  return met;
}

Status MissionClock::resetMission()
{
  Reading r = readRtc();
  haveStart_ = r.ok;
  start_ = r.ok ? r.s2000 : 0;
  anchorMet_ = 0;
  anchorMillis_ = millis_();
  lastMet_ = 0;
  lastSavedMet_ = 0;
  source_ = r.ok ? ClockSource::RTC : ClockSource::ESTIMATED;
  return save(0);
}