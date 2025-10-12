#ifndef INI_PARSER_H
#define INI_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *key;
    char *value;
} ini_pair_t;

typedef struct {
    char *section;
    ini_pair_t *pairs;
    int pair_count;
    char **lines;     // For non-key=value sections like targets
    int line_count;
} ini_section_t;

typedef struct {
    ini_section_t *sections;
    int section_count;
} ini_file_t;

ini_file_t *ini_parse_file(const char *filename);
void ini_free(ini_file_t *ini);
char *ini_get_value(ini_file_t *ini, const char *section, const char *key);
char **ini_get_lines(ini_file_t *ini, const char *section, int *count);

#endif
