#include "bme680.h" // Include the header file that defines the BME680 class for the BME680 environmental sensor.

// Register addresses, values and formulas follow the Bosch BME680 datasheet and Bosch's official
// BME68x Sensor API (github.com/boschsensortec/BME68x_SensorAPI), floating-point version.
namespace
{
constexpr uint8_t kChipId = 0x61; // Value the chip-ID register always holds on a BME680 (and BME688).

constexpr uint8_t kRegChipId = 0xD0; // Chip identification register.
constexpr uint8_t kRegSoftReset = 0xE0; // Writing kSoftResetCmd here resets the chip to its power-on state.
constexpr uint8_t kSoftResetCmd = 0xB6; // The "please reset" command value.
constexpr uint8_t kRegCtrlHum = 0x72; // Humidity oversampling setting (bits 2..0).
constexpr uint8_t kRegCtrlMeas = 0x74; // Temperature oversampling (bits 7..5), pressure oversampling (bits 4..2), mode (bits 1..0).
constexpr uint8_t kRegConfig = 0x75; // IIR filter setting (bits 4..2).
constexpr uint8_t kRegField = 0x1D; // First register of the measurement results (status byte, then pressure, temperature, humidity).
constexpr uint8_t kRegCalib1 = 0x8A; // First block of calibration coefficients (23 bytes, 0x8A..0xA0).
constexpr uint8_t kRegCalib2 = 0xE1; // Second block of calibration coefficients (14 bytes, 0xE1..0xEE).
constexpr size_t kLenCalib1 = 23; // Length of the first calibration block.
constexpr size_t kLenCalib2 = 14; // Length of the second calibration block.
constexpr size_t kLenField = 10; // Status + 3 pressure + 3 temperature + 2 humidity bytes, plus one index byte (0x1D..0x26).

constexpr uint8_t kNewDataBit = 0x80; // Bit 7 of the status byte: "a new measurement is ready".
constexpr uint8_t kModeSleep = 0x00; // Sleep mode: chip idle, lowest power.
constexpr uint8_t kModeForced = 0x01; // Forced mode: take ONE measurement, then go back to sleep automatically.

// Oversampling codes: 1 = x1, 2 = x2, 3 = x4, 4 = x8, 5 = x16. Same settings as Bosch's own forced-mode example.
constexpr uint8_t kOsTemp = 2; // Temperature x2.
constexpr uint8_t kOsPres = 1; // Pressure x1.
constexpr uint8_t kOsHum = 5; // Humidity x16.
constexpr uint8_t kFilterOff = 0; // IIR filter off (the filter smooths fast changes; greenhouse air changes slowly).
constexpr uint8_t kCtrlMeasBase = static_cast<uint8_t>((kOsTemp << 5) | (kOsPres << 2)); // ctrl_meas value without the mode bits.

// How long one forced measurement takes, from Bosch's bme68x_get_meas_dur() formula, in microseconds:
// 1963 us per oversampling cycle + 477*4 us switching + 477*5 us (gas slot) + 1000 us wake-up.
constexpr uint32_t kCycles = 2 + 1 + 16; // Oversampling cycles for x2 temperature, x1 pressure, x16 humidity.
constexpr uint32_t kMeasureUs = kCycles * 1963u + 477u * 4u + 477u * 5u + 1000u; // = 42 590 us.
constexpr uint32_t kMeasureMs = (kMeasureUs + 999u) / 1000u; // Rounded UP to whole milliseconds = 43 ms.
constexpr uint32_t kResetMs = 10; // Wait after a soft reset (Bosch reference driver uses 10 ms).
constexpr uint32_t kPollMs = 10; // Wait between "is it done yet?" checks (Bosch reference driver uses 10 ms).
constexpr int kPollTries = 5; // How many times to check before giving up (Bosch reference driver uses 5).

// Plausibility limits = the sensor's specified operating range (Bosch BME680 datasheet).
constexpr float kMinTempC = -40.0f; // Lowest temperature the sensor is specified for.
constexpr float kMaxTempC = 85.0f; // Highest temperature the sensor is specified for.
constexpr float kMinPressurePa = 30000.0f; // 300 hPa, lowest pressure the sensor is specified for.
constexpr float kMaxPressurePa = 110000.0f; // 1100 hPa, highest pressure the sensor is specified for.

// Positions of each coefficient inside the combined 37-byte calibration buffer (same indices as Bosch's driver).
constexpr int kIdxT2Lsb = 0, kIdxT2Msb = 1, kIdxT3 = 2;
constexpr int kIdxP1Lsb = 4, kIdxP1Msb = 5, kIdxP2Lsb = 6, kIdxP2Msb = 7, kIdxP3 = 8;
constexpr int kIdxP4Lsb = 10, kIdxP4Msb = 11, kIdxP5Lsb = 12, kIdxP5Msb = 13, kIdxP7 = 14, kIdxP6 = 15;
constexpr int kIdxP8Lsb = 18, kIdxP8Msb = 19, kIdxP9Lsb = 20, kIdxP9Msb = 21, kIdxP10 = 22;
constexpr int kIdxH2Msb = 23, kIdxH1H2Lsb = 24, kIdxH1Msb = 25, kIdxH3 = 26, kIdxH4 = 27, kIdxH5 = 28, kIdxH6 = 29, kIdxH7 = 30;
constexpr int kIdxT1Lsb = 31, kIdxT1Msb = 32;

uint16_t join(uint8_t msb, uint8_t lsb) // Combine two bytes (most significant first) into one 16-bit number.
{
  return static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8) | lsb);
}
}

