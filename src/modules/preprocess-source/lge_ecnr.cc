/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */
#define _USE_MATH_DEFINES

// #ifdef HAVE_CONFIG_H
#include <config.h>
// #endif


#include "lge_ecnr.h"

lt_dlhandle ECNR_lib_handle;
shECNRInstT * (*ECNR_Create)(int);
void (*ECNR_Init) (shECNRInstT *, char *, char *);
void (*ECNR_Process) (shECNRInstT *, float *, float *, float *, int);
void (*ECNR_Free) (shECNRInstT *);

void float2short(float *src, short *dest, int size) {
    for (int i = 0; i < size; i++) {
        if (src[i] >= 1) {
            dest[i] = 32767;
        } else if (src[i] < -1) {
            dest[i] = -32768;
        } else {
            dest[i] = (short) (src[i] * 32768);
        }
    }
}

void short2float(short *src, float *dest, int size) {
    for (int i = 0; i < size; i++) {
        dest[i] = (float) (src[i]) / 32768.0f;
    }
}

static int webrtc_volume_from_pa(pa_volume_t v)
{
    return (v * 255) / PA_VOLUME_NORM;
}

static pa_volume_t webrtc_volume_to_pa(int v)
{
    return (v * PA_VOLUME_NORM) / 255;
}

static void ecnr_fixate_spec(pa_ecnr_params *ec, pa_sample_spec rec_ss, pa_channel_map rec_map,
                                  pa_sample_spec play_ss, pa_channel_map play_map,
                                  pa_sample_spec out_ss, pa_channel_map out_map, bool beamformer) {
    ec->rec_ss = rec_ss;
    ec->play_ss = play_ss;
    ec->out_ss = out_ss;
}


bool ecnr_init_internal(pa_ecnr_params *ec, const char *args) {

    //  speex echo canceller init
    ec->ecnr.echo_state = speex_echo_state_init(ec->blocksize, 1024);
    speex_echo_ctl(ec->ecnr.echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &ec->out_ss.rate);

    ec->ecnr.preprocess_state = speex_preprocess_state_init(ec->blocksize, ec->out_ss.rate);
    speex_preprocess_ctl(ec->ecnr.preprocess_state, SPEEX_PREPROCESS_SET_ECHO_STATE, ec->ecnr.echo_state);

    //  load ecnr library
    char libmodule_ec_nr_path[100];
    sprintf(libmodule_ec_nr_path, "%s/audioeffects/preprocess/libmodule_ec_nr.so", lt_dlgetsearchpath());
    ECNR_lib_handle = lt_dlopen(libmodule_ec_nr_path);
    if (ECNR_lib_handle == NULL) {
        pa_log("ECNR: fail to open AI ECNR library: %s %s", lt_dlerror(), libmodule_ec_nr_path);
        return false;
    }
    pa_log_debug("ECNR: AI ECNR library open: %s", libmodule_ec_nr_path);

    ECNR_Create = (shECNRInstT* (*)(int)) lt_dlsym(ECNR_lib_handle, "shECNR_create");
    ECNR_Init = (void (*)(shECNRInst*, char*, char*)) lt_dlsym(ECNR_lib_handle, "shECNR_init");
    ECNR_Process = (void (*)(shECNRInst*, float*, float*, float*, int)) lt_dlsym(ECNR_lib_handle, "shECNR_process");
    ECNR_Free = (void (*)(shECNRInst*)) lt_dlsym(ECNR_lib_handle, "shECNR_free");

    //  initialize ecnr
    ec->ecnr.ECNR_handle = ECNR_Create(0);
    char tfliteFilePath[100], windowFilePath[100];
    sprintf(tfliteFilePath, "%s/audioeffects/preprocess/model_ecnr.tflite", lt_dlgetsearchpath());
    sprintf(windowFilePath, "%s/audioeffects/preprocess/hann.txt", lt_dlgetsearchpath());
    pa_log_debug("ECNR: AI ECNR Init: %s %s", tfliteFilePath, windowFilePath);

    ECNR_Init(ec->ecnr.ECNR_handle, tfliteFilePath, windowFilePath);

    return true;
}


void *speech_enhancement_getHandle()
{
    if(!ecnrHandle)
    ecnrHandle = pa_xnew(pa_ecnr_params, 1);
    return ecnrHandle;
}

