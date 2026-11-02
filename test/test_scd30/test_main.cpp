#include <unity.h>
#include <string.h>
#include "i2c_bus.h"
#include "drivers/scd30.h"

// Every byte frame below was generated with Sensirion's official SCD30 driver code (embedded-i2c-scd30),
// so these tests do not depend on our own checksum code being right.

static const uint8_t W_FIRMWARE[] = {0xD1, 0x00};
static const uint8_t W_DATA_READY[] = {0x02, 0x02};
static const uint8_t W_READ_MEAS[] = {0x03, 0x00};
static const uint8_t W_GET_INTERVAL[] = {0x46, 0x00};
static const uint8_t W_GET_ASC[] = {0x53, 0x06};
static const uint8_t W_SET_INTERVAL_2[] = {0x46, 0x00, 0x00, 0x02, 0xE3};
static const uint8_t W_ASC_OFF[] = {0x53, 0x06, 0x00, 0x00, 0x81};
static const uint8_t W_START_0[] = {0x00, 0x10, 0x00, 0x00, 0x81};
static const uint8_t W_START_835[] = {0x00, 0x10, 0x03, 0x43, 0xC2};

static const uint8_t R_FIRMWARE[] = {0x03, 0x42, 0xF3}; // Version 3.66.
static const uint8_t R_WORD_0[] = {0x00, 0x00, 0x81};
static const uint8_t R_WORD_1[] = {0x00, 0x01, 0xB0};
static const uint8_t R_WORD_2[] = {0x00, 0x02, 0xE3};
static const uint8_t R_WORD_5[] = {0x00, 0x05, 0x74};
static const uint8_t R_MEAS_812[] = {0x44, 0x4B, 0x5C, 0x20, 0x00, 0x5D, 0x41, 0xB3, 0x10, // CO2 812.5 ppm, 22.4 C,
                                     0x33, 0x33, 0x88, 0x41, 0xF8, 0xC7, 0x00, 0x00, 0x81}; // 31.0 %RH
static const uint8_t R_MEAS_ZERO[] = {0x00, 0x00, 0x81, 0x00, 0x00, 0x81, 0x00, 0x00, 0x81,
                                      0x00, 0x00, 0x81, 0x00, 0x00, 0x81, 0x00, 0x00, 0x81};
static const uint8_t R_MEAS_45000[] = {0x47, 0x2F, 0x0E, 0xC8, 0x00, 0x1C, 0x41, 0xB3, 0x10,
                                       0x33, 0x33, 0x88, 0x41, 0xF8, 0xC7, 0x00, 0x00, 0x81};
static const uint8_t R_MEAS_NAN[] = {0x7F, 0xC0, 0x64, 0x00, 0x00, 0x81, 0x41, 0xB3, 0x10,
                                     0x33, 0x33, 0x88, 0x41, 0xF8, 0xC7, 0x00, 0x00, 0x81};

// A pretend SCD30. Like the real one, it has no registers: it answers the LAST command it was sent.

class FakeScd30Bus : public I2CBus
{
  public:
  bool connected = true;
  const uint8_t* interval = R_WORD_5; // What "get interval" returns (5 s, so the driver must change it).
  const uint8_t* asc = R_WORD_1; // What "get ASC" returns (on, so the driver must turn it off).
  const uint8_t* measurement = R_MEAS_812;
  int notReadyPolls = 0; // How many "data ready?" checks answer 0 before answering 1.
  bool corruptNextRead = false; // Flip one bit in the next reply.
  int registerCalls = 0; // readRegs/writeRegs calls: must stay 0 (the SCD30 has no registers).

  uint8_t writes[32][5] = {}; // Every frame written, in order.
  size_t writeLens[32] = {};
  int writeCount = 0;

  bool probe(uint8_t addr) override { return connected && addr == 0x61; }
  Status readRegs(uint8_t, uint8_t, uint8_t*, size_t) override { registerCalls++; return Status::BUS_ERROR; }
  Status writeRegs(uint8_t, uint8_t, const uint8_t*, size_t) override { registerCalls++; return Status::BUS_ERROR; }

  Status write(uint8_t addr, const uint8_t* data, size_t len) override
  {
    if (!connected || addr != 0x61 || len > 5) return Status::BUS_ERROR;
    if (writeCount < 32)
    {
      memcpy(writes[writeCount], data, len);
      writeLens[writeCount] = len;
    }
    writeCount++;
    memcpy(last_, data, 2);
    return Status::OK;
  }

