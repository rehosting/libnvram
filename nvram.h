#ifndef INCLUDE_NVRAM_H
#define INCLUDE_NVRAM_H

// We prefix every "exposed" function with `libinject_` as we don't want to clobber
// anything by default.
// Internal methods will be prefixed with `_libinject_`.

// Locks the nvram directory. Will block.
//static int _libinject_dir_lock();
// Unlocks the nvram directory.
//static void _libinject_dir_unlock(int dirfd);

/* The following functions form the standard NVRAM API. Functions that return integers
 * will generally return E_SUCCESS/E_FAILURE, with the exception of nvram_get_int(). */

// Initializes NVRAM with default values. Will hold lock.
int libinject_nvram_init(void);
// Restores original NVRAM default values.
int libinject_nvram_reset(void);
// Clears NVRAM values. Will hold lock.
int libinject_nvram_clear(void);
// Pretends to close NVRAM, does nothing.
int libinject_nvram_close(void);
// Pretends to commit NVRAM, actually synchronizes file system.
int libinject_nvram_commit(void);

// Given a key, gets the corresponding NVRAM value. If key is non-existent, returns NULL.
// Will dynamically allocate memory, so the user should call free().
// On MIPS, will use $a1 as key if $a0 is NULL.
char *libinject_nvram_get(const char *key);
// Given a key, gets the corresponding NVRAM value. If key is non-existent, returns "".
// Will dynamically allocate memory.
char *libinject_nvram_safe_get(const char *key);
// Given a key, gets the corresponding NVRAM value. If key is non-existent, returns val.
// Otherwise, returns NULL. Will dynamically allocate memory.
char *libinject_nvram_default_get(const char *key, const char *val);
// Given a key, gets the corresponding NVRAM value into a user-supplied buffer.
// Will hold lock.
int libinject_nvram_get_buf(const char *key, char *buf, size_t sz);
// Given a key, gets the corresponding NVRAM value as integer. If key is non-existent, returns E_FAILURE.
// Will hold lock.
int libinject_nvram_get_int(const char *key);
// Gets all NVRAM keys and values into a user-supplied buffer, of the format "key=value...".
// Will hold lock.
int libinject_nvram_getall(char *buf, size_t len);

// Given a key and value, sets the corresponding NVRAM value. Will hold lock.
int libinject_nvram_set(const char *key, const char *val);
// Given a key and value as integer, sets the corresponding NVRAM value. Will hold lock.
int libinject_nvram_set_int(const char *key, const int val);
// Given a key, unsets the corresponding NVRAM value. Will hold lock.
int libinject_nvram_unset(const char *key);
// Given a key, unset the corresponding NVRAM value if it is set. Will hold lock if it's set
int libinject_nvram_safe_unset(const char *key);

// Reloads default NVRAM values. XXX: unused in libinject as we initialize differently
//int libinject_nvram_set_default(void);

// Adds a list entry to a NVRAM value.
int libinject_nvram_list_add(const char *key, const char *val);
// Checks whether a list entry exists in a NVRAM value. If the magic argument
// is equal to LIST_MAGIC, will either return a pointer to the match or NULL.
char *libinject_nvram_list_exist(const char *key, const char *val, int magic);
// Deletes a list entry from a NVRAM value.
int libinject_nvram_list_del(const char *key, const char *val);

// Given a key, checks whether the corresponding NVRAM value matches val.
int libinject_nvram_match(const char *key, const char *val);
// Given a key, checks whether the corresponding NVRAM value does not match val.
int libinject_nvram_invmatch(const char *key, const char *val);

// Parse a default nvram including null byte and passing key,val to nvram_set(JR6150)
int libinject_parse_nvram_from_file(const char *file);

/* Atheros/Broadcom NVRAM */
int libinject_nvram_get_nvramspace(void);
char *libinject_nvram_nget(const char *fmt, ...);
int libinject_nvram_nset(const char *val, const char *fmt, ...);
int libinject_nvram_nset_int(const int val, const char *fmt, ...);
int libinject_nvram_nmatch(const char *val, const char *fmt, ...);

/* Realtek */
int libinject_apmib_get(const int key, void *buf);
int libinject_apmib_set(const int key, void *buf);

/* Netgear ACOS */
int libinject_WAN_ith_CONFIG_GET(char *buf, const char *fmt, ...);

/* ZyXel / Edimax */
int libinject_nvram_getall_adv(int unk, char *buf, size_t len);
char *libinject_nvram_get_adv(int unk, const char *key);
int libinject_nvram_set_adv(int unk, const char *key, const char *val);
int libinject_nvram_state(int unk1, void *unk2, void *unk3);
int libinject_envram_commit(void);
int libinject_envram_default(void);
int libinject_envram_load(void);
int libinject_envram_safe_load(void);
int libinject_envram_match(const char *key, const char *val);
int libinject_envram_get(const char* key, char *buf);
int libinject_envram_getf(const char* key, const char *fmt, ...);
int libinject_envram_set(const char *key, const char *val);
int libinject_envram_setf(const char* key, const char* fmt, ...);
int libinject_envram_unset(const char *key);

/* Ralink */
char *libinject_nvram_bufget(int idx, const char *key);
int libinject_nvram_bufset(int idx, const char *key, const char *val);

// Base functions
int libinject_ret_true();
int libinject_ret_false();
int libinject_ret_true1(char* a1);
int libinject_ret_false1(char* a1);

#endif
