#include <ata.h>
#include <port_based.h>
#include <string.h>
#include <debug.h>
#include <vga.h>

static ata_drive_t drives[4];

static void ata_wait(uint16_t base) {
    uint8_t status; uint32_t timeout=1000000;
    do {
        status = inb(base + ATA_STATUS);
        if(--timeout==0){
            break;
        }
    } while (status & ATA_SR_BSY);
}

static int ata_check_error(uint16_t base) {
    uint8_t status = inb(base + ATA_STATUS);
    if (status & ATA_SR_ERR) return -1;
    return 0;
}

static void ata_select_drive(uint16_t base, uint8_t drive) {
    uint8_t value = 0xE0 | (drive << 4);
    outb(base + ATA_DRIVE, value);
    ata_wait(base);
}

static int ata_init_drive(uint16_t base, uint8_t drive) {
    ata_select_drive(base, drive);
    
    outb(base + ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_wait(base);
    
    // More robust check for drive presence and readiness
    uint8_t status = inb(base + ATA_STATUS);
    uint32_t timeout = 1000000; // Increased timeout
    while ((status & ATA_SR_BSY) && (--timeout != 0)) {
        status = inb(base + ATA_STATUS);
    }
    if (timeout == 0) return -1; // Timeout while waiting for BSY to clear

    if (ata_check_error(base)) return -1;

    // Check if DRQ is set, indicating data is ready to be read
    timeout = 1000000; // Reset timeout
    while (!((status & ATA_SR_DRQ) || (status & ATA_SR_ERR)) && (--timeout != 0)) {
        status = inb(base + ATA_STATUS);
    }
    if (timeout == 0 || (status & ATA_SR_ERR)) return -1; // If DRQ not set or error occurred

    uint16_t buffer[256];
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(base + ATA_DATA);
    }

    drives[drive].present = 1;
    drives[drive].type = 1; // ATA
    drives[drive].sectors = *(uint32_t*)&buffer[60];
    drives[drive].size = drives[drive].sectors * 512;

    // Correctly extract model (word 27-46, 40 bytes, swap bytes in each word)
    char model[41] = {0};
    for (int i = 0; i < 20; i++) {
        model[i*2] = (buffer[27+i] >> 8) & 0xFF; // High byte (first char)
        model[i*2+1] = buffer[27+i] & 0xFF;     // Low byte (second char)
    }
    model[40] = 0;
    trim(model);
    strncpy(drives[drive].name, model, 40);
    drives[drive].name[40] = 0;

    // Extract serial (word 10-19, 20 bytes, swap bytes in each word)
    char serial[21] = {0};
    for (int i = 0; i < 10; i++) {
        serial[i*2] = (buffer[10+i] >> 8) & 0xFF; // High byte (first char)
        serial[i*2+1] = buffer[10+i] & 0xFF;     // Low byte (second char)
    }
    serial[20] = 0;
    trim(serial);
    strncpy(drives[drive].serial, serial, 20);
    drives[drive].serial[20] = 0;

    // Vendor: first word of model (up to space or dash)
    char vendor_name[41] = {0};
    int vi = 0;
    for (int i = 0; i < 40 && model[i] != '\0'; i++) {
        if (model[i] == ' ' || model[i] == '-') break;
        vendor_name[vi++] = model[i];
    }
    vendor_name[vi] = '\0';
    // Remove trailing spaces from vendor_name
    for (int i = vi - 1; i >= 0 && vendor_name[i] == ' '; i--) {
        vendor_name[i] = '\0';
    }

    // Save serial if needed in struct (not used now)
    strncpy(drives[drive].vendor, vendor_name, 40);
    drives[drive].vendor[40] = 0;

    return 0;
}

static void read_device_info(uint16_t base, char* model) {

    outb(base + ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_wait(base);
    
    if (ata_check_error(base)) {
        strcpy(model, "Unknown");
        return;
    }

    uint16_t buffer[256];
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(base + ATA_DATA);
    }

    char* name = (char*)&buffer[27];
    for (int i = 0; i < 20; i++) {
        model[i*2] = name[i*2+1];
        model[i*2+1] = name[i*2];
    }
    model[40] = '\0';

    int len = strlen(model);
    while (len > 0 && model[len-1] == ' ') {
        model[len-1] = '\0';
        len--;
    }
}

