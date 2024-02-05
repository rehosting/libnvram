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

    // TODO: we could support retries
    igloo_hypercall2(cmd, (unsigned long)m.value, strlen(m.value));
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
        if (minimal_strncmp(s1, TARGET_VALUE, sizeof(TARGET_VALUE), 0) == 0) {
            log_match((match) {STRCMP, s2});
        } else if (minimal_strncmp(s2, TARGET_VALUE, sizeof(TARGET_VALUE), 0) == 0) {
            log_match((match) {STRCMP, s1});
        }
    }
    return s1[i] - s2[i];
}

int minimal_strncmp(const char *s1, const char *s2, size_t n, short do_log) {
    for (size_t i = 0; i < n; ++i) {
        if (s1[i] != s2[i] || !s1[i]) {
            // Additional logic to log if TARGET_VALUE is present
            if (do_log) {
                if (minimal_strncmp(s1, TARGET_VALUE, sizeof(TARGET_VALUE), 0) == 0) {
                    log_match((match) {STRNCMP, s2});
                } else if (minimal_strncmp(s2, TARGET_VALUE, sizeof(TARGET_VALUE), 0) == 0) {
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
    return minimal_strncmp(s1, s2, n, 1);
}

char *minimal_getenv(const char *name) {
    size_t len = minimal_strlen(name);
    for (char **env = environ; *env; ++env) {
        if (minimal_strncmp(*env, name, len, 0) == 0 && (*env)[len] == '=') {
            return *env + len + 1; // Return the value part of the KEY=value pair
        }
    }
    return NULL; // Not found
}

char *getenv(const char *name) {
    // Call the original getenv
    char *result = minimal_getenv(name);

    // if the first len(TARGET_VALUE) characters of result match our target, log it
    if (result && minimal_strncmp(result, TARGET_VALUE, minimal_strlen(result), 0) == 0) {
        log_match((match) {GETENV, name});
    }

    // Return the original result
    return result;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;

    // It's not actually a match - strstr is a bit more
    // involved so we just pass the args out every time
    igloo_hypercall2(104, (unsigned long)haystack, (unsigned long)needle);

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
