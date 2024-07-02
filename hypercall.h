static inline void igloo_hypercall(unsigned long num, unsigned long arg1) {
#if defined(CONFIG_MIPS)
    register unsigned long a0 asm("a0") = num;
    register unsigned long a1 asm("a1") = arg1;

    asm volatile(
       "movz $0, $0, $0"
        : "+r"(a0)  // Input and output in R0
        : "r"(a1) // arg1 in register A1
        : "memory"
    );

#elif defined(CONFIG_AARCH64)
    register unsigned long long x0 asm("x0") = num;
    register unsigned long long x1 asm("x1") = arg1;

    asm volatile(
        "mov x0, %0 \t\n\
        mov x1, %1 \t\n\
        msr S0_0_c5_c0_0, xzr"
        :
        : "r"(x0), "r"(x1)
        : "memory"
    );
#elif defined(CONFIG_ARM)
  register uint32_t r0 asm("r0") = num;
  register uint32_t r1 asm("r1") = arg1;
  asm volatile(
     "mov r0, %0 \t\n\
      mov r1, %1 \t\n\
      mcr p7, 0, r0, c0, c0, 0"
      :
      : "r"(r0), "r"(r1)
      : "memory"
  );
#else
#error "No igloo_hypercall support for architecture"
#endif
}

static inline unsigned long igloo_hypercall2(unsigned long num, unsigned long arg1, unsigned long arg2) {
#if defined(CONFIG_ARM)
    register unsigned long r0 asm("r0") = num;
    register unsigned long r1 asm("r1") = arg1;
    register unsigned long r2 asm("r2") = arg2;

    asm volatile(
       "mcr p7, 0, r0, c0, c0, 0"
        : "+r"(r0)  // Input and output
        : "r"(r1), "r"(r2)
        : "memory"
    );

    return r0;
#elif defined(CONFIG_AARCH64)
    register unsigned long long x0 asm("x0") = num;
    register unsigned long long x1 asm("x1") = arg1;
    register unsigned long long x2 asm("x2") = arg2;

    asm volatile(
       "msr S0_0_c5_c0_0, xzr"
        : "+r"(x0)
        : "r"(x1), "r"(x2)
        : "memory"
    );

    return x0;
#elif defined(CONFIG_MIPS)
    register unsigned long a0 asm("a0") = num;
    register unsigned long a1 asm("a1") = arg1;
    register unsigned long a2 asm("a2") = arg2;

    asm volatile(
       "movz $0, $0, $0"
        : "+r"(a0)  // Input and output in R0
        : "r"(a1) , "r" (a2)// arg1 in register A1
        : "memory"
    );
    return a0;

#else
    #error "No igloo_hypercall2 support for architecture"
    return 0;
#endif
}