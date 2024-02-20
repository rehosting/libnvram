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

#include "alias.h"
#include "nvram.h"
#include "config.h"
#include "strings.h"

// Two left-over weak symbols from the original code
extern const char *router_defaults[] __attribute__((weak));
extern const char *Nvrams[] __attribute__((weak));

// https://lkml.org/lkml/2007/3/9/10
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + sizeof(typeof(int[1 - 2 * !!__builtin_types_compatible_p(typeof(arr), typeof(&arr[0]))])) * 0)

#define PRINT_MSG(fmt, ...) do { if (DEBUG) { fprintf(stderr, "%s: "fmt, __FUNCTION__, __VA_ARGS__); } } while (0)

/* Weak symbol definitions for library functions that may not be present */
__typeof__(setmntent) __attribute__((weak)) setmntent;

/* Global variables */
static int init = 0;
static char temp[BUFFER_SIZE];
static int is_load_env = 0;
static int firmae_nvram = 0;

static void firmae_load_env()
{
    char* env = getenv("FIRMAE_NVRAM");
    if (env && env[0] == 't')
        firmae_nvram = 1;
    is_load_env = 1;
}

static int dir_lock() {
    int dirfd;
    // If not initialized, check for existing mount before triggering NVRAM init
    if (!init) {
        //when we initialize we touch /mounted in the directory as a flag
        FILE *f;
        if ((f = fopen(MOUNT_POINT "/mounted", "rb")) != NULL) {
            fclose(f);
            // We were able to open MOUNT_POINT/mounted  - we probably mounted this previously, bail
            goto cont;
        }

        PRINT_MSG("%s\n", "Triggering NVRAM initialization!");
        nvram_init();
    }

cont:
    dirfd = open(MOUNT_POINT, O_DIRECTORY | O_RDONLY);
    if(dirfd < 0) {
       PRINT_MSG("Couldn't open %s\n", MOUNT_POINT);
    }
    //if(_syscall2(int,sysflock,int,dirfd,int,LOCK_EX) < 0) {
    if(flock_asm(dirfd,LOCK_EX) < 0) {
       PRINT_MSG("Couldn't lock %s\n", MOUNT_POINT);
    }
    return dirfd;
}

static void dir_unlock(int dirfd) {
    //if(_syscall2(int,sysflock,int,dirfd,int,LOCK_UN) < 0) {
    if(flock_asm(dirfd,LOCK_UN) < 0) {
       PRINT_MSG("Couldn't unlock %s\n", MOUNT_POINT);
    }
    close(dirfd);
    return;
}

int nvram_init(void) {
    FILE *f;
    int dirfd;

    PRINT_MSG("%s\n", "Initializing NVRAM...");

    if (init) {
        PRINT_MSG("%s\n", "Early termination!");
        return E_SUCCESS;
    }
    init = 1;

    dirfd = dir_lock();

    if (mount("tmpfs", MOUNT_POINT, "tmpfs", MS_NOEXEC | MS_NOSUID | MS_SYNCHRONOUS, "") == -1) {
        dir_unlock(dirfd);
        PRINT_MSG("Unable to mount tmpfs on mount point %s!\n", MOUNT_POINT);
        return E_FAILURE;
    }

    // Touch /mounted so we know it exists if we don't have semset
    if ((f = fopen(MOUNT_POINT "/mounted", "w+")) == NULL) {
        PRINT_MSG("%s\n", "Unable open mount_point/mounted");
    }

    // Checked by certain Ralink routers
    if ((f = fopen("/var/run/nvramd.pid", "w+")) == NULL) {
        PRINT_MSG("Unable to touch Ralink PID file: %s!\n", "/var/run/nvramd.pid");
    }
    else {
        fclose(f);
    }

    dir_unlock(dirfd);

    return nvram_set_default();
}

int nvram_reset(void) {
    PRINT_MSG("%s\n", "Reseting NVRAM...");

    if (nvram_clear() != E_SUCCESS) {
        PRINT_MSG("%s\n", "Unable to clear NVRAM!");
        return E_FAILURE;
    }

    return nvram_set_default();
}

int nvram_clear(void) {
    char path[PATH_MAX] = MOUNT_POINT;
    struct dirent *entry;
    int ret = E_SUCCESS;
    DIR *dir;
    int dirfd;

    PRINT_MSG("%s\n", "Clearing NVRAM...");

    dirfd = dir_lock();

    if (!(dir = opendir(MOUNT_POINT))) {
        dir_unlock(dirfd);
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
    }

    closedir(dir);
    dir_unlock(dirfd);
    return ret;
}

