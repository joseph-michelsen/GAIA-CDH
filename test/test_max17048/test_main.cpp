#include <unity.h>
#include "i2c_bus.h"
#include "drivers/max17048.h"

// A pretend I2C bus: tests choose what each register "contains".
class FakeI2CBus : public I2CBus 
{
  public:
  uint16_t regs[256] = {};
  bool connected = true;

  bool probe(uint8_t addr) override { return connected && addr == 0x36; }

  Status readRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) override 
  {
    if (!connected || addr != 0x36 || len != 2) return Status::BUS_ERROR;
    out[0] = static_cast<uint8_t>(regs[reg] >> 8);
    out[1] = static_cast<uint8_t>(regs[reg] & 0xFF);
    return Status::OK;
  }

  Status writeRegs(uint8_t, uint8_t, const uint8_t*, size_t) override { return Status::OK; }
};

static int asInt(Status s) { return static_cast<int>(s); }

void setUp() {}
void tearDown() {}

void test_detects_real_chip() 
{
  FakeI2CBus bus;
  bus.regs[0x08] = 0x0012; // a valid version
  Max17048 gauge(bus);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(gauge.begin()));
}

void test_no_battery_reports_not_present() 
{
  FakeI2CBus bus;
  bus.regs[0x08] = 0xFFFF; // what the chip reports without a battery
  Max17048 gauge(bus);
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(gauge.begin()));
  Sample s[2];
  TEST_ASSERT_EQUAL_UINT(2, gauge.read(s, 2, 10));
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(s[0].status));
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(s[1].status));
}

void test_converts_voltage_and_charge() 
{
  FakeI2CBus bus;
  bus.regs[0x08] = 0x0012;
  bus.regs[0x02] = 0xB900; // 47360 x 78.125 uV = 3.700 V
  bus.regs[0x04] = 0x3200; // 12800 / 256 = 50.0 %
  Max17048 gauge(bus);
  gauge.begin();
  Sample s[2];
  gauge.read(s, 2, 42);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.700f, s[0].value);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, s[1].value);
  TEST_ASSERT_TRUE(isUsable(s[0].status));
  TEST_ASSERT_EQUAL_UINT32(42, s[1].MET_s);
}

void test_impossible_voltage_flagged() 
{
  FakeI2CBus bus;
  bus.regs[0x08] = 0x0012;
  bus.regs[0x02] = 0x0000; // 0 V: impossible for a connected cell
  Max17048 gauge(bus);
  gauge.begin();
  Sample s[2];
  gauge.read(s, 2, 0);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(s[0].status));
}

void test_lost_bus_marks_bus_error() {
  FakeI2CBus bus;
  bus.regs[0x08] = 0x0012;
  Max17048 gauge(bus);
  gauge.begin();
  bus.connected = false; // the wire "breaks" mid-mission
  Sample s[2];
  gauge.read(s, 2, 0);
  TEST_ASSERT_EQUAL_INT(asInt(Status::BUS_ERROR), asInt(s[0].status));
  TEST_ASSERT_EQUAL_INT(asInt(Status::BUS_ERROR), asInt(s[1].status));
}

int main() 
{
  UNITY_BEGIN();
  RUN_TEST(test_detects_real_chip);
  RUN_TEST(test_no_battery_reports_not_present);
  RUN_TEST(test_converts_voltage_and_charge);
  RUN_TEST(test_impossible_voltage_flagged);
  RUN_TEST(test_lost_bus_marks_bus_error);
  return UNITY_END();
}