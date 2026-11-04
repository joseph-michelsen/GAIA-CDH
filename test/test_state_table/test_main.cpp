#include <unity.h>
#include "sample.h"
#include "services/state_table.h"

// No fakes needed: the state table is pure logic (Samples in, latest Sample out).

static int asInt(Status s) { return static_cast<int>(s); }

static void put(StateTable& t, ParamID id, float value, Status status, uint32_t MET_s) // Store a single sample.
{
  Sample s = makeSample(id, value, status, MET_s);
  t.update(&s, 1);
}

void setUp() {}
void tearDown() {}

// ---------- Tests ----------

void test_never_received_is_not_present()
{
  StateTable t;
  Sample s = t.latest(ParamID::CO2, 100, 30);
  TEST_ASSERT_TRUE(s.id == ParamID::CO2);
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(s.status));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.value);
}

void test_recent_sample_is_returned_as_stored()
{
  StateTable t;
  put(t, ParamID::PRESSURE, 83500.0f, Status::OK, 100);
  Sample s = t.latest(ParamID::PRESSURE, 120, 30); // 20 s old, limit 30 s.
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(s.status));
  TEST_ASSERT_EQUAL_FLOAT(83500.0f, s.value);
  TEST_ASSERT_EQUAL_UINT32(100, s.MET_s); // When it was measured, not when it was asked for.
}

void test_exactly_max_age_is_still_fresh()
{
  StateTable t;
  put(t, ParamID::CO2, 800.0f, Status::OK, 100);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(t.latest(ParamID::CO2, 130, 30).status)); // 30 s old: allowed.
  TEST_ASSERT_EQUAL_INT(asInt(Status::STALE), asInt(t.latest(ParamID::CO2, 131, 30).status)); // 31 s old: stale.
}

void test_stale_sample_keeps_its_value()
{
  StateTable t;
  put(t, ParamID::CO2, 812.0f, Status::OK, 0);
  Sample s = t.latest(ParamID::CO2, 240, 30);
  TEST_ASSERT_EQUAL_INT(asInt(Status::STALE), asInt(s.status));
  TEST_ASSERT_EQUAL_FLOAT(812.0f, s.value); // Still available for display: "812 ppm, 4 min ago".
}

void test_newest_sample_wins_even_if_it_is_an_error()
{
  StateTable t;
  put(t, ParamID::PRESSURE, 83500.0f, Status::OK, 100);
  put(t, ParamID::PRESSURE, 0.0f, Status::BUS_ERROR, 110);
  TEST_ASSERT_EQUAL_INT(asInt(Status::BUS_ERROR), asInt(t.latest(ParamID::PRESSURE, 110, 30).status)); // The current truth: broken.
}

void test_received_not_present_differs_from_never_received()
{
  StateTable t;
  put(t, ParamID::CO2, 0.0f, Status::NOT_PRESENT, 100); // e.g. SCD-30 warming up.
  Sample s = t.latest(ParamID::CO2, 500, 30);
  TEST_ASSERT_EQUAL_INT(asInt(Status::STALE), asInt(s.status)); // A real (old) reading, so it can go stale...
  TEST_ASSERT_EQUAL_UINT32(100, s.MET_s); // ...and it keeps its own timestamp.
}

void test_parameters_are_independent()
{
  StateTable t;
  Sample cycle[2] = {makeSample(ParamID::AIR_TEMP, 21.5f, Status::OK, 50),
                     makeSample(ParamID::HUMIDITY, 40.0f, Status::OK, 50)};
  t.update(cycle, 2);
  put(t, ParamID::AIR_TEMP, 22.0f, Status::OK, 60);
  TEST_ASSERT_EQUAL_FLOAT(22.0f, t.latest(ParamID::AIR_TEMP, 60, 30).value);
  TEST_ASSERT_EQUAL_FLOAT(40.0f, t.latest(ParamID::HUMIDITY, 60, 30).value); // Untouched by the AIR_TEMP update.
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(t.latest(ParamID::PH, 60, 30).status));
}

void test_future_sample_counts_as_brand_new()
{
  StateTable t;
  put(t, ParamID::CO2, 800.0f, Status::OK, 200);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(t.latest(ParamID::CO2, 100, 0).status)); // No unsigned wrap-around to a huge age.
}

void test_invalid_ids_are_ignored()
{
  StateTable t;
  Sample blank; // id = ParamID::COUNT: never filled in.
  t.update(&blank, 1); // Must not write past the end of the table.
  t.update(nullptr, 3);
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(t.latest(ParamID::COUNT, 0, 30).status));
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_never_received_is_not_present);
  RUN_TEST(test_recent_sample_is_returned_as_stored);
  RUN_TEST(test_exactly_max_age_is_still_fresh);
  RUN_TEST(test_stale_sample_keeps_its_value);
  RUN_TEST(test_newest_sample_wins_even_if_it_is_an_error);
  RUN_TEST(test_received_not_present_differs_from_never_received);
  RUN_TEST(test_parameters_are_independent);
  RUN_TEST(test_future_sample_counts_as_brand_new);
  RUN_TEST(test_invalid_ids_are_ignored);
  return UNITY_END();
}
