/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef AudioPostProcess_H
#define AudioPostProcess_H

#include <stdbool.h>

#include "AudioPostProcessConfig.h"
#include "module/BassBoost.h"
#include "module/Equalizer.h"
#include "module/DynamicRangeControl.h"

//	for snd drc
#include "module/drc/drc_wrap.h"

//		Parameter Structure
typedef struct {
	int sampleRate;
	int channelNum;

	bool equalizerEnable;
	bool bassBoostEnable;
	bool dynamicRangeControlEnable;

	BassBoostMemory bassBoostMem;
	EqualizerMemory equalizerMem;
	DynamicRangeControlMemory dynamicRangeControlMem;
	//	for snd drc
	SndDrcMemory sndDrcMem;
} AudioPostProcessMemory;

//	API
void AudioPostProcess_Init(AudioPostProcessMemory *mem, int sampleRate, int channelNum);
void AudioPostProcess_Proc(AudioPostProcessMemory *mem, int samplesPerChannel, float *in, float *out);
void AudioPostProcess_Free(AudioPostProcessMemory *mem);

//	Bass Boost API
void AudioPostProcess_BassBoost_setEnable(AudioPostProcessMemory *mem, bool enable);

//	Equalizer API
void AudioPostProcess_Equalizer_setEnable(AudioPostProcessMemory *mem, bool enable);
void AudioPostProcess_Equalizer_setBandLevel(AudioPostProcessMemory *mem, int band, float level);
void AudioPostProcess_Equalizer_setPreset(AudioPostProcessMemory *mem, EqualizerPreset preset);

//	Dynamic Range Control API
void AudioPostProcess_DynamicRangeControl_setEnable(AudioPostProcessMemory *mem, bool enable);

#endif	//	AudioPostProcess_H
