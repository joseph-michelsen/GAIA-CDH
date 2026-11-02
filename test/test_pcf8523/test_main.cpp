#include <unity.h>
#include <string.h>
#include "i2c_bus.h"
#include "drivers/pcf8523.h"

// A pretend PCF8523: 32 one-byte registers that auto-increment on reads and writes, like the real chip.

class FakeRtcBus : public I2CBus
{
  public:
  uint8_t regs[32] = {};
  bool connected = true;

  FakeRtcBus() { regs[0x02] = 0xE0; regs[0x03] = 0x80; } // Power-on state: backup battery disabled, "oscillator stopped" flag set.

  bool probe(uint8_t addr) override { return connected && addr == 0x68; }

  Status readRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) override
  {
    if (!connected || addr != 0x68 || reg + len > 32) return Status::BUS_ERROR;
    memcpy(out, &regs[reg], len);
    return Status::OK;
  }

  Status writeRegs(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) override
  {
    if (!connected || addr != 0x68 || reg + len > 32) return Status::BUS_ERROR;
    memcpy(&regs[reg], data, len);
    return Status::OK;
  }

  Status write(uint8_t, const uint8_t*, size_t) override { return Status::BUS_ERROR; } // The PCF8523 never uses plain transfers.
  Status read(uint8_t, uint8_t*, size_t) override { return Status::BUS_ERROR; }
};

static int asInt(Status s) { return static_cast<int>(s); }

static RtcTime makeTime(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s)
{
  RtcTime t;
  t.year = y; t.month = mo; t.day = d; t.hour = h; t.minute = mi; t.second = s;
  return t;
}

static void loadTime(FakeRtcBus& bus) // 2026-10-29 14:30:45, clock trustworthy.
{
  const uint8_t t[7] = {0x45, 0x30, 0x14, 0x29, 0x04, 0x10, 0x26};
  memcpy(&bus.regs[0x03], t, 7);
}

void setUp() {}
void tearDown() {}

// ---------- Driver ----------

void test_begin_enables_backup_battery()
{
  FakeRtcBus bus;
  PCF8523 rtc(bus);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(rtc.begin()));
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.regs[0x02] & 0xE0); // PM bits now 000: coin cell takes over when power drops.
}

void test_begin_restarts_stopped_clock()
{
  FakeRtcBus bus;
  bus.regs[0x00] = 0x20; // STOP bit set.
  PCF8523 rtc(bus);
  rtc.begin();
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.regs[0x00] & 0x20);
}

void test_missing_chip_reports_not_present()
{
  FakeRtcBus bus;
  bus.connected = false;
  PCF8523 rtc(bus);
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(rtc.begin()));
}

void test_reads_bcd_time()
{
  FakeRtcBus bus;
  loadTime(bus);
  PCF8523 rtc(bus);
  RtcTime t;
  bool ok = false;
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(rtc.readTime(t, ok)));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT16(2026, t.year);
  TEST_ASSERT_EQUAL_UINT8(10, t.month);
  TEST_ASSERT_EQUAL_UINT8(29, t.day);
  TEST_ASSERT_EQUAL_UINT8(14, t.hour);
  TEST_ASSERT_EQUAL_UINT8(30, t.minute);
  TEST_ASSERT_EQUAL_UINT8(45, t.second);
}

void test_lost_time_is_flagged()
{
  FakeRtcBus bus;
  loadTime(bus);
  bus.regs[0x03] |= 0x80; // "Oscillator stopped" flag.
  PCF8523 rtc(bus);
  RtcTime t;
  bool ok = true;
  rtc.readTime(t, ok);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT8(45, t.second); // Flag bit is not mixed into the seconds.
}

void test_set_time_writes_bcd_and_clears_flag()
{
  FakeRtcBus bus;
  PCF8523 rtc(bus);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(rtc.setTime(makeTime(2026, 10, 29, 15, 4, 5))));
  TEST_ASSERT_EQUAL_HEX8(0x05, bus.regs[0x03]); // Seconds, OS flag cleared.
  TEST_ASSERT_EQUAL_HEX8(0x04, bus.regs[0x04]);
  TEST_ASSERT_EQUAL_HEX8(0x15, bus.regs[0x05]);
  TEST_ASSERT_EQUAL_HEX8(0x29, bus.regs[0x06]);
  TEST_ASSERT_EQUAL_HEX8(0x10, bus.regs[0x08]);
  TEST_ASSERT_EQUAL_HEX8(0x26, bus.regs[0x09]);
}

