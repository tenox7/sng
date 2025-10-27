#ifndef SNMP_CLIENT_H
#define SNMP_CLIENT_H

#include "../compat.h"

#define SNMP_PORT 161
#define SNMP_MAX_MSG_SIZE 1500

int snmp_get_counter32(const char *host, const char *community,
                       const uint32_t *oid, int oid_len, uint32_t *result);

#endif
