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
#include <sys/mman.h>
#include <unistd.h>
#include <mntent.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <ctype.h>

#include "nvram.h"
#include "config.h"
#include "strings.h"

// https://lkml.org/lkml/2007/3/9/10
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + sizeof(typeof(int[1 - 2 * !!__builtin_types_compatible_p(typeof(arr), typeof(&arr[0]))])) * 0)

#define PRINT_MSG(fmt, ...) do { if (DEBUG) { fprintf(stderr, "%s: "fmt, __FUNCTION__, __VA_ARGS__); } } while (0)

/* --- NEW: Debug Logging Infrastructure --- */
#define NVRAM_LOG_FILE "/igloo/nvramlog.txt"

#ifdef DEBUG_LOGGING

static inline void nvram_log(const char *action, const char *key, const char *val) {
    int fd = open(NVRAM_LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) return;

    char msg[1024];
    int len;
    if (val) {
        len = snprintf(msg, sizeof(msg), "[PID:%d] %-8s | KEY: %-32s | VAL: %s\n", 
                       getpid(), action, key ? key : "NULL", val);
    } else {
        len = snprintf(msg, sizeof(msg), "[PID:%d] %-8s | KEY: %-32s\n", 
                       getpid(), action, key ? key : "NULL");
    }

    if (len > 0 && len < sizeof(msg)) {
        write(fd, msg, len);
    }
    close(fd);
}
#else
static inline void nvram_log(const char *action, const char *key, const char *val) {}

#endif
/* --------------------------------------- */
/* Universal 64-bit kernel dirent struct */
struct kernel_dirent64 {
    unsigned long long d_ino;
    long long          d_off;
    unsigned short     d_reclen;
    unsigned char      d_type;
    char               d_name[256];
};

struct kernel_dirent {
    unsigned long      d_ino;
    unsigned long      d_off;
    unsigned short     d_reclen;
    char               d_name[256];
};

/* Hybrid Shared Memory Configuration */
#define SHM_FILE "/igloo/nvram.shm"
#define SHM_MAGIC 0x4E56524D

#define HASH_SIZE 8192 // 2x the max entries for a low load factor

#define MAX_KEY_LEN 256
#define INLINE_VAL_LEN 128
#define MAX_NVRAM_ENTRIES 4096

typedef struct {
    unsigned char in_use;
    unsigned char is_file_backed;
    unsigned char is_binary;     // Flag for set_int variables
    unsigned char padding;       // Explicit 4-byte alignment
    char key[MAX_KEY_LEN];       // 256 bytes (ends at offset 260)
    uint32_t val_len;            // 4 bytes (aligned)
    int32_t next_idx;            // 4 bytes (aligned)
    char inline_val[INLINE_VAL_LEN]; // 128 bytes (aligned)
} nvram_entry_t;

typedef struct {
    uint32_t magic;
    volatile int lock_pid;
    uint32_t used_entries;
    int32_t free_head;           // Head of the O(1) allocation stack
    int32_t hash_table[HASH_SIZE];
    nvram_entry_t entries[MAX_NVRAM_ENTRIES];
} nvram_shm_t;

/* Global variables */
static int init = 0;
static int init_failed = 0;
static volatile int logging_enabled = 0;
static int shm_fd = -1;
static nvram_shm_t *shm_base = MAP_FAILED;
/* * Rotating buffer pool for list_exist returns.
 * Provides thread-safety without requiring TLS support in the loader.
 */
#define NUM_RET_BUFS 16
static char list_ret_pool[NUM_RET_BUFS][BUFFER_SIZE];
static volatile unsigned int pool_idx = 0;

#define FIRMAE_NVRAM 1

/* Futex-based lock (syscall-only, no libpthread dependency) */
#if !defined(SYS_futex)
#  if defined(__NR_futex)
#    define SYS_futex __NR_futex
#  endif
#endif

/* Some toolchains may not ship <linux/futex.h>. Define minimal ops. */
#ifndef FUTEX_WAIT
#  define FUTEX_WAIT 0
#endif
#ifndef FUTEX_WAKE
#  define FUTEX_WAKE 1
#endif

/* Compatibility for architectures that lack legacy syscalls (AArch64, RISC-V) */
#ifndef SYS_mkdir
#  if defined(SYS_mkdirat)
#    define SYS_mkdir SYS_mkdirat
#  endif
#endif

#ifndef AT_FDCWD
#  define AT_FDCWD -100
#endif

static int _futex_wait(volatile int *uaddr, int val) {
#if defined(SYS_futex)
    struct timespec ts = {2, 0}; // 2-second timeout dead-man's switch
    return (int)syscall(SYS_futex, uaddr, FUTEX_WAIT, val, &ts, NULL, 0);
#else
    (void)uaddr; (void)val;
    usleep(1000); // 1ms sleep to prevent burning CPU
    return -1;
#endif
}

static int _futex_wake(volatile int *uaddr) {
#if defined(SYS_futex)
    return (int)syscall(SYS_futex, uaddr, FUTEX_WAKE, 1, NULL, NULL, 0);
#else
    (void)uaddr;
    return -1;
#endif
}

/* Manual Atomic Helpers to bypass libgcc dependency (__sync_val_compare_and_swap_4, etc) */
/* * Universal Atomic Helpers 
 * Supports: x86, x64, ARM (v4-v7 via Kernel Helpers), AArch64, MIPS, PPC, RISC-V, LoongArch
 */

/* * Universal Atomic Helpers 
 * Supports: x86, x64, ARM (Universal), AArch64, MIPS, PPC, RISC-V, LoongArch
 */

