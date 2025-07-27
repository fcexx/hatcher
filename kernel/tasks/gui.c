#include <hatcher.h>
#include <vga.h>
#include <gpu.h>
#include <thread.h>
#include <debug.h>
#include <string.h>
#include <stdbool.h>
#include <timer.h>
#include <ps2.h>

#include <guitasks.h>
#include <cpu.h>
#include <gdt.h>

extern uint64_t timer_ticks;
uint32_t system_time = 0; //Время системных операций
uint32_t irq_time = 0; //Время обработки прерываний
uint32_t idle_time = 0; //Время простоя
uint32_t last_cpu_update = 0;
uint64_t sys_seconds = 0;
uint64_t sys_minutes = 0;
uint64_t sys_hours = 0;
uint32_t user_time = 0;

static int calculate_cpu_usage() {
    uint32_t current_ticks = timer_ticks;
    uint32_t delta_ticks = current_ticks - last_cpu_update;
    
    if (delta_ticks == 0) return 0;
 
    thread_t* current = thread_current();
    
    if (current && strcmp(current->name, "idle") == 0) {
        idle_time += delta_ticks;
    } else if (current && strcmp(current->name, "shell") == 0) {
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