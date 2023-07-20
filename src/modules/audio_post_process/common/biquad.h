/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef biquad_H
#define biquad_H

typedef struct {
    float sampleRate;
    float b0, b1, b2;
    float a1, a2;
    float state[4];
} biquadMemory;

typedef enum FilterType {
    LOW_PASS_FILTER,
    HIGH_PASS_FILTER,
    BAND_PASS_FILTER,
    NOTCH_FILTER,
    ALL_PASS_FILTER,
    PEAKING_EQ_FILTER,
    LOW_SHELF_FILTER,
    HIGH_SHELF_FILTER,
} FilterType;

void biquad_init(biquadMemory *mem, int sampleRate);
void biquad_setFilter(biquadMemory *mem, FilterType filterType, float frequency, float qFactor, float gainDB);
void biquad_setCoeff(biquadMemory *mem, float b0, float b1, float b2, float a1, float a2);
float biquad_proc(biquadMemory *mem, float sample);

#endif  // biquad_H