static inline int _libinject_cas_asm(volatile int *ptr, int oldval, int newval) {
    int prev;
#if defined(__arm__)
    typedef int (*kernel_cas_t)(int oldval, int newval, volatile int *ptr);
    kernel_cas_t cas_func = (kernel_cas_t)0xffff0fc0;
    do {
        prev = *ptr;
        if (prev != oldval) return prev;
    } while (cas_func(oldval, newval, ptr) != 0);
    return oldval;

#elif defined(__mips__) || defined(__mips64__)
    asm volatile(
        "1: ll      %0, %1\n"
        "   bne     %0, %2, 2f\n"
        "   move    $t0, %3\n"
        "   sc      $t0, %1\n"
        "   beqz    $t0, 1b\n"
        "2: "
        : "=&r" (prev), "+m" (*ptr)
        : "r" (oldval), "r" (newval)
        : "t0", "memory"
    );
#elif defined(__aarch64__)
    int status;
    asm volatile(
        "1: ldxr    %w0, [%3]\n"
        "   cmp     %w0, %w1\n"
        "   bne     2f\n"
        "   stxr    %w2, %w4, [%3]\n"
        "   cbnz    %w2, 1b\n"
        "2: "
        : "=&r" (prev), "+r" (oldval), "=&r" (status)
        : "r" (ptr), "r" (newval)
        : "cc", "memory"
    );
#elif defined(__x86_64__) || defined(__i386__)
    asm volatile(
        "lock; cmpxchgl %2, %1"
        : "=a" (prev), "+m" (*ptr)
        : "r" (newval), "0" (oldval)
        : "memory"
    );
#elif defined(__loongarch64)
    int tmp;
    asm volatile(
        "1: ll.w %0, %2, 0\n"
        "   bne  %0, %3, 2f\n"
        "   move %1, %4\n"
        "   sc.w %1, %2, 0\n"
        "   beqz %1, 1b\n"
        "2: "
        : "=&r" (prev), "=&r" (tmp)
        : "r" (ptr), "r" (oldval), "r" (newval)
        : "memory");
#elif defined(__powerpc__) || defined(__powerpc64__)
    asm volatile(
        "1: lwarx   %0, 0, %2\n"
        "   cmpw    %0, %3\n"
        "   bne     2f\n"
        "   stwcx.  %4, 0, %2\n"
        "   bne-    1b\n"
        "2: "
        : "=&r" (prev), "+m" (*ptr)
        : "r" (ptr), "r" (oldval), "r" (newval)
        : "cc", "memory"
    );
#elif defined(__riscv)
    int tmp;
    asm volatile(
        "1: lr.w    %0, (%2)\n"
        "   bne     %0, %3, 2f\n"
        "   sc.w    %1, %4, (%2)\n"
        "   bnez    %1, 1b\n"
        "2: "
        : "=&r" (prev), "=&r" (tmp)
        : "r" (ptr), "r" (oldval), "r" (newval)
        : "memory"
    );
#endif
    return prev;
}

/* * Final Universal flock assembly 
 * Uses register constraints ("r") for syscall numbers to prevent immediate overflows
 */
static int _libinject_flock_asm(int fd, int op) {
    int retval;
#if defined(__mips64__) || defined(__mips__)
    asm volatile(
        "move $a0, %1\n"
        "move $a1, %2\n"
        "move $v0, %3\n"
        "syscall\n"
        "move %0, $v0\n"
        : "=r" (retval)
        : "r" (fd), "r" (op), "r" (SYS_flock)
        : "$v0", "$a0", "$a1", "memory"
    );
#elif defined(__arm__)
    asm volatile(
        "mov r0, %1\n"
        "mov r1, %2\n"
        "mov r7, %3\n"
        "svc 0\n"
        "mov %0, r0\n"
        : "=r" (retval)
        : "r" (fd), "r" (op), "r" (SYS_flock)
        : "r0", "r1", "r7", "memory"
    );
#elif defined(__aarch64__)
    asm volatile(
        "mov x0, %1\n"
        "mov x1, %2\n"
        "mov x8, %3\n"
        "svc 0\n"
        "mov %0, x0\n"
        : "=r" (retval)
        : "r" ((long)fd), "r" ((long)op), "r" ((long)SYS_flock)
        : "x0", "x1", "x8", "memory"
    );
#elif defined(__x86_64__)
    asm volatile(
        "mov %1, %%edi\n"
        "mov %2, %%esi\n"
        "mov %3, %%eax\n"
        "syscall\n"
        "mov %%eax, %0\n"
        : "=r" (retval)
        : "r" (fd), "r" (op), "r" (SYS_flock)
        : "rax", "rdi", "rsi", "memory"
    );
#elif defined(__i386__)
    asm volatile(
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "mov %3, %%eax\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r" (retval)
        : "r" (fd), "r" (op), "r" (SYS_flock)
        : "eax", "ebx", "ecx", "memory"
    );
#elif defined(__loongarch64)
    asm volatile(
        "move $a0, %1\n"              // fd -> a0
        "move $a1, %2\n"              // op -> a1
        "move $a7, %3\n"              // syscall number -> a7
        "syscall 0\n"                 // execute with literal 0
        "move %0, $a0\n"              // result -> retval
        : "=r" (retval)
        : "r" (fd), "r" (op), "r" (SYS_flock)
        : "$a0", "$a1", "$a7", "memory"
    );
#elif defined(__powerpc__) || defined(__powerpc64__)
    asm volatile(
        "mr 3, %1\n"
        "mr 4, %2\n"
        "mr 0, %3\n"
        "sc\n"
        "mr %0, 3\n"
        : "=r" (retval)
        : "r" (fd), "r" (op), "r" (SYS_flock)
        : "r0", "r3", "r4", "memory"
    );
#elif defined(__riscv)
    asm volatile(
        "mv a0, %1\n"
        "mv a1, %2\n"
        "mv a7, %3\n"
        "ecall\n"
        "mv %0, a0\n"
        : "=r" (retval)
        : "r" (fd), "r" (op), "r" (SYS_flock)
        : "a0", "a1", "a7", "memory"
    );
#else
#error "Unsupported architecture for flock assembly"
#endif
    return retval;
}

