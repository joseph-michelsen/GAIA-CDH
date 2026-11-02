#include "scd30.h" // Include the header file that defines the SCD30 class for the SCD30 CO2 sensor.
#include <string.h> // Include memcpy, used to turn 4 raw bytes into a float safely.
#include <math.h> // Include isfinite, used to reject "not a number" readings.

// Commands, checksum, data format and timing follow Sensirion's "SCD30 Interface Description" and
// Sensirion's official driver (github.com/Sensirion/embedded-i2c-scd30).

namespace
{
constexpr uint16_t kCmdStartContinuous = 0x0010; // Start continuous measurement; argument = ambient pressure in mbar (0 = off).
constexpr uint16_t kCmdMeasurementInterval = 0x4600; // Get (no argument) or set (with argument) the measurement interval in seconds.
constexpr uint16_t kCmdDataReady = 0x0202; // Returns 1 if a new measurement is waiting, 0 if not.
constexpr uint16_t kCmdReadMeasurement = 0x0300; // Returns CO2, temperature, humidity as three 32-bit floats.
constexpr uint16_t kCmdAutoCalibration = 0x5306; // Get or set automatic self-calibration (ASC): 1 = on, 0 = off.
constexpr uint16_t kCmdFirmwareVersion = 0xD100; // Returns the firmware version (major in the high byte, minor in the low byte).

constexpr uint16_t kIntervalS = 2; // Measure every 2 seconds (the sensor's default and fastest setting).
constexpr uint16_t kAscOff = 0; // Automatic self-calibration OFF (see the wiki for why).

constexpr uint32_t kCommandDelayMs = 10; // Wait after each command before the next transfer (Sensirion's driver uses 10 ms).
constexpr uint32_t kPollMs = 100; // Wait between "is data ready?" checks (Sensirion's driver uses 100 ms).
constexpr int kPollTries = 25; // Give up after 25 checks = 2.5 s, longer than one 2 s measurement interval.

constexpr uint16_t kMinPressureMbar = 700; // Lowest pressure the sensor can compensate for.
constexpr uint16_t kMaxPressureMbar = 1200; // Highest pressure the sensor can compensate for.
constexpr int kPressureStepMbar = 2; // Only resend the pressure if it changes by at least this much.

constexpr float kMinCo2Ppm = 0.0f; // Bottom of the sensor's measurement range.
constexpr float kMaxCo2Ppm = 40000.0f; // Top of the sensor's measurement range.

constexpr size_t kMaxWords = 6; // The longest reply we read: 6 words (3 floats).
}

uint8_t SCD30::crc8(const uint8_t* data, size_t len)
{
  uint8_t crc = 0xFF; // Start value.
  for (size_t i = 0; i < len; ++i)
  {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
    {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1); // Polynomial x^8 + x^5 + x^4 + 1.
    }
  }
  return crc;
}

Status SCD30::sendCommand(uint16_t command)
{
  const uint8_t frame[2] = {static_cast<uint8_t>(command >> 8), static_cast<uint8_t>(command & 0xFF)}; // Most significant byte first.
  return bus_.write(kAddress, frame, sizeof(frame));
}

Status SCD30::sendCommand(uint16_t command, uint16_t argument)
{
  uint8_t frame[5] = {static_cast<uint8_t>(command >> 8), static_cast<uint8_t>(command & 0xFF),
                      static_cast<uint8_t>(argument >> 8), static_cast<uint8_t>(argument & 0xFF), 0};
  frame[4] = crc8(&frame[2], 2); // Every 2-byte argument is followed by its checksum.
  return bus_.write(kAddress, frame, sizeof(frame));
}

Status SCD30::query(uint16_t command, uint16_t* words, size_t count)
{
  if (count == 0 || count > kMaxWords)
  {
    return Status::BUS_ERROR; // Programming mistake: refuse rather than overflow the buffer.
  }

  Status s = sendCommand(command); // Ask the question (ends with a STOP: the SCD30 doesn't support repeated start).
  if (s != Status::OK)
  {
    return s;
  }
  if (delayMs_ != nullptr)
  {
    delayMs_(kCommandDelayMs); // Give the sensor time to prepare its answer.
  }

  uint8_t raw[kMaxWords * 3] = {}; // Each word arrives as 2 data bytes + 1 checksum byte.
  s = bus_.read(kAddress, raw, count * 3); // Read the answer in its own transaction.
  if (s != Status::OK)
  {
    return s;
  }

  for (size_t i = 0; i < count; ++i)
  {
    const uint8_t* w = &raw[i * 3];
    if (crc8(w, 2) != w[2])
    {
      return Status::BUS_ERROR; // The data was corrupted on the wires: don't use any of it.
    }
    words[i] = static_cast<uint16_t>((static_cast<uint16_t>(w[0]) << 8) | w[1]);
  }
  return Status::OK;
}

Status SCD30::startMeasuring()
{
  Status s = sendCommand(kCmdStartContinuous, pressureMbar_); // Start (or restart) measuring, with pressure compensation.
  if (s == Status::OK && delayMs_ != nullptr)
  {
    delayMs_(kCommandDelayMs);
  }
  return s;
}

