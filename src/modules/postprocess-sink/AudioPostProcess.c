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

	mem->bassBoostEnable = false;
	mem->equalizerEnable = false;

	BassBoost_init(&mem->bassBoostMem, sampleRate, channelNum);
	Equalizer_init(&mem->equalizerMem, sampleRate, channelNum);
	DynamicRangeControl_init(&mem->dynamicRangeControlMem, sampleRate, channelNum);
	//	snd drc initialize function
    char *sndConfig = "/etc/pulse/sndfilter.txt";
	snd_drc_init(&mem->sndDrcMem, sndConfig, sampleRate);
}

void AudioPostProcess_Proc(AudioPostProcessMemory *mem, int samplesPerChannel, float *in, float *out) {
	//	bypass copy
	memcpy(out, in, samplesPerChannel * mem->channelNum * sizeof(float));

	//	bass boost
	if (mem->bassBoostEnable) {
		BassBoost_proc(&mem->bassBoostMem, samplesPerChannel, out);
	}

	//	equalizer
	if (mem->equalizerEnable) {
		Equalizer_proc(&mem->equalizerMem, samplesPerChannel, out);
	}

	//	dynamic range control
	if (mem->dynamicRangeControlEnable) {
		//	snd drc process function
		snd_drc_process(&mem->sndDrcMem, samplesPerChannel, in, out);
	}

	//	peak limiter
	DynamicRangeControl_proc(&mem->dynamicRangeControlMem, samplesPerChannel, out);
}

void AudioPostProcess_Free(AudioPostProcessMemory *mem) {
	//	free memories
}

//	Bass Boost API
void AudioPostProcess_BassBoost_setEnable(AudioPostProcessMemory *mem, bool enable) {
	mem->bassBoostEnable = enable;
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

//	Dynamic Range Control API
void AudioPostProcess_DynamicRangeControl_setEnable(AudioPostProcessMemory *mem, bool enable) {
	mem->dynamicRangeControlEnable = enable;
}