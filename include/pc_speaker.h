#ifndef PC_SPEAKER_H
#define PC_SPEAKER_H

#include <stdint.h>

void pc_speaker_play_sound(uint32_t frequency);
void pc_speaker_nosound();
void pc_speaker_beep(uint32_t frequency, uint32_t duration);

#endif // PC_SPEAKER_H 