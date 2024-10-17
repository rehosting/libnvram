#ifndef INCLUDE_STRINGS_H
#define INCLUDE_STRINGS_H

#include "libhc/hypercall.h"
#include <stddef.h>

#define PAGE_IN(s) _libinject_page_in_str((volatile const char *)(s))

#define TARGET_VALUE "DYNVALDYNVALDYNVAL"
extern char **environ;

// Internal data types
typedef enum {
    STRCMP,
    STRNCMP,
    GETENV
} source;
typedef struct {
    source source;
    const char* value;
} match;

// Internal methods
void _libinject_log_match(match m);
void _libinject_page_in_str(volatile const char *s);
size_t _libinject_minimal_strlen(const char *s);
int _libinject_minimal_strncmp(short do_log, size_t n, const char *s1, const char *s2);
int _libinject_minimal_strcmp(const char *s1, const char *s2, short do_log);
char *_libinject_minimal_getenv(const char *name);

// External methods: getenv and strstr
int libinject_strcmp(const char *s1, const char *s2);
int libinject_strncmp(const char *s1, const char *s2, size_t n);
char *libinject_getenv(const char *name);
char *libinject_strstr(const char *haystack, const char *needle);

#endif
