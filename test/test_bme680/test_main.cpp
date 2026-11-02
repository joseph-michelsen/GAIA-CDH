#include <unity.h>
#include <string.h>
#include "i2c_bus.h"
#include "drivers/bme680.h"

// Calibration and measurement bytes below were fed through Bosch's official BME68x Sensor API
// (floating-point build) to produce the REF_ values. The coefficients are representative, not from a specific chip.

static const uint8_t kCalib1[23] = {0x6B, 0x66, 0x03, 0x00, 0x1D, 0x8D, 0xA9, 0xD7, 0x58, 0x00, 0x24, 0x1B,
                                    0x74, 0xFF, 0x23, 0x1E, 0x00, 0x00, 0xEB, 0xF8, 0x9D, 0xF3, 0x1E}; // 0x8A..0xA0
static const uint8_t kCalib2[14] = {0x3F, 0x7F, 0x30, 0x00, 0x2D, 0x14, 0x78, 0x9C, 0xB3, 0x66, 0x00, 0x00, 0x00, 0x00}; // 0xE1..0xEE
static const uint8_t kField[10] = {0x80, 0x00, 0x6E, 0x3B, 0x00, 0x77, 0xE1, 0xC0, 0x49, 0x4A}; // 0x1D..0x26
static const float REF_TEMP_C = 22.000311f;
static const float REF_PRESSURE_PA = 83499.9219f;
static const float REF_HUMIDITY = 30.002012f;

// A pretend BME680: 256 one-byte registers. Reads auto-increment like the real chip.
// Writes must be ONE register at a time (the real chip expects register/value pairs), so longer writes fail.

class FakeBme680Bus : public I2CBus
{
  public:
  uint8_t regs[256] = {};
  bool connected = true;
  bool finishesMeasurement = true; // If false, the "new data" bit never turns on (simulates a stuck chip).
  int writes = 0;

  FakeBme680Bus()
  {
    regs[0xD0] = 0x61;
    memcpy(&regs[0x8A], kCalib1, sizeof(kCalib1));
    memcpy(&regs[0xE1], kCalib2, sizeof(kCalib2));
    memcpy(&regs[0x1D], kField, sizeof(kField));
    regs[0x1D] = 0x00; // No new data until a measurement is started.
  }

  bool probe(uint8_t addr) override { return connected && addr == 0x77; }

  Status readRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) override
  {
    if (!connected || addr != 0x77 || reg + len > 256) return Status::BUS_ERROR;
    memcpy(out, &regs[reg], len);
    return Status::OK;
  }

  Status writeRegs(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) override
  {
    if (!connected || addr != 0x77 || len != 1) return Status::BUS_ERROR;
    regs[reg] = data[0];
    writes++;
    if (reg == 0x74 && (data[0] & 0x03) == 0x01 && finishesMeasurement) regs[0x1D] |= 0x80; // Forced mode -> results ready.
    if (reg == 0xE0 && data[0] == 0xB6) { regs[0x72] = 0; regs[0x74] = 0; regs[0x75] = 0; } // Soft reset clears settings.
    return Status::OK;
  }

  Status write(uint8_t, const uint8_t*, size_t) override { return Status::BUS_ERROR; } // The BME680 never uses plain transfers.
  Status read(uint8_t, uint8_t*, size_t) override { return Status::BUS_ERROR; }
};

static uint32_t g_waitedMs = 0;
static void fakeDelay(uint32_t ms) { g_waitedMs += ms; }
static int asInt(Status s) { return static_cast<int>(s); }

void setUp() { g_waitedMs = 0; }
void tearDown() {}

void test_detects_and_configures_chip()
{
  FakeBme680Bus bus;
  BME680 env(bus, fakeDelay);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(env.begin()));
  TEST_ASSERT_EQUAL_HEX8(0xB6, bus.regs[0xE0]); // Soft reset was sent.
  TEST_ASSERT_EQUAL_HEX8(0x05, bus.regs[0x72]); // Humidity x16.
  TEST_ASSERT_EQUAL_HEX8(0x44, bus.regs[0x74]); // Temperature x2, pressure x1, sleep mode.
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.regs[0x75]); // Filter off.
}

void test_wrong_chip_reports_not_present()
{
  FakeBme680Bus bus;
  bus.regs[0xD0] = 0x60; // Some other chip.
  BME680 env(bus, fakeDelay);
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(env.begin()));
  Sample s[3];
  TEST_ASSERT_EQUAL_UINT(3, env.read(s, 3, 7));
  for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(s[i].status));
}

