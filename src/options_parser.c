#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "util.h"

#define WARN_AT_(opt, iter, warn_msg, ...)                 \
    docopt_log(stderr, "warning", (iter) - (opt)->pattern, \
        (opt)->pattern_len, (opt)->pattern,                \
        warn_msg, __VA_ARGS__);

#define ERROR_AT_(opt, iter, err_msg, ...)                 \
    docopt_log(stderr, "warning", (iter) - (opt)->pattern, \
        (opt)->pattern_len, (opt)->pattern,                \
        err_msg, __VA_ARGS__);

#define ERROR_AT(opt, iter, err_msg) ERROR_AT_(opt, iter, err_msg, NULL)
#define WARN_AT(opt, iter, warn_msg) WARN_AT_(opt, iter, warn_msg, NULL)

static bool is_arg_char(char c)
{
    return (islower(c) || isdigit(c) || c == ' ' || c == '_' || c == '-');
}

static bool is_cmd_char(char c)
{
    return (islower(c) || isdigit(c) || c == '_' || c == '-');
}

void free_options_list(struct opt_spec* opt)
{
    while (opt) {
        struct opt_spec* del = opt;
        opt = opt->next;
        free(del);
    }
}

static const char* parse_argument(struct opt_spec* opt, const char* iter)
{
    // pattern: <arg> = '<' /[a-z0-9_- ]+/ '>' | /[A-Z]+[a-z0-9_- ]*/
    int len = 0;
    const char* arg = iter;
    if (*iter == '<') {
        ++arg;
        while (is_arg_char(arg[len]))
            ++len;
        if (len == 0) {
            ERROR_AT(opt, arg + len, "expected an argument name.");
            return NULL;
        } else if (arg[len] != '>') {
            ERROR_AT(opt, arg + len,
                "unterminated argument name, expected '>'.");
            return NULL;
        }
        iter += len + 2;
    } else if (isupper(*iter)) {
        while (isupper(iter[len]) && is_cmd_char(tolower(iter[len])))
            ++len;
        iter += len;
    } else {
        ERROR_AT(opt, iter, "expected an argument name.");
        return NULL;
    }

    opt->arg_name = arg;
    opt->arg_name_len = len;
    opt->prop |= OPT_ARG;
    return iter;
}

static const char* parse_opt_arg_spec(struct opt_spec* opt, const char* iter)
{
    // pattern: '[' '='? <arg> ']' | '=' <arg> | <arg>
    bool is_optional = false;
    if (*iter == '[') {
        ++iter;
        is_optional = true;
    }

    if (*iter == '=')
        ++iter;

    // save old spec data
    const char* old_arg = opt->arg_name;
    int old_len = opt->arg_name_len;
    enum opt_prop old_prop = opt->prop;

    iter = parse_argument(opt, iter);
    if (!iter)
        return NULL;

    if (old_prop & OPT_ARG) {
        if (opt->arg_name_len != old_len
            || strncmp(opt->arg_name, old_arg, old_len) != 0
            || is_optional != (bool)(old_prop & OPT_ARG_OPTIONAL)) {
            // arg spec doesn't match
            WARN_AT_(opt, old_arg,
                "argument specification overrides previous "
                "one, expected %s'%.*s'.",
                (old_prop & OPT_ARG_OPTIONAL) ? "optional " : "",
                old_len, old_arg);
        }
    }

    if (is_optional) {
        if (*iter != ']') {
            ERROR_AT(opt, iter, "expected ']'.");
            return NULL;
        }
        ++iter;
        opt->prop |= OPT_ARG_OPTIONAL;
    }

    return iter;
}

