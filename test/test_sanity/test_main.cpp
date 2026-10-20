#include <unity.h>

void setUp() {}      // runs before each test
void tearDown() {}   // runs after each test

void test_math_works() {
  TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_math_works);
  return UNITY_END();
}
