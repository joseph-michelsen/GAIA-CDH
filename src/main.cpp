// FLIGHT SOFTWARE entry point: builds every object once, then runs the flight loop every 5 s.
// This file is only "glue": it owns the hardware objects and decides WHEN things happen.
// WHAT happens lives in app/ (flight loop, rules) and services/ (clock, logger, averager, state table).
// The old sensor-test sketch is now src/bench/bench_main.cpp (pio run -e bench).

#include <Arduino.h> // Include the Arduino core library for ESP32 development.
#include <Wire.h> // Include the Wire library for I2C communication.
#include <SPI.h> // Include the SPI library (the SD card is an SPI device).
#include "board_pins.h" // Include the pin names for this board.
#include "drivers/arduino_i2c_bus.h" // I2C bus adapter.
#include "drivers/sd_card.h" // SD card (Storage).
#include "drivers/pcf8523.h" // Real-time clock.
#include "drivers/max17048.h" // Battery fuel gauge.
#include "drivers/bme680.h" // Air temperature, humidity, pressure.
#include "drivers/scd30.h" // CO2.
#include "services/mission_clock.h" // MET_s.
#include "services/data_logger.h" // CSV day files on the SD card.
#include "services/averager.h" // 1-minute averages (for the radio, later).
#include "services/state_table.h" // Latest value of each parameter.
#include "app/flight_loop.h" // sense -> timestamp -> store, once per cycle.
#include "app/rules.h" // Cross-sensor rules.

// ---------- Hardware ----------

static ArduinoI2CBus bus0(Wire); // I2C bus 0 (STEMMA QT): every sensor and the RTC.
static SdCard card(SPI, pins::SD_CS); // microSD on the Adalogger FeatherWing.
static PCF8523 rtc(bus0); // Real-time clock on the Adalogger FeatherWing (0x68).

static MAX17048 battery(bus0); // 0x36
static BME680 environment(bus0, delay); // 0x77
static SCD30 co2Sensor(bus0, delay); // 0x61

// ---------- Services ----------

static uint32_t boardMillis() { return millis(); } // MissionClock needs a plain function, not Arduino's overloaded millis().
static MissionClock missionClock(rtc, card, boardMillis);
static DataLogger logger(card);
static Averager averager;
static StateTable state;

// ---------- Sensor table: how often each sensor is read ----------
// Periods are multiples of FlightLoop::kCyclePeriod_s (5 s). Rationale for each rate: see the "Sensor rates" wiki page.

static constexpr uint16_t every(uint32_t seconds) { return static_cast<uint16_t>(seconds / FlightLoop::kCyclePeriod_s); }

static SensorSlot sensorTable[] = {
  {&environment, every(10)}, // AIR_TEMP, HUMIDITY, PRESSURE: fastest air dynamics; pressure also feeds the SCD30.
  {&co2Sensor, every(10)}, // CO2: changes quickly with the light cycle.
  {&battery, every(0)}, // BATT_VOLTAGE, BATT_SOC: slow; telemetry and fault protection.
};
static constexpr size_t kSensorCount = sizeof(sensorTable) / sizeof(sensorTable[0]); // Counts the table, so adding a row needs no other change.

static FlightLoop flightLoop(sensorTable, kSensorCount, missionClock, logger, averager, state);

// ---------- Helpers ----------

static const char* clockSourceName(ClockSource s) { return s == ClockSource::RTC ? "RTC" : "ESTIMATED"; }

static void applyRules() // Cross-sensor rules, run after each cycle (see app/rules.h).
{
  float pressurePa = 0.0f;
  if (rules::co2PressureCompensation(state, flightLoop.lastMet(), pressurePa))
  {
    co2Sensor.setAmbientPressure(pressurePa); // The driver only talks to the sensor if the pressure changed by 2 mbar or more.
  }
}

static void reportCycle(Status logStatus) // Serial: one line per cycle, for bench testing and the soak test.
{
  Serial.printf("MET %lu s [%s] log=%s rows=%lu dropped=%lu emptyReads=%lu\n",
                static_cast<unsigned long>(flightLoop.lastMet()), clockSourceName(missionClock.source()), statusName(logStatus),
                static_cast<unsigned long>(logger.rowsWritten()), static_cast<unsigned long>(logger.rowsDropped()),
                static_cast<unsigned long>(flightLoop.readsDropped()));
}

static void reportAverages() // Serial: a finished 1-minute window. Stands in for the radio until the LoRa step.
{
  Average out[Averager::kParamCount];
  uint32_t start_s = 0;
  size_t n = averager.takeCompleted(out, Averager::kParamCount, start_s);
  Serial.printf("  1-min averages from MET %lu s:\n", static_cast<unsigned long>(start_s));
  for (size_t i = 0; i < n; ++i)
  {
    Serial.printf("    %-12s %10.2f  %-12s %u/%u ok\n", paramName(out[i].id), out[i].mean, statusName(out[i].status), out[i].okCount, out[i].sampleCount);
  }
}

// ---------- Arduino entry points ----------

static constexpr uint32_t kCyclePeriod_ms = FlightLoop::kCyclePeriod_s * 1000u;
static uint32_t nextCycle_ms = 0; // When the next cycle is due (millis() time).

void setup()
{
  Serial.begin(115200);
  delay(2000); // Give a serial monitor time to connect (harmless with no USB attached).
  Serial.println("CDH boot: flight software");

  pinMode(pins::STEMMA_POWER, OUTPUT);
  digitalWrite(pins::STEMMA_POWER, HIGH); // Power the STEMMA QT / I2C side.
  delay(10);

#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH); // Reverse TFT boards only: keep the display deselected so it ignores the shared SPI bus.
#endif

  if (!bus0.begin(pins::I2C0_SDA, pins::I2C0_SCL, 100000, 50)) // 100 kHz: the SCD30's maximum.
  {
    Serial.println("I2C bus failed to start"); // Keep going: every sensor will report BUS_ERROR, and that gets logged.
  }
  SPI.begin(pins::SPI_SCK, pins::SPI_MISO, pins::SPI_MOSI); // Shared SPI bus (SD card now; camera and radio later).

  // Order matters: the clock mounts the card and finds the mission's MET before any sample is stamped.
  Status cs = missionClock.begin();
  Serial.printf("mission clock: %s, source %s, MET %lu s\n", statusName(cs), clockSourceName(missionClock.source()),
                static_cast<unsigned long>(missionClock.now()));
  Serial.printf("sensors begin: %s\n", statusName(flightLoop.begin())); // OK, or the first problem (that sensor retries itself).

  nextCycle_ms = millis(); // First cycle right away.
}

void loop()
{
  const uint32_t now_ms = millis();
  if (static_cast<int32_t>(now_ms - nextCycle_ms) < 0)
  {
    return; // Not time yet. (Signed difference: still correct when millis() wraps after ~49.7 days.)
  }

  nextCycle_ms += kCyclePeriod_ms; // Fixed schedule: a slow cycle doesn't push every later cycle back.
  if (static_cast<int32_t>(now_ms - nextCycle_ms) >= 0)
  {
    nextCycle_ms = now_ms + kCyclePeriod_ms; // Fell a whole cycle behind (e.g. a very slow card): skip ahead instead of running a burst of catch-up cycles.
  }

  Status logStatus = flightLoop.runCycle();
  applyRules();
  reportCycle(logStatus);
  if (averager.hasCompleted())
  {
    reportAverages();
  }
}
