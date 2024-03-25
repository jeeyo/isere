#include "platform.h"

#include "hardware/gpio.h"

void platform_init() {
// #ifdef PICO_DEFAULT_LED_PIN
  gpio_init(25);
  gpio_set_dir(25, GPIO_OUT);
  gpio_put(25, 1);
// #endif
}
