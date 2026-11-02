#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <SPI.h> // Include the SPI library (the SD card is an SPI device).
#include "storage.h" // Include the header file that defines the Storage contract this class fills in.

// The real Storage: the microSD card on the Adafruit Adalogger FeatherWing, using the ESP32 Arduino SD library (FAT16/FAT32).
// Like ArduinoI2CBus, this is a thin hardware adapter: all the "thinking" lives in the services that use Storage.

class SdCard : public Storage // Define a class named SdCard that inherits from the Storage class.
{
    public:
    SdCard(SPIClass& spi, uint8_t csPin) : spi_(spi), csPin_(csPin) {} // Constructor: which SPI bus, and which pin selects the card (pins::SD_CS = 10).

    Status begin() override; // Mount the card. NOT_PRESENT if there is no card (or it isn't FAT formatted).
    bool exists(const char* path) override; // True if the file exists.
    Status append(const char* path, const uint8_t* data, size_t len) override; // Add bytes to the end of a file (created if needed); on the card when OK.
    Status writeFile(const char* path, const uint8_t* data, size_t len) override; // Replace a whole file; on the card when OK.
    Status readFile(const char* path, uint8_t* out, size_t capacity, size_t& got) override; // Read a whole file into out.
    Status freeBytes(uint64_t& bytes) override; // Space left on the card.

    private:
    static constexpr uint32_t kSpiHz = 4000000; // 4 MHz: the ESP32 SD library's default speed, reliable on short wires and shared with the TFT.

    bool ensureMounted(); // Mount the card if it isn't already (lets the system recover after a card is reseated).
    Status writeOut(const char* path, const char* mode, const uint8_t* data, size_t len); // Shared code for append() and writeFile().

    SPIClass& spi_; // The SPI bus the card is on.
    uint8_t csPin_; // The card's chip-select pin.
    bool mounted_ = false; // True while the card is mounted and working.
};