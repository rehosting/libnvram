#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <mntent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mount.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <mntent.h>
#include <fcntl.h>

#include "nvram.h"
#include "config.h"
#include "strings.h"

// https://lkml.org/lkml/2007/3/9/10
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + sizeof(typeof(int[1 - 2 * !!__builtin_types_compatible_p(typeof(arr), typeof(&arr[0]))])) * 0)

#define PRINT_MSG(fmt, ...) do { if (DEBUG) { fprintf(stderr, "%s: "fmt, __FUNCTION__, __VA_ARGS__); } } while (0)

/* Global variables */
static int init = 0;
#define FIRMAE_NVRAM 1

static int _libinject_flock_asm(int fd, int op) {
    // File lock with SYS_flock. We do this in assembly
    // for portability - libc may not be available / match versions
    // with the library we're building
    int retval;
#if defined(__mips64__)
    asm volatile(
    "daddiu $a0, %1, 0\n"  // Move fd to $a0
    "daddiu $a1, %2, 0\n"  // Move op to $a1
    "li $v0, %3\n"         // Load SYS_flock (the system call number) into $v0
    "syscall\n"            // Make the system call
    "move %0, $v0\n"       // Move the result from $v0 to retval
    : "=r" (retval)        // Output
    : "r" (fd), "r" (op), "i" (SYS_flock)  // Inputs
    : "v0", "a0", "a1"     // Clobber list
);
#elif defined(__mips__)
    asm volatile(
    "move $a0, %1\n"        // Correctly move fd (from C variable) to $a0
    "move $a1, %2\n"        // Correctly move op (from C variable) to $a1
    "li $v0, %3\n"          // Load the syscall number for flock into $v0
    "syscall\n"             // Perform the syscall
    "move %0, $v0"          // Move the result from $v0 to retval
    : "=r" (retval)         // Output
    : "r" (fd), "r" (op), "i" (SYS_flock) // Inputs; "i" for immediate syscall number
    : "v0", "a0", "a1"      // Clobber list
);
#elif defined(__arm__)
    asm volatile(
    "mov r0, %1\n"  // Move fd to r0, the first argument for the system call
    "mov r1, %2\n"  // Move op to r1, the second argument for the system call
    "mov r7, %3\n"  // Move SYS_flock (the system call number) to r7
    "svc 0x00000000\n"  // Make the system call
    "mov %[result], r0"  // Move the result from r0 to retval
    : [result]"=r" (retval)  // Output
    : "r"(fd), "r"(op), "i"(SYS_flock)  // Inputs
    : "r0", "r1", "r7"  // Clobber list
);
#elif defined(__aarch64__)  // AArch64
    // XXX: using %w registers for 32-bit movs. This made the compiler
    // happy but I'm not sure why we can't be operating on 64-bit ints
    asm volatile(
    "mov w0, %w1\n"        // Move fd to w0, the first argument for the system call
    "mov w1, %w2\n"        // Move op to w1, the second argument for the system call
    "mov x8, %3\n"         // Move SYS_flock (the system call number) to x8
    "svc 0\n"              // Make the system call (Supervisor Call)
    "mov %w0, w0\n"        // Move the result from w0 to retval
    : "=r" (retval)        // Output
    : "r" (fd), "r" (op), "i" (SYS_flock)  // Inputs
    : "x0", "x1", "x8"     // Clobber list
);
#elif defined(__x86_64__)  // x86_64
    // XXX: movl's for 32-bit movs. This made the compiler
    // happy but I'm not sure why we can't be operating on 64-bit ints
    // I think it should be fine though
    asm volatile(
    "movl %1, %%edi\n"       // Move fd to rdi (1st argument)
    "movl %2, %%esi\n"       // Move op to rsi (2nd argument)
    "movl %3, %%eax\n"       // Move SYS_flock to rax (syscall number)
    "syscall\n"             // Make the syscall
    "movl %%eax, %0\n"       // Move the result from rax to retval
    : "=r" (retval)         // Output
    : "r" (fd), "r" (op), "i" (SYS_flock)  // Inputs
    : "rax", "rdi", "rsi"   // Clobber list
);
#elif defined(__i386__)  // x86 32-bit
    asm volatile(
    "movl %1, %%ebx\n"      // Move fd to ebx
    "movl %2, %%ecx\n"      // Move op to ecx
    "movl %3, %%eax\n"      // Move SYS_flock to eax
    "int $0x80\n"           // Make the syscall
    "movl %%eax, %0\n"      // Move the result from eax to retval
    : "=r" (retval)         // Output
    : "r" (fd), "r" (op), "i" (SYS_flock)  // Inputs
    : "eax", "ebx", "ecx"   // Clobber list
);
#elif defined(__powerpc__) || defined(__powerpc64__)
    asm volatile(
    "mr 3, %1\n"           // Move fd to r3 (1st argument)
    "mr 4, %2\n"           // Move op to r4 (2nd argument)
    "li 0, %3\n"           // Load SYS_flock (the system call number) into r0
    "sc\n"                 // Make the system call
    "mr %0, 3\n"           // Move the result from r3 to retval
    : "=r" (retval)        // Output
    : "r" (fd), "r" (op), "i" (SYS_flock)  // Inputs
    : "r0", "r3", "r4"     // Clobber list
);
#elif defined(__riscv)
    asm volatile(
    "mv a0, %1\n"          // Move fd to a0 (1st argument)
    "mv a1, %2\n"          // Move op to a1 (2nd argument)
    "li a7, %3\n"          // Load SYS_flock (the system call number) into a7
    "ecall\n"              // Make the system call
    "mv %0, a0\n"          // Move the result from a0 to retval
    : "=r" (retval)        // Output
    : "r" (fd), "r" (op), "i" (SYS_flock)  // Inputs
    : "a0", "a1", "a7"     // Clobber list
);
#elif defined(__loongarch64)
    asm volatile(
    "move $a0, %1\n"       // Move fd to $a0 (1st argument)
    "move $a1, %2\n"       // Move op to $a1 (2nd argument)
    "addi.d $a7, $zero, %3\n" // Load SYS_flock (the system call number) into $a7
    "syscall 0\n"          // Make the system call
    "move %0, $a0\n"       // Move the result from $a0 to retval
    : "=r" (retval)        // Output
    : "r" (fd), "r" (op), "i" (SYS_flock)  // Inputs
    : "a0", "a1", "a7"     // Clobber list
);
#else
#error "Unsupported architecture"
#endif
    return retval;
}


