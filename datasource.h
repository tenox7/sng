#ifndef DATASOURCE_H
#define DATASOURCE_H

#include "compat.h"
#include <stddef.h>

typedef struct {
    double min;
    double max;
    double avg;
    double last;
    double min_secondary;
    double max_secondary;
    double avg_secondary;
    double last_secondary;
} datasource_stats_t;

typedef struct {
    int (*init)(const char *target, void **context);
    int (*collect)(void *context, double *value);
    int (*collect_dual)(void *context, double *value1, double *value2);
    int (*get_stats)(void *context, datasource_stats_t *stats);
    void (*format_value)(double value, char *buffer, size_t buffer_size);
    double (*get_max_scale)(void *context);
    void (*cleanup)(void *context);
    const char *name;
    const char *unit;
    int is_dual;
    double max_scale;
} datasource_handler_t;

typedef struct {
    datasource_handler_t *handler;
    void *context;
    char *target;
} datasource_t;

datasource_t *datasource_create(const char *type, const char *target);
int datasource_collect(datasource_t *ds, double *value);
void datasource_destroy(datasource_t *ds);
const char *datasource_get_unit(datasource_t *ds);
double datasource_get_max_scale(datasource_t *ds);
void datasource_set_refresh_interval(datasource_t *ds, int32_t refresh_interval_ms);

#endif
