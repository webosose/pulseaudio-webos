/***
  This file is part of PulseAudio.
  Copyright (c) 2013-2018 LG Electronics, Inc.
  All rights reserved.

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

/**********************************************************************
 * palm-resampler.c - palm resampler with pre generated filter coefficients.
 **********************************************************************/

#include "palm-resampler.h"

#ifdef __ARM_NEON__

#include <arm_neon.h>
int16_t fir_simd(int16_t *x, int16_t *h, unsigned taps)
{
    int32_t sum;
    int16x4_t h_vec, x_vec;
    int32x4_t result_vec;
    /* Clear the scalar and vector sums */
    result_vec = vdupq_n_s32(0);

    h_vec = vld1_s16(h);
    x_vec = vld1_s16(x);
    result_vec = vmlal_s16(result_vec, h_vec, x_vec);

    h_vec = vld1_s16(h + 4);
    x_vec = vld1_s16(x + 4);
    result_vec = vmlal_s16(result_vec, h_vec, x_vec);

    h_vec = vld1_s16(h + 8);
    x_vec = vld1_s16(x + 8);
    result_vec = vmlal_s16(result_vec, h_vec, x_vec);

    h_vec = vld1_s16(h + 12);
    x_vec = vld1_s16(x + 12);
    result_vec = vmlal_s16(result_vec, h_vec, x_vec);

    h_vec = vld1_s16(h + 16);
    x_vec = vld1_s16(x + 16);
    result_vec = vmlal_s16(result_vec, h_vec, x_vec);

    h_vec = vld1_s16(h + 20);
    x_vec = vld1_s16(x + 20);
    result_vec = vmlal_s16(result_vec, h_vec, x_vec);

    /* Reduction operation - add each vector lane result to the sum */
    sum = vgetq_lane_s32(result_vec, 0);
    sum += vgetq_lane_s32(result_vec, 1);
    sum += vgetq_lane_s32(result_vec, 2);
    sum += vgetq_lane_s32(result_vec, 3);

    /* consume the last few data using scalar operations */
    if(taps > 24) {
        h += 24;
        x += 24;
        sum += *h++ * *x++;
        sum += *h++ * *x++;
        sum += *h++ * *x++;
        sum += *h * *x;
    }

    sum >>= 15;
    if(sum > 32767)
        sum = 32767;
    else if(sum < -32768)
        sum = -32768;

    return sum;

}

#else
int16_t fir_unroll(int16_t *x, int16_t *h, unsigned taps) {
    int32_t sum;

    sum  = (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);
    sum += (int32_t)(*x++) * (int32_t)(*h++);

    if(taps > 24) {
        sum += (int32_t)(*x++) * (int32_t)(*h++);
        sum += (int32_t)(*x++) * (int32_t)(*h++);
        sum += (int32_t)(*x++) * (int32_t)(*h++);
        sum += (int32_t)(*x) * (int32_t)(*h);
    }

    sum >>= 15;
    if(sum > 32767) {
        sum = 32767;
    }
    else if(sum < -32768) {
        sum = -32768;
    }
    return sum;
}
#endif

/****************************************************************************/
void palm_polyphase(int16_t *x, int16_t *y, unsigned int in_n_frames, unsigned *out_n_frames,
                    palm_resampler *pr, unsigned int channel, unsigned stage)
{
    int16_t L = pr->u_sequence[stage];
    int16_t M = pr->d_sequence[stage];
    uint16_t n_taps = pr->poly->taps[stage];
    int16_t phase = pr->poly->phase[stage][channel];

    int16_t *y_base = y;
    int16_t *h = pr->poly->coeffs[stage];
    int16_t *z = pr->poly->states[stage][channel];

    while(in_n_frames-- > 0) {

        if(phase >= L)
            phase -= L;

        /* shift delay line and add new samples from input buffer */
        memmove(z + 1, z, (n_taps - 1) << 1); /* << 1 is replaced for multiplying by sizeof(int16_t) */
        *z = *x++;

        /* calculate fir output for a polyphase filter,
           goto next phase, increased by decimation factor M*/
        while(phase < L) {
            #ifdef __ARM_NEON__
            *y++ = fir_simd(z, h + phase*n_taps, n_taps);
            #else
            *y++ = fir_unroll(z, h + phase*n_taps, n_taps);
            #endif
            phase += M;
        }
    }

    *out_n_frames = (y - y_base);               /* set how many output samples calculated */
    pr->poly->phase[stage][channel] = phase;    /* set the current phase number to be used on next call */
}


void set_palm_resampler(palm_resampler *pr, int16_t stages,
                                            int16_t u1, int16_t d1, int16_t t1, int16_t *c1,
                                            int16_t u2, int16_t d2, int16_t t2, int16_t *c2)
{
    pr->stages = stages;

    pr->u_sequence[0] = u1;
    pr->d_sequence[0] = d1;
    pr->poly->taps[0] = t1;
    pr->poly->coeffs[0] = c1;

    if(stages == 2) {
        pr->u_sequence[1] = u2;
        pr->d_sequence[1] = d2;
        pr->poly->taps[1] = t2;
        pr->poly->coeffs[1] = c2;
    }
}

