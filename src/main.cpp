#include <Arduino.h> // Include the Arduino core library for ESP32 development.

#ifdef HAS_DEBUG_DISPLAY 
#include <Adafruit_GFX.h> // Include the Adafruit GFX library for graphics support.
#include <Adafruit_ST7789.h> // Include the Adafruit ST7789 library for display support.
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST); 
#endif

static uint32_t seconds = 0; // Initialize a static variable to keep track of elapsed seconds.

void setup() 
{
  Serial.begin(115200); // Initialize the serial communication at a baud rate of 115200.
  delay(2000); // Wait for 2 seconds to allow the serial monitor to initialize.
  Serial.println("CDH boot"); // Print a boot message to the serial monitor.

  #ifdef HAS_DEBUG_DISPLAY
  pinMode(TFT_I2C_POWER, OUTPUT); // Set the TFT_I2C_POWER pin as an output.
  digitalWrite(TFT_I2C_POWER, HIGH); // Turn on the power to the TFT display.
  pinMode(TFT_BACKLITE, OUTPUT); // Set the TFT_BACKLIGHT pin as an output.
  digitalWrite(TFT_BACKLITE, HIGH); // Turn on the backlight of the TFT display.
  delay(100); // Wait for 100 milliseconds to allow the display to power up.

  tft.init(135, 240); // Initialize the TFT display with a resolution of 135x240 pixels.)
  tft.setRotation(3); // Set the rotation of the display to 3 (landscape mode).
  tft.fillScreen(ST77XX_BLACK); // Fill the display with a black background.
  tft.setTextSize(3); // Set the text size to 3 for larger text.
  tft.setTextColor(ST77XX_WHITE); // Set the text color to white.
  tft.setCursor(10, 10); // Set the cursor position to (10, 10) for text output.
  tft.println("CDH boot"); // Print a boot message to the TFT display.
  #endif
}

void loop() 
{
  Serial.println("alive");
  
  #ifdef HAS_DEBUG_DISPLAY
  tft.setTextSize(2); // Set the text size to 2 for smaller text.
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK); // Set the text color to green with a black background.
  tft.setCursor(10, 60); // Set the cursor position to (10, 50) for text output.
  tft.print("Alive for "); 
  tft.print(seconds);
  tft.print(" s    ");
  #endif

  seconds++; // Increment the seconds counter.
  delay(1000); // Wait for 1 second before the next loop iteration.
}