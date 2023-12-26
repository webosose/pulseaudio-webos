/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef AudioPostProcessConfig_H
#define AudioPostProcessConfig_H

//  Audio Post Processor
#define APP_MAX_CHANNEL 8

//  Equalizer
#define EQUALIZER_BANDS 6

static float equalizer_frequency[EQUALIZER_BANDS] = {
    60, 150, 400, 1000, 3000, 14000,
};

typedef enum EqualizerPreset {
    CLASSIC = 0,
    JAZZ,
    POP,
    ROCK,
    PRESET_MAX
} EqualizerPreset;

static float equalizer_presets[PRESET_MAX][EQUALIZER_BANDS] = {
    {-4, -1, 0, 0, 2, 3},       //  CLASSIC
    {2, 4, -3, -2, 1, -2},      //  JAZZ
    {-2, -1, 1, 3, 2, -3},      //  POP
    {2, 3, 1, -3, -1, 2},       //  ROCK
};

#endif  //  AudioPostProcessConfig_H