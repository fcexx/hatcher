// from github
#include <fat32.h>
#include <ata.h>
#include <vga.h>
#include <debug.h>
#include <string.h>
#include <heap.h>
#include <stdbool.h>

#ifndef FAT_DEBUG
#define FAT_DEBUG 0
#endif

/* ---------------------------------------------------------------------
 *  Global variables, accessible to other modules
 * -------------------------------------------------------------------*/
fat32_bpb_t fat32_bpb;
uint32_t    fat_start              = 0;
uint32_t    root_dir_first_cluster = 2;
uint32_t    current_dir_cluster    = 2;

/* private */
static uint32_t cluster_begin_lba   = 0;
static uint32_t sectors_per_fat     = 0;
static uint32_t total_clusters      = 0;
static uint32_t partition_lba       = 0;  // LBA начала раздела
/* Hint for fast search for free clusters */
static uint32_t next_free_hint      = 3;

/* Cache of one FAT sector for speeding up get_next_cluster */
static uint32_t cached_fat_sector   = 0xFFFFFFFF;
static uint8_t  fat_cache[512];

/* Forward declarations for helpers located later in this file */
static uint32_t find_free_cluster(uint8_t drive);
static int      fat_write_fat_entry(uint8_t drive, uint32_t cluster, uint32_t value);

// ------------------------------------------------------------------
static char toupper_ascii(char c) {
    return (c>='a' && c<='z') ? (c - ('a'-'A')) : c;
}

/* Decode 13 UTF-16 characters from LFN record to ASCII.
 * Returns the number of added characters. */
static int lfn_copy_part(char *dst, const fat32_lfn_entry_t *lfn) {
    int pos = 0;
    const uint16_t *src16;
    // name1
    src16 = lfn->name1;
    for (int i=0;i<5;i++) {
        uint8_t ch = src16[i] & 0xFF;
        if (ch==0x00 || ch==0xFF) { dst[pos]=0; return pos; }
        dst[pos++] = ch;
    }
    // name2
    src16 = lfn->name2;
    for (int i=0;i<6;i++) {
        uint8_t ch = src16[i] & 0xFF;
        if (ch==0x00 || ch==0xFF) { dst[pos]=0; return pos; }
        dst[pos++] = ch;
    }
    // name3
    src16 = lfn->name3;
    for (int i=0;i<2;i++) {
        uint8_t ch = src16[i] & 0xFF;
        if (ch==0x00 || ch==0xFF) { dst[pos]=0; return pos; }
        dst[pos++] = ch;
    }
    dst[pos]=0;
    return pos;
}

/* Comparison of ASCII strings without regard to case. */
static int strcasecmp_ascii(const char *a, const char *b) {
    while (*a && *b) {
        char ca = toupper_ascii(*a);
        char cb = toupper_ascii(*b);
        if (ca!=cb) return ca - cb;
        ++a; ++b;
    }
    return (*a) - (*b);
}

/* Подсчитать checksum короткого имени (для LFN). */
static uint8_t shortname_checksum(const char name[11]) {
    uint8_t sum = 0;
    for (int i=0;i<11;i++) {
        sum = ((sum>>1) | (sum<<7)) + (uint8_t)name[i];
    }
    return sum;
}

/* Преобразовать короткое имя в человекочитаемую строку (без пробелов). */
static void shortname_to_string(const char in[11], char *out) {
    int pos=0;
    /* имя */
    for (int i=0;i<8;i++) {
        if (in[i]==' ') break;
        out[pos++] = in[i];
    }
    /* расширение */
    int has_ext=0;
    for (int i=8;i<11;i++) if (in[i]!=' ') { has_ext=1; break; }
    if (has_ext) {
        out[pos++]='.';
        for (int i=8;i<11;i++) {
            if (in[i]==' ') break;
            out[pos++] = in[i];
        }
    }
    out[pos]=0;
}

/* ---------------------------------------------------------------------
 *                          НИЗКОУРОВНЕВЫЕ ФУНКЦИИ
 * -------------------------------------------------------------------*/