static inline int _libinject_atomic_add_asm(volatile int *ptr, int val) {
    int result;
#if defined(__arm__)
    /* ARM Universal: Re-use CAS helper to perform atomic add. 
     * This avoids ldrex/strex build errors on ARMv4/v5 toolchains. */
    int old, new;
    do {
        old = *ptr;
        new = old + val;
    } while (_libinject_cas_asm(ptr, old, new) != old);
    return old;

#elif defined(__mips__) || defined(__mips64__)
    asm volatile(
        "1: ll      %0, %1\n"
        "   addu    $t0, %0, %2\n"
        "   sc      $t0, %1\n"
        "   beqz    $t0, 1b\n"
        : "=&r" (result), "+m" (*ptr)
        : "r" (val)
        : "t0", "memory"
    );
#elif defined(__aarch64__)
    int tmp, status;
    asm volatile(
        "1: ldxr    %w0, [%3]\n"
        "   add     %w1, %w0, %w4\n"
        "   stxr    %w2, %w1, [%3]\n"
        "   cbnz    %w2, 1b\n"
        : "=&r" (result), "=&r" (tmp), "=&r" (status)
        : "r" (ptr), "r" (val)
        : "cc", "memory"
    );
#elif defined(__x86_64__) || defined(__i386__)
    result = val;
    asm volatile(
        "lock; xaddl %0, %1"
        : "+r" (result), "+m" (*ptr)
        : : "memory"
    );
#elif defined(__powerpc__) || defined(__powerpc64__)
    int tmp;
    asm volatile(
        "1: lwarx   %0, 0, %2\n"
        "   add     %1, %0, %3\n"
        "   stwcx.  %1, 0, %2\n"
        "   bne-    1b\n"
        : "=&r" (result), "=&r" (tmp)
        : "r" (ptr), "r" (val)
        : "cc", "memory"
    );
#elif defined(__riscv)
    asm volatile(
        "amoadd.w.aqrl %0, %2, (%1)"
        : "=r" (result)
        : "r" (ptr), "r" (val)
        : "memory"
    );
#elif defined(__loongarch64)
    int old, new_val;
    do {
        old = *ptr;
        new_val = old + val;
    } while (_libinject_cas_asm(ptr, old, new_val) != old);
    result = old;
#else
#error "Unsupported architecture for Atomic Add"
#endif
    return result;
}

static inline void _libinject_barrier_asm(void) {
#if defined(__x86_64__) || defined(__i386__)
    asm volatile("mfence" : : : "memory");
#elif defined(__arm__)
    /* Use Kernel Helper for memory barrier */
    typedef void (*kernel_barrier_t)(void);
    ((kernel_barrier_t)0xffff0fa0)();
#elif defined(__aarch64__)
    asm volatile("dmb ish" : : : "memory");
#elif defined(__mips__) || defined(__mips64__)
    asm volatile("sync" : : : "memory");
#elif defined(__powerpc__) || defined(__powerpc64__)
    asm volatile("sync" : : : "memory");
#elif defined(__riscv)
    asm volatile("fence" : : : "memory");
#elif defined(__loongarch64)
    asm volatile("dbar 0" : : : "memory");
#endif
}


static uint32_t fnv1a_hash(const char *key) {
    uint32_t hash = 2166136261u;
    while (*key) {
        hash ^= (unsigned char)(*key++);
        hash *= 16777619u;
    }
    return hash;
}

static void shm_lock(void) {
    pid_t me = getpid();
    int retries = 0;
    
    for (;;) {
        // Use manual CAS to avoid libgcc dependency
        if (_libinject_cas_asm(&shm_base->lock_pid, 0, me) == 0) {
            _libinject_barrier_asm();
            return;
        }

        pid_t current_owner = shm_base->lock_pid;
        
        // Recovery: Steal if owner is dead, OR if we timed out
        if (retries > 2000 || (current_owner > 0 && kill(current_owner, 0) == -1 && errno == ESRCH)) {
            if (_libinject_cas_asm(&shm_base->lock_pid, current_owner, me) == current_owner) {
                _libinject_barrier_asm();
                return;
            }
            continue;
        }

        int ret = _futex_wait(&shm_base->lock_pid, current_owner);
        if (ret == -1 && errno == ETIMEDOUT) {
            retries = 3000; 
        } else if (ret == -1 && errno != EINTR && errno != EAGAIN) {
            retries++; 
        }
    }
}

static void shm_unlock(void) {
    _libinject_barrier_asm();
    shm_base->lock_pid = 0;
    _futex_wake(&shm_base->lock_pid);
}

/* POSIX I/O Wrappers */
static int _libinject_read_file(const char *path, char *buf, size_t sz) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    size_t total_read = 0;
    ssize_t bytes_read;

    while (total_read < sz - 1 && (bytes_read = read(fd, buf + total_read, sz - 1 - total_read)) > 0) {
        total_read += (size_t)bytes_read;
    }

    buf[total_read] = '\0';
    close(fd);
    return (int)total_read;
}

static int _libinject_write_file(const char *path, const void *data, size_t sz) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;

    size_t total_written = 0;
    ssize_t bytes_written;

    while (total_written < sz) {
        bytes_written = write(fd, ((const char *)data) + total_written, sz - total_written);
        if (bytes_written <= 0) {
            close(fd);
            return -1;
        }
        total_written += (size_t)bytes_written;
    }

    close(fd);
    return (int)total_written;
}

int libnvram_strcmp (const char *p1, const char *p2)
{
  register const unsigned char *s1 = (const unsigned char *) p1;
  register const unsigned char *s2 = (const unsigned char *) p2;
  unsigned char c1, c2;

  do
    {
      c1 = (unsigned char) *s1++;
      c2 = (unsigned char) *s2++;
      if (c1 == '\0')
	return c1 - c2;
    }
  while (c1 == c2);

  return c1 - c2;
}

