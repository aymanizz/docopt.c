#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "util.h"

#define WARN_AT_(st, warn_msg, ...)                       \
    docopt_log(stderr, "warning", (st)->it - (st)->begin, \
        (st)->end - (st)->begin, (st)->begin,             \
        warn_msg, __VA_ARGS__);

#define ERROR_AT_(st, err_msg, ...)                     \
    docopt_log(stderr, "error", (st)->it - (st)->begin, \
        (st)->end - (st)->begin, (st)->begin,           \
        err_msg, __VA_ARGS__);

#define ERROR_AT(st, err_msg) ERROR_AT_(st, err_msg, NULL)
#define WARN_AT(st, warn_msg) WARN_AT_(st, warn_msg, NULL)

struct tok_state {
    const char* begin;
    const char* it;
    const char* end;
};

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
        if (del->prop & OPT_ARG)
            free(del->arg);
        free(del);
    }
}

static bool parse_argument(struct arg_spec *spec, struct tok_state* st)
{
    // pattern: <arg> = '<' /[a-z0-9_- ]+/ '>' | /[A-Z]+[a-z0-9_- ]*/
    int len = 0;
    const char* arg = st->it;
    if (*arg == '<') {
        ++arg;
        while (is_arg_char(arg[len]))
            ++len;
        if (len == 0) {
            ERROR_AT(st, "expected an argument name.");
            return false;
        } else if (arg[len] != '>') {
            ERROR_AT(st, "unterminated argument name, expected '>'.");
            return false;
        }
        st->it += len + 2;
    } else if (isupper(*arg)) {
        while (isupper(arg[len]) && is_cmd_char(tolower(arg[len])))
            ++len;
        st->it += len;
    } else {
        ERROR_AT(st, "expected an argument name.");
        return false;
    }

    spec->name = arg;
    spec->length = len;
    return true;
}

bool parse_opt_arg_spec(
    struct opt_spec *opt, struct tok_state *st)
{
    // pattern: '[' '='? <arg> ']' | '=' <arg> | <arg>
    bool is_optional = false;
    if (*st->it == '[') {
        ++st->it;
        is_optional = true;
    }

    if (*st->it == '=')
        ++st->it;

    // save old spec data if any
    bool old_is_optional = opt->prop & OPT_ARG_OPTIONAL;
    struct arg_spec old_arg = { 0 };
    if (opt->prop & OPT_ARG) {
        old_arg.name = opt->arg->name;
        old_arg.length = opt->arg->length;
    } else if (!opt->arg) {
        opt->arg = malloc(sizeof(*opt->arg));
    }

    if (!parse_argument(opt->arg, st))
        goto error;

    if (old_arg.name) {
        if (old_is_optional != is_optional
            || old_arg.length != opt->arg->length
            || strncmp(opt->arg->name, old_arg.name, old_arg.length) != 0) {
            // arg spec doesn't match
            WARN_AT_(st,
                "argument specification overrides previous "
                "one, expected '%s%.*s'.",
                is_optional ? "optional " : "",
                old_arg.length, old_arg.name);
        }
    }

    if (is_optional) {
        if (*st->it != ']') {
            ERROR_AT(st, "expected ']'.");
            goto error;
        }
        ++st->it;
        opt->prop |= OPT_ARG_OPTIONAL;
    }

    opt->prop |= OPT_ARG;
    return true;
error:
    if (opt->arg) {
        free(opt->arg);
        opt->arg = NULL;
    }
    return false;
}

static bool parse_long_option(
    struct opt_spec* opt, struct tok_state* st)
{
    // long option pattern: ([no-])? /[a-zA-Z0-9_-]+/ <opt_arg_spec>
    if (st->it == st->end) {
        // special doubledash
        opt->prop |= OPT_DOUBLEDASH;
        return true;
    }
    if (*st->it == '[') {
        ++st->it;
        if (strncmp(st->it, "no-", 3) != 0) {
            ERROR_AT(st, "only [no-] is allowed.");
            return false;
        }
        st->it += 3;
        if (*st->it != ']') {
            ERROR_AT(st, "expected ']'.");
            return false;
        }
        ++st->it;
        opt->prop |= OPT_NEGATABLE;
    } else if (!strncmp(st->it, "no-", 3)) {
        opt->prop |= OPT_NEGATED;
    }

