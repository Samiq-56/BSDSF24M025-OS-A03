#include "shell.h"

/*
 * Feature 8 - shell variables (VARNAME=value, $VARNAME, set).
 *
 * Storage: a singly linked list of key-value pairs (the PDF suggests a
 * linked list or a hash table; a list is enough for a teaching shell and
 * keeps the code short enough to explain in a viva). Each node owns
 * heap copies of its name and value (strdup-style, via malloc+strcpy,
 * because the build is -std=c11 and strdup() is POSIX).
 *
 * Assignment (VARNAME=value) is a built-in action: it never forks.
 * run_single_command() in main.c detects a line that starts with a
 * valid name followed by '=' and hands the whole line to
 * handle_assignment() before any tokenizing / piping happens, so a
 * value may contain spaces when quoted, e.g. MESSAGE="Hello, World!".
 *
 * Expansion ($VARNAME) is a pre-execution step: after tokenizing but
 * before executing (built-in or external), expand_variables() walks the
 * arglist and replaces every token starting with '$' by the variable's
 * value. A token is replaced by a freshly malloc'd string - the PDF
 * warns about this - because the new value can be longer than the
 * ARGLEN (30) buffer tokenize() gave the token, so writing in place
 * would overflow it. An undefined variable expands to the empty
 * string (bash behaviour). If a name is not a shell variable,
 * get_variable() also falls back to the process environment
 * (getenv), so $HOME keeps working like in a real shell.
 */

/* One variable: name, value, next node. */
struct var_node {
    char* name;
    char* value;
    struct var_node* next;
};

/* Head of the list; NULL until the first assignment. */
static struct var_node* var_list = NULL;

/* strdup() without relying on POSIX feature macros (we build -std=c11). */
static char* dup_string(const char* s) {
    char* copy = (char*) malloc(strlen(s) + 1);
    if (copy != NULL) {
        strcpy(copy, s);
    }
    return copy;
}

/* True if s is a valid variable name: starts with a letter or '_',
   then letters, digits or '_' only (same rule as POSIX shells). */