static int _libinject_dir_lock() {
    int dirfd;
    // If not initialized, check for existing mount before triggering NVRAM init
    if (!init) {
        PRINT_MSG("%s\n", "Triggering NVRAM initialization!");
        libinject_nvram_init();
    }

    dirfd = open(MOUNT_POINT, O_DIRECTORY | O_RDONLY);
    if(dirfd < 0) {
       PRINT_MSG("Couldn't open %s\n", MOUNT_POINT);
    }
    if(_libinject_flock_asm(dirfd,LOCK_EX) < 0) {
       PRINT_MSG("Couldn't lock %s\n", MOUNT_POINT);
    }
    return dirfd;
}

static void _libinject_dir_unlock(int dirfd) {
    if(_libinject_flock_asm(dirfd,LOCK_UN) < 0) {
       PRINT_MSG("Couldn't unlock %s\n", MOUNT_POINT);
    }
    close(dirfd);
    return;
}

int libinject_ret_1() {
    return E_SUCCESS; // 1
}

int libinject_ret_0() {
    return E_FAILURE; // 0
}

int libinject_ret_1_arg(char* a1) {
    return E_SUCCESS; //1
}

int libinject_ret_0_arg(char* a1) {
    return E_FAILURE; // 0
}


int libinject_nvram_init(void) {
    init = 1;
    return E_SUCCESS;
}

int libinject_nvram_reset(void) {
    PRINT_MSG("%s\n", "Reseting NVRAM...");

    if (libinject_nvram_clear() != E_SUCCESS) {
        PRINT_MSG("%s\n", "Unable to clear NVRAM!");
        return E_FAILURE;
    }
    return E_SUCCESS;
}

