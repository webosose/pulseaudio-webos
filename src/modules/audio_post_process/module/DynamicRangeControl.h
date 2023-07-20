/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef DynamicRangeControl_H
#define DynamicRangeControl_H

typedef struct {
    float sampleRate;
    int channelNum;

    float targetGain;
    float peakLimit;

    int attack;
    int attackCount;
    float releaseRate;
    
    float gain;
} DynamicRangeControlMemory;

void DynamicRangeControl_init(DynamicRangeControlMemory *mem, int sampleRate, int channelNum);
void DynamicRangeControl_update(DynamicRangeControlMemory *mem, float gainDB, float limitDB, float attackMs, float releaseMs);
void DynamicRangeControl_proc(DynamicRangeControlMemory *mem, int samplesPerChannel, float *io);

#endif  // DynamicRangeControl_H