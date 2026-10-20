#include <Arduino.h> // Include the Arduino core library for ESP32 development.
#include <Wire.h> // Include the Wire library for I2C communication.
#include "board_pins.h" // Include the header file that defines the pin mappings for the board.
#include "drivers/arduino_i2c_bus.h" // Include the header file that defines the ArduinoI2CBus class for I2C communication using the Arduino Wire library.

#ifdef HAS_DEBUG_DISPLAY // Check if the HAS_DEBUG_DISPLAY macro is defined to include the display-related libraries and code.
#include <Adafruit_GFX.h> // Include the Adafruit GFX library for graphics support.
#include <Adafruit_ST7789.h> // Include the Adafruit ST7789 library for display support.
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST); 
#endif

static ArduinoI2CBus bus0(Wire); // Create an instance of the ArduinoI2CBus class for I2C communication using the Wire library.

static void scanBus()
{
  Serial.println("I2C Bus 0 Scan:"); 

  #ifdef HAS_DEBUG_DISPLAY
  tft.fillScreen(ST77XX_BLACK); // Fill the display with a black background.
  tft.setCursor(0, 0); // Set the cursor position to (0, 0) for text output.
  tft.setTextSize(2); // Set the text size to 2 for smaller text.
  tft.setTextColor(ST77XX_WHITE); // Set the text color to white.
  tft.println("I2C Bus 0: "); // Print a message to the TFT display.
  #endif

  int found = 0; // Initialize a counter for the number of devices found on the I2C bus.
  
  for (uint8_t addr = 1; addr < 127; addr++)
  {
    if (bus0.probe(addr)) // Check if a device is present at the current I2C address.
    {
      found++; // Increment the counter for the number of devices found.
      Serial.printf(" found 0x%02X\n", addr); // Print the address of the found device to the serial monitor.
      #ifdef HAS_DEBUG_DISPLAY
      tft.printf(" found 0x%02X\n", addr); // Print the address of the found device to the TFT display.
      #endif
    }
  }

  if (found == 0) // Check if no devices were found on the I2C bus.
  {
    Serial.println(" found nothing"); // Print a message to the serial monitor indicating that no devices were found.
    #ifdef HAS_DEBUG_DISPLAY
    tft.println(" found nothing"); // Print a message to the TFT display indicating that no devices were found.
    #endif
  }
}

void setup() 
{
  Serial.begin(115200); // Initialize the serial communication at a baud rate of 115200.
  delay(2000); // Wait for 2 seconds to allow the serial monitor to initialize.
  Serial.println("CDH boot"); // Print a boot message to the serial monitor.

  pinMode(pins::STEMMA_POWER, OUTPUT); // Set the STEMMA_POWER pin as an output pin.
  digitalWrite(pins::STEMMA_POWER, HIGH); // Set the STEMMA_POWER pin to HIGH to enable power to the STEMMA devices.
  delay(10); // Wait for 10 milliseconds to allow the STEMMA devices to power up.

  #ifdef HAS_DEBUG_DISPLAY
  pinMode(TFT_BACKLITE, OUTPUT); // Set the TFT_BACKLIGHT pin as an output pin.
  digitalWrite(TFT_BACKLITE, HIGH); // Set the TFT_BACKLIGHT pin to HIGH to enable the backlight of the TFT display.
  tft.init(135, 240); // Initialize the TFT display with a width of 135 pixels and a height of 240 pixels.
  tft.setRotation(3); // Set the rotation of the TFT display to 3 (landscape mode).
  #endif

  if (!bus0.begin(pins::I2C0_SDA, pins::I2C0_SCL, 100000, 50)) // Initialize the I2C bus with the specified SDA and SCL pins, clock frequency of 100 kHz, and timeout of 50 ms. If initialization fails, print an error message and halt execution.
  {
    Serial.println("Failed to initialize I2C bus"); // Print an error message to the serial monitor if the I2C bus initialization fails.
  }
}

void loop() 
{
  scanBus(); // Call the scanBus function to scan the I2C bus for connected devices.
  delay(5000); // Wait for 5 seconds before scanning the I2C bus again.
}