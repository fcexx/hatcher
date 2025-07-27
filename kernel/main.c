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
#include <stdbool.h>
#include <timer.h>
#include <pc_speaker.h>
#include <pci.h>
#include <guitasks.h>
#include <gpu.h>
#include <fat32.h>
#include <ata.h>
#include <usb.h>
#include <thread.h>
#include <spinlock.h>
#include <kernutils.h>
#include <sys.h>

extern uint32_t timer_ticks;

int end = 0;
int drive_num = 0;

//dec: 0123456789
//hex: 0123456789ABCDEF

void idle_irq1(cpu_registers_t* regs) {
    (void)inb(0x60);
}

void kernel_main(uint32_t magic, uint32_t addr)
{
    gdt_init();
    kprintf("\n<(0F)>%s %s Operating System\n\n", KERNEL_FNAME, KERNEL_VERSION);
    gdt_print_gdt();
    idt_init();
    kdbg(KINFO, "kernel_main: base success\n");
    kdbg(KINFO, "pic_remap: remapping 0x20, 0x28\n");
    pic_remap(0x20, 0x28);
    paging_init();

    pci_init();

    kdbg(KWARN, "pic_set_mask: masking all irqs (some devices may not work eg. mouse)\n");
    for (uint8_t irq = 0; irq < 16; ++irq) {
        pic_set_mask(irq);
    }
    kdbg(KINFO, "pic_clear_mask: enabling irq0, irq1\n");
    pic_clear_mask(0); 
    pic_clear_mask(1); 
    
    init_timer(); 
    idt_register_handler(0x21, idle_irq1);
    idt_register_handler(0x20, timer_isr_wrapper); 
    ata_init();

    heap_init(0x200000, 0x1000000); // start at 2MB, size 16MB
    kdbg(KINFO, "heap_init: initialized at 0x200000, size 16MB\n");

    fat32_mount(0);
    fat32_mount(1);

    gpu_info_t gpu;
    if (gpu_init(&gpu) == 0) {
        gpu_print_info(&gpu);
        uint32_t reg = gpu_mmio_read32(gpu.mmio_base, 0x0);
        gpu_mmio_write32(gpu.mmio_base, 0x4, 0x12345678);
    }
    else {
        kdbg(KWARN, "gpu_get_info: failed to get gpu info\n");
    }

    thread_init();
    thread_create(ui_bar, "hatchui");
    thread_create(calc_time, "calctime");
    thread_create(sys_time, "systime");
    thread_create(shell, "shell");
    
    __asm__("sti");


    pc_speaker_beep(400, 100);
    pc_speaker_beep(500, 100);
    pc_speaker_beep(700, 300);


    while (end != 1);
    for (int i = 0; i < thread_get_count(); i++) {
        thread_stop(thread_get(i)->tid);
    }

    kdbg(KWARN, "kernel_main: kernel end");
    pc_speaker_beep(700, 100);
    pc_speaker_beep(500, 100);
    pc_speaker_beep(400, 300);
    __asm__("cli");
    for (;;) __asm__("hlt");
}