int fat32_mount(uint8_t drive) {
    uint8_t *sector = kmalloc(512);
    if (!sector) return -1;
    
    // Читаем MBR (сектор 0)
    if (ata_read_sector(drive, 0, sector)!=0) { kfree(sector); return -2; }
    
    // Проверяем сигнатуру MBR
    if (sector[0x1FE] != 0x55 || sector[0x1FF] != 0xAA) {
        kfree(sector); 
        kdbg(KERR, "fat32_mount: invalid mbr signature (0x%02X%02X)\n", sector[0x1FE], sector[0x1FF]); 
        return -3;
    }
    
    // Ищем активный раздел FAT32
    uint32_t partition_lba = 0;
    for (int i = 0; i < 4; i++) {
        int offset = 0x1BE + i * 16;
        uint8_t status = sector[offset];
        uint8_t type = sector[offset + 4];
        
        kdbg(KINFO, "fat32_mount: partition %d: status=0x%02X, type=0x%02X\n", i, status, type);
        
        // Проверяем что раздел имеет тип FAT32 (не обязательно активный)
        if (type == 0x0B || type == 0x0C) {
            // Читаем LBA первого сектора раздела
            partition_lba = *(uint32_t*)(&sector[offset + 8]);
            kdbg(KINFO, "fat32_mount: found fat32 partition at LBA %u\n", partition_lba);
            break;
        }
    }
    
    if (partition_lba == 0) {
        kfree(sector); 
        kdbg(KERR, "fat32_mount: no fat32 partition found\n"); 
        return -4;
    }
    
    // Читаем загрузочный сектор раздела
    if (ata_read_sector(drive, partition_lba, sector)!=0) { kfree(sector); return -5; }
    
    // Проверяем сигнатуру загрузочного сектора
    if (sector[0x1FE] != 0x55 || sector[0x1FF] != 0xAA) {
        kfree(sector); 
        kdbg(KERR, "fat32_mount: invalid boot sector signature\n"); 
        return -6;
    }
    
    memcpy(&fat32_bpb, sector, sizeof(fat32_bpb)); /* dst=bpb, src=sector */

    if (fat32_bpb.table_size_32==0) { kfree(sector); kdbg(KERR, "fat32_mount: table_size_32 is 0\n"); return -7; }
    sectors_per_fat     = fat32_bpb.table_size_32;
    fat_start           = fat32_bpb.reserved_sector_count;
    cluster_begin_lba   = partition_lba + fat_start + fat32_bpb.table_count * sectors_per_fat;
    root_dir_first_cluster = fat32_bpb.root_cluster ? fat32_bpb.root_cluster : 2;
    current_dir_cluster = root_dir_first_cluster;

    kdbg(KINFO, "fat32: sectors_per_fat: %u\n", sectors_per_fat);
    kdbg(KINFO, "fat32: fat_start: %u\n", fat_start);
    kdbg(KINFO, "fat32: cluster_begin_lba: %u\n", cluster_begin_lba);
    kdbg(KINFO, "fat32: root_dir_first_cluster: %u\n", root_dir_first_cluster);
    kdbg(KINFO, "fat32: current_dir_cluster: %u\n", current_dir_cluster);

    /* количество доступных кластеров на разделе */
    uint32_t data_sectors = fat32_bpb.total_sectors_32 - cluster_begin_lba;
    total_clusters = data_sectors / fat32_bpb.sectors_per_cluster;

    kfree(sector);
    cached_fat_sector = 0xFFFFFFFF; // сброс кеша
    next_free_hint    = 3;          // сброс hint-указателя

    return 0;
}

uint32_t fat32_cluster_to_lba(uint32_t cluster) {
    return partition_lba + cluster_begin_lba + (cluster-2) * fat32_bpb.sectors_per_cluster;
}

uint32_t fat32_get_next_cluster(uint8_t drive, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;               // 4 байта на запись
    uint32_t fat_sector = partition_lba + fat_start + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    if (fat_sector != cached_fat_sector) {
        if (ata_read_sector(drive, fat_sector, fat_cache)!=0)
            return 0x0FFFFFFF; // ошибка
        cached_fat_sector = fat_sector;
    }
    uint32_t next = *(uint32_t*)(&fat_cache[ent_offset]) & 0x0FFFFFFF;
    return next;
}

/* ------------------------------------------------------------------
 *            ЧТЕНИЕ КАТАЛОГА (с построением LFN)
 * ----------------------------------------------------------------*/
