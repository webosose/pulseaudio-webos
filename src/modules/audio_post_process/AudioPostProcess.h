/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef AudioPostProcess_H
#define AudioPostProcess_H

#include <stdbool.h>

#include "AudioPostProcessConfig.h"
#include "module/Equalizer.h"
#include "module/DynamicRangeControl.h"

//		Parameter Structure
typedef struct {
	int sampleRate;
	int channelNum;

	bool equalizerEnable;

	EqualizerMemory equalizerMem;
	DynamicRangeControlMemory dynamicRangeControlMem;
} AudioPostProcessMemory;

//	API
void AudioPostProcess_Init(AudioPostProcessMemory *mem, int sampleRate, int channelNum);
void AudioPostProcess_Proc(AudioPostProcessMemory *mem, int samplesPerChannel, float *in, float *out);
void AudioPostProcess_Free(AudioPostProcessMemory *mem);

//	Equalizer API
void AudioPostProcess_Equalizer_setEnable(AudioPostProcessMemory *mem, bool enable);
void AudioPostProcess_Equalizer_setBandLevel(AudioPostProcessMemory *mem, int band, float level);
void AudioPostProcess_Equalizer_setPreset(AudioPostProcessMemory *mem, EqualizerPreset preset);

#endif	//	AudioPostProcess_H
