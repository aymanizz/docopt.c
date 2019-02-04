#ifndef CDOCOPT_PARSER_H
#define CDOCOPT_PARSER_H

#include <stdbool.h>

enum opt_prop {
    OPT_SHORT = 1,
    OPT_LONG = 2,
    OPT_NEGATED = 4,
    OPT_NEGATABLE = 8,
    OPT_ARG = 16,
    OPT_ARG_OPTIONAL = 32,
};

struct option {
    enum opt_prop prop;
    char oshort;
    int pattern_len;
    int olong_len;
    int arg_name_len;
    const char* pattern;
    const char* olong;
    const char* arg_name;
    struct option* next;
};

void free_options_list(struct option* opt);
struct option* get_options_list(const char* iter);

#endif
