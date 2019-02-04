#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "util.h"

#define WARN(opt, fmt, ...) docopt_diagnostic(    \
    "warning", "on pattern: %.*s\n\n" fmt "\n\n", \
    (opt)->pattern_len, (opt)->pattern, __VA_ARGS__)

#define ERROR(opt, fmt, ...) docopt_diagnostic( \
    "error", "on pattern: %.*s\n\n" fmt "\n\n", \
    (opt)->pattern_len, (opt)->pattern, __VA_ARGS__)

static bool is_arg_char(char c)
{
    return (islower(c) || isdigit(c) || c == ' ' || c == '_' || c == '-');
}

static bool is_cmd_char(char c)
{
    return (islower(c) || isdigit(c) || c == '_' || c == '-');
}

void free_options_list(struct option* opt)
{
    while (opt) {
        struct option* del = opt;
        opt = opt->next;
        free(del);
    }
}

static const char* parse_opt_arg_spec(
    struct option* opt, const char* iter)
{
    // pattern: ( <arg> | '=' <arg> | '[' '='? <arg> ']'
    //          | ' ' <arg> | " [" <arg> ']' )
    // where
    //          <arg> = '<' /[a-z0-9_- ]+/ '>'
    //                | /[A-Z]+[a-z0-9_- ]*/

    bool has_space = false;
    if (*iter == ' ') {
        ++iter;
        has_space = true;
    }

    bool is_optional = false;
    if (*iter == '[') {
        ++iter;
        is_optional = true;
    }

    if (!has_space && *iter == '=')
        ++iter;

    int len = 0;
    if (*iter == '<') {
        ++iter;
        while (is_arg_char(iter[len]))
            ++len;
        if (len == 0 || iter[len] != '>') {
            ERROR(opt, "unterminated argument name, expected '>' before '%c'.",
                iter[len]);
            iter += len + 1;
            return NULL;
        }
    } else if (isupper(*iter)) {
        while (isupper(iter[len]) && is_cmd_char(tolower(iter[len])))
            ++len;
    } else {
        ERROR(opt, "expected an argument name, found '%c'", *iter);
        return NULL;
    }

    bool new_spec = !(opt->prop & OPT_ARG);
    if (!new_spec) {
        if (strncmp(opt->arg_name, iter, len) != 0
            || is_optional != (bool)(opt->prop & OPT_ARG_OPTIONAL)) {
            // arg spec doesn't match
            const char* old_optional = (opt->prop & OPT_ARG_OPTIONAL) ? "optional " : "";
            const char* new_optional = is_optional ? "optional " : "";
            WARN(opt, "argument specification overrides previous one, "
                      "expected %s'%.*s', found %s'%.*s'.",
                old_optional, opt->arg_name_len, opt->arg_name,
                new_optional, len, iter);
        }
    }

    opt->arg_name = iter;
    opt->arg_name_len = len;
    iter += len + 1;
    if (!strncmp(iter, "  ", 2) || *str_skip_blank(iter) == '\n')

        if (is_optional) {
            if (*iter != ']') {
                ERROR(opt, "expected ']', found '%c'.", *iter);
                return NULL;
            }
            opt->prop |= OPT_ARG_OPTIONAL;
            ++iter;
        }

    return iter;
}

static const char* parse_long_option(
    struct option* opt, const char* iter)
{
    // long option pattern: ([no-])? /[a-zA-Z0-9_-]+/ <opt_arg_spec>
    if (*iter == '[') {
        ++iter;

        if (!strncmp(iter, "no-", 3)) {
            iter += 3;
        } else {
            ERROR(opt, "only [no-] (case sensitive) is allowed, found '%c'.", *iter);
            return NULL;
        }

        if (*iter != ']') {
            ERROR(opt, "expected ']', found '%c'.", *iter);
            return NULL;
        }
        ++iter;

        opt->prop |= OPT_NEGATABLE;
    }

