#include <stdbool.h>

#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"

TEST_GROUP(FirstTestGroup) {};

TEST(FirstTestGroup, SecondTest)
{
  STRCMP_EQUAL("hello", "world");
  LONGS_EQUAL(1, 2);
  CHECK(false);
}

int main(int argc, char** argv)
{
  return CommandLineTestRunner::RunAllTests(argc, argv);
}
