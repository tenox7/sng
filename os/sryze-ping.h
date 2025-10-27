/*
 * sryze-ping library interface
 * Based on https://github.com/sryze/ping
 *
 * Copyright (c) 2018-2021 Sergey Zolotarev
 * See LICENSE in sryze-ping.c for full license text.
 */

#ifndef SRYZE_PING_H
#define SRYZE_PING_H

#include "../compat.h"

typedef struct sryze_ping_context sryze_ping_context_t;

sryze_ping_context_t *sryze_ping_create(const char *hostname, uint32_t timeout_ms);
int sryze_ping_send(sryze_ping_context_t *ctx, double *ping_time_ms);
void sryze_ping_destroy(sryze_ping_context_t *ctx);

#endif