Status BME680::writeReg(uint8_t reg, uint8_t value)
{
  return bus_.writeRegs(address_, reg, &value, 1); // Always one register per write: the BME680 expects register/value PAIRS on I2C, not a run of values.
}

Status BME680::readCalibration()
{
  uint8_t c[kLenCalib1 + kLenCalib2] = {}; // One buffer for both calibration blocks, laid out the same way as Bosch's driver.

  Status s = bus_.readRegs(address_, kRegCalib1, c, kLenCalib1); // Read block 1 (0x8A..0xA0).
  if (s != Status::OK)
  {
    return s; // Stop if the bus failed.
  }

  s = bus_.readRegs(address_, kRegCalib2, c + kLenCalib1, kLenCalib2); // Read block 2 (0xE1..0xEE) into the rest of the buffer.
  if (s != Status::OK)
  {
    return s; // Stop if the bus failed.
  }

  cal_.t1 = join(c[kIdxT1Msb], c[kIdxT1Lsb]); // Unsigned 16-bit.
  cal_.t2 = static_cast<int16_t>(join(c[kIdxT2Msb], c[kIdxT2Lsb])); // Signed 16-bit.
  cal_.t3 = static_cast<int8_t>(c[kIdxT3]); // Signed 8-bit.

  cal_.p1 = join(c[kIdxP1Msb], c[kIdxP1Lsb]);
  cal_.p2 = static_cast<int16_t>(join(c[kIdxP2Msb], c[kIdxP2Lsb]));
  cal_.p3 = static_cast<int8_t>(c[kIdxP3]);
  cal_.p4 = static_cast<int16_t>(join(c[kIdxP4Msb], c[kIdxP4Lsb]));
  cal_.p5 = static_cast<int16_t>(join(c[kIdxP5Msb], c[kIdxP5Lsb]));
  cal_.p6 = static_cast<int8_t>(c[kIdxP6]);
  cal_.p7 = static_cast<int8_t>(c[kIdxP7]);
  cal_.p8 = static_cast<int16_t>(join(c[kIdxP8Msb], c[kIdxP8Lsb]));
  cal_.p9 = static_cast<int16_t>(join(c[kIdxP9Msb], c[kIdxP9Lsb]));
  cal_.p10 = c[kIdxP10];

  // h1 and h2 are 12-bit values that SHARE register 0xE2: h1 uses its low 4 bits, h2 uses its high 4 bits.
  cal_.h1 = static_cast<uint16_t>((static_cast<uint16_t>(c[kIdxH1Msb]) << 4) | (c[kIdxH1H2Lsb] & 0x0F));
  cal_.h2 = static_cast<uint16_t>((static_cast<uint16_t>(c[kIdxH2Msb]) << 4) | (c[kIdxH1H2Lsb] >> 4));
  cal_.h3 = static_cast<int8_t>(c[kIdxH3]);
  cal_.h4 = static_cast<int8_t>(c[kIdxH4]);
  cal_.h5 = static_cast<int8_t>(c[kIdxH5]);
  cal_.h6 = c[kIdxH6];
  cal_.h7 = static_cast<int8_t>(c[kIdxH7]);

  return Status::OK; // Calibration loaded.
}

Status BME680::configure()
{
  Status s = writeReg(kRegCtrlHum, kOsHum); // Humidity oversampling. (Only takes effect after the next ctrl_meas write, which comes below.)
  if (s == Status::OK)
  {
    s = writeReg(kRegConfig, static_cast<uint8_t>(kFilterOff << 2)); // IIR filter off.
  }
  if (s == Status::OK)
  {
    s = writeReg(kRegCtrlMeas, static_cast<uint8_t>(kCtrlMeasBase | kModeSleep)); // Temperature + pressure oversampling, stay asleep for now.
  }
  return s; // OK if all three writes worked, otherwise the first failure.
}

