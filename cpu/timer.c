#include <stdint.h>
#include <port_based.h>
#include <cpu.h>
#include <string.h>
#include <vga.h>
#include <debug.h>
#include <pic.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21

#define PIT_CH0 0x40
#define PIT_CMD 0x43

// Счетчик тиков
volatile uint32_t timer_ticks = 0;

/*void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}*/

void remap_pic() {
    outb(PIC1_CMD, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC1_DATA, 0x04);
    outb(PIC1_DATA, 0x01);
    outb(PIC1_DATA, 0xFE);
}

void set_pit_frequency(uint16_t hz) {
    uint16_t divisor = 1193180 / hz;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
}

void timer_handler() {
    timer_ticks++;
}

void timer_isr_wrapper(cpu_registers_t* regs) {
	(void)regs;
	timer_handler();
    pic_send_eoi(0);
} 

void init_timer() {
    set_pit_frequency(1000);
}

void enable_interrupts() {
    __asm__ volatile ("sti");
}