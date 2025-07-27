#include <kernutils.h>
#include <fat32.h>
#include <heap.h>
#include <string.h>
#include <debug.h>
#include <stdint.h>
#include <vga.h>
#include <stdbool.h>

uint8_t hex_char_to_byte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

//from osdev
void hexstr_to_bytes(const char* hex_str, uint8_t* byte_array, size_t max_bytes) {
    size_t len = strlen(hex_str);
    for (size_t i = 0; i < len / 2 && i < max_bytes; ++i) {
        uint8_t high_nibble = hex_char_to_byte(hex_str[i * 2]);
        uint8_t low_nibble = hex_char_to_byte(hex_str[i * 2 + 1]);
        byte_array[i] = (high_nibble << 4) | low_nibble;
    }
}

int build_path(uint32_t cluster, char path[][9], int max_depth) {
    extern uint32_t root_dir_first_cluster;
    int depth = 0;
    uint32_t cur = cluster;

    while (cur != root_dir_first_cluster && depth < max_depth) {
        /* 1. Прочитаем текущий каталог, чтобы узнать кластер родителя (запись "..") */
        fat32_dir_entry_t *entries = kmalloc(sizeof(fat32_dir_entry_t) * 32);
        if (!entries) break;
        int n = fat32_read_dir(0, cur, entries, 32);
        uint32_t parent_cluster = root_dir_first_cluster;
        for (int i = 0; i < n; i++) {
            if (entries[i].name[0] == '.' && entries[i].name[1] == '.') {
                parent_cluster = ((uint32_t)entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
                if (parent_cluster == 0) parent_cluster = root_dir_first_cluster;
                break;
            }
        }

        /* 2. Прочитаем родительский каталог, чтобы найти имя текущего каталога */
        fat32_dir_entry_t *pentries = kmalloc(sizeof(fat32_dir_entry_t) * 32);
        if (!pentries) { kfree(entries); break; }
        int pn = fat32_read_dir(0, parent_cluster, pentries, 32);
        char name[9] = {0};
        for (int i = 0; i < pn; i++) {
            if ((pentries[i].attr & 0x10) != 0x10) continue; /* только каталоги */
            uint32_t cl = ((uint32_t)pentries[i].first_cluster_high << 16) | pentries[i].first_cluster_low;
            if (cl == cur) {
                int pos = 0;
                for (int j = 0; j < 8; j++) {
                    if (pentries[i].name[j] != ' ' && pentries[i].name[j] != 0) {
                        name[pos++] = pentries[i].name[j];
                    }
                }
                name[pos] = 0;
                break;
            }
        }

        /* 3. Сохраняем имя в массиве path */
        for (int i = 0; i < 9; i++) path[depth][i] = name[i];

        /* 4. Освобождаем память и поднимаемся вверх */
        kfree(entries);
        kfree(pentries);
        cur = parent_cluster;
        depth++;
    }
    return depth;
}


void print_prompt() {
    extern uint32_t current_dir_cluster;
    extern uint32_t root_dir_first_cluster;
    if (current_dir_cluster == root_dir_first_cluster) {
        kprintf("%d:\\>", drive_num);
    } else {
        char path[32][9];
        int depth = build_path(current_dir_cluster, path, 32);
        kprintf("%d:", drive_num);
        for (int i = depth - 1; i >= 0; i--) {
            kprint("\\");
            kprint(path[i]);
        }
        kprint(">");
    }
}

void fat_name_from_string(const char *src, char dest[11])
{
    memset(dest, ' ', 11);
    if (!src) return;
    int dot = -1, len = strlen(src);
    for (int i = 0; i < len; i++)
        if (src[i] == '.') { dot = i; break; }

    if (dot == -1) {
        for (int i = 0; i < len && i < 8; i++)
            dest[i] = toupper(src[i]);
    } else {
        for (int i = 0; i < dot && i < 8; i++)
            dest[i] = toupper(src[i]);
        for (int i = dot + 1, j = 8; i < len && j < 11; i++, j++)
            dest[j] = toupper(src[i]);
    }
}
