/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef fooechocancelhfoo
#define fooechocancelhfoo

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulsecore/core.h>
#include <pulsecore/macro.h>

/* Common data structures */

typedef struct pa_agc_msg pa_agc_msg;

typedef struct pa_agc_params pa_agc_params;

struct pa_agc_params {
    union {
        struct {
            pa_sample_spec out_ss;
        } null;

        struct {
            /* This is a void* so that we don't have to convert this whole file
             * to C++ linkage. apm is a pointer to an AudioProcessing object */
            void *apm;
            unsigned int blocksize; /* in frames */
            pa_sample_spec rec_ss, play_ss, out_ss;
            float *rec_buffer[PA_CHANNELS_MAX], *play_buffer[PA_CHANNELS_MAX]; /* for deinterleaved buffers */
            void *trace_callback;
            bool agc;
            bool first;
            unsigned int agc_start_volume;
        } webrtc;
        /* each agc-specific structure goes here */
    };

    /* Set this if agc can do drift compensation. Also see set_drift()
     * below */
    bool drift_compensation;
};

typedef struct pa_agc_struct pa_agc_struct;

struct pa_agc_struct {
    /* Initialise agc engine. */
    bool   (*init)                      (pa_core *c,
                                         pa_agc_struct *ec,
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
    void        (*play)                 (pa_agc_struct *ec, const uint8_t *play);
    /* Feed the engine 'nframes' record frames. nframes processed frames are
     * returned in out. */
    void        (*record)               (pa_agc_struct *ec, const uint8_t *rec, uint8_t *out);
    /* Feed the engine nframes playback and record frames, with a reasonable
     * effort at keeping the two in sync. nframes processed frames are
     * returned in out. */
    void        (*run)                  (pa_agc_struct *ec, const uint8_t *rec, const uint8_t *play, uint8_t *out);

    /* Optional callback to set the drift, expressed as the ratio of the
     * difference in number of playback and capture samples to the number of
     * capture samples, for some instant of time. This is used only if the
     * agc signals that it supports drift compensation, and is called
     * before record(). The actual implementation needs to derive drift based
     * on point samples -- the individual values are not accurate enough to use
     * as-is. */
    /* NOTE: the semantics of this function might change in the future. */
    void        (*set_drift)            (pa_agc_struct *ec, float drift);

    /* Free up resources. */
    void        (*done)                 (pa_agc_struct *ec);

    /* Structure with common and engine-specific agc parameters. */
    pa_agc_params params;

    /* msgobject that can be used to send messages back to the main thread */
    pa_agc_msg *msg;
};

/* Functions to be used by the agc analog gain control routines */
pa_volume_t pa_agc_get_capture_volume(pa_agc_struct *ec);
void pa_agc_set_capture_volume(pa_agc_struct *ec, pa_volume_t volume);

/* Computes EC block size in frames (rounded down to nearest power-of-2) based
 * on sample rate and milliseconds. */
uint32_t pa_agc_blocksize_power2(unsigned rate, unsigned ms);


/* WebRTC agc functions */
PA_C_DECL_BEGIN
bool pa_webrtc_agc_init(pa_core *c, pa_agc_struct *ec,
                       pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                       pa_sample_spec *play_ss, pa_channel_map *play_map,
                       pa_sample_spec *out_ss, pa_channel_map *out_map,
                       uint32_t *nframes, const char *args);
void pa_webrtc_agc_play(pa_agc_struct *ec, const uint8_t *play);
void pa_webrtc_agc_record(pa_agc_struct *ec, const uint8_t *rec, uint8_t *out);
void pa_webrtc_agc_set_drift(pa_agc_struct *ec, float drift);
void pa_webrtc_agc_run(pa_agc_struct *ec, const uint8_t *rec, const uint8_t *play, uint8_t *out);
void pa_webrtc_agc_done(pa_agc_struct *ec);
PA_C_DECL_END

#endif /* fooechocancelhfoo */
