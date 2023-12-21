/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#include "lge_beamforming.h"

static void beamforming_fixate_spec(pa_beamforming_params *ec, pa_sample_spec rec_ss, pa_channel_map rec_map,
                                  pa_sample_spec play_ss, pa_channel_map play_map,
                                  pa_sample_spec out_ss, pa_channel_map out_map, bool beamformer) {
    ec->rec_ss = rec_ss;
    ec->play_ss = play_ss;
    ec->out_ss = out_ss;
}


static void get_mic_geometry(std::vector<webrtc_ecnr::Point>& geometry) {
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

void *beamforming_getHandle()
{
    if(!beamformingHandle)
    beamformingHandle = pa_xnew(pa_beamforming_params, 1);
    return beamformingHandle;
}

bool beamforming_init_internal(pa_beamforming_params *ec, const char *args) {

    webrtc_ecnr::AudioProcessing *apm = NULL;
    webrtc_ecnr::ProcessingConfig pconfig;
    webrtc_ecnr::Config config;
    bool hpf, agc, auto_aim;
    uint32_t agc_start_volume;

    hpf = true;
    agc = false;
    agc_start_volume = 16;
    auto_aim = true;

    /* We do this after fixate because we need the capture channel count */
    std::vector<webrtc_ecnr::Point> geometry(ec->rec_ss.channels);
    webrtc_ecnr::SphericalPointf direction(M_PI_2, 0.0f, 0.0f);
    const char *mic_geometry;
    float inner_product;

    //  mic geometry check
    get_mic_geometry(geometry);

    inner_product = 0;
    for (int i = 0; i < geometry.size(); i++)
        inner_product += geometry[i].c[0] * geometry[i].c[1];
    if (inner_product == 0) ec->is_linear_array = true;
    else ec->is_linear_array = false;

    config.Set<webrtc_ecnr::Beamforming>(new webrtc_ecnr::Beamforming(true, geometry, direction));

    apm = webrtc_ecnr::AudioProcessing::Create(config);

    pconfig = {
        webrtc_ecnr::StreamConfig(ec->rec_ss.rate, ec->rec_ss.channels, false), /* input stream */
        webrtc_ecnr::StreamConfig(ec->out_ss.rate, ec->out_ss.channels, false), /* output stream */
        webrtc_ecnr::StreamConfig(ec->play_ss.rate, ec->play_ss.channels, false), /* reverse input stream */
        webrtc_ecnr::StreamConfig(ec->play_ss.rate, ec->play_ss.channels, false), /* reverse output stream */
    };
    if (apm->Initialize(pconfig) != webrtc_ecnr::AudioProcessing::kNoError) {
        pa_log("ECNR: Error initialising audio processing module");
        goto fail;
    }

    if (hpf)
        apm->high_pass_filter()->Enable(true);

    apm->set_beamformer_auto_aim(auto_aim);

    ec->apm = apm;
    ec->first = true;

    return true;

fail:
    if (apm)
        delete apm;

    return false;
}


void beamforming_play(pa_beamforming_params *ec) {
    webrtc_ecnr::AudioProcessing *apm = (webrtc_ecnr::AudioProcessing*)ec->apm;
    const pa_sample_spec *ss = &ec->play_ss;
    float **buf = ec->play_buffer;
    webrtc_ecnr::StreamConfig config(ss->rate, ss->channels, false);

    pa_assert_se(apm->ProcessReverseStream(buf, config, config, buf) == webrtc_ecnr::AudioProcessing::kNoError);
}

static pa_volume_t webrtc_volume_to_pa(int v)
{
    return (v * PA_VOLUME_NORM) / 255;
}

void beamforming_record(pa_beamforming_params *ec) {
    webrtc_ecnr::AudioProcessing *apm = (webrtc_ecnr::AudioProcessing*)ec->apm;
    const pa_sample_spec *rec_ss = &ec->rec_ss;
    const pa_sample_spec *out_ss = &ec->out_ss;
    float **buf = ec->rec_buffer;
    int old_volume, new_volume;
    webrtc_ecnr::StreamConfig rec_config(rec_ss->rate, rec_ss->channels, false);
    webrtc_ecnr::StreamConfig out_config(out_ss->rate, out_ss->channels, false);
    apm->set_stream_delay_ms(0);
    pa_assert_se(apm->ProcessStream(buf, rec_config, out_config, buf) == webrtc_ecnr::AudioProcessing::kNoError);

}


bool beamforming_process(const uint8_t *rec, const uint8_t *play, uint8_t *out) {
    //pa_log("beamforming_process");

    pa_beamforming_params *ec = (pa_beamforming_params*)beamforming_getHandle();
    if (!ec->enable) {
        pa_log("uninit beamforming, dont process");
        return true;
    }
    const pa_sample_spec *play_ss = &ec->play_ss;
    const pa_sample_spec *rec_ss = &ec->rec_ss;
    const pa_sample_spec *out_ss = &ec->out_ss;

    int n = ec->blocksize;
    //pa_log("ec->blocksize  %d,%d",ec->blocksize ,n);
    float **pbuf = ec->play_buffer;
    float **rbuf = ec->rec_buffer;

    pa_deinterleave(play, (void **) pbuf, play_ss->channels, pa_sample_size(play_ss), n);
    pa_deinterleave(rec, (void **) rbuf, rec_ss->channels, pa_sample_size(rec_ss), n);

    if (ec->enable) {
        beamforming_play(ec);
        beamforming_record(ec);
    }

    pa_interleave((const void **) rbuf, out_ss->channels, out, pa_sample_size(out_ss), n);
    return true;

}

bool beamforming_done() {
    pa_log("%s",__FUNCTION__);
    //  free apm
    pa_beamforming_params *ec = (pa_beamforming_params*)beamforming_getHandle();
    if (!ec->enable) {
        pa_log("uninit beamforming, dont process");
        return true;
    }
    if (ec->apm) {
        delete (webrtc_ecnr::AudioProcessing*)ec->apm;
        ec->apm = NULL;
    }

    //  free buffers
    for (int i = 0; i < ec->rec_ss.channels; i++)
        pa_xfree(ec->rec_buffer[i]);
    for (int i = 0; i < ec->play_ss.channels; i++)
        pa_xfree(ec->play_buffer[i]);

    pa_xfree(ec->s_rec_buf);
    pa_xfree(ec->s_play_buf);
    pa_xfree(ec->s_out_buf);

    if(beamformingHandle)
    {
        pa_xfree(beamformingHandle);
        beamformingHandle = nullptr;
    }

    pa_log_debug("beamforming: finalized");
    return true;

}


bool beamforming_init(pa_sample_spec rec_ss, pa_channel_map rec_map,
                     pa_sample_spec play_ss, pa_channel_map play_map,
                     pa_sample_spec out_ss, pa_channel_map out_map,
                     uint32_t nframes, const char *args) {

    pa_beamforming_params *ec = (pa_beamforming_params*)beamforming_getHandle();

    ec->enable = true;

    pa_log("beamforming_init rec_ss.channels %d playss.channels %d",rec_ss.channels,play_ss.channels);
    if (rec_ss.channels < 4)
    {
        pa_log("beamforming_init not doing as channel count is not supported");
        ec->enable = false;
        return true;
    }

    beamforming_fixate_spec(ec, rec_ss, rec_map, play_ss, play_map, out_ss, out_map, ec->enable);

    ec->blocksize = 128;     //(uint64_t) out_ss->rate * BLOCK_SIZE_US / PA_USEC_PER_SEC;
    int numframes = ec->blocksize;

    if (!beamforming_init_internal(ec, args)) {
        pa_log("ECNR: beamformer initialization failed");
        return false;
    }


    for (int i = 0; i < rec_ss.channels; i++)
        ec->rec_buffer[i] = pa_xnew(float, numframes);
    for (int i = 0; i < play_ss.channels; i++)
        ec->play_buffer[i] = pa_xnew(float, numframes);
    ec->out_buffer = pa_xnew(float, numframes);

    ec->s_rec_buf = pa_xnew(short, numframes);
    ec->s_play_buf = pa_xnew(short,numframes);
    ec->s_out_buf = pa_xnew(short, numframes);
    pa_log_debug("ec->blocksize %d",ec->blocksize);
    return true;
}