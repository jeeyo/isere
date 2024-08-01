#include "CppUTest/TestHarness.h"

#include <cstring>

#include "isere.h"
#include "loader.h"

TEST_GROUP(LoaderTest) {};

static void fake_logger_fn(const char *tag, const char *fmt, ...) {}

static isere_logger_t fake_logger = {
  .error = fake_logger_fn,
  .warning = fake_logger_fn,
  .info = fake_logger_fn,
  .debug = fake_logger_fn,
};

TEST(LoaderTest, ShouldReturnErrorWhenLoggerIsNull)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));

  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));

  int ret = isere_loader_init(&isere, &loader);

  LONGS_EQUAL_TEXT(ret, -1, "isere_loader_init() did not return -1 when logger is NULL");
}

TEST(LoaderTest, ShouldReturnErrorWhenLoaderIsAlreadyInitialized)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));
  isere.logger = &fake_logger;

  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));

  isere_loader_init(&isere, &loader);
  int ret = isere_loader_init(&isere, &loader);

  LONGS_EQUAL_TEXT(ret, -1, "isere_loader_init() did not return -1 when logger is already initialized");
}

TEST(LoaderTest, ShouldInitializeLoaderSuccessfully)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));
  isere.logger = &fake_logger;

  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));

  int ret = isere_loader_init(&isere, &loader);

  LONGS_EQUAL_TEXT(ret, 0, "isere_loader_init() did not return 0");
}

TEST(LoaderTest, ShouldLoadHandlerDllSuccessfully)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));
  isere.logger = &fake_logger;

  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));

  int ret = isere_loader_init(&isere, &loader);

  LONGS_EQUAL_TEXT(ret, 0, "isere_loader_init() did not return 0");
  STRCMP_EQUAL_TEXT((const char *)loader.fn, "console.log('hello world');", "loader.fn is not correct");
  LONGS_EQUAL_TEXT(loader.fn_size, 27, "loader.fn_size is not correct");
}
