/*
 * Copyright (c) 2023 LG Electronics Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/cdecl.h>

PA_C_DECL_BEGIN
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>

#include <pulse/timeval.h>
#include "agc.h"
PA_C_DECL_END

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

static void webrtc_ec_fixate_spec(pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                                  pa_sample_spec *play_ss, pa_channel_map *play_map,
                                  pa_sample_spec *out_ss, pa_channel_map *out_map,
                                  bool beamforming)
{
    rec_ss->format = PA_SAMPLE_FLOAT32NE;
    //play_ss->format = PA_SAMPLE_FLOAT32NE;

    /* AudioProcessing expects one of the following rates */
    if (rec_ss->rate >= 48000)
        rec_ss->rate = 48000;
    else if (rec_ss->rate >= 32000)
        rec_ss->rate = 32000;
    else if (rec_ss->rate >= 16000)
        rec_ss->rate = 16000;
    else
        rec_ss->rate = 8000;

    *out_ss = *rec_ss;
    *out_map = *rec_map;

    if (beamforming) {
        /* The beamformer gives us a single channel */
        out_ss->channels = 1;
        pa_channel_map_init_mono(out_map);
    }
}

static bool parse_point(const char **point, float (&f)[3]) {
    int ret, length;

    ret = sscanf(*point, "%g,%g,%g%n", &f[0], &f[1], &f[2], &length);
    if (ret != 3)
        return false;

    /* Consume the bytes we've read so far */
    *point += length;

    return true;
}

bool pa_webrtc_agc_init(pa_core *c, pa_agc_struct *ec,
                       pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                       pa_sample_spec *play_ss, pa_channel_map *play_map,
                       pa_sample_spec *out_ss, pa_channel_map *out_map,
                       uint32_t *nframes, const char *args) {

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

    webrtc_ec_fixate_spec(rec_ss, rec_map, play_ss, play_map, out_ss, out_map, false);

    pa_log_info("Config %s", config.ToString().c_str());

    ec->params.webrtc.blocksize = (uint64_t) out_ss->rate * BLOCK_SIZE_US / PA_USEC_PER_SEC;
    *nframes = ec->params.webrtc.blocksize;

    for (int i = 0; i < rec_ss->channels; i++)
        ec->params.webrtc.rec_buffer[i] = pa_xnew(float, *nframes);

    ec->params.webrtc.apm = apm;
    ec->params.webrtc.rec_ss = *rec_ss;
    ec->params.webrtc.out_ss = *out_ss;
    pa_log_info("Done init function");

    return true;
}

void pa_webrtc_agc_play(pa_agc_struct *ec, const uint8_t *play) {

}

void pa_webrtc_agc_record(pa_agc_struct *ec, const uint8_t *rec, uint8_t *out) {

}

void pa_webrtc_agc_set_drift(pa_agc_struct *ec, float drift) {

}

void pa_webrtc_agc_run(pa_agc_struct *ec, const uint8_t *rec, const uint8_t *play, uint8_t *out) {
    webrtc::AudioProcessing *apm = (webrtc::AudioProcessing*)ec->params.webrtc.apm;
    const pa_sample_spec *rec_ss = &ec->params.webrtc.rec_ss;
    const pa_sample_spec *out_ss = &ec->params.webrtc.out_ss;
    float **buf = ec->params.webrtc.rec_buffer;
    int n = ec->params.webrtc.blocksize;
    int old_volume, new_volume;
    webrtc::StreamConfig rec_config(rec_ss->rate, rec_ss->channels, false);
    webrtc::StreamConfig out_config(out_ss->rate, out_ss->channels, false);

    pa_deinterleave(rec, (void **) buf, rec_ss->channels, pa_sample_size(rec_ss), n);
    pa_assert_se(apm->ProcessStream(buf, rec_config, out_config, buf) == webrtc::AudioProcessing::kNoError);
    pa_interleave((const void **) buf, out_ss->channels, out, pa_sample_size(out_ss), n);

    pa_log_info("inside webrtc_1.cc run function");
}

void pa_webrtc_agc_done(pa_agc_struct *ec) {
    int i;

    if (ec->params.webrtc.apm) {
        delete (webrtc::AudioProcessing*)ec->params.webrtc.apm;
        ec->params.webrtc.apm = NULL;
    }

    for (i = 0; i < ec->params.webrtc.rec_ss.channels; i++)
        pa_xfree(ec->params.webrtc.rec_buffer[i]);

    pa_log_info(" inside webrtc_1.cc done function");
}
