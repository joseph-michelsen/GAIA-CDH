#include "max17048.h" // Include the header file that defines the Max17048 class for the MAX17048 fuel gauge sensor.

namespace 
{
constexpr uint8_t kAddress = 0x36; // Define the I2C address of the MAX17048 fuel gauge sensor.
constexpr uint8_t kRegVcell = 0x02; // Define the register address for reading the battery voltage (Vcell) from the MAX17048 sensor.
constexpr uint8_t kRegSoc = 0x04; // Define the register address for reading the state of charge (SOC) from the MAX17048 sensor.
constexpr uint8_t kRegVersion = 0x08; // Define the register address for reading the version information from the MAX17048 sensor.

constexpr float kVoltsPerBit = 78.125e-6f; // Define the conversion factor for converting raw register values to volts. Each bit represents 78.125 microvolts (0.000078125 volts).
constexpr float kPercentPerBit = 1.0f / 256.0f; // Define the conversion factor for converting raw register values to percentage. Each bit represents 1/256 of a percent (approximately 0.00390625%).

constexpr float kMinVolts = 2.5f; // Define the minimum acceptable voltage for the battery. If the measured voltage is below this threshold, it may indicate a problem with the battery or sensor.
constexpr float kMaxVolts = 4.5f; // Define the maximum acceptable voltage for the battery. If the measured voltage is above this threshold, it may indicate a problem with the battery or sensor.
constexpr float kMaxPercent = 110.0f; // Define the maximum acceptable state of charge (SOC) percentage. If the measured SOC is above this threshold, it may indicate a problem with the battery or sensor.
} 

Status Max17048::readWord(uint8_t reg, uint16_t& value) // Define a member function of the Max17048 class to read a 16-bit word from the specified register of the MAX17048 sensor. The function takes the register address (reg) and a reference to a uint16_t variable (value) to store the read value.
{
  uint8_t buf[2] = {0, 0}; // Create a buffer of 2 bytes to hold the raw data read from the sensor register.
  Status s = bus_.readRegs(kAddress, reg, buf, 2); // Use the I2CBus object (bus_) to read 2 bytes from the specified register (reg) of the MAX17048 sensor at the defined I2C address (kAddress). The read data is stored in the buf array, and the status of the operation is returned in the variable s.
  
  if (s == Status::OK) 
  {
    value = static_cast<uint16_t>((buf[0] << 8) | buf[1]); // If the read operation is successful (s == Status::OK), combine the two bytes in the buf array into a single 16-bit value. The first byte (buf[0]) is shifted left by 8 bits and combined with the second byte (buf[1]) using a bitwise OR operation. The resulting 16-bit value is stored in the variable value.
  }

  return s; // Return the status of the read operation (s) to indicate whether the read was successful or if an error occurred.
}

Status Max17048::begin() 
{
  uint16_t version = 0; // Declare a variable to hold the version information read from the MAX17048 sensor. This variable will be used to determine if the sensor is present and functioning correctly.
  Status s = readWord(kRegVersion, version); // Call the readWord function to read the version information from the MAX17048 sensor by specifying the register address (kRegVersion) and passing the version variable by reference. The status of the read operation is stored in the variable s.
 
  present_ = (s == Status::OK) && ((version & 0xFFF0) == 0x0010); // Check if the read operation was successful (s == Status::OK) and if the version information matches the expected value for the MAX17048 sensor. The version is masked with 0xFFF0 to ignore the lower 4 bits, and it is compared to 0x0010, which represents the expected version. If both conditions are met, set the present_ member variable to true, indicating that the sensor is present and functioning correctly; otherwise, set it to false.
  return present_ ? Status::OK : Status::NOT_PRESENT; // Return Status::OK if the sensor is present and functioning correctly; otherwise, return Status::NOT_PRESENT to indicate that the sensor is not detected or not functioning properly.
}

