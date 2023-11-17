/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */
#define _USE_MATH_DEFINES

// #ifdef HAVE_CONFIG_H
#include <config.h>
// #endif

#include <pulse/cdecl.h>

PA_C_DECL_BEGIN
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>

#include <pulse/timeval.h>
#include "echo-cancel.h"
PA_C_DECL_END

#include "webrtc/modules/audio_processing/include/audio_processing.h"
// #include "webrtc/modules/interface/module_common_types.h"
// #include "webrtc/system_wrappers/include/trace.h"

#define BLOCK_SIZE_US 10000

#define DEFAULT_BEAMFORMER_ENABLE false
#define DEFAULT_ECNR_ENABLE true

lt_dlhandle ECNR_lib_handle;
shECNRInstT * (*ECNR_Create)(int);
void (*ECNR_Init) (shECNRInstT *, char *, char *);
void (*ECNR_Process) (shECNRInstT *, float *, float *, float *, int);
void (*ECNR_Free) (shECNRInstT *);

static const char* const valid_modargs[] = {
    "ecnr",
    "beamformer",
    "high_pass_filter",
    "analog_gain_control",
    "agc_start_volume",
    "auto_aim",
    NULL
};

float gain_saturation(float in, float gain) {
    float out = in * gain;
    if (out >= 1) {
        out = 1;
    } else if (out < -1) {
        out = -1;
    }
    return out;
}

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

static void ec_fixate_spec(pa_echo_canceller *ec, pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                                  pa_sample_spec *play_ss, pa_channel_map *play_map,
                                  pa_sample_spec *out_ss, pa_channel_map *out_map, bool beamformer) {

    pa_sample_format_t fixed_format = PA_SAMPLE_FLOAT32NE;
    uint32_t fixed_rate = 16000;

    play_ss->format = fixed_format;
    play_ss->rate = fixed_rate;
    play_ss->channels = 1;
    pa_channel_map_init_mono(play_map);

    *out_ss = *play_ss;
    *out_map = *play_map;

    rec_ss->format = fixed_format;
    rec_ss->rate = fixed_rate;
    if (!beamformer) {
        rec_ss->channels = 1;
        pa_channel_map_init_mono(rec_map);
    }

    ec->params.rec_ss = *rec_ss;
    ec->params.play_ss = *play_ss;
    ec->params.out_ss = *out_ss;
}

static void get_mic_geometry(std::vector<webrtc::Point>& geometry) {
    float mic_geometry[] = {
        #include "mic_geometry.txt"
    };

    for (int i = 0; i < geometry.size(); i++) {
        geometry[i].c[0] = mic_geometry[i * 3];
        geometry[i].c[1] = mic_geometry[i * 3 + 1];
        geometry[i].c[2] = mic_geometry[i * 3 + 2];
        pa_log_debug("ECNR: mic[%d]: %.3f, %.3f, %.3f", i, geometry[i].c[0], geometry[i].c[1], geometry[i].c[2]);
    }
}

