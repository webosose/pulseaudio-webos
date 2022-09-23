/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/cdecl.h>

PA_C_DECL_BEGIN
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include "echo-cancel.h"
#include "module_ecnr_c.h"
PA_C_DECL_END

#define ECNR_N_FRAMES 128

void *ECNR_lib_handle = NULL;
shECNRInstT * (*ECNR_Create)();
void (*ECNR_Init) (shECNRInstT *, char *, char *);
void (*ECNR_Process) (shECNRInstT *, float *, float *, float *, int);
void (*ECNR_Free) (shECNRInstT *);
shECNRInstT *ECNR_handle;

SpeexEchoState *speexEchoState;
SpeexPreprocessState *speexPreprocessState;

bool lge_ecnr_init(pa_core *c, pa_echo_canceller *ec,
                     pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                     pa_sample_spec *play_ss, pa_channel_map *play_map,
                     pa_sample_spec *out_ss, pa_channel_map *out_map,
                     uint32_t *nframes, const char *args) {

    char libmodule_ec_nr_path[100];
    sprintf(libmodule_ec_nr_path, "%s/ecnr/libmodule_ec_nr.so", lt_dlgetsearchpath());
    ECNR_lib_handle = lt_dlopen(libmodule_ec_nr_path);
    if (ECNR_lib_handle == NULL) {
        pa_log("fail to open ECNR library: %s %s", lt_dlerror(), libmodule_ec_nr_path);
        return false;
    }
    pa_log_debug("ECNR library open: %s", libmodule_ec_nr_path);

    ECNR_Create = lt_dlsym(ECNR_lib_handle, "shECNR_create");
    ECNR_Init = lt_dlsym(ECNR_lib_handle, "shECNR_init");
    ECNR_Process = lt_dlsym(ECNR_lib_handle, "shECNR_process");
    ECNR_Free = lt_dlsym(ECNR_lib_handle, "shECNR_free");

    ECNR_handle = ECNR_Create(0);
    char tfliteFilePath[100], windowFilePath[100];
    sprintf(tfliteFilePath, "%s/ecnr/model_ecnr.tflite", lt_dlgetsearchpath());
    sprintf(windowFilePath, "%s/ecnr/hann.txt", lt_dlgetsearchpath());

    pa_log_debug("ECNR Init: %s %s", tfliteFilePath, windowFilePath);
    ECNR_Init(ECNR_handle, tfliteFilePath, windowFilePath);

    char strss_source[PA_SAMPLE_SPEC_SNPRINT_MAX];
    char strss_sink[PA_SAMPLE_SPEC_SNPRINT_MAX];

    *nframes = ECNR_N_FRAMES;
    ec->params.ecnr.out_ss = *out_ss;
    int sampleRate = 16000;

    *rec_ss = *out_ss;
    *rec_map = *out_map;

    speexEchoState = speex_echo_state_init(ECNR_N_FRAMES, 1000);
    speexPreprocessState = speex_preprocess_state_init(ECNR_N_FRAMES, sampleRate);
    speex_echo_ctl(speexEchoState, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);
    speex_preprocess_ctl(speexPreprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, speexEchoState);

    pa_log_debug("LGE ECNR AEC: nframes=%u, sample spec source=%s, sample spec sink=%s", *nframes,
                 pa_sample_spec_snprint(strss_source, sizeof(strss_source), out_ss),
                 pa_sample_spec_snprint(strss_sink, sizeof(strss_sink), play_ss));

    return true;
}

void lge_ecnr_run(pa_echo_canceller *ec, const uint8_t *rec, const uint8_t *play, uint8_t *out) {
    short *far = (short *) play;
    short *near = (short *) rec;
    short *output = (short *) out;

    //  speex echo cancellation
    speex_echo_cancellation(speexEchoState, rec, play, out);
    speex_preprocess_run(speexPreprocessState, out);

    //  short to float
    float fFar[ECNR_N_FRAMES];
    float fEcnrIn[ECNR_N_FRAMES];
    float fEcnrOut[ECNR_N_FRAMES];
    for (int i = 0; i < ECNR_N_FRAMES; i++) {
        fFar[i] = ((float) far[i]) / 32768.0F;
        fEcnrIn[i] = ((float) output[i]) / 32768.0F;
        fEcnrOut[i] = 0.0F;
    }

    //  ECNR
    ECNR_Process(ECNR_handle, fEcnrIn, fFar, fEcnrOut, ECNR_N_FRAMES);

    //  float to short
    for (int i = 0; i < ECNR_N_FRAMES; i++) {
        if (fEcnrOut[i] >= 1) {
            output[i] = 32767;
        } else if (fEcnrOut[i] < -1) {
            output[i] = -32768;
        } else {
            output[i] = (short) (fEcnrOut[i] * 32768);
        }
    }

    //  bypass
    // for (int i = 0; i < ECNR_N_FRAMES; i++) {
    //     output[i] = near[i];
    // }
}

void lge_ecnr_done(pa_echo_canceller *ec) {
    speex_echo_state_destroy(speexEchoState);
    speex_preprocess_state_destroy(speexPreprocessState);

    ECNR_Free(ECNR_handle);
}
