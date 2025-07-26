#ifndef GPU_H
#define GPU_H
#include <stdint.h>

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus, slot, func;
    uint64_t mmio_base;
    uint32_t mmio_size;
    char name[64];
} gpu_info_t;

int gpu_init(gpu_info_t* out_gpu);
void gpu_print_info(const gpu_info_t* gpu);
uint32_t gpu_mmio_read32(uint64_t base, uint32_t offset);
void gpu_mmio_write32(uint64_t base, uint32_t offset, uint32_t value);

int gpu_set_video_mode(uint64_t mmio_base, int width, int height, int bpp);
int gpu_get_video_mode(uint64_t mmio_base, int* width, int* height, int* bpp);
void gpu_set_pixel(uint64_t mmio_base, int x, int y, uint32_t color);

#endif // GPU_H 