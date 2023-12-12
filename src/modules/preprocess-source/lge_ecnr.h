/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */
#ifndef fooecnr
#define fooecnr
#define _USE_MATH_DEFINES

// #ifdef HAVE_CONFIG_H
#include <config.h>
// #endif

#include <pulse/cdecl.h>

PA_C_DECL_BEGIN
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulse/sample.h>
#include <pulse/timeval.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include <pulse/channelmap.h>
#include <pulsecore/core.h>
#include <pulsecore/macro.h>
#include <ltdl.h>
PA_C_DECL_END

struct shECNRInst;
typedef struct shECNRInst shECNRInstT;

struct pa_ecnr_params {
    unsigned int blocksize; /* in frames */
    pa_sample_spec rec_ss, play_ss, out_ss;
    float *rec_buffer[PA_CHANNELS_MAX], *play_buffer[PA_CHANNELS_MAX], *out_buffer;
    short *s_rec_buf, *s_play_buf, *s_out_buf;

    //  speex + ecnr
    struct {
        bool enable;
        pa_sample_spec out_ss;
        SpeexEchoState *echo_state;
        SpeexPreprocessState *preprocess_state;
        shECNRInstT *ECNR_handle;
    } ecnr;
    /* Set this if canceller can do drift compensation. Also see set_drift()
     * below */
    bool drift_compensation;
};

typedef struct pa_ecnr_params pa_ecnr_params;



#include "webrtc/modules/audio_processing/include/audio_processing.h"

#define BLOCK_SIZE_US 10000

#define DEFAULT_ECNR_ENABLE true



static const char* const valid_modargs[] = {
    "ecnr",
    "beamformer",
    "high_pass_filter",
    "analog_gain_control",
    "agc_start_volume",
    "auto_aim",
    NULL
};

PA_C_DECL_BEGIN
bool speech_enhancement_init(void *handle,
                     pa_sample_spec rec_ss, pa_channel_map rec_map,
                     pa_sample_spec play_ss, pa_channel_map play_map,
                     pa_sample_spec out_ss, pa_channel_map out_map,
                     uint32_t nframes, const char *args) ;



bool speech_enhancement_process(void *handle, const uint8_t *rec, const uint8_t *play, uint8_t *out);

bool speech_enhancement_done(void *handle);

void *speech_enhancement_getHandle();
PA_C_DECL_END

#endif