static const char* parse_long_option(
    struct opt_spec* opt, const char* iter, const char* const end)
{
    // long option pattern: ([no-])? /[a-zA-Z0-9_-]+/ <opt_arg_spec>
    opt->prop |= OPT_LONG;
    if (iter == end) {
        // special doubledash
        opt->prop |= OPT_DOUBLEDASH;
        return end;
    }
    if (*iter == '[') {
        ++iter;
        if (strncmp(iter, "no-", 3) != 0) {
            ERROR_AT(opt, iter, "only [no-] is allowed.");
            return NULL;
        }
        iter += 3;
        if (*iter != ']') {
            ERROR_AT(opt, iter, "expected ']'.");
            return NULL;
        }
        ++iter;
        opt->prop |= OPT_NEGATABLE;
    } else if (!strncmp(iter, "no-", 3)) {
        opt->prop |= OPT_NEGATED;
    }

    int len = 0;
    while (isupper(iter[len]) || is_cmd_char(iter[len]))
        ++len;
    if (len == 0) {
        ERROR_AT(opt, iter, "expected an option name.");
        return NULL;
    }

    opt->olong = iter;
    opt->olong_len = len;
    iter += len;

    return iter;
}

const char* parse_short_option(
    struct opt_spec* opt, const char* iter, const char* end)
{
    // short option pattern: /[a-zA-z0-9]/ <opt_arg_spec>?
    opt->prop |= OPT_SHORT;
    if (iter == end) {
        // special single dash
        opt->prop |= OPT_DASH;
        return end;
    }
    if (!isalnum(*iter)) {
        ERROR_AT(opt, iter, "expected an alphanumeric character");
        return NULL;
    }

    opt->oshort = *iter;
    ++iter;

    return iter;
}

struct opt_spec* get_options_list(const char* iter)
{
    // options pattern : /^\w+/ ( ( '--' <long_opt> )
    //                 | ( '-' <short_opt> ((/,\w+/ | ' ') '--' long_opt>)? ) )
    struct opt_spec* head = NULL;
    struct opt_spec** opt = &head;
    // for tracking the patterns column start
    int indent = -1;
    // used to report warning about erroneous non-pattern formatting only once
    bool warned_about_formatting = false;
    // the pattern line end
    const char* end = NULL;

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
            int line_len = str_skip_line(iter) - iter - 1;
            docopt_log(
                stderr, "warning", col, line_len, iter,
                "line indentation matches that of a line with a pattern.\n"
                "suggestion: add more indentation.\n"
                "this warning is reported only once, subsequent formatting "
                "errors won't be reported.");
            warned_about_formatting = true;
            continue;
        }
        iter += col;

        end = iter;
        while (*end && strncmp(end, "  ", 2) != 0 && *end != '\n')
            ++end;

        if (!*opt)
            *opt = calloc(1, sizeof(**opt));
        else
            memset(*opt, 0, sizeof(**opt));
        (*opt)->pattern = iter;
        (*opt)->pattern_len = end - iter;

        if (iter[0] == '-' && iter[1] != '-') {
            ++iter;
            const char* next = parse_short_option(*opt, iter, end);
            if (!next)
                continue;
            iter = next;

            next = str_skip_blank(iter);
            if (iter != end && *next != ',' && *next != '-') {
                next = parse_opt_arg_spec(*opt, next);
                if (!next)
                    continue;
                iter = next;
                next = str_skip_blank(iter);
            }

            if (iter != end && (isspace(*iter) || *next == ',')) {
                if (*next == ',') {
                    ++next;
                    next = str_skip_blank(next);
                }
                iter = next;
                if (strncmp(iter, "--", 2) != 0) {
                    ERROR_AT(*opt, iter, "expected a long option.");
                    continue;
                }
            }
        }

        if (!strncmp(iter, "--", 2)) {
            iter += 2;
            const char* next = parse_long_option(*opt, iter, end);
            if (!next)
                continue;
            iter = next;
            if (iter != end) {
                next = parse_opt_arg_spec(*opt, str_skip_blank(iter));
                if (!next)
                    continue;
                iter = next;
            }
        }

        if (iter != end) {
            ERROR_AT(*opt, iter, "unexpected character.");
            continue;
        }
        opt = &(*opt)->next;
    }

    // make sure we don't leave an allocated but not used memory
    if (*opt)
        free(*opt);
    return head;
}
