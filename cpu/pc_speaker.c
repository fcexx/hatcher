#include <stdint.h>
#include <stdbool.h>
#include <port_based.h>
#include <pc_speaker.h>
#include <timer.h>
#include <thread.h>

#define PIT_FREQUENCY 1193180

#define PIT_CHANNEL_2_DATA_PORT 0x42
#define PIT_COMMAND_PORT        0x43
#define KEYBOARD_CONTROLLER_PORT    0x61

void pc_speaker_play_sound(uint32_t frequency) {
    if (frequency == 0) {
        pc_speaker_nosound();
        return;
    }

    uint32_t divisor = PIT_FREQUENCY / frequency;

    outb(PIT_COMMAND_PORT, 0xB6);

    outb(PIT_CHANNEL_2_DATA_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL_2_DATA_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t tmp = inb(KEYBOARD_CONTROLLER_PORT);
    if (tmp != (tmp | 3)) {
        outb(KEYBOARD_CONTROLLER_PORT, tmp | 3);
    }
}

void pc_speaker_nosound() {
    uint8_t tmp = inb(KEYBOARD_CONTROLLER_PORT) & 0xFC;
    outb(KEYBOARD_CONTROLLER_PORT, tmp);
}

void pc_speaker_beep(uint32_t frequency, uint32_t duration) {
    pc_speaker_play_sound(frequency);
    wait(duration);
    pc_speaker_nosound();
} 