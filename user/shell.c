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
#include <kernutils.h>
#include <fat32.h>
#include <ata.h>
#include <usb.h>
#include <thread.h>

extern int end;
extern int drive_num;
extern uint32_t current_dir_cluster;

// Объявления функций
int sh_execute_script(const char *filename);
int exec_sh_script(const char *pathname);
int sh_exec_single(const char *cmd);

// Структура для переменных окружения
typedef struct {
    char name[32];
    char value[128];
} env_var_t;

#define MAX_ENV_VARS 16
static env_var_t env_vars[MAX_ENV_VARS];
static int env_count = 0;

// Функция для установки переменной окружения
void set_env_var(const char *name, const char *value) {
    // Ищем существующую переменную
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            strncpy(env_vars[i].value, value, sizeof(env_vars[i].value) - 1);
            env_vars[i].value[sizeof(env_vars[i].value) - 1] = '\0';
            return;
        }
    }
    
    // Добавляем новую переменную
    if (env_count < MAX_ENV_VARS) {
        strncpy(env_vars[env_count].name, name, sizeof(env_vars[env_count].name) - 1);
        env_vars[env_count].name[sizeof(env_vars[env_count].name) - 1] = '\0';
        strncpy(env_vars[env_count].value, value, sizeof(env_vars[env_count].value) - 1);
        env_vars[env_count].value[sizeof(env_vars[env_count].value) - 1] = '\0';
        env_count++;
    }
}

// Функция для получения значения переменной окружения
const char *get_env_var(const char *name) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            return env_vars[i].value;
        }
    }
    return NULL;
}

// Функция для подстановки переменных в строку
void expand_variables(char *line, char *expanded, size_t max_len) {
    char *src = line;
    char *dst = expanded;
    size_t len = 0;
    
    while (*src && len < max_len - 1) {
        if (*src == '$' && *(src + 1) == '{') {
            // Переменная в формате ${VAR}
            src += 2; // Пропускаем ${
            char var_name[32] = {0};
            int var_len = 0;
            
            while (*src && *src != '}' && var_len < 31) {
                var_name[var_len++] = *src++;
            }
            var_name[var_len] = '\0';
            
            if (*src == '}') {
                src++; // Пропускаем }
                const char *var_value = get_env_var(var_name);
                if (var_value) {
                    while (*var_value && len < max_len - 1) {
                        *dst++ = *var_value++;
                        len++;
                    }
                }
            }
        } else if (*src == '$' && (*(src + 1) >= 'A' && *(src + 1) <= 'Z' || 
                                   *(src + 1) >= 'a' && *(src + 1) <= 'z' || 
                                   *(src + 1) == '_')) {
            // Переменная в формате $VAR
            src++; // Пропускаем $
            char var_name[32] = {0};
            int var_len = 0;
            
            while (*src && ((*src >= 'A' && *src <= 'Z') || 
                           (*src >= 'a' && *src <= 'z') || 
                           (*src >= '0' && *src <= '9') || 
                           *src == '_') && var_len < 31) {
                var_name[var_len++] = *src++;
            }
            var_name[var_len] = '\0';
            
            const char *var_value = get_env_var(var_name);
            if (var_value) {
                while (*var_value && len < max_len - 1) {
                    *dst++ = *var_value++;
                    len++;
                }
            }
        } else {
            *dst++ = *src++;
            len++;
        }
    }
    *dst = '\0';
}

