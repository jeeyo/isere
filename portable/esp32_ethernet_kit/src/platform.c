#include "platform.h"

void platform_init() {
  // if (watchdog_caused_reboot()) {
  //   // TODO: Log the watchdog reboot
  // }

  // // Enable the watchdog, requiring the watchdog to be updated every 100ms or the chip will reboot
  // // second arg is pause on debug which means the watchdog will pause when stepping through code
  // watchdog_enable(100, 1);

  // // Keep updating the watchdog every 100ms
  // for (int i = 0; i < 10; i++) {
  //   sleep_ms(10);
  //   watchdog_update();
  // }

// #ifdef PICO_DEFAULT_LED_PIN
  // gpio_init(25);
  // gpio_set_dir(25, GPIO_OUT);
  // gpio_put(25, 1);
// #endif
}
