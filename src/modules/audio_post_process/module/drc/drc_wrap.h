/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef DRC_WRAP_H
#define DRC_WRAP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "snd.h"
#include "compressor.h"

//		Parameter Structure
typedef struct {
    int sampleRate;

    float inBuf[SF_COMPRESSOR_SPU * 2];
    float outBuf[SF_COMPRESSOR_SPU * 2];
    int inIdx;
    int outIdx;

    sf_compressor_state_st state;

    float pregain;
    float threshold;
    float knee;
    float ratio;
    float attack;
    float release;
    float predelay;
    float releasezone1;
    float releasezone2;
    float releasezone3;
    float releasezone4;
    float postgain;
    float wet;
} SndDrcMemory;

void snd_drc_init(SndDrcMemory *mem, const char *filePath, int sampleRate);
void snd_drc_process(SndDrcMemory *mem, int samplesPerChannels, float *in, float *out);

#endif  //  DRC_WRAP_H