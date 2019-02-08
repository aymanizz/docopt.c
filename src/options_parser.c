#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "util.h"

#define WARN(opt, fmt, ...) docopt_diagnostic(    \
    "warning", "on pattern: %.*s\n" fmt "\n\n", \
    (opt)->pattern_len, (opt)->pattern, __VA_ARGS__)

#define ERROR(opt, fmt, ...) docopt_diagnostic( \
    "error", "on pattern: %.*s\n" fmt "\n\n", \
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
    const char* arg;
    if (*iter == '<') {
        ++iter;
        arg = iter;
        while (is_arg_char(iter[len]))
            ++len;
        if (len == 0 || iter[len] != '>') {
            ERROR(opt, "unterminated argument name, expected '>' before '%c'.",
                iter[len]);
            return NULL;
        }
        iter += len + 1;
    } else if (isupper(*iter)) {
        arg = iter;
        while (isupper(iter[len]) && is_cmd_char(tolower(iter[len])))
            ++len;
        iter += len;
    } else {
        ERROR(opt, "expected an argument name, found '%c'", *iter);
        return NULL;
    }

    bool new_spec = !(opt->prop & OPT_ARG);
    if (!new_spec) {
        if (strncmp(opt->arg_name, arg, len) != 0
            || is_optional != (bool)(opt->prop & OPT_ARG_OPTIONAL)) {
            // arg spec doesn't match
            const char* old_optional =
                (opt->prop & OPT_ARG_OPTIONAL) ? "optional " : "";
            const char* new_optional = is_optional ? "optional " : "";
            WARN(opt, "argument specification overrides previous one, "
                      "expected %s'%.*s', found %s'%.*s'.",
                old_optional, opt->arg_name_len, opt->arg_name,
                new_optional, len, arg);
        }
    }

    opt->arg_name = arg;
    opt->arg_name_len = len;
    opt->prop |= OPT_ARG;

    if (is_optional) {
        if (*iter != ']') {
            ERROR(opt, "expected ']', found '%c'.", *iter);
            return NULL;
        }
        ++iter;
        opt->prop |= OPT_ARG_OPTIONAL;
    }

    return iter;
}

static const char* parse_long_option(
    struct option* opt, const char* iter)
{
    // long option pattern: ([no-])? /[a-zA-Z0-9_-]+/ <opt_arg_spec>
    if (*iter == '[') {
        ++iter;
        if (strncmp(iter, "no-", 3) != 0) {
            ERROR(opt, "only [no-] is allowed, found '%c'.", *iter);
            return NULL;
        }
        iter += 3;
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
    if (!*end || *end == ',' || *end == '\n' || !strncmp(iter, "  ", 2)) {
        return iter;
    }

    iter = parse_opt_arg_spec(opt, iter);
    if (!iter)
        return NULL;

    return iter;
}

struct option* get_options_list(const char* iter)
{
    // options pattern : /^\w+/ ( ( '--' <long_opt> )
    //                 | ( '-' <short_opt> ((/,\w+/ | ' ') '--' long_opt>)? ) )

    struct option* head = NULL;
    struct option** opt = &head;
    // for tracking the patterns column start
    int indent = -1;
    // used to report warning about erroneous non-pattern formatting only once
    bool warned_about_formatting = false;

    // options section end with an unindented line (has an indentation level
    // that is below the indentation level of option patterns), or a line that
    // has two consecutive newlines
    for (; strncmp(iter, "\n\n", 2) != 0; iter = str_skip_line(iter)) {
        int col = str_skip_blank(iter) - iter;
        if (indent == -1) {
            if (iter[col] != '-')
                continue;
            indent = col;
        } else if (iter[col] == '\n' || col > indent) {
            continue;
        } else if (col < indent) {
            break;
        } else if (iter[col] != '-') {
            // warn about erroneous formatting.
            if (warned_about_formatting)
                continue;
            const char* line = iter + col;
            int line_len = str_skip_line(line) - line;
            docopt_diagnostic(
                "warning",
                "on line: %.*s\n"
                "line indentation matches that of a line with a pattern.\n"
                "suggestion: add more indentation.\n"
                "this warning is reported only once, subsequent formatting "
                "errors won't be reported.\n\n",
                line_len - 1, line);
            warned_about_formatting = true;
            continue;
        }
        iter += col;

        int pattern_len = 0;
        while (iter[pattern_len] && strncmp(iter + pattern_len, "  ", 2) != 0
            && iter[pattern_len] != '\n') {
            ++pattern_len;
        }

        if (!*opt)
            *opt = calloc(1, sizeof(**opt));
        else
            memset(*opt, 0, sizeof(**opt));
        (*opt)->pattern = iter;
        (*opt)->pattern_len = pattern_len;

        if (iter[0] == '-' && iter[1] != '-') {
            // TODO: handle special case "-" used commonly for stdin
            const char* short_end = parse_short_option(*opt, iter + 1);
            if (!short_end)
                continue;
            iter = short_end;

            const char *next_col = str_skip_blank(iter);
            if (*next_col == ',' || strncmp(iter, "  ", 2) != 0) {
                // seperator one of: ' ' | ',' |
                if (*next_col == ',') {
                    ++next_col;
                    next_col = str_skip_blank(next_col);
                }
                iter = next_col;

                if (strncmp(iter, "--", 2) != 0)
                    WARN(*opt,
                        "expected a long option '--<option>', found '%c'.",
                        *iter);
            }
        }

        if (!strncmp(iter, "--", 2)) {
            // TODO: handle special case "--" used commonly for stopping
            // options parsing
            const char* long_end = parse_long_option(*opt, iter + 2);
            if (!long_end)
                continue;
            iter = long_end;
        }

        opt = &(*opt)->next;
    }

    // make sure we don't leave an allocated but not used memory
    if (*opt)
        free(*opt);
    return head;
}
