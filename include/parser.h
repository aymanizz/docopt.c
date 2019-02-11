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
    OPT_DOUBLEDASH = 64,
    OPT_DASH = 128,
};

struct opt_spec {
    enum opt_prop prop;
    char oshort;
    int olong_len;
    const char* olong;
    struct arg_spec* arg;
    struct opt_spec* next;
};

struct arg_spec {
    const char* name;
    int length;
};

void free_options_list(struct opt_spec* opt);
struct opt_spec* get_options_list(const char* iter);

#endif
