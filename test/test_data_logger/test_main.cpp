#include <unity.h>
#include <string.h>
#include "storage.h"
#include "sample.h"
#include "services/data_logger.h"

// A pretend card that can hold a few files, and counts calls so tests can check how the logger writes.

class FakeStorage : public Storage
{
  public:
  struct File
  {
    char path[Storage::kMaxPathLength + 1] = {};
    char text[4096] = {};
    size_t size = 0;
    bool used = false;
  };

  File files[4];
  bool cardPresent = true;
  int appends = 0;
  int existsCalls = 0;

  Status begin() override { return cardPresent ? Status::OK : Status::NOT_PRESENT; }
  bool exists(const char* p) override { existsCalls++; return cardPresent && find(p) != nullptr; }
  Status append(const char* p, const uint8_t* d, size_t len) override
  {
    if (!cardPresent) return Status::NOT_PRESENT;
    if (!isValidPath(p)) return Status::OUT_OF_RANGE;
    File* f = find(p);
    if (f == nullptr) f = create(p);
    if (f == nullptr || f->size + len >= sizeof(f->text)) return Status::OUT_OF_RANGE;
    memcpy(f->text + f->size, d, len);
    f->size += len;
    appends++;
    return Status::OK;
  }
  Status writeFile(const char*, const uint8_t*, size_t) override { return Status::BUS_ERROR; } // Not used by the logger.
  Status readFile(const char*, uint8_t*, size_t, size_t& got) override { got = 0; return Status::BUS_ERROR; } // Not used.
  Status freeBytes(uint64_t& bytes) override { bytes = 1000; return Status::OK; }

  const char* text(const char* p) { File* f = find(p); return f ? f->text : ""; }
  int fileCount() { int n = 0; for (auto& f : files) n += f.used ? 1 : 0; return n; }
  void removeAll() { for (auto& f : files) f = File(); }

  private:
  File* find(const char* p) { for (auto& f : files) if (f.used && strcmp(f.path, p) == 0) return &f; return nullptr; }
  File* create(const char* p)
  {
    for (auto& f : files)
      if (!f.used) { f.used = true; strncpy(f.path, p, sizeof(f.path) - 1); return &f; }
    return nullptr;
  }
};

static int asInt(Status s) { return static_cast<int>(s); }

static int countOf(const char* haystack, const char* needle) // How many times needle appears.
{
  int n = 0;
  for (const char* p = strstr(haystack, needle); p != nullptr; p = strstr(p + 1, needle)) n++;
  return n;
}

void setUp() {}
void tearDown() {}

void test_day_file_names()
{
  char name[DataLogger::kFileNameLength];
  DataLogger::dayFileName(0, name, sizeof(name));
  TEST_ASSERT_EQUAL_STRING("/day_000.csv", name);
  DataLogger::dayFileName(86399, name, sizeof(name)); // Last second of day 0.
  TEST_ASSERT_EQUAL_STRING("/day_000.csv", name);
  DataLogger::dayFileName(86400, name, sizeof(name)); // First second of day 1.
  TEST_ASSERT_EQUAL_STRING("/day_001.csv", name);
  DataLogger::dayFileName(15u * 86400u - 1u, name, sizeof(name)); // Last second of a 15-day mission.
  TEST_ASSERT_EQUAL_STRING("/day_014.csv", name);
}

void test_writes_header_and_rows()
{
  FakeStorage card;
  DataLogger logger(card);
  const Sample s[] = {makeSample(ParamID::CO2, 812.5f, Status::OK, 120),
                      makeSample(ParamID::AIR_TEMP, 22.4f, Status::OK, 120)};
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(logger.log(s, 2)));
  TEST_ASSERT_EQUAL_STRING("met_s,param,value,status\n"
                           "120,CO2,812.5,OK\n"
                           "120,AIR_TEMP,22.4,OK\n",
                           card.text("/day_000.csv"));
  TEST_ASSERT_EQUAL_UINT32(2, logger.rowsWritten());
}

void test_status_and_unusual_values_are_readable()
{
  FakeStorage card;
  DataLogger logger(card);
  const Sample s[] = {makeSample(ParamID::PRESSURE, 83499.92f, Status::OK, 5),
                      makeSample(ParamID::HUMIDITY, 0.0f, Status::BUS_ERROR, 5),
                      makeSample(ParamID::BATT_VOLTAGE, 0.0f, Status::NOT_PRESENT, 5)};
  logger.log(s, 3);
  const char* t = card.text("/day_000.csv");
  TEST_ASSERT_NOT_NULL(strstr(t, "5,PRESSURE,83499.92,OK\n"));
  TEST_ASSERT_NOT_NULL(strstr(t, "5,HUMIDITY,0,BUS_ERROR\n")); // Failures are logged too, so gaps are explained.
  TEST_ASSERT_NOT_NULL(strstr(t, "5,BATT_VOLTAGE,0,NOT_PRESENT\n"));
}

void test_one_write_per_batch()
{
  FakeStorage card;
  DataLogger logger(card);
  Sample s[6];
  for (int i = 0; i < 6; i++) s[i] = makeSample(ParamID::CO2, 400.0f + static_cast<float>(i), Status::OK, 10);
  logger.log(s, 6);
  TEST_ASSERT_EQUAL_INT(2, card.appends); // Header + one append for all 6 rows (not 7 separate SD writes).
}

