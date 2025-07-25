#include <thread.h>
#include <heap.h>
#include <string.h>
#include <timer.h>
#include <cpu.h>
#include <debug.h>
#include <vga.h>

#define MAX_THREADS 32
static thread_t* threads[MAX_THREADS];
static int thread_count = 0;
static thread_t* current = NULL;

static thread_t main_thread;

void thread_init() {
    memset(&main_thread, 0, sizeof(main_thread));
    main_thread.state = THREAD_RUNNING;
    main_thread.tid = 0;
    current = &main_thread;
    threads[0] = &main_thread;
    thread_count = 1;
    strncpy(main_thread.name, "idle", sizeof(main_thread.name));
    kdbg(KINFO, "thread_init: idle thread created with pid %d\n", main_thread.tid);
}

// для старта потока
static void thread_trampoline(void) {
    void (*entry)(void);
    __asm__ __volatile__("movq %%r12, %0" : "=r"(entry)); // entry = r12
    entry();
    for (;;) __asm__("hlt");
}

thread_t* thread_create(void (*entry)(void), const char* name) {
    if (thread_count >= MAX_THREADS) return NULL;
    thread_t* t = (thread_t*)kmalloc(sizeof(thread_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(thread_t));
    t->kernel_stack = (uint64_t)kmalloc(8192 + 16) + 8192;
    uint64_t* stack = (uint64_t*)t->kernel_stack;
    stack[-1] = (uint64_t)thread_trampoline; // ret пойдёт на trampoline
    t->context.rsp = (uint64_t)&stack[-1];
    t->context.r12 = (uint64_t)entry; // entry передаётся через r12
    t->context.rflags = 0x202;
    t->state = THREAD_READY;
    t->tid = thread_count;
    strncpy(t->name, name, sizeof(t->name));
    threads[thread_count++] = t;
    kdbg(KINFO, "thread_create: created thread '%s' with pid %d\n", t->name, t->tid);
    return t;
}

thread_t* thread_current() {
    return current;
}

void thread_yield() {
    thread_schedule();
}

void thread_stop(int pid) {
    for (int i = 0; i < thread_count; ++i) {
        if (threads[i] && threads[i]->tid == pid && threads[i]->state != THREAD_TERMINATED) {
            threads[i]->state = THREAD_TERMINATED;
            return;
        }
    }
}

void thread_block(int pid) {
    for (int i = 0; i < thread_count; ++i) {
        if (threads[i] && threads[i]->tid == pid && threads[i]->state != THREAD_BLOCKED) {
            threads[i]->state = THREAD_BLOCKED;
            return;
        }
    }
}

void thread_schedule() {
    int next = (current->tid + 1) % thread_count;
    for (int i = 0; i < thread_count; ++i) {
        int idx = (next + i) % thread_count;
        if (threads[idx]->state == THREAD_READY) {
            thread_t* prev = current;
            current = threads[idx];
            current->state = THREAD_RUNNING;
            prev->state = THREAD_READY;
            context_switch(&prev->context, &current->context);
            // После возврата из context_switch поток снова активен
            current->state = THREAD_RUNNING;
            return;
        }
    }
    // Если нет READY потоков, остаёмся в текущем
}

void thread_unblock(int pid) {
    for (int i = 0; i < thread_count; ++i) {
        if (threads[i] && threads[i]->tid == pid && threads[i]->state == THREAD_BLOCKED) {
            threads[i]->state = THREAD_READY;
            return;
        }
    }
}

// get thread info by pid
thread_t* thread_get(int pid) {
    for (int i = 0; i < thread_count; ++i) {
        if (threads[i] && threads[i]->tid == pid) {
            return threads[i];
        }
    }
    return NULL;
}

int thread_get_pid(const char* name) {
    for (int i = 0; i < thread_count; ++i) {
        if (threads[i] && strcmp(threads[i]->name, name) == 0) {
            return threads[i]->tid;
        }
    }
    return -1;
}

int thread_get_state(int pid) {
    for (int i = 0; i < thread_count; ++i) {
        if (threads[i] && threads[i]->tid == pid) {
            return threads[i]->state;
        }
    }
    return -1;
}

int thread_get_count() {
    return thread_count;
}