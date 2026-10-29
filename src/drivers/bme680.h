#pragma once // Ensure the header file is included only once during compilation to prevent multiple definition errors.
#include <stdint.h> // Include the standard integer types header for fixed-width integer types.
#include <stddef.h> // Include the standard definitions header for size_t and NULL.
#include "i2c_bus.h" // Include the header file that defines the I2CBus class for I2C communication.
#include "sensor.h" // Include the header file that defines the Sensor class for sensor management.

class BME680 : public Sensor // Define the BME680 sensor class that inherits from the Sensor base class.
{
    public:
    using DelayFn = void (*)(uint32_t ms); // Define a type alias for a delay function that takes a time duration in milliseconds.

    static constexpr uint8_t kDefaultAddress = 0x77; // Define the default I2C address for the BME680 sensor.
    static constexpr uint8_t kSampleCount = 3; // The number of samples that read() writes so that callers can size their arrays.

    BME680(I2CBus& bus, DelayFn delayMs, uint8_t address = kDefaultAddress) // Constructor: the bus to talk through, the wait function, and the chip's I2C address.
        : bus_(bus), delayMs_(delayMs), address_(address) {}

    const char* name() const override { return "bme680"; } // Override the name function to return the name of the sensor as a string.
    Status begin() override; // Override the begin function to initialize the BME680 sensor.
    size_t read(Sample* out, size_t capacity, uint32_t MET_s) override; // Override the read function to read sensor data into the provided output array.

    private:
    struct Calibration
    {
        uint16_t t1 = 0; int16_t t2 = 0; int8_t t3 = 0; // Temperature calibration coefficients.
        uint16_t p1 = 0; int16_t p2 = 0; int8_t p3 = 0; int16_t p4 = 0; int16_t p5 = 0; int8_t p6 = 0; int8_t p7 = 0; int16_t p8 = 0; int16_t p9 = 0; uint8_t p10 = 0; // Pressure calibration coefficients.
        uint16_t h1 = 0; uint16_t h2 = 0; int8_t h3 = 0; int8_t h4 = 0; int8_t h5 = 0; uint8_t h6 = 0; int8_t h7 = 0; // Humidity calibration coefficients.
    };

    Status writeReg(uint8_t reg, uint8_t value); // Write a value to a specific register of the BME680 sensor.
    Status readCalibration(); // Read the calibration data from the BME680 sensor into cal_.
    Status configure(); // Configure the BME680 sensor with the appropriate settings.
    bool stillConfigured(); // Check if the BME680 sensor is still configured correctly.
    void fillAll(Sample* out, Status status, uint32_t MET_s); // Write 3 samples that all carry the same status (used for failures).

    float compensateTemperature(uint32_t adc, float& t_fine) const; // Compensate the raw temperature reading using the calibration coefficients.
    float compensatePressure(uint32_t adc, float t_fine) const; // Compensate the raw pressure reading using the calibration coefficients.
    float compensateHumidity(uint16_t adc, float tempC) const; // Convert the raw humidity reading using the calibration coefficients and temperature.

    I2CBus& bus_; // Reference to the I2C bus used to communicate with the BME680 sensor.
    DelayFn delayMs_; // Function pointer to the delay function used by the BME680 driver.
    uint8_t address_; // The I2C address of the BME680 sensor.
    bool present_ = false; // Flag indicating if the BME680 sensor is present and accessible.
    Calibration cal_; // Storage for the calibration coefficients read from the BME680 sensor.
};