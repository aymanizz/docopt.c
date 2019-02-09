#ifndef CDOCOPT_UTIL_H
#define CDOCOPT_UTIL_H

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static inline void docopt_log(
    FILE* file, const char* level, int at,
    int line_len, const char* line,
    const char* fmt, ...)
{
    fprintf(file, "docopt: %s: ", level);
    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);
    fputs("\n", file);
    if (at != -1)
        fprintf(file, "%.*s\n%*s^-- here\n\n", line_len, line, at, "");
}

static inline const char* str_skip_line(const char* str)
{
    while (*str && *str != '\n')
        ++str;
    if (*str == '\n')
        ++str;
    return str;
}

static inline const char* str_skip_blank(const char* str)
{
    while (isblank(*str) || *str == '\r')
        ++str;
    return str;
}

static inline const char* str_skip_space(const char* str)
{
    while (isspace(*str) && *str != '\n')
        ++str;
    if (*str == '\n')
        ++str;
    return str;
}

static inline bool str_match_str(const char** str, const char *expected)
{
    if (strcmp(*str, expected) != 0)
        return false;
    *str += strlen(expected);
    return true;
}

static inline bool str_match_chr(const char** str, char expected)
{
    if (*str[0] != expected)
        return false;
    *str += 1;
    return true;
}

#endif
