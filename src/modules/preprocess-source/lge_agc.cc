/*
 * Copyright (c) 2023 LG Electronics Inc.
 */

#include <config.h>

#include <pulse/cdecl.h>

PA_C_DECL_BEGIN
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>

#include <pulse/timeval.h>
PA_C_DECL_END

#include "lge_agc.h"

#include <modules/audio_processing/include/audio_processing.h>

#include <iostream>
using namespace std;

#define BLOCK_SIZE_US 10000

#define DEFAULT_HIGH_PASS_FILTER true
#define DEFAULT_NOISE_SUPPRESSION true
#define DEFAULT_ANALOG_GAIN_CONTROL true
#define DEFAULT_DIGITAL_GAIN_CONTROL false
#define DEFAULT_MOBILE false
#define DEFAULT_ROUTING_MODE "speakerphone"
#define DEFAULT_COMFORT_NOISE true
#define DEFAULT_DRIFT_COMPENSATION false
#define DEFAULT_VAD true
#define DEFAULT_EXTENDED_FILTER false
#define DEFAULT_INTELLIGIBILITY_ENHANCER false
#define DEFAULT_EXPERIMENTAL_AGC false
#define DEFAULT_AGC_START_VOLUME 85
#define DEFAULT_BEAMFORMING false
#define DEFAULT_TRACE false

#define WEBRTC_AGC_MAX_VOLUME 255

static const char* const valid_modargs[] = {
    "high_pass_filter",
    "noise_suppression",
    "analog_gain_control",
    "digital_gain_control",
    "mobile",
    "routing_mode",
    "comfort_noise",
    "drift_compensation",
    "voice_detection",
    "extended_filter",
    "intelligibility_enhancer",
    "experimental_agc",
    "agc_start_volume",
    "beamforming",
    "mic_geometry", /* documented in parse_mic_geometry() */
    "target_direction", /* documented in parse_mic_geometry() */
    "trace",
    NULL
};

static int webrtc_volume_from_pa(pa_volume_t v)
{
    return (v * WEBRTC_AGC_MAX_VOLUME) / PA_VOLUME_NORM;
}

static pa_volume_t webrtc_volume_to_pa(int v)
{
    return (v * PA_VOLUME_NORM) / WEBRTC_AGC_MAX_VOLUME;
}

/*
static void webrtc_ec_fixate_spec(pa_sample_spec rec_ss, pa_channel_map rec_map,
                                  pa_sample_spec play_ss, pa_channel_map play_map,
                                  pa_sample_spec out_ss, pa_channel_map out_map,
                                  bool beamforming)
{
    ec->out_ss = rec_ss;
    *out_map = rec_map;
}*/

static bool parse_point(const char **point, float (&f)[3]) {
    int ret, length;

    ret = sscanf(*point, "%g,%g,%g%n", &f[0], &f[1], &f[2], &length);
    if (ret != 3)
        return false;

    /* Consume the bytes we've read so far */
    *point += length;

    return true;
}

void *gain_control_getHandle()
{
    if(!agcHandle)
    agcHandle = pa_xnew(pa_agc_struct, 1);
    return agcHandle;
}

bool gain_control_init(pa_sample_spec rec_ss, pa_channel_map rec_map,
                       pa_sample_spec play_ss, pa_channel_map play_map,
                       pa_sample_spec out_ss, pa_channel_map out_map,
                       uint32_t nframes, const char *args) {
    pa_agc_struct *ec = (pa_agc_struct*) gain_control_getHandle();
    pa_log("agc_init");

    webrtc::AudioProcessing* apm = webrtc::AudioProcessingBuilder().Create();
    webrtc::AudioProcessing::Config config;
    config.echo_canceller.enabled = false;
    config.echo_canceller.mobile_mode = false;

    config.gain_controller1.enabled = true;
    config.gain_controller1.mode = webrtc::AudioProcessing::Config::GainController1::kAdaptiveAnalog;
    config.gain_controller1.analog_level_minimum = 0;
    config.gain_controller1.analog_level_maximum = 255;
    config.gain_controller2.enabled = true;
    apm->ApplyConfig(config);

    pa_log_info("Config %s", config.ToString().c_str());

    ec->blocksize = 128;//(uint64_t) out_ss->rate * BLOCK_SIZE_US / PA_USEC_PER_SEC;
    int numframes = ec->blocksize;
    for (int i = 0; i < rec_ss.channels; i++)
        ec->rec_buffer[i] = pa_xnew(float, numframes*4);

    ec->apm = apm;
    ec->rec_ss = rec_ss;
    ec->play_ss  = play_ss;
    ec->out_ss = out_ss;
    pa_log_info("Done init function");

    return true;
}

void agc_play(pa_agc_struct *ec, const uint8_t *play) {

}

void agc_record(pa_agc_struct *ec, const uint8_t *rec, uint8_t *out) {

}

void agc_set_drift(pa_agc_struct *ec, float drift) {

}

bool gain_control_process(const uint8_t *rec, const uint8_t *play, uint8_t *out) {
    //pa_log("agc_process");
    pa_agc_struct *ec = (pa_agc_struct*) gain_control_getHandle();
    webrtc::AudioProcessing *apm = (webrtc::AudioProcessing*)ec->apm;
    const pa_sample_spec *rec_ss = &ec->rec_ss;
    const pa_sample_spec *out_ss = &ec->out_ss;
    float **rbuf = ec->rec_buffer;
    int old_volume, new_volume;

    //  input signal wrote on out by lge_preprocess_run's data copy or beamforming
    webrtc::StreamConfig rec_config(out_ss->rate, out_ss->channels, false);
    webrtc::StreamConfig out_config(out_ss->rate, out_ss->channels, false);

    memcpy(ec->rec_buffer[0], out, ec->blocksize * pa_sample_size(&(ec->out_ss)));

    pa_assert_se(apm->ProcessStream(rbuf, rec_config, out_config, rbuf) == webrtc::AudioProcessing::kNoError);

    memcpy(out, ec->rec_buffer[0], ec->blocksize * pa_sample_size(&(ec->out_ss)));
    return true;
}

bool gain_control_done() {
    pa_agc_struct *ec = (pa_agc_struct*) gain_control_getHandle();
    int i;
    pa_log("%s",__FUNCTION__);

    if (ec->apm) {
        delete (webrtc::AudioProcessing*)ec->apm;
        ec->apm = NULL;
    }

    for (i = 0; i < ec->rec_ss.channels; i++)
        pa_xfree(ec->rec_buffer[i]);

    if(agcHandle)
    {
        pa_xfree(agcHandle);
        agcHandle = nullptr;
    }

    return true;
}