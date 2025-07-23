#include <gdt.h>
#include <vga.h>
#include <paging.h>
#include <idt.h>
#include <entix.h>
#include <pic.h>
#include <string.h>
#include <cpu.h>
#include <heap.h>
#include <debug.h>
#include <ps2.h>
#include <timer.h> // Added for timer functions
#include <pci.h>
#include <ata.h>
#include <usb.h>

extern uint64_t timer_ticks;

void shell(void) {
    kprintf("%s %s ring 0 shell\nBSD 3-Clause License\nCopyright (c) 2025, ALoutFER, Michael78Bugaev\n", KERNEL_NAME, KERNEL_VERSION);
    while (1) {
        kprintf("> ");
        char *buf = kgets();
        if (strcmp(buf, "exit") == 0) {
            break;
        }
        else if (strcmp(buf, "help") == 0) {
            kprint("help command\n");
        }
        else if (strcmp(buf, "") == 0);
        else {
            kprintf("<(0C)>Incorrect command: %s<(07)>\n", buf);
        }
    }
}

void kernel_main(uint32_t magic, uint32_t addr)
{
    __asm__("cli");
    gdt_init();
    kprintf("\n<(0F)>%s %s Operating System\n\n", KERNEL_FNAME, KERNEL_VERSION);
    idt_init();
    kdbg(KINFO, "kernel_main: base success\n");
    kdbg(KINFO, "pic_remap: remapping 0x20, 0x28\n");
    pic_remap(0x20, 0x28);
    paging_init(); // Moved here

    pci_init();

    kdbg(KINFO, "pic_set_mask: masking all IRQs by default\n");
    for (uint8_t irq = 0; irq < 16; ++irq) {
        pic_set_mask(irq);
    }
    kdbg(KINFO, "pic_clear_mask: enabling IRQ0 (timer) and IRQ1 (keyboard)\n");
    pic_clear_mask(0); 
    pic_clear_mask(1); 
    
    init_timer(); 
    idt_register_handler(0x20, timer_isr_wrapper); 
    ata_init();
    usb_init();

    // Initialize heap
    heap_init(0x200000, 0x1000000); // Start at 2MB, size 16MB
    kdbg(KINFO, "heap_init: initialized at 0x200000, size 16MB\n");
    __asm__("sti");

    uint8_t buf[512];
    ata_read_sector(0, 0, buf);
    for (int i = 0; i < 128; i++) {
        if (i % 16 == 0) {
            if (i > 0) {
                kprintf("  ");
                for (int j = i - 16; j < i; j++) {
                    if (buf[j] >= 32 && buf[j] <= 126)
                        kprintf("%c", buf[j]);
                    else
                        kprintf(".");
                }
            }
            kprintf("\n");
        }
        kprintf("%02x ", buf[i]);
    }
    // Print ASCII for last line
    int remaining = 128 % 16;
    if (remaining == 0) remaining = 16;
    for (int i = 0; i < (16 - remaining) * 3; i++)
        kprintf(" ");
    kprintf("  ");
    for (int i = 128 - remaining; i < 128; i++) {
        if (buf[i] >= 32 && buf[i] <= 126)
            kprintf("%c", buf[i]);
        else
            kprintf(".");
    }
    kprintf("\n");
    
    shell();

    kprintf("Kernel ended.");
    for (;;);
}