int nvram_close(void) {
    PRINT_MSG("%s\n", "Closing NVRAM...");
    return E_SUCCESS;
}

int nvram_list_add(const char *key, const char *val) {
    char *pos;

    PRINT_MSG("%s = %s + %s\n", val, temp, key);

    if (nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        return nvram_set(key, val);
    }

    if (!key || !val) {
        return E_FAILURE;
    }

    if (strlen(temp) + 1 + strlen(val) + 1 > BUFFER_SIZE) {
        return E_FAILURE;
    }

    // This will overwrite the temp buffer, but it is OK
    if (nvram_list_exist(key, val, LIST_MAGIC) != NULL) {
        return E_SUCCESS;
    }

    // Replace terminating NULL of list with LIST_SEP
    pos = temp + strlen(temp);
    if (pos != temp) {
        *pos++ = LIST_SEP[0];
    }

    if (strcpy(pos, val) != pos) {
        return E_FAILURE;
    }

    return nvram_set(key, temp);
}

char *nvram_list_exist(const char *key, const char *val, int magic) {
    char *pos = NULL;

    if (nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        return E_FAILURE;
    }

    PRINT_MSG("%s ?in %s (%s)\n", val, key, temp);

    if (!val) {
        return (magic == LIST_MAGIC) ? NULL : (char *) E_FAILURE;
    }

    while ((pos = strtok(!pos ? temp : NULL, LIST_SEP))) {
        if (!strcmp(pos + 1, val)) {
            return (magic == LIST_MAGIC) ? pos + 1 : (char *) E_SUCCESS;
        }
    }

    return (magic == LIST_MAGIC) ? NULL : (char *) E_FAILURE;
}

int nvram_list_del(const char *key, const char *val) {
    char *pos;

    if (nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        return E_SUCCESS;
    }

    PRINT_MSG("%s = %s - %s\n", key, temp, val);

    if (!val) {
        return E_FAILURE;
    }

    // This will overwrite the temp buffer, but it is OK.
    if ((pos = nvram_list_exist(key, val, LIST_MAGIC))) {
        while (*pos && *pos != LIST_SEP[0]) {
            *pos++ = LIST_SEP[0];
        }
    }

    return nvram_set(key, temp);
}

char *nvram_get(const char *key) {
// Some routers pass the key as the second argument, instead of the first.
// We attempt to fix this directly in assembly for MIPS if the key is NULL.
#if defined(mips)
    if (!key) {
        asm ("move %0, $a1" :"=r"(key));
    }
#endif

    return (nvram_get_buf(key, temp, BUFFER_SIZE) == E_SUCCESS) ? strndup(temp, BUFFER_SIZE) : NULL;
}

char *nvram_safe_get(const char *key) {
    char* ret = nvram_get(key);
    return ret ? ret : strdup("");
}

char *nvram_default_get(const char *key, const char *val) {
    char *ret = nvram_get(key);

    PRINT_MSG("%s = %s || %s\n", key, ret, val);

    if (ret) {
        return ret;
    }

    if (val && nvram_set(key, val)) {
        return nvram_get(key);
    }

    return NULL;
}

int nvram_get_buf(const char *key, char *buf, size_t sz) {
    char path[PATH_MAX] = MOUNT_POINT;
    FILE *f;
    int dirfd;
    if (!is_load_env) firmae_load_env();

    if (!buf) {
        PRINT_MSG("NULL output buffer, key: %s!\n", key);
        return E_FAILURE;
    }

    if (!key) {
        PRINT_MSG("NULL input key, buffer: %s!\n", buf);
        if (firmae_nvram)
            return E_SUCCESS;
        else
            return E_FAILURE;
    }

    PRINT_MSG("%s\n", key);

    strncat(path, key, ARRAY_SIZE(path) - ARRAY_SIZE(MOUNT_POINT) - 1);

    dirfd = dir_lock();

    if ((f = fopen(path, "rb")) == NULL) {
        dir_unlock(dirfd);
        PRINT_MSG("Unable to open key: %s! Set default value to \"\"\n", path);
        if (firmae_nvram)
        {
            //If key value is not found, make the default value to ""
            if (!strcmp(key, "noinitrc"))
                return E_FAILURE;
            strcpy(buf,"");
            return E_SUCCESS;
        }
        else
            return E_FAILURE;
    }
    else
    {
        PRINT_MSG("\n\n[NVRAM] %d %s\n\n", strlen(key), key);
    }

    buf[0] = '\0';
    char tmp[sz];
    while(fgets(tmp, sz, f)) {
        strncat (buf, tmp, sz);
    }

    fclose(f);
    dir_unlock(dirfd);

    PRINT_MSG("= \"%s\"\n", buf);

    return E_SUCCESS;
}