void test_matches_bosch_reference()
{
  FakeBme680Bus bus;
  BME680 env(bus, fakeDelay);
  env.begin();
  Sample s[3];
  TEST_ASSERT_EQUAL_UINT(3, env.read(s, 3, 120));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParamID::AIR_TEMP), static_cast<int>(s[0].id));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParamID::HUMIDITY), static_cast<int>(s[1].id));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParamID::PRESSURE), static_cast<int>(s[2].id));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, REF_TEMP_C, s[0].value);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, REF_HUMIDITY, s[1].value);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, REF_PRESSURE_PA, s[2].value);
  for (int i = 0; i < 3; i++)
  {
    TEST_ASSERT_TRUE(isUsable(s[i].status));
    TEST_ASSERT_EQUAL_UINT32(120, s[i].MET_s);
  }
}

void test_waits_for_measurement()
{
  FakeBme680Bus bus;
  BME680 env(bus, fakeDelay);
  env.begin();
  g_waitedMs = 0;
  Sample s[3];
  env.read(s, 3, 0);
  TEST_ASSERT_EQUAL_UINT32(43, g_waitedMs); // Bosch formula: 42.59 ms, rounded up.
}

void test_stuck_measurement_marks_bus_error()
{
  FakeBme680Bus bus;
  bus.finishesMeasurement = false;
  BME680 env(bus, fakeDelay);
  env.begin();
  g_waitedMs = 0;
  Sample s[3];
  TEST_ASSERT_EQUAL_UINT(3, env.read(s, 3, 0));
  for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_INT(asInt(Status::BUS_ERROR), asInt(s[i].status));
  TEST_ASSERT_EQUAL_UINT32(43 + 5 * 10, g_waitedMs); // Waited, then checked 5 times, 10 ms apart, then gave up.
}

void test_lost_bus_marks_bus_error()
{
  FakeBme680Bus bus;
  BME680 env(bus, fakeDelay);
  env.begin();
  bus.connected = false; // The wire "breaks".
  Sample s[3];
  env.read(s, 3, 0);
  for (int i = 0; i < 3; i++) TEST_ASSERT_FALSE(isUsable(s[i].status));
}

void test_recovers_after_sensor_power_loss()
{
  FakeBme680Bus bus;
  BME680 env(bus, fakeDelay);
  env.begin();
  bus.regs[0x72] = 0; bus.regs[0x74] = 0; // The sensor lost power and forgot its settings.
  Sample s[3];
  env.read(s, 3, 0);
  TEST_ASSERT_EQUAL_HEX8(0x05, bus.regs[0x72]); // Settings were put back...
  TEST_ASSERT_FLOAT_WITHIN(0.001f, REF_HUMIDITY, s[1].value); // ...so humidity is real, not "skipped".
  TEST_ASSERT_TRUE(isUsable(s[1].status));
}

void test_recovers_after_wire_reconnected()
{
  FakeBme680Bus bus;
  BME680 env(bus, fakeDelay);
  env.begin();
  Sample s[3];
  bus.connected = false;
  env.read(s, 3, 0); // Fails.
  bus.connected = true;
  env.read(s, 3, 2); // Should set itself up again and work.
  TEST_ASSERT_TRUE(isUsable(s[0].status));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, REF_TEMP_C, s[0].value);
}

void test_impossible_temperature_taints_all_three()
{
  FakeBme680Bus bus;
  bus.regs[0x22] = 0xB4; bus.regs[0x23] = 0xC4; bus.regs[0x24] = 0x60; // Raw value Bosch's formula turns into 100 C.
  BME680 env(bus, fakeDelay);
  env.begin();
  Sample s[3];
  env.read(s, 3, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, s[0].value);
  for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(s[i].status));
}

void test_too_small_buffer_writes_nothing()
{
  FakeBme680Bus bus;
  BME680 env(bus, fakeDelay);
  Sample s[2];
  TEST_ASSERT_EQUAL_UINT(0, env.read(s, 2, 0));
  TEST_ASSERT_EQUAL_INT(0, bus.writes); // Didn't even touch the chip.
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_detects_and_configures_chip);
  RUN_TEST(test_wrong_chip_reports_not_present);
  RUN_TEST(test_matches_bosch_reference);
  RUN_TEST(test_waits_for_measurement);
  RUN_TEST(test_stuck_measurement_marks_bus_error);
  RUN_TEST(test_lost_bus_marks_bus_error);
  RUN_TEST(test_recovers_after_sensor_power_loss);
  RUN_TEST(test_recovers_after_wire_reconnected);
  RUN_TEST(test_impossible_temperature_taints_all_three);
  RUN_TEST(test_too_small_buffer_writes_nothing);
  return UNITY_END();
}