int fat32_list_dir(uint8_t drive, uint32_t cluster,
                   fat32_entry_t* out, int max_entries) {
    uint8_t *sector = kmalloc(512);
    if (!sector) return -1;
    int count = 0;

    char lfn_parts[20][14]; // до 20 частей х 13 символов = 260
    int  lfn_present = 0;

    uint32_t cl = cluster;
    while (cl < 0x0FFFFFF8) {
        for (uint8_t s=0; s<fat32_bpb.sectors_per_cluster; s++) {
            uint32_t lba = fat32_cluster_to_lba(cl)+s;
            if (ata_read_sector(drive, lba, sector)!=0) { kfree(sector); return -2; }

            for (int off=0; off<512; off+=32) {
                fat32_dir_entry_t *ent = (fat32_dir_entry_t*)&sector[off];
                if (ent->name[0]==0x00) { kfree(sector); return count; }
                if (ent->attr==0x0F) {
                    fat32_lfn_entry_t *lfn = (fat32_lfn_entry_t*)ent;
                    int ord = lfn->order & 0x1F;    // 1..N
                    if (lfn->order & 0x40) {
                        /* это начало новой цепочки LFN – очищаем буфер */
                        memset(lfn_parts, 0, sizeof(lfn_parts));
                    }
                    if (ord>0 && ord<=20) {
                        lfn_copy_part(lfn_parts[ord-1], lfn);
                        if (lfn->order & 0x40) lfn_present = ord; // последний элемент
                    }
                    continue;
                }
                if (ent->name[0]==0xE5) { lfn_present=0; continue; } // удалённая
                if ((ent->attr & 0x08)==0x08) { lfn_present=0; continue; } // volume label
                if (count>=max_entries) { kfree(sector); return count; }

                // --- заполняем выходную структуру ---
                fat32_entry_t *dst = &out[count];
                memset(dst,0,sizeof(*dst));
                if (lfn_present) {
                    /* склеиваем части LFN в правильном порядке */
                    dst->name[0] = '\0';
                    for (int i = lfn_present - 1; i >= 0; i--) {
                        int len = strlen(lfn_parts[i]);
                        int pos = strlen(dst->name);
                        for(int k=0;k<len && pos < FAT32_MAX_NAME; k++)
                            dst->name[pos++] = lfn_parts[i][k];
                        dst->name[pos] = '\0';
                    }
                } else {
                    shortname_to_string(ent->name, dst->name);
                }
                dst->attr = ent->attr;
                dst->first_cluster = ((uint32_t)ent->first_cluster_high<<16) | ent->first_cluster_low;
                dst->size = ent->file_size;

                count++;
                lfn_present = 0; // сброс для следующего файла
            }
        }
        cl = fat32_get_next_cluster(drive, cl);
    }
    kfree(sector);
    return count;
}

/* Упрощённая обёртка для совместимости: возвращаем только короткие записи
 * (LFN игнорируются). Старый код shell.c продолжит работать, хотя длинные
 * имена будут скрыты. */
int fat32_read_dir(uint8_t drive, uint32_t cluster,
                   fat32_dir_entry_t* entries, int max_entries) {
    uint8_t *sector = kmalloc(512);
    if (!sector) return -1;
    int count=0;
    uint32_t cl = cluster;
    while (cl < 0x0FFFFFF8) {
        for (uint8_t s=0; s<fat32_bpb.sectors_per_cluster; s++) {
            uint32_t lba = fat32_cluster_to_lba(cl)+s;
            if (ata_read_sector(drive, lba, sector)!=0) { kfree(sector); return -2; }
            for (int off=0; off<512; off+=32) {
                fat32_dir_entry_t *ent = (fat32_dir_entry_t*)&sector[off];
                if (ent->name[0]==0x00) { kfree(sector); return count; }
                if (ent->attr==0x0F || ent->name[0]==0xE5) continue; // пропускаем LFN и удалённые
                if (count>=max_entries) { kfree(sector); return count; }
                /* копируем 32-байтную запись в выходной массив (src,dst) */
                for (int j=0;j<sizeof(fat32_dir_entry_t);j++)
                    ((uint8_t*)&entries[count])[j] = ((uint8_t*)ent)[j];
                count++;
            }
        }
        cl = fat32_get_next_cluster(drive, cl);
    }
    kfree(sector);
    return count;
}

/* --------------------- Прочитать файл целиком -------------------------*/
int fat32_read_file(uint8_t drive, uint32_t first_cluster,
                    uint8_t* buf, uint32_t size) {
    if (first_cluster<2) return -1;
    uint32_t cluster = first_cluster;
    uint32_t total   = 0;
    uint8_t *sector  = kmalloc(512);
    if (!sector) return -2;

    while (cluster < 0x0FFFFFF8 && total < size) {
        for (uint8_t s=0; s<fat32_bpb.sectors_per_cluster; s++) {
            uint32_t lba = fat32_cluster_to_lba(cluster)+s;
            // if (ata_read_sector(drive, lba, sector)!=0) { kfree(sector); return -3; }
            ata_read_sector(drive, lba, sector);
            uint32_t copy = (size-total>512)?512:(size-total);
            memcpy(buf + total, sector, copy);   /* src = sector, dst = buf+total (src,dst,len) */
            total += copy;
            if (total>=size) break;
        }
        cluster = fat32_get_next_cluster(drive, cluster);
    }
    kfree(sector);
    return total;
}

