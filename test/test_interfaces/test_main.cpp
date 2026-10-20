#include <unity.h>
#include "board_pins.h"
#include "sample.h"
#include "sensor.h"

void setUp() {}
void tearDown() {}

void test_default_sample_is_empty() {
  Sample s;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParamID::COUNT), static_cast<int>(s.id));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::NOT_PRESENT), static_cast<int>(s.status));
}

void test_make_sample_fills_fields() {
  Sample s = makeSample(ParamID::WATER_TEMP, 21.4f, Status::OK, 120);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.4f, s.value);
  TEST_ASSERT_EQUAL_UINT32(120, s.MET_s);
  TEST_ASSERT_TRUE(isUsable(s.status));
}

void test_only_ok_is_usable() {
  TEST_ASSERT_FALSE(isUsable(Status::STALE));
  TEST_ASSERT_FALSE(isUsable(Status::OUT_OF_RANGE));
  TEST_ASSERT_FALSE(isUsable(Status::BUS_ERROR));
  TEST_ASSERT_FALSE(isUsable(Status::NOT_PRESENT));
}

void test_pins_are_unique() {
  const uint8_t all[] = {
    pins::I2C0_SDA, pins::I2C0_SCL, pins::I2C1_SDA, pins::I2C1_SCL,
    pins::SPI_SCK, pins::SPI_MOSI, pins::SPI_MISO,
    pins::STEMMA_POWER, pins::PWM_OE
  };
  const size_t n = sizeof(all) / sizeof(all[0]);
  for (size_t i = 0; i < n; i++) {
    for (size_t j = i + 1; j < n; j++) {
      TEST_ASSERT_NOT_EQUAL(all[i], all[j]);
    }
  }
}

// A fake sensor: proves the interface works with no hardware at all.
class FakeTempSensor : public Sensor {
 public:
  FakeTempSensor(float value, Status status) : value_(value), status_(status) {}
  const char* name() const override { return "fake_temp"; }
  Status begin() override { return Status::OK; }
  size_t read(Sample* out, size_t capacity, uint32_t MET_s) override {
    if (capacity < 1) return 0;
    out[0] = makeSample(ParamID::WATER_TEMP, value_, status_, MET_s);
    return 1;
  }
 private:
  float value_;
  Status status_;
};

void test_fake_sensor_through_interface() {
  FakeTempSensor fake(21.4f, Status::BUS_ERROR);
  Sensor& sensor = fake;                 // used only via the interface
  Sample buf[2];
  size_t n = sensor.read(buf, 2, 500);
  TEST_ASSERT_EQUAL_UINT(1, n);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParamID::WATER_TEMP), static_cast<int>(buf[0].id));
  TEST_ASSERT_FALSE(isUsable(buf[0].status));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_sample_is_empty);
  RUN_TEST(test_make_sample_fills_fields);
  RUN_TEST(test_only_ok_is_usable);
  RUN_TEST(test_pins_are_unique);
  RUN_TEST(test_fake_sensor_through_interface);
  return UNITY_END();
}