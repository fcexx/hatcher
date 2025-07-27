#ifndef GUI_TASKS_H
#define GUI_TASKS_H

#include <hatcher.h>
#include <vga.h>
#include <gpu.h>
#include <thread.h>
#include <debug.h>

extern uint32_t system_time; //Время системных операций
extern uint32_t irq_time; //Время обработки прерываний
extern uint32_t idle_time; //Время простоя
extern uint32_t last_cpu_update;
extern uint64_t sys_seconds;
extern uint64_t sys_minutes;
extern uint64_t sys_hours;

void sys_time();
void calc_time();
void ui_bar();

#endif