bool lge_apm_init(pa_echo_canceller *ec, const char *args) {

    webrtc::AudioProcessing *apm = NULL;
    webrtc::ProcessingConfig pconfig;
    webrtc::Config config;
    bool hpf, agc, auto_aim;
    uint32_t agc_start_volume;
    pa_modargs *ma;

    hpf = true;
    agc = false;
    agc_start_volume = 16;
    auto_aim = true;

    ma = pa_modargs_new(args, valid_modargs);
    pa_modargs_get_value_boolean(ma, "high_pass_filter", &hpf);
    pa_modargs_get_value_boolean(ma, "analog_gain_control", &agc);
    pa_modargs_get_value_u32(ma, "agc_start_volume", &agc_start_volume);
    pa_modargs_get_value_boolean(ma, "auto_aim", &auto_aim);
    ec->params.beamformer.agc_start_volume = agc_start_volume;

    /* We do this after fixate because we need the capture channel count */
    std::vector<webrtc::Point> geometry(ec->params.rec_ss.channels);
    webrtc::SphericalPointf direction(M_PI_2, 0.0f, 0.0f);
    const char *mic_geometry;
    float inner_product;

    //  mic geometry check
    get_mic_geometry(geometry);

    inner_product = 0;
    for (int i = 0; i < geometry.size(); i++)
        inner_product += geometry[i].c[0] * geometry[i].c[1];
    if (inner_product == 0) ec->params.beamformer.is_linear_array = true;
    else ec->params.beamformer.is_linear_array = false;

    config.Set<webrtc::Beamforming>(new webrtc::Beamforming(true, geometry, direction));

    apm = webrtc::AudioProcessing::Create(config);

    pconfig = {
        webrtc::StreamConfig(ec->params.rec_ss.rate, ec->params.rec_ss.channels, false), /* input stream */
        webrtc::StreamConfig(ec->params.out_ss.rate, ec->params.out_ss.channels, false), /* output stream */
        webrtc::StreamConfig(ec->params.play_ss.rate, ec->params.play_ss.channels, false), /* reverse input stream */
        webrtc::StreamConfig(ec->params.play_ss.rate, ec->params.play_ss.channels, false), /* reverse output stream */
    };
    if (apm->Initialize(pconfig) != webrtc::AudioProcessing::kNoError) {
        pa_log("ECNR: Error initialising audio processing module");
        goto fail;
    }

    if (hpf)
        apm->high_pass_filter()->Enable(true);

    if (agc) {
        apm->gain_control()->set_mode(webrtc::GainControl::kAdaptiveAnalog);
        if (apm->gain_control()->set_analog_level_limits(0, 255) !=
                webrtc::AudioProcessing::kNoError) {
            pa_log("ECNR: Failed to initialise AGC");
            goto fail;
        }
        ec->params.beamformer.agc = true;

        apm->gain_control()->Enable(true);
    }

    apm->set_beamformer_auto_aim(auto_aim);

    ec->params.beamformer.apm = apm;
    ec->params.beamformer.first = true;

    pa_modargs_free(ma);
    return true;

fail:
    if (ma)
        pa_modargs_free(ma);
    if (apm)
        delete apm;

    return false;
}

bool lge_ai_ecnr_init(pa_echo_canceller *ec, const char *args) {

    //  speex echo canceller init
    ec->params.ecnr.echo_state = speex_echo_state_init(ec->params.blocksize, 1024);
    speex_echo_ctl(ec->params.ecnr.echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &ec->params.out_ss.rate);

    ec->params.ecnr.preprocess_state = speex_preprocess_state_init(ec->params.blocksize, ec->params.out_ss.rate);
    speex_preprocess_ctl(ec->params.ecnr.preprocess_state, SPEEX_PREPROCESS_SET_ECHO_STATE, ec->params.ecnr.echo_state);

    //  load ecnr library
    char libmodule_ec_nr_path[100];
    sprintf(libmodule_ec_nr_path, "%s/ecnr/libmodule_ec_nr.so", lt_dlgetsearchpath());
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
    ec->params.ecnr.ECNR_handle = ECNR_Create(0);
    char tfliteFilePath[100], windowFilePath[100];
    sprintf(tfliteFilePath, "%s/ecnr/model_ecnr.tflite", lt_dlgetsearchpath());
    sprintf(windowFilePath, "%s/ecnr/hann.txt", lt_dlgetsearchpath());
    pa_log_debug("ECNR: AI ECNR Init: %s %s", tfliteFilePath, windowFilePath);

    ECNR_Init(ec->params.ecnr.ECNR_handle, tfliteFilePath, windowFilePath);

    return true;
}

