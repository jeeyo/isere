#include "CppUTest/TestHarness.h"

#include <cstring>

#include "isere.h"
#include "js.h"

TEST_GROUP(JSTest) {};

static void fake_logger_fn(const char *fmt, ...) {}

static isere_logger_t fake_logger = {
  .error = fake_logger_fn,
  .warning = fake_logger_fn,
  .info = fake_logger_fn,
  .debug = fake_logger_fn,
};

TEST(JSTest, ShouldReturnErrorWhenLoggerIsNull)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));

  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));

  int ret = js_init(&isere, &js);

  LONGS_EQUAL_TEXT(ret, -1, "js_init() did not return -1 when logger is NULL");
}

TEST(JSTest, ShouldReturnErrorWhenLoaderIsAlreadyInitialized)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));

  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));

  js_init(&isere, &js);
  int ret = js_init(&isere, &js);

  LONGS_EQUAL_TEXT(ret, -1, "js_init() did not return -1 when logger is already initialized");
}

TEST(JSTest, ShouldInitializeLoaderSuccessfully)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));
  isere.logger = &fake_logger;

  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));

  int ret = js_init(&isere, &js);

  LONGS_EQUAL_TEXT(ret, 0, "js_init() did not return 0");
}

static isere_loader_t fake_loader = {
  .fn = (uint8_t *)"export const handler = (event, context) => { console.log('hello world'); };",
  .fn_size = 75,
};

TEST(JSTest, ShouldEvaluateHelloWorldFunctionSuccessfully)
{
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));
  isere.logger = &fake_logger;
  isere.loader = &fake_loader;

  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));

  int ret = js_init(&isere, &js);
  LONGS_EQUAL_TEXT(ret, 0, "loader_init() did not return 0");

  ret = js_eval(&js);
  LONGS_EQUAL_TEXT(ret, 0, "js_eval() did not return 0");

  // TODO: get the result of the function and check it
}