int libinject_nvram_clear(void) {
    char path[PATH_MAX] = MOUNT_POINT;
    struct dirent *entry;
    int ret = E_SUCCESS;
    DIR *dir;
    int dirfd;
    int rv;

    PRINT_MSG("%s\n", "Clearing NVRAM...");

    dirfd = _libinject_dir_lock();

    if (!(dir = opendir(MOUNT_POINT))) {
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to open directory %s!\n", MOUNT_POINT);
        return E_FAILURE;
    }

    while ((entry = readdir(dir))) {
        if (!strncmp(entry->d_name, ".", 1) || !strcmp(entry->d_name, "..")) {
            PRINT_MSG("Skipping %s\n", entry->d_name);
            continue;
        }

        strncpy(path + strlen(MOUNT_POINT), entry->d_name, ARRAY_SIZE(path) - ARRAY_SIZE(MOUNT_POINT) - 1);
        path[PATH_MAX - 1] = '\0';

        PRINT_MSG("%s\n", path);

        if (unlink(path) == -1 && errno != ENOENT) {
            PRINT_MSG("Unable to unlink %s!\n", path);
            ret = E_FAILURE;
        }
        // Clear is really a bunch of unsets
        rv = igloo_hypercall2(110, (unsigned long)path, strlen(path));
        while (rv == 1) {
            PAGE_IN(path);
            rv = igloo_hypercall2(110, (unsigned long)path, strlen(path));
        }
    }

    closedir(dir);
    _libinject_dir_unlock(dirfd);
    return ret;
}

int libinject_nvram_close(void) {
    PRINT_MSG("%s\n", "Closing NVRAM...");
    return E_SUCCESS;
}

int libinject_nvram_list_add(const char *key, const char *val) {
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    char *pos;

    PRINT_MSG("%s = %s + %s\n", val, temp, key);

    if (libinject_nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        free(temp);
        return libinject_nvram_set(key, val);
    }

    if (!key || !val) {
        free(temp);
        return E_FAILURE;
    }

    if (strlen(temp) + 1 + strlen(val) + 1 > BUFFER_SIZE) {
        free(temp);
        return E_FAILURE;
    }

    // This will overwrite the temp buffer, but it is OK
    if (libinject_nvram_list_exist(key, val, LIST_MAGIC) != NULL) {
        free(temp);
        return E_SUCCESS;
    }

    // Replace terminating NULL of list with LIST_SEP
    pos = temp + strlen(temp);
    if (pos != temp) {
        *pos++ = LIST_SEP[0];
    }

    if (strcpy(pos, val) != pos) {
        free(temp);
        return E_FAILURE;
    }

    int ret = libinject_nvram_set(key, temp);
    free(temp);
    return ret;
}

char *libinject_nvram_list_exist(const char *key, const char *val, int magic) {
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return (magic == LIST_MAGIC) ? NULL : (char *)E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    char *pos = NULL;

    if (libinject_nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        free(temp);
        return (magic == LIST_MAGIC) ? NULL : (char *)E_FAILURE;
    }

    PRINT_MSG("%s ?in %s (%s)\n", val, key, temp);

    if (!val) {
        free(temp);
        return (magic == LIST_MAGIC) ? NULL : (char *) E_FAILURE;
    }

    while ((pos = strtok(!pos ? temp : NULL, LIST_SEP))) {
        if (!strcmp(pos + 1, val)) {
            char *result = (magic == LIST_MAGIC) ? pos + 1 : (char *) E_SUCCESS;
            free(temp);
            return result;
        }
    }
    free(temp);
    return (magic == LIST_MAGIC) ? NULL : (char *) E_FAILURE;
}

int libinject_nvram_list_del(const char *key, const char *val) {
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    char *pos;

    if (libinject_nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        free(temp);
        return E_SUCCESS;
    }

    PRINT_MSG("%s = %s - %s\n", key, temp, val);

    if (!val) {
        free(temp);
        return E_FAILURE;
    }

    // This will overwrite the temp buffer, but it is OK.
    if ((pos = libinject_nvram_list_exist(key, val, LIST_MAGIC))) {
        while (*pos && *pos != LIST_SEP[0]) {
            *pos++ = LIST_SEP[0];
        }
    }

    int ret = libinject_nvram_set(key, temp);
    free(temp);
    return ret;
}

