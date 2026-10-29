#include <Arduino.h> // Include the Arduino core library for ESP32 development.
#include <Wire.h> // Include the Wire library for I2C communication.
#include "board_pins.h" // Include the header file that defines the pin mappings for the board.
#include "drivers/arduino_i2c_bus.h" // Include the header file that defines the ArduinoI2CBus class for I2C communication using the Arduino Wire library.
#include "drivers/max17048.h" // Include the header file that defines the Max17048 class for battery fuel gauge functionality.
#include "drivers/bme680.h" // Include the header file that defines the Bme680 class for the temperature/humidity/pressure sensor.
 
#ifdef HAS_DEBUG_DISPLAY // Check if the HAS_DEBUG_DISPLAY macro is defined to include the display-related libraries and code.
#include <Adafruit_GFX.h> // Include the Adafruit GFX library for graphics support.
#include <Adafruit_ST7789.h> // Include the Adafruit ST7789 library for display support.
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST); // The Reverse TFT's built-in 240x135 display.
#endif
 
static ArduinoI2CBus bus0(Wire); // Create an instance of the ArduinoI2CBus class for I2C communication using the Wire library.
static Max17048 battery(bus0); // Create an instance of the Max17048 class for battery fuel gauge functionality using the ArduinoI2CBus instance bus0.
static BME680 environment(bus0, delay); // BME680 on bus 0 (STEMMA QT, address 0x77). It uses Arduino's delay() to wait while it measures.
 
static constexpr size_t kMaxDevices = 16; // Most I2C addresses we remember from one scan (far more than we expect).
static uint8_t foundAddrs[kMaxDevices]; // Addresses that answered during the latest scan.
static size_t foundCount = 0; // How many addresses answered (can be more than kMaxDevices; only the first kMaxDevices are stored).
 
 
static const char* deviceName(uint8_t addr) // What we expect to find at each I2C address.
{
  switch (addr)
  {
    case 0x36: return "MAX17048 battery gauge (on the Feather)";
    case 0x68: return "PCF8523 real-time clock (Adalogger)";
    case 0x76: return "BME680 env sensor (SDO tied low)";
    case 0x77: return "BME680 env sensor";
    default: return "unknown device";
  }
}
 
static const char* labelOf(ParamID id) // Short label for the display (max 4 characters).
{
  switch (id)
  {
    case ParamID::BATT_VOLTAGE: return "Batt";
    case ParamID::BATT_SOC: return "SOC";
    case ParamID::AIR_TEMP: return "Temp";
    case ParamID::HUMIDITY: return "RH";
    case ParamID::PRESSURE: return "Pres";
    default: return "?";
  }
}
 
static const char* unitOf(ParamID id) // Engineering unit of each parameter.
{
  switch (id)
  {
    case ParamID::BATT_VOLTAGE: return "V";
    case ParamID::BATT_SOC: return "%";
    case ParamID::AIR_TEMP: return "C";
    case ParamID::HUMIDITY: return "%";
    case ParamID::PRESSURE: return "Pa";
    default: return "";
  }
}
 
static int decimalsOf(ParamID id) // How many decimal places are worth showing.
{
  switch (id)
  {
    case ParamID::BATT_VOLTAGE: return 2; // 3.70 V
    case ParamID::PRESSURE: return 0; // 83500 Pa
    default: return 1; // 22.4 C, 30.1 %
  }
}
 
static void scanBus() // Probe every I2C address and remember which ones answered.
{
  foundCount = 0;
  for (uint8_t addr = 1; addr < 127; addr++)
  {
    if (bus0.probe(addr)) // Check if a device is present at the current I2C address.
    {
      if (foundCount < kMaxDevices)
      {
        foundAddrs[foundCount] = addr; // Remember it.
      }
      foundCount++;
    }
  }
}
 
static void printScan() // Serial: every address found, with what we think it is.
{
  Serial.printf("I2C bus 0: %u device(s)\n", static_cast<unsigned>(foundCount));
  for (size_t i = 0; i < foundCount && i < kMaxDevices; i++)
  {
    Serial.printf("  0x%02X  %s\n", foundAddrs[i], deviceName(foundAddrs[i]));
  }
}
 
static void printSamples(const Sensor& sensor, const Sample* s, size_t n) // Serial: one line per reading.
{
  for (size_t i = 0; i < n; i++)
  {
    Serial.printf("  %-8s %-4s = %.*f %s [%s]\n", sensor.name(), labelOf(s[i].id), decimalsOf(s[i].id),
                  s[i].value, unitOf(s[i].id), statusName(s[i].status));
  }
}
 
// ---------- Display (Reverse TFT only) ----------
 
#ifdef HAS_DEBUG_DISPLAY
static constexpr int kLineHeight = 16; // Text size 2 = 16 pixels tall, 12 wide: 8 lines of 20 characters on a 240x135 screen.
static constexpr int kColumns = 20;
 
static void drawLine(int row, uint16_t color, const char* text) // Draw one full-width line without clearing the screen (no flicker).
{
  char padded[kColumns + 1]; // Text padded with spaces so it overwrites whatever was there before.
  snprintf(padded, sizeof(padded), "%-20s", text);
  tft.setCursor(0, row * kLineHeight);
  tft.setTextColor(color, ST77XX_BLACK); // Drawing with a background color erases the old characters.
  tft.print(padded);
}
 
