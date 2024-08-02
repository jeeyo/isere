#include "CppUTest/TestHarness.h"

#include <cstring>

#include "isere.h"
#include "js.h"

TEST_GROUP(JSTest) {};

TEST(JSTest, ShouldReturnErrorWhenQuickJSRuntimeIsAlreadyInitialized)
{
  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));
  js.runtime = (JSRuntime *)malloc(1);

  int ret = isere_js_init(&js);

  free(js.runtime);

  LONGS_EQUAL_TEXT(ret, -1, "isere_js_init() did not return -1 when runtime is already initialized");
}

TEST(JSTest, ShouldReturnErrorWhenQuickJSContextIsAlreadyInitialized)
{
  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));
  js.context = (JSContext *)malloc(1);

  int ret = isere_js_init(&js);

  free(js.context);

  LONGS_EQUAL_TEXT(ret, -1, "isere_js_init() did not return -1 when context is already initialized");
}

TEST(JSTest, ShouldInitializeSuccessfully)
{
  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));

  int ret = isere_js_init(&js);
  isere_js_deinit(&js);

  LONGS_EQUAL_TEXT(ret, 0, "isere_js_init() did not return 0");
}
