#include <unity.h>
#include <string.h>
#include "i2c_bus.h"
#include "storage.h"
#include "services/mission_clock.h"

// ---------- Fakes ----------

// A pretend PCF8523 (same behaviour as in test_pcf8523), plus a helper to set its time.
class FakeRtcBus : public I2CBus
{
  public:
  uint8_t regs[32] = {};
  bool connected = true;

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
  Status write(uint8_t, const uint8_t*, size_t) override { return Status::BUS_ERROR; }
  Status read(uint8_t, uint8_t*, size_t) override { return Status::BUS_ERROR; }

  void set(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s, bool stopped = false)
  {
    auto bcd = [](uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); };
    regs[0x03] = static_cast<uint8_t>(bcd(s) | (stopped ? 0x80 : 0x00)); // Bit 7 = "oscillator stopped".
    regs[0x04] = bcd(mi);
    regs[0x05] = bcd(h);
    regs[0x06] = bcd(d);
    regs[0x08] = bcd(mo);
    regs[0x09] = bcd(static_cast<uint8_t>(y - 2000));
  }
};

// A pretend card holding one small file (enough for the mission file).
class FakeStorage : public Storage
{
  public:
  bool cardPresent = true;
  char path[Storage::kMaxPathLength + 1] = {};
  uint8_t data[128] = {};
  size_t size = 0;
  bool hasFile = false;
  int writes = 0;

  Status begin() override { return cardPresent ? Status::OK : Status::NOT_PRESENT; }
  bool exists(const char* p) override { return cardPresent && hasFile && strcmp(p, path) == 0; }
  Status append(const char*, const uint8_t*, size_t) override { return Status::BUS_ERROR; } // Not used by the clock.
  Status writeFile(const char* p, const uint8_t* d, size_t len) override
  {
    if (!cardPresent) return Status::NOT_PRESENT;
    if (!isValidPath(p) || len > sizeof(data)) return Status::OUT_OF_RANGE;
    strncpy(path, p, sizeof(path) - 1);
    memcpy(data, d, len);
    size = len;
    hasFile = true;
    writes++;
    return Status::OK;
  }
  Status readFile(const char* p, uint8_t* out, size_t capacity, size_t& got) override
  {
    got = 0;
    if (!exists(p)) return Status::NOT_PRESENT;
    if (size > capacity) return Status::OUT_OF_RANGE;
    memcpy(out, data, size);
    got = size;
    return Status::OK;
  }
  Status freeBytes(uint64_t& bytes) override { bytes = 1000; return Status::OK; }

  const char* text() { data[size < sizeof(data) ? size : sizeof(data) - 1] = 0; return reinterpret_cast<const char*>(data); }
};

static uint32_t g_millis = 0;
static uint32_t fakeMillis() { return g_millis; }
static int asInt(Status s) { return static_cast<int>(s); }

void setUp() { g_millis = 0; }
void tearDown() {}

// ---------- Tests ----------

void test_first_boot_starts_mission_at_zero()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  MissionClock clock(rtc, card, fakeMillis);

  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(clock.begin()));
  TEST_ASSERT_EQUAL_UINT32(0, clock.now());
  TEST_ASSERT_TRUE(clock.source() == ClockSource::RTC);
  TEST_ASSERT_TRUE(card.hasFile); // Mission start saved immediately.
  TEST_ASSERT_NOT_NULL(strstr(card.text(), "met=0"));
}

void test_met_follows_the_rtc()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  MissionClock clock(rtc, card, fakeMillis);
  clock.begin();

  rtcBus.set(2026, 11, 2, 21, 0, 30); // One day and 30 s later (across midnight).
  TEST_ASSERT_EQUAL_UINT32(86430, clock.now());
}

void test_met_survives_reboot()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card; // The card keeps its contents across the "reboot".
  {
    MissionClock firstBoot(rtc, card, fakeMillis);
    firstBoot.begin();
  }
  rtcBus.set(2026, 11, 10, 9, 0, 0); // The board reboots 8.5 days later.
  g_millis = 0; // millis() restarts at 0 after a reboot.
  MissionClock secondBoot(rtc, card, fakeMillis);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(secondBoot.begin()));
  TEST_ASSERT_EQUAL_UINT32(8u * 86400u + 12u * 3600u, secondBoot.now()); // Not 0.
  TEST_ASSERT_TRUE(secondBoot.source() == ClockSource::RTC);
}

void test_lost_rtc_time_falls_back_to_estimate()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  {
    MissionClock firstBoot(rtc, card, fakeMillis);
    firstBoot.begin();
    rtcBus.set(2026, 11, 1, 22, 0, 0);
    firstBoot.now(); // MET 3600: checkpoint saved (more than 60 s since the last save).
  }
  rtcBus.set(2000, 1, 1, 0, 0, 5, true); // Power was lost with no coin cell: time reset, "stopped" flag set.
  g_millis = 0;
  MissionClock secondBoot(rtc, card, fakeMillis);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(secondBoot.begin())); // Runs, but warns MET may be short.
  TEST_ASSERT_EQUAL_UINT32(3600, secondBoot.now()); // Continues from the last checkpoint...
  g_millis = 10000;
  TEST_ASSERT_EQUAL_UINT32(3610, secondBoot.now()); // ...counting with the processor's own timer.
  TEST_ASSERT_TRUE(secondBoot.source() == ClockSource::ESTIMATED);
}

