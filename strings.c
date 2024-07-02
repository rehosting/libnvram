#ifndef INCLUDE_STRINGS_C
#define INCLUDE_STRINGS_C

#include "strings.h"

// ENUM for sources: strcmp, strncmp, getenv
enum source {
    STRCMP,
    STRNCMP,
    GETENV
};
typedef struct {
    enum source source;
    const char* value;
} match;

void igloo_page_in_str(volatile const char *s) {
    while (*s++);
}

void log_match(match m) {
    int cmd = -1;
    switch (m.source) {
        case STRCMP:
            cmd = 101;
            break;
        case STRNCMP:
            cmd = 102;
            break;
        case GETENV:
            cmd = 103;
            break;
    }

    int rv = igloo_hypercall2(cmd, (unsigned long)m.value, minimal_strlen(m.value));
    while (rv == 1) {
        PAGE_IN(m.value);
        rv = igloo_hypercall2(cmd, (unsigned long)m.value, minimal_strlen(m.value));
    }
}

// Simple string length calculation
size_t minimal_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) ++len;
    return len;
}

int minimal_strcmp(const char *s1, const char *s2, short do_log) {
    size_t i = 0;
    while (s1[i] && s2[i]) {
        if (s1[i] != s2[i]) {
            break;
        }
        i++;
    }
    if (do_log) {
        // Additional logic to log if TARGET_VALUE is present
        if (minimal_strncmp(0, sizeof(TARGET_VALUE), s1, TARGET_VALUE) == 0) {
            log_match((match) {STRCMP, s2});
        } else if (minimal_strncmp(0, sizeof(TARGET_VALUE), s2, TARGET_VALUE) == 0) {
            log_match((match) {STRCMP, s1});
        }
    }
    return s1[i] - s2[i];
}

// XXX: weird arg order here which is critical - we don't want our dynamic analysis to
// detect function calls with DYNVAL in arg1 or arg2.
int minimal_strncmp(short do_log, size_t n, const char *s1, const char *s2) {
    for (size_t i = 0; i < n; ++i) {
        if (s1[i] != s2[i] || !s1[i]) {
            // Additional logic to log if TARGET_VALUE is present
            if (do_log) {
                if (minimal_strncmp(0, sizeof(TARGET_VALUE), s1, TARGET_VALUE) == 0) {
                    log_match((match) {STRNCMP, s2});
                } else if (minimal_strncmp(0, sizeof(TARGET_VALUE), s2, TARGET_VALUE) == 0) {
                    log_match((match) {STRNCMP, s1});
                }
            }
            return s1[i] - s2[i];
        }
    }
    return 0;
}

int strcmp(const char *s1, const char *s2) {
    return minimal_strcmp(s1, s2, 1);
}

int strncmp(const char *s1, const char *s2, size_t n) {
    return minimal_strncmp(1, n, s1, s2);
}

char *minimal_getenv(const char *name) {
    size_t len = minimal_strlen(name);
    for (char **env = environ; *env; ++env) {
        if (minimal_strncmp(0, len, *env, name) == 0 && (*env)[len] == '=') {
            return *env + len + 1; // Return the value part of the KEY=value pair
        }
    }
    return NULL; // Not found
}

char *getenv(const char *name) {
    // Call the original getenv
    char *result = minimal_getenv(name);

    // if the first len(TARGET_VALUE) characters of result match our target, log it
    if (result && minimal_strncmp(0, minimal_strlen(result), result, TARGET_VALUE) == 0) {
        log_match((match) {GETENV, name});
    }

    // Return the original result
    return result;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;

    // It's not actually a match - strstr is a bit more
    // involved so we just pass the args out every time
    int rv = igloo_hypercall2(104, (unsigned long)haystack, (unsigned long)needle);

    // Hypercall returns -1 on read fail. If no hypercall is available we'd get a different reval
    // so we wouldn't infinite loop. Hopefully.
    while (rv == 1) {
        PAGE_IN(haystack);
        PAGE_IN(needle);
        rv = igloo_hypercall2(104, (unsigned long)haystack, (unsigned long)needle);
    }

    for (; *haystack; ++haystack) {
        const char *h = haystack;
        const char *n = needle;

        while (*n && *h && *h == *n) {
            ++h;
            ++n;
        }

        if (!*n) {
            // Found needle in haystack
            return (char *)haystack;
        }
    }

    return NULL;
}


#endif
