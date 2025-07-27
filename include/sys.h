#ifndef SYS_H
#define SYS_H

#include <hatcher.h>
#include <vga.h>
#include <gpu.h>
#include <thread.h>
#include <debug.h>

void shell(void);
void sh_exec(const char *cmd);

#endif