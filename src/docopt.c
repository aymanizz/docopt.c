#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "parser.h"
#include "util.h"

static FILE* open_file(const char* filename, const char* mode)
{
    FILE* file = fopen(filename, mode);
    if (!file) {
        fprintf(stderr, "error: couldn't open file '%s'.\n", filename);
        exit(10);
    }

    return file;
}

static char* read_file(const char* filename)
{
    FILE* file = open_file(filename, "rb");

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    char* content = malloc(size + 1);
    if (!content) {
        fprintf(stderr, "error: not enough memory to read file '%s'.\n",
            filename);
        exit(74);
    }

    size_t bytes_read = fread(content, 1, size, file);
    if (bytes_read < size) {
        fprintf(stderr, "error: couldn't read file '%s'.\n", filename);
        exit(74);
    }
    content[bytes_read] = '\0';

    fclose(file);

    return content;
}

const char* find_doc_pattern(const char* str, const char* const pattern)
{
    while (*str && strncmp(str, pattern, strlen(pattern)) != 0)
        str = str_skip_line(str);
    if (!*str)
        return NULL;
    str += strlen(pattern);

    str = str_skip_blank(str);

    if (*str == ':') {
        ++str;
        str = str_skip_blank(str);
    }

    if (*str == '\n') {
        ++str;
    }

    return str;
}

static int parse(const char* src)
{
    const char* options_start = find_doc_pattern(src, "OPTIONS");
    struct option* options = get_options_list(options_start);

    for (struct option* opt = options; opt; opt = opt->next) {
        printf("option --( %.*s )-- %c | %.*s:\n\targ=%.*s, required=%s, "
               "negatable=%s\n\n",
               opt->pattern_len, opt->pattern,
               opt->oshort, opt->olong_len, opt->olong,
               opt->arg_name_len, opt->arg_name,
               opt->prop & OPT_ARG_OPTIONAL ? "true" : "false",
               (opt->prop & OPT_NEGATABLE) == OPT_NEGATABLE ? "true" : "false");
    }

    free_options_list(options);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        printf("usage: %s SOURCE_FILE\n", argv[0]);
        return -1;
    }

    const char* src = read_file(argv[1]);
    return parse(src);
}
