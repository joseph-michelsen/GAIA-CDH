#include <Arduino.h>
#include <SPI.h>
#include <unity.h>
#include "board_pins.h"
#include <SD.h>
#include "drivers/sd_card.h"

// ON-BOARD test: runs on the ESP32 with the Adalogger FeatherWing attached and a FAT32 microSD card inserted.
// Run with:  pio test -e board_test
// (The native tests can't cover SdCard: Arduino's SD library only exists on the board.)

static SdCard card(SPI, pins::SD_CS);
static const char* kFile = "/sdtest.txt"; // Scratch file, deleted at the end of the run.

static int asInt(Status s) { return static_cast<int>(s); }

void setUp() {}
void tearDown() {}

void test_mounts_card()
{
  TEST_ASSERT_EQUAL_INT_MESSAGE(asInt(Status::OK), asInt(card.begin()), "No card, not FAT32, or wrong CS pin");
}

void test_write_then_read_back()
{
  const uint8_t text[] = "hello lunar greenhouse";
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(card.writeFile(kFile, text, sizeof(text) - 1)));
  TEST_ASSERT_TRUE(card.exists(kFile));
  uint8_t out[64] = {};
  size_t got = 0;
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(card.readFile(kFile, out, sizeof(out), got)));
  TEST_ASSERT_EQUAL_UINT(sizeof(text) - 1, got);
  TEST_ASSERT_EQUAL_MEMORY(text, out, got);
}

void test_append_adds_to_end()
{
  const uint8_t a[] = {'a', 'b'};
  const uint8_t b[] = {'c'};
  card.writeFile(kFile, a, 2); // Start fresh with "ab".
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(card.append(kFile, b, 1)));
  uint8_t out[8] = {};
  size_t got = 0;
  card.readFile(kFile, out, sizeof(out), got);
  TEST_ASSERT_EQUAL_UINT(3, got);
  TEST_ASSERT_EQUAL_MEMORY("abc", out, 3);
}

void test_write_replaces_whole_file()
{
  const uint8_t longer[] = {'1', '2', '3', '4'};
  const uint8_t shorter[] = {'9'};
  card.writeFile(kFile, longer, 4);
  card.writeFile(kFile, shorter, 1);
  uint8_t out[8] = {};
  size_t got = 0;
  card.readFile(kFile, out, sizeof(out), got);
  TEST_ASSERT_EQUAL_UINT(1, got); // Not "9234".
}

void test_append_creates_missing_file()
{
  SD.remove(kFile);
  const uint8_t x[] = {'x'};
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(card.append(kFile, x, 1)));
  TEST_ASSERT_TRUE(card.exists(kFile));
}

void test_errors_use_status_words()
{
  uint8_t out[2] = {};
  size_t got = 0;
  const uint8_t x[] = {'x', 'y', 'z'};
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(card.readFile("/nofile.txt", out, sizeof(out), got)));
  TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(card.append("/a/b.txt", x, 3))); // Sub-folder: not allowed.
  card.writeFile(kFile, x, 3);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OUT_OF_RANGE), asInt(card.readFile(kFile, out, sizeof(out), got))); // 3 bytes, buffer holds 2.
  TEST_ASSERT_EQUAL_UINT(0, got);
}

void test_reports_free_space()
{
  uint64_t space = 0;
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(card.freeBytes(space)));
  TEST_ASSERT_TRUE(space > 0);
  Serial.printf("Free space: %llu MB\n", static_cast<unsigned long long>(space / (1024ULL * 1024ULL)));
}

void setup()
{
  delay(2000); // Give the serial monitor time to connect.
#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH); // Keep the Reverse TFT's display deselected so it ignores the shared SPI bus.
#endif
  SPI.begin(pins::SPI_SCK, pins::SPI_MISO, pins::SPI_MOSI); // Start the shared SPI bus.

  UNITY_BEGIN();
  RUN_TEST(test_mounts_card);
  RUN_TEST(test_write_then_read_back);
  RUN_TEST(test_append_adds_to_end);
  RUN_TEST(test_write_replaces_whole_file);
  RUN_TEST(test_append_creates_missing_file);
  RUN_TEST(test_errors_use_status_words);
  RUN_TEST(test_reports_free_space);
  SD.remove(kFile); // Leave the card as we found it.
  UNITY_END();
}

void loop() {}