int libnvram_strncmp (const char *s1, const char *s2, size_t n)
{
  unsigned char c1 = '\0';
  unsigned char c2 = '\0';

  if (n >= 4)
    {
      size_t n4 = n >> 2;
      do
	{
	  c1 = (unsigned char) *s1++;
	  c2 = (unsigned char) *s2++;
	  if (c1 == '\0' || c1 != c2)
	    return c1 - c2;
	  c1 = (unsigned char) *s1++;
	  c2 = (unsigned char) *s2++;
	  if (c1 == '\0' || c1 != c2)
	    return c1 - c2;
	  c1 = (unsigned char) *s1++;
	  c2 = (unsigned char) *s2++;
	  if (c1 == '\0' || c1 != c2)
	    return c1 - c2;
	  c1 = (unsigned char) *s1++;
	  c2 = (unsigned char) *s2++;
	  if (c1 == '\0' || c1 != c2)
	    return c1 - c2;
	} while (--n4 > 0);
      n &= 3;
    }

  while (n > 0)
    {
      c1 = (unsigned char) *s1++;
      c2 = (unsigned char) *s2++;
      if (c1 == '\0' || c1 != c2)
	return c1 - c2;
      n--;
    }

  return c1 - c2;
}


static int _libinject_ready(void) {
    if (init_failed) return 0;
    if (!init) return 0;
    if (shm_base == MAP_FAILED || shm_base == NULL) return 0;
    if (shm_base->magic != SHM_MAGIC) return 0;
    return 1;
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
    if (init_failed) return E_FAILURE;
    if (init) return E_SUCCESS;

    nvram_log("INIT", "START", NULL);
    logging_enabled = igloo_hypercall2(111, 0, 0);

#ifdef SYS_mkdirat
    if (SYS_mkdir == SYS_mkdirat) (void)syscall(SYS_mkdir, AT_FDCWD, MOUNT_POINT, 0777);
    else (void)syscall(SYS_mkdir, MOUNT_POINT, 0777);
#else
    (void)syscall(SYS_mkdir, MOUNT_POINT, 0777);
#endif

    shm_fd = open(SHM_FILE, O_RDWR | O_CREAT| O_LARGEFILE, 0666);
    if (shm_fd < 0) {
#ifdef DEBUG_LOGGING
        char err[64];
        snprintf(err, sizeof(err), "errno=%d", errno);
        nvram_log("ERROR", "INIT SHM_OPEN", err);
#endif
        init_failed = 1;
        return E_FAILURE;
    }

    off_t current_size = lseek(shm_fd, 0, SEEK_END);
    if (current_size != (off_t)sizeof(nvram_shm_t)) {
        nvram_log("INIT", "TRUNCATE", "Resizing SHM");
#if defined(SYS_ftruncate64)
        if (syscall(SYS_ftruncate64, shm_fd, (long long)sizeof(nvram_shm_t)) != 0) {
#else
        if (syscall(SYS_ftruncate, shm_fd, (long)sizeof(nvram_shm_t)) != 0) {
#endif
            init_failed = 1;
            close(shm_fd);
            return E_FAILURE;
        }
    }

    shm_base = mmap(NULL, sizeof(nvram_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_base == MAP_FAILED) {
        nvram_log("ERROR", "INIT", "Failed to mmap SHM");
        init_failed = 1; close(shm_fd); return E_FAILURE;
    }

    _libinject_flock_asm(shm_fd, LOCK_EX);

    if (shm_base->magic != SHM_MAGIC) {
        nvram_log("INIT", "SEED", "Wiping SHM and ingesting directory");
        memset(shm_base, 0, sizeof(nvram_shm_t));
        shm_base->magic = SHM_MAGIC;
        shm_base->lock_pid = 0;
        shm_base->used_entries = 0;
        shm_base->free_head = 0;
        for (int i = 0; i < MAX_NVRAM_ENTRIES; i++) {
            shm_base->entries[i].next_idx = (i < MAX_NVRAM_ENTRIES - 1) ? i + 1 : -1;
            shm_base->entries[i].in_use = 0;
        }
        for (int i = 0; i < HASH_SIZE; i++) shm_base->hash_table[i] = -1;
        
        // Flag ready before we start SETs to prevent deadlock
        init = 1; 

        int dir_fd = open(MOUNT_POINT, O_RDONLY);
        nvram_log("SEED_DIR", "open result", dir_fd >= 0 ? "OK" : "FAIL");
        if (dir_fd >= 0) {
            char d_buf[4096];
            int use_64 = 1;
            int nread = 0;
            int ingested = 0;
#if defined(SYS_getdents64)
            nread = syscall(SYS_getdents64, dir_fd, d_buf, sizeof(d_buf));
            char nread_str[32];
            snprintf(nread_str, sizeof(nread_str), "%d (errno=%d)", nread, errno);
            nvram_log("SEED_GETDENTS", "initial nread", nread_str);
            if (nread < 0) {
                use_64 = 0;
                lseek(dir_fd, 0, SEEK_SET);
            }
#else
            use_64 = 0;
            nvram_log("SEED_GETDENTS", "getdents64", "NOT AVAILABLE");
#endif


            while (nread > 0) {
                for (int bpos = 0; bpos < nread;) {
                    char *d_name = NULL;
                    unsigned short d_reclen = 0;
                    unsigned char dtype = 0;

                    if (use_64) {
#if defined(SYS_getdents64)
                        struct kernel_dirent64 *d = (struct kernel_dirent64 *)(d_buf + bpos);
                        d_name = d->d_name;
                        d_reclen = d->d_reclen;
                        dtype = d->d_type;
#endif
                    } else {
#if defined(SYS_getdents)
                        struct kernel_dirent *d = (struct kernel_dirent *)(d_buf + bpos);
                        d_name = d->d_name;
                        d_reclen = d->d_reclen;
                        dtype = DT_UNKNOWN; // Legacy getdents doesn't provide d_type
#endif
                    }
                    nvram_log("SEED_ENTRY", "d_name raw", d_name);
                    char reclen_str[32];
                    snprintf(reclen_str, sizeof(reclen_str), "%d", (int)d_reclen);
                    nvram_log("SEED_ENTRY", "d_reclen", reclen_str);

                    if (d_reclen == 0) {
                        nvram_log("SEED_ENTRY", "d_reclen is 0", "Breaking loop to prevent infinite loop");
                        break; // Defensive guard
                    }

                    
                    if (libnvram_strcmp(d_name, ".") != 0 && libnvram_strcmp(d_name, "..") != 0) {
                        char path[PATH_MAX];
                        snprintf(path, sizeof(path), "%s%s", MOUNT_POINT, d_name);
                        
                        int fd = open(path, O_RDONLY);
                        if (fd >= 0) {
                            char stack_buf[INLINE_VAL_LEN];
                            ssize_t b = read(fd, stack_buf, INLINE_VAL_LEN);
                            
                            int is_file = (b == INLINE_VAL_LEN); // If it maxed out the read, it's too big to inline
                            if (b < 0) b = 0;

                            /* Lockless-style direct SHM injection */
                            shm_lock();
                            uint32_t h = fnv1a_hash(d_name) % HASH_SIZE;
                            int32_t idx = shm_base->hash_table[h], target = -1;
                            
                            while (idx >= 0) {
                                if (shm_base->entries[idx].in_use && libnvram_strncmp(shm_base->entries[idx].key, d_name, MAX_KEY_LEN) == 0) { 
                                    target = idx; break; 
                                }
                                idx = shm_base->entries[idx].next_idx;
                            }

                            if (target == -1 && shm_base->free_head != -1) {
                                target = shm_base->free_head;
                                shm_base->free_head = shm_base->entries[target].next_idx;
                                shm_base->entries[target].next_idx = shm_base->hash_table[h];
                                shm_base->hash_table[h] = target;
                                
                                shm_base->entries[target].in_use = 1;
                                strncpy(shm_base->entries[target].key, d_name, MAX_KEY_LEN - 1);
                                shm_base->entries[target].key[MAX_KEY_LEN - 1] = '\0';
                                shm_base->used_entries++;
                            }

                            if (target != -1) {
                                nvram_entry_t *e = &shm_base->entries[target];
                                e->is_file_backed = is_file;
                                e->is_binary = 0;
                                
                                if (is_file) {
                                    e->val_len = 0; // Lazy loading will handle size later
                                    memset(e->inline_val, 0, INLINE_VAL_LEN);
                                    nvram_log("SEED_FILE", d_name, "Registered as File-Backed");
                                } else {
                                    e->val_len = b;
                                    memcpy(e->inline_val, stack_buf, b);
                                    e->inline_val[b] = '\0'; // Safe because max b is 127
                                    unlink(path); 
                                    nvram_log("SEED_INLINE", d_name, "Cached and unlinked");
                                }
                            }
                            shm_unlock();
                            close(fd);
                            ingested++;
                        } else {
                            nvram_log("SEED_ERR", "FILE_OPEN", path);
                        }
                    }
                    bpos += d_reclen;
                }

                if (use_64) {
#if defined(SYS_getdents64)
                    nread = syscall(SYS_getdents64, dir_fd, d_buf, sizeof(d_buf));
#endif
                } else {
#if defined(SYS_getdents)
                    nread = syscall(SYS_getdents, dir_fd, d_buf, sizeof(d_buf));
#endif
                }
            }
            snprintf(d_buf, sizeof(d_buf), "Ingested %u entries", shm_base->used_entries);
            nvram_log("SEED", "COMPLETE", d_buf);
            close(dir_fd);
        }else{
            nvram_log("SEED_ERR", "Failed to open mount directory", MOUNT_POINT);
        }
    }

    init = 1;
    _libinject_flock_asm(shm_fd, LOCK_UN);
    nvram_log("INIT", "SUCCESS", NULL);
    return _libinject_ready() ? E_SUCCESS : E_FAILURE;
}


int libinject_nvram_reset(void) {
    PRINT_MSG("%s\n", "Reseting NVRAM...");
    nvram_log("RESET", "START", NULL);

    if (libinject_nvram_clear() != E_SUCCESS) {
        PRINT_MSG("%s\n", "Unable to clear NVRAM!");
        return E_FAILURE;
    }
    return E_SUCCESS;
}
int libinject_nvram_clear(void) {
    nvram_log("CLEAR", "START", NULL);
    if (!init && libinject_nvram_init() != E_SUCCESS) return E_FAILURE;

    shm_lock();
    for (int i = 0; i < MAX_NVRAM_ENTRIES; i++) {
        nvram_entry_t *entry = &shm_base->entries[i];
        if (entry->in_use) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s%s", MOUNT_POINT, entry->key);
            unlink(path);
        }
    }

    shm_base->free_head = 0;
    for (int i = 0; i < MAX_NVRAM_ENTRIES; i++) {
        shm_base->entries[i].in_use = 0;
        shm_base->entries[i].next_idx = (i < MAX_NVRAM_ENTRIES - 1) ? i + 1 : -1;
    }
    for (int i = 0; i < HASH_SIZE; i++) shm_base->hash_table[i] = -1;
    shm_base->used_entries = 0;
    shm_unlock();

    return E_SUCCESS;
}

int libinject_nvram_close(void) {
    PRINT_MSG("%s\n", "Closing NVRAM...");
    nvram_log("CLOSE", "START", NULL);
    return E_SUCCESS;
}

int libinject_nvram_list_add(const char *key, const char *val) {
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);
    char *pos;

    PRINT_MSG("%s = %s + %s\n", val, temp, key);
    nvram_log("L_ADD", key, val);

    if (libinject_nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        free(temp);
        return libinject_nvram_set(key, val);
    }

    if (!key || !val || strlen(temp) + 1 + strlen(val) + 1 > BUFFER_SIZE) {
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
    nvram_log("L_EXST", key, val);
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return (magic == LIST_MAGIC) ? NULL : (char *)E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);

    if (libinject_nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        free(temp);
        return (magic == LIST_MAGIC) ? NULL : (char *)E_FAILURE;
    }
    if (!val) {
        free(temp);
        return (magic == LIST_MAGIC) ? NULL : (char *)E_FAILURE;
    }

    char *saveptr = NULL;
    char *tok = strtok_r(temp, LIST_SEP, &saveptr);
    while (tok) {
        if (!libnvram_strcmp(tok, val) || (tok[0] != '\0' && !libnvram_strcmp(tok + 1, val))) {
            if (magic == LIST_MAGIC) {
                // Use manual atomic add to avoid libgcc dependency
                unsigned int idx = (unsigned int)_libinject_atomic_add_asm((int*)&pool_idx, 1) % NUM_RET_BUFS;
                char *out_buf = list_ret_pool[idx];
                
                strncpy(out_buf, tok, BUFFER_SIZE - 1);
                out_buf[BUFFER_SIZE - 1] = '\0';
                
                free(temp);
                return out_buf;
            }
            free(temp);
            return (char *)E_SUCCESS;
        }
        tok = strtok_r(NULL, LIST_SEP, &saveptr);
    }

    free(temp);
    return (magic == LIST_MAGIC) ? NULL : (char *) E_FAILURE;
}