/* --------------- Заглушки для записи (пока не реализованы) ------------*/
int fat32_write_file(uint8_t drive, const char* path, const uint8_t* buf, uint32_t size){ (void)drive; (void)path; (void)buf; (void)size; return -1; }
/* старый stub fat32_create_file удалён */
int fat32_write_file_data(uint8_t drive,const char*name,const uint8_t*buf,uint32_t size,uint32_t offset){
    if(!name||!buf||size==0) return -1;

    /* --- ищем файл в текущем каталоге --- */
    fat32_entry_t *list = kmalloc(64*sizeof(fat32_entry_t));
    if (!list) return -1;
    int n = fat32_list_dir(drive, current_dir_cluster, list, 64);
    int idx=-1;
    for(int i=0;i<n;i++) if(!(list[i].attr&0x10))
        if(strcasecmp_ascii(list[i].name,name)==0){ idx=i; break; }

    if(idx==-1){
        /* создаём файл */
        if(offset!=0){ return -1; }
        if(fat32_create_file(drive,name)!=0) {kfree(list); return -1;}
        n = fat32_list_dir(drive, current_dir_cluster, list, 64);
        for(int i=0;i<n;i++) if(!(list[i].attr&0x10))
            if(strcasecmp_ascii(list[i].name,name)==0){ idx=i; break; }
        if(idx==-1) {kfree(list); return -1;}
    }

    fat32_entry_t *ent = &list[idx];
    uint32_t file_size = ent->size;
    uint32_t first_cluster = ent->first_cluster;
    if(first_cluster==0){ /* allocate first cluster */
        uint32_t cl = find_free_cluster(drive);
        if(!cl) {kfree(list); return -1;}
        fat_write_fat_entry(drive, cl, 0x0FFFFFFF);
        first_cluster = cl;
        ent->first_cluster = cl;
    }

    /* --- обеспечиваем достаточно кластеров --- */
    uint32_t cluster_size = fat32_bpb.sectors_per_cluster * 512;
    if (cluster_size == 0) cluster_size = 512; /* страховка от деления на ноль */
    uint32_t need_size = offset + size;
    uint32_t need_clusters = (need_size + cluster_size -1)/cluster_size;

    uint32_t cl = first_cluster; uint32_t chain_len=1;
    while(1){
        uint32_t next = fat32_get_next_cluster(drive, cl);
        if(next>=0x0FFFFFF8) break;
        chain_len++; cl=next;
    }
    while(chain_len<need_clusters){
        uint32_t newcl = find_free_cluster(drive);
        if(!newcl) {kfree(list); return -1;}
        fat_write_fat_entry(drive, cl, newcl);
        fat_write_fat_entry(drive, newcl, 0x0FFFFFFF);
        chain_len++; cl=newcl;
    }

    /* --- запись --- */
    uint32_t pos=0; uint32_t cur_off=offset; cl = first_cluster;
    uint32_t skip = cur_off/cluster_size;
    for(uint32_t i=0;i<skip;i++){ cl = fat32_get_next_cluster(drive, cl); }

    uint8_t *sector = kmalloc(512);
    if (!sector) {kfree(list); return -1;}
    while(pos<size){
        uint32_t within = cur_off % cluster_size;
        uint32_t sec_in_cluster = within / 512;
        uint32_t sec_off = within % 512;
        uint32_t lba = fat32_cluster_to_lba(cl)+sec_in_cluster;
        if(sec_off==0 && (size-pos)>=512){
            /* можем писать полный сектор */
            if(ata_write_sector(drive, lba, (uint8_t*)buf+pos)!=0) { kfree(sector); return -1; }
            pos+=512; cur_off+=512;
        } else {
            /* читаем сектор, модифицируем */
            if(ata_read_sector(drive, lba, sector)!=0) { kfree(sector); return -1; }
            uint32_t chunk = 512-sec_off; if(chunk>size-pos) chunk=size-pos;
            /* копируем данные из пользовательского буфера в считанный сектор */
            memcpy(sector+sec_off, (uint8_t*)buf+pos, chunk);
            if(ata_write_sector(drive, lba, sector)!=0) { kfree(sector); return -1; }
            pos+=chunk; cur_off+=chunk;
        }
        if((cur_off % cluster_size)==0 && pos<size){
            cl = fat32_get_next_cluster(drive, cl);
        }
    }
    kfree(sector);
    /* --- обновляем размер, если увеличился --- */
    if(need_size>file_size){
        ent->size = need_size;
        /* найти и обновить запись в каталоге (SFN) */
        uint8_t *sect = kmalloc(512);
        if (!sect) {kfree(list); return -1;}
        for(uint8_t sc=0; sc<fat32_bpb.sectors_per_cluster; sc++){
            uint32_t lba = fat32_cluster_to_lba(current_dir_cluster)+sc;
            if(ata_read_sector(drive,lba,sect)!=0) { kfree(sect); return -1; }
            for(int off=0; off<512; off+=32){
                fat32_dir_entry_t *e = (fat32_dir_entry_t*)&sect[off];
                if((e->attr&0x0F)==0x0F) continue;
                char tmp[64]; shortname_to_string(e->name,tmp);
                if(strcasecmp_ascii(tmp, ent->name)==0){
                    e->file_size = need_size;
                    e->first_cluster_high = (first_cluster>>16)&0xFFFF; /* запись SFN всё ещё содержит high/low */
                    e->first_cluster_low = first_cluster & 0xFFFF;
                    if(ata_write_sector(drive,lba,sect)!=0) { kfree(sect); return -1; }
                    sc=0xFF; break;
                }
            }
        }
        kfree(sect);
    }
    kfree(list);

    /* --- операция завершена: сброс кеша FAT, чтобы следующие вызовы
       (например cd) видели уже записанные изменения --- */
    cached_fat_sector = 0xFFFFFFFF;
    next_free_hint    = 2;

    return size;
}

