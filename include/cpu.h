#ifndef _CPU_H
#define _CPU_H

#include <stdint.h>

typedef struct {
    uint64_t int_no;
    uint64_t err_code;
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;

    uint64_t rip, cs, rflags, rsp, ss;
} cpu_registers_t;

static inline uint64_t get_cr3() {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r" (value));
    return value;
}

static inline void set_cr3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r" (value));
}

#endif // _CPU_H