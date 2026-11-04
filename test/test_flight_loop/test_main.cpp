#include <unity.h>
#include <string.h>
#include "i2c_bus.h"
#include "storage.h"
#include "sensor.h"
#include "services/mission_clock.h"
#include "services/data_logger.h"
#include "services/averager.h"
#include "services/state_table.h"
#include "app/flight_loop.h"

// ---------- Fakes ----------

// A pretend sensor: reports a fixed value and records how it was called.
class FakeSensor : public Sensor
{
  public:
  ParamID id = ParamID::CO2; // Parameter this sensor reports.
  size_t samplesPerRead = 1; // How many samples each read() writes.
  float value = 1.0f; // Value it reports.
  Status beginStatus = Status::OK; // What begin() returns.
  Status readStatus = Status::OK; // Status of the samples read() writes.
  int begins = 0; // How many times begin() was called.
  int reads = 0; // How many times read() was called.
  uint32_t lastMet = 0xFFFFFFFFu; // MET passed to the latest read().

  const char* name() const override { return "Fake"; }
  Status begin() override { begins++; return beginStatus; }
  size_t read(Sample* out, size_t capacity, uint32_t MET_s) override
  {
    reads++;
    lastMet = MET_s;
    if (out == nullptr || capacity < samplesPerRead) return 0; // Same rule as the real drivers: no room, write nothing.
    for (size_t i = 0; i < samplesPerRead; ++i) out[i] = makeSample(id, value, readStatus, MET_s);
    return samplesPerRead;
  }
};

// A pretend PCF8523 (same as in test_mission_clock), plus a helper to set MET directly.
class FakeRtcBus : public I2CBus
{
  public:
  uint8_t regs[32] = {};

  bool probe(uint8_t addr) override { return addr == 0x68; }
  Status readRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) override
  {
    if (addr != 0x68 || reg + len > 32) return Status::BUS_ERROR;
    memcpy(out, &regs[reg], len);
    return Status::OK;
  }
  Status writeRegs(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) override
  {
    if (addr != 0x68 || reg + len > 32) return Status::BUS_ERROR;
    memcpy(&regs[reg], data, len);
    return Status::OK;
  }
  Status write(uint8_t, const uint8_t*, size_t) override { return Status::BUS_ERROR; }
  Status read(uint8_t, uint8_t*, size_t) override { return Status::BUS_ERROR; }

  void setMet(uint32_t t) // Clock time = 2026-11-01 00:00:00 + t seconds (t under one day), so MET = t after a fresh start at t = 0.
  {
    auto bcd = [](uint32_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); };
    regs[0x03] = bcd(t % 60);
    regs[0x04] = bcd((t / 60) % 60);
    regs[0x05] = bcd(t / 3600);
    regs[0x06] = bcd(1);
    regs[0x08] = bcd(11);
    regs[0x09] = bcd(26);
  }
};

// A pretend card holding a few small files. Supports both what MissionClock uses (writeFile/readFile)
// and what DataLogger uses (exists/append).
class FakeStorage : public Storage
{
  public:
  struct File
  {
    char path[Storage::kMaxPathLength + 1] = {};
    char text[8192] = {};
    size_t size = 0;
    bool used = false;
  };

  File files[4];
  bool cardPresent = true;

  Status begin() override { return cardPresent ? Status::OK : Status::NOT_PRESENT; }
  bool exists(const char* p) override { return cardPresent && find(p) != nullptr; }
  Status append(const char* p, const uint8_t* d, size_t len) override { return put(p, d, len, false); }
  Status writeFile(const char* p, const uint8_t* d, size_t len) override { return put(p, d, len, true); }
  Status readFile(const char* p, uint8_t* out, size_t capacity, size_t& got) override
  {
    got = 0;
    if (!cardPresent) return Status::NOT_PRESENT;
    File* f = find(p);
    if (f == nullptr) return Status::NOT_PRESENT;
    if (f->size > capacity) return Status::OUT_OF_RANGE;
    memcpy(out, f->text, f->size);
    got = f->size;
    return Status::OK;
  }
  Status freeBytes(uint64_t& bytes) override { bytes = 1000; return Status::OK; }

  const char* text(const char* p) { File* f = find(p); return f ? f->text : ""; }

