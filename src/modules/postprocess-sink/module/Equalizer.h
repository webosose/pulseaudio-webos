/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef Equalizer_H
#define Equalizer_H

#include "../AudioPostProcessConfig.h"
#include "../common/biquad.h"

typedef struct {
    int sampleRate;
    int channelNum;

    float bandFrequency[EQUALIZER_BANDS];
    float bandGain[EQUALIZER_BANDS];

    biquadMemory biquadMem[APP_MAX_CHANNEL][EQUALIZER_BANDS];
} EqualizerMemory;

void Equalizer_init(EqualizerMemory *mem, int sampleRate, int channelNum);
void Equalizer_setBandLevel(EqualizerMemory *mem, int band, float level);
void Equalizer_setPreset(EqualizerMemory *mem, EqualizerPreset preset);
void Equalizer_proc(EqualizerMemory *mem, int samplesPerChannel, float *io);

#endif  //  Equalizer_H