int nvram_get_int(const char *key) {
    char path[PATH_MAX] = MOUNT_POINT;
    FILE *f;
    char buf[32]; // Buffer to store ASCII representation of the integer
    int ret = 0;
    int dirfd;

    if (!key) {
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }

    PRINT_MSG("%s\n", key);

    strncat(path, key, ARRAY_SIZE(path) - ARRAY_SIZE(MOUNT_POINT) - 1);

    dirfd = dir_lock();

    // Try to open the file
    if ((f = fopen(path, "rb")) == NULL) {
        dir_unlock(dirfd);
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
                dir_unlock(dirfd);
                return E_FAILURE;
            }
        }
    } else {
        fclose(f);
        dir_unlock(dirfd);
        PRINT_MSG("Unable to read key: %s!\n", path);
        return E_FAILURE;
    }

    fclose(f);
    dir_unlock(dirfd);

    PRINT_MSG("= %d\n", ret);

    return ret;
}

int nvram_getall(char *buf, size_t len) {
    char path[PATH_MAX] = MOUNT_POINT;
    struct dirent *entry;
    size_t pos = 0, ret;
    DIR *dir;
    FILE *f;
    int dirfd;

    if (!buf || !len) {
        PRINT_MSG("%s\n", "NULL buffer or zero length!");
        return E_FAILURE;
    }

    dirfd = dir_lock();

    if (!(dir = opendir(MOUNT_POINT))) {
        dir_unlock(dirfd);
        PRINT_MSG("Unable to open directory %s!\n", MOUNT_POINT);
        return E_FAILURE;
    }

    while ((entry = readdir(dir))) {
        if (!strncmp(entry->d_name, ".", 1) || !strcmp(entry->d_name, "..")) {
            continue;
        }

        strncpy(path + strlen(MOUNT_POINT), entry->d_name, ARRAY_SIZE(path) - ARRAY_SIZE(MOUNT_POINT) - 1);
        path[PATH_MAX - 1] = '\0';

        if ((ret = snprintf(buf + pos, len - pos, "%s=", entry->d_name)) != strlen(entry->d_name) + 1) {
            closedir(dir);
            dir_unlock(dirfd);
            PRINT_MSG("Unable to append key %s!\n", buf + pos);
            return E_FAILURE;
        }

        pos += ret;

        if ((f = fopen(path, "rb")) == NULL) {
            closedir(dir);
            dir_unlock(dirfd);
            PRINT_MSG("Unable to open key: %s!\n", path);
            return E_FAILURE;
        }

        ret = fread(temp, sizeof(*temp), BUFFER_SIZE, f);
        if (ferror(f)) {
            fclose(f);
            closedir(dir);
            dir_unlock(dirfd);
            PRINT_MSG("Unable to read key: %s!\n", path);
            return E_FAILURE;
        }

        memcpy(buf + pos, temp, ret);
        buf[pos + ret] = '\0';
        pos += ret + 1;

        fclose(f);
    }

    closedir(dir);
    dir_unlock(dirfd);
    return E_SUCCESS;
}

int nvram_set(const char *key, const char *val) {
    char path[PATH_MAX] = MOUNT_POINT;
    FILE *f;
    int dirfd;

    if (!key || !val) {
        PRINT_MSG("%s\n", "NULL key or value!");
        return E_FAILURE;
    }

    PRINT_MSG("%s = \"%s\"\n", key, val);

    strncat(path, key, ARRAY_SIZE(path) - ARRAY_SIZE(MOUNT_POINT) - 1);

    dirfd = dir_lock();

    if ((f = fopen(path, "wb")) == NULL) {
        dir_unlock(dirfd);
        PRINT_MSG("Unable to open key: %s!\n", path);
        return E_FAILURE;
    }

    if (fwrite(val, sizeof(*val), strlen(val), f) != strlen(val)) {
        fclose(f);
        dir_unlock(dirfd);
        PRINT_MSG("Unable to write value: %s to key: %s!\n", val, path);
        return E_FAILURE;
    }

    fclose(f);
    dir_unlock(dirfd);
    return E_SUCCESS;
}

int nvram_set_int(const char *key, const int val) {
    char path[PATH_MAX] = MOUNT_POINT;
    FILE *f;
    int dirfd;

    if (!key) {
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }

    PRINT_MSG("%s = %d\n", key, val);

    strncat(path, key, ARRAY_SIZE(path) - ARRAY_SIZE(MOUNT_POINT) - 1);

    dirfd = dir_lock();

    if ((f = fopen(path, "wb")) == NULL) {
        dir_unlock(dirfd);
        PRINT_MSG("Unable to open key: %s!\n", path);
        return E_FAILURE;
    }

    if (fwrite(&val, sizeof(val), 1, f) != 1) {
        fclose(f);
        dir_unlock(dirfd);
        PRINT_MSG("Unable to write value: %d to key: %s!\n", val, path);
        return E_FAILURE;
    }

    fclose(f);
    dir_unlock(dirfd);
    return E_SUCCESS;
}