  private:
  File* find(const char* p) { for (auto& f : files) if (f.used && strcmp(f.path, p) == 0) return &f; return nullptr; }
  Status put(const char* p, const uint8_t* d, size_t len, bool replace)
  {
    if (!cardPresent) return Status::NOT_PRESENT;
    if (!isValidPath(p)) return Status::OUT_OF_RANGE;
    File* f = find(p);
    if (f == nullptr)
    {
      for (auto& slot : files) if (!slot.used) { slot.used = true; strncpy(slot.path, p, sizeof(slot.path) - 1); f = &slot; break; }
      if (f == nullptr) return Status::OUT_OF_RANGE;
    }
    if (replace) { f->size = 0; memset(f->text, 0, sizeof(f->text)); }
    if (f->size + len >= sizeof(f->text)) return Status::OUT_OF_RANGE;
    memcpy(f->text + f->size, d, len);
    f->size += len;
    return Status::OK;
  }
};

static uint32_t fakeMillis() { return 0; }
static int asInt(Status s) { return static_cast<int>(s); }

static int countOf(const char* haystack, const char* needle) // How many times needle appears.
{
  int n = 0;
  for (const char* p = strstr(haystack, needle); p != nullptr; p = strstr(p + 1, needle)) n++;
  return n;
}

// Everything a flight loop needs, wired together the way main.cpp will do it.
struct Rig
{
  FakeRtcBus rtcBus;
  PCF8523 rtc{rtcBus};
  FakeStorage card;
  MissionClock clock{rtc, card, fakeMillis};
  DataLogger logger{card};
  Averager averager;
  StateTable state;

  Rig() { rtcBus.setMet(0); clock.begin(); } // Fresh mission: MET 0 now.

  void runCycles(FlightLoop& loop, uint32_t cycles) // Run cycles 5 s apart, like main.cpp will.
  {
    for (uint32_t i = 0; i < cycles; ++i)
    {
      rtcBus.setMet(loop.cycles() * FlightLoop::kCyclePeriod_s);
      loop.runCycle();
    }
  }
};

void setUp() {}
void tearDown() {}

// ---------- Tests ----------

void test_begin_starts_every_sensor_and_reports_first_problem()
{
  Rig rig;
  FakeSensor ok, missing, broken;
  missing.beginStatus = Status::NOT_PRESENT;
  broken.beginStatus = Status::BUS_ERROR;
  SensorSlot slots[] = {{&ok, 1}, {&missing, 1}, {&broken, 1}};
  FlightLoop loop(slots, 3, rig.clock, rig.logger, rig.averager, rig.state);

  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(loop.begin())); // The FIRST problem...
  TEST_ASSERT_EQUAL_INT(1, broken.begins); // ...but it kept going and started the rest.
  TEST_ASSERT_EQUAL_INT(1, ok.begins);
}

void test_begin_ok_when_all_sensors_start()
{
  Rig rig;
  FakeSensor a, b;
  SensorSlot slots[] = {{&a, 1}, {&b, 2}};
  FlightLoop loop(slots, 2, rig.clock, rig.logger, rig.averager, rig.state);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(loop.begin()));
}

void test_cycle_zero_reads_every_sensor()
{
  Rig rig;
  FakeSensor fast, slow;
  SensorSlot slots[] = {{&fast, 2}, {&slow, 12}};
  FlightLoop loop(slots, 2, rig.clock, rig.logger, rig.averager, rig.state);

  rig.runCycles(loop, 1);
  TEST_ASSERT_EQUAL_INT(1, fast.reads);
  TEST_ASSERT_EQUAL_INT(1, slow.reads); // Full snapshot at boot.
}

void test_sensors_are_read_at_their_own_rates()
{
  Rig rig;
  FakeSensor every5s, every10s, every60s;
  SensorSlot slots[] = {{&every5s, 1}, {&every10s, 2}, {&every60s, 12}};
  FlightLoop loop(slots, 3, rig.clock, rig.logger, rig.averager, rig.state);

  rig.runCycles(loop, 24); // 2 minutes.
  TEST_ASSERT_EQUAL_INT(24, every5s.reads);
  TEST_ASSERT_EQUAL_INT(12, every10s.reads);
  TEST_ASSERT_EQUAL_INT(2, every60s.reads);
  TEST_ASSERT_EQUAL_UINT32(24, loop.cycles());
}

void test_disabled_sensor_is_never_read()
{
  Rig rig;
  FakeSensor on, off;
  SensorSlot slots[] = {{&on, 1}, {&off, 0}}; // 0 = disabled (and must not divide by zero).
  FlightLoop loop(slots, 2, rig.clock, rig.logger, rig.averager, rig.state);

  rig.runCycles(loop, 5);
  TEST_ASSERT_EQUAL_INT(5, on.reads);
  TEST_ASSERT_EQUAL_INT(0, off.reads);
}

