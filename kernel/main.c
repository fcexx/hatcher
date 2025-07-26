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
#include <gpu.h>
#include <ata.h>
#include <usb.h>
#include <thread.h>
#include <spinlock.h>

extern uint32_t timer_ticks;

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

static uint32_t user_time = 0; //Время пользовательских потоков
static uint32_t system_time = 0; //Время системных операций
static uint32_t irq_time = 0; //Время обработки прерываний
static uint32_t idle_time = 0; //Время простоя
static uint32_t last_cpu_update = 0;
static uint64_t sys_seconds = 0;
static uint64_t sys_minutes = 0;
static uint64_t sys_hours = 0;

static int calculate_cpu_usage() {
    uint32_t current_ticks = timer_ticks;
    uint32_t delta_ticks = current_ticks - last_cpu_update;
    
    if (delta_ticks == 0) return 0;
 
    thread_t* current = thread_current();
    
    if (current && strcmp(current->name, "idle") == 0) {
        idle_time += delta_ticks;
    } else if (current && strcmp(current->name, "Hatcher shell") == 0) {
        user_time += delta_ticks;
    } else if (current && strcmp(current->name, "systime") == 0) {
        system_time += delta_ticks;
    } else {
        system_time += delta_ticks;
    }
    
    uint32_t total_active_time = user_time + system_time + irq_time;
    
    uint32_t total_cpu_time = total_active_time + idle_time;
    
    int cpu_percent = 0;
    if (total_cpu_time > 0) {
        cpu_percent = (total_active_time * 100) / total_cpu_time;
    }
    
    if (total_cpu_time > 100) {
        user_time = 0;
        system_time = 0;
        irq_time = 0;
        idle_time = 0;
    }
    
    last_cpu_update = current_ticks;
    return cpu_percent;
}

void sys_time() {
    char time_str[10];
    while (1) {
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", sys_hours, sys_minutes, sys_seconds);
        vga_draw_text(time_str, 12, 0, 0x70);
        thread_yield();
    }
}

void calc_time()
{
    while (1) {
        sys_seconds++;
        if (sys_seconds == 60) {
            sys_seconds = 0;
            sys_minutes++;
        }
        if (sys_minutes == 60) {
            sys_minutes = 0;
            sys_hours++;
        }
        if (sys_hours == 24) {
            sys_hours = 0;
        }
        thread_sleep(1000);
    }
}

void shell(void) {
    kprintf("\n\n");
    while (1) {
        kprintf("%d:> ", drive_num);
        char *buf = kgets();
        int count = 0;
        char **args = split(buf, ' ', &count);
        if (count > 0)
        {
            if (strcmp(args[0], "exit") == 0) {
                end = 1;
                //break;
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
            else if (strcmp(args[0], "info") == 0) {
                kprintf("%s %s Operating System\nBSD 3-Clause License\nCopyright (c) 2025, ALoutFER, fcexx\n", KERNEL_NAME, KERNEL_VERSION);
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
                        } else if (t->state == THREAD_SLEEPING) {
                            strcpy(state_str, "SLEEPING");
                        }
                        kprintf("[%d] %s, state: %s", t->tid, t->name, state_str);
                        if (t->state == THREAD_SLEEPING) {
                            kprintf(" (wake at tick %u)", t->sleep_until);
                        }
                        kprintf("\n");
                    }
                }
                else {
                    kprintf("<(0C)>Usage: lspid<(07)>\n");
                }
            }
            else if (strcmp(args[0], "sleep") == 0) {
                if (count == 2) {
                    uint32_t ms = atoi(args[1]);
                    kprintf("Sleeping for %u ms...\n", ms);
                    thread_sleep(ms);
                    kprintf("Woke up after %u ms!\n", ms);
                }
                else {
                    kprintf("<(0C)>Usage: sleep <milliseconds><(07)>\n");
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

void idle_irq1(cpu_registers_t* regs) {
    (void)inb(0x60);
}

void ui_bar()
{
    while (1) {
        vga_draw_text(" Hatcher |                                                    | CPU Usage:    % ", 0, 0, 0x70);

        int cpu_percent = calculate_cpu_usage();
        kprintci_vidmem(cpu_percent, 0x70, 0 * MAX_ROWS + 75 * 2);
        
        
        thread_sleep(5);
        
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
    thread_create(shell, "Hatcher shell");
    
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