void test_rtc_dropout_mid_mission_keeps_counting()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  MissionClock clock(rtc, card, fakeMillis);
  clock.begin();
  rtcBus.set(2026, 11, 1, 21, 0, 20);
  g_millis = 20000;
  TEST_ASSERT_EQUAL_UINT32(20, clock.now());

  rtcBus.connected = false; // The RTC stops answering.
  g_millis = 25000;
  TEST_ASSERT_EQUAL_UINT32(25, clock.now()); // Estimated from the last good reading.
  TEST_ASSERT_TRUE(clock.source() == ClockSource::ESTIMATED);

  rtcBus.connected = true; // It comes back.
  rtcBus.set(2026, 11, 1, 21, 0, 30);
  g_millis = 30000;
  TEST_ASSERT_EQUAL_UINT32(30, clock.now());
  TEST_ASSERT_TRUE(clock.source() == ClockSource::RTC);
}

void test_met_never_goes_backwards()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  MissionClock clock(rtc, card, fakeMillis);
  clock.begin();
  rtcBus.set(2026, 11, 1, 21, 10, 0);
  TEST_ASSERT_EQUAL_UINT32(600, clock.now());
  rtcBus.set(2026, 11, 1, 21, 5, 0); // Someone sets the RTC back 5 minutes.
  TEST_ASSERT_EQUAL_UINT32(600, clock.now()); // Holds at 600 instead of jumping back to 300.
}

void test_checkpoint_at_most_once_a_minute()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  MissionClock clock(rtc, card, fakeMillis);
  clock.begin();
  int afterBegin = card.writes;
  for (uint8_t s = 2; s < 60; s += 2)
  {
    rtcBus.set(2026, 11, 1, 21, 0, s);
    clock.now(); // 29 calls within the first minute...
  }
  TEST_ASSERT_EQUAL_INT(afterBegin, card.writes); // ...no extra writes.
  rtcBus.set(2026, 11, 1, 21, 1, 0);
  clock.now(); // MET 60.
  TEST_ASSERT_EQUAL_INT(afterBegin + 1, card.writes);
  TEST_ASSERT_NOT_NULL(strstr(card.text(), "met=60"));
}

void test_no_card_still_keeps_time()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  card.cardPresent = false;
  MissionClock clock(rtc, card, fakeMillis);
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(clock.begin())); // Reports the problem...
  rtcBus.set(2026, 11, 1, 21, 2, 0);
  TEST_ASSERT_EQUAL_UINT32(120, clock.now()); // ...but MET still works for this boot.
}

void test_corrupt_file_starts_new_mission()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  const char junk[] = "garbage";
  card.writeFile(MissionClock::kMissionFile, reinterpret_cast<const uint8_t*>(junk), sizeof(junk) - 1);
  MissionClock clock(rtc, card, fakeMillis);
  clock.begin();
  TEST_ASSERT_EQUAL_UINT32(0, clock.now());
  TEST_ASSERT_NOT_NULL(strstr(card.text(), "start="));
}

void test_reset_mission_returns_to_zero()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2026, 11, 1, 21, 0, 0);
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  MissionClock clock(rtc, card, fakeMillis);
  clock.begin();
  rtcBus.set(2026, 11, 3, 21, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(2u * 86400u, clock.now());
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(clock.resetMission()));
  TEST_ASSERT_EQUAL_UINT32(0, clock.now());
  rtcBus.set(2026, 11, 3, 21, 0, 45);
  TEST_ASSERT_EQUAL_UINT32(45, clock.now());
}

void test_full_15_day_mission()
{
  FakeRtcBus rtcBus;
  rtcBus.set(2028, 2, 20, 12, 0, 0); // Crosses 29 Feb 2028.
  PCF8523 rtc(rtcBus);
  FakeStorage card;
  MissionClock clock(rtc, card, fakeMillis);
  clock.begin();
  rtcBus.set(2028, 3, 6, 12, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(15u * 86400u, clock.now()); // DR 5.0: 15 days = 1 296 000 s.
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_first_boot_starts_mission_at_zero);
  RUN_TEST(test_met_follows_the_rtc);
  RUN_TEST(test_met_survives_reboot);
  RUN_TEST(test_lost_rtc_time_falls_back_to_estimate);
  RUN_TEST(test_rtc_dropout_mid_mission_keeps_counting);
  RUN_TEST(test_met_never_goes_backwards);
  RUN_TEST(test_checkpoint_at_most_once_a_minute);
  RUN_TEST(test_no_card_still_keeps_time);
  RUN_TEST(test_corrupt_file_starts_new_mission);
  RUN_TEST(test_reset_mission_returns_to_zero);
  RUN_TEST(test_full_15_day_mission);
  return UNITY_END();
}