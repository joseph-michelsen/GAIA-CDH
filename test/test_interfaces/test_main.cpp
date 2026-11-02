#include <unity.h>
#include "board_pins.h"
#include "sample.h"
#include "sensor.h"
#include "storage.h"
#include <string.h>

void setUp() {}
void tearDown() {}

void test_default_sample_is_empty() 
{
  Sample s;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParamID::COUNT), static_cast<int>(s.id));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::NOT_PRESENT), static_cast<int>(s.status));
}

void test_make_sample_fills_fields() 
{
  Sample s = makeSample(ParamID::WATER_TEMP, 21.4f, Status::OK, 120);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.4f, s.value);
  TEST_ASSERT_EQUAL_UINT32(120, s.MET_s);
  TEST_ASSERT_TRUE(isUsable(s.status));
}

void test_only_ok_is_usable() 
{
  TEST_ASSERT_FALSE(isUsable(Status::STALE));
  TEST_ASSERT_FALSE(isUsable(Status::OUT_OF_RANGE));
  TEST_ASSERT_FALSE(isUsable(Status::BUS_ERROR));
  TEST_ASSERT_FALSE(isUsable(Status::NOT_PRESENT));
}

void test_pins_are_unique() 
{
  const uint8_t all[] = 
  {
    pins::I2C0_SDA, pins::I2C0_SCL, pins::I2C1_SDA, pins::I2C1_SCL,
    pins::SPI_SCK, pins::SPI_MOSI, pins::SPI_MISO, pins::SD_CS,
    pins::STEMMA_POWER, pins::PWM_OE
  };

  const size_t n = sizeof(all) / sizeof(all[0]);
  for (size_t i = 0; i < n; i++) 
  {
    for (size_t j = i + 1; j < n; j++) 
    {
      TEST_ASSERT_NOT_EQUAL(all[i], all[j]);
    }
  }
}

// A fake sensor: proves the interface works with no hardware at all.
class FakeTempSensor : public Sensor 
{
  public:
  FakeTempSensor(float value, Status status) : value_(value), status_(status) {}
  const char* name() const override { return "fake_temp"; }
  Status begin() override { return Status::OK; }
  size_t read(Sample* out, size_t capacity, uint32_t MET_s) override 
  {
    if (capacity < 1) return 0;
    out[0] = makeSample(ParamID::WATER_TEMP, value_, status_, MET_s);
    return 1;
  }
  
  private:
  float value_;
  Status status_;
};

void test_fake_sensor_through_interface() 
{
  FakeTempSensor fake(21.4f, Status::BUS_ERROR);
  Sensor& sensor = fake; // used only via the interface
  Sample buf[2];
  size_t n = sensor.read(buf, 2, 500);
  TEST_ASSERT_EQUAL_UINT(1, n);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParamID::WATER_TEMP), static_cast<int>(buf[0].id));
  TEST_ASSERT_FALSE(isUsable(buf[0].status));
}

// A fake storage: proves the Storage contract is complete and usable with no SD card at all.
class FakeStorage : public Storage
{
  public:
  char path[Storage::kMaxPathLength + 1] = {};
  uint8_t data[64] = {};
  size_t size = 0;

  Status begin() override { return Status::OK; }
  bool exists(const char* p) override { return size > 0 && strcmp(p, path) == 0; }
  Status append(const char* p, const uint8_t* d, size_t len) override
  {
    if (!isValidPath(p) || size + len > sizeof(data)) return Status::OUT_OF_RANGE;
    strncpy(path, p, sizeof(path) - 1);
    memcpy(data + size, d, len);
    size += len;
    return Status::OK;
  }
  Status writeFile(const char* p, const uint8_t* d, size_t len) override { size = 0; return append(p, d, len); }
  Status readFile(const char* p, uint8_t* out, size_t capacity, size_t& got) override
  {
    got = 0;
    if (!exists(p)) return Status::NOT_PRESENT;
    if (size > capacity) return Status::OUT_OF_RANGE;
    memcpy(out, data, size);
    got = size;
    return Status::OK;
  }
  Status freeBytes(uint64_t& bytes) override { bytes = sizeof(data) - size; return Status::OK; }
};

void test_fake_storage_through_interface()
{
  FakeStorage fake;
  Storage& store = fake; // used only via the interface
  const uint8_t a[] = {'a', 'b'};
  const uint8_t b[] = {'c'};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::OK), static_cast<int>(store.append("/log.csv", a, 2)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::OK), static_cast<int>(store.append("/log.csv", b, 1)));
  uint8_t out[8];
  size_t got = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::OK), static_cast<int>(store.readFile("/log.csv", out, sizeof(out), got)));
  TEST_ASSERT_EQUAL_UINT(3, got);
  TEST_ASSERT_EQUAL_MEMORY("abc", out, 3);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::NOT_PRESENT), static_cast<int>(store.readFile("/other.csv", out, sizeof(out), got)));
}

void test_param_names()
{
  TEST_ASSERT_EQUAL_STRING("CO2", paramName(ParamID::CO2));
  TEST_ASSERT_EQUAL_STRING("AIR_TEMP", paramName(ParamID::AIR_TEMP));
  TEST_ASSERT_EQUAL_STRING("BATT_VOLTAGE", paramName(ParamID::BATT_VOLTAGE));
  TEST_ASSERT_EQUAL_STRING("INVALID", paramName(ParamID::COUNT));
  for (uint8_t i = 0; i < static_cast<uint8_t>(ParamID::COUNT); i++)
  {
    TEST_ASSERT_NOT_EQUAL(0, strcmp("INVALID", paramName(static_cast<ParamID>(i)))); // Every real parameter has a name.
  }
}

void test_path_rules()
{
  TEST_ASSERT_TRUE(isValidPath("/log_2026-11-01.csv"));
  TEST_ASSERT_TRUE(isValidPath("/m"));
  TEST_ASSERT_FALSE(isValidPath(nullptr));
  TEST_ASSERT_FALSE(isValidPath("log.csv")); // Must start with "/".
  TEST_ASSERT_FALSE(isValidPath("/"));
  TEST_ASSERT_FALSE(isValidPath("/logs/day1.csv")); // No sub-folders.
  TEST_ASSERT_TRUE(isValidPath("/abcdefghijklmnopqrstuvwxyz1234")); // 31 characters: the limit.
  TEST_ASSERT_FALSE(isValidPath("/abcdefghijklmnopqrstuvwxyz12345")); // 32: too long.
}

int main() 
{
  UNITY_BEGIN();
  RUN_TEST(test_default_sample_is_empty);
  RUN_TEST(test_make_sample_fills_fields);
  RUN_TEST(test_only_ok_is_usable);
  RUN_TEST(test_pins_are_unique);
  RUN_TEST(test_fake_sensor_through_interface);
  RUN_TEST(test_fake_storage_through_interface);
  RUN_TEST(test_path_rules);
  RUN_TEST(test_param_names);
  return UNITY_END();
}