bool BME680::stillConfigured()
{
  uint8_t ctrlHum = 0; // Holds the humidity-oversampling register as read back from the chip.
  if (bus_.readRegs(address_, kRegCtrlHum, &ctrlHum, 1) != Status::OK)
  {
    return false; // Can't tell, so treat it as not configured.
  }
  return (ctrlHum & 0x07) == kOsHum; // After a power loss this register resets to 0, so a mismatch means "the chip forgot our settings".
}

Status BME680::begin()
{
  present_ = false; // Assume nothing until every step below succeeds.

  uint8_t id = 0; // Holds the chip ID read from the sensor.
  Status s = bus_.readRegs(address_, kRegChipId, &id, 1); // Read the chip-ID register.
  if (s != Status::OK || id != kChipId)
  {
    return Status::NOT_PRESENT; // Nothing answered, or something that isn't a BME680 did.
  }

  s = writeReg(kRegSoftReset, kSoftResetCmd); // Reset to a known state (clears any settings left over from before a reboot).
  if (s != Status::OK)
  {
    return s; // The chip was there a moment ago, so this is a bus problem.
  }
  if (delayMs_ != nullptr)
  {
    delayMs_(kResetMs); // Give the chip time to finish resetting.
  }

  s = readCalibration(); // Load this chip's factory calibration coefficients.
  if (s != Status::OK)
  {
    return s;
  }

  s = configure(); // Apply our oversampling and filter settings.
  if (s != Status::OK)
  {
    return s;
  }

  present_ = true; // Chip identified, calibrated and configured.
  return Status::OK;
}

void BME680::fillAll(Sample* out, Status status, uint32_t MET_s)
{
  out[0] = makeSample(ParamID::AIR_TEMP, 0.0f, status, MET_s); // Temperature slot.
  out[1] = makeSample(ParamID::HUMIDITY, 0.0f, status, MET_s); // Humidity slot.
  out[2] = makeSample(ParamID::PRESSURE, 0.0f, status, MET_s); // Pressure slot.
}

size_t BME680::read(Sample* out, size_t capacity, uint32_t MET_s)
{
  if (out == nullptr || capacity < kSampleCount)
  {
    return 0; // No room for the 3 samples: write nothing.
  }

  if (present_ && !stillConfigured())
  {
    present_ = false; // The chip lost power (or was swapped) since begin(): set it up again below.
  }

  if (!present_)
  {
    Status s = begin(); // (Re)identify, reset, calibrate and configure.
    if (s != Status::OK)
    {
      fillAll(out, s, MET_s); // Report WHY there's no data (NOT_PRESENT or BUS_ERROR).
      return kSampleCount;
    }
  }

  if (writeReg(kRegCtrlMeas, static_cast<uint8_t>(kCtrlMeasBase | kModeForced)) != Status::OK) // Start ONE measurement.
  {
    present_ = false; // Start fresh next time.
    fillAll(out, Status::BUS_ERROR, MET_s);
    return kSampleCount;
  }

  if (delayMs_ != nullptr)
  {
    delayMs_(kMeasureMs); // Wait for the measurement to finish (about 43 ms with our settings).
  }

  uint8_t f[kLenField] = {}; // Holds the status byte and raw results, read in ONE burst so they all belong to the same measurement.
  bool ready = false; // Becomes true when the chip says a new measurement is available.
  for (int attempt = 0; attempt < kPollTries; ++attempt)
  {
    if (bus_.readRegs(address_, kRegField, f, kLenField) != Status::OK)
    {
      present_ = false; // Start fresh next time.
      fillAll(out, Status::BUS_ERROR, MET_s);
      return kSampleCount;
    }
    if (f[0] & kNewDataBit)
    {
      ready = true; // Results are in.
      break;
    }
    if (delayMs_ != nullptr)
    {
      delayMs_(kPollMs); // Not done yet: wait a little and check again.
    }
  }

  if (!ready)
  {
    present_ = false; // The chip never finished: treat it like a device that stopped responding.
    fillAll(out, Status::BUS_ERROR, MET_s);
    return kSampleCount;
  }

  // Unpack the raw readings: pressure and temperature are 20 bits (MSB, LSB, top 4 bits of XLSB); humidity is 16 bits.
  uint32_t adcPres = (static_cast<uint32_t>(f[2]) << 12) | (static_cast<uint32_t>(f[3]) << 4) | (f[4] >> 4);
  uint32_t adcTemp = (static_cast<uint32_t>(f[5]) << 12) | (static_cast<uint32_t>(f[6]) << 4) | (f[7] >> 4);
  uint16_t adcHum = join(f[8], f[9]);

  float tFine = 0.0f; // Bosch's intermediate "fine temperature", shared with the pressure formula.
  float tempC = compensateTemperature(adcTemp, tFine); // Raw -> degrees C.
  float pressurePa = compensatePressure(adcPres, tFine); // Raw -> Pa (temperature-corrected).
  float humidity = compensateHumidity(adcHum, tempC); // Raw -> %RH (temperature-corrected).

  Status st = Status::OK; // Temperature status.
  Status sh = Status::OK; // Humidity status.
  Status sp = Status::OK; // Pressure status.

  if (tempC < kMinTempC || tempC > kMaxTempC)
  {
    st = sh = sp = Status::OUT_OF_RANGE; // Pressure and humidity are corrected USING temperature, so a bad temperature taints all three.
  }
  else if (pressurePa < kMinPressurePa || pressurePa > kMaxPressurePa)
  {
    sp = Status::OUT_OF_RANGE; // Only pressure is affected.
  }

  out[0] = makeSample(ParamID::AIR_TEMP, tempC, st, MET_s);
  out[1] = makeSample(ParamID::HUMIDITY, humidity, sh, MET_s);
  out[2] = makeSample(ParamID::PRESSURE, pressurePa, sp, MET_s);
  return kSampleCount; // Wrote 3 samples.
}