bool speech_enhancement_init(pa_sample_spec rec_ss, pa_channel_map rec_map,
                     pa_sample_spec play_ss, pa_channel_map play_map,
                     pa_sample_spec out_ss, pa_channel_map out_map,
                     uint32_t nframes, const char *args) {
    pa_modargs *ma;
    pa_ecnr_params *ec = (pa_ecnr_params*)speech_enhancement_getHandle();
    pa_log_debug("ECNR: mod args: %s", args);
    if (!(ma = pa_modargs_new(args, valid_modargs))) {
        pa_log("ECNR: Failed to parse submodule arguments.");
        return false;
    }

    ec->ecnr.enable = DEFAULT_ECNR_ENABLE;

    pa_modargs_get_value_boolean(ma, "ecnr", &ec->ecnr.enable);
    pa_log_debug("ECNR: ecnr[%B]", ec->ecnr.enable);

    ecnr_fixate_spec(ec, rec_ss, rec_map, play_ss, play_map, out_ss, out_map, false);

    ec->blocksize = 128;     //(uint64_t) out_ss->rate * BLOCK_SIZE_US / PA_USEC_PER_SEC;
    int numframes = ec->blocksize;

    if (ec->ecnr.enable) {
        if (!ecnr_init_internal(ec, args)) {
            pa_log("ECNR: ai ecnr initialization failed");
            return false;
        }
    }

    for (int i = 0; i < rec_ss.channels; i++)
        ec->rec_buffer[i] = pa_xnew(float, numframes);
    for (int i = 0; i < play_ss.channels; i++)
        ec->play_buffer[i] = pa_xnew(float, numframes);
    ec->out_buffer = pa_xnew(float, numframes);

    ec->s_rec_buf = pa_xnew(short, numframes);
    ec->s_play_buf = pa_xnew(short, numframes);
    ec->s_out_buf = pa_xnew(short, numframes);

    pa_modargs_free(ma);
    return true;
}


void lge_ai_ecnr_run(pa_ecnr_params *ec) {

    //  float to short
    float2short(ec->rec_buffer[0], ec->s_rec_buf, ec->blocksize);
    float2short(ec->play_buffer[0], ec->s_play_buf, ec->blocksize);

    //  speex
    speex_echo_cancellation(ec->ecnr.echo_state,
                            (const spx_int16_t *) ec->s_rec_buf,
                            (const spx_int16_t *) ec->s_play_buf,
                            (spx_int16_t *) ec->s_out_buf);
    speex_preprocess_run(ec->ecnr.preprocess_state, (spx_int16_t *) ec->s_out_buf);

    //  short to float
    short2float(ec->s_out_buf, ec->rec_buffer[0], ec->blocksize);

    //  ecnr
    ECNR_Process(ec->ecnr.ECNR_handle, ec->rec_buffer[0], ec->play_buffer[0], ec->out_buffer, ec->blocksize);
}

bool speech_enhancement_process(const uint8_t *rec, const uint8_t *play, uint8_t *out) {
    pa_ecnr_params *ec = (pa_ecnr_params*)speech_enhancement_getHandle();

    //  input signal wrote on out by lge_preprocess_run's data copy or beamforming
    memcpy(ec->rec_buffer[0], out, ec->blocksize * pa_sample_size(&(ec->out_ss)));
    memcpy(ec->play_buffer[0], play, ec->blocksize * pa_sample_size(&(ec->play_ss)));

    if (ec->ecnr.enable) {
        lge_ai_ecnr_run(ec);
    }

    memcpy(out, ec->out_buffer, ec->blocksize * pa_sample_size(&(ec->out_ss)));
    return true;
}

bool speech_enhancement_done() {

    //  free speex & ecnr
    pa_log("%s",__FUNCTION__);
    pa_ecnr_params *ec = (pa_ecnr_params*)speech_enhancement_getHandle();
    if (ec->ecnr.preprocess_state) {
        speex_preprocess_state_destroy(ec->ecnr.preprocess_state);
        ec->ecnr.preprocess_state = NULL;
    }
    if (ec->ecnr.echo_state) {
        speex_echo_state_destroy(ec->ecnr.echo_state);
        ec->ecnr.echo_state = NULL;
    }
    if (ec->ecnr.ECNR_handle) {
        ECNR_Free(ec->ecnr.ECNR_handle);
    }

    //  free buffers
    for (int i = 0; i < ec->rec_ss.channels; i++)
        pa_xfree(ec->rec_buffer[i]);
    for (int i = 0; i < ec->play_ss.channels; i++)
        pa_xfree(ec->play_buffer[i]);
    pa_xfree(ec->out_buffer);

    pa_xfree(ec->s_rec_buf);
    pa_xfree(ec->s_play_buf);
    pa_xfree(ec->s_out_buf);

    if(ecnrHandle)
    {
        pa_xfree(ecnrHandle);
        ecnrHandle = nullptr;
    }

    pa_log_debug("ECNR: finalized");
    return true;

}