#include "arduino_i2c_bus.h" // Include the header file that defines the ArduinoI2CBus class for I2C communication using the Arduino Wire library.

bool ArduinoI2CBus::begin(uint8_t sda, uint8_t scl, uint32_t hz, uint16_t timeout_ms) 
{
    if (!wire_.begin(sda, scl, hz))
    {
        return false; // Return false if the I2C bus initialization fails.
    }

    wire_.setTimeOut(timeout_ms); // Set the timeout for I2C transactions in milliseconds.
    return true; // Return true if the I2C bus initialization is successful.
}

bool ArduinoI2CBus::probe(uint8_t addr) 
{
    wire_.beginTransmission(addr); // Begin an I2C transmission to the specified address.
    return wire_.endTransmission() == 0; // Return true if the device at the specified address responds, indicating its presence.
}

Status ArduinoI2CBus::readRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) 
{
    if (out == nullptr || len == 0 || len > 32)
    {
        return Status::BUS_ERROR; // Return BUS_ERROR if the output buffer is null, length is zero, or length exceeds 32 bytes.
    }

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) 
    {
        wire_.beginTransmission(addr); // Begin an I2C transmission to the specified address.
        wire_.write(reg); // Write the register address to read from.

        if (wire_.endTransmission(false) == 0) 
        {
            uint8_t got = wire_.requestFrom(addr, static_cast<uint8_t>(len)); // Request 'len' bytes from the device at the specified address.
            if (got == len) 
            {
                for (size_t i = 0; i < len; ++i) 
                {
                    out[i] = static_cast<uint8_t>(wire_.read());
                }
                return Status::OK; // Return OK if the read operation is successful.
            }
        }

        delay(2); // Delay for 2 milliseconds before retrying the read operation.
    }

    return Status::BUS_ERROR; // Return BUS_ERROR if the read operation fails after the maximum number of attempts.
}

Status ArduinoI2CBus::writeRegs(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) 
{
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) 
    {
        wire_.beginTransmission(addr); // Begin an I2C transmission to the specified address.
        wire_.write(reg); // Write the register address to write to.
        wire_.write(data, len); // Write 'len' bytes of data to the device.

        if (wire_.endTransmission() == 0) 
        {
            return Status::OK; // Return OK if the write operation is successful.
        }

        delay(2); // Delay for 2 milliseconds before retrying the write operation.
    }
}