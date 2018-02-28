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
 * palm-resampler.h - palm resampler with pre generated filter coefficients.
 **********************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef MAX
	#define MAX(a, b) (a > b ? a : b)
#endif

/*****************************************************************************
	filter structure holds data for resampling.
	This is assuming at the most, the resampling does a 2 stage cascade.

	states : hold the delay states for each channel and stage
	taps : specifies the number of taps to use for each filter
	phase : holds the current phase number for the polyphase filter
	coeffs : holds the coefficients for the low pass filter
******************************************************************************/
typedef struct {
	int16_t **states[2];
	int16_t taps[2];
	int16_t *phase[2];
	int16_t *coeffs[2];
} palm_filter;



/*****************************************************************************
	palm_resampler structure

	x, y : holds temporary data buffers for each channel of audio data
	size : the size of the x and y buffers
	buf_factor: the ratio to multiply by the frame size to have a large enough temp buffer
	channels : the number of channels of the audio signal
	u_sequence, d_sequence : holds the upsample/downsample factors
	stages : the number of cascade stages, at the most 2.
	poly : pointer to a palm_filter
******************************************************************************/
typedef struct {
	int16_t channels;
	int16_t u_sequence[2];
	int16_t d_sequence[2];
	int16_t stages;
	palm_filter *poly;
} palm_resampler;


#ifdef __ARM_NEON__
/********************************************************************************
	caculates the output for an fir filter.  Only used if simd is available

	x = input data buffer
	h = the fir filter coefficients
	taps = number of taps for the filter
*********************************************************************************/
int16_t fir_simd(int16_t *x, int16_t *h, unsigned taps);

#else
/********************************************************************************
	caculates the output for an fir filter.  Unrolls fir loops 24 or 28 times

	x = input data buffer
	h = the fir filter coefficients
	taps = number of taps for the filter
*********************************************************************************/
int16_t fir_unroll(int16_t *x, int16_t *h, unsigned taps);
#endif


/********************************************************************************
	implements the polyphase decomposition for resampling audio data

	in_n_frames : number of samples in the input buffer
	out_n_frames : number of processed output samples is returned to this
	pr : pointer to a palm_resampler struct
	channel : current audio channel
	stage : current stage of the resampler cascade
*********************************************************************************/
void palm_polyphase(int16_t *x, int16_t * y, unsigned int in_n_frames, unsigned *out_n_frames, palm_resampler *pr, unsigned int channel, unsigned stage);




/********************************************************************************
	pr : pointer to palm_resampler
	u1 : upsampling factor for the first stage
	d1 : downsampling factor for the first stage
	t1 : number of taps for the first stage
	c1 : pointer to the filter coefficients for the first stage
	u2 : upsampling factor for the second stage
	d2 : downsampling factor for the second stage
	t2 : number of taps for the second stage
	c2 : pointer to the filter coefficients for the second stage
*********************************************************************************/
void set_palm_resampler(palm_resampler *pr, int16_t stages,
						int16_t u1, int16_t d1, int16_t t1, int16_t *c1,
						int16_t u2, int16_t d2, int16_t t2, int16_t *c2);

