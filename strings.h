#ifndef INCLUDE_STRINGS_H
#define INCLUDE_STRINGS_H

#include "hypercall.h"
#include <stddef.h>

#define TARGET_VALUE "DYNVALDYNVALDYNVAL"
extern char **environ;

size_t minimal_strlen(const char *s);
inline int minimal_strncmp(const char *s1, const char *s2, size_t n, short do_log);
int minimal_strcmp(const char *s1, const char *s2, short do_log);
char *minimal_getenv(const char *name);
char *getenv(const char *name);

#endif
