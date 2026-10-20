#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.

namespace pins
{
    constexpr uint8_t I2C0_SDA = 3;
    constexpr uint8_t I2C0_SCL = 4;
    constexpr uint8_t I2C1_SDA = 5;
    constexpr uint8_t I2C1_SCL = 6;

    constexpr uint8_t SPI_MOSI = 35;
    constexpr uint8_t SPI_SCK = 36;
    constexpr uint8_t SPI_MISO = 37;

    constexpr uint8_t STEMMA_POWER = 7; 
    constexpr uint8_t PWM_OE = 14;
}