char *libinject_nvram_get(const char *key) {
// Some routers pass the key as the second argument, instead of the first.
// We attempt to fix this directly in assembly for MIPS if the key is NULL.
#if defined(mips)
    if (!key) {
        asm ("move %0, $a1" :"=r"(key));
    }
#endif
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return NULL;
    memset(temp, 0, BUFFER_SIZE);
    char *result = (libinject_nvram_get_buf(key, temp, BUFFER_SIZE) == E_SUCCESS) ? strndup(temp, BUFFER_SIZE) : NULL;
    free(temp);
    return result;
}

char *libinject_nvram_safe_get(const char *key) {
    char* ret = libinject_nvram_get(key);
    return ret ? ret : strdup("");
}

char *libinject_nvram_default_get(const char *key, const char *val) {
    char *ret = libinject_nvram_get(key);

    PRINT_MSG("%s = %s || %s\n", key, ret, val);

    if (ret) {
        return ret;
    }

    if (val && libinject_nvram_set(key, val)) {
        return libinject_nvram_get(key);
    }

    return NULL;
}

int libinject_nvram_get_buf(const char *key, char *buf, size_t sz) {
    memset(buf, 0, sz);
    char *path = malloc(PATH_MAX);
    if (!path) return E_FAILURE;
    strncpy(path, MOUNT_POINT, PATH_MAX - 1);
    path[PATH_MAX - 1] = '\0';
    FILE *f;
    int dirfd;
    int rv;

    if (!buf) {
        PRINT_MSG("NULL output buffer, key: %s!\n", key);
        return E_FAILURE;
    }

    if (!key) {
        PRINT_MSG("NULL input key, buffer: %s!\n", buf);
#ifdef FIRMAE_NVRAM
            return E_SUCCESS;
#else
            return E_FAILURE;
#endif
    }

    PRINT_MSG("%s\n", key);

    strncat(path, key, PATH_MAX - strlen(path) - 1);

    // Before taking the lock, check if the key exists, if not bail
    if (access(path, F_OK) != 0) {
        rv = igloo_hypercall2(107, (unsigned long)path, strlen(path));
#ifdef FIRMAE_NVRAM
        // Key doesn't exist, set default empty value
        buf[0] = '\0';
        return E_SUCCESS;
#else
        return E_FAILURE;
#endif
    }

    dirfd = _libinject_dir_lock();

    if ((f = fopen(path, "rb")) == NULL) {
        // We just checked without the lock, but it's empty after we took the lock
        // Someone must have just deleted it. Slow path but not wrong.
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to open key: %s! Set default value to \"\"\n", path);

        rv = igloo_hypercall2(107, (unsigned long)path, strlen(path));
        while (rv == 1) {
            PAGE_IN(path);
            rv = igloo_hypercall2(107, (unsigned long)path, strlen(path));
        }


#ifdef FIRMAE_NVRAM
            //If key value is not found, make the default value to ""
            //if (!strcmp(key, "noinitrc")) // Weird magic constant from FirmAE
            //    return E_FAILURE;
            buf[0] = '\0';
            return E_SUCCESS;
#else
            return E_FAILURE;
#endif

    }
    else
    {
        PRINT_MSG("\n\n[NVRAM] %d %s\n\n", (int)strlen(key), key);

        // success
        rv = igloo_hypercall2(108, (unsigned long)path, strlen(path));
        while (rv == 1) {
            PAGE_IN(path);
            rv = igloo_hypercall2(108, (unsigned long)path, strlen(path));
        }
    }

    buf[0] = '\0';
    char tmp[sz];
    while(fgets(tmp, sz, f)) {
        strncat (buf, tmp, sz);
    }

    fclose(f);
    _libinject_dir_unlock(dirfd);
    free(path);
    PRINT_MSG("= \"%s\"\n", buf);

    return E_SUCCESS;
}

