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
#include <thread.h>

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

static int end = 0;
static int drive_num = 0;

void shell(void) {
    int j = 0;
    while (j != 1) {
        kprintf("\n%s %s ring 0 shell\nBSD 3-Clause License\nCopyright (c) 2025, ALoutFER, Michael78Bugaev\n\n", KERNEL_NAME, KERNEL_VERSION);
        j = 1;
    }
    while (1) {
        kprintf("%d:> ", drive_num);
        char *buf = kgets();
        int count = 0;
        char **args = split(buf, ' ', &count);
        if (count > 0)
        {
            if (strcmp(args[0], "exit") == 0) {
                break;
            }
            else if (strcmp(args[0], "disk") == 0) {
                if (count == 2) {
                    drive_num = atoi(args[1]);
                }
                else if (count == 1) {
                    kprintf("%d\n", drive_num);
                }
                else kprintf("<(0C)>Usage: disk <drive_number><(07)>\n");
            }
            else if (strcmp(args[0], "help") == 0) {
                kprint("help command\n");
            }
            else if (strcmp(args[0], "clear") == 0) {
                kclear();
            }
            else if (strcmp(args[0], "lspid") == 0) {
                if (count == 1)
                {
                    for (int i = 0; i < thread_get_count(); ++i) {
                        thread_t* t = thread_get(i);
                        char state_str[10];
                        if (t->state == THREAD_READY) {
                            strcpy(state_str, "READY");
                        } else if (t->state == THREAD_RUNNING) {
                            strcpy(state_str, "RUNNING");
                        } else if (t->state == THREAD_BLOCKED) {
                            strcpy(state_str, "BLOCKED");
                        } else if (t->state == THREAD_TERMINATED) {
                            strcpy(state_str, "TERMINATED");
                        }
                        kprintf("[%d] %s, state: %s\n", t->tid, t->name, state_str);
                    }
                }
                else {
                    kprintf("<(0C)>Usage: lspid<(07)>\n");
                }
            }
            else if (strcmp(args[0], "stop") == 0) {
                if (count == 2) {
                    int pid = atoi(args[1]);
                    if (pid == 0) kdbg(KERR, "thread_stop: access denied\n");
                    else if (pid == thread_get_pid("Hatcher shell")) {
                        kdbg(KWARN, "thread_stop: stopping shell\n");
                        thread_stop(pid);
                        end = 1;
                    }
                    else {
                        thread_stop(pid);
                        kdbg(KINFO, "thread_stop: pid %d stopped\n", pid);
                    }
                }
                else kprintf("<(0C)>Usage: stop <pid><(07)>\n");
            }
            else if (strcmp(args[0], "block") == 0) {
                if (count == 2) {
                    int pid = atoi(args[1]);
                    if (pid == 0) kdbg(KERR, "thread_block: access denied\n");
                    else {
                        thread_block(pid);
                        kdbg(KINFO, "thread_block: pid %d blocked\n", pid);
                    }
                }
                else kprintf("<(0C)>Usage: block <pid><(07)>\n");
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

                    if (ata_write_sector(drive_num, lba, buffer) == 0) {
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
                            if (ata_read_sector(drive_num, start_lba + i, read_buffer + (i * 512)) != 0) {
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
                    if (ata_read_sector(drive_num, lba, buffer) == 0) {
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
            else if (strcmp(args[0], "yield") == 0) {
                thread_yield();
            }
            else {
                kprintf("<(0C)>%s?<(07)>\n", args[0]);
            }
        }
        thread_yield();
    }
}

// Это нужно для стабильности инициализации
void idle_irq1(cpu_registers_t* regs) {
    (void)inb(0x60);
}

void ui_bar()
{
    while (1) {//                                                                                      "
        vga_draw_text(" Hatcher |                                                    | CPU Usage:    % ", 0, 0, 0x70);
        thread_yield();
    }
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

    thread_init();
    
    __asm__("sti");


    pc_speaker_beep(400, 100);
    pc_speaker_beep(500, 100);
    pc_speaker_beep(700, 300);

    thread_create(shell, "Hatcher shell");

    while (end == 0);

    kdbg(KINFO, "kernel_main: kernel end");
    pc_speaker_beep(700, 100);
    pc_speaker_beep(500, 100);
    pc_speaker_beep(400, 300);
    __asm__("cli");
    for (;;) __asm__("hlt");
}