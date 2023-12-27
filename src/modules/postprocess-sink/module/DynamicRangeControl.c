/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#include "DynamicRangeControl.h"
#include <math.h>

void DynamicRangeControl_init(DynamicRangeControlMemory *mem, int sampleRate, int channelNum) {
    mem->sampleRate = (float) sampleRate;
    mem->channelNum = channelNum;
    mem->gain = 1.0F;

    DynamicRangeControl_update(mem, 0.0F, -0.1F, 100.0F, 1000.0F);
}

void DynamicRangeControl_update(DynamicRangeControlMemory *mem, float gainDB, float limitDB, float attackMs, float releaseMs) {
    mem->targetGain = powf(10, (gainDB / 20));
    mem->peakLimit = powf(10, (limitDB / 20));

    mem->attack = (int) (mem->sampleRate * attackMs * 0.001);
    mem->attackCount = mem->attack;
    mem->releaseRate = 5 / (mem->sampleRate * releaseMs * 0.001);
}

void DynamicRangeControl_proc(DynamicRangeControlMemory *mem, int samplesPerChannel, float *io) {
    float sample, margin;
    
    for (int i = 0; i < samplesPerChannel; i++) {
        //  pick max in sample block
        sample = 0;
        for (int ch = 0; ch < mem->channelNum; ch++) {
            if (sample < fabsf(io[i * mem->channelNum + ch])) {
                sample = fabsf(io[i * mem->channelNum + ch]);
            }
        }

        if (sample == 0) {
            margin = 65536.0F;     //  about 96 dB
        } else {
            margin = fabsf(mem->peakLimit / sample);
        }

        if (mem->gain > margin) {
            //  attck: decrease the gain
            mem->gain = margin;
            mem->attackCount = mem->attack;
        } else {
            if (mem->gain < mem->targetGain) {
                if (mem->attackCount > 0) {
                    //  hold: keep the gain same
                    mem->attackCount--;
                } else {
                    //  release: increase gain
                    mem->gain = mem->gain + (mem->targetGain - mem->gain) * mem->releaseRate;
                }
            } else {
                //  set to target gain
                mem->gain = mem->targetGain;
            }
        }
        
        //  apply gain on sample block
        for (int ch = 0;  ch < mem->channelNum; ch++) {
            io[i * mem->channelNum + ch] *= mem->gain;
        }
    }
}