int libinject_nvram_list_del(const char *key, const char *val) {
    nvram_log("L_DEL", key, val);
    char *temp = malloc(BUFFER_SIZE);
    if (!temp) return E_FAILURE;
    memset(temp, 0, BUFFER_SIZE);

    if (libinject_nvram_get_buf(key, temp, BUFFER_SIZE) != E_SUCCESS) {
        free(temp);
        return E_SUCCESS;
    }

    PRINT_MSG("%s = %s - %s\n", key, temp, val);

    if (!val) {
        free(temp);
        return E_FAILURE;
    }

    size_t val_len = strlen(val);
    char *curr = temp;

    // FIX 2: Non-destructive dual-matching token wipe
    while (*curr) {
        if (*curr == LIST_SEP[0]) {
            curr++;
            continue;
        }

        // Measure current token length up to the next separator
        char *end = strchr(curr, LIST_SEP[0]);
        size_t tok_len = end ? (size_t)(end - curr) : strlen(curr);

        int match = 0;
        // Check standard exact match
        if (tok_len == val_len && libnvram_strncmp(curr, val, val_len) == 0) {
            match = 1;
        } 
        // Check legacy prefix match
        else if (tok_len == val_len + 1 && libnvram_strncmp(curr + 1, val, val_len) == 0) {
            match = 1;
        }

        if (match) {
            memset(curr, LIST_SEP[0], tok_len); // Safely overwrite only the matched token
        }
        
        curr += tok_len;
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
    if (!buf || sz == 0) return E_FAILURE;
    memset(buf, 0, sz);
    if (!key) {
        PRINT_MSG("NULL input key, buffer: %s!\n", buf);
        nvram_log("ERROR", "NULL_KEY", "get_buf");
#ifdef FIRMAE_NVRAM
        return E_SUCCESS;
#else
        return E_FAILURE;
#endif
    }

    nvram_log("GET", key, NULL);

    if (!init && libinject_nvram_init() != E_SUCCESS) return E_FAILURE;
    if (!_libinject_ready()) return E_FAILURE;

    int found = 0;
    int read_from_file = 0;
    int rv;
    char fake_path[PATH_MAX];
    snprintf(fake_path, sizeof(fake_path), "%s%s", MOUNT_POINT, key);

    shm_lock();

    uint32_t h = fnv1a_hash(key) % HASH_SIZE;
    int32_t idx = shm_base->hash_table[h];
    int hops = 0;

    while (idx >= 0 && idx < MAX_NVRAM_ENTRIES && hops++ < MAX_NVRAM_ENTRIES) {
        nvram_entry_t *entry = &shm_base->entries[idx];
        if (entry->in_use && libnvram_strncmp(entry->key, key, MAX_KEY_LEN) == 0) {
            found = 1;
            if (entry->is_file_backed) {
                read_from_file = 1;
            } else {
                // Restore original behavior: Raw binary string copy without ASCII conversion
                strncpy(buf, entry->inline_val, sz - 1);
            }
            break;
        }
        idx = entry->next_idx;
    }

    shm_unlock();

    if (!found) {
        nvram_log("GET_MISS", key, NULL);
        if (logging_enabled) {
            rv = igloo_hypercall2(107, (unsigned long)fake_path, strlen(fake_path));
            (void)rv;
        }
#ifdef FIRMAE_NVRAM
            //If key value is not found, make the default value to ""
            //if (!libnvram_strcmp(key, "noinitrc")) // Weird magic constant from FirmAE
            //    return E_FAILURE;
        buf[0] = '\0';
        return E_SUCCESS;
#else
        return E_FAILURE;
#endif
    }

    if (read_from_file) {
        if (_libinject_read_file(fake_path, buf, sz) < 0) {
            nvram_log("GET_ERR", key, "File Read Failed");
            return E_FAILURE;
        }
    }

    if (logging_enabled) {
        rv = igloo_hypercall2(108, (unsigned long)fake_path, strlen(fake_path));
        while (rv == 1) {
            PAGE_IN(fake_path);
            rv = igloo_hypercall2(108, (unsigned long)fake_path, strlen(fake_path));
        }
    }

    PRINT_MSG("= \"%s\"\n", buf);
    nvram_log("GET_OK", key, buf);
    return E_SUCCESS;
}

int libinject_nvram_get_int(const char *key) {
    char buf[32];
    nvram_log("GET_INT", key, NULL);

    if (!key) return E_FAILURE;
    if (!init && libinject_nvram_init() != E_SUCCESS) return E_FAILURE;
    if (!_libinject_ready()) return E_FAILURE;

    char fake_path[PATH_MAX];
    snprintf(fake_path, sizeof(fake_path), "%s%s", MOUNT_POINT, key);

    shm_lock();
    uint32_t h = fnv1a_hash(key) % HASH_SIZE;
    int32_t idx = shm_base->hash_table[h];
    int hops = 0;

    while (idx >= 0 && idx < MAX_NVRAM_ENTRIES && hops++ < MAX_NVRAM_ENTRIES) {
        nvram_entry_t *entry = &shm_base->entries[idx];
        if (entry->in_use && libnvram_strncmp(entry->key, key, MAX_KEY_LEN) == 0) {
            if (entry->is_file_backed) {
                shm_unlock();
                int fd = open(fake_path, O_RDONLY);
                if (fd < 0) return E_FAILURE;
                size_t b = read(fd, buf, sizeof(buf) - 1);
                if (b <= 0) { close(fd); return E_FAILURE; }
                buf[b] = '\0';
                close(fd);
                /* fall through to ASCII parse below */
                goto parse_ascii;
            } else if (entry->is_binary) {
                /* Stored via set_int — raw binary copy */
                int ret;
                memcpy(&ret, entry->inline_val, sizeof(ret));
                shm_unlock();
                nvram_log("GET_INT_B", key, "Success");
                return ret;
            } else {
                /* Stored via set as a string — parse as ASCII */
                strncpy(buf, entry->inline_val, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                shm_unlock();
                goto parse_ascii;
            }
        }
        idx = entry->next_idx;
    }
    shm_unlock();
    nvram_log("GET_INT_M", key, "Miss");
    return E_FAILURE; /* Key not found */

parse_ascii:;
    char *endptr;
    long val = strtol(buf, &endptr, 10);
    if (endptr != buf && (*endptr == '\0' || *endptr == '\n')) {
        nvram_log("GET_INT_A", key, "Success");
        return (int)val;
    }
    nvram_log("GET_INT_E", key, "Parse Fail");
    return E_FAILURE;
}


int libinject_nvram_getall(char *buf, size_t len) {
    nvram_log("GETALL", "START", NULL);
    if (!buf || len < 2) return E_FAILURE;   // need room for final "\0\0"
    if (!init && libinject_nvram_init() != E_SUCCESS) return E_FAILURE;
    if (!_libinject_ready()) return E_FAILURE;

    size_t pos = 0;
    buf[0] = '\0';

    shm_lock();
    for (int i = 0; i < MAX_NVRAM_ENTRIES; i++) {
        nvram_entry_t *entry = &shm_base->entries[i];
        if (!entry->in_use) continue;

        // Reserve 2 bytes for final double-NUL
        if (pos + 2 >= len) break;

        // Write "key=" first
        int n = snprintf(buf + pos, len - pos, "%s=", entry->key);
        if (n < 0) { shm_unlock(); return E_FAILURE; }
        if ((size_t)n >= len - pos) break;   // no space
        pos += (size_t)n;

        // Append value
        if (entry->is_file_backed) {
            char path[PATH_MAX];
            int pn = snprintf(path, sizeof(path), "%s%s", MOUNT_POINT, entry->key);
            if (pn >= 0 && (size_t)pn < sizeof(path)) {
                int fd = open(path, O_RDONLY);
                if (fd >= 0) {
                    size_t maxread = (len - pos) - 2;
                    ssize_t r = read(fd, buf + pos, maxread);
                    if (r > 0) {
                        pos += (size_t)r;
                    }
                    close(fd);
                }
            }
        } else {
             /*
              * FIX(5): Match original getall behavior:
              * it copied raw bytes from the underlying storage (files),
              * so binary ints appeared as raw binary, not decimal strings.
              * Copy entry->val_len bytes (bounded) from inline_val.
              */
             size_t vlen = entry->val_len;
             if (vlen > INLINE_VAL_LEN) vlen = INLINE_VAL_LEN;
             size_t maxcopy = (len - pos) - 2;
             if (vlen > maxcopy) vlen = maxcopy;
             memcpy(buf + pos, entry->inline_val, vlen);
             pos += vlen;
        }

        // Per-entry NUL terminator
        buf[pos++] = '\0';
    }
    shm_unlock();

    // Final extra NUL (double-NUL termination)
    buf[pos++] = '\0';
    return E_SUCCESS;
}

int libinject_nvram_set(const char *key, const char *val) {
    if (!key || !val) return E_FAILURE;
    if (!init && libinject_nvram_init() != E_SUCCESS) return E_FAILURE;
    if (!_libinject_ready()) return E_FAILURE;
    nvram_log("SET", key, val);
    
    char skey[MAX_KEY_LEN]; 
    strncpy(skey, key, MAX_KEY_LEN-1); 
    skey[MAX_KEY_LEN-1] = '\0';
    size_t vlen = strlen(val);
    int use_file = (vlen >= INLINE_VAL_LEN);
    char path[PATH_MAX]; snprintf(path, sizeof(path), "%s%s", MOUNT_POINT, skey);

    if (logging_enabled) igloo_hypercall2(109, (unsigned long)path, (unsigned long)val);
    if (use_file) {
        if (_libinject_write_file(path, val, vlen) < 0) {
            return E_FAILURE;
        }
    }

    shm_lock();
    uint32_t h = fnv1a_hash(skey) % HASH_SIZE;
    int32_t idx = shm_base->hash_table[h], target = -1;
    int hops = 0;
    while (idx >= 0 && idx < MAX_NVRAM_ENTRIES && hops++ < MAX_NVRAM_ENTRIES){
        if (shm_base->entries[idx].in_use && libnvram_strncmp(shm_base->entries[idx].key, skey, MAX_KEY_LEN) == 0) { target = idx; break; }
        idx = shm_base->entries[idx].next_idx;
    }

    if (target == -1) {
        target = shm_base->free_head;
        if (target != -1) {
            shm_base->free_head = shm_base->entries[target].next_idx;
            shm_base->entries[target].next_idx = shm_base->hash_table[h];
            shm_base->hash_table[h] = target;
            shm_base->entries[target].in_use = 1;
            strcpy(shm_base->entries[target].key, skey);
            shm_base->used_entries++;
        }
    }

    if (target != -1) {
        nvram_entry_t *e = &shm_base->entries[target];
        e->val_len = vlen; e->is_file_backed = use_file; e->is_binary = 0;
        if (use_file) memset(e->inline_val, 0, INLINE_VAL_LEN);
        else { strncpy(e->inline_val, val, INLINE_VAL_LEN-1); e->inline_val[INLINE_VAL_LEN-1] = '\0'; unlink(path); }
    }
    shm_unlock();
    
    if (target == -1) nvram_log("ERROR", "SET", "OOM");
    
    return (target != -1) ? E_SUCCESS : E_FAILURE;
}

int libinject_nvram_set_int(const char *key, const int val) {
    if (!key) return E_FAILURE;
    nvram_log("SET_INT", key, "INT_VALUE");
    
    if (!init && libinject_nvram_init() != E_SUCCESS) return E_FAILURE;
    if (!_libinject_ready()) return E_FAILURE;

    char fake_path[PATH_MAX];
    snprintf(fake_path, sizeof(fake_path), "%s%s", MOUNT_POINT, key);

    shm_lock();
    uint32_t h = fnv1a_hash(key) % HASH_SIZE;
    int32_t idx = shm_base->hash_table[h];
    int32_t target_idx = -1;
    int hops = 0;

    // Walk hash chain with loop protection
    while (idx >= 0 && idx < MAX_NVRAM_ENTRIES && hops++ < MAX_NVRAM_ENTRIES) {
        if (shm_base->entries[idx].in_use && libnvram_strncmp(shm_base->entries[idx].key, key, MAX_KEY_LEN) == 0) {
            target_idx = idx;
            break;
        }
        idx = shm_base->entries[idx].next_idx;
    }

    // O(1) Allocation from free list
    if (target_idx == -1) {
        target_idx = shm_base->free_head;
        if (target_idx == -1) {
            shm_unlock();
            nvram_log("ERROR", "SET_INT", "OOM");
            return E_FAILURE;
        }
        
        // Pop from free list
        shm_base->free_head = shm_base->entries[target_idx].next_idx;

        // Link to head of hash bucket
        shm_base->entries[target_idx].next_idx = shm_base->hash_table[h];
        shm_base->hash_table[h] = target_idx;
        
        shm_base->entries[target_idx].in_use = 1;
        strncpy(shm_base->entries[target_idx].key, key, MAX_KEY_LEN - 1);
        shm_base->entries[target_idx].key[MAX_KEY_LEN - 1] = '\0';
        shm_base->used_entries++;
    }

    // Store integer internally
    nvram_entry_t *target_entry = &shm_base->entries[target_idx];
    target_entry->val_len = (uint32_t)sizeof(val);
    target_entry->is_file_backed = 0;
    target_entry->is_binary = 1; // Explicitly tag as binary integer
    memcpy(target_entry->inline_val, &val, sizeof(val));
    unlink(fake_path);

    shm_unlock();
    return E_SUCCESS;
}

int libinject_nvram_unset(const char *key) {
    if (!key) return E_FAILURE;
    if (!init && libinject_nvram_init() != E_SUCCESS) return E_FAILURE;
    if (!_libinject_ready()) return E_FAILURE;
    nvram_log("UNSET", key, NULL);
    char skey[MAX_KEY_LEN]; strncpy(skey, key, MAX_KEY_LEN-1); skey[MAX_KEY_LEN-1] = '\0';
    char path[PATH_MAX]; snprintf(path, sizeof(path), "%s%s", MOUNT_POINT, skey);

    if (logging_enabled) {
        int rv = igloo_hypercall2(110, (unsigned long)path, strlen(path));
        while (rv == 1) {
            PAGE_IN(path);
            rv = igloo_hypercall2(110, (unsigned long)path, strlen(path));
        }
    }

    shm_lock();
    uint32_t h = fnv1a_hash(skey) % HASH_SIZE;
    int32_t idx = shm_base->hash_table[h], prev = -1;
    while (idx >= 0) {
        nvram_entry_t *e = &shm_base->entries[idx];
        if (e->in_use && libnvram_strncmp(e->key, skey, MAX_KEY_LEN) == 0) {
            if (prev == -1) shm_base->hash_table[h] = e->next_idx;
            else shm_base->entries[prev].next_idx = e->next_idx;
            e->in_use = 0;
            if (e->is_file_backed) unlink(path);
            e->next_idx = shm_base->free_head;
            shm_base->free_head = idx;
            shm_base->used_entries--;
            break;
        }
        prev = idx; idx = e->next_idx;
    }
    shm_unlock();
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

    int cmp = libnvram_strncmp(temp, val, BUFFER_SIZE);
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
    nvram_log("COMMIT", "SYNC", NULL);
    sync();
    return E_SUCCESS;
}

int libinject_parse_nvram_from_file(const char *file) {
    nvram_log("PARSE", "START", file);
    int fd = open(file, O_RDONLY);
    if (fd < 0) return E_FAILURE;

    // Use lseek to determine file length without stat()
    off_t fileLen = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    if (fileLen <= 0) {
        close(fd);
        return E_FAILURE;
    }

    char *buffer = (char*)malloc((size_t)fileLen + 1);
    if (!buffer) {
        close(fd);
        return E_FAILURE;
    }

    ssize_t bytes_read = read(fd, buffer, (size_t)fileLen);
    close(fd);

    if (bytes_read < 0) {
        free(buffer);
        return E_FAILURE;
    }
    buffer[bytes_read] = '\0';

    /* split the buffer including null byte */
#define LEN 1024
    int i=0, j=0, k=0; int left = 1;
    char larr[LEN]="", rarr[LEN]="";

    for(i=0; i < bytes_read; i++) {
        char c = buffer[i];

        if (left == 1 && j < LEN-1)
            larr[j++] = c;
        else if (left == 0 && k < LEN-1)
            rarr[k++] = c;

        if (c == '=') {
            left = 0;
            larr[j-1] = '\0';
        }
        if (c == '\0') {
            rarr[k] = '\0';
            libinject_nvram_set(larr, rarr);
            j = 0; k = 0; left = 1;
            memset(larr, 0, LEN); 
            memset(rarr, 0, LEN);
        }
    }
    free(buffer);
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