void test_header_only_once_per_file()
{
  FakeStorage card;
  DataLogger logger(card);
  for (uint32_t met = 0; met < 20; met += 2)
  {
    Sample s = makeSample(ParamID::CO2, 800.0f, Status::OK, met);
    logger.log(&s, 1);
  }
  TEST_ASSERT_EQUAL_INT(1, countOf(card.text("/day_000.csv"), "met_s,"));
  TEST_ASSERT_EQUAL_INT(10, countOf(card.text("/day_000.csv"), ",CO2,"));
  TEST_ASSERT_EQUAL_INT(1, card.existsCalls); // Checked once, then remembered.
}

void test_new_file_each_mission_day()
{
  FakeStorage card;
  DataLogger logger(card);
  Sample a = makeSample(ParamID::CO2, 800.0f, Status::OK, 86398);
  Sample b = makeSample(ParamID::CO2, 801.0f, Status::OK, 86400);
  logger.log(&a, 1);
  logger.log(&b, 1);
  TEST_ASSERT_EQUAL_INT(2, card.fileCount());
  TEST_ASSERT_NOT_NULL(strstr(card.text("/day_000.csv"), "86398,CO2,800,OK"));
  TEST_ASSERT_EQUAL_STRING("met_s,param,value,status\n86400,CO2,801,OK\n", card.text("/day_001.csv"));
}

void test_batch_spanning_midnight_is_split()
{
  FakeStorage card;
  DataLogger logger(card);
  const Sample s[] = {makeSample(ParamID::CO2, 800.0f, Status::OK, 86399),
                      makeSample(ParamID::CO2, 801.0f, Status::OK, 86400)};
  logger.log(s, 2);
  TEST_ASSERT_EQUAL_INT(1, countOf(card.text("/day_000.csv"), ",CO2,"));
  TEST_ASSERT_EQUAL_INT(1, countOf(card.text("/day_001.csv"), ",CO2,"));
}

void test_continues_existing_file_after_reboot()
{
  FakeStorage card;
  {
    DataLogger firstBoot(card);
    Sample s = makeSample(ParamID::CO2, 800.0f, Status::OK, 100);
    firstBoot.log(&s, 1);
  }
  DataLogger secondBoot(card); // New logger, same card.
  Sample s = makeSample(ParamID::CO2, 805.0f, Status::OK, 200);
  secondBoot.log(&s, 1);
  TEST_ASSERT_EQUAL_INT(1, countOf(card.text("/day_000.csv"), "met_s,")); // No second header.
  TEST_ASSERT_EQUAL_INT(2, countOf(card.text("/day_000.csv"), ",CO2,"));
}

void test_card_failure_counts_dropped_rows_then_recovers()
{
  FakeStorage card;
  DataLogger logger(card);
  Sample s = makeSample(ParamID::CO2, 800.0f, Status::OK, 10);
  logger.log(&s, 1);

  card.cardPresent = false; // Card pulled out...
  TEST_ASSERT_EQUAL_INT(asInt(Status::NOT_PRESENT), asInt(logger.log(&s, 1)));
  TEST_ASSERT_EQUAL_UINT32(1, logger.rowsDropped());

  card.removeAll(); // ...and a blank card put back.
  card.cardPresent = true;
  s.MET_s = 20;
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(logger.log(&s, 1)));
  TEST_ASSERT_EQUAL_STRING("met_s,param,value,status\n20,CO2,800,OK\n", card.text("/day_000.csv")); // Header rewritten.
}

void test_large_batch_fills_buffer_without_losing_rows()
{
  FakeStorage card;
  DataLogger logger(card);
  Sample s[40];
  for (int i = 0; i < 40; i++) s[i] = makeSample(ParamID::ELECTRONICS_TEMP, -12.345f, Status::OUT_OF_RANGE, 1296000u - 1u);
  TEST_ASSERT_EQUAL_INT(asInt(Status::OK), asInt(logger.log(s, 40)));
  TEST_ASSERT_EQUAL_INT(40, countOf(card.text("/day_014.csv"), "1295999,ELECTRONICS_TEMP,-12.345,OUT_OF_RANGE\n"));
  TEST_ASSERT_EQUAL_UINT32(40, logger.rowsWritten());
  TEST_ASSERT_EQUAL_UINT32(0, logger.rowsDropped());
}

void test_skips_samples_never_filled_in()
{
  FakeStorage card;
  DataLogger logger(card);
  Sample s[2]; // Defaults: id = COUNT.
  s[1] = makeSample(ParamID::PH, 6.1f, Status::OK, 3);
  logger.log(s, 2);
  TEST_ASSERT_EQUAL_STRING("met_s,param,value,status\n3,PH,6.1,OK\n", card.text("/day_000.csv"));
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_day_file_names);
  RUN_TEST(test_writes_header_and_rows);
  RUN_TEST(test_status_and_unusual_values_are_readable);
  RUN_TEST(test_one_write_per_batch);
  RUN_TEST(test_header_only_once_per_file);
  RUN_TEST(test_new_file_each_mission_day);
  RUN_TEST(test_batch_spanning_midnight_is_split);
  RUN_TEST(test_continues_existing_file_after_reboot);
  RUN_TEST(test_card_failure_counts_dropped_rows_then_recovers);
  RUN_TEST(test_large_batch_fills_buffer_without_losing_rows);
  RUN_TEST(test_skips_samples_never_filled_in);
  return UNITY_END();
}