// Функция для выполнения shell-скрипта с расширенными возможностями
int exec_sh_script(const char *pathname) {
    fat32_dir_entry_t *entries = kmalloc(sizeof(fat32_dir_entry_t)*128);
    if (!entries) { 
        kprintf("exec_sh_script: OOM\n"); 
        return 1;
    }
    
    int n = fat32_read_dir(drive_num, current_dir_cluster, entries, 128);
    if (n < 0) { 
        kprintf("exec_sh_script: dir error\n"); 
        kfree(entries); 
        return 1;
    }
    
    char fatname[12]; fat_name_from_string(pathname, fatname);
    int file_found = 0;
    int status = 1;
    
    for (int i = 0; i < n; i++) {
        if (!(entries[i].attr & 0x10)) { // файл
            if (!memcmp(entries[i].name, fatname, 11)) {
                uint32_t size = entries[i].file_size;
                uint32_t first_cluster = (entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
                uint8_t *buf = kmalloc(size + 1); // +1 для null-terminator
                if (!buf) { 
                    kprintf("exec_sh_script: OOM\n"); 
                    kfree(entries); 
                    return 1;
                }
                
                int rd = fat32_read_file(drive_num, first_cluster, buf, size);
                if (rd > 0) {
                    buf[rd] = '\0'; // Добавляем null-terminator
                    
                    // Разбиваем файл на строки и выполняем каждую
                    char *line = strtok((char*)buf, "\n\r");
                    int line_num = 1;
                    
                    while (line != NULL) {
                        // Пропускаем пустые строки и комментарии
                        if (strlen(line) > 0 && line[0] != '#') {
                            // Убираем пробелы в начале и конце
                            while (*line == ' ' || *line == '\t') line++;
                            char *end = line + strlen(line) - 1;
                            while (end > line && (*end == ' ' || *end == '\t' || *end == '\r')) end--;
                            *(end + 1) = '\0';
                            
                            if (strlen(line) > 0) {
                                // Проверяем, является ли это присваиванием переменной
                                char *equals = strchr(line, '=');
                                if (equals && equals != line) {
                                    // Это присваивание переменной
                                    char var_name[32] = {0};
                                    char var_value[128] = {0};
                                    
                                    int name_len = equals - line;
                                    if (name_len < 31) {
                                        strncpy(var_name, line, name_len);
                                        var_name[name_len] = '\0';
                                        
                                        // Убираем пробелы из имени переменной
                                        char *name_end = var_name + strlen(var_name) - 1;
                                        while (name_end > var_name && (*name_end == ' ' || *name_end == '\t')) {
                                            *name_end = '\0';
                                            name_end--;
                                        }
                                        
                                        strcpy(var_value, equals + 1);
                                        
                                        // Убираем кавычки из значения
                                        if (var_value[0] == '"' || var_value[0] == '\'') {
                                            char quote = var_value[0];
                                            memmove(var_value, var_value + 1, strlen(var_value));
                                            char *quote_end = strchr(var_value, quote);
                                            if (quote_end) *quote_end = '\0';
                                        }
                                        
                                        set_env_var(var_name, var_value);
                                        kprintf("Set %s=%s\n", var_name, var_value);
                                    }
                                } else {
                                    // Это команда - подставляем переменные
                                    char expanded_line[256];
                                    expand_variables(line, expanded_line, sizeof(expanded_line));
                                    
                                    int cmd_status = sh_exec_single(expanded_line);
                                    if (cmd_status != 0) {
                                        kprintf("exec_sh_script: line %d failed: %s\n", line_num, line);
                                        status = cmd_status;
                                    }
                                }
                            }
                        }
                        line = strtok(NULL, "\n\r");
                        line_num++;
                    }
                    status = 0;
                }
                kfree(buf);
                file_found = 1;
                break;
            }
        }
    }
    
    kfree(entries);
    
    if (!file_found) {
        kprintf("<(0C)>exec_sh_script: %s: not found<(07)>\n", pathname);
        status = 1;
    }
    
    return status;
}

// Функция для выполнения одной команды и возврата статуса
int sh_exec_single(const char *cmd) {
    int count = 0;
    char **args = split(cmd, ' ', &count);
    if (count == 0) return 0;
    
    int status = 0; // 0 = успех, 1 = ошибка
    
    if (strcmp(args[0], "exit") == 0) {
        end = 1;
        status = 0;
    }
    else if (strcmp(args[0], "disk") == 0) {
        if (count == 2) {
            drive_num = atoi(args[1]);
            status = 0;
        }
        else if (count == 1) {
            kprintf("%d\n", drive_num);
            status = 0;
        }
        else {
            kprintf("<(0C)>Usage: disk <drive_number><(07)>\n");
            status = 1;
        }
    }
    else if (strcmp(args[0], "help") == 0) {
        kprint("help command\n");
        status = 0;
    }
    else if (strcmp(args[0], "cat") == 0) {
        if (count < 2) { 
            kprint("<(0C)>Usage: cat [FILENAME]<(07)>\n"); 
            status = 1;
        } else {
            const char* filename = args[1];
            fat32_dir_entry_t *entries = kmalloc(sizeof(fat32_dir_entry_t)*128);
            if (!entries) { 
                kprint("cat: OOM\n"); 
                status = 1;
            } else {
                int n = fat32_read_dir(drive_num, current_dir_cluster, entries, 128);
                if (n<0) { 
                    kprint("cat: dir error\n"); 
                    kfree(entries); 
                    status = 1;
                } else {
                    char fatname[12]; fat_name_from_string(filename, fatname);
                    int file_found = 0;
                    for (int i=0;i<n;i++) {
                        if (!(entries[i].attr & 0x10)) { // файл
                            if (!memcmp(entries[i].name, fatname, 11)) {
                                uint32_t size = entries[i].file_size;
                                uint32_t first_cluster = (entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
                                uint8_t *buf = kmalloc(size);
                                if (!buf) { 
                                    kprint("cat: OOM\n"); 
                                    kfree(entries); 
                                    status = 1;
                                    break;
                                }
                                int rd = fat32_read_file(drive_num, first_cluster, buf, size);
                                if (rd > 0) {
                                    for (int b=0; b<rd; b++) putchar(buf[b],0x07);
                                    kprint("\n");
                                    status = 0;
                                } else {
                                    status = 1;
                                }
                                kfree(buf);
                                file_found = 1;
                                break;
                            }
                        }
                    }
                    kfree(entries);
                    
                    if (!file_found) {
                        kprintf("<(0C)>cat: %s: not found<(07)>\n", filename);
                        status = 1;
                    }
                }
            }
        }
    }
    else if (strcmp(args[0], "info") == 0) {
        kprintf("%s %s Operating System\nBSD 3-Clause License\nCopyright (c) 2025 fcexx\n", KERNEL_NAME, KERNEL_VERSION);
        status = 0;
    }
    else if (strcmp(args[0], "clear") == 0) {
        kclear();
        kprintf("\n\n");
        status = 0;
    }
    else if (strcmp(args[0], "lspid") == 0) {
        if (count == 1) {
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
            status = 0;
        } else {
            kprintf("<(0C)>Usage: lspid<(07)>\n");
            status = 1;
        }
    }
    else if (strcmp(args[0], "mkfs.fat32") == 0) {
        fat32_create_fs(drive_num);
        status = 0;
    }
    else if (strcmp(args[0], "xxd") == 0) {
        if (count < 2) { 
            kprint("Usage: xxd [-l len] <file>\n"); 
            status = 1;
        } else {
            uint32_t len_limit = 0;
            int fidx = 1;
            if (strcmp(args[1], "-l") == 0) {
                if (count < 4) { 
                    kprint("Usage: xxd -l <len> <file>\n"); 
                    status = 1;
                } else {
                    len_limit = atoi(args[2]);
                    if (!len_limit) { 
                        kprint("Bad length\n"); 
                        status = 1;
                    } else {
                        fidx = 3;
                        status = 0;
                    }
                }
            }
            
            if (status == 0) {
                const char *fname = args[fidx];
                fat32_dir_entry_t *dir = kmalloc(sizeof(fat32_dir_entry_t)*32);
                if (!dir) { 
                    kprint("xxd: OOM error\n"); 
                    status = 1;
                } else {
                    int n = fat32_read_dir(drive_num, current_dir_cluster, dir, 32);
                    if (n < 0) { 
                        kprint("xxd: dir error\n"); 
                        kfree(dir);
                        status = 1;
                    } else {
                        char fatname[12]; fat_name_from_string(fname, fatname);
                        uint32_t clu = 0, fsize = 0;
                        for (int i=0;i<n;i++)
                            if (!memcmp(dir[i].name,fatname,11)) {
                                clu = (dir[i].first_cluster_high<<16)|dir[i].first_cluster_low;
                                fsize = dir[i].file_size; break;
                            }
                        if (!clu){ 
                            kprintf("xxd: %s not found\n", fname); 
                            kfree(dir); 
                            status = 1;
                        } else {
                            uint32_t max = (len_limit && len_limit < fsize) ? len_limit : fsize;
                            uint8_t *file_buf = kmalloc(max);
                            if (!file_buf) { 
                                kprint("xxd: OOM error\n"); 
                                kfree(dir); 
                                status = 1;
                            } else {
                                int read_result = fat32_read_file(drive_num, clu, file_buf, max);
                                if (read_result < 0) { 
                                    kprintf("xxd: read error (result %d)\n", read_result); 
                                    kfree(file_buf); 
                                    kfree(dir); 
                                    status = 1;
                                } else {
                                    for (uint32_t off=0; off<max; off+=16){
                                        uint32_t chunk = (max-off>16)?16:max-off;
                                        
                                        kprintf("%08X: ", off);
                                        for(int i=0;i<16;i++){
                                            if(i<chunk) kprintf("%02X ", file_buf[off+i]);
                                            else kprint("   ");
                                        }
                                        kprint(" ");
                                        for(int i=0;i<chunk;i++){
                                            char c=(file_buf[off+i]>=32&&file_buf[off+i]<=126)?file_buf[off+i]:'.';
                                            putchar(c,0x07);
                                        }
                                        kprint("\n");
                                    }
                                    kfree(file_buf);
                                    kfree(dir);
                                    status = 0;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    else if (strcmp(args[0], "sleep") == 0) {
        if (count == 2) {
            uint32_t ms = atoi(args[1]);
            thread_sleep(ms);
            status = 0;
        } else {
            kprintf("<(0C)>Usage: sleep <milliseconds><(07)>\n");
            status = 1;
        }
    }
    else if (strcmp(args[0], "ls") == 0) {
        fat32_entry_t *entries = kmalloc(sizeof(fat32_entry_t) * 128);
        if (!entries) { 
            kprintf("<(0c)>ls: OOM<(0f)>\n"); 
            status = 1;
        } else {
            int n = fat32_list_dir(drive_num, current_dir_cluster, entries, 128);
            if (n < 0) { 
                kprintf("ls: dir read error\n"); 
                kfree(entries); 
                status = 1;
            } else {
                /* Сначала директории */
                for (int i = 0; i < n; i++) {
                    if (entries[i].attr & 0x10) {
                        kprintf(" <DIR>  %s\n", entries[i].name);
                    }
                }
                /* Затем файлы */
                for (int i = 0; i < n; i++) {
                    if (!(entries[i].attr & 0x10)) {
                        uint32_t size = entries[i].size;
                        if (size < 1024)
                            kprintf(" <FILE> %s (%u bytes)\n", entries[i].name, size);
                        else if (size < 1024*1024)
                            kprintf(" <FILE> %s (%u.%u KB)\n", entries[i].name, size/1024, (size%1024)/100);
                        else
                            kprintf(" <FILE> %s (%u.%u MB)\n", entries[i].name, size/(1024*1024), (size%(1024*1024))/100000);
                    }
                }
                kfree(entries);
                status = 0;
            }
        }
    }
    else if (strcmp(args[0], "cd") == 0) {
        if (count < 2) {
            kprint("<(0C)>Usage: cd <path><(07)>\n");
            status = 1;
        } else {
            int cd_res = fat32_change_dir(drive_num, args[1]);
            if (cd_res == 0) {
                status = 0;
            } else if (cd_res == -1) {
                kprintf("<(0C)>No such directory: %s<(07)>\n", args[1]);
                status = 1;
            } else if (cd_res == -2) {
                kprintf("<(0C)>Not a directory: %s<(07)>\n", args[1]);
                status = 1;
            } else if (cd_res == -3) {
                kprintf("Already at root directory\n");
                status = 0;
            } else {
                kprintf("<(0C)>Failed to change directory<(07)>\n");
                status = 1;
            }
        }
    }
    else if (strcmp(args[0], "stop") == 0) {
        if (count == 2) {
            int pid = atoi(args[1]);
            if (pid == 0) {
                kdbg(KERR, "thread_stop: access denied\n");
                status = 1;
            } else if (pid == thread_get_pid("shell")) {
                kdbg(KWARN, "thread_stop: stopping shell\n");
                end = 1;
                thread_stop(pid);
                status = 0;
            } else {
                thread_stop(pid);
                kdbg(KINFO, "thread_stop: pid %d stopped\n", pid);
                status = 0;
            }
        } else {
            kprintf("<(0C)>Usage: stop <pid><(07)>\n");
            status = 1;
        }
    }
    else if (strcmp(args[0], "unblock") == 0) {
        if (count == 2) {
            int pid = atoi(args[1]);
            thread_unblock(pid);
            kdbg(KINFO, "thread_unblock: pid %d unblocked\n", pid);
            status = 0;
        } else {
            kprintf("<(0C)>Usage: unblock <pid><(07)>\n");
            status = 1;
        }
    }
    else if (strcmp(args[0], "block") == 0) {
        if (count == 2) {
            int pid = atoi(args[1]);
            if (pid == 0) {
                kdbg(KERR, "thread_block: access denied\n");
                status = 1;
            } else {
                thread_block(pid);
                kdbg(KINFO, "thread_block: pid %d blocked\n", pid);
                status = 0;
            }
        } else {
            kprintf("<(0C)>Usage: block <pid><(07)>\n");
            status = 1;
        }
    }
    else if (strcmp(args[0], "beep") == 0) {
        if (count > 2) {
            int freq = atoi(args[1]);
            int duration = atoi(args[2]);
            pc_speaker_beep(freq, duration);
            status = 0;
        } else {
            kprintf("<(0C)>Usage: beep <frequency> <duration><(07)>\n");
            status = 1;
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
                status = 0;
            } else {
                kprintf("Error writing sector %u.\n", lba);
                status = 1;
            }
        } else {
            kprintf("<(0C)>Usage: write <lba> <hex_data><(07)>\n");
            status = 1;
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
                status = 1;
            } else if (size_to_read > MAX_READ_BUFFER_SIZE) {
                kprintf("requested size %u exceeds max buffer size %u. please request less data.\n", size_to_read, MAX_READ_BUFFER_SIZE);
                status = 1;
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
                    status = 0;
                } else {
                    status = 1;
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
                status = 0;
            } else {
                kprintf("error reading sector %u.\n", lba);
                status = 1;
            }
        } else {
            kprintf("<(0C)>Usage: read <lba> OR read -l <bytes> <lba><(07)>\n");
            status = 1;
        }
    }
    else if (strcmp(args[0], "yield") == 0) {
        thread_yield();
        status = 0;
    }
    else if (strcmp(args[0], "sh") == 0) {
        // Интерпретатор shell-скриптов
        if (count < 2) {
            kprintf("<(0C)>Usage: sh <script_file><(07)>\n");
            status = 1;
        } else {
            status = sh_execute_script(args[1]);
        }
    }
    else if (strcmp(args[0], "exec") == 0) {
        // Расширенный интерпретатор shell-скриптов с переменными
        if (count < 2) {
            kprintf("<(0C)>Usage: exec <script_file><(07)>\n");
            status = 1;
        } else {
            status = exec_sh_script(args[1]);
        }
    }
    else {
        kprintf("<(0C)>%s?<(07)>\n", args[0]);
        status = 1;
    }
    
    thread_yield();
    return status;
}

// Функция для выполнения shell-скрипта
int sh_execute_script(const char *filename) {
    fat32_dir_entry_t *entries = kmalloc(sizeof(fat32_dir_entry_t)*128);
    if (!entries) { 
        kprintf("sh: OOM\n"); 
        return 1;
    }
    
    int n = fat32_read_dir(drive_num, current_dir_cluster, entries, 128);
    if (n < 0) { 
        kprintf("sh: dir error\n"); 
        kfree(entries); 
        return 1;
    }
    
    char fatname[12]; fat_name_from_string(filename, fatname);
    int file_found = 0;
    int status = 1;
    
    for (int i = 0; i < n; i++) {
        if (!(entries[i].attr & 0x10)) { // файл
            if (!memcmp(entries[i].name, fatname, 11)) {
                uint32_t size = entries[i].file_size;
                uint32_t first_cluster = (entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
                uint8_t *buf = kmalloc(size + 1); // +1 для null-terminator
                if (!buf) { 
                    kprintf("sh: OOM\n"); 
                    kfree(entries); 
                    return 1;
                }
                
                int rd = fat32_read_file(drive_num, first_cluster, buf, size);
                if (rd > 0) {
                    buf[rd] = '\0'; // Добавляем null-terminator
                    
                    // Разбиваем файл на строки и выполняем каждую
                    char *line = strtok((char*)buf, "\n\r");
                    while (line != NULL) {
                        // Пропускаем пустые строки и комментарии
                        if (strlen(line) > 0 && line[0] != '#') {
                            // Убираем пробелы в начале и конце
                            while (*line == ' ' || *line == '\t') line++;
                            char *end = line + strlen(line) - 1;
                            while (end > line && (*end == ' ' || *end == '\t' || *end == '\r')) end--;
                            *(end + 1) = '\0';
                            
                            if (strlen(line) > 0) {
                                int cmd_status = sh_exec_single(line);
                                if (cmd_status != 0) {
                                    kprintf("sh: command failed: %s\n", line);
                                }
                            }
                        }
                        line = strtok(NULL, "\n\r");
                    }
                    status = 0;
                }
                kfree(buf);
                file_found = 1;
                break;
            }
        }
    }
    
    kfree(entries);
    
    if (!file_found) {
        kprintf("<(0C)>sh: %s: not found<(07)>\n", filename);
        status = 1;
    }
    
    return status;
}

// Функция для разбора команд с поддержкой &&
void sh_exec(const char *cmd) {
    char *cmd_copy = kmalloc(strlen(cmd) + 1);
    if (!cmd_copy) return;
    
    strcpy(cmd_copy, cmd);
    
    // Разбиваем команду по &&
    char *token = strtok(cmd_copy, "&");
    int last_status = 0;
    
    while (token != NULL) {
        // Убираем пробелы в начале и конце
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) end--;
        *(end + 1) = '\0';
        
        if (strlen(token) > 0) {
            // Проверяем, есть ли && после этой команды
            char *next_pos = strstr(token, "&&");
            if (next_pos != NULL) {
                // Есть && - обрезаем команду и выполняем только если предыдущая успешна
                *next_pos = '\0';
                if (last_status == 0) {
                    last_status = sh_exec_single(token);
                }
            } else {
                // Последняя команда - выполняем всегда
                last_status = sh_exec_single(token);
            }
        }
        
        token = strtok(NULL, "&");
    }
    
    kfree(cmd_copy);
}
