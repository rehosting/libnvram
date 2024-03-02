#ifndef INCLUDE_NVRAM_H
#define INCLUDE_NVRAM_H

// Locks the nvram directory. Will block.
static int dir_lock();
// Unlocks the nvram directory.
static void dir_unlock(int dirfd);

/* The following functions form the standard NVRAM API. Functions that return integers
 * will generally return E_SUCCESS/E_FAILURE, with the exception of nvram_get_int(). */

// Initializes NVRAM with default values. Will hold lock.
int nvram_init(void);
// Restores original NVRAM default values.
int nvram_reset(void);
// Clears NVRAM values. Will hold lock.
int nvram_clear(void);
// Pretends to close NVRAM, does nothing.
int nvram_close(void);
// Pretends to commit NVRAM, actually synchronizes file system.
int nvram_commit(void);

// Given a key, gets the corresponding NVRAM value. If key is non-existent, returns NULL.
// Will dynamically allocate memory, so the user should call free().
// On MIPS, will use $a1 as key if $a0 is NULL.
char *nvram_get(const char *key);
// Given a key, gets the corresponding NVRAM value. If key is non-existent, returns "".
// Will dynamically allocate memory.
char *nvram_safe_get(const char *key);
// Given a key, gets the corresponding NVRAM value. If key is non-existent, returns val.
// Otherwise, returns NULL. Will dynamically allocate memory.
char *nvram_default_get(const char *key, const char *val);
// Given a key, gets the corresponding NVRAM value into a user-supplied buffer.
// Will hold lock.
int nvram_get_buf(const char *key, char *buf, size_t sz);
// Given a key, gets the corresponding NVRAM value as integer. If key is non-existent, returns E_FAILURE.
// Will hold lock.
int nvram_get_int(const char *key);
// Gets all NVRAM keys and values into a user-supplied buffer, of the format "key=value...".
// Will hold lock.
int nvram_getall(char *buf, size_t len);

// Given a key and value, sets the corresponding NVRAM value. Will hold lock.
int nvram_set(const char *key, const char *val);
// Given a key and value as integer, sets the corresponding NVRAM value. Will hold lock.
int nvram_set_int(const char *key, const int val);
// Given a key, unsets the corresponding NVRAM value. Will hold lock.
int nvram_unset(const char *key);
// Given a key, unset the corresponding NVRAM value if it is set. Will hold lock if it's set
int nvram_safe_unset(const char *key);
// Reloads default NVRAM values.
int nvram_set_default(void);

// Adds a list entry to a NVRAM value.
int nvram_list_add(const char *key, const char *val);
// Checks whether a list entry exists in a NVRAM value. If the magic argument
// is equal to LIST_MAGIC, will either return a pointer to the match or NULL.
char *nvram_list_exist(const char *key, const char *val, int magic);
// Deletes a list entry from a NVRAM value.
int nvram_list_del(const char *key, const char *val);

// Given a key, checks whether the corresponding NVRAM value matches val.
int nvram_match(const char *key, const char *val);
// Given a key, checks whether the corresponding NVRAM value does not match val.
int nvram_invmatch(const char *key, const char *val);

// Parse a default nvram including null byte and passing key,val to nvram_set(JR6150)
int parse_nvram_from_file(const char *file);

#ifdef FIRMAE_KERNEL
int VCTGetPortAutoNegSetting(char *a1, int a2);
// R6200V2, R6250-V1, R6300v2, R6700-V1, R7000-V1 patch infinite loop in httpd
int agApi_fwGetFirstTriggerConf(char *a1);
// R6200V2, R6250-V1, R6300v2, R6700-V1, R7000-V1 patch infinite loop in httpd
int agApi_fwGetNextTriggerConf(char *a1);
#endif

int flock_asm(int fd, int op) {
    int retval;
#if defined(__mips__)
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
#endif
    return retval;
}
#endif
