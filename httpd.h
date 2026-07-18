#ifndef HTTPD_H
#define HTTPD_H

#include "config.h"
#include "threading.h"

int httpd_start(config_t *config, data_collector_t *collector);
void httpd_stop(void);

#endif
