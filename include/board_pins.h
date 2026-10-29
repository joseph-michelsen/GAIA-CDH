#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.

namespace pins
{
    constexpr uint8_t I2C0_SDA = 3; // Define the pin number for the primary I2C bus SDA (data) line.
    constexpr uint8_t I2C0_SCL = 4; // Define the pin number for the primary I2C bus SCL (clock) line.
    constexpr uint8_t I2C1_SDA = 5; // Define the pin number for the secondary I2C bus SDA (data) line.
    constexpr uint8_t I2C1_SCL = 6; // Define the pin number for the secondary I2C bus SCL (clock) line.
 
    constexpr uint8_t SPI_MOSI = 35; // Define the pin number for the SPI bus MOSI (Master Out Slave In) line.
    constexpr uint8_t SPI_SCK = 36; // Define the pin number for the SPI bus SCK (Serial Clock) line.
    constexpr uint8_t SPI_MISO = 37; // Define the pin number for the SPI bus MISO (Master In Slave Out) line.
    constexpr uint8_t SD_CS = 10; // Define the pin number for the Adalogger FeatherWing SD card chip select (Feather pin D10 = GPIO 10).

    constexpr uint8_t STEMMA_POWER = 7; // Define the pin number for controlling power to STEMMA devices (I2C sensors and peripherals).
    constexpr uint8_t PWM_OE = 14; // Define the pin number for controlling the output enable (OE) of a PWM (Pulse Width Modulation) signal.
}