void test_set_time_rejects_impossible_time()
{
  FakeRtcBus bus;
  PCF8523 rtc(bus);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(rtc.setTime(makeTime(2026, 2, 30, 0, 0, 0)))); // 30 February.
  TEST_ASSERT_EQUAL_HEX8(0x80, bus.regs[0x03]); // Nothing was written.
}

void test_garbage_registers_flagged()
{
  FakeRtcBus bus;
  loadTime(bus);
  bus.regs[0x04] = 0x7A; // Minutes = "7A": not a real value.
  PCF8523 rtc(bus);
  RtcTime t;
  bool ok = false;
  TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(rtc.readTime(t, ok)));
}

void test_lost_bus_marks_bus_error()
{
  FakeRtcBus bus;
  PCF8523 rtc(bus);
  bus.connected = false;
  RtcTime t;
  bool ok = false;
  TEST_ASSERT_EQUAL_INT(asInt(Status::BUS_ERROR), asInt(rtc.readTime(t, ok)));
}

// ---------- Time helpers ----------

void test_valid_time_knows_month_lengths()
{
  TEST_ASSERT_TRUE(isValidTime(makeTime(2028, 2, 29, 0, 0, 0))); // 2028 is a leap year.
  TEST_ASSERT_FALSE(isValidTime(makeTime(2026, 2, 29, 0, 0, 0))); // 2026 is not.
  TEST_ASSERT_FALSE(isValidTime(makeTime(2026, 4, 31, 0, 0, 0))); // April has 30 days.
  TEST_ASSERT_TRUE(isValidTime(makeTime(2026, 12, 31, 23, 59, 59)));
  TEST_ASSERT_FALSE(isValidTime(makeTime(2026, 13, 1, 0, 0, 0)));
}

void test_seconds_since_2000_known_values()
{
  TEST_ASSERT_EQUAL_UINT32(0, toSeconds2000(makeTime(2000, 1, 1, 0, 0, 0)));
  TEST_ASSERT_EQUAL_UINT32(86400u * 366u, toSeconds2000(makeTime(2001, 1, 1, 0, 0, 0))); // 2000 was a leap year.
  // 2026-11-01 20:54:05 = Unix time 1793566445 minus Unix time of 2000-01-01 (946684800).
  TEST_ASSERT_EQUAL_UINT32(1793566445u - 946684800u, toSeconds2000(makeTime(2026, 11, 1, 20, 54, 5)));
}

void test_elapsed_time_across_month_and_leap_day()
{
  // 15-day mission starting 2028-02-20 12:00:00 ends 2028-03-06 12:00:00 (crosses 29 Feb 2028).
  uint32_t start = toSeconds2000(makeTime(2028, 2, 20, 12, 0, 0));
  uint32_t end = toSeconds2000(makeTime(2028, 3, 6, 12, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(15u * 86400u, end - start);
}

void test_parses_build_time()
{
  RtcTime t;
  TEST_ASSERT_TRUE(parseBuildTime("Oct 29 2026", "15:04:05", t));
  TEST_ASSERT_EQUAL_UINT16(2026, t.year);
  TEST_ASSERT_EQUAL_UINT8(10, t.month);
  TEST_ASSERT_EQUAL_UINT8(29, t.day);
  TEST_ASSERT_EQUAL_UINT8(15, t.hour);
  TEST_ASSERT_EQUAL_UINT8(4, t.minute);
  TEST_ASSERT_EQUAL_UINT8(5, t.second);
  TEST_ASSERT_TRUE(parseBuildTime("Nov  1 2026", "00:00:00", t)); // Space-padded day.
  TEST_ASSERT_EQUAL_UINT8(1, t.day);
  TEST_ASSERT_FALSE(parseBuildTime("Foo 05 2026", "00:00:00", t));
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_begin_enables_backup_battery);
  RUN_TEST(test_begin_restarts_stopped_clock);
  RUN_TEST(test_missing_chip_reports_not_present);
  RUN_TEST(test_reads_bcd_time);
  RUN_TEST(test_lost_time_is_flagged);
  RUN_TEST(test_set_time_writes_bcd_and_clears_flag);
  RUN_TEST(test_set_time_rejects_impossible_time);
  RUN_TEST(test_garbage_registers_flagged);
  RUN_TEST(test_lost_bus_marks_bus_error);
  RUN_TEST(test_valid_time_knows_month_lengths);
  RUN_TEST(test_seconds_since_2000_known_values);
  RUN_TEST(test_elapsed_time_across_month_and_leap_day);
  RUN_TEST(test_parses_build_time);
  return UNITY_END();
}