int fat32_read_file_data(uint8_t d,const char*p,uint8_t*b,uint32_t s,uint32_t o){(void)d;(void)p;(void)b;(void)s;(void)o;return -1;}

/* -------------------------------------------------------------
 *          Простейшая реализация разрешения пути
 * -----------------------------------------------------------*/
int fat32_resolve_path(uint8_t drive, const char* path, uint32_t* target_cluster) {
    if (!path || !target_cluster) return -1;

    /* Снимаем возможный префикс "X:\" или "X:/" (номер диска) */
    if (path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
        /* если после префикса ничего нет – это корень */
        if (path[3] == '\0') {
            *target_cluster = root_dir_first_cluster;
            return 0;
        }
        /* пропускаем "X:\" */
        path += 3;
    }

    /* Абсолютный корень */
    if ((path[0]=='/' || path[0]=='\\') && path[1]=='\0') {
        *target_cluster = root_dir_first_cluster;
        return 0;
    }

    /* Текущий каталог */
    if (path[0]=='\0' || (path[0]=='.' && path[1]=='\0')) {
        *target_cluster = current_dir_cluster;
        return 0;
    }

    /* Родитель */
    if (path[0]=='.' && path[1]=='.' && path[2]=='\0') {
        fat32_entry_t list[2];
        int n = fat32_list_dir(drive, current_dir_cluster, list, 2);
        if (n<2) return -1;
        *target_cluster = list[1].first_cluster;
        /* если ".." указывает на текущий каталог – считаем, что это корень */
        if (*target_cluster == current_dir_cluster || *target_cluster == 0)
            *target_cluster = root_dir_first_cluster;
        return 0;
    }

    /* Ищем директорию/файл в текущем каталоге */
    fat32_entry_t list[64];
    int n = fat32_list_dir(drive, current_dir_cluster, list, 64);
    if (n<0) return -1;
    for (int i=0;i<n;i++) {
        if ((list[i].attr & 0x10)==0) continue; /* нужна только DIR */
        if (strcasecmp_ascii(list[i].name, path)==0) {
            *target_cluster = list[i].first_cluster;
            return 0;
        }
    }
    return -1; /* не найдено */
}

int fat32_change_dir(uint8_t d,const char* p){
    uint32_t c;
    int r=fat32_resolve_path(d,p,&c);
    if(!r && c>=2){ current_dir_cluster=c; }
    return r;
}

/* Найти свободный кластер (значение 0 в FAT) */
static uint32_t find_free_cluster(uint8_t drive){
    if(total_clusters==0) return 0;
    uint32_t start = next_free_hint;
    for(uint32_t iter=0; iter<total_clusters; iter++){
        uint32_t cl = 2 + ((start -2 + iter) % total_clusters); /* диапазон 2..2+total_clusters-1 */
        if(fat32_get_next_cluster(drive, cl)==0x00000000){
            next_free_hint = cl+1;
            if(next_free_hint >= 2+total_clusters) next_free_hint = 2;
            return cl;
        }
    }
    return 0; /* нет свободных */
}
/* Записать значение в FAT для указанного кластера */
static int fat_write_fat_entry(uint8_t drive, uint32_t cluster, uint32_t value){
    uint32_t fat_offset = cluster*4;
    for(uint8_t t=0;t<fat32_bpb.table_count;t++){
        uint32_t fat_sector = fat_start + t*sectors_per_fat + fat_offset/512;
        uint32_t ent_off    = fat_offset%512;
        uint8_t sector[512];
        if(ata_read_sector(drive,fat_sector,sector)!=0) return -1;
        *(uint32_t*)&sector[ent_off] = value & 0x0FFFFFFF;
        if(ata_write_sector(drive,fat_sector,sector)!=0) return -1;
    }
    cached_fat_sector = 0xFFFFFFFF;
    return 0;
}
/* Подготовить SFN из long_name (упрощённо) */
static void make_sfn(const char *longname, char sfn[11]){
    memset(sfn,' ',11);
    int len=strlen(longname); int dot=-1;
    for(int i=0;i<len;i++) if(longname[i]=='.'){dot=i;break;}
    if(dot==-1){
        for(int i=0;i<len && i<8;i++) sfn[i]=toupper_ascii(longname[i]);
    } else {
        for(int i=0;i<dot && i<8;i++) sfn[i]=toupper_ascii(longname[i]);
        for(int i=dot+1,j=8;i<len && j<11;i++,j++) sfn[j]=toupper_ascii(longname[i]);
    }
}
/* Записать последовательность LFN+SFN в каталог (один сектор, без расширения) */
static int dir_write_entries(uint8_t drive, uint32_t lba, int offset, const uint8_t *entries, int count){
    uint8_t sector[512];
    if(ata_read_sector(drive,lba,sector)!=0) return -1;
    /* копируем записи по байтам во внутренний буфер сектора */
    for(int i=0;i<count*32;i++)
        sector[offset+i] = entries[i];
    if(ata_write_sector(drive,lba,sector)!=0) return -1;
    return 0;
}

