#include "CppUTest/TestHarness.h"

#include <cstring>

#include "isere.h"
#include "loader.h"

#define HELLOWORLD_DLL_PATH "./tests/helloworld.so"
#define DEFAULT_DLL_PATH HELLOWORLD_DLL_PATH

TEST_GROUP(LoaderTest) {};

static void fake_logger_fn(const char *fmt, ...) {
  // (void)fmt;
  va_list vargs;
  va_start(vargs, fmt);
  vprintf(fmt, vargs);
  va_end(vargs);
}

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

  int ret = loader_init(&isere, &loader, DEFAULT_DLL_PATH);

  LONGS_EQUAL_TEXT(ret, -1, "loader_init() did not return -1 when logger is NULL");
}

TEST(LoaderTest, ShouldReturnErrorWhenDllPathIsNull)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));

  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));

  int ret = loader_init(&isere, &loader, NULL);

  LONGS_EQUAL_TEXT(ret, -1, "loader_init() did not return -1 when dll_path is NULL");
}

TEST(LoaderTest, ShouldReturnErrorWhenLoaderIsAlreadyInitialized)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));
  isere.logger = &fake_logger;

  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));

  loader_init(&isere, &loader, DEFAULT_DLL_PATH);
  int ret = loader_init(&isere, &loader, DEFAULT_DLL_PATH);

  LONGS_EQUAL_TEXT(ret, -1, "loader_init() did not return -1 when logger is already initialized");
}

TEST(LoaderTest, ShouldInitializeLoaderSuccessfully)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));
  isere.logger = &fake_logger;

  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));

  int ret = loader_init(&isere, &loader, DEFAULT_DLL_PATH);

  LONGS_EQUAL_TEXT(ret, 0, "loader_init() did not return 0");
}

TEST(LoaderTest, ShouldLoadHelloWorldDllSuccessfully)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));
  isere.logger = &fake_logger;

  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));

  int ret = loader_init(&isere, &loader, HELLOWORLD_DLL_PATH);

  LONGS_EQUAL_TEXT(ret, 0, "loader_init() did not return 0");
  STRCMP_EQUAL_TEXT((const char *)loader.fn, "console.log('hello world');", "loader.fn is not correct");
  LONGS_EQUAL_TEXT(loader.fn_size, 27, "loader.fn_size is not correct");
}