int nvram_set_default(void) {
    if (!is_load_env) firmae_load_env();

    if (router_defaults) {
        PRINT_MSG("Loading from native built-in table: %s (%p) = %d!\n", "router_defaults", router_defaults, nvram_set_default_table(router_defaults));
    }
    if (Nvrams) {
        PRINT_MSG("Loading from native built-in table: %s (%p) = %d!\n", "Nvrams", Nvrams, nvram_set_default_table(Nvrams));
    }
    return nvram_set_default_image();
}

static int nvram_set_default_image(void) {
    int dirfd;
    PRINT_MSG("%s\n", "Copying overrides from defaults folder!");
    dirfd = dir_lock();
    system("/bin/cp "OVERRIDE_POINT"* "MOUNT_POINT);
    dir_unlock(dirfd);
    return E_SUCCESS;
}

static int nvram_set_default_table(const char *tbl[]) {
    size_t i = 0;

    while (tbl[i]) {
        nvram_set(tbl[i], tbl[i + 1]);
        i += (tbl[i + 2] != 0 && tbl[i + 2] != (char *) 1) ? 2 : 3;
    }

    return E_SUCCESS;
}

int nvram_unset(const char *key) {
    char path[PATH_MAX] = MOUNT_POINT;
    int dirfd;

    if (!key) {
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }

    PRINT_MSG("%s\n", key);

    strncat(path, key, ARRAY_SIZE(path) - ARRAY_SIZE(MOUNT_POINT) - 1);

    dirfd = dir_lock();
    if (unlink(path) == -1 && errno != ENOENT) {
        dir_unlock(dirfd);
        PRINT_MSG("Unable to unlink %s!\n", path);
        return E_FAILURE;
    }
    dir_unlock(dirfd);
    return E_SUCCESS;
}

int nvram_safe_unset(const char *key) {
    // If we have a value for this key, unset it. Otherwise no-op
    // Always return E_SUCCESS(?)
    if (nvram_get_buf(key, temp, BUFFER_SIZE) == E_SUCCESS) {
      nvram_unset(key);
    }
    return E_SUCCESS;
}

int nvram_match(const char *key, const char *val) {
    if (!key) {
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }

    if (nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        return !val ? E_SUCCESS : E_FAILURE;
    }

    PRINT_MSG("%s (%s) ?= \"%s\"\n", key, temp, val);

    if (strncmp(temp, val, BUFFER_SIZE)) {
        PRINT_MSG("%s\n", "false");
        return E_FAILURE;
    }

    PRINT_MSG("%s\n", "true");
    return E_SUCCESS;
}

int nvram_invmatch(const char *key, const char *val) {
    if (!key) {
        PRINT_MSG("%s\n", "NULL key!");
        return E_FAILURE;
    }

    PRINT_MSG("%s ~?= \"%s\"\n", key, val);
    return !nvram_match(key, val);
}

int nvram_commit(void) {
    int dirfd;
    dirfd = dir_lock();
    sync();
    dir_unlock(dirfd);

    return E_SUCCESS;
}

int parse_nvram_from_file(const char *file)
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
    fread(buffer, 1, fileLen, f);
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
            nvram_set(key, val);
            j=0; k=0; left=1;
            memset(larr, 0, LEN); memset(rarr, 0, LEN);
        }
    }
    return E_SUCCESS;
}

#ifdef FIRMAE_KERNEL
//DIR-615I2, DIR-615I3, DIR-825C1 patch
int VCTGetPortAutoNegSetting(char *a1, int a2){
    PRINT_MSG("%s\n", "Dealing wth ioctl ...");
    return 0;
}

// netgear 'Rxxxx' series patch to prevent infinite loop in httpd
int agApi_fwGetFirstTriggerConf(char *a1)
{
    PRINT_MSG("%s\n", "agApi_fwGetFirstTriggerConf called!");
    return 1;
}

// netgear 'Rxxxx' series patch to prevent infinite loop in httpd
int agApi_fwGetNextTriggerConf(char *a1)
{
    PRINT_MSG("%s\n", "agApi_fwGetNextTriggerConf called!");
    return 1;
}
#endif

// Hack to use static variables in shared library
#include "alias.c"
#include "strings.c"
