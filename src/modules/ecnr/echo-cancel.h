/***
    This file is part of PulseAudio.

    Copyright 2010 Arun Raghavan <arun.raghavan@collabora.co.uk>

    PulseAudio is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License,
    or (at your option) any later version.

    PulseAudio is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifndef fooechocancelhfoo
#define fooechocancelhfoo

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulsecore/core.h>
#include <pulsecore/macro.h>

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

#include <ltdl.h>
#include "module_ecnr_c.h"

/* Common data structures */

typedef struct pa_echo_canceller_msg pa_echo_canceller_msg;

typedef struct pa_echo_canceller_params pa_echo_canceller_params;

struct pa_echo_canceller_params {
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
    //  beamformer
    struct {
        bool enable;
        void *apm;
        bool agc;
        bool first;
        unsigned int agc_start_volume;
        bool is_linear_array;
    } beamformer;

    /* Set this if canceller can do drift compensation. Also see set_drift()
     * below */
    bool drift_compensation;
};

typedef struct pa_echo_canceller pa_echo_canceller;

struct pa_echo_canceller {
    /* Initialise canceller engine. */
    bool   (*init)                      (pa_core *c,
                                         pa_echo_canceller *ec,
                                         pa_sample_spec *rec_ss,
                                         pa_channel_map *rec_map,
                                         pa_sample_spec *play_ss,
                                         pa_channel_map *play_map,
                                         pa_sample_spec *out_ss,
                                         pa_channel_map *out_map,
                                         uint32_t *nframes,
                                         const char *args);

    /* You should have only one of play()+record() or run() set. The first
     * works under the assumption that you'll handle buffering and matching up
     * samples yourself. If you set run(), module-echo-cancel will handle
     * synchronising the playback and record streams. */

    /* Feed the engine 'nframes' playback frames. */
    void        (*play)                 (pa_echo_canceller *ec, const uint8_t *play);
    /* Feed the engine 'nframes' record frames. nframes processed frames are
     * returned in out. */
    void        (*record)               (pa_echo_canceller *ec, const uint8_t *rec, uint8_t *out);
    /* Feed the engine nframes playback and record frames, with a reasonable
     * effort at keeping the two in sync. nframes processed frames are
     * returned in out. */
    void        (*run)                  (pa_echo_canceller *ec, const uint8_t *rec, const uint8_t *play, uint8_t *out);

    /* Optional callback to set the drift, expressed as the ratio of the
     * difference in number of playback and capture samples to the number of
     * capture samples, for some instant of time. This is used only if the
     * canceller signals that it supports drift compensation, and is called
     * before record(). The actual implementation needs to derive drift based
     * on point samples -- the individual values are not accurate enough to use
     * as-is. */
    /* NOTE: the semantics of this function might change in the future. */
    void        (*set_drift)            (pa_echo_canceller *ec, float drift);

    /* Free up resources. */
    void        (*done)                 (pa_echo_canceller *ec);

    /* Structure with common and engine-specific canceller parameters. */
    pa_echo_canceller_params params;

    /* msgobject that can be used to send messages back to the main thread */
    pa_echo_canceller_msg *msg;
};

/* Functions to be used by the canceller analog gain control routines */
pa_volume_t pa_echo_canceller_get_capture_volume(pa_echo_canceller *ec);
void pa_echo_canceller_set_capture_volume(pa_echo_canceller *ec, pa_volume_t volume);

/* Computes EC block size in frames (rounded down to nearest power-of-2) based
 * on sample rate and milliseconds. */
uint32_t pa_echo_canceller_blocksize_power2(unsigned rate, unsigned ms);

/* LGE ECNR functions */
bool lge_ecnr_init(pa_core *c, pa_echo_canceller *ec,
                     pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                     pa_sample_spec *play_ss, pa_channel_map *play_map,
                     pa_sample_spec *out_ss, pa_channel_map *out_map,
                     uint32_t *nframes, const char *args);
void lge_ecnr_run(pa_echo_canceller *ec, const uint8_t *rec, const uint8_t *play, uint8_t *out);
void lge_ecnr_done(pa_echo_canceller *ec);

#endif /* fooechocancelhfoo */