Status SCD30::begin()
{
  present_ = false; // Assume nothing until every step below succeeds.

  uint16_t firmware = 0; // The SCD30 has no chip-ID register; a valid, checksummed firmware-version reply is the identity check.
  if (query(kCmdFirmwareVersion, &firmware, 1) != Status::OK)
  {
    return Status::NOT_PRESENT;
  }

  // Interval and ASC are stored in the sensor's non-volatile memory, so only write them if they differ.
  
  uint16_t value = 0;
  Status s = query(kCmdMeasurementInterval, &value, 1);
  if (s == Status::OK && value != kIntervalS)
  {
    s = sendCommand(kCmdMeasurementInterval, kIntervalS);
    if (s == Status::OK && delayMs_ != nullptr)
    {
      delayMs_(kCommandDelayMs);
    }
  }
  if (s != Status::OK)
  {
    return s;
  }

  s = query(kCmdAutoCalibration, &value, 1);
  if (s == Status::OK && value != kAscOff)
  {
    s = sendCommand(kCmdAutoCalibration, kAscOff);
    if (s == Status::OK && delayMs_ != nullptr)
    {
      delayMs_(kCommandDelayMs);
    }
  }
  if (s != Status::OK)
  {
    return s;
  }

  s = startMeasuring(); // Begin continuous measurement.
  if (s != Status::OK)
  {
    return s;
  }

  present_ = true;
  return Status::OK;
}

Status SCD30::setAmbientPressure(float pressurePa)
{
  uint16_t mbar = 0; // 0 = compensation off.
  if (isfinite(pressurePa))
  {
    float rounded = pressurePa / 100.0f + 0.5f; // Pa -> mbar (1 mbar = 100 Pa), rounded to the nearest whole mbar.
    if (rounded >= kMinPressureMbar && rounded < kMaxPressureMbar + 1)
    {
      mbar = static_cast<uint16_t>(rounded);
    }
  }

  int change = static_cast<int>(mbar) - static_cast<int>(pressureMbar_);
  bool onOff = (mbar == 0) != (pressureMbar_ == 0); // Turning compensation on or off always counts as a change.
  if (!onOff && change < kPressureStepMbar && change > -kPressureStepMbar)
  {
    return Status::OK; // Too small to matter: don't bother the sensor.
  }

  pressureMbar_ = mbar;
  if (!present_)
  {
    return Status::OK; // Will be applied by begin().
  }

  Status s = startMeasuring(); // Sending the start command again is how a new pressure is applied.
  if (s != Status::OK)
  {
    present_ = false; // Set everything up again next time.
  }
  return s;
}

size_t SCD30::read(Sample* out, size_t capacity, uint32_t MET_s)
{
  if (out == nullptr || capacity < kSampleCount)
  {
    return 0; // No room for the sample: write nothing.
  }

  if (!present_)
  {
    Status s = begin(); // (Re)find, configure and start the sensor.
    if (s != Status::OK)
    {
      out[0] = makeSample(ParamID::CO2, 0.0f, s, MET_s);
      return kSampleCount;
    }
  }

  bool ready = false; // Becomes true when the sensor has a new measurement.
  for (int attempt = 0; attempt < kPollTries; ++attempt)
  {
    uint16_t flag = 0;
    if (query(kCmdDataReady, &flag, 1) != Status::OK)
    {
      present_ = false;
      out[0] = makeSample(ParamID::CO2, 0.0f, Status::BUS_ERROR, MET_s);
      return kSampleCount;
    }
    if (flag == 1)
    {
      ready = true;
      break;
    }
    if (delayMs_ != nullptr)
    {
      delayMs_(kPollMs); // Not yet: wait and ask again.
    }
  }

  if (!ready)
  {
    present_ = false; // The sensor stopped producing measurements: set it up again next time.
    out[0] = makeSample(ParamID::CO2, 0.0f, Status::BUS_ERROR, MET_s);
    return kSampleCount;
  }

  uint16_t w[6] = {}; // CO2, temperature, humidity: each a float sent as two 16-bit words.
  if (query(kCmdReadMeasurement, w, 6) != Status::OK)
  {
    present_ = false;
    out[0] = makeSample(ParamID::CO2, 0.0f, Status::BUS_ERROR, MET_s);
    return kSampleCount;
  }

  uint32_t bits = (static_cast<uint32_t>(w[0]) << 16) | w[1]; // The 32 bits of the CO2 float, most significant word first.
  float co2 = 0.0f;
  memcpy(&co2, &bits, sizeof(co2)); // Reinterpret the bits as an IEEE-754 float (the sensor's format).

  Status st = Status::OK;
  if (!isfinite(co2) || co2 < kMinCo2Ppm || co2 > kMaxCo2Ppm)
  {
    st = Status::OUT_OF_RANGE; // Impossible value (or "not a number").
  }
  else if (co2 == 0.0f)
  {
    st = Status::NOT_PRESENT; // Exactly 0 ppm = the sensor is still warming up and has no real reading yet.
  }

  out[0] = makeSample(ParamID::CO2, co2, st, MET_s);
  return kSampleCount;
}