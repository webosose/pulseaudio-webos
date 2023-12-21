/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */
#ifndef foobeamforming
#define foobeamforming
#define _USE_MATH_DEFINES

// #ifdef HAVE_CONFIG_H
#include <config.h>
// #endif

#include <pulse/cdecl.h>

PA_C_DECL_BEGIN
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulsecore/core.h>
#include <pulsecore/macro.h>
#include <pulse/timeval.h>
PA_C_DECL_END


#include "webrtc/modules/audio_processing/include/audio_processing.h"

#define BLOCK_SIZE_US 10000

#define DEFAULT_BEAMFORMER_ENABLE false



struct pa_beamforming_params {
    unsigned int blocksize; /* in frames */
    pa_sample_spec rec_ss, play_ss, out_ss;
    float *rec_buffer[PA_CHANNELS_MAX], *play_buffer[PA_CHANNELS_MAX], *out_buffer;
    short *s_rec_buf, *s_play_buf, *s_out_buf;

    bool enable;
    void *apm;
    bool agc;
    bool first;
    unsigned int agc_start_volume;
    bool is_linear_array;
    /* Set this if canceller can do drift compensation. Also see set_drift()
     * below */
    bool drift_compensation;
};
typedef struct pa_beamforming_params pa_beamforming_params;
pa_beamforming_params *beamformingHandle = nullptr;

PA_C_DECL_BEGIN
bool beamforming_process(const uint8_t *rec, const uint8_t *play, uint8_t *out) ;
bool beamforming_done() ;

bool beamforming_init(pa_sample_spec rec_ss, pa_channel_map rec_map,
                     pa_sample_spec play_ss, pa_channel_map play_map,
                     pa_sample_spec out_ss, pa_channel_map out_map,
                     uint32_t nframes, const char *args);
void *beamforming_getHandle();
PA_C_DECL_END

#endif