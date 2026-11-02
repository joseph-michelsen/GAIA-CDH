#include "pcf8523.h" // Include the header file that defines the PCF8523 class for the PCF8523 real-time clock.

// Register addresses and bit meanings follow the NXP PCF8523 datasheet.

namespace
{
constexpr uint8_t kRegControl1 = 0x00; // Control_1: bit 5 = STOP (1 freezes the clock).
constexpr uint8_t kRegControl3 = 0x02; // Control_3: bits 7..5 = PM, the backup-battery (power management) setting.
constexpr uint8_t kRegSeconds = 0x03; // First of 7 time registers: seconds, minutes, hours, days, weekdays, months, years.

constexpr uint8_t kStopBit = 0x20; // Control_1 bit 5.
constexpr uint8_t kPmMask = 0xE0; // Control_3 bits 7..5. Power-on default is 111 = battery switch-over DISABLED.
constexpr uint8_t kPmStandard = 0x00; // 000 = switch to the coin cell when main power drops (standard mode), with battery-low detection.
constexpr uint8_t kOsFlag = 0x80; // Seconds bit 7, "oscillator stopped": 1 means the time can't be trusted.

constexpr uint8_t kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; // February gets +1 in leap years.

uint8_t fromBcd(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); } // Binary-coded decimal -> number (0x45 -> 45).
uint8_t toBcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); } // Number -> binary-coded decimal (45 -> 0x45).

bool isLeapYear(uint16_t year) // For 2000-2099 every 4th year is a leap year (2000 is one: divisible by 400).
{
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) // month is 1-12.
{
  return static_cast<uint8_t>(kDaysInMonth[month - 1] + ((month == 2 && isLeapYear(year)) ? 1 : 0));
}
}

bool isValidTime(const RtcTime& t)
{
  if (t.year < 2000 || t.year > 2099 || t.month < 1 || t.month > 12) // The chip stores a 2-digit year, so 2000-2099 only.
  {
    return false;
  }
  return t.day >= 1 && t.day <= daysInMonth(t.year, t.month) && t.hour <= 23 && t.minute <= 59 && t.second <= 59;
}

uint32_t toSeconds2000(const RtcTime& t)
{
  uint32_t days = 0; // Whole days from 2000-01-01 to the start of t's day.
  for (uint16_t y = 2000; y < t.year; ++y)
  {
    days += isLeapYear(y) ? 366u : 365u; // Add each complete year.
  }
  for (uint8_t m = 1; m < t.month; ++m)
  {
    days += daysInMonth(t.year, m); // Add each complete month of this year.
  }
  days += static_cast<uint32_t>(t.day - 1); // Add the complete days of this month.

  return days * 86400u + static_cast<uint32_t>(t.hour) * 3600u + static_cast<uint32_t>(t.minute) * 60u + t.second; // 86 400 s per day.
}

bool parseBuildTime(const char* date, const char* time, RtcTime& out)
{
  // __DATE__ looks like "Nov  1 2026" (day is space-padded); __TIME__ looks like "20:54:05".

  static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  if (date == nullptr || time == nullptr)
  {
    return false;
  }

  RtcTime t;
  t.month = 0;
  for (uint8_t m = 0; m < 12; ++m)
  {
    if (date[0] == kMonths[m * 3] && date[1] == kMonths[m * 3 + 1] && date[2] == kMonths[m * 3 + 2])
    {
      t.month = static_cast<uint8_t>(m + 1); // Found the month name.
    }
  }

  auto digit = [](char c) { return (c >= '0' && c <= '9') ? c - '0' : 0; }; // Character -> number (spaces count as 0).
  t.day = static_cast<uint8_t>(digit(date[4]) * 10 + digit(date[5]));
  t.year = static_cast<uint16_t>(digit(date[7]) * 1000 + digit(date[8]) * 100 + digit(date[9]) * 10 + digit(date[10]));
  t.hour = static_cast<uint8_t>(digit(time[0]) * 10 + digit(time[1]));
  t.minute = static_cast<uint8_t>(digit(time[3]) * 10 + digit(time[4]));
  t.second = static_cast<uint8_t>(digit(time[6]) * 10 + digit(time[7]));

  if (!isValidTime(t))
  {
    return false; // Unrecognised format.
  }
  out = t;
  return true;
}

Status PCF8523::begin()
{
  uint8_t c[3] = {}; // Control_1, Control_2, Control_3.
  if (bus_.readRegs(kAddress, kRegControl1, c, 3) != Status::OK)
  {
    return Status::NOT_PRESENT; // Nothing answered at 0x68. (The PCF8523 has no ID register, so answering is the best check available.)
  }

  if (c[0] & kStopBit)
  {
    uint8_t ctrl1 = static_cast<uint8_t>(c[0] & ~kStopBit); // Same settings, STOP cleared.
    if (bus_.writeRegs(kAddress, kRegControl1, &ctrl1, 1) != Status::OK)
    {
      return Status::BUS_ERROR;
    }
  }

  if ((c[2] & kPmMask) != kPmStandard)
  {
    uint8_t ctrl3 = kPmStandard; // Out of the box the chip IGNORES its coin cell; this turns the backup battery on.
    if (bus_.writeRegs(kAddress, kRegControl3, &ctrl3, 1) != Status::OK)
    {
      return Status::BUS_ERROR;
    }
  }

  return Status::OK;
}

Status PCF8523::readTime(RtcTime& out, bool& trustworthy)
{
  uint8_t r[7] = {}; // Seconds, minutes, hours, days, weekdays, months, years (all BCD).
  Status s = bus_.readRegs(kAddress, kRegSeconds, r, 7); // One burst read, so all fields belong to the same instant.
  if (s != Status::OK)
  {
    return s;
  }

  RtcTime t;
  t.second = fromBcd(r[0] & 0x7F); // Bit 7 is the OS flag, not part of the seconds.
  t.minute = fromBcd(r[1] & 0x7F);
  t.hour = fromBcd(r[2] & 0x3F); // 24-hour mode (the power-on default).
  t.day = fromBcd(r[3] & 0x3F);
  t.month = fromBcd(r[5] & 0x1F); // r[4] is the weekday, which CDH doesn't use.
  t.year = static_cast<uint16_t>(2000 + fromBcd(r[6]));

  if (!isValidTime(t))
  {
    return Status::OUT_OF_RANGE; // Garbage in the time registers.
  }

  out = t;
  trustworthy = (r[0] & kOsFlag) == 0; // OS flag set = the clock stopped at some point (e.g. no coin cell during a power loss).
  return Status::OK;
}

Status PCF8523::setTime(const RtcTime& t)
{
  if (!isValidTime(t))
  {
    return Status::OUT_OF_RANGE; // Refuse to write an impossible time.
  }

  uint8_t r[7] = {
      toBcd(t.second), // Writing seconds with bit 7 = 0 also clears the "oscillator stopped" flag.
      toBcd(t.minute),
      toBcd(t.hour),
      toBcd(t.day),
      0, // Weekday: not used by CDH.
      toBcd(t.month),
      toBcd(static_cast<uint8_t>(t.year - 2000))};
  return bus_.writeRegs(kAddress, kRegSeconds, r, 7); // One burst write (the PCF8523 steps through its registers automatically).
}