static void drawScan(int row) // e.g. "I2C(2): 36 77"
{
  char line[kColumns + 1];
  if (foundCount == 0)
  {
    drawLine(row, ST77XX_RED, "I2C: none found");
    return;
  }
  int pos = snprintf(line, sizeof(line), "I2C(%u):", static_cast<unsigned>(foundCount));
  for (size_t i = 0; i < foundCount && i < kMaxDevices && pos + 3 <= kColumns; i++)
  {
    pos += snprintf(line + pos, sizeof(line) - static_cast<size_t>(pos), " %02X", foundAddrs[i]); // Addresses in hex, without "0x" to save space.
  }
  drawLine(row, ST77XX_CYAN, line);
}
 
static void drawSample(int row, const Sample& s) // Green = OK, yellow = out of range (value still shown), red = no data.
{
  char line[kColumns + 1];
  uint16_t color = ST77XX_GREEN;
  if (s.status == Status::OK || s.status == Status::OUT_OF_RANGE)
  {
    snprintf(line, sizeof(line), "%-5s%.*f %s%s", labelOf(s.id), decimalsOf(s.id), s.value, unitOf(s.id),
             s.status == Status::OUT_OF_RANGE ? " !" : "");
    if (s.status == Status::OUT_OF_RANGE)
    {
      color = ST77XX_YELLOW;
    }
  }
  else
  {
    snprintf(line, sizeof(line), "%-5s%s", labelOf(s.id), statusName(s.status)); // e.g. "Temp BUS_ERROR"
    color = ST77XX_RED;
  }
  drawLine(row, color, line);
}
 
static void drawSamples(int firstRow, const Sample* s, size_t n, size_t expected) // Draw each sample on its own row; blank any missing rows.
{
  for (size_t i = 0; i < expected; i++)
  {
    if (i < n)
    {
      drawSample(firstRow + static_cast<int>(i), s[i]);
    }
    else
    {
      drawLine(firstRow + static_cast<int>(i), ST77XX_RED, "--- no data ---");
    }
  }
}
#endif
 
// ---------- Arduino entry points ----------
 
void setup()
{
  Serial.begin(115200); // Initialize the serial communication at a baud rate of 115200.
  delay(2000); // Wait for 2 seconds to allow the serial monitor to initialize.
  Serial.println("CDH boot: sensor test"); // Print a boot message to the serial monitor.
 
  pinMode(pins::STEMMA_POWER, OUTPUT); // Set the STEMMA_POWER pin as an output pin.
  digitalWrite(pins::STEMMA_POWER, HIGH); // Set the STEMMA_POWER pin to HIGH to enable power to the STEMMA devices (and the display).
  delay(10); // Wait for 10 milliseconds to allow the STEMMA devices to power up.
 
  #ifdef HAS_DEBUG_DISPLAY
  pinMode(TFT_BACKLITE, OUTPUT); // Set the TFT_BACKLIGHT pin as an output pin.
  digitalWrite(TFT_BACKLITE, HIGH); // Set the TFT_BACKLIGHT pin to HIGH to enable the backlight of the TFT display.
  tft.init(135, 240); // Initialize the TFT display with a width of 135 pixels and a height of 240 pixels.
  tft.setRotation(3); // Set the rotation of the TFT display to 3 (landscape mode).
  tft.fillScreen(ST77XX_BLACK); // Clear the screen once; after this, lines are overwritten in place.
  tft.setTextSize(2); // 12x16-pixel characters.
  tft.setTextWrap(false); // Never wrap long lines onto the next row.
  #endif
 
  if (!bus0.begin(pins::I2C0_SDA, pins::I2C0_SCL, 100000, 50)) // Initialize the I2C bus: SDA/SCL pins, 100 kHz, 50 ms timeout.
  {
    Serial.println("Failed to initialize I2C bus"); // Report the failure and keep going (the scan will show nothing).
  }
 
  scanBus(); // Find everything on the bus.
  printScan(); // Expect 0x36 (battery gauge) and 0x77 (BME680).
  Serial.printf("max17048 begin: %s\n", statusName(battery.begin())); // Initialize the battery gauge and print the result.
  Serial.printf("bme680 begin: %s\n", statusName(environment.begin())); // Initialize the BME680 and print the result.
}
 
void loop()
{
  static uint32_t MET_s = 0; // Mission Elapsed Time in seconds (a simple counter for now).
 
  scanBus(); // Re-scan every cycle so you can plug and unplug boards and watch the list change.
 
  Sample bat[2]; // Battery voltage and state of charge.
  size_t nBat = battery.read(bat, 2, MET_s); // Returns the number of samples written.
 
  Sample env[BME680::kSampleCount]; // Air temperature, humidity, pressure.
  size_t nEnv = environment.read(env, BME680::kSampleCount, MET_s); // Takes about 45 ms.
 
  Serial.printf("--- MET %lu s ---\n", static_cast<unsigned long>(MET_s));
  printScan();
  printSamples(battery, bat, nBat);
  printSamples(environment, env, nEnv);
 
  #ifdef HAS_DEBUG_DISPLAY
  drawScan(0); // Row 0: I2C addresses found.
  drawSamples(1, bat, nBat, 2); // Rows 1-2: battery voltage, state of charge.
  drawSamples(3, env, nEnv, BME680::kSampleCount); // Rows 3-5: temperature, humidity, pressure.
  char metLine[kColumns + 1];
  snprintf(metLine, sizeof(metLine), "MET %lu s", static_cast<unsigned long>(MET_s));
  drawLine(6, ST77XX_WHITE, metLine); // Row 6: time since boot.
  #endif
 
  MET_s += 2;
  delay(2000);
}