    if (!strncmp(iter, "no-", 3)) {
        opt->prop |= OPT_NEGATED;
    }

    int len = 0;
    while (isupper(iter[len]) || is_cmd_char(iter[len]))
        ++len;
    if (len == 0) {
        ERROR(opt, "expected an option name, found '%c'.", *iter);
        return NULL;
    }

    opt->olong = iter;
    opt->olong_len = len;
    iter += len;

    if (*str_skip_blank(iter) == '\n' || !strncmp(iter, "  ", 2)) {
        return iter;
    }

    iter = parse_opt_arg_spec(opt, iter);
    if (!iter)
        return NULL;

    if (!isspace(*iter)) {
        ERROR(opt, "unexpected character '%c'.", *iter);
        return NULL;
    }

    return iter;
}

const char* parse_short_option(struct option* opt, const char* iter)
{
    // short option pattern: /[a-zA-z0-9]/ <opt_arg_spec>?
    if (!isalnum(*iter)) {
        ERROR(opt, "expected an alphanumeric character, found '%c'", *iter);
        return NULL;
    }

    opt->oshort = *iter;
    ++iter;

    const char* end = str_skip_blank(iter);
    if (*end == ',' || *end == '\n' || !strncmp(iter, "  ", 2)) {
        return iter;
    }

    iter = parse_opt_arg_spec(opt, iter);
    if (!iter)
        return NULL;

    return iter;
}

struct option* get_options_list(const char* iter)
{
    // options have the pattern:
    // /^\w+/ ( ( '--' <long_opt> )
    //        | ( '-' <short_opt> ((/,\w+/ | ' ') '--' long_opt>)? ) )
    // for the patterns of <short_opt> and <long_opt> see functions
    // parse_short_option and parse_long_option respectively.
    // should this be indentation sensitive?

    struct option* head = NULL;
    struct option** opt = &head;
    int option_start_column = -1; // unspecified yet.

    for (; *iter || !strncmp(iter, "\n\n", 2); iter = str_skip_line(iter)) {
        // find next line with an option.
        if (option_start_column == -1) {
            const char* opt_start = str_skip_blank(iter);
            if (*opt_start != '-')
                continue;
            option_start_column = opt_start - iter;
            iter = opt_start;
        } else {
            int i = str_skip_blank(iter) - iter;
            if (i != option_start_column)
                continue;
            iter = iter + i;
        }

        const char* line_end;
        if (*iter == '-') {
            ++iter;
            line_end = iter;
            while (*line_end && strncmp(line_end, "  ", 2) != 0
                && *line_end != '\n')
                ++line_end;

            if (!*opt)
                *opt = calloc(1, sizeof(struct option));
            (*opt)->pattern = iter;
            (*opt)->pattern_len = line_end - iter;
        } else {
            line_end = str_skip_line(iter);
            docopt_diagnostic(
                "warning", "on line: %.*s\n\n"
                           "line indentation matches that of a line with "
                           "a pattern.\n"
                           "suggestion: add more indentation.",
                line_end - iter, iter);
            continue;
        }

        if (*iter != '-') {
            // TODO: handle special case "-" used commonly for stdin
            const char* short_end = parse_short_option(*opt, iter);
            if (!short_end)
                continue;
            iter = short_end;

            if (strncmp(iter, "  ", 2) != 0) {
                // seperator one of: ' ' | ',' | ', '
                // yikes.
                if (*iter == ' ')
                    ++iter;
                if (*iter == ',')
                    ++iter;
                if (*iter == ' ')
                    ++iter;

                if (*iter != '-')
                    WARN(*opt,
                        "expected a long option '--<option>', found '%c'.",
                        *iter);
                ++iter;
            }
        }

        if (*iter == '-') {
            // TODO: handle special case "--" used commonly for stopping
            // options parsing
            ++iter;
            const char* long_end = parse_long_option(*opt, iter);
            if (!long_end)
                continue;
        }

        opt = &(*opt)->next;
    }

    return head;
}