  Status read(uint8_t addr, uint8_t* out, size_t len) override
  {
    if (!connected || addr != 0x61) return Status::BUS_ERROR;
    const uint8_t* reply = nullptr;
    size_t replyLen = 3;
    if (is(W_FIRMWARE)) reply = R_FIRMWARE;
    else if (is(W_GET_INTERVAL)) reply = interval;
    else if (is(W_GET_ASC)) reply = asc;
    else if (is(W_DATA_READY))
    {
      reply = (notReadyPolls > 0) ? R_WORD_0 : R_WORD_1;
      if (notReadyPolls > 0) notReadyPolls--;
    }
    else if (is(W_READ_MEAS)) { reply = measurement; replyLen = 18; }
    if (reply == nullptr || len != replyLen) return Status::BUS_ERROR; // A real SCD30 would NACK.
    memcpy(out, reply, len);
    if (corruptNextRead) { out[0] ^= 0x01; corruptNextRead = false; }
    return Status::OK;
  }

  bool wrote(const uint8_t* frame, size_t len) const // Was this exact frame ever written?
  {
    for (int i = 0; i < writeCount && i < 32; i++)
      if (writeLens[i] == len && memcmp(writes[i], frame, len) == 0) return true;
    return false;
  }

  private:
  uint8_t last_[2] = {};
  bool is(const uint8_t* cmd) const { return last_[0] == cmd[0] && last_[1] == cmd[1]; }
};

static uint32_t g_waitedMs = 0;
static void fakeDelay(uint32_t ms) { g_waitedMs += ms; }
static int asInt(Status s) { return static_cast<int>(s); }

void setUp() { g_waitedMs = 0; }
void tearDown() {}

void test_crc_matches_datasheet_example()
{
  const uint8_t beef[2] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, SCD30::crc8(beef, 2)); // Worked example from Sensirion's interface description.
}

void test_begin_configures_and_starts()
{
  FakeScd30Bus bus;
  SCD30 co2(bus, fakeDelay);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(co2.begin()));
  TEST_ASSERT_TRUE(bus.wrote(W_SET_INTERVAL_2, 5)); // Interval was 5 s, so it was changed to 2 s.
  TEST_ASSERT_TRUE(bus.wrote(W_ASC_OFF, 5)); // Self-calibration was on, so it was turned off.
  TEST_ASSERT_TRUE(bus.wrote(W_START_0, 5)); // Measuring started, pressure compensation off.
  TEST_ASSERT_EQUAL_INT(0, bus.registerCalls); // Never used register-style transfers.
}

void test_begin_skips_settings_already_correct()
{
  FakeScd30Bus bus;
  bus.interval = R_WORD_2;
  bus.asc = R_WORD_0;
  SCD30 co2(bus, fakeDelay);
  co2.begin();
  TEST_ASSERT_FALSE(bus.wrote(W_SET_INTERVAL_2, 5)); // Non-volatile memory left alone.
  TEST_ASSERT_FALSE(bus.wrote(W_ASC_OFF, 5));
  TEST_ASSERT_TRUE(bus.wrote(W_START_0, 5));
}

void test_missing_sensor_reports_not_present()
{
  FakeScd30Bus bus;
  bus.connected = false;
  SCD30 co2(bus, fakeDelay);
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(co2.begin()));
  Sample s[1];
  TEST_ASSERT_EQUAL_UINT(1, co2.read(s, 1, 4));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParamID::CO2), static_cast<int>(s[0].id));
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(s[0].status));
}

void test_reads_co2()
{
  FakeScd30Bus bus;
  SCD30 co2(bus, fakeDelay);
  co2.begin();
  Sample s[1];
  TEST_ASSERT_EQUAL_UINT(1, co2.read(s, 1, 120));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParamID::CO2), static_cast<int>(s[0].id));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 812.5f, s[0].value);
  TEST_ASSERT_TRUE(isUsable(s[0].status));
  TEST_ASSERT_EQUAL_UINT32(120, s[0].MET_s);
}

void test_waits_for_data_ready()
{
  FakeScd30Bus bus;
  bus.notReadyPolls = 3;
  SCD30 co2(bus, fakeDelay);
  co2.begin();
  g_waitedMs = 0;
  Sample s[1];
  co2.read(s, 1, 0);
  TEST_ASSERT_TRUE(isUsable(s[0].status));
  TEST_ASSERT_EQUAL_UINT32(4 * 10 + 3 * 100 + 10, g_waitedMs); // 4 ready-checks (10 ms each), 3 waits of 100 ms, then the read (10 ms).
}

