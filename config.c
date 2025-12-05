#define _GNU_SOURCE
#include "compat.h"
#include "config.h"
#include "ini_parser.h"
#include "default_config.h"
#include "os/os_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static config_t *global_config = NULL;

static char *create_default_config_file(const char *filename) {
    static char config_path[256];
    FILE *f;

    strcpy(config_path, filename);
    f = fopen(config_path, "w");
    if (!f) return NULL;

    fprintf(f, "%s", DEFAULT_CONFIG_INI);
    fclose(f);

    return config_path;
}

static color_t parse_color(const char *str) {
    color_t color;
    unsigned int r, g, b;

    color = (color_t){0, 0, 0, 255};
    if (!str || strlen(str) != 6) return color;
    if (sscanf(str, "%2x%2x%2x", &r, &g, &b) == 3) {
        color.r = (uint8_t)r;
        color.g = (uint8_t)g;
        color.b = (uint8_t)b;
    }
    return color;
}

static int parse_type_target(const char *type, const char *target, plot_config_t *plot, config_t *config) {
    const char *actual_type;
    const char *actual_target;
    char auto_name[256];
    char type_upper[64];
    int i;
    char host[128], community[64], interface[32];
    char *pipe_pos;
    size_t len;
    char truncated_target[256];

    if (!type || !target) return 0;

    actual_type = type;
    actual_target = target;

    if (strcmp(type, "bw") == 0) {
        if (strncmp(target, "snmp1,", 6) == 0) {
            actual_type = "snmp";
            actual_target = target + 6;
        } else {
            actual_type = "if_thr";
        }
    }

    for (i = 0; type[i] && i < 63; i++) {
        type_upper[i] = (type[i] >= 'a' && type[i] <= 'z') ? type[i] - 'a' + 'A' : type[i];
    }
    type_upper[strlen(type)] = '\0';

    if (strcmp(actual_type, "snmp") == 0) {
        if (sscanf(actual_target, "%127[^,],%63[^,],%31s", host, community, interface) == 3) {
            snprintf(auto_name, sizeof(auto_name), "BW - %s:%s", host, interface);
        } else {
            snprintf(auto_name, sizeof(auto_name), "BW - %s", actual_target);
        }
    } else if (strcmp(type, "bw") == 0) {
        snprintf(auto_name, sizeof(auto_name), "BW - %s", actual_target);
    } else if (strcmp(actual_type, "shell") == 0) {
        pipe_pos = strchr(actual_target, '|');
        if (pipe_pos) {
            len = pipe_pos - actual_target;
            while (len > 0 && (actual_target[len-1] == ' ' || actual_target[len-1] == '\t')) len--;
            strncpy(truncated_target, actual_target, len);
            truncated_target[len] = '\0';
            snprintf(auto_name, sizeof(auto_name), "%s - %s", type_upper, truncated_target);
        } else {
            snprintf(auto_name, sizeof(auto_name), "%s - %s", type_upper, actual_target);
        }
    } else {
        snprintf(auto_name, sizeof(auto_name), "%s - %s", type_upper, actual_target);
    }


    plot->type = malloc(strlen(actual_type) + 1);
    plot->target = malloc(strlen(actual_target) + 1);
    plot->name = malloc(strlen(auto_name) + 1);

    if (!plot->type || !plot->target || !plot->name) {
        free(plot->type);
        free(plot->target);
        free(plot->name);
        return 0;
    }

    strcpy(plot->type, actual_type);
    strcpy(plot->target, actual_target);
    strcpy(plot->name, auto_name);

    plot->line_color = config->line_color;
    plot->line_color_secondary = config->line_color_secondary;

    plot->background_color = (color_t){100, 100, 100, 255};
    plot->height = 100;
    plot->refresh_interval_ms = 0;

    return 1;
}

static void parse_plot_config(ini_file_t *ini, plot_config_t *plot, const char *section_name) {
    char *value;
    
    if ((value = ini_get_value(ini, section_name, "line_color"))) {
        plot->line_color = parse_color(value);
    }

    if ((value = ini_get_value(ini, section_name, "line_color_secondary"))) {
        plot->line_color_secondary = parse_color(value);
    }

    if ((value = ini_get_value(ini, section_name, "background_color"))) {
        plot->background_color = parse_color(value);
    }

    if ((value = ini_get_value(ini, section_name, "height"))) {
        plot->height = atoi(value);
    }

    if ((value = ini_get_value(ini, section_name, "refresh_interval_sec"))) {
        plot->refresh_interval_ms = atoi(value) * 1000;
    }
}

static int is_config_valid(ini_file_t *ini) {
    int i;
    if (!ini || ini->section_count == 0) return 0;

    for (i = 0; i < ini->section_count; i++) {
        if (strcmp(ini->sections[i].section, "global") == 0) {
            return 1;
        }
    }
    return 0;
}