// ---- Compensation formulas: floating-point versions from Bosch's BME68x Sensor API (calc_temperature/pressure/humidity) ----

float BME680::compensateTemperature(uint32_t adc, float& t_fine) const
{
  float var1 = ((static_cast<float>(adc) / 16384.0f) - (static_cast<float>(cal_.t1) / 1024.0f)) * static_cast<float>(cal_.t2);
  float d = (static_cast<float>(adc) / 131072.0f) - (static_cast<float>(cal_.t1) / 8192.0f);
  float var2 = d * d * (static_cast<float>(cal_.t3) * 16.0f);
  t_fine = var1 + var2; // Saved for the pressure formula.
  return t_fine / 5120.0f; // Degrees C.
}

float BME680::compensatePressure(uint32_t adc, float t_fine) const
{
  float var1 = (t_fine / 2.0f) - 64000.0f;
  float var2 = var1 * var1 * (static_cast<float>(cal_.p6) / 131072.0f);
  var2 = var2 + (var1 * static_cast<float>(cal_.p5) * 2.0f);
  var2 = (var2 / 4.0f) + (static_cast<float>(cal_.p4) * 65536.0f);
  var1 = (((static_cast<float>(cal_.p3) * var1 * var1) / 16384.0f) + (static_cast<float>(cal_.p2) * var1)) / 524288.0f;
  var1 = (1.0f + (var1 / 32768.0f)) * static_cast<float>(cal_.p1);

  if (static_cast<int>(var1) == 0)
  {
    return 0.0f; // Avoid dividing by zero (only happens with corrupt calibration); 0 Pa then fails the range check.
  }

  float p = 1048576.0f - static_cast<float>(adc);
  p = ((p - (var2 / 4096.0f)) * 6250.0f) / var1;
  var1 = (static_cast<float>(cal_.p9) * p * p) / 2147483648.0f;
  var2 = p * (static_cast<float>(cal_.p8) / 32768.0f);
  float var3 = (p / 256.0f) * (p / 256.0f) * (p / 256.0f) * (static_cast<float>(cal_.p10) / 131072.0f);
  return p + (var1 + var2 + var3 + (static_cast<float>(cal_.p7) * 128.0f)) / 16.0f; // Pascals.
}

float BME680::compensateHumidity(uint16_t adc, float tempC) const
{
  float var1 = static_cast<float>(adc) - ((static_cast<float>(cal_.h1) * 16.0f) + ((static_cast<float>(cal_.h3) / 2.0f) * tempC));
  float var2 = var1 * ((static_cast<float>(cal_.h2) / 262144.0f) *
                       (1.0f + ((static_cast<float>(cal_.h4) / 16384.0f) * tempC) +
                        ((static_cast<float>(cal_.h5) / 1048576.0f) * tempC * tempC)));
  float var3 = static_cast<float>(cal_.h6) / 16384.0f;
  float var4 = static_cast<float>(cal_.h7) / 2097152.0f;
  float h = var2 + ((var3 + (var4 * tempC)) * var2 * var2);

  if (h > 100.0f)
  {
    h = 100.0f; // Clamp to the physical range, exactly as Bosch's reference driver does.
  }
  else if (h < 0.0f)
  {
    h = 0.0f;
  }
  return h; // Percent relative humidity.
}