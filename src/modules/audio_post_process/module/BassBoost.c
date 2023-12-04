/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#include "BassBoost.h"

void BassBoost_init(BassBoostMemory *mem, int sampleRate, int channelNum) {
    mem->sampleRate = sampleRate;
    mem->channelNum = channelNum;

    biquad_init(&mem->biquadMem[0], sampleRate);
    biquad_init(&mem->biquadMem[1], sampleRate);
    biquad_init(&mem->biquadMem[2], sampleRate);
    biquad_init(&mem->biquadMem[3], sampleRate);
    biquad_setFilter(&mem->biquadMem[0], LOW_PASS_FILTER, BASS_BOOST_CUTOFF_FREQ, 1, 0);
    biquad_setFilter(&mem->biquadMem[1], HIGH_SHELF_FILTER, BASS_BOOST_CUTOFF_FREQ * 10, 0.3F, -18);
    biquad_setFilter(&mem->biquadMem[2], HIGH_PASS_FILTER, 10.0F, 1, 0);
    biquad_setFilter(&mem->biquadMem[3], LOW_PASS_FILTER, BASS_BOOST_CUTOFF_FREQ * 10, 1, 0);

    BassBoost_setHarmonicLevel(mem, 1.0F);

    mem->prevSample = -1;
    mem->integrate = 0;
}

void BassBoost_setHarmonicLevel(BassBoostMemory *mem, float harmonicLevel) {
    if (harmonicLevel < 0.0F || harmonicLevel > 3.0F) return;
    mem->harmonicLevel = harmonicLevel;
}

void BassBoost_proc(BassBoostMemory *mem, int samplesPerChannel, float *io) {
    float mono, bass, even, odd, harmonic;

    for (int i = 0; i < samplesPerChannel; i++) {
        //  extract mono signal
        mono = 0;
        for (int ch = 0; ch < mem->channelNum; ch++) {
            mono += io[i * mem->channelNum + ch];
        }
        mono /= mem->channelNum;

        //  extract bass signal
        bass = biquad_proc(&mem->biquadMem[0], mono);

        //  Full wave integrator
        // if (mem->prevSample < 0 && bass >= 0) {
        //     mem->integrate = 0;
        // }
        // mem->integrate += fabs(bass);
        // mem->prevSample = bass;
        // harmonic = mem->integrate * 0.0005;
        // harmonic = biquad_proc(&mem->biquadMem[1], harmonic);

        //  generate harmonic signal
        if (bass > 0) {
            even = bass - bass * bass;
            odd = bass * 0.0F;
        } else {
            even = bass + bass * bass;
            odd = bass;
        }
        even = 0;
        odd = biquad_proc(&mem->biquadMem[1], odd);
        harmonic = even + odd;

        //  bass enhancement level
        harmonic += bass * 0.5;
        harmonic = harmonic * mem->harmonicLevel;

        //  high pass filter for remove DC
        harmonic = biquad_proc(&mem->biquadMem[2], harmonic);
        harmonic = biquad_proc(&mem->biquadMem[3], harmonic);

        //  add harmonic signal
        for (int ch = 0; ch < mem->channelNum; ch++) {
            io[i * mem->channelNum + ch] += harmonic;
        }
    }
}