/* Создать файл (пустой) с длинным именем в текущем каталоге */
int fat32_create_file(uint8_t drive, const char* name){
    /* Подготовка SFN */
    char sfn[11]; make_sfn(name,sfn);
    uint8_t checksum = shortname_checksum(sfn);
    int namelen=strlen(name);
    int lfn_entries = (namelen+12)/13;

    /* Собираем массив будущих записей */
    int total_entries = lfn_entries+1;
    uint8_t *buf = kmalloc(total_entries*32); if(!buf) return -1;
    memset(buf,0,total_entries*32);
    /* LFN – номера 1..N; запись с 0x40 (последняя) содержит начало имени */
    for(int seq=lfn_entries; seq>=1; seq--){
        int buf_idx = lfn_entries - seq;              /* положение в буфере */
        fat32_lfn_entry_t *lfn=(fat32_lfn_entry_t*)&buf[buf_idx*32];
        lfn->order = seq;
        if(seq==lfn_entries) lfn->order |= 0x40;      /* последняя часть */
        lfn->attr  = 0x0F; lfn->type=0; lfn->checksum=checksum; lfn->first_cluster_low=0;

        int start = (seq-1)*13;                       /* смещение в имени */
        for(int j=0;j<13;j++){
            uint16_t ch = 0xFFFF;
            if(start+j < namelen) ch = (uint8_t)name[start+j];
            uint16_t *dst = (j<5)? &lfn->name1[j] : (j<11)? &lfn->name2[j-5] : &lfn->name3[j-11];
            *dst = ch;
        }
    }
    /* SFN entry */
    fat32_dir_entry_t *s = (fat32_dir_entry_t*)&buf[lfn_entries*32];
    for(int i=0;i<11;i++) s->name[i] = sfn[i];
    s->attr = 0x20; /* file */
    s->file_size=0; s->first_cluster_high=0; s->first_cluster_low=0;

    /* Найти место в текущем каталоге */
    uint32_t cl=current_dir_cluster;
    uint8_t sector[512];
    while(1){
        for(uint8_t sec=0;sec<fat32_bpb.sectors_per_cluster;sec++){
            uint32_t lba=fat32_cluster_to_lba(cl)+sec;
            if(ata_read_sector(drive,lba,sector)!=0){kfree(buf);return -1;}
            for(int off=0;off<=512-32*total_entries;off+=32){
                int free_ok=1;
                for(int e=0;e<total_entries;e++) if(sector[off+e*32]!=0x00 && sector[off+e*32]!=0xE5){free_ok=0;break;}
                if(free_ok){
                    /* пишем наши записи в сектор */
                    for(int i=0;i<total_entries*32;i++)
                        sector[off+i] = buf[i];
                    int end=off+total_entries*32;
                    if(end<512) sector[end]=0x00;
                    if(ata_write_sector(drive,lba,sector)!=0){kfree(buf);return -1;}
                    kfree(buf); return 0;
                }
                /* конец каталога метка 0x00 */
                if(sector[off]==0x00){
                    /* достаточно ли места? если нет, сдвигаем конец */
                    memset(&sector[off],0x00,512-off); /* clear to end*/
                    for(int i=0;i<total_entries*32;i++)
                        sector[off+i] = buf[i];
                    int end=off+total_entries*32;
                    if(end<512) sector[end]=0x00;
                    if(ata_write_sector(drive,lba,sector)!=0){kfree(buf);return -1;}
                    kfree(buf); return 0;
                }
            }
        }
        /* нет места, нужно расширить каталог */
        uint32_t next = fat32_get_next_cluster(drive, cl);
        if(next>=0x0FFFFFF8){ /* allocate new */
            uint32_t newcl = find_free_cluster(drive); if(!newcl){kfree(buf);return -1;}
            fat_write_fat_entry(drive, cl, newcl);
            fat_write_fat_entry(drive, newcl, 0x0FFFFFFF);
            /* zero new cluster */
            uint8_t zero[512]; memset(zero,0,512);
            for(uint8_t sct=0;sct<fat32_bpb.sectors_per_cluster;sct++) ata_write_sector(drive,fat32_cluster_to_lba(newcl)+sct,zero);
            cl=newcl;
        } else cl=next;
    }
}