int libinject_nvram_get_int(const char *key) {
    char *path = malloc(PATH_MAX);
    if (!path) return E_FAILURE;
    strncpy(path, MOUNT_POINT, PATH_MAX - 1);
    path[PATH_MAX - 1] = '\0';
    FILE *f;
    char buf[32]; // Buffer to store ASCII representation of the integer
    int ret = 0;
    int dirfd;

    if (!key) {
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }

    PRINT_MSG("%s\n", key);

    strncat(path, key, PATH_MAX - strlen(path) - 1);

    // Before taking the lock, check if the key exists, if not bail
    if (access(path, F_OK) != 0) {
        return E_FAILURE;
    }

    dirfd = _libinject_dir_lock();

    // Try to open the file
    if ((f = fopen(path, "rb")) == NULL) {
        free(path);
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to open key: %s!\n", path);
        return E_FAILURE;
    }

    // Attempt to read the ASCII representation of the integer
    if (fgets(buf, sizeof(buf), f) != NULL) {
        // Attempt to convert the read string to an integer
        char *endptr;
        long val = strtol(buf, &endptr, 10);

        // Check for conversion errors (no digits found or not all string parsed)
        if ((endptr != buf && *endptr == '\n') || *endptr == '\0') {
            ret = (int)val; // Successfully converted ASCII to integer
        } else {
            // Reset file pointer and try reading as binary integer
            fseek(f, 0, SEEK_SET);
            if (fread(&ret, sizeof(ret), 1, f) != 1) {
                PRINT_MSG("Unable to read key as binary int: %s!\n", path);
                fclose(f);
                _libinject_dir_unlock(dirfd);
                return E_FAILURE;
            }
        }
    } else {
        fclose(f);
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to read key: %s!\n", path);
        return E_FAILURE;
    }

    fclose(f);
    _libinject_dir_unlock(dirfd);
    free(path);
    PRINT_MSG("= %d\n", ret);
    return ret;
}

int libinject_nvram_getall(char *buf, size_t len) {
    char *path = malloc(PATH_MAX);
    if (!path) return E_FAILURE;
    strncpy(path, MOUNT_POINT, PATH_MAX - 1);
    path[PATH_MAX - 1] = '\0';
    struct dirent *entry;
    size_t pos = 0, ret;
    DIR *dir;
    FILE *f;
    int dirfd;

    if (!buf || !len) {
        PRINT_MSG("%s\n", "NULL buffer or zero length!");
        return E_FAILURE;
    }

    dirfd = _libinject_dir_lock();

    if (!(dir = opendir(MOUNT_POINT))) {
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to open directory %s!\n", MOUNT_POINT);
        return E_FAILURE;
    }

    while ((entry = readdir(dir))) {
        if (!strncmp(entry->d_name, ".", 1) || !strcmp(entry->d_name, "..")) {
            continue;
        }

        strncpy(path + strlen(MOUNT_POINT), entry->d_name, PATH_MAX - strlen(MOUNT_POINT) - 1);
        path[PATH_MAX - 1] = '\0';

        if ((ret = snprintf(buf + pos, len - pos, "%s=", entry->d_name)) != strlen(entry->d_name) + 1) {
            closedir(dir);
            _libinject_dir_unlock(dirfd);
            PRINT_MSG("Unable to append key %s!\n", buf + pos);
            return E_FAILURE;
        }

        pos += ret;

        if ((f = fopen(path, "rb")) == NULL) {
            closedir(dir);
            _libinject_dir_unlock(dirfd);
            free(path);
            PRINT_MSG("Unable to open key: %s!\n", path);
            return E_FAILURE;
        }
        // Determine file size
        fseek(f, 0, SEEK_END);
        long filesize = ftell(f);
        rewind(f);
        if (filesize < 0) filesize = 0;
        char *temp = malloc(filesize + 1);
        if (!temp) {
            fclose(f);
            closedir(dir);
            _libinject_dir_unlock(dirfd);
            free(path);
            PRINT_MSG("Unable to allocate buffer for key: %s!\n", path);
            return E_FAILURE;
        }
        ret = fread(temp, 1, filesize, f);
        if (ferror(f)) {
            free(temp);
            fclose(f);
            closedir(dir);
            _libinject_dir_unlock(dirfd);
            free(path);
            PRINT_MSG("Unable to read key: %s!\n", path);
            return E_FAILURE;
        }
        memcpy(buf + pos, temp, ret);
        buf[pos + ret] = '\0';
        pos += ret + 1;
        free(temp);
        fclose(f);
    }
    
    closedir(dir);
    _libinject_dir_unlock(dirfd);
    free(path);
    return E_SUCCESS;
}