size_t Max17048::read(Sample* out, size_t capacity, uint32_t MET_s) 
{
  if (out == nullptr || capacity < 2) 
  {
    return 0; // If the output buffer (out) is null or the capacity is less than 2, return 0 to indicate that no samples can be read due to insufficient buffer space or invalid input.
  }

  if (!present_) 
  {
    begin(); // If the sensor is not marked as present (present_ is false), call the begin() function to attempt to initialize the sensor and check for its presence. This ensures that the sensor is properly initialized before attempting to read data from it.
  }

  if (!present_) 
  {
    out[0] = makeSample(ParamID::BATT_VOLTAGE, 0.0f, Status::NOT_PRESENT, MET_s); // If the sensor is still not present after attempting to initialize it, create a sample for the battery voltage parameter (ParamID::BATT_VOLTAGE) with a value of 0.0, a status of Status::NOT_PRESENT, and the provided Mission Elapsed Time (MET_s). This indicates that the battery voltage reading is not available due to the sensor not being detected.
    out[1] = makeSample(ParamID::BATT_SOC, 0.0f, Status::NOT_PRESENT, MET_s); // Similarly, create a sample for the battery state of charge parameter (ParamID::BATT_SOC) with a value of 0.0, a status of Status::NOT_PRESENT, and the provided Mission Elapsed Time (MET_s). This indicates that the battery SOC reading is not available due to the sensor not being detected.
    return 2; // Return 2 to indicate that two samples (battery voltage and battery SOC) have been created, even though they are marked as not present due to the sensor not being detected.
  }

  uint16_t raw_v = 0; // Declare a variable to hold the raw battery voltage value read from the MAX17048 sensor. This variable will be used to store the 16-bit value read from the Vcell register.
  uint16_t raw_soc = 0; // Declare a variable to hold the raw state of charge (SOC) value read from the MAX17048 sensor. This variable will be used to store the 16-bit value read from the SOC register.
  Status sv = readWord(kRegVcell, raw_v); // Call the readWord function to read the raw battery voltage value from the MAX17048 sensor by specifying the register address (kRegVcell) and passing the raw_v variable by reference. The status of the read operation is stored in the variable sv.
  Status ss = readWord(kRegSoc, raw_soc); // Call the readWord function to read the raw state of charge (SOC) value from the MAX17048 sensor by specifying the register address (kRegSoc) and passing the raw_soc variable by reference. The status of the read operation is stored in the variable ss.

  float volts = raw_v * kVoltsPerBit; // Convert the raw battery voltage value (raw_v) to volts by multiplying it by the conversion factor (kVoltsPerBit). This gives the actual battery voltage in volts, which is stored in the variable volts.
  float percent = raw_soc * kPercentPerBit; // Convert the raw state of charge (SOC) value (raw_soc) to percentage by multiplying it by the conversion factor (kPercentPerBit). This gives the actual battery SOC in percentage, which is stored in the variable percent.

  if (sv == Status::OK && (volts < kMinVolts || volts > kMaxVolts)) 
  {
    sv = Status::OUT_OF_RANGE; // If the battery voltage reading is successful (sv == Status::OK) but the measured voltage (volts) is outside the acceptable range defined by kMinVolts and kMaxVolts, set the status sv to Status::OUT_OF_RANGE to indicate that the battery voltage is not within the expected limits.
  }

  if (ss == Status::OK && percent > kMaxPercent) 
  {
    ss = Status::OUT_OF_RANGE; // If the state of charge (SOC) reading is successful (ss == Status::OK) but the measured SOC (percent) exceeds the maximum acceptable percentage defined by kMaxPercent, set the status ss to Status::OUT_OF_RANGE to indicate that the battery SOC is not within the expected limits.
  }

  out[0] = makeSample(ParamID::BATT_VOLTAGE, volts, sv, MET_s); // Create a sample for the battery voltage parameter (ParamID::BATT_VOLTAGE) with the measured voltage value (volts), the status of the voltage reading (sv), and the provided Mission Elapsed Time (MET_s). This sample is stored in the first element of the output buffer (out[0]).
  out[1] = makeSample(ParamID::BATT_SOC, percent, ss, MET_s); // Create a sample for the battery state of charge parameter (ParamID::BATT_SOC) with the measured SOC value (percent), the status of the SOC reading (ss), and the provided Mission Elapsed Time (MET_s). This sample is stored in the second element of the output buffer (out[1]).
  return 2; // Return 2 to indicate that two samples (battery voltage and battery SOC) have been created and stored in the output buffer (out).
}