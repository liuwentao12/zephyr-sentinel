#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <stdint.h>

#define SPECTRUM_MAX_BANDS 32

struct audio_features {
    float rms;
    float peak_frequency_hz;
    uint8_t bands[SPECTRUM_MAX_BANDS];
};

#endif