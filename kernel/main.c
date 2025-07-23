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
#include <ata.h>
#include <usb.h>

extern uint64_t timer_ticks;

//dec: 0123456789
//hex: 0123456789ABCDEF

static uint8_t hex_char_to_byte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

//from osdev
static void hexstr_to_bytes(const char* hex_str, uint8_t* byte_array, size_t max_bytes) {
    size_t len = strlen(hex_str);
    for (size_t i = 0; i < len / 2 && i < max_bytes; ++i) {
        uint8_t high_nibble = hex_char_to_byte(hex_str[i * 2]);
        uint8_t low_nibble = hex_char_to_byte(hex_str[i * 2 + 1]);
        byte_array[i] = (high_nibble << 4) | low_nibble;
    }
}

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
                    kprintf("<(0C)>Usage: beep <frequency> <duration><(07)>\n");
                }
            }
            else if (strcmp(args[0], "write") == 0) {
                if (count == 3) {
                    uint32_t lba = atoi(args[1]);
                    const char* hex_data_str = args[2];
                    uint8_t buffer[512]; // Сектор 512 байт
                    memset(buffer, 0, 512); // Очищаем буфер

                    hexstr_to_bytes(hex_data_str, buffer, 512);

                    if (ata_write_sector(0, lba, buffer) == 0) {
                        kprintf("Sector %u written successfully.\n", lba);
                    } else {
                        kprintf("Error writing sector %u.\n", lba);
                    }
                } else {
                    kprintf("<(0C)>Usage: write <lba> <hex_data><(07)>\n");
                }
            }
            else if (strcmp(args[0], "read") == 0) {
                if (count == 4 && strcmp(args[1], "-l") == 0) { // read -l <bytes> <lba>
                    uint32_t size_to_read = atoi(args[2]);
                    uint32_t start_lba = atoi(args[3]);

                    #define MAX_READ_BUFFER_SIZE 4096
                    uint8_t read_buffer[MAX_READ_BUFFER_SIZE];

                    if (size_to_read == 0) {
                        kprintf("size to read cannot be zero.\n");
                    } else if (size_to_read > MAX_READ_BUFFER_SIZE) {
                        kprintf("requested size %u exceeds max buffer size %u. please request less data.\n", size_to_read, MAX_READ_BUFFER_SIZE);
                    } else {
                        uint32_t sectors_to_read = (size_to_read + 511) / 512;
                        bool read_success = true;
                        for (uint32_t i = 0; i < sectors_to_read; ++i) {
                            if (ata_read_sector(0, start_lba + i, read_buffer + (i * 512)) != 0) {
                                kprintf("error reading sector %u.\n", start_lba + i);
                                read_success = false;
                                break;
                            }
                        }
                        
                        if (read_success) {
                            kprintf("data from lba %u (first %u bytes):\n", start_lba, size_to_read);
                            for (uint32_t i = 0; i < size_to_read; ++i) {
                                kprintf("%02X ", read_buffer[i]);
                                if ((i + 1) % 16 == 0) {
                                    kprintf("\n");
                                }
                            }
                            if (size_to_read % 16 != 0) {
                                kprintf("\n");
                            }
                        }
                    }
                } else if (count == 2) {
                    uint32_t lba = atoi(args[1]);
                    uint8_t buffer[512];
                    if (ata_read_sector(0, lba, buffer) == 0) {
                        kprintf("sector %u (512 bytes):\n", lba);
                        for (int i = 0; i < 512; ++i) {
                            kprintf("%02X ", buffer[i]);
                            if ((i + 1) % 16 == 0) {
                                kprintf("\n");
                            }
                        }
                        kprintf("\n");
                    } else {
                        kprintf("error reading sector %u.\n", lba);
                    }
                }
                else {
                    kprintf("<(0C)>Usage: read <lba> OR read -l <bytes> <lba><(07)>\n");
                }
            }
            else {
                kprintf("<(0C)>Incorrect command: %s<(07)>\n", args[0]);
            }
        }
    }
}

// Это нужно для стабильности инициализации
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
    __asm__("sti");
    
    pc_speaker_beep(400, 100);
    pc_speaker_beep(500, 100);
    pc_speaker_beep(700, 300);


    shell();

    kprintf("Kernel ended.");
    pc_speaker_beep(700, 100);
    pc_speaker_beep(500, 100);
    pc_speaker_beep(400, 300);
    for (;;);
}