bool lge_ecnr_init(pa_core *c, pa_echo_canceller *ec,
                     pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                     pa_sample_spec *play_ss, pa_channel_map *play_map,
                     pa_sample_spec *out_ss, pa_channel_map *out_map,
                     uint32_t *nframes, const char *args) {

    pa_modargs *ma;
    pa_log_debug("ECNR: mod args: %s", args);
    if (!(ma = pa_modargs_new(args, valid_modargs))) {
        pa_log("ECNR: Failed to parse submodule arguments.");
        return false;
    }

    ec->params.beamformer.enable = DEFAULT_BEAMFORMER_ENABLE;
    ec->params.ecnr.enable = DEFAULT_ECNR_ENABLE;

    pa_modargs_get_value_boolean(ma, "beamformer", &ec->params.beamformer.enable);
    pa_modargs_get_value_boolean(ma, "ecnr", &ec->params.ecnr.enable);
    pa_log_debug("ECNR: beamformer[%B] ecnr[%B]", ec->params.beamformer.enable, ec->params.ecnr.enable);

    ec_fixate_spec(ec, rec_ss, rec_map, play_ss, play_map, out_ss, out_map, ec->params.beamformer.enable);

    ec->params.blocksize = 128;     //(uint64_t) out_ss->rate * BLOCK_SIZE_US / PA_USEC_PER_SEC;
    *nframes = ec->params.blocksize;

    if (ec->params.beamformer.enable) {
        if (!lge_apm_init(ec, args)) {
            pa_log("ECNR: beamformer initialization failed");
            return false;
        }
    }

    if (ec->params.ecnr.enable) {
        if (!lge_ai_ecnr_init(ec, args)) {
            pa_log("ECNR: ai ecnr initialization failed");
            return false;
        }
    }

    for (int i = 0; i < rec_ss->channels; i++)
        ec->params.rec_buffer[i] = pa_xnew(float, *nframes);
    for (int i = 0; i < play_ss->channels; i++)
        ec->params.play_buffer[i] = pa_xnew(float, *nframes);
    ec->params.out_buffer = pa_xnew(float, *nframes);

    ec->params.s_rec_buf = pa_xnew(short, *nframes);
    ec->params.s_play_buf = pa_xnew(short, *nframes);
    ec->params.s_out_buf = pa_xnew(short, *nframes);

    pa_modargs_free(ma);
    return true;
}

void lge_apm_play(pa_echo_canceller *ec) {
    webrtc::AudioProcessing *apm = (webrtc::AudioProcessing*)ec->params.beamformer.apm;
    const pa_sample_spec *ss = &ec->params.play_ss;
    float **buf = ec->params.play_buffer;
    webrtc::StreamConfig config(ss->rate, ss->channels, false);

    pa_assert_se(apm->ProcessReverseStream(buf, config, config, buf) == webrtc::AudioProcessing::kNoError);
}

void lge_apm_record(pa_echo_canceller *ec) {
    webrtc::AudioProcessing *apm = (webrtc::AudioProcessing*)ec->params.beamformer.apm;
    const pa_sample_spec *rec_ss = &ec->params.rec_ss;
    const pa_sample_spec *out_ss = &ec->params.out_ss;
    float **buf = ec->params.rec_buffer;
    int old_volume, new_volume;
    webrtc::StreamConfig rec_config(rec_ss->rate, rec_ss->channels, false);
    webrtc::StreamConfig out_config(out_ss->rate, out_ss->channels, false);

    //  input dump: (need chmod 777 /home/root)
    // FILE* dumpRec0 = fopen("/home/root/rec0.pcm", "a+");
    // FILE* dumpRec1 = fopen("/home/root/rec1.pcm", "a+");
    // FILE* dumpRec2 = fopen("/home/root/rec2.pcm", "a+");
    // FILE* dumpRec3 = fopen("/home/root/rec3.pcm", "a+");
    // fwrite(ec->params.rec_buffer[0], sizeof(float), ec->params.blocksize, dumpRec0);
    // fwrite(ec->params.rec_buffer[1], sizeof(float), ec->params.blocksize, dumpRec1);
    // fwrite(ec->params.rec_buffer[2], sizeof(float), ec->params.blocksize, dumpRec2);
    // fwrite(ec->params.rec_buffer[3], sizeof(float), ec->params.blocksize, dumpRec3);
    // fclose(dumpRec0);fclose(dumpRec1);fclose(dumpRec2);fclose(dumpRec3);

    if (ec->params.beamformer.agc) {
        pa_volume_t v = pa_echo_canceller_get_capture_volume(ec);
        old_volume = webrtc_volume_from_pa(v);
        apm->gain_control()->set_stream_analog_level(old_volume);
    }

    apm->set_stream_delay_ms(0);
    pa_assert_se(apm->ProcessStream(buf, rec_config, out_config, buf) == webrtc::AudioProcessing::kNoError);

}

