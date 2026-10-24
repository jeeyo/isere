// JerryScript measures stack use from a base it records in jerry_init(). isère
// calls into an instance from different depths (a request, a timer, a fetch
// callback), so the base is moved to the caller's frame on every entry; the
// limit (JERRY_STACK_LIMIT) then counts from where the JS work starts, as
// QuickJS does with JS_UpdateStackTop(). JerryScript has no public API for
// this, so this file is built against its private headers.

#include <stdint.h>

#include "jcontext.h"

void isere_jerry_set_stack_base(uintptr_t base)
{
#if (JERRY_STACK_LIMIT != 0)
  JERRY_CONTEXT (stack_base) = base;
#else
  (void) base;
#endif
}
