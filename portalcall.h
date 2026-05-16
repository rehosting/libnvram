#ifndef LIBNVRAM_PORTALCALL_H
#define LIBNVRAM_PORTALCALL_H

#include <stdint.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#if __SIZEOF_POINTER__ == 8
#define PORTAL_MAGIC ((unsigned long)0xc1d1e1f1)
#else
#define PORTAL_MAGIC ((unsigned int)0xc1d1e1f1)
#endif

static inline unsigned long portal_call(unsigned long user_magic, int argc,
                                        const uint64_t *args)
{
    return (unsigned long)syscall(SYS_sendto, PORTAL_MAGIC, user_magic, argc, args, 0, 0);
}

static inline unsigned long portal_call2(unsigned long user_magic, uint64_t a1,
                                         uint64_t a2)
{
    uint64_t args[2] = { a1, a2 };
    return portal_call(user_magic, 2, args);
}

#endif
