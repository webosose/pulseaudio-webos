/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#include "Equalizer.h"

void Equalizer_init(EqualizerMemory *mem, int sampleRate, int channelNum) {
    mem->sampleRate = sampleRate;
    mem->channelNum = channelNum;

    for (int i = 0; i < EQUALIZER_BANDS; i++) {
        mem->bandFrequency[i] = equalizer_frequency[i];
        mem->bandGain[i] = 0;
        for (int ch = 0; ch < channelNum; ch++) {
            biquad_init(&mem->biquadMem[ch][i], sampleRate);
        }
    }
}

void Equalizer_setBandLevel(EqualizerMemory *mem, int band, float level) {
    if (band < 0 || band >= EQUALIZER_BANDS) return;

    for (int ch = 0; ch < mem->channelNum; ch++) {
        biquad_setFilter(&mem->biquadMem[ch][band], PEAKING_EQ_FILTER, mem->bandFrequency[band], 1.0F, level);
    }
}

void Equalizer_setPreset(EqualizerMemory *mem, EqualizerPreset preset) {
    if (preset < 0 || preset >= PRESET_MAX) return;

    for (int i = 0; i < EQUALIZER_BANDS; i++) {
        Equalizer_setBandLevel(mem, i, equalizer_presets[preset][i]);
    }
}

void Equalizer_proc(EqualizerMemory *mem, int samplesPerChannel, float *io) {
    float sample;

    //  biquad equalizer for each samples, each channels, each bands
    for (int i = 0; i < samplesPerChannel; i++) {
        for (int ch = 0; ch < mem->channelNum; ch++) {
            sample = io[i * mem->channelNum + ch];

            for (int band = 0; band < EQUALIZER_BANDS; band++) {
                sample = biquad_proc(&mem->biquadMem[ch][band], sample);
            }

            io[i * mem->channelNum + ch] = sample;
        }
    }
}
