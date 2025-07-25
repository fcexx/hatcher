#ifndef THREAD_H
#define THREAD_H
#include <stdint.h>
#include <context.h>

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_TERMINATED
} thread_state_t;

typedef struct thread {
    context_t context;
    uint64_t kernel_stack;
    thread_state_t state;
    struct thread* next;
    uint64_t tid;
    char name[32];
} thread_t;

void thread_init();
thread_t* thread_create(void (*entry)(void), const char* name);
void thread_yield();
void thread_schedule();
thread_t* thread_current();
void thread_stop(int pid);
thread_t* thread_get(int pid);
int thread_get_pid(const char* name);
void thread_block(int pid);
void thread_unblock(int pid);
int thread_get_state(int pid);
int thread_get_count();

#endif // THREAD_H 