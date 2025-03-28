static inline void igloo_hypercall(unsigned long num, unsigned long arg1) {
#if defined(CONFIG_MIPS)
    register unsigned long reg0 asm("v0") = num;
    register unsigned long reg1 asm("a0") = arg1;

    asm volatile(
       "movz $0, $0, $0"
        : "+r"(reg0)
        : "r"(reg1) // num in register v0
        : "memory"
    );


#elif defined(CONFIG_AARCH64)
    register unsigned long reg0 asm("x8") = num;
    register unsigned long reg1 asm("x0") = arg1;
    asm volatile(
            "msr S0_0_c5_c0_0, xzr \n"
            : "+r"(reg1)
            : "r"(reg0)
            : "memory"
        );
#elif defined(CONFIG_ARM) 
    register unsigned long reg0 asm("r7") = num;
    register unsigned long reg1 asm("r0") = arg1;

    asm volatile(
    "mcr p7, 0, r0, c0, c0, 0"
      : "+r"(reg1)
      : "r"(reg0)
      : "memory"
  );
#elif defined(CONFIG_X86_64)
    register unsigned long reg0 asm("rax") = num;
    register unsigned long reg1 asm("rdi") = arg1;

    asm volatile(
        "cpuid"
        : "+r"(reg0)           // hypercall num + return value in rax
        : "r"(reg1)            // arguments
        : "memory", "rbx", "rcx", "rdx"
    );
#elif defined(CONFIG_I386)
    register unsigned long reg0 asm("eax") = num;
    register unsigned long reg1 asm("edi") = arg1;

    asm volatile(
        "cpuid"
        : "+r"(reg0)           // hypercall num + return value in rax
        : "r"(reg1)            // arguments
        : "memory", "ebx", "ecx", "edx"
    );

#elif defined(CONFIG_LOONGARCH) || defined(CONFIG_LOONGARCH64)
	register unsigned long reg0 asm("a7") = num;
	register unsigned long reg1 asm("a0") = arg1;
    
    asm volatile(
        "cpucfg $r0, $r0"
        : "+r"(reg0)
        : "r"(reg1)
        : "memory"  // No clobber
    );
#elif defined(CONFIG_PPC) || defined(CONFIG_PPC64)
	register unsigned long reg0 asm("r4") = num;
	register unsigned long reg1 asm("r5") = arg1;

    asm volatile(
        "xori 10, 10, 0"
        : "+r"(reg0) 
        : "r"(reg1)
        : "memory"
    );
#elif defined(CONFIG_RISCV) || defined(CONFIG_RISCV64)
	register unsigned long reg0 asm("a0") = num;
	register unsigned long reg1 asm("a1") = arg1;

    asm volatile(
        "xori x0, x0, 0"
        : "+r"(reg0)
        : "r"(reg1)
        : "memory"
    );
#else
#error "No igloo_hypercall support for architecture"
#endif
}

static inline unsigned long igloo_hypercall2(unsigned long num, unsigned long arg1, unsigned long arg2) {
#if defined(CONFIG_AARCH64)
    register unsigned long reg0 asm("x8") = num;
    register unsigned long reg1 asm("x0") = arg1;
    register unsigned long reg2 asm("x1") = arg2;
    asm volatile(
       "msr S0_0_c5_c0_0, xzr \n"
        : "+r"(reg1)  // Input and output
        : "r"(reg0), "r"(reg2)
        : "memory"
    );
    return reg1;
#elif defined(CONFIG_ARM)
    register unsigned long reg0 asm("r7") = num;
    register unsigned long reg1 asm("r0") = arg1;
    register unsigned long reg2 asm("r1") = arg2;

    asm volatile(
       "mcr p7, 0, r0, c0, c0, 0"
        : "+r"(reg1)  // Input and output
        : "r"(reg0), "r"(reg2)
        : "memory"
    );

    return reg1;

#elif defined(CONFIG_MIPS)
    register unsigned long reg0 asm("v0") = num;
    register unsigned long reg1 asm("a0") = arg1;
    register unsigned long reg2 asm("a1") = arg2;

    asm volatile(
       "movz $0, $0, $0"
        : "+r"(reg0)  // Input and output in R0
        : "r"(reg1) , "r" (reg2)// arg2 in register A1
        : "memory"
    );
    return reg0;
#elif defined(CONFIG_X86_64)
    register unsigned long reg0 asm("rax") = num;
    register unsigned long reg1 asm("rdi") = arg1;
    register unsigned long reg2 asm("rsi") = arg2;

    asm volatile(
        "cpuid"
        : "+r"(reg0)           // hypercall num + return value in rax
        : "r"(reg1), "r"(reg2) // arguments
        : "memory", "rbx", "rcx", "rdx"
    );

    return reg0;
#elif defined(CONFIG_I386)
    register unsigned long reg0 asm("eax") = num;
    register unsigned long reg1 asm("edi") = arg1;
    register unsigned long reg2 asm("esi") = arg2;

    asm volatile(
        "cpuid"
        : "+r"(reg0)           // hypercall num + return value in rax
        : "r"(reg1), "r"(reg2) // arguments
        : "memory", "ebx", "ecx", "edx"
    );

    return reg0;
#elif defined(CONFIG_LOONGARCH) || defined(CONFIG_LOONGARCH64)
	register unsigned long reg0 asm("a7") = num;
	register unsigned long reg1 asm("a0") = arg1;
	register unsigned long reg2 asm("a1") = arg2;
    
    asm volatile(
        "cpucfg $r0, $r0"
        : "+r"(reg0)
        : "r"(reg1), "r"(reg2)
        : "memory"  // No clobber
    );
    return reg0;
#elif defined(CONFIG_PPC) || defined(CONFIG_PPC64)
	register unsigned long reg0 asm("r4") = num;
	register unsigned long reg1 asm("r5") = arg1;
	register unsigned long reg2 asm("r6") = arg2;

    asm volatile(
        "xori 10, 10, 0"
        : "+r"(reg0)
        : "r"(reg1), "r" (reg2) 
        : "memory"
    );
    return reg0;
#elif defined(CONFIG_RISCV) || defined(CONFIG_RISCV64)
	register unsigned long reg0 asm("a0") = num;
	register unsigned long reg1 asm("a1") = arg1;
	register unsigned long reg2 asm("a2") = arg2;

    asm volatile(
        "xori x0, x0, 0"
        : "+r"(reg0)
        : "r"(reg1), "r" (reg2) 
        : "memory"
    );
    return reg0;
#else
#error "No igloo_hypercall2 support for architecture"
#endif
}
