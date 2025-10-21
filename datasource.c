#define _GNU_SOURCE
#include "compat.h"
#include "datasource.h"
#include <stdlib.h>
#include <string.h>

extern datasource_handler_t ping_handler;
extern datasource_handler_t cpu_handler;
extern datasource_handler_t memory_handler;
extern datasource_handler_t snmp_handler;
extern datasource_handler_t if_thr_handler;
extern datasource_handler_t loadavg_handler;
extern datasource_handler_t shell_handler;
extern datasource_handler_t clock_handler;

static datasource_handler_t *handlers[] = {
    &ping_handler,
    &cpu_handler,
    &memory_handler,
    &snmp_handler,
    &if_thr_handler,
    &loadavg_handler,
    &shell_handler,
    &clock_handler,
    NULL
};

datasource_t *datasource_create(const char *type, const char *target) {
    int i;
    datasource_handler_t *handler;
    datasource_t *ds;

    if (!type) return NULL;

    handler = NULL;
    for (i = 0; handlers[i]; i++) {
        if (strcmp(handlers[i]->name, type) == 0) {
            handler = handlers[i];
            break;
        }
    }

    if (!handler) return NULL;

    ds = malloc(sizeof(datasource_t));
    if (!ds) return NULL;

    ds->handler = handler;
    ds->target = target ? strdup(target) : NULL;

    if (!handler->init(target, &ds->context)) {
        free(ds->target);
        free(ds);
        return NULL;
    }

    return ds;
}

int datasource_collect(datasource_t *ds, double *value) {
    if (!ds || !ds->handler) return 0;
    return ds->handler->collect(ds->context, value);
}

void datasource_destroy(datasource_t *ds) {
    if (!ds) return;

    if (ds->handler && ds->handler->cleanup) {
        ds->handler->cleanup(ds->context);
    }

    free(ds->target);
    free(ds);
}

const char *datasource_get_unit(datasource_t *ds) {
    if (!ds || !ds->handler) return "";
    return ds->handler->unit;
}

double datasource_get_max_scale(datasource_t *ds) {
    if (!ds || !ds->handler) return 0.0;
    if (ds->handler->get_max_scale) {
        return ds->handler->get_max_scale(ds->context);
    }
    return ds->handler->max_scale;
}

void datasource_set_refresh_interval(datasource_t *ds, int32_t refresh_interval_ms) {
    if (!ds || !ds->handler) return;
    if (ds->handler == &shell_handler) {
        extern void shell_set_refresh_interval(void *context, int32_t refresh_interval_ms);
        shell_set_refresh_interval(ds->context, refresh_interval_ms);
    }
}