void lge_ai_ecnr_run(pa_echo_canceller *ec) {

    //  ecnr input dump
    // FILE* dumpRec0 = fopen("/home/root/ecnr_in.pcm", "a+");
    // fwrite(ec->params.rec_buffer[0], sizeof(float), ec->params.blocksize, dumpRec0);
    // fclose(dumpRec0);

    // FILE* dumpRec1 = fopen("/home/root/ecnr_far.pcm", "a+");
    // fwrite(ec->params.play_buffer[0], sizeof(float), ec->params.blocksize, dumpRec1);
    // fclose(dumpRec1);

    //  float to short
    float2short(ec->params.rec_buffer[0], ec->params.s_rec_buf, ec->params.blocksize);
    float2short(ec->params.play_buffer[0], ec->params.s_play_buf, ec->params.blocksize);

    //  speex
    speex_echo_cancellation(ec->params.ecnr.echo_state,
                            (const spx_int16_t *) ec->params.s_rec_buf,
                            (const spx_int16_t *) ec->params.s_play_buf,
                            (spx_int16_t *) ec->params.s_out_buf);
    speex_preprocess_run(ec->params.ecnr.preprocess_state, (spx_int16_t *) ec->params.s_out_buf);

    //  short to float
    short2float(ec->params.s_out_buf, ec->params.out_buffer, ec->params.blocksize);

    //  speex out dump
    // FILE* dumpRec2 = fopen("/home/root/ecnr_speex.pcm", "a+");
    // fwrite(ec->params.out_buffer, sizeof(float), ec->params.blocksize, dumpRec2);
    // fclose(dumpRec2);

    //  ecnr
    ECNR_Process(ec->params.ecnr.ECNR_handle, ec->params.out_buffer, ec->params.play_buffer[0],
                ec->params.rec_buffer[0], ec->params.blocksize);

    //  ecnr out dump
    // FILE* dumpRec3 = fopen("/home/root/ecnr_out.pcm", "a+");
    // fwrite(ec->params.rec_buffer[0], sizeof(float), ec->params.blocksize, dumpRec3);
    // fclose(dumpRec3);
}

void lge_ecnr_run(pa_echo_canceller *ec, const uint8_t *rec, const uint8_t *play, uint8_t *out) {

    const pa_sample_spec *play_ss = &ec->params.play_ss;
    const pa_sample_spec *rec_ss = &ec->params.rec_ss;
    const pa_sample_spec *out_ss = &ec->params.out_ss;
    int n = ec->params.blocksize;
    float **pbuf = ec->params.play_buffer;
    float **rbuf = ec->params.rec_buffer;

    pa_deinterleave(play, (void **) pbuf, play_ss->channels, pa_sample_size(play_ss), n);
    pa_deinterleave(rec, (void **) rbuf, rec_ss->channels, pa_sample_size(rec_ss), n);

    if (ec->params.beamformer.enable) {
        lge_apm_play(ec);
        lge_apm_record(ec);
    }

    if (ec->params.ecnr.enable) {
        lge_ai_ecnr_run(ec);
    }

    pa_interleave((const void **) rbuf, out_ss->channels, out, pa_sample_size(out_ss), n);
}

void lge_ecnr_done(pa_echo_canceller *ec) {
    //  free apm
    if (ec->params.beamformer.apm) {
        delete (webrtc::AudioProcessing*)ec->params.beamformer.apm;
        ec->params.beamformer.apm = NULL;
    }

    //  free speex & ecnr
    if (ec->params.ecnr.preprocess_state) {
        speex_preprocess_state_destroy(ec->params.ecnr.preprocess_state);
        ec->params.ecnr.preprocess_state = NULL;
    }
    if (ec->params.ecnr.echo_state) {
        speex_echo_state_destroy(ec->params.ecnr.echo_state);
        ec->params.ecnr.echo_state = NULL;
    }
    if (ec->params.ecnr.ECNR_handle) {
        ECNR_Free(ec->params.ecnr.ECNR_handle);
    }

    //  free buffers
    for (int i = 0; i < ec->params.rec_ss.channels; i++)
        pa_xfree(ec->params.rec_buffer[i]);
    for (int i = 0; i < ec->params.play_ss.channels; i++)
        pa_xfree(ec->params.play_buffer[i]);

    pa_xfree(ec->params.s_rec_buf);
    pa_xfree(ec->params.s_play_buf);
    pa_xfree(ec->params.s_out_buf);

    pa_log_debug("ECNR: finalized");
}
