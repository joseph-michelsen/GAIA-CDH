#include <unity.h>
#include "sample.h"
#include "services/state_table.h"
#include "app/rules.h"

// No fakes needed: each rule is pure logic (state table in, decision out).

static void put(StateTable& t, ParamID id, float value, Status status, uint32_t MET_s) // Store a single sample.
{
  Sample s = makeSample(id, value, status, MET_s);
  t.update(&s, 1);
}

void setUp() {}
void tearDown() {}

// ---------- co2PressureCompensation ----------

void test_fresh_ok_pressure_is_passed_on()
{
  StateTable t;
  put(t, ParamID::PRESSURE, 83500.0f, Status::OK, 100);
  float pa = 0.0f;
  TEST_ASSERT_TRUE(rules::co2PressureCompensation(t, 110, pa));
  TEST_ASSERT_EQUAL_FLOAT(83500.0f, pa);
}

void test_no_pressure_yet_does_nothing()
{
  StateTable t;
  float pa = -1.0f;
  TEST_ASSERT_FALSE(rules::co2PressureCompensation(t, 0, pa));
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, pa); // Output untouched when the rule says "do nothing".
}

void test_stale_pressure_does_nothing()
{
  StateTable t;
  put(t, ParamID::PRESSURE, 83500.0f, Status::OK, 100);
  float pa = -1.0f;
  TEST_ASSERT_TRUE(rules::co2PressureCompensation(t, 100 + rules::kPressureMaxAge_s, pa)); // At the limit: still fresh.
  TEST_ASSERT_FALSE(rules::co2PressureCompensation(t, 101 + rules::kPressureMaxAge_s, pa)); // Past it: stale.
}

void test_failed_pressure_reading_does_nothing()
{
  StateTable t;
  put(t, ParamID::PRESSURE, 83500.0f, Status::OK, 100);
  put(t, ParamID::PRESSURE, 0.0f, Status::BUS_ERROR, 110); // The BME680 just failed.
  float pa = -1.0f;
  TEST_ASSERT_FALSE(rules::co2PressureCompensation(t, 110, pa)); // 0 Pa must never reach the SCD30.
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, pa);
}

void test_out_of_range_pressure_does_nothing()
{
  StateTable t;
  put(t, ParamID::PRESSURE, 250000.0f, Status::OUT_OF_RANGE, 100); // The BME680 flagged it as impossible.
  float pa = -1.0f;
  TEST_ASSERT_FALSE(rules::co2PressureCompensation(t, 100, pa));
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_fresh_ok_pressure_is_passed_on);
  RUN_TEST(test_no_pressure_yet_does_nothing);
  RUN_TEST(test_stale_pressure_does_nothing);
  RUN_TEST(test_failed_pressure_reading_does_nothing);
  RUN_TEST(test_out_of_range_pressure_does_nothing);
  return UNITY_END();
}
