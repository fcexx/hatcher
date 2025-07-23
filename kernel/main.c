#include <gdt.h>
#include <vga.h>
#include <paging.h>
#include <idt.h>
#include <hatcher.h>
#include <pic.h>
#include <string.h>
#include <cpu.h>
#include <heap.h>
#include <debug.h>
#include <ps2.h>
#include <timer.h>
#include <pc_speaker.h>
#include <pci.h>
#include <ata.h>
#include <usb.h>

extern uint64_t timer_ticks;

void shell(void) {
    kprintf("%s %s ring 0 shell\nBSD 3-Clause License\nCopyright (c) 2025, ALoutFER, Michael78Bugaev\n", KERNEL_NAME, KERNEL_VERSION);
    while (1) {
        kprintf("> ");
        char *buf = kgets();
        int count = 0;
        char **args = split(buf, ' ', &count);
        if (count > 0)
        {
            if (strcmp(args[0], "exit") == 0) {
                break;
            }
            else if (strcmp(args[0], "help") == 0) {
                kprint("help command\n");
            }
            else if (strcmp(args[0], "clear") == 0) {
                kclear();
            }
            else if (strcmp(args[0], "beep") == 0) {
                if (count > 2)
                {
                    int freq = atoi(args[1]);
                    int duration = atoi(args[2]);
                    pc_speaker_beep(freq, duration);
                }
                else {
                    kprintf("Usage: beep <frequency> <duration>\n");
                }
            }
            else {
                kprintf("<(0C)>Incorrect command: %s<(07)>\n", args[0]);
            }
        }
    }
}

void kernel_main(uint32_t magic, uint32_t addr)
{
    __asm__("cli");
    gdt_init();
    kprintf("\n<(0F)>%s %s Operating System\n\n", KERNEL_FNAME, KERNEL_VERSION);
    gdt_print_gdt();
    idt_init();
    kdbg(KINFO, "kernel_main: base success\n");
    kdbg(KINFO, "pic_remap: remapping 0x20, 0x28\n");
    pic_remap(0x20, 0x28);
    paging_init(); // Moved here

    pci_init();

    kdbg(KWARN, "pic_set_mask: masking all irqs (some devices may not work eg. mouse)\n");
    for (uint8_t irq = 0; irq < 16; ++irq) {
        pic_set_mask(irq);
    }
    kdbg(KINFO, "pic_clear_mask: enabling irq0, irq1\n");
    pic_clear_mask(0); 
    pic_clear_mask(1); 
    
    init_timer(); 
    idt_register_handler(0x20, timer_isr_wrapper); 
    ata_init();

    heap_init(0x200000, 0x1000000); // start at 2MB, size 16MB
    kdbg(KINFO, "heap_init: initialized at 0x200000, size 16MB\n");
    __asm__("sti");
    
    pc_speaker_beep(1000, 200);

    shell();

    kprintf("Kernel ended.");
    for (;;);
}
