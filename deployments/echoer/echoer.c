#include "isere.h"

int echoer(isere_t *isere) {
  isere_logger_t *logger = isere->logger;
  logger->debug("hell yeah\n");
  return 0;
}