    int len = 0;
    while (isupper(st->it[len]) || is_cmd_char(st->it[len]))
        ++len;
    if (len == 0) {
        ERROR_AT(st, "expected an option name.");
        return false;
    }

    opt->olong = st->it;
    opt->olong_len = len;
    st->it += len;

    return true;
}

static bool parse_short_option(
    struct opt_spec* opt, struct tok_state* st)
{
    // short option pattern: /[a-zA-z0-9]/ <opt_arg_spec>?
    opt->prop |= OPT_SHORT;
    if (st->it == st->end) {
        // special single dash
        opt->prop |= OPT_DASH;
        return st->end;
    }
    if (!isalnum(*st->it)) {
        ERROR_AT(st, "expected an alphanumeric character");
        return false;
    }

    opt->oshort = *st->it;
    ++st->it;

    return st->it;
}

struct opt_spec* get_options_list(const char* iter)
{
    // options pattern : /^\w+/ ( ( '--' <long_opt> )
    //                 | ( '-' <short_opt> ((/,\w+/ | ' ') '--' long_opt>)? ) )
    struct tok_state st = { .it = iter, .begin = iter };
    struct opt_spec* head = NULL;
    struct opt_spec** opt = &head;
    // for tracking the patterns column start
    int indent = -1;
    // used to report warning about erroneous non-pattern formatting only once
    bool warned_about_formatting = false;

    // options section end with an unindented line (has an indentation level
    // that is below the indentation level of option patterns), or a line that
    // has two consecutive newlines
    for (; strncmp(st.it, "\n\n", 2) != 0; st.it = str_skip_line(st.it)) {
        int col = str_skip_blank(st.it) - st.it;
        if (indent == -1) {
            if (st.it[col] != '-')
                continue;
            indent = col;
        } else if (st.it[col] == '\n' || col > indent) {
            continue;
        } else if (col < indent) {
            break;
        } else if (st.it[col] != '-') {
            // warn about erroneous formatting.
            if (warned_about_formatting)
                continue;
            int line_len = str_skip_line(st.it) - st.it - 1;
            docopt_log(
                stderr, "warning", col, line_len, st.it,
                "line indentation matches that of a line with a pattern.\n"
                "suggestion: add more indentation.\n"
                "this warning is reported only once, subsequent formatting "
                "errors won't be reported.");
            warned_about_formatting = true;
            continue;
        }
        st.it += col;

        st.begin = st.it;
        st.end = st.it;
        while (*st.end && strncmp(st.end, "  ", 2) != 0 && *st.end != '\n')
            ++st.end;

        if (!*opt) {
            *opt = calloc(1, sizeof(**opt));
        } else {
            if (!((*opt)->prop & OPT_ARG))
                free((*opt)->arg);
            memset(*opt, 0, sizeof(**opt));
        }

        if (st.it[0] == '-' && st.it[1] != '-') {
            ++st.it;
            if (!parse_short_option(*opt, &st))
                continue;

            const char* next = str_skip_blank(st.it);
            if (st.it != st.end && *next != ',' && *next != '-') {
                st.it = next;
                if (!parse_opt_arg_spec(*opt, &st))
                    continue;
                next = str_skip_blank(st.it);
            }

            if (st.it != st.end && (isspace(*st.it) || *next == ',')) {
                if (*next == ',') {
                    ++next;
                    next = str_skip_blank(next);
                }
                st.it = next;
                if (strncmp(st.it, "--", 2) != 0) {
                    ERROR_AT(&st, "expected a long option.");
                    continue;
                }
            }
        }

        if (!strncmp(st.it, "--", 2)) {
            st.it += 2;
            if (!parse_long_option(*opt, &st))
                continue;
            if (st.it != st.end) {
                st.it = str_skip_blank(st.it);
                if (!parse_opt_arg_spec(*opt, &st))
                    continue;
            }
        }

        if (st.it != st.end) {
            ERROR_AT(&st, "unexpected character.");
            continue;
        }
        opt = &(*opt)->next;
    }

    // make sure we don't leave an allocated but not used memory
    if (*opt) {
        free_options_list(*opt);
        *opt = NULL;
    }
    return head;
}
