#include <Arduino.h>
#include <Wire.h>
#include <unity.h>
#include "board_pins.h"
#include "drivers/arduino_i2c_bus.h"
#include "drivers/pcf8523.h"

// ON-BOARD test: Adalogger FeatherWing with a CR1220 coin cell fitted.
// Run with:  pio test -e board_test -f test_board_rtc

static ArduinoI2CBus bus(Wire);
static PCF8523 rtc(bus);

static int asInt(Status s) { return static_cast<int>(s); }

void setUp() {}
void tearDown() {}

void test_rtc_answers()
{
  TEST_ASSERT_EQUAL_INT_MESSAGE(asInt(Status::OK), asInt(rtc.begin()), "No answer at 0x68: Adalogger seated? SDA/SCL joints?");
}

void test_time_survived_power_off()
{
  RtcTime t;
  bool trustworthy = false;
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(rtc.readTime(t, trustworthy)));
  if (!trustworthy) // First run ever, or no coin cell during the last power-off.
  {
    RtcTime build;
    TEST_ASSERT_TRUE(parseBuildTime(__DATE__, __TIME__, build));
    TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(rtc.setTime(build)));
    TEST_IGNORE_MESSAGE("Time was lost, so it was just set. Unplug USB for a minute, then run again: this should PASS.");
  }
  Serial.printf("RTC time: %04u-%02u-%02u %02u:%02u:%02u\n", t.year, t.month, t.day, t.hour, t.minute, t.second);
}

void test_clock_ticks()
{
  RtcTime a, b;
  bool ok = false;
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(rtc.readTime(a, ok)));
  delay(2500);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(rtc.readTime(b, ok)));
  uint32_t elapsed = toSeconds2000(b) - toSeconds2000(a);
  TEST_ASSERT_TRUE_MESSAGE(elapsed >= 2 && elapsed <= 3, "Clock isn't ticking at 1 s per second");
}

void setup()
{
  delay(2000); // Give the serial monitor time to connect.
  pinMode(pins::STEMMA_POWER, OUTPUT);
  digitalWrite(pins::STEMMA_POWER, HIGH); // Same as main.cpp: power the I2C/STEMMA side.
  delay(10);
  bus.begin(pins::I2C0_SDA, pins::I2C0_SCL, 100000, 50);

  UNITY_BEGIN();
  RUN_TEST(test_rtc_answers);
  RUN_TEST(test_time_survived_power_off);
  RUN_TEST(test_clock_ticks);
  UNITY_END();
}

void loop() {}