#ifndef INCLUDE_CONFIG_H
#define INCLUDE_CONFIG_H

// Determines whether debugging information should be printed to stderr.
#define DEBUG               0
// Determines the size of the internal buffer, used for manipulating and storing key values, etc.
#define BUFFER_SIZE         1024
// Determines the size of the "emulated" NVRAM, used by nvram_get_nvramspace().
#define NVRAM_SIZE          2048
// Determines the maximum size of the user-supplied output buffer when a length is not supplied.
#define USER_BUFFER_SIZE    64
// Determines the unique separator character (as string) used for the list implementation. Do not use "\0".
#define LIST_SEP            "\xff"
// Special argument used to change the semantics of the nvram_list_exist() function.
#define LIST_MAGIC          0xdeadbeef
// Identifier value used to generate IPC key in ftok()
#define IPC_KEY             'A'
// Timeout for the semaphore
#define IPC_TIMEOUT         1000
// Mount point of the base NVRAM implementation.
#define MOUNT_POINT         "/igloo/libnvram_tmpfs/"

// Define the semantics for success and failure error codes.
#define E_FAILURE  0
#define E_SUCCESS  1

#endif