int libinject_nvram_set(const char *key, const char *val) {
    char *path = malloc(PATH_MAX);
    if (!path) return E_FAILURE;
    strncpy(path, MOUNT_POINT, PATH_MAX - 1);
    path[PATH_MAX - 1] = '\0';
    FILE *f;
    int dirfd;
    int rv;

    if (!key || !val) {
        PRINT_MSG("%s\n", "NULL key or value!");
        return E_FAILURE;
    }

    PRINT_MSG("%s = \"%s\"\n", key, val);

    strncat(path, key, PATH_MAX - strlen(path) - 1);

    rv = igloo_hypercall2(109, (unsigned long)path, (unsigned long)val);
    while (rv == 1) {
        PAGE_IN(path);
        rv = igloo_hypercall2(109, (unsigned long)path, (unsigned long)val);
    }

    dirfd = _libinject_dir_lock();

    if ((f = fopen(path, "wb")) == NULL) {
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to open key: %s!\n", path);
        free(path);
        return E_FAILURE;
    }

    if (fwrite(val, sizeof(*val), strlen(val), f) != strlen(val)) {
        fclose(f);
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to write value: %s to key: %s!\n", val, path);
        return E_FAILURE;
    }
    PRINT_MSG("Wrote value: %s to key: %s!\n", val, path);

    fclose(f);
    _libinject_dir_unlock(dirfd);
    free(path);
    return E_SUCCESS;
}

int libinject_nvram_set_int(const char *key, const int val) {
    size_t path_len = strlen(MOUNT_POINT) + strlen(key) + 1;
    char *path = malloc(path_len);
    FILE *f;
    int dirfd;

    if (!key) {
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }
    // Truncate key if too long
    size_t max_key_len = PATH_MAX - strlen(MOUNT_POINT) - 1;
    char truncated_key[PATH_MAX];
    strncpy(truncated_key, key, max_key_len);
    truncated_key[max_key_len] = '\0';

    PRINT_MSG("%s = %d\n", truncated_key, val);

    snprintf(path, path_len, "%s%s", MOUNT_POINT, truncated_key);

    dirfd = _libinject_dir_lock();

    if ((f = fopen(path, "wb")) == NULL) {
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to open key: %s!\n", path);
        free(path);
        return E_FAILURE;
    }

    if (fwrite(&val, sizeof(val), 1, f) != 1) {
        fclose(f);
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to write value: %d to key: %s!\n", val, path);
        free(path);
        return E_FAILURE;
    }

    fclose(f);
    _libinject_dir_unlock(dirfd);
    free(path);
    return E_SUCCESS;
}

int libinject_nvram_unset(const char *key) {
    size_t path_len = strlen(MOUNT_POINT) + strlen(key) + 1;
    char *path = malloc(path_len);
    int dirfd;
    int rv;

    if (!key) {
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }
    // Truncate key if too long
    size_t max_key_len = PATH_MAX - strlen(MOUNT_POINT) - 1;
    char truncated_key[PATH_MAX];
    strncpy(truncated_key, key, max_key_len);
    truncated_key[max_key_len] = '\0';

    PRINT_MSG("%s\n", truncated_key);

    snprintf(path, path_len, "%s%s", MOUNT_POINT, truncated_key);

    rv = igloo_hypercall2(110, (unsigned long)path, strlen(path));
    while (rv == 1) {
        PAGE_IN(path);
        rv = igloo_hypercall2(110, (unsigned long)path, strlen(path));
    }

    dirfd = _libinject_dir_lock();
    if (unlink(path) == -1 && errno != ENOENT) {
        _libinject_dir_unlock(dirfd);
        PRINT_MSG("Unable to unlink %s!\n", path);
        free(path);
        return E_FAILURE;
    }
    _libinject_dir_unlock(dirfd);
    free(path);
    return E_SUCCESS;
}

