#include <gpu.h>
#include <pci.h>
#include <string.h>
#include <vga.h>
#include <debug.h>

uint32_t gpu_mmio_read32(uint64_t base, uint32_t offset) {
    volatile uint32_t* ptr = (volatile uint32_t*)(base + offset);
    return *ptr;
}

void gpu_mmio_write32(uint64_t base, uint32_t offset, uint32_t value) {
    volatile uint32_t* ptr = (volatile uint32_t*)(base + offset);
    *ptr = value;
}

static void get_gpu_name_from_registers(uint64_t mmio_base, uint16_t vendor_id, uint16_t device_id, char* name, size_t name_size) {
    uint32_t gpu_id1 = gpu_mmio_read32(mmio_base, 0x0000);
    uint32_t gpu_id2 = gpu_mmio_read32(mmio_base, 0x0004);
    uint32_t gpu_id3 = gpu_mmio_read32(mmio_base, 0x0008);
    uint32_t gpu_id4 = gpu_mmio_read32(mmio_base, 0x000C);

    uint32_t model_reg1 = gpu_mmio_read32(mmio_base, 0x1000);
    uint32_t model_reg2 = gpu_mmio_read32(mmio_base, 0x1004);
    uint32_t model_reg3 = gpu_mmio_read32(mmio_base, 0x1008);
    uint32_t model_reg4 = gpu_mmio_read32(mmio_base, 0x100C);

    uint32_t version_reg = gpu_mmio_read32(mmio_base, 0x2000);
    uint32_t revision_reg = gpu_mmio_read32(mmio_base, 0x2004);

    uint32_t mem_reg1 = gpu_mmio_read32(mmio_base, 0x3000);
    uint32_t mem_reg2 = gpu_mmio_read32(mmio_base, 0x3004);

    uint32_t config_reg1 = gpu_mmio_read32(mmio_base, 0x4000);
    uint32_t config_reg2 = gpu_mmio_read32(mmio_base, 0x4004);

    uint32_t svga_reg1 = gpu_mmio_read32(mmio_base, 0x0000);
    uint32_t svga_reg2 = gpu_mmio_read32(mmio_base, 0x0004);
    uint32_t svga_reg3 = gpu_mmio_read32(mmio_base, 0x0008);

    uint32_t qemu_reg1 = gpu_mmio_read32(mmio_base, 0x3C0);
    uint32_t qemu_reg2 = gpu_mmio_read32(mmio_base, 0x3C4);
    uint32_t qemu_reg3 = gpu_mmio_read32(mmio_base, 0x3C8);

    char vendor_name[32] = "Unknown";
    char model_name[32] = "GPU";
    char series_name[32] = "";

    if (vendor_id == 0x1234 && device_id == 0x1111) {
        strcpy(vendor_name, "QEMU");
        strcpy(model_name, "VGA");
        strcpy(series_name, "Bochs");
    } else if (vendor_id == 0x15AD) {
        strcpy(vendor_name, "VMware");
        strcpy(model_name, "SVGA");
        strcpy(series_name, "3D");
    }

    else if (svga_reg1 == 0x90000000 || svga_reg2 == 0x90000000 || 
        (svga_reg1 & 0xFFFF0000) == 0x15AD0000 || (svga_reg2 & 0xFFFF0000) == 0x15AD0000) {
        strcpy(vendor_name, "VMware");
        strcpy(model_name, "SVGA");
        strcpy(series_name, "3D");
    }

    else if (qemu_reg1 == 0x11111234 || qemu_reg2 == 0x11111234 ||
             (gpu_id1 & 0xFFFF) == 0x1234 || (gpu_id2 & 0xFFFF) == 0x1111 ||
             (model_reg1 & 0xFFFF) == 0x1234 || (model_reg2 & 0xFFFF) == 0x1111) {
        strcpy(vendor_name, "QEMU");
        strcpy(model_name, "VGA");
        strcpy(series_name, "Bochs");
    }
    else if ((gpu_id1 & 0xFF000000) == 0x10000000 || (model_reg1 & 0xFF000000) == 0x10000000) {
        strcpy(vendor_name, "NVIDIA");
        strcpy(model_name, "GeForce");

        uint32_t series_id = (model_reg2 >> 16) & 0xFFFF;
        if (series_id >= 0x2000 && series_id <= 0x2FFF) {
            strcpy(series_name, "RTX 4000");
        } else if (series_id >= 0x3000 && series_id <= 0x3FFF) {
            strcpy(series_name, "RTX 3000");
        } else if (series_id >= 0x4000 && series_id <= 0x4FFF) {
            strcpy(series_name, "GTX 1600");
        } else if (series_id >= 0x5000 && series_id <= 0x5FFF) {
            strcpy(series_name, "GTX 1000");
        } else {
            snprintf(series_name, sizeof(series_name), "%04X", series_id);
        }
    } else if ((gpu_id1 & 0xFF000000) == 0x20000000 || (model_reg1 & 0xFF000000) == 0x20000000) {
        strcpy(vendor_name, "AMD");
        strcpy(model_name, "Radeon");

        uint32_t series_id = (model_reg2 >> 16) & 0xFFFF;
        if (series_id >= 0x6000 && series_id <= 0x6FFF) {
            strcpy(series_name, "RX 6000");
        } else if (series_id >= 0x5000 && series_id <= 0x5FFF) {
            strcpy(series_name, "RX 5000");
        } else {
            snprintf(series_name, sizeof(series_name), "%04X", series_id);
        }
    } else if ((gpu_id1 & 0xFF000000) == 0x30000000 || (model_reg1 & 0xFF000000) == 0x30000000) {
        strcpy(vendor_name, "Intel");
        strcpy(model_name, "Graphics");

        uint32_t series_id = (model_reg2 >> 16) & 0xFFFF;
        if (series_id >= 0x4000 && series_id <= 0x4FFF) {
            strcpy(series_name, "Arc A");
        } else if (series_id >= 0x3000 && series_id <= 0x3FFF) {
            strcpy(series_name, "UHD");
        } else {
            snprintf(series_name, sizeof(series_name), "%04X", series_id);
        }
    }

    if (strlen(series_name) > 0) {
        snprintf(name, name_size, "%s %s %s", vendor_name, model_name, series_name);
    } else {
        snprintf(name, name_size, "%s %s %08X", vendor_name, model_name, gpu_id2);
    }
}