void test_empty_slot_is_skipped()
{
  Rig rig;
  FakeSensor a;
  SensorSlot slots[] = {{nullptr, 1}, {&a, 1}};
  FlightLoop loop(slots, 2, rig.clock, rig.logger, rig.averager, rig.state);

  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(loop.begin()));
  rig.runCycles(loop, 2);
  TEST_ASSERT_EQUAL_INT(2, a.reads);
}

void test_all_samples_in_a_cycle_share_one_met()
{
  Rig rig;
  FakeSensor a, b;
  SensorSlot slots[] = {{&a, 1}, {&b, 1}};
  FlightLoop loop(slots, 2, rig.clock, rig.logger, rig.averager, rig.state);

  rig.runCycles(loop, 4); // Last cycle at MET 15.
  TEST_ASSERT_EQUAL_UINT32(15, a.lastMet);
  TEST_ASSERT_EQUAL_UINT32(15, b.lastMet);
}

void test_samples_are_logged_fresh_only()
{
  Rig rig;
  FakeSensor co2, ph;
  co2.id = ParamID::CO2;
  co2.value = 800.0f;
  ph.id = ParamID::PH;
  ph.value = 6.0f;
  SensorSlot slots[] = {{&co2, 1}, {&ph, 3}};
  FlightLoop loop(slots, 2, rig.clock, rig.logger, rig.averager, rig.state);

  rig.runCycles(loop, 6); // CO2 on cycles 0-5, pH on cycles 0 and 3.
  const char* log = rig.card.text("/day_000.csv");
  TEST_ASSERT_EQUAL_INT(6, countOf(log, ",CO2,"));
  TEST_ASSERT_EQUAL_INT(2, countOf(log, ",PH,")); // No repeated values between real readings.
  TEST_ASSERT_NOT_NULL(strstr(log, "15,PH,6"));
}

void test_failed_sensor_is_logged_and_does_not_stop_the_cycle()
{
  Rig rig;
  FakeSensor good, bad;
  good.id = ParamID::AIR_TEMP;
  bad.id = ParamID::CO2;
  bad.readStatus = Status::BUS_ERROR;
  SensorSlot slots[] = {{&bad, 1}, {&good, 1}}; // The bad one is first, to prove it doesn't block the rest.
  FlightLoop loop(slots, 2, rig.clock, rig.logger, rig.averager, rig.state);

  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(loop.runCycle()));
  const char* log = rig.card.text("/day_000.csv");
  TEST_ASSERT_NOT_NULL(strstr(log, "0,CO2,")); // The failure is recorded, not a silent gap...
  TEST_ASSERT_NOT_NULL(strstr(log, "BUS_ERROR"));
  TEST_ASSERT_NOT_NULL(strstr(log, "0,AIR_TEMP,")); // ...and the good sensor still got logged.
}

void test_averager_receives_every_sample()
{
  Rig rig;
  FakeSensor co2;
  co2.value = 800.0f;
  SensorSlot slots[] = {{&co2, 1}};
  FlightLoop loop(slots, 1, rig.clock, rig.logger, rig.averager, rig.state);

  rig.runCycles(loop, 13); // MET 0-60: the sample at MET 60 closes window 0.
  TEST_ASSERT_TRUE(rig.averager.hasCompleted());

  Average out[Averager::kParamCount];
  uint32_t start = 0;
  TEST_ASSERT_EQUAL_UINT32(1, rig.averager.takeCompleted(out, Averager::kParamCount, start));
  TEST_ASSERT_EQUAL_UINT8(12, out[0].sampleCount); // MET 0, 5, ... 55.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 800.0f, out[0].mean);
}

void test_logging_failure_is_returned_but_averaging_continues()
{
  Rig rig;
  FakeSensor co2;
  SensorSlot slots[] = {{&co2, 1}};
  FlightLoop loop(slots, 1, rig.clock, rig.logger, rig.averager, rig.state);

  rig.card.cardPresent = false; // Card pulled after boot.
  for (uint32_t i = 0; i < 13; ++i)
  {
    rig.rtcBus.setMet(loop.cycles() * FlightLoop::kCyclePeriod_s);
    TEST_ASSERT_NOT_EQUAL(asInt(Status::OK), asInt(loop.runCycle())); // The problem is reported...
  }
  TEST_ASSERT_TRUE(rig.averager.hasCompleted()); // ...but the radio summaries still work.
  TEST_ASSERT_EQUAL_INT(13, co2.reads);
}

