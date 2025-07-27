#ifndef KERNUTILS_H
#define KERNUTILS_H

#include <hatcher.h>
#include <vga.h>
#include <gpu.h>
#include <thread.h>
#include <debug.h>

extern int end;
extern int drive_num;

uint8_t hex_char_to_byte(char c);
void hexstr_to_bytes(const char* hex_str, uint8_t* byte_array, size_t max_bytes);
int build_path(uint32_t cluster, char path[][9], int max_depth);
void print_prompt();
void fat_name_from_string(const char *src, char dest[11]);

#endif