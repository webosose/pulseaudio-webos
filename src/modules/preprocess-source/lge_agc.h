/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef fooagc
#define fooagc

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
PA_C_DECL_BEGIN
#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulsecore/core.h>
#include <pulsecore/macro.h>
PA_C_DECL_END
/* Common data structures */

typedef struct pa_agc_msg pa_agc_msg;


struct pa_agc_struct {

    void *apm;
   /* Set this if agc can do drift compensation. Also see set_drift()
     * below */
    bool drift_compensation;
    unsigned int blocksize; /* in frames */
    pa_sample_spec rec_ss, play_ss, out_ss;
    float *rec_buffer[PA_CHANNELS_MAX], *play_buffer[PA_CHANNELS_MAX], *out_buffer;
    short *s_rec_buf, *s_play_buf, *s_out_buf;
};

typedef struct pa_agc_struct pa_agc_struct;

/* WebRTC agc functions */
PA_C_DECL_BEGIN
bool gain_control_init(void *handle,
                       pa_sample_spec rec_ss, pa_channel_map rec_map,
                       pa_sample_spec play_ss, pa_channel_map play_map,
                       pa_sample_spec out_ss, pa_channel_map out_map,
                       uint32_t nframes, const char *args);
bool gain_control_process(void *handle, const uint8_t *rec, const uint8_t *play, uint8_t *out);
bool gain_control_done(void *handle);
void *gain_control_getHandle();
PA_C_DECL_END

#endif /* fooechocancelhfoo */