void test_full_buffer_counts_dropped_reads()
{
  Rig rig;
  FakeSensor a, b, c;
  a.samplesPerRead = 10;
  b.samplesPerRead = 10;
  c.samplesPerRead = 10; // 30 samples, but the buffer holds 24: c has no room.
  SensorSlot slots[] = {{&a, 1}, {&b, 1}, {&c, 1}};
  FlightLoop loop(slots, 3, rig.clock, rig.logger, rig.averager, rig.state);

  rig.runCycles(loop, 1);
  TEST_ASSERT_EQUAL_UINT32(1, loop.readsDropped()); // The loss is visible, not silent.
  TEST_ASSERT_EQUAL_INT(1, c.reads);
}

void test_oversized_table_is_reported_and_never_overrun()
{
  Rig rig;
  FakeSensor sensors[FlightLoop::kMaxSensors + 1];
  SensorSlot slots[FlightLoop::kMaxSensors + 1];
  for (size_t i = 0; i <= FlightLoop::kMaxSensors; ++i)
  {
    sensors[i].samplesPerRead = 1;
    slots[i] = {&sensors[i], 1};
  }
  FlightLoop loop(slots, FlightLoop::kMaxSensors + 1, rig.clock, rig.logger, rig.averager, rig.state);

  TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(loop.begin()));
  rig.runCycles(loop, 1);
  TEST_ASSERT_EQUAL_INT(1, sensors[FlightLoop::kMaxSensors - 1].reads); // The 12th entry is read...
  TEST_ASSERT_EQUAL_INT(0, sensors[FlightLoop::kMaxSensors].reads); // ...the 13th never is.
}

void test_state_table_holds_latest_sample_of_each_parameter()
{
  Rig rig;
  FakeSensor co2, ph;
  co2.id = ParamID::CO2;
  co2.value = 800.0f;
  ph.id = ParamID::PH;
  ph.value = 6.0f;
  SensorSlot slots[] = {{&co2, 1}, {&ph, 3}};
  FlightLoop loop(slots, 2, rig.clock, rig.logger, rig.averager, rig.state);

  rig.runCycles(loop, 5); // Cycles at MET 0-20: CO2 every cycle, pH at MET 0 and 15.
  TEST_ASSERT_EQUAL_UINT32(20, loop.lastMet());
  Sample c = rig.state.latest(ParamID::CO2, loop.lastMet(), 30);
  Sample p = rig.state.latest(ParamID::PH, loop.lastMet(), 30);
  TEST_ASSERT_EQUAL_UINT32(20, c.MET_s); // From the latest cycle...
  TEST_ASSERT_EQUAL_UINT32(15, p.MET_s); // ...or from the latest cycle that read it.
  TEST_ASSERT_EQUAL_FLOAT(6.0f, p.value);
}

void test_state_table_updates_even_when_logging_fails()
{
  Rig rig;
  FakeSensor co2;
  SensorSlot slots[] = {{&co2, 1}};
  FlightLoop loop(slots, 1, rig.clock, rig.logger, rig.averager, rig.state);

  rig.card.cardPresent = false;
  rig.runCycles(loop, 1);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(rig.state.latest(ParamID::CO2, 0, 30).status)); // Rules and control don't depend on the card.
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_begin_starts_every_sensor_and_reports_first_problem);
  RUN_TEST(test_begin_ok_when_all_sensors_start);
  RUN_TEST(test_cycle_zero_reads_every_sensor);
  RUN_TEST(test_sensors_are_read_at_their_own_rates);
  RUN_TEST(test_disabled_sensor_is_never_read);
  RUN_TEST(test_empty_slot_is_skipped);
  RUN_TEST(test_all_samples_in_a_cycle_share_one_met);
  RUN_TEST(test_samples_are_logged_fresh_only);
  RUN_TEST(test_failed_sensor_is_logged_and_does_not_stop_the_cycle);
  RUN_TEST(test_averager_receives_every_sample);
  RUN_TEST(test_logging_failure_is_returned_but_averaging_continues);
  RUN_TEST(test_full_buffer_counts_dropped_reads);
  RUN_TEST(test_oversized_table_is_reported_and_never_overrun);
  RUN_TEST(test_state_table_holds_latest_sample_of_each_parameter);
  RUN_TEST(test_state_table_updates_even_when_logging_fails);
  return UNITY_END();
}