int fat32_create_dir(uint8_t drive, const char* name){
    /* create directory entry first (similar to file) */
    char sfn[11]; make_sfn(name,sfn); uint8_t checksum=shortname_checksum(sfn);
    int namelen=strlen(name); int lcnt=(namelen+12)/13; int total=lcnt+1;
    uint8_t *buf = kmalloc(total*32); if(!buf) return -1; memset(buf,0,total*32);
    for(int seq=lcnt; seq>=1; seq--){
        int buf_idx = lcnt-seq;
        fat32_lfn_entry_t *lfn=(fat32_lfn_entry_t*)&buf[buf_idx*32];
        lfn->order = seq;
        if(seq==lcnt) lfn->order |= 0x40;
        lfn->attr=0x0F; lfn->type=0; lfn->checksum=checksum; lfn->first_cluster_low=0;
        int start=(seq-1)*13;
        for(int j=0;j<13;j++){
            uint16_t ch=0xFFFF; if(start+j<namelen) ch=(uint8_t)name[start+j];
            uint16_t *dst=(j<5)?&lfn->name1[j]:(j<11)?&lfn->name2[j-5]:&lfn->name3[j-11];
            *dst=ch;
        }
    }
    fat32_dir_entry_t *d=(fat32_dir_entry_t*)&buf[lcnt*32];
    for(int i=0;i<11;i++) d->name[i] = sfn[i];
    d->attr=0x10; /* dir */
    uint32_t newcl=find_free_cluster(drive); if(!newcl){kfree(buf);return -1;}
    d->first_cluster_high=newcl>>16; d->first_cluster_low=newcl&0xFFFF; d->file_size=0;
    /* mark cluster as end */
    fat_write_fat_entry(drive, newcl, 0x0FFFFFFF);

    /* write dir entry into current directory */
    uint32_t cl=current_dir_cluster; uint8_t sector[512];
    while(1){
        for(uint8_t sct=0;sct<fat32_bpb.sectors_per_cluster;sct++){
            uint32_t lba=fat32_cluster_to_lba(cl)+sct;
            if(ata_read_sector(drive,lba,sector)!=0){kfree(buf);return -1;}
            for(int off=0;off<=512-32*total;off+=32){
                int free_ok=1; for(int e=0;e<total;e++) if(sector[off+32*e]!=0x00 && sector[off+32*e]!=0xE5){free_ok=0;break;}
                if(free_ok){
                    for(int i=0;i<total*32;i++)
                        sector[off+i] = buf[i];
                    int end=off+total*32;
                    if(end<512) sector[end]=0x00;
                    if(ata_write_sector(drive,lba,sector)!=0){kfree(buf);return -1;}
                    kfree(buf);
                    /* init new directory cluster with '.' and '..' */
                    uint8_t dirsec[512]; memset(dirsec,0,512);
                    /* . */
                    memset(dirsec,' ',11); dirsec[0]='.'; dirsec[11]=0x10;
                    *(uint16_t*)(&dirsec[20])=(newcl>>16)&0xFFFF; *(uint16_t*)(&dirsec[26])=newcl&0xFFFF;
                    /* .. */
                    memset(&dirsec[32],' ',11); dirsec[32]='.'; dirsec[33]='.'; dirsec[43]=0x10;
                    *(uint16_t*)(&dirsec[52])=(current_dir_cluster>>16)&0xFFFF; *(uint16_t*)(&dirsec[58])=current_dir_cluster&0xFFFF;
                    for(uint8_t sc=0;sc<fat32_bpb.sectors_per_cluster;sc++) ata_write_sector(drive,fat32_cluster_to_lba(newcl)+sc,dirsec);
                return 0;
                }
                if(sector[off]==0x00){
                    memset(&sector[off],0,512-off);
                    for(int i=0;i<total*32;i++)
                        sector[off+i] = buf[i];
                    int end=off+total*32;
                    if(end<512) sector[end]=0x00;
                    if(ata_write_sector(drive,lba,sector)!=0){kfree(buf);return -1;}
                    kfree(buf);
                    /* init new dir cluster same as above */
                    uint8_t dirsec[512]; memset(dirsec,0,512);
                    memset(dirsec,' ',11); dirsec[0]='.'; dirsec[11]=0x10;
                    *(uint16_t*)(&dirsec[20])=(newcl>>16)&0xFFFF; *(uint16_t*)(&dirsec[26])=newcl&0xFFFF;
                    memset(&dirsec[32],' ',11); dirsec[32]='.'; dirsec[33]='.'; dirsec[43]=0x10;
                    *(uint16_t*)(&dirsec[52])=(current_dir_cluster>>16)&0xFFFF; *(uint16_t*)(&dirsec[58])=current_dir_cluster&0xFFFF;
                    for(uint8_t sc=0;sc<fat32_bpb.sectors_per_cluster;sc++) ata_write_sector(drive,fat32_cluster_to_lba(newcl)+sc,dirsec);
                    return 0;
                }
            }
        }
        uint32_t next=fat32_get_next_cluster(drive,cl);
        if(next>=0x0FFFFFF8){uint32_t newc=find_free_cluster(drive); if(!newc){kfree(buf);return -1;} fat_write_fat_entry(drive,cl,newc); fat_write_fat_entry(drive,newc,0x0FFFFFFF); uint8_t zero[512]; memset(zero,0,512); for(uint8_t s=0;s<fat32_bpb.sectors_per_cluster;s++) ata_write_sector(drive,fat32_cluster_to_lba(newc)+s,zero); cl=newc;}
        else cl=next;
    }
}