static int ata_identify_device(uint16_t base, uint8_t drive, char* model) {
    outb(base + ATA_DRIVE, 0xA0 | (drive << 4));
    inb(base + ATA_STATUS);
    for (int i = 0; i < 1000; i++) {
        if (!(inb(base + ATA_STATUS) & 0x80)) break;
    }
    outb(base + ATA_COMMAND, 0xEC);
    uint8_t status = 0;
    for (int i = 0; i < 1000; i++) {
        status = inb(base + ATA_STATUS);
        if (!(status & 0x80) && (status & 0x08)) break;
    }
    if (!(status & 0x08)) return -1;

    uint16_t buffer[256];
    for (int i = 0; i < 256; i++) buffer[i] = inw(base + ATA_DATA);

    for (int i = 0; i < 20; i++) {
        model[i*2] = buffer[27+i] >> 8;
        model[i*2+1] = buffer[27+i] & 0xFF;
    }
    model[40] = 0;
    for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;
    return 0;
}

void ata_init() {
    memset(drives, 0, sizeof(drives));
    kdbg(KINFO, "ata_init\n");
    if (ata_init_drive(ATA_PRIMARY_BASE, 0) == 0) {
        kdbg(KINFO, "ata 0: %s, ven: %s, ser: %s, sec: %u\n", 
            drives[0].name, drives[0].vendor, drives[0].serial, drives[0].sectors);
    }
    
    if (ata_init_drive(ATA_PRIMARY_BASE, 1) == 0) {
        kdbg(KINFO, "ata 1: %s, ven: %s, ser: %s, sec: %u\n", 
            drives[1].name, drives[1].vendor, drives[1].serial, drives[1].sectors);
    }
    
    if (ata_init_drive(ATA_SECONDARY_BASE, 0) == 0) {
        kdbg(KINFO, "ata 2: %s, ven: %s, ser: %s, sec: %u\n", 
            drives[2].name, drives[2].vendor, drives[2].serial, drives[2].sectors);
    }
    
    if (ata_init_drive(ATA_SECONDARY_BASE, 1) == 0) {
        kdbg(KINFO, "ata 3: %s, ven: %s, ser: %s, sec: %u\n", 
            drives[3].name, drives[3].vendor, drives[3].serial, drives[3].sectors);
    }
}

int ata_read_sector(uint8_t drive, uint32_t lba, uint8_t* buffer) {
    if (drive >= 4 || !drives[drive].present) return -1;
    uint16_t base = (drive < 2) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
    drive = drive % 2;

    ata_select_drive(base, drive);

    outb(base + ATA_SECTOR_COUNT, 1);
    outb(base + ATA_SECTOR_NUM, lba & 0xFF);
    outb(base + ATA_CYL_LOW, (lba >> 8) & 0xFF);
    outb(base + ATA_CYL_HIGH, (lba >> 16) & 0xFF);
    outb(base + ATA_DRIVE, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));

    outb(base + ATA_COMMAND, ATA_CMD_READ);
    ata_wait(base);

    if (ata_check_error(base)) return -1;

    __asm__ volatile (
        "rep insw"
        : "+D"(buffer)
        : "d"(base + ATA_DATA), "c"(256)
        : "memory"
    );

    return 0;
}

int ata_write_sector(uint8_t drive, uint32_t lba, uint8_t* buffer) {
    if (drive >= 4 || !drives[drive].present) return -1;
    uint16_t base = (drive < 2) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
    uint8_t head = drive % 2;
    ata_select_drive(base, head);
    outb(base + ATA_SECTOR_COUNT, 1);
    outb(base + ATA_SECTOR_NUM, lba & 0xFF);
    outb(base + ATA_CYL_LOW, (lba >> 8) & 0xFF);
    outb(base + ATA_CYL_HIGH, (lba >> 16) & 0xFF);
    outb(base + ATA_DRIVE, 0xE0 | (head << 4) | ((lba >> 24) & 0x0F));
    outb(base + ATA_COMMAND, ATA_CMD_WRITE);
    uint8_t status;
    uint32_t timeout = 1000000;

    do { status = inb(base + ATA_STATUS); } while (((status & ATA_SR_BSY) || !(status & ATA_SR_DRQ)) && --timeout);

    if (timeout == 0 || (status & ATA_SR_ERR)) {
        return -1;
    }
    for (int i = 0; i < 256; i++) {
        uint16_t data = buffer[i*2] | (buffer[i*2+1] << 8);
        outw(base + ATA_DATA, data);
    }
    timeout = 1000000;

    do { status = inb(base + ATA_STATUS); } while ((status & ATA_SR_BSY) && --timeout);
    if (timeout == 0 || (status & ATA_SR_ERR)) {
        return -1;
    }
    return 0;
}

ata_drive_t* ata_get_drive(uint8_t drive) {
    if (drive >= 4 || !drives[drive].present) return NULL;
    return &drives[drive];
} 