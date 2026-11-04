#include <unity.h>
#include "sample.h"
#include "services/averager.h"

// No fakes needed: the averager is pure logic (Samples in, Averages out).

static int asInt(Status s) { return static_cast<int>(s); }

static void addOne(Averager& a, ParamID id, float value, Status status, uint32_t MET_s) // Feed a single sample.
{
  Sample s = makeSample(id, value, status, MET_s);
  a.add(&s, 1);
}

static const Average* find(const Average* out, size_t n, ParamID id) // The average for id, or nullptr if it wasn't reported.
{
  for (size_t i = 0; i < n; ++i)
  {
    if (out[i].id == id) return &out[i];
  }
  return nullptr;
}

void setUp() {}
void tearDown() {}

// ---------- Tests ----------

void test_nothing_to_take_at_start()
{
  Averager a;
  Average out[Averager::kParamCount];
  uint32_t start = 999;
  TEST_ASSERT_FALSE(a.hasCompleted());
  TEST_ASSERT_EQUAL_UINT32(0, a.takeCompleted(out, Averager::kParamCount, start));
  TEST_ASSERT_EQUAL_UINT32(999, start); // Untouched when nothing was taken.
}

void test_window_completes_only_when_next_window_starts()
{
  Averager a;
  for (uint32_t t = 0; t < 60; t += 5)
  {
    addOne(a, ParamID::CO2, 800.0f, Status::OK, t);
  }
  TEST_ASSERT_FALSE(a.hasCompleted()); // MET 55 is still window 0.

  addOne(a, ParamID::CO2, 800.0f, Status::OK, 60); // First sample of window 1.
  TEST_ASSERT_TRUE(a.hasCompleted());
}

void test_mean_uses_only_ok_samples()
{
  Averager a;
  addOne(a, ParamID::CO2, 800.0f, Status::OK, 0);
  addOne(a, ParamID::CO2, 0.0f, Status::BUS_ERROR, 5); // Must NOT drag the mean down.
  addOne(a, ParamID::CO2, 900.0f, Status::OK, 10);
  addOne(a, ParamID::CO2, 5000.0f, Status::OUT_OF_RANGE, 15); // Not OK either.
  addOne(a, ParamID::CO2, 0.0f, Status::OK, 60); // Closes window 0.

  Average out[Averager::kParamCount];
  uint32_t start = 0;
  size_t n = a.takeCompleted(out, Averager::kParamCount, start);
  TEST_ASSERT_EQUAL_UINT32(1, n);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(out[0].status));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 850.0f, out[0].mean);
  TEST_ASSERT_EQUAL_UINT8(2, out[0].okCount);
  TEST_ASSERT_EQUAL_UINT8(4, out[0].sampleCount);
}

void test_no_ok_samples_reports_latest_problem()
{
  Averager a;
  addOne(a, ParamID::PH, 0.0f, Status::NOT_PRESENT, 0);
  addOne(a, ParamID::PH, 0.0f, Status::BUS_ERROR, 30); // Most recent problem.
  addOne(a, ParamID::PH, 7.0f, Status::OK, 60); // Closes window 0.

  Average out[Averager::kParamCount];
  uint32_t start = 0;
  TEST_ASSERT_EQUAL_UINT32(1, a.takeCompleted(out, Averager::kParamCount, start));
  TEST_ASSERT_EQUAL_INT(asInt(Status::BUS_ERROR), asInt(out[0].status));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, out[0].mean);
  TEST_ASSERT_EQUAL_UINT8(0, out[0].okCount);
  TEST_ASSERT_EQUAL_UINT8(2, out[0].sampleCount);
}

void test_only_parameters_with_samples_are_reported()
{
  Averager a;
  Sample cycle[2] = {makeSample(ParamID::AIR_TEMP, 21.0f, Status::OK, 0),
                     makeSample(ParamID::CO2, 800.0f, Status::OK, 0)};
  a.add(cycle, 2);
  addOne(a, ParamID::CO2, 800.0f, Status::OK, 60); // Closes window 0.

  Average out[Averager::kParamCount];
  uint32_t start = 0;
  size_t n = a.takeCompleted(out, Averager::kParamCount, start);
  TEST_ASSERT_EQUAL_UINT32(2, n); // pH, O2, ... had no samples: not reported, not filled in.
  TEST_ASSERT_NOT_NULL(find(out, n, ParamID::AIR_TEMP));
  TEST_ASSERT_NOT_NULL(find(out, n, ParamID::CO2));
  TEST_ASSERT_NULL(find(out, n, ParamID::PH));
}

void test_window_start_matches_met()
{
  Averager a;
  addOne(a, ParamID::CO2, 800.0f, Status::OK, 2520); // Window 42.
  addOne(a, ParamID::CO2, 800.0f, Status::OK, 2580); // Window 43: closes 42.

  Average out[Averager::kParamCount];
  uint32_t start = 0;
  a.takeCompleted(out, Averager::kParamCount, start);
  TEST_ASSERT_EQUAL_UINT32(2520, start);
}

