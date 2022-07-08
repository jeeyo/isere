#include "logger.h"

int echoer() {
  logger_init();
  logger_debug("hell yeah\n");
  return 0;
}