void test_never_ready_marks_bus_error()
{
  FakeScd30Bus bus;
  bus.notReadyPolls = 1000;
  SCD30 co2(bus, fakeDelay);
  co2.begin();
  g_waitedMs = 0;
  Sample s[1];
  co2.read(s, 1, 0);
  TEST_ASSERT_EQUAL_INT(asInt(Status::BUS_ERROR), asInt(s[0].status));
  TEST_ASSERT_EQUAL_UINT32(25 * 10 + 25 * 100, g_waitedMs); // Gave up after 25 checks (2.75 s), not forever.
}

void test_corrupted_data_rejected()
{
  FakeScd30Bus bus;
  SCD30 co2(bus, fakeDelay);
  co2.begin();
  bus.corruptNextRead = true; // The data-ready reply gets a flipped bit.
  Sample s[1];
  co2.read(s, 1, 0);
  TEST_ASSERT_EQUAL_INT(asInt(Status::BUS_ERROR), asInt(s[0].status));
}

void test_warm_up_zero_is_not_present()
{
  FakeScd30Bus bus;
  bus.measurement = R_MEAS_ZERO;
  SCD30 co2(bus, fakeDelay);
  co2.begin();
  Sample s[1];
  co2.read(s, 1, 0);
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(s[0].status));
}

void test_impossible_values_flagged()
{
  FakeScd30Bus bus;
  bus.measurement = R_MEAS_45000; // Above the 40 000 ppm range.
  SCD30 co2(bus, fakeDelay);
  co2.begin();
  Sample s[1];
  co2.read(s, 1, 0);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(s[0].status));
  bus.measurement = R_MEAS_NAN; // "Not a number".
  co2.read(s, 1, 0);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(s[0].status));
}

void test_pressure_compensation()
{
  FakeScd30Bus bus;
  SCD30 co2(bus, fakeDelay);
  co2.begin();
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(co2.setAmbientPressure(83500.0f))); // 835 mbar (Boulder).
  TEST_ASSERT_TRUE(bus.wrote(W_START_835, 5));
  int before = bus.writeCount;
  co2.setAmbientPressure(83560.0f); // Rounds to 836 mbar: a 1 mbar change, not worth sending.
  TEST_ASSERT_EQUAL_INT(before, bus.writeCount);
  co2.setAmbientPressure(60000.0f); // 600 mbar is below what the sensor supports: compensation switched off.
  TEST_ASSERT_EQUAL_INT(before + 1, bus.writeCount);
  TEST_ASSERT_EQUAL_MEMORY(W_START_0, bus.writes[before], 5);
}

void test_pressure_set_before_begin_is_applied()
{
  FakeScd30Bus bus;
  SCD30 co2(bus, fakeDelay);
  co2.setAmbientPressure(83500.0f); // Not started yet: remembered...
  TEST_ASSERT_EQUAL_INT(0, bus.writeCount);
  co2.begin(); // ...and used when measuring starts.
  TEST_ASSERT_TRUE(bus.wrote(W_START_835, 5));
  TEST_ASSERT_FALSE(bus.wrote(W_START_0, 5));
}

void test_recovers_after_wire_reconnected()
{
  FakeScd30Bus bus;
  SCD30 co2(bus, fakeDelay);
  co2.begin();
  Sample s[1];
  bus.connected = false;
  co2.read(s, 1, 0);
  TEST_ASSERT_FALSE(isUsable(s[0].status));
  bus.connected = true;
  co2.read(s, 1, 2); // Sets itself up again and reads.
  TEST_ASSERT_TRUE(isUsable(s[0].status));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 812.5f, s[0].value);
}

void test_too_small_buffer_writes_nothing()
{
  FakeScd30Bus bus;
  SCD30 co2(bus, fakeDelay);
  TEST_ASSERT_EQUAL_UINT(0, co2.read(nullptr, 1, 0));
  Sample s[1];
  TEST_ASSERT_EQUAL_UINT(0, co2.read(s, 0, 0));
  TEST_ASSERT_EQUAL_INT(0, bus.writeCount); // Didn't touch the sensor.
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_crc_matches_datasheet_example);
  RUN_TEST(test_begin_configures_and_starts);
  RUN_TEST(test_begin_skips_settings_already_correct);
  RUN_TEST(test_missing_sensor_reports_not_present);
  RUN_TEST(test_reads_co2);
  RUN_TEST(test_waits_for_data_ready);
  RUN_TEST(test_never_ready_marks_bus_error);
  RUN_TEST(test_corrupted_data_rejected);
  RUN_TEST(test_warm_up_zero_is_not_present);
  RUN_TEST(test_impossible_values_flagged);
  RUN_TEST(test_pressure_compensation);
  RUN_TEST(test_pressure_set_before_begin_is_applied);
  RUN_TEST(test_recovers_after_wire_reconnected);
  RUN_TEST(test_too_small_buffer_writes_nothing);
  return UNITY_END();
}