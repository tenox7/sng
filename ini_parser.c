#include "ini_parser.h"

static void trim_whitespace(char *str) {
    char *start = str;
    char *end = str + strlen(str) - 1;
    
    while (*start == ' ' || *start == '\t') start++;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end--;
    
    *(end + 1) = '\0';
    if (start != str) {
        memmove(str, start, end - start + 1);
    }
}

ini_file_t *ini_parse_file(const char *filename) {
    FILE *file;
    ini_file_t *ini;
    char line[512];
    char current_section[128];
    ini_section_t *current_section_ptr;
    char *eq;
    char *key;
    char *value;
    ini_pair_t *pair;

    file = fopen(filename, "r");
    if (!file) return NULL;

    ini = malloc(sizeof(ini_file_t));
    if (!ini) {
        fclose(file);
        return NULL;
    }

    ini->sections = NULL;
    ini->section_count = 0;
    
    current_section[0] = '\0';
    current_section_ptr = NULL;
    
    while (fgets(line, sizeof(line), file)) {
        if (ferror(file)) {
            ini_free(ini);
            fclose(file);
            return NULL;
        }
        trim_whitespace(line);
        
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#') {
            continue;
        }
        
        if (line[0] == '[' && line[strlen(line)-1] == ']') {
            strncpy(current_section, line + 1, sizeof(current_section) - 1);
            current_section[strlen(current_section) - 1] = '\0';
            trim_whitespace(current_section);
            
            ini->section_count++;
            ini->sections = realloc(ini->sections, sizeof(ini_section_t) * ini->section_count);
            if (!ini->sections) {
                ini_free(ini);
                fclose(file);
                return NULL;
            }
            current_section_ptr = &ini->sections[ini->section_count - 1];
            current_section_ptr->section = malloc(strlen(current_section) + 1);
            if (!current_section_ptr->section) {
                ini_free(ini);
                fclose(file);
                return NULL;
            }
            strcpy(current_section_ptr->section, current_section);
            current_section_ptr->pairs = NULL;
            current_section_ptr->pair_count = 0;
            current_section_ptr->lines = NULL;
            current_section_ptr->line_count = 0;
            
        } else if (current_section_ptr && strchr(line, '=')) {
            eq = strchr(line, '=');
            *eq = '\0';
            key = line;
            value = eq + 1;

            trim_whitespace(key);
            trim_whitespace(value);

            current_section_ptr->pair_count++;
            current_section_ptr->pairs = realloc(current_section_ptr->pairs,
                                               sizeof(ini_pair_t) * current_section_ptr->pair_count);
            if (!current_section_ptr->pairs) {
                ini_free(ini);
                fclose(file);
                return NULL;
            }

            pair = &current_section_ptr->pairs[current_section_ptr->pair_count - 1];
            pair->key = malloc(strlen(key) + 1);
            pair->value = malloc(strlen(value) + 1);
            if (!pair->key || !pair->value) {
                free(pair->key);
                free(pair->value);
                ini_free(ini);
                fclose(file);
                return NULL;
            }
            strcpy(pair->key, key);
            strcpy(pair->value, value);

        }
    }

    fclose(file);
    return ini;
}

void ini_free(ini_file_t *ini) {
    int i, j;

    if (!ini) return;
    
    for (i = 0; i < ini->section_count; i++) {
        free(ini->sections[i].section);
        for (j = 0; j < ini->sections[i].pair_count; j++) {
            free(ini->sections[i].pairs[j].key);
            free(ini->sections[i].pairs[j].value);
        }
        free(ini->sections[i].pairs);
    }
    free(ini->sections);
    free(ini);
}

char *ini_get_value(ini_file_t *ini, const char *section, const char *key) {
    int i, j;

    if (!ini) return NULL;
    
    for (i = 0; i < ini->section_count; i++) {
        if (strcmp(ini->sections[i].section, section) == 0) {
            for (j = 0; j < ini->sections[i].pair_count; j++) {
                if (strcmp(ini->sections[i].pairs[j].key, key) == 0) {
                    return ini->sections[i].pairs[j].value;
                }
            }
        }
    }
    return NULL;
}
