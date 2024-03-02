#ifndef INCLUDE_ALIAS_C
#define INCLUDE_ALIAS_C

#include "config.h"
#include "nvram.h"

/* Aliased base functions */

int nvram_load(void) __attribute__ ((alias ("nvram_init")));
int nvram_loaddefault(void) __attribute__ ((alias ("true")));
char *_nvram_get(const char *key) __attribute__ ((alias ("nvram_get")));
int nvram_get_state(const char *key) __attribute__ ((alias ("nvram_get_int")));
int nvram_set_state(const char *key, const int val) __attribute__ ((alias ("nvram_set_int")));
int nvram_restore_default(void) __attribute__ ((alias ("nvram_reset")));
int nvram_upgrade(void* ptr) __attribute__ ((alias ("nvram_commit")));
int get_default_mac() __attribute__ ((alias ("true")));

//DIR-615I2, DIR-615I3, DIR-825C1 patch
int VCTGetPortAutoNegSetting(char *a1, int a2) __attribute__ ((alias ("false1")));
// netgear 'Rxxxx' series patch to prevent infinite loop in httpd
int agApi_fwGetFirstTriggerConf(char *a1) __attribute__ ((alias ("true1")));
// netgear 'Rxxxx' series patch to prevent infinite loop in httpd
int agApi_fwGetNextTriggerConf(char *a1)  __attribute__ ((alias ("true1")));

/* D-Link */
char *artblock_get(const char *key) __attribute__ ((alias ("nvram_get")));
char *artblock_fast_get(const char *key) __attribute__ ((alias ("nvram_safe_get")));
char *artblock_safe_get(const char *key) __attribute__ ((alias ("nvram_safe_get")));
int artblock_set(const char *key, const char *val) __attribute__ ((alias ("nvram_set")));
int nvram_flag_set(int unk) __attribute__ ((alias ("true")));
int nvram_flag_reset(int unk) __attribute__ ((alias ("true")));

/* D-Link ARM */
int nvram_master_init() __attribute__ ((alias ("false")));
int nvram_slave_init() __attribute__ ((alias ("false")));

/* Realtek */
// These functions expect integer keys, so we convert to string first.
// Unfortunately, this implementation is not entirely correct because some
// values are integers and others are string, but we treat all as integers.
int apmib_init() __attribute__ ((alias ("true")));
int apmib_reinit() __attribute__ ((alias ("true")));
// int apmib_hwconf() __attribute__ ((alias ("true")));
// int apmib_dsconf() __attribute__ ((alias ("true")));
// int apmib_load_hwconf() __attribute__ ((alias ("true")));
// int apmib_load_dsconf() __attribute__ ((alias ("true")));
// int apmib_load_csconf() __attribute__ ((alias ("true")));
int apmib_update(const int key) __attribute__((alias ("true")));

/* Netgear ACOS */

int WAN_ith_CONFIG_SET_AS_STR(const char *val, const char *fmt, ...) __attribute__ ((alias ("nvram_nset")));

int WAN_ith_CONFIG_SET_AS_INT(const int val, const char *fmt, ...) __attribute__ ((alias ("nvram_nset_int")));

int acos_nvram_init(void) __attribute__ ((alias ("nvram_init")));
char *acos_nvram_get(const char *key) __attribute__ ((alias ("nvram_get")));
int acos_nvram_read (const char *key, char *buf, size_t sz) __attribute__ ((alias ("nvram_get_buf")));
int acos_nvram_set(const char *key, const char *val) __attribute__ ((alias ("nvram_set")));
int acos_nvram_loaddefault(void) __attribute__ ((alias ("true")));
int acos_nvram_unset(const char *key) __attribute__ ((alias ("nvram_unset")));
int acos_nvram_commit(void) __attribute__ ((alias ("nvram_commit")));

int acosNvramConfig_init(char *mount) __attribute__ ((alias ("nvram_init")));
char *acosNvramConfig_get(const char *key) __attribute__ ((alias ("nvram_get")));
int acosNvramConfig_read (const char *key, char *buf, size_t sz) __attribute__ ((alias ("nvram_get_buf")));
int acosNvramConfig_set(const char *key, const char *val) __attribute__ ((alias ("nvram_set")));
int acosNvramConfig_write(const char *key, const char *val) __attribute__ ((alias ("nvram_set")));
int acosNvramConfig_unset(const char *key) __attribute__ ((alias ("nvram_unset")));
int acosNvramConfig_match(const char *key, const char *val) __attribute__ ((alias ("nvram_match")));
int acosNvramConfig_invmatch(const char *key, const char *val) __attribute__ ((alias ("nvram_invmatch")));
int acosNvramConfig_save(void) __attribute__ ((alias ("nvram_commit")));
int acosNvramConfig_save_config(void) __attribute__ ((alias ("nvram_commit")));
int acosNvramConfig_loadFactoryDefault(const char* key);

/* ZyXel / Edimax */
// many functions expect the opposite return values: (0) success, failure (1/-1)

int nvram_commit_adv(int) __attribute__ ((alias ("nvram_commit")));
int nvram_unlock_adv(int) __attribute__ ((alias ("true")));
int nvram_lock_adv(int) __attribute__ ((alias ("true")));
int nvram_check(void) __attribute__ ((alias ("true")));

int envram_get_func(const char* key, char *buf) __attribute__ ((alias ("envram_get")));
int nvram_getf(const char* key, const char *fmt, ...) __attribute__ ((alias ("envram_getf")));

int envram_set_func(const char *key, const char *val) __attribute__ ((alias ("envram_set")));

int nvram_setf(const char* key, const char* fmt, ...) __attribute__ ((alias ("envram_setf")));

int envram_unset_func(void) __attribute__ ((alias ("envram_unset")));

#endif