int gpu_init(gpu_info_t* out_gpu) {
    for (uint8_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t vendor = pci_config_read16(bus, slot, func, 0x00);
                if (vendor == 0xFFFF) continue;
                uint16_t device = pci_config_read16(bus, slot, func, 0x02);
                uint8_t class = pci_config_read8(bus, slot, func, 0x0B);
                uint8_t subclass = pci_config_read8(bus, slot, func, 0x0A);
                if (class == 0x03) { // VGA/Display controller
                    memset(out_gpu, 0, sizeof(gpu_info_t));
                    out_gpu->vendor_id = vendor;
                    out_gpu->device_id = device;
                    out_gpu->bus = bus;
                    out_gpu->slot = slot;
                    out_gpu->func = func;

                    uint32_t bar0_low = pci_config_read32(bus, slot, func, 0x10);
                    out_gpu->mmio_base = (uint64_t)(bar0_low & ~0xF);

                    uint32_t bar1 = pci_config_read32(bus, slot, func, 0x14);
                    if ((bar0_low & 0x6) == 0x4) {
                        out_gpu->mmio_base |= ((uint64_t)bar1 << 32);
                    }

                    get_gpu_name_from_registers(out_gpu->mmio_base, out_gpu->vendor_id, out_gpu->device_id, out_gpu->name, sizeof(out_gpu->name));
                    
                    return 0;
                }
            }
        }
    }
    return -1;
}

void gpu_print_info(const gpu_info_t* gpu) {
    kdbg(KINFO, "found gpu: %s, mmio: 0x016x\n", gpu->name, gpu->mmio_base);
}