static int is_valid_name(const char* s, size_t len) {
    size_t i;
    if (len == 0) {
        return 0;
    }
    if (!( (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') ||
           s[0] == '_' )) {
        return 0;
    }
    for (i = 1; i < len; i++) {
        if (!( (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') ||
               (s[i] >= '0' && s[i] <= '9') || s[i] == '_' )) {
            return 0;
        }
    }
    return 1;
}

/* Store name=value. Re-assignment replaces the old value in place
   (the old string is freed); a new name gets a new list node. */
void set_variable(const char* name, const char* value) {
    struct var_node* node = var_list;
    char* name_copy;
    char* value_copy;

    while (node != NULL) {
        if (strcmp(node->name, name) == 0) {
            /* Already defined: swap the value, keep the node. */
            value_copy = dup_string(value);
            if (value_copy != NULL) {
                free(node->value);
                node->value = value_copy;
            }
            return;
        }
        node = node->next;
    }

    /* New variable: prepend a node (order in `set` is newest-first;
       that is fine - `set` is a listing, not an ordered history). */
    name_copy = dup_string(name);
    value_copy = dup_string(value);
    node = (struct var_node*) malloc(sizeof(struct var_node));
    if (name_copy == NULL || value_copy == NULL || node == NULL) {
        free(name_copy);
        free(value_copy);
        free(node);
        return;
    }
    node->name = name_copy;
    node->value = value_copy;
    node->next = var_list;
    var_list = node;
}

/* Look a variable up. Returns the stored value, or the process
   environment's value (getenv), or NULL if it is defined nowhere. */
const char* get_variable(const char* name) {
    struct var_node* node = var_list;
    while (node != NULL) {
        if (strcmp(node->name, name) == 0) {
            return node->value;
        }
        node = node->next;
    }
    return getenv(name); /* shell variables shadow nothing here: a
                            name is either ours or the environment's */
}

/* `set`: print every defined shell variable as NAME=value, one per
   line. Environment variables are NOT listed (bash's `set` shows
   those too, but only ours are "defined" by this shell). */
void print_variables(void) {
    struct var_node* node = var_list;
    while (node != NULL) {
        printf("%s=%s\n", node->name, node->value);
        node = node->next;
    }
}

/*
 * is_assignment_line - is this whole (trimmed) line an assignment?
 * True only when the line starts with a valid variable name directly
 * followed by '='. That rules out ordinary commands ("echo a=b" starts
 * with "echo", which has no '=' after it) and names with spaces or a
 * leading digit ("1ABC=x" is not an assignment - it falls through to
 * execute() and fails as an unknown command, like in bash).
 */
int is_assignment_line(const char* line) {
    const char* eq;
    if (line == NULL) {
        return 0;
    }
    eq = strchr(line, '=');
    if (eq == NULL) {
        return 0;
    }
    return is_valid_name(line, (size_t) (eq - line));
}

/*
 * handle_assignment - store the assignment in one line.
 *
 * The value is everything after the first '='. A value wrapped in a
 * matching pair of quotes - MESSAGE="Hello, World!" or NAME='x y' -
 * has the quotes stripped, so `echo $MESSAGE` prints Hello, World!
 * without the quote characters (bash does the same). Unquoted values
 * are stored as-is.
 */
void handle_assignment(const char* line) {
    const char* eq = strchr(line, '=');
    const char* value = eq + 1;
    size_t name_len = (size_t) (eq - line);
    size_t value_len = strlen(value);
    char* name;

    /* Strip one matching pair of surrounding quotes, if present. */
    if (value_len >= 2 &&
        ((value[0] == '"' && value[value_len - 1] == '"') ||
         (value[0] == '\'' && value[value_len - 1] == '\''))) {
        value++;
        value_len -= 2;
    }

    name = (char*) malloc(name_len + 1);
    if (name == NULL) {
        return;
    }
    strncpy(name, line, name_len);
    name[name_len] = '\0';

    /* set_variable() copies the value itself, so pass a temporary
       NUL-terminated copy when quotes were stripped. */
    if (value_len != strlen(value)) {
        char* vcopy = (char*) malloc(value_len + 1);
        if (vcopy != NULL) {
            strncpy(vcopy, value, value_len);
            vcopy[value_len] = '\0';
            set_variable(name, vcopy);
            free(vcopy);
        }
    } else {
        set_variable(name, value);
    }
    free(name);
}

/*
 * expand_variables - the pre-execution expansion step (PDF Task 3).
 *
 * For every token starting with '$', take the variable name (the
 * [A-Za-z0-9_] run after the '$'), look it up, and rebuild the token
 * as value + any trailing characters (so "$MSG!" becomes the value
 * followed by '!'). The old token is freed and replaced by a fresh
 * allocation sized for the result - never written in place, because
 * tokenize() only gave each token ARGLEN bytes. Undefined variables
 * expand to the empty string.
 */
void expand_variables(char** arglist) {
    int i;
    if (arglist == NULL) {
        return;
    }
    for (i = 0; arglist[i] != NULL; i++) {
        char* token = arglist[i];
        if (token[0] == '$' && token[1] != '\0') {
            const char* p = token + 1;
            size_t name_len = 0;
            char* name;
            const char* value;
            char* expanded;

            while ((p[name_len] >= 'A' && p[name_len] <= 'Z') ||
                   (p[name_len] >= 'a' && p[name_len] <= 'z') ||
                   (p[name_len] >= '0' && p[name_len] <= '9') ||
                   p[name_len] == '_') {
                name_len++;
            }
            if (name_len == 0) {
                continue; /* bare '$' or '$!' etc: leave the token alone */
            }

            name = (char*) malloc(name_len + 1);
            if (name == NULL) {
                continue;
            }
            strncpy(name, p, name_len);
            name[name_len] = '\0';

            value = get_variable(name);
            if (value == NULL) {
                value = ""; /* undefined variable -> empty string */
            }
            /* New token = value + whatever followed the name. */
            expanded = (char*) malloc(strlen(value) +
                                      strlen(p + name_len) + 1);
            if (expanded != NULL) {
                strcpy(expanded, value);
                strcat(expanded, p + name_len);
                free(token);
                arglist[i] = expanded;
            }
            free(name);
        }
    }
}
