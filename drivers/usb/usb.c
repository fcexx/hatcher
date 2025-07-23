#include <usb.h>
#include <debug.h>
#include <pci.h>

void usb_init() {
    kdbg(KINFO, "USB subsystem initialized.\n");
}

void usb_handle_pci_device(uint8_t bus, uint8_t device, uint8_t function, uint8_t class_code, uint8_t subclass, uint8_t prog_if, uint32_t bar0) {
    // USB Host Controller Interface (EHCI, OHCI, UHCI, XHCI)
    if (class_code == 0x0C && subclass == 0x03) { // Serial Bus Controller -> USB
        kdbg(KINFO, "Found USB Host Controller: bus=%x device=%x function=%x (Class: %x, Subclass: %x, ProgIF: %x, BAR0: %x)\n",
            bus, device, function, class_code, subclass, prog_if, bar0);
        
        // TODO: Implement specific controller initialization (UHCI/OHCI/EHCI/XHCI)
        if (prog_if == 0x00) {
            kdbg(KINFO, "  -> UHCI Controller\n");
        } else if (prog_if == 0x10) {
            kdbg(KINFO, "  -> OHCI Controller\n");
        } else if (prog_if == 0x20) {
            kdbg(KINFO, "  -> EHCI Controller\n");
        } else if (prog_if == 0x30) {
            kdbg(KINFO, "  -> XHCI Controller\n");
        } else {
            kdbg(KINFO, "  -> Unknown USB Controller\n");
        }
    }
} 