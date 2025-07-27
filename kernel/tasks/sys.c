#include <kernutils.h>
#include <guitasks.h>
#include <fat32.h>
#include <ata.h>
#include <vga.h>
#include <thread.h>
#include <pc_speaker.h>
#include <stdbool.h>
#include <heap.h>
#include <ps2.h>
#include <stdint.h>
#include <string.h>
#include <debug.h>
#include <sys.h>

void shell(void) {
    kprintf("\n\n");
    while (1) {
        print_prompt();
        char *buf = kgets();
        sh_exec(buf);
    }
}
