#include <pci.h>
#include <port_based.h>
#include <vga.h>
#include <debug.h>
#include <usb.h>

static uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (1U << 31)
        | ((uint32_t)bus << 16)
        | ((uint32_t)device << 11)
        | ((uint32_t)function << 8)
        | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, device, function, offset);
    return (value >> ((offset & 2) * 8)) & 0xFFFF;
}

static uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, device, function, offset);
    return (value >> ((offset & 3) * 8)) & 0xFF;
}

static void pci_print_device(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor_id = pci_config_read16(bus, device, function, 0x00);
    if (vendor_id == 0xFFFF) return;
    uint16_t device_id = pci_config_read16(bus, device, function, 0x02);
    uint8_t class_code = pci_config_read8(bus, device, function, 0x0B);
    uint8_t subclass = pci_config_read8(bus, device, function, 0x0A);
    uint8_t prog_if = pci_config_read8(bus, device, function, 0x09);
    uint8_t revision_id = pci_config_read8(bus, device, function, 0x08);
    kdbg(KINFO, "pci: %04x:%04x bus=%04x func=%04x cl=%04x subcl=%04x\n", vendor_id, device_id, bus, function, class_code, subclass);

    // Handle USB Host Controllers
    if (class_code == 0x0C && subclass == 0x03) { // Serial Bus Controller -> USB
        uint32_t bar0 = pci_config_read32(bus, device, function, 0x10);
        kdbg(KINFO, "  BAR0: 0x%x\n", bar0);
        usb_handle_pci_device(bus, device, function, class_code, subclass, prog_if, bar0);
    }
}

void pci_init() {
    for (uint8_t bus = 0; bus < 2; ++bus) {
        for (uint8_t device = 0; device < 32; ++device) {
            uint16_t vendor_id = pci_config_read16(bus, device, 0, 0x00);
            if (vendor_id == 0xFFFF) continue;
            uint8_t header_type = pci_config_read8(bus, device, 0, 0x0E);
            if (header_type & 0x80) {
                for (uint8_t function = 0; function < 8; ++function) {
                    vendor_id = pci_config_read16(bus, device, function, 0x00);
                    if (vendor_id == 0xFFFF) continue;
                    pci_print_device(bus, device, function);
                }
            } else {
                pci_print_device(bus, device, 0);
            }
        }
    }
}