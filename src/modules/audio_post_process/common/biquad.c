/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#define _USE_MATH_DEFINES
#include <math.h>

#include "biquad.h"

void biquad_init(biquadMemory *mem, int sampleRate) {
    mem->sampleRate = (float) sampleRate;
    mem->state[0] = 0;
    mem->state[1] = 0;

    biquad_setCoeff(mem, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F);
}

void biquad_setFilter(biquadMemory *mem, FilterType filterType, float frequency, float qFactor, float gainDB) {
    float freq = frequency / (mem->sampleRate * 0.5f);
    float alpha = sinf(M_PI * freq) / (2.0f * qFactor);
    float cos_w0 = cosf(M_PI * freq);
    float A, a0_inv, beta;

    switch (filterType) {
        case LOW_PASS_FILTER:
            a0_inv = 1.0f / (1.0f + alpha);
            mem->b0 = 0.5f * (1.0f - cos_w0) * a0_inv;
            mem->b1 = (1.0f - cos_w0) * a0_inv;
            mem->b2 = mem->b0;
            mem->a1 = (-2.0f * cos_w0) * a0_inv;
            mem->a2 = (1.0f - alpha) * a0_inv;
            break;
        case HIGH_PASS_FILTER:
            a0_inv = 1.0f / (1.0f + alpha);
            mem->b0 = 0.5f * (1.0f + cos_w0) * a0_inv;
            mem->b1 = (-1.0f - cos_w0) * a0_inv;
            mem->b2 = mem->b0;
            mem->a1 = (-2.0f * cos_w0) * a0_inv;
            mem->a2 = (1.0f - alpha) * a0_inv;
            break;
        case BAND_PASS_FILTER:
            a0_inv = 1.0f / (1.0f + alpha);
            mem->b0 = alpha * a0_inv;
            mem->b1 = 0.0f;
            mem->b2 = -mem->b0;
            mem->a1 = (-2.0f * cos_w0) * a0_inv;
            mem->a2 = (1.0f - alpha) * a0_inv;
            break;
        case NOTCH_FILTER:
            a0_inv = 1.0f / (1.0f + alpha);
            mem->b0 = a0_inv;
            mem->b1 = (-2.0f * cos_w0) * a0_inv;
            mem->b2 = -mem->b0;
            mem->a1 = mem->b1;
            mem->a2 = (1.0f - alpha) * a0_inv;
            break;
        case ALL_PASS_FILTER:
            a0_inv = 1.0f / (1.0f + alpha);
            mem->b0 = (1.0f - alpha) * a0_inv;
            mem->b1 = (-2.0f * cos_w0) * a0_inv;
            mem->b2 = 1.0f;
            mem->a1 = mem->b1;
            mem->a2 = mem->b0;
            break;
        case PEAKING_EQ_FILTER:
            A = powf(10.0F, gainDB * 0.025f);
            a0_inv = 1.0f / (1.0f + alpha / A);
            mem->b0 = (1.0f + alpha * A) * a0_inv;
            mem->b1 = (-2.0f * cos_w0) * a0_inv;
            mem->b2 = (1.0f - alpha * A) * a0_inv;
            mem->a1 = mem->b1;
            mem->a2 = (1.0f - alpha / A) * a0_inv;
            break;
        case LOW_SHELF_FILTER:
            A = powf(10.0F, gainDB * 0.025f);
            beta = 2.0f * sqrtf(A) * alpha;
            a0_inv = 1 / ((A + 1.0f) + (A - 1.0f) * cos_w0 + beta);
            mem->b0 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + beta) * a0_inv;
            mem->b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0) * a0_inv;
            mem->b2 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - beta) * a0_inv;
            mem->a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0) * a0_inv;
            mem->a2 = ((A + 1.0f) + (A - 1.0f) * cos_w0 - beta) * a0_inv;
            break;
        case HIGH_SHELF_FILTER:
            A = powf(10.0F, gainDB * 0.025f);
            beta = 2.0f * sqrtf(A) * alpha;
            a0_inv = 1 / ((A + 1.0f) - (A - 1.0f) * cos_w0 + beta);
            mem->b0 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + beta) * a0_inv;
            mem->b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0) * a0_inv;
            mem->b2 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - beta) * a0_inv;
            mem->a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0) * a0_inv;
            mem->a2 = ((A + 1.0f) - (A - 1.0f) * cos_w0 - beta) * a0_inv;
            break;
        default:
            mem->b0 = 1.0f;
            mem->b1 = 0.0f;
            mem->b2 = 0.0f;
            mem->a1 = 0.0f;
            mem->a2 = 0.0f;
            break;
    }
}

void biquad_setCoeff(biquadMemory *mem, float b0, float b1, float b2, float a1, float a2) {
    mem->b0 = b0;
    mem->b1 = b1;
    mem->b2 = b2;
    mem->a1 = a1;
    mem->a2 = a2;
}

// float biquad_proc(biquadMemory *mem, float sample) {
//     //  direct form 1
//     float filtered = mem->b0 * sample +
//                     mem->b1 * mem->state[0] +
//                     mem->b2 * mem->state[1] -
//                     mem->a1 * mem->state[2] -
//                     mem->a2 * mem->state[3];

//     mem->state[3] = mem->state[2];
//     mem->state[2] = filtered;
//     mem->state[1] = mem->state[0];
//     mem->state[0] = sample;

//     return filtered;
// }

float biquad_proc(biquadMemory *mem, float sample) {
    //  transposed direct form 2
    float filtered = mem->b0 * sample + mem->state[0];
    mem->state[0] = mem->state[1] + mem->b1 * sample - mem->a1 * filtered;
    mem->state[1] = mem->b2 * sample - mem->a2 * filtered;
    return filtered;
}