void test_met_jump_skips_empty_windows()
{
  Averager a;
  addOne(a, ParamID::CO2, 800.0f, Status::OK, 10); // Window 0.
  addOne(a, ParamID::CO2, 900.0f, Status::OK, 600); // Window 10 (MET jumped, e.g. after a power loss).

  Average out[Averager::kParamCount];
  uint32_t start = 999;
  TEST_ASSERT_EQUAL_UINT32(1, a.takeCompleted(out, Averager::kParamCount, start));
  TEST_ASSERT_EQUAL_UINT32(0, start); // The finished window is window 0...
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 800.0f, out[0].mean); // ...with only its own data.
  TEST_ASSERT_FALSE(a.hasCompleted()); // Windows 1-9 had no data: nothing invented for them.
  TEST_ASSERT_EQUAL_UINT32(0, a.windowsDropped());
}

void test_untaken_window_is_counted_as_dropped()
{
  Averager a;
  addOne(a, ParamID::CO2, 1.0f, Status::OK, 0);
  addOne(a, ParamID::CO2, 2.0f, Status::OK, 60); // Window 0 complete, not taken.
  addOne(a, ParamID::CO2, 3.0f, Status::OK, 120); // Window 1 complete: replaces window 0.
  TEST_ASSERT_EQUAL_UINT32(1, a.windowsDropped());

  Average out[Averager::kParamCount];
  uint32_t start = 0;
  a.takeCompleted(out, Averager::kParamCount, start);
  TEST_ASSERT_EQUAL_UINT32(60, start); // The newest finished window is the one kept.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, out[0].mean);
}

void test_taking_clears_and_prevents_drop()
{
  Averager a;
  addOne(a, ParamID::CO2, 1.0f, Status::OK, 0);
  addOne(a, ParamID::CO2, 2.0f, Status::OK, 60);

  Average out[Averager::kParamCount];
  uint32_t start = 0;
  a.takeCompleted(out, Averager::kParamCount, start);
  TEST_ASSERT_FALSE(a.hasCompleted());
  TEST_ASSERT_EQUAL_UINT32(0, a.takeCompleted(out, Averager::kParamCount, start)); // Can't take the same window twice.

  addOne(a, ParamID::CO2, 3.0f, Status::OK, 120);
  TEST_ASSERT_EQUAL_UINT32(0, a.windowsDropped()); // Window 0 was taken in time.
}

void test_null_output_keeps_window()
{
  Averager a;
  addOne(a, ParamID::CO2, 1.0f, Status::OK, 0);
  addOne(a, ParamID::CO2, 2.0f, Status::OK, 60);

  uint32_t start = 0;
  TEST_ASSERT_EQUAL_UINT32(0, a.takeCompleted(nullptr, 5, start));
  TEST_ASSERT_TRUE(a.hasCompleted()); // A caller's mistake must not destroy data.
}

void test_capacity_limits_output()
{
  Averager a;
  Sample cycle[3] = {makeSample(ParamID::AIR_TEMP, 21.0f, Status::OK, 0),
                     makeSample(ParamID::HUMIDITY, 40.0f, Status::OK, 0),
                     makeSample(ParamID::CO2, 800.0f, Status::OK, 0)};
  a.add(cycle, 3);
  addOne(a, ParamID::CO2, 800.0f, Status::OK, 60);

  Average out[2];
  uint32_t start = 0;
  TEST_ASSERT_EQUAL_UINT32(2, a.takeCompleted(out, 2, start)); // Never writes past the end of out.
}

void test_invalid_id_is_ignored()
{
  Averager a;
  Sample blank; // id = ParamID::COUNT: never filled in.
  blank.MET_s = 0;
  a.add(&blank, 1);
  addOne(a, ParamID::CO2, 800.0f, Status::OK, 60); // Would close window 0 if blank had started one.
  TEST_ASSERT_FALSE(a.hasCompleted());
}

void test_null_samples_are_ignored()
{
  Averager a;
  a.add(nullptr, 3);
  TEST_ASSERT_FALSE(a.hasCompleted());
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_nothing_to_take_at_start);
  RUN_TEST(test_window_completes_only_when_next_window_starts);
  RUN_TEST(test_mean_uses_only_ok_samples);
  RUN_TEST(test_no_ok_samples_reports_latest_problem);
  RUN_TEST(test_only_parameters_with_samples_are_reported);
  RUN_TEST(test_window_start_matches_met);
  RUN_TEST(test_met_jump_skips_empty_windows);
  RUN_TEST(test_untaken_window_is_counted_as_dropped);
  RUN_TEST(test_taking_clears_and_prevents_drop);
  RUN_TEST(test_null_output_keeps_window);
  RUN_TEST(test_capacity_limits_output);
  RUN_TEST(test_invalid_id_is_ignored);
  RUN_TEST(test_null_samples_are_ignored);
  return UNITY_END();
}