config_t *config_load(const char *filename) {
    ini_file_t *ini;
    char *config_path;
    int use_defaults;
    config_t *config;
    char *value;
    plot_config_t *plots;
    uint32_t plot_count;
    uint32_t plot_capacity;
    int i, j;
    char *type, *target;
    char *section_name;
    char *platform_config_path;

    ini = ini_parse_file(filename);
    config_path = NULL;
    use_defaults = 0;

    if (!ini) {
        platform_config_path = os_get_config_path(filename);
        if (platform_config_path) {
            ini = ini_parse_file(platform_config_path);
        }

        if (!ini) {
            use_defaults = 1;
        }
    }

    if (ini && !is_config_valid(ini)) {
        ini_free(ini);
        ini = NULL;
        use_defaults = 1;
    }

    if (use_defaults) {
        config_path = create_default_config_file(filename);
        if (!config_path) {
            fprintf(stderr, "Could not create config file %s\n", filename);
            return NULL;
        }
        ini = ini_parse_file(config_path);
        if (!ini) {
            fprintf(stderr, "Could not parse config file %s\n", filename);
            return NULL;
        }
    }
    
    config = malloc(sizeof(config_t));
    if (!config) {
        ini_free(ini);
        return NULL;
    }
    
    config->background_color = (color_t){100, 100, 100, 255};
    config->text_color = (color_t){255, 255, 255, 255};
    config->border_color = (color_t){255, 255, 255, 255};
    config->line_color = (color_t){0, 255, 0, 255};
    config->line_color_secondary = (color_t){0, 0, 255, 255};
    config->error_line_color = (color_t){255, 0, 0, 255};
    config->default_height = 80;
    config->default_width = 300;
    config->refresh_interval_ms = 10000;
    config->window_margin = 5;
    config->max_fps = 30;
    config->fullscreen = FULLSCREEN_OFF;
    config->fps_counter = 0;
    config->font_size = 1.0f;
    config->font_name = NULL;
    config->plots = NULL;
    config->plot_count = 0;
    
    if ((value = ini_get_value(ini, "global", "background_color"))) {
        config->background_color = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "text_color"))) {
        config->text_color = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "border_color"))) {
        config->border_color = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "line_color"))) {
        config->line_color = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "line_color_secondary"))) {
        config->line_color_secondary = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "error_line_color"))) {
        config->error_line_color = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "default_height"))) {
        config->default_height = atoi(value);
    }
    if ((value = ini_get_value(ini, "global", "default_width"))) {
        config->default_width = atoi(value);
    }
    if ((value = ini_get_value(ini, "global", "refresh_interval_sec"))) {
        config->refresh_interval_ms = atoi(value) * 1000;
    }
    if ((value = ini_get_value(ini, "global", "window_margin"))) {
        config->window_margin = atoi(value);
    }
    if ((value = ini_get_value(ini, "global", "max_fps"))) {
        config->max_fps = atoi(value);
    }
    if ((value = ini_get_value(ini, "global", "fullscreen"))) {
        if (strcmp(value, "force") == 0) {
            config->fullscreen = FULLSCREEN_FORCE;
        } else if (strcmp(value, "1") == 0 || strcmp(value, "1") == 0) {
            config->fullscreen = FULLSCREEN_ON;
        } else {
            config->fullscreen = FULLSCREEN_OFF;
        }
    }
    if ((value = ini_get_value(ini, "global", "fps_counter"))) {
        config->fps_counter = (strcmp(value, "1") == 0 || strcmp(value, "1") == 0);
    }
    if ((value = ini_get_value(ini, "global", "font_size"))) {
        if (strchr(value, 'x')) {
            config->font_name = malloc(strlen(value) + 1);
            if (config->font_name) {
                strcpy(config->font_name, value);
            }
        } else {
            float font_size;
            font_size = atof(value);
            if (font_size > 0.0f) {
                config->font_size = font_size;
            }
        }
    }
    if ((value = ini_get_value(ini, "global", "font_name"))) {
        config->font_name = malloc(strlen(value) + 1);
        if (config->font_name) {
            strcpy(config->font_name, value);
        }
    }

    plots = NULL;
    plot_count = 0;
    plot_capacity = 0;
    
    for (i = 0; i < ini->section_count; i++) {
        if (strcmp(ini->sections[i].section, "targets") == 0) {
            for (j = 0; j < ini->sections[i].pair_count; j++) {
                if (plot_count >= plot_capacity) {
                    plot_capacity = plot_capacity ? plot_capacity * 2 : 4;
                    plots = realloc(plots, sizeof(plot_config_t) * plot_capacity);
                    if (!plots) {
                        ini_free(ini);
                        free(config);
                        return NULL;
                    }
                }
                
                type = ini->sections[i].pairs[j].key;
                target = ini->sections[i].pairs[j].value;

                if (parse_type_target(type, target, &plots[plot_count], config)) {
                    plot_count++;
                }
            }
            break;
        }
    }
    
    for (i = 0; i < ini->section_count; i++) {
        section_name = ini->sections[i].section;
        
        if (strcmp(section_name, "global") == 0 || strcmp(section_name, "targets") == 0) {
            continue;
        }
        
        for (j = 0; j < plot_count; j++) {
            if (strcmp(plots[j].name, section_name) == 0) {
                parse_plot_config(ini, &plots[j], section_name);
                break;
            }
        }
    }
    
    config->plots = plots;
    config->plot_count = plot_count;

    global_config = config;

    ini_free(ini);
    return config;
}

void config_destroy(config_t *config) {
    uint32_t i;

    if (!config) return;

    if (global_config == config) {
        global_config = NULL;
    }

    for (i = 0; i < config->plot_count; i++) {
        free(config->plots[i].name);
        free(config->plots[i].type);
        free(config->plots[i].target);
    }

    free(config->plots);
    if (config->font_name) {
        free(config->font_name);
    }
    free(config);
}

int config_get_max_fps(void) {
    if (!global_config) return 2;
    return global_config->max_fps;
}