int libinject_nvram_safe_unset(const char *key) {
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_SUCCESS;
    memset(temp, 0, BUFFER_SIZE);
    if (libinject_nvram_get_buf(key, temp, BUFFER_SIZE) == E_SUCCESS) {
      libinject_nvram_unset(key);
    }
    free(temp);
    return E_SUCCESS;
}

int libinject_nvram_match(const char *key, const char *val) {
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    if (!key) {
        free(temp);
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }

    if (libinject_nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        free(temp);
        return !val ? E_SUCCESS : E_FAILURE;
    }

    PRINT_MSG("%s (%s) ?= \"%s\"\n", key, temp, val);

    int cmp = strncmp(temp, val, BUFFER_SIZE);
    free(temp);
    if (cmp) {
        PRINT_MSG("%s\n", "false");
        return E_FAILURE;
    }

    PRINT_MSG("%s\n", "true");
    return E_SUCCESS;
}

int libinject_nvram_invmatch(const char *key, const char *val) {
    if (!key) {
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }

    PRINT_MSG("%s ~?= \"%s\"\n", key, val);
    return !libinject_nvram_match(key, val);
}

int libinject_nvram_commit(void) {
    int dirfd;
    dirfd = _libinject_dir_lock();
    sync();
    _libinject_dir_unlock(dirfd);

    return E_SUCCESS;
}

int libinject_parse_nvram_from_file(const char *file)
{
    FILE *f;
    char *buffer;
    int fileLen=0;

    if((f = fopen(file, "rb")) == NULL){
        PRINT_MSG("Unable to open file: %s!\n", file);
        return E_FAILURE;
    }

    /* Get file length */
    fseek(f, 0, SEEK_END);
    fileLen = ftell(f);
    rewind(f);

    /* Allocate memory */
    buffer = (char*)malloc(sizeof(char) *fileLen);
    int rv = fread(buffer, 1, fileLen, f);
    if (rv != fileLen) {
        PRINT_MSG("Unable to read file: %s: %d!\n", file, rv);
        free(buffer);
        fclose(f);
        return E_FAILURE;
    }
    fclose(f);

    /* split the buffer including null byte */
    #define LEN 1024
    int i=0,j=0,k=0; int left = 1;
    char *key="", *val="";
    char larr[LEN]="", rarr[LEN]="";

    for(i=0; i < fileLen; i++)
    {
        char tmp[4];
        sprintf(tmp, "%c", *(buffer+i));

        if (left==1 && j<LEN)
            larr[j++] = tmp[0];
        else if(left==0 && k<LEN)
            rarr[k++] = tmp[0];

        if(!memcmp(tmp,"=",1)){
            left=0;
            larr[j-1]='\0';
        }
        if (!memcmp(tmp,"\x00",1)){
            key = larr; val = rarr;
            libinject_nvram_set(key, val);
            j=0; k=0; left=1;
            memset(larr, 0, LEN); memset(rarr, 0, LEN);
        }
    }
    return E_SUCCESS;
}

/* Atheros/Broadcom NVRAM */

int libinject_nvram_get_nvramspace(void) {
    return NVRAM_SIZE;
}


char *libinject_nvram_nget(const char *fmt, ...) {
    va_list va;
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return NULL;
    va_start(va, fmt);
    vsnprintf(temp, BUFFER_SIZE, fmt, va);
    va_end(va);
    char *result = libinject_nvram_get(temp);
    free(temp);
    return result;
}

int libinject_nvram_nset(const char *val, const char *fmt, ...) {
    va_list va;
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    va_start(va, fmt);
    vsnprintf(temp, BUFFER_SIZE, fmt, va);
    va_end(va);

    int ret = libinject_nvram_set(temp, val);
    free(temp);
    return ret;
}