void fat32_create_fs(uint8_t drive) {
    uint8_t bootloader_bin_[62] = {
                0xEB, 0x21, 0x90,             /* jmp short start (to 0x21) */
                /* BPB (заполняется ниже) */
                /* offset 0x21 (start): */
                0xB8, 0x00, 0x7C,             /* mov ax,0x7C00 */
                0x8E, 0xD8,                   /* mov ds,ax */
                0xBE, 0x4E, 0x00,             /* mov si,0x4E */
                /* loop: */
                0xAC,                         /* lodsb */
                0x0C, 0x00,                   /* or al,al */
                0x74, 0x09,                   /* jz hang */
                0xB4, 0x0E,                   /* mov ah,0x0E */
                0xBB, 0x07, 0x00,             /* mov bx,0x0007 */
                0xCD, 0x10,                   /* int 0x10 */
                0xEB, 0xF3,                   /* jmp short loop */
                /* hang: */
                0xF4,                         /* hlt */
                0xEB, 0xFE,                   /* jmp $ */
                /* padding до 62 байт */
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
            };
            const char bootmsg[] = "This is not a bootable disk\r\n";
            uint8_t* sector = kmalloc(512);
            if (!sector) {
                kdbg(KERR, "fat32_createfs: error allocating memory\n");
                return;
            }
            memset(sector, 0, 512);
            // 1. Jump (3 байта)
            sector[0] = 0xEB; sector[1] = 0x3C; sector[2] = 0x90;
            // 2. BPB (с 3 по 0x3A)
            strncpy((char*)&sector[3], "MSDOS5.0", 8); // OEM
            *(uint16_t*)&sector[11] = 512; // bytes per sector
            sector[13] = 1; // sectors per cluster
            *(uint16_t*)&sector[14] = 32; // reserved sectors
            sector[16] = 2; // FAT count
            *(uint16_t*)&sector[17] = 0; // root entries (FAT32)
            *(uint16_t*)&sector[19] = 0; // total sectors 16
            sector[21] = 0xF8; // media
            *(uint16_t*)&sector[22] = 0; // FAT size 16
            *(uint16_t*)&sector[24] = 63; // sectors per track
            *(uint16_t*)&sector[26] = 255; // heads
            *(uint32_t*)&sector[28] = 0; // hidden sectors
            *(uint32_t*)&sector[32] = 65536; // total sectors 32 (пример: 32 МБ)
            *(uint32_t*)&sector[36] = 123; // FAT size 32
            *(uint16_t*)&sector[44] = 0; // ext flags
            *(uint16_t*)&sector[46] = 0; // FAT version
            *(uint32_t*)&sector[48] = 2; // root cluster
            *(uint16_t*)&sector[52] = 1; // FSInfo
            *(uint16_t*)&sector[54] = 6; // backup boot sector
            sector[64] = 0x80; // drive number
            sector[66] = 0x29; // boot signature
            *(uint32_t*)&sector[67] = 0x12345678; // volume id
            strncpy((char*)&sector[71], "HATCHER    ", 11); // volume label
            strncpy((char*)&sector[82], "FAT32   ", 8);      // fat type label
            // 3. Код загрузчика (начиная с 0x3E)
            for (int i = 0; i < (int)(sizeof(bootloader_bin_) - 3); i++)
                sector[0x3E + i] = bootloader_bin_[3 + i];
            // 4. Сообщение (начиная с 0x4E)
            for (int i = 0; i < (int)sizeof(bootmsg); i++)
                sector[0x4E + i] = bootmsg[i];
            sector[510] = 0x55; sector[511] = 0xAA;
            // Write Boot Sector
            if (ata_write_sector(drive, 0, sector) != 0) {
                kdbg(KERR, "fat32_createfs: error writing boot sector\n");
                return;
            }
            // FSInfo
            memset(sector, 0, 512);
            *(uint32_t*)&sector[0] = 0x41615252;
            *(uint32_t*)&sector[484] = 0x61417272;
            *(uint32_t*)&sector[488] = 0xFFFFFFFF;
            *(uint32_t*)&sector[492] = 0xFFFFFFFF;
            sector[510] = 0x55; sector[511] = 0xAA;
            if (ata_write_sector(drive, 1, sector) != 0) {
                kdbg(KERR, "fat32_createfs: error writing fsinfo\n");
                return;
            }
            // Clear FAT and root cluster
            memset(sector, 0, 512);
            for (int i = 0; i < 32; i++) {
                ata_write_sector(drive, 32 + i, sector); // root directory
            }
            for (int i = 0; i < 123 * 2; i++) {
                ata_write_sector(drive, 32 + 32 + i, sector); // FAT
            }
            kdbg(KINFO, "fat32_createfs: fat32 created\n");
}