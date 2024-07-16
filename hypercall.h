static inline void igloo_hypercall(unsigned long num, unsigned long arg1) {
#if defined(CONFIG_MIPS)
    register unsigned long v0 asm("v0") = num;
    register unsigned long a0 asm("a0") = arg1;

    asm volatile(
       "movz $0, $0, $0"
        : "+r"(v0)
        : "r"(a0) 
        : "memory"
    );

#elif defined(CONFIG_AARCH64)
    register unsigned long long x8 asm("x8") = num;
    register unsigned long long x0 asm("x0") = arg1;

    asm volatile(
        "mov x8, %0 \t\n\
        mov x0, %1 \t\n\
        msr S0_0_c5_c0_0, xzr"
        :
        : "r"(x8), "r"(x0)
        : "memory"
    );
#elif defined(CONFIG_ARM)
  register uint32_t r7 asm("r7") = num;
  register uint32_t r0 asm("r0") = arg1;
  asm volatile(
     "mov r7, %0 \t\n\
      mov r0, %1 \t\n\
      mcr p7, 0, r0, c0, c0, 0"
      :
      : "r"(r7), "r"(r0)
      : "memory"
  );
#else
#error "No igloo_hypercall support for architecture"
#endif
}

static inline unsigned long igloo_hypercall2(unsigned long num, unsigned long arg1, unsigned long arg2) {
#if defined(CONFIG_ARM)
    register unsigned long r7 asm("r7") = num;
    register unsigned long r0 asm("r0") = arg1;
    register unsigned long r1 asm("r1") = arg2;

    asm volatile(
       "mcr p7, 0, r0, c0, c0, 0"
        : "+r"(r0)  // Input and output
        : "r"(r7), "r"(r1)
        : "memory"
    );

    return r0;
#elif defined(CONFIG_AARCH64)
    register unsigned long long x8 asm("x8") = num;
    register unsigned long long x0 asm("x0") = arg1;
    register unsigned long long x1 asm("x1") = arg2;

    asm volatile(
       "msr S0_0_c5_c0_0, xzr"
        : "+r"(x0)
        : "r"(x8), "r"(x1)
        : "memory"
    );

    return x0;
#elif defined(CONFIG_MIPS)
    register unsigned long v0 asm("v0") = num;
    register unsigned long a0 asm("a0") = arg1;
    register unsigned long a1 asm("a1") = arg2;

    asm volatile(
       "movz $0, $0, $0"
        : "+r"(v0) 
        : "r"(a0) , "r" (a1)
        : "memory"
    );
    return v0;

#else
    #error "No igloo_hypercall2 support for architecture"
    return 0;
#endif
}