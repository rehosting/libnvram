string_introspection = {
    "strcmp": "libinject_strcmp",
    "strncmp": "libinject_strncmp",
    "getenv": "libinject_getenv",
    "strstr": "libinject_strstr"
}


atheros_broadcom = {
    "nvram_get_nvramspace": "libinject_nvram_get_nvramspace",
    "nvram_nget": "libinject_nvram_nget",
    "nvram_nset": "libinject_nvram_nset",
    "nvram_nset_int": "libinject_nvram_nset_int",
    "nvram_nmatch": "libinject_nvram_nmatch"
}

realtek = {
    "apmib_get": "libinject_apmib_get",
    "apmib_set": "libinject_apmib_set"
}

netgear_acos = {
    "WAN_ith_CONFIG_GET": "libinject_WAN_ith_CONFIG_GET"
}

zyxel_or_edimax = {
    "nvram_getall_adv": "libinject_nvram_getall_adv",
    "nvram_get_adv": "libinject_nvram_get_adv",
    "nvram_set_adv": "libinject_nvram_set_adv",
    "nvram_state": "libinject_nvram_state",
    "envram_commit": "libinject_envram_commit",
    "envram_default": "libinject_envram_default",
    "envram_load": "libinject_envram_load",
    "envram_safe_load": "libinject_envram_safe_load",
    "envram_match": "libinject_envram_match",
    "envram_get": "libinject_envram_get",
    "envram_getf": "libinject_envram_getf",
    "envram_set": "libinject_envram_set",
    "envram_setf": "libinject_envram_setf",
    "envram_unset": "libinject_envram_unset"
}

ralink = {
    "nvram_bufget": "libinject_nvram_bufget",
    "nvram_bufset": "libinject_nvram_bufset"
}


# One to one mappings of orig fn to shim
base_names = {
    "nvram_init": "libinject_nvram_init",
    "nvram_reset": "libinject_nvram_reset",
    "nvram_clear": "libinject_nvram_clear",
    "nvram_close": "libinject_nvram_close",
    "nvram_commit": "libinject_nvram_commit",
    "nvram_get": "libinject_nvram_get",
    "nvram_safe_get": "libinject_nvram_safe_get",
    "nvram_default_get": "libinject_nvram_default_get",
    "nvram_get_buf": "libinject_nvram_get_buf",
    "nvram_get_int": "libinject_nvram_get_int",
    "nvram_getall": "libinject_nvram_getall",
    "nvram_set": "libinject_nvram_set",
    "nvram_set_int": "libinject_nvram_set_int",
    "nvram_unset": "libinject_nvram_unset",
    "nvram_safe_unset": "libinject_nvram_safe_unset",
    "nvram_list_add": "libinject_nvram_list_add",
    "nvram_list_exist": "libinject_nvram_list_exist",
    "nvram_list_del": "libinject_nvram_list_del",
    "nvram_match": "libinject_nvram_match",
    "nvram_invmatch": "libinject_nvram_invmatch",
    "nvram_parse_nvram_from_file": "libinject_parse_nvram_from_file",
}

# Alternative names for the same function
base_aliases = {
    "nvram_load": "libinject_nvram_init",
    "nvram_loaddefault": "libinject_ret_1",
    "_nvram_get": "libinject_nvram_get",
    "nvram_get_state": "libinject_nvram_get_int",
    "nvram_set_state": "libinject_nvram_set_int",
    "nvram_restore_default": "libinject_nvram_reset",
    "nvram_upgrade": "libinject_nvram_commit",
    "get_default_mac": "libinject_ret_1",
    "VCTGetPortAutoNegSetting": "libinject_ret_0_arg",
    "agApi_fwGetFirstTriggerConf": "libinject_ret_1_arg",
    "agApi_fwGetNextTriggerConf": "libinject_ret_1_arg",
    "artblock_get": "libinject_nvram_get",
    "artblock_fast_get": "libinject_nvram_safe_get",
    "artblock_safe_get": "libinject_nvram_safe_get",
    "artblock_set": "libinject_nvram_set",
    "nvram_flag_set": "libinject_ret_1",
    "nvram_flag_reset": "libinject_ret_1",
    "nvram_master_init": "libinject_ret_0",
    "nvram_slave_init": "libinject_ret_0",
    "apmib_init": "libinject_ret_1",
    "apmib_reinit": "libinject_ret_1",
    "apmib_update": "libinject_ret_1",
    "WAN_ith_CONFIG_SET_AS_STR": "libinject_nvram_nset",
    "WAN_ith_CONFIG_SET_AS_INT": "libinject_nvram_nset_int",
    "acos_nvram_init": "libinject_nvram_init",
    "acos_nvram_get": "libinject_nvram_get",
    "acos_nvram_read": "libinject_nvram_get_buf",
    "acos_nvram_set": "libinject_nvram_set",
    "acos_nvram_loaddefault": "libinject_ret_1",
    "acos_nvram_unset": "libinject_nvram_unset",
    "acos_nvram_commit": "libinject_nvram_commit",
    "acosNvramConfig_init": "libinject_nvram_init",
    "acosNvramConfig_get": "libinject_nvram_get",
    "acosNvramConfig_read": "libinject_nvram_get_buf",
    "acosNvramConfig_set": "libinject_nvram_set",
    "acosNvramConfig_write": "libinject_nvram_set",
    "acosNvramConfig_unset": "libinject_nvram_unset",
    "acosNvramConfig_match": "libinject_nvram_match",
    "acosNvramConfig_invmatch": "libinject_nvram_invmatch",
    "acosNvramConfig_save": "libinject_nvram_commit",
    "acosNvramConfig_save_config": "libinject_nvram_commit",
    "acosNvramConfig_loadFactoryDefault": "libinject_ret_1",
    "nvram_commit_adv": "libinject_nvram_commit",
    "nvram_unlock_adv": "libinject_ret_1",
    "nvram_lock_adv": "libinject_ret_1",
    "nvram_check": "libinject_ret_1",
    "envram_get_func": "libinject_envram_get",
    "nvram_getf": "libinject_envram_getf",
    "envram_set_func": "libinject_envram_set",
    "nvram_setf": "libinject_envram_setf",
    "envram_unset_func": "libinject_envram_unset",
}

# All variables together
default_lib_aliases = {
    string_introspection,
    atheros_broadcom,
    realtek,
    netgear_acos,
    zyxel_or_edimax,
    ralink,
    base_names,
    base_aliases
}