int libinject_nvram_nset_int(const int val, const char *fmt, ...) {
    va_list va;
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    va_start(va, fmt);
    vsnprintf(temp, BUFFER_SIZE, fmt, va);
    va_end(va);

    int ret = libinject_nvram_set_int(temp, val);
    free(temp);
    return ret;
}

int libinject_nvram_nmatch(const char *val, const char *fmt, ...) {
    va_list va;
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    va_start(va, fmt);
    vsnprintf(temp, BUFFER_SIZE, fmt, va);
    va_end(va);

    int ret = libinject_nvram_match(temp, val);
    free(temp);
    return ret;
}

/* Realtek */
int libinject_apmib_get(const int key, void *buf) {
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return 0;
    memset(temp, 0, BUFFER_SIZE);
    int res;
    snprintf(temp, BUFFER_SIZE, "%d", key);
    if ((res = libinject_nvram_get_int(temp))) {
        (*(int32_t *) buf) = res;
    }
    free(temp);
    return res;
}

int libinject_apmib_set(const int key, void *buf) {
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    snprintf(temp, BUFFER_SIZE, "%d", key);
    int ret = libinject_nvram_set_int(temp, ((int32_t *) buf)[0]);
    free(temp);
    return ret;
}

/* Netgear ACOS */

int libinject_WAN_ith_CONFIG_GET(char *buf, const char *fmt, ...) {
    va_list va;
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    va_start(va, fmt);
    vsnprintf(temp, BUFFER_SIZE, fmt, va);
    va_end(va);

    int ret = libinject_nvram_get_buf(temp, buf, USER_BUFFER_SIZE);
    free(temp);
    return ret;
}

/* ZyXel / Edimax */
// many functions expect the opposite return values: (0) success, failure (1/-1)

int libinject_nvram_getall_adv(int unk, char *buf, size_t len) {
    return libinject_nvram_getall(buf, len) == E_SUCCESS ? E_FAILURE : E_SUCCESS;
}

char *libinject_nvram_get_adv(int unk, const char *key) {
    return libinject_nvram_get(key);
}

int libinject_nvram_set_adv(int unk, const char *key, const char *val) {
    return libinject_nvram_set(key, val);
}

int libinject_nvram_state(int unk1, void *unk2, void *unk3) {
    return E_FAILURE;
}

int libinject_envram_commit(void) {
    return !libinject_nvram_commit();
}

int libinject_envram_default(void) {
    return E_FAILURE;
}

int libinject_envram_load(void)  {
    return !libinject_nvram_init();
}

int libinject_envram_safe_load(void)  {
    return !libinject_nvram_init();
}

int libinject_envram_match(const char *key, const char *val)  {
    return !libinject_nvram_match(key, val);
}

int libinject_envram_get(const char* key, char *buf) {
    // may be incorrect
    return !libinject_nvram_get_buf(key, buf, USER_BUFFER_SIZE);
}

int libinject_envram_getf(const char* key, const char *fmt, ...) {
    va_list va;
    char *val = libinject_nvram_get(key);

    if (!val) {
        return !E_SUCCESS;
    }

    va_start(va, fmt);
    vsscanf(val, fmt, va);
    va_end(va);

    free(val);
    return !E_FAILURE;
}

int libinject_envram_set(const char *key, const char *val) {
    return !libinject_nvram_set(key, val);
}

int libinject_envram_setf(const char* key, const char* fmt, ...) {
    va_list va;
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    va_start(va, fmt);
    vsnprintf(temp, BUFFER_SIZE, fmt, va);
    va_end(va);

    int ret = !libinject_nvram_set(key, temp);
    free(temp);
    return ret;
}

int libinject_envram_unset(const char *key) {
    return !libinject_nvram_unset(key);
}

/* Ralink */

char *libinject_nvram_bufget(int idx, const char *key) {
    return libinject_nvram_safe_get(key);
}

int libinject_nvram_bufset(int idx, const char *key, const char *val) {
    return libinject_nvram_set(key, val);
}

// Hack to use static variables in shared library
#include "strings.c"
