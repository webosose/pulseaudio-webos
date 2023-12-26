/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#include <string.h>

#include "AudioPostProcess.h"

//	API
void AudioPostProcess_Init(AudioPostProcessMemory *mem, int sampleRate, int channelNum) {
	mem->sampleRate = sampleRate;
	mem->channelNum = channelNum;

	mem->equalizerEnable = false;

	Equalizer_init(&mem->equalizerMem, sampleRate, channelNum);
	DynamicRangeControl_init(&mem->dynamicRangeControlMem, sampleRate, channelNum);
}

void AudioPostProcess_Proc(AudioPostProcessMemory *mem, int samplesPerChannel, float *in, float *out) {
	//	bypass copy
	memcpy(out, in, samplesPerChannel * mem->channelNum * sizeof(float));
	
	//	equalizer
	if (mem->equalizerEnable)
		Equalizer_proc(&mem->equalizerMem, samplesPerChannel, out);
	
	//	peak limiter
	if (mem->equalizerEnable)
		DynamicRangeControl_proc(&mem->dynamicRangeControlMem, samplesPerChannel, out);
}

void AudioPostProcess_Free(AudioPostProcessMemory *mem) {
	//	free memories
}

//	Equalizer Module API
void AudioPostProcess_Equalizer_setEnable(AudioPostProcessMemory *mem, bool enable) {
	mem->equalizerEnable = enable;
}

void AudioPostProcess_Equalizer_setBandLevel(AudioPostProcessMemory *mem, int band, float level) {
	Equalizer_setBandLevel(&mem->equalizerMem, band, level);
}

void AudioPostProcess_Equalizer_setPreset(AudioPostProcessMemory *mem, EqualizerPreset preset) {
	Equalizer_setPreset(&mem->equalizerMem, preset);
}