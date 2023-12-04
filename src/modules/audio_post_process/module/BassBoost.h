/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef BassBoost_H
#define BassBoost_H

#include "../AudioPostProcessConfig.h"
#include "../common/biquad.h"

typedef struct {
    int sampleRate;
    int channelNum;

    float prevSample;
    float integrate;
    float harmonicLevel;
    biquadMemory biquadMem[4];
} BassBoostMemory;

void BassBoost_init(BassBoostMemory *mem, int sampleRate, int channelNum);
void BassBoost_setHarmonicLevel(BassBoostMemory *mem, float harmonicLevel);
void BassBoost_proc(BassBoostMemory *mem, int samplesPerChannel, float *io);

#endif  //  BassBoost_H