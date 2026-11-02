#include "sd_card.h" // Include the header file that defines the SdCard class.
#include <SD.h> // Include the ESP32 SD card library.

Status SdCard::begin()
{
  SD.end(); // Start clean (harmless if nothing was mounted).
  mounted_ = SD.begin(csPin_, spi_, kSpiHz); // Mount the FAT file system using OUR chip-select pin (the board's default CS is the TFT's).
  if (!mounted_ || SD.cardType() == CARD_NONE)
  {
    mounted_ = false;
    return Status::NOT_PRESENT; // No card, unreadable card, or not FAT16/FAT32 formatted.
  }
  return Status::OK;
}

bool SdCard::ensureMounted()
{
  return mounted_ || begin() == Status::OK; // Already mounted, or try once to (re)mount.
}

bool SdCard::exists(const char* path)
{
  return isValidPath(path) && ensureMounted() && SD.exists(path);
}

Status SdCard::writeOut(const char* path, const char* mode, const uint8_t* data, size_t len)
{
  if (!isValidPath(path) || (data == nullptr && len > 0))
  {
    return Status::OUT_OF_RANGE; // Bad request: refuse before touching the card.
  }
  if (!ensureMounted())
  {
    return Status::NOT_PRESENT;
  }

  File f = SD.open(path, mode); // "a" = append (create if missing), "w" = replace.
  if (!f)
  {
    mounted_ = false; // The card probably went away: remount next time.
    return Status::BUS_ERROR;
  }

  size_t written = (len > 0) ? f.write(data, len) : 0; // Write the bytes into the library's buffer...
  f.close(); // ...and closing pushes them onto the card. This is what makes "OK means it's on the card" true.

  if (written == len)
  {
    return Status::OK;
  }

  uint64_t space = 0;
  if (freeBytes(space) == Status::OK && space < len)
  {
    return Status::OUT_OF_RANGE; // The card is full.
  }
  mounted_ = false; // Something else failed: remount next time.
  return Status::BUS_ERROR;
}

Status SdCard::append(const char* path, const uint8_t* data, size_t len)
{
  return writeOut(path, FILE_APPEND, data, len);
}

Status SdCard::writeFile(const char* path, const uint8_t* data, size_t len)
{
  return writeOut(path, FILE_WRITE, data, len);
}

Status SdCard::readFile(const char* path, uint8_t* out, size_t capacity, size_t& got)
{
  got = 0;
  if (!isValidPath(path) || out == nullptr)
  {
    return Status::OUT_OF_RANGE;
  }
  if (!ensureMounted())
  {
    return Status::NOT_PRESENT;
  }
  if (!SD.exists(path))
  {
    return Status::NOT_PRESENT; // No such file.
  }

  File f = SD.open(path, FILE_READ);
  if (!f)
  {
    mounted_ = false;
    return Status::BUS_ERROR;
  }

  size_t size = f.size(); // How big the file is.
  if (size > capacity)
  {
    f.close();
    return Status::OUT_OF_RANGE; // Doesn't fit in the caller's buffer: read nothing rather than half a file.
  }

  got = f.read(out, size); // Read the whole file.
  f.close();
  if (got != size)
  {
    mounted_ = false;
    return Status::BUS_ERROR;
  }
  return Status::OK;
}

Status SdCard::freeBytes(uint64_t& bytes)
{
  bytes = 0;
  if (!ensureMounted())
  {
    return Status::NOT_PRESENT;
  }
  uint64_t total = SD.totalBytes(); // Size of the file system.
  uint64_t used = SD.usedBytes(); // Space taken by files.
  bytes = (total > used) ? total - used : 0;
  return Status::OK;
}