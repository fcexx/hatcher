#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>
#include <cpu.h>

extern volatile uint32_t timer_tcks;

void init_timer();
void timer_handler();
void set_pic_frequency(uint16_t hz);

void timer_isr_wrapper(cpu_registers_t* regs);
#endif
