/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <math.h>

#include "agc.h"

#include <pulse/xmalloc.h>
#include <pulse/timeval.h>
#include <pulse/rtclock.h>

#include <pulsecore/i18n.h>
#include <pulsecore/atomic.h>
#include <pulsecore/macro.h>
#include <pulsecore/namereg.h>
#include <pulsecore/module.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/sample-util.h>
#include <pulsecore/ltdl-helper.h>

PA_MODULE_AUTHOR("LG Electronics");
PA_MODULE_DESCRIPTION("AGC Implementation based on webrtc");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(false);
PA_MODULE_USAGE(
        _("source_name=<name for the source> "
          "source_properties=<properties for the source> "
          "source_master=<name of source to filter> "
          "adjust_time=<how often to readjust rates in s> "
          "adjust_threshold=<how much drift to readjust after in ms> "
          "format=<sample format> "
          "rate=<sample rate> "
          "channels=<number of channels> "
          "channel_map=<channel map> "
          "aec_args=<parameters for the AEC engine> "
          "save_aec=<save AEC data in /tmp> "
          "autoloaded=<set if this module is being loaded automatically> "
          "use_volume_sharing=<yes or no> "
          "use_master_format=<yes or no> "
        ));


static const pa_agc_struct ec_table[] = {
    {
        /* WebRTC's audio processing engine */
        .init                   = pa_webrtc_agc_init,
        .play                   = pa_webrtc_agc_play,
        .record                 = pa_webrtc_agc_record,
        .set_drift              = pa_webrtc_agc_set_drift,
        .run                    = pa_webrtc_agc_run,
        .done                   = pa_webrtc_agc_done,
    },
};

#define DEFAULT_RATE 32000
#define DEFAULT_CHANNELS 1
#define DEFAULT_ADJUST_TIME_USEC (1*PA_USEC_PER_SEC)
#define DEFAULT_ADJUST_TOLERANCE (5*PA_USEC_PER_MSEC)
#define DEFAULT_SAVE_AEC false
#define DEFAULT_AUTOLOADED false
#define DEFAULT_USE_MASTER_FORMAT false

#define MEMBLOCKQ_MAXLENGTH (16*1024*1024)

#define MAX_LATENCY_BLOCKS 10

/* Can only be used in main context */
#define IS_ACTIVE(u) (((u)->source->state == PA_SOURCE_RUNNING))

/* This module creates a new (virtual) source.
 *
 * Data read from source_master is matched against the save data and
 * agc data is then pushed onto the new source.
 *
 * Both source and  masters have their own threads to push/pull data
 * respectively. We however perform all our actions in the source IO thread.
 * To do this we send all played samples to the source IO thread where they
 * are then pushed into the memblockq.
 *
 */

struct userdata;

struct pa_agc_msg {
    pa_msgobject parent;
    bool dead;
    struct userdata *userdata;
};

PA_DEFINE_PRIVATE_CLASS(pa_agc_msg, pa_msgobject);
#define PA_AGC_MSG(o) (pa_agc_msg_cast(o))

struct snapshot {
    int64_t send_counter;

    pa_usec_t source_now;
    pa_usec_t source_latency;
    size_t source_delay;
    int64_t recv_counter;
    size_t rlen;
    size_t plen;
};

struct userdata {
    pa_core *core;
    pa_module *module;

    bool dead;
    bool save_aec;

    pa_agc_struct *ec;
    uint32_t source_output_blocksize;
    uint32_t source_blocksize;

    bool need_realign;

    /* to wakeup the source I/O thread */
    pa_asyncmsgq *asyncmsgq;
    pa_rtpoll_item *rtpoll_item_read, *rtpoll_item_write;

    pa_source *source;
    bool source_auto_desc;
    pa_source_output *source_output;
    pa_memblockq *source_memblockq;
    size_t source_skip;

    int64_t recv_counter;

    /* Bytes left over from previous iteration */
    size_t source_rem;

    pa_atomic_t request_resync;

    pa_time_event *time_event;
    pa_usec_t adjust_time;
    int adjust_threshold;

    FILE *captured_file;
    FILE *played_file;
    FILE *canceled_file;
    FILE *drift_file;

    bool use_volume_sharing;

    struct {
        pa_cvolume current_volume;
    } thread_info;
};

static void source_output_snapshot_within_thread(struct userdata *u, struct snapshot *snapshot);

static const char* const valid_modargs[] = {
    "source_name",
    "source_properties",
    "source_master",
    "adjust_time",
    "adjust_threshold",
    "format",
    "rate",
    "channels",
    "channel_map",
    "aec_method",
    "aec_args",
    "save_aec",
    "autoloaded",
    "use_volume_sharing",
    "use_master_format",
    NULL
};

enum {
    SOURCE_OUTPUT_MESSAGE_POST = PA_SOURCE_OUTPUT_MESSAGE_MAX,
    SOURCE_OUTPUT_MESSAGE_REWIND,
    SOURCE_OUTPUT_MESSAGE_LATENCY_SNAPSHOT,
    SOURCE_OUTPUT_MESSAGE_APPLY_DIFF_TIME
};

enum {
    AGC_MESSAGE_SET_VOLUME,
};

static int64_t calc_diff(struct userdata *u, struct snapshot *snapshot) {
    int64_t diff_time, buffer_latency;
    pa_usec_t plen, rlen, source_delay, recv_counter, send_counter;

    /* get latency difference between playback and record */
    rlen = pa_bytes_to_usec(snapshot->rlen, &u->source_output->sample_spec);
    if (plen > rlen)
        buffer_latency = plen - rlen;
    else
        buffer_latency = 0;

    source_delay = pa_bytes_to_usec(snapshot->source_delay, &u->source_output->sample_spec);
    buffer_latency += source_delay;

    if (recv_counter <= send_counter)
        buffer_latency += (int64_t) (send_counter - recv_counter);
    else
        buffer_latency = PA_CLIP_SUB(buffer_latency, (int64_t) (recv_counter - send_counter));

    return diff_time;
}

/* Called from main context */
static void time_callback(pa_mainloop_api *a, pa_time_event *e, const struct timeval *t, void *userdata) {
    struct userdata *u = userdata;
    uint32_t old_rate, base_rate, new_rate;
    int64_t diff_time;
    /*size_t fs*/
    struct snapshot latency_snapshot;

    pa_assert(u);
    pa_assert(a);
    pa_assert(u->time_event == e);
    pa_assert_ctl_context();

    if (!IS_ACTIVE(u))
        return;

    /* update our snapshots */
    pa_asyncmsgq_send(u->source_output->source->asyncmsgq, PA_MSGOBJECT(u->source_output), SOURCE_OUTPUT_MESSAGE_LATENCY_SNAPSHOT, &latency_snapshot, 0, NULL);

    /* calculate drift between capture and playback */
    diff_time = calc_diff(u, &latency_snapshot);

    /*fs = pa_frame_size(&u->source_output->sample_spec);*/
    base_rate = u->source_output->sample_spec.rate;

    if (diff_time < 0) {
        /* recording before playback, we need to adjust quickly. The
         * agc does not work in this case. */
        pa_asyncmsgq_post(u->asyncmsgq, PA_MSGOBJECT(u->source_output), SOURCE_OUTPUT_MESSAGE_APPLY_DIFF_TIME,
            NULL, diff_time, NULL, NULL);
        /*new_rate = base_rate - ((pa_usec_to_bytes(-diff_time, &u->source_output->sample_spec) / fs) * PA_USEC_PER_SEC) / u->adjust_time;*/
        new_rate = base_rate;
    }
    else {
        if (diff_time > u->adjust_threshold) {
            /* diff too big, quickly adjust */
            pa_asyncmsgq_post(u->asyncmsgq, PA_MSGOBJECT(u->source_output), SOURCE_OUTPUT_MESSAGE_APPLY_DIFF_TIME,
                NULL, diff_time, NULL, NULL);
        }

        /* recording behind playback, we need to slowly adjust the rate to match */
        /*new_rate = base_rate + ((pa_usec_to_bytes(diff_time, &u->source_output->sample_spec) / fs) * PA_USEC_PER_SEC) / u->adjust_time;*/

        /* assume equal samplerates for now */
        new_rate = base_rate;
    }

    /* make sure we don't make too big adjustments because that sounds horrible */
    if (new_rate > base_rate * 1.1 || new_rate < base_rate * 0.9)
        new_rate = base_rate;

    if (new_rate != old_rate) {
        pa_log_info("Old rate %lu Hz, new rate %lu Hz", (unsigned long) old_rate, (unsigned long) new_rate);
    }

    pa_core_rttime_restart(u->core, u->time_event, pa_rtclock_now() + u->adjust_time);
}

/* Called from source I/O thread context */
static int source_process_msg_cb(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SOURCE(o)->userdata;

    switch (code) {

        case PA_SOURCE_MESSAGE_GET_LATENCY:

            /* The source is _put() before the source output is, so let's
             * make sure we don't access it in that time. Also, the
             * source output is first shut down, the source second. */
            if (!PA_SOURCE_IS_LINKED(u->source->thread_info.state) ||
                !PA_SOURCE_OUTPUT_IS_LINKED(u->source_output->thread_info.state)) {
                *((int64_t*) data) = 0;
                return 0;
            }

            *((int64_t*) data) =

                /* Get the latency of the master source */
                pa_source_get_latency_within_thread(u->source_output->source, true) +
                /* Add the latency internal to our source output on top */
                pa_bytes_to_usec(pa_memblockq_get_length(u->source_output->thread_info.delay_memblockq), &u->source_output->source->sample_spec) +
                /* and the buffering we do on the source */
                pa_bytes_to_usec(u->source_output_blocksize, &u->source_output->source->sample_spec);

            return 0;

        case PA_SOURCE_MESSAGE_SET_VOLUME_SYNCED:
            u->thread_info.current_volume = u->source->reference_volume;
            break;
    }

    return pa_source_process_msg(o, code, data, offset, chunk);
}

/* Called from main context */
static int source_set_state_in_main_thread_cb(pa_source *s, pa_source_state_t state, pa_suspend_cause_t suspend_cause) {
    struct userdata *u;

    pa_source_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SOURCE_IS_LINKED(state) ||
        !PA_SOURCE_OUTPUT_IS_LINKED(u->source_output->state))
        return 0;

    if (state == PA_SOURCE_RUNNING) {
        pa_atomic_store(&u->request_resync, 1);
        pa_source_output_cork(u->source_output, false);
    } else if (state == PA_SOURCE_SUSPENDED) {
        pa_source_output_cork(u->source_output, true);
    }

    return 0;
}

/* Called from source I/O thread context */
static void source_update_requested_latency_cb(pa_source *s) {
    struct userdata *u;
    pa_usec_t latency;

    pa_source_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SOURCE_IS_LINKED(u->source->thread_info.state) ||
        !PA_SOURCE_OUTPUT_IS_LINKED(u->source_output->thread_info.state))
        return;

    pa_log_debug("Source update requested latency");

    /* Cap the maximum latency so we don't have to process too large chunks */
    latency = PA_MIN(pa_source_get_requested_latency_within_thread(s),
                     pa_bytes_to_usec(u->source_blocksize, &s->sample_spec) * MAX_LATENCY_BLOCKS);

    pa_source_output_set_requested_latency_within_thread(u->source_output, latency);
}

/* Called from main context */
static void source_set_volume_cb(pa_source *s) {
    struct userdata *u;

    pa_source_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SOURCE_IS_LINKED(s->state) ||
        !PA_SOURCE_OUTPUT_IS_LINKED(u->source_output->state))
        return;

    pa_source_output_set_volume(u->source_output, &s->real_volume, s->save_volume, true);
}

/* Called from main context. */
static void source_get_volume_cb(pa_source *s) {
    struct userdata *u;
    pa_cvolume v;

    pa_source_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SOURCE_IS_LINKED(s->state) ||
        !PA_SOURCE_OUTPUT_IS_LINKED(u->source_output->state))
        return;

    pa_source_output_get_volume(u->source_output, &v, true);

    if (pa_cvolume_equal(&s->real_volume, &v))
        /* no change */
        return;

    s->real_volume = v;
    pa_source_set_soft_volume(s, NULL);
}

/* Called from main context */
static void source_set_mute_cb(pa_source *s) {
    struct userdata *u;

    pa_source_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SOURCE_IS_LINKED(s->state) ||
        !PA_SOURCE_OUTPUT_IS_LINKED(u->source_output->state))
        return;

    pa_source_output_set_mute(u->source_output, s->muted, s->save_muted);
}

/* This one's simpler than the drift compensation case -- we just iterate over
 * the capture buffer, and pass the agc blocksize bytes of playback and
 * capture data. If playback is currently inactive, we just push silence.
 *
 * Called from source I/O thread context. */
static void do_push(struct userdata *u) {
    size_t rlen;
    pa_memchunk rchunk, pchunk, cchunk;
    uint8_t *rdata, *pdata, *cdata;
    int unused PA_GCC_UNUSED;

    rlen = pa_memblockq_get_length(u->source_memblockq);

    while (rlen >= u->source_output_blocksize) {

        /* take fixed blocks from recorded and played samples */
        pa_memblockq_peek_fixed_size(u->source_memblockq, u->source_output_blocksize, &rchunk);

        rdata = pa_memblock_acquire(rchunk.memblock);
        rdata += rchunk.index;

        cchunk.index = 0;
        cchunk.length = u->source_blocksize;
        cchunk.memblock = pa_memblock_new(u->source->core->mempool, cchunk.length);
        cdata = pa_memblock_acquire(cchunk.memblock);

        if (u->save_aec) {
            if (u->captured_file)
                unused = fwrite(rdata, 1, u->source_output_blocksize, u->captured_file);
        }

        /* perform AGC */
        u->ec->run(u->ec, rdata, pdata, cdata);

        if (u->save_aec) {
            if (u->canceled_file)
                unused = fwrite(cdata, 1, u->source_blocksize, u->canceled_file);
        }

        pa_memblock_release(cchunk.memblock);
        pa_memblock_release(rchunk.memblock);

        /* drop consumed source samples */
        pa_memblockq_drop(u->source_memblockq, u->source_output_blocksize);
        pa_memblock_unref(rchunk.memblock);
        rlen -= u->source_output_blocksize;

        /* forward the AGC-ed data to the virtual source */
        pa_source_post(u->source, &cchunk);
        pa_memblock_unref(cchunk.memblock);
    }
}

/* Called from source I/O thread context. */
static void source_output_push_cb(pa_source_output *o, const pa_memchunk *chunk) {
    struct userdata *u;
    size_t rlen, plen, to_skip;
    pa_memchunk rchunk;

    pa_source_output_assert_ref(o);
    pa_source_output_assert_io_context(o);
    pa_assert_se(u = o->userdata);

    if (!PA_SOURCE_IS_LINKED(u->source->thread_info.state))
        return;

    if (!PA_SOURCE_OUTPUT_IS_LINKED(u->source_output->thread_info.state)) {
        pa_log("Push when no link?");
        return;
    }

    /* handle queued messages, do any message sending of our own */
    while (pa_asyncmsgq_process_one(u->asyncmsgq) > 0)
        ;

    pa_memblockq_push_align(u->source_memblockq, chunk);

    rlen = pa_memblockq_get_length(u->source_memblockq);

    /* Let's not do anything else till we have enough data to process */
    if (rlen < u->source_output_blocksize)
        return;

    /* See if we need to drop samples in order to sync */
    if (pa_atomic_cmpxchg (&u->request_resync, 1, 0)) {
        //do_resync(u);
    }

    /* Okay, skip agc for skipped source samples if needed. */
    if (PA_UNLIKELY(u->source_skip)) {
        /* The slightly tricky bit here is that we drop all but modulo
         * blocksize bytes and then adjust for that last bit on the  side.
         * We do this because the source data is coming at a fixed rate, which
         * means the only way to try to catch up is drop  samples and let
         * the agc cope up with this. */
        to_skip = rlen >= u->source_skip ? u->source_skip : rlen;
        to_skip -= to_skip % u->source_output_blocksize;

        if (to_skip) {
            pa_memblockq_peek_fixed_size(u->source_memblockq, to_skip, &rchunk);
            pa_source_post(u->source, &rchunk);

            pa_memblock_unref(rchunk.memblock);
            pa_memblockq_drop(u->source_memblockq, to_skip);

            rlen -= to_skip;
            u->source_skip -= to_skip;
        }

        if (rlen && u->source_skip % u->source_output_blocksize) {
            u->source_skip -= (u->source_skip % u->source_output_blocksize);
        }
    }


    /* process and push out samples */
    if (u->ec->params.drift_compensation)
    {}
        //do_push_drift_comp(u);
    else
        do_push(u);
}

/* Called from source I/O thread context. */
static void source_output_process_rewind_cb(pa_source_output *o, size_t nbytes) {
    struct userdata *u;

    pa_source_output_assert_ref(o);
    pa_source_output_assert_io_context(o);
    pa_assert_se(u = o->userdata);

    /* If the source is not yet linked, there is nothing to rewind */
    if (!PA_SOURCE_IS_LINKED(u->source->thread_info.state))
        return;

    pa_source_process_rewind(u->source, nbytes);

    /* manipulate write index */
    pa_memblockq_seek(u->source_memblockq, -nbytes, PA_SEEK_RELATIVE, true);

    pa_log_debug("Source rewind (%lld) %lld", (long long) nbytes,
        (long long) pa_memblockq_get_length (u->source_memblockq));
}

/* Called from source I/O thread context. */
static void source_output_snapshot_within_thread(struct userdata *u, struct snapshot *snapshot) {
    size_t delay, rlen, plen;
    pa_usec_t now, latency;

    now = pa_rtclock_now();
    latency = pa_source_get_latency_within_thread(u->source_output->source, false);
    delay = pa_memblockq_get_length(u->source_output->thread_info.delay_memblockq);

    delay = (u->source_output->thread_info.resampler ? pa_resampler_request(u->source_output->thread_info.resampler, delay) : delay);
    rlen = pa_memblockq_get_length(u->source_memblockq);

    snapshot->source_now = now;
    snapshot->source_latency = latency;
    snapshot->source_delay = delay;
    snapshot->recv_counter = u->recv_counter;
    snapshot->plen = plen + u->source_skip;
}

/* Called from source I/O thread context. */
static int source_output_process_msg_cb(pa_msgobject *obj, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SOURCE_OUTPUT(obj)->userdata;

    switch (code) {

        case SOURCE_OUTPUT_MESSAGE_POST:

            pa_source_output_assert_io_context(u->source_output);

            u->recv_counter += (int64_t) chunk->length;

            return 0;

        case SOURCE_OUTPUT_MESSAGE_REWIND:
            pa_source_output_assert_io_context(u->source_output);

            u->recv_counter -= offset;

            return 0;

        case SOURCE_OUTPUT_MESSAGE_LATENCY_SNAPSHOT: {
            struct snapshot *snapshot = (struct snapshot *) data;

            source_output_snapshot_within_thread(u, snapshot);
            return 0;
        }

        case SOURCE_OUTPUT_MESSAGE_APPLY_DIFF_TIME:
            //apply_diff_time(u, offset);
            return 0;

    }

    return pa_source_output_process_msg(obj, code, data, offset, chunk);
}

/* Called from source I/O thread context. */
static void source_output_update_max_rewind_cb(pa_source_output *o, size_t nbytes) {
    struct userdata *u;

    pa_source_output_assert_ref(o);
    pa_assert_se(u = o->userdata);

    pa_log_debug("Source output update max rewind %lld", (long long) nbytes);

    pa_source_set_max_rewind_within_thread(u->source, nbytes);
}

/* Called from source I/O thread context. */
static void source_output_update_source_requested_latency_cb(pa_source_output *o) {
    struct userdata *u;
    pa_usec_t latency;

    pa_source_output_assert_ref(o);
    pa_assert_se(u = o->userdata);

    latency = pa_source_get_requested_latency_within_thread(o->source);

    pa_log_debug("Source output update requested latency %lld", (long long) latency);
}

/* Called from source I/O thread context. */
static void source_output_update_source_latency_range_cb(pa_source_output *o) {
    struct userdata *u;

    pa_source_output_assert_ref(o);
    pa_assert_se(u = o->userdata);

    pa_log_debug("Source output update latency range %lld %lld",
        (long long) o->source->thread_info.min_latency,
        (long long) o->source->thread_info.max_latency);

    pa_source_set_latency_range_within_thread(u->source, o->source->thread_info.min_latency, o->source->thread_info.max_latency);
}

/* Called from source I/O thread context. */
static void source_output_update_source_fixed_latency_cb(pa_source_output *o) {
    struct userdata *u;

    pa_source_output_assert_ref(o);
    pa_assert_se(u = o->userdata);

    pa_log_debug("Source output update fixed latency %lld",
        (long long) o->source->thread_info.fixed_latency);

    pa_source_set_fixed_latency_within_thread(u->source, o->source->thread_info.fixed_latency);
}

/* Called from source I/O thread context. */
static void source_output_attach_cb(pa_source_output *o) {
    struct userdata *u;

    pa_source_output_assert_ref(o);
    pa_source_output_assert_io_context(o);
    pa_assert_se(u = o->userdata);

    pa_source_set_rtpoll(u->source, o->source->thread_info.rtpoll);
    pa_source_set_latency_range_within_thread(u->source, o->source->thread_info.min_latency, o->source->thread_info.max_latency);
    pa_source_set_fixed_latency_within_thread(u->source, o->source->thread_info.fixed_latency);
    pa_source_set_max_rewind_within_thread(u->source, pa_source_output_get_max_rewind(o));

    pa_log_debug("Source output %d attach", o->index);

    if (PA_SOURCE_IS_LINKED(u->source->thread_info.state))
        pa_source_attach_within_thread(u->source);

    u->rtpoll_item_read = pa_rtpoll_item_new_asyncmsgq_read(
            o->source->thread_info.rtpoll,
            PA_RTPOLL_LATE,
            u->asyncmsgq);
}

/* Called from source I/O thread context. */
static void source_output_detach_cb(pa_source_output *o) {
    struct userdata *u;

    pa_source_output_assert_ref(o);
    pa_source_output_assert_io_context(o);
    pa_assert_se(u = o->userdata);

    if (PA_SOURCE_IS_LINKED(u->source->thread_info.state))
        pa_source_detach_within_thread(u->source);
    pa_source_set_rtpoll(u->source, NULL);

    pa_log_debug("Source output %d detach", o->index);

    if (u->rtpoll_item_read) {
        pa_rtpoll_item_free(u->rtpoll_item_read);
        u->rtpoll_item_read = NULL;
    }
}

/* Called from source I/O thread context except when cork() is called without valid source. */
static void source_output_state_change_cb(pa_source_output *o, pa_source_output_state_t state) {
    struct userdata *u;

    pa_source_output_assert_ref(o);
    pa_assert_se(u = o->userdata);

    pa_log_debug("Source output %d state %d", o->index, state);
}

/* Called from main context. */
static void source_output_kill_cb(pa_source_output *o) {
    struct userdata *u;

    pa_source_output_assert_ref(o);
    pa_assert_ctl_context();
    pa_assert_se(u = o->userdata);

    u->dead = true;

    /* The order here matters! We first kill the source so that streams can
     * properly be moved away while the source output is still connected to
     * the master. */
    pa_source_output_cork(u->source_output, true);
    pa_source_unlink(u->source);
    pa_source_output_unlink(u->source_output);

    pa_source_output_unref(u->source_output);
    u->source_output = NULL;

    pa_source_unref(u->source);
    u->source = NULL;

    pa_log_debug("Source output kill %d", o->index);

    pa_module_unload_request(u->module, true);
}

/* Called from main context. */
static bool source_output_may_move_to_cb(pa_source_output *o, pa_source *dest) {
    struct userdata *u;

    pa_source_output_assert_ref(o);
    pa_assert_ctl_context();
    pa_assert_se(u = o->userdata);

    if (u->dead)
        return false;

    return (u->source != dest);
}

/* Called from main context. */
static void source_output_moving_cb(pa_source_output *o, pa_source *dest) {
    struct userdata *u;
    uint32_t idx;
    pa_source_output *output;

    pa_source_output_assert_ref(o);
    pa_assert_ctl_context();
    pa_assert_se(u = o->userdata);

    if (dest) {
        pa_source_set_asyncmsgq(u->source, dest->asyncmsgq);
        pa_source_update_flags(u->source, PA_SOURCE_LATENCY|PA_SOURCE_DYNAMIC_LATENCY, dest->flags);
    } else
        pa_source_set_asyncmsgq(u->source, NULL);

    /* Propagate asyncmsq change to attached virtual sources */
    PA_IDXSET_FOREACH(output, u->source->outputs, idx) {
        if (output->destination_source && output->moving)
            output->moving(output, u->source);
    }

    if (u->source_auto_desc && dest) {
        const char *y, *z;
        pa_proplist *pl;

        pl = pa_proplist_new();

        z = pa_proplist_gets(dest->proplist, PA_PROP_DEVICE_DESCRIPTION);
        pa_proplist_setf(pl, PA_PROP_DEVICE_DESCRIPTION, "%s (agc with)", z ? z : dest->name);

        pa_source_update_proplist(u->source, PA_UPDATE_REPLACE, pl);
        pa_proplist_free(pl);
    }
}

/* Called from main context */
static int agc_process_msg_cb(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk) {
    struct pa_agc_msg *msg;
    struct userdata *u;

    pa_assert(o);

    msg = PA_AGC_MSG(o);

    /* When the module is unloaded, there may still remain queued messages for
     * the agc. Messages are sent to the main thread using the master
     * source's asyncmsgq, and that message queue isn't (and can't be, at least
     * with the current asyncmsgq API) cleared from the agc messages when
     * module-agc is unloaded.
     *
     * The userdata may already have been freed at this point, but the
     * asyncmsgq holds a reference to the pa_agc_msg object, which
     * contains a flag to indicate that all remaining messages have to be
     * ignored. */
    if (msg->dead)
        return 0;

    u = msg->userdata;

    switch (code) {
        case AGC_MESSAGE_SET_VOLUME: {
            pa_volume_t v = PA_PTR_TO_UINT(userdata);
            pa_cvolume vol;

            if (u->use_volume_sharing) {
                pa_cvolume_set(&vol, u->source->sample_spec.channels, v);
                pa_source_set_volume(u->source, &vol, true, false);
            } else {
                pa_cvolume_set(&vol, u->source_output->sample_spec.channels, v);
                pa_source_output_set_volume(u->source_output, &vol, false, true);
            }

            break;
        }

        default:
            pa_assert_not_reached();
            break;
    }

    return 0;
}

/* Called by the agc, so source I/O thread context. */
pa_volume_t pa_agc_get_capture_volume(pa_agc_struct *ec) {
    return pa_cvolume_avg(&ec->msg->userdata->thread_info.current_volume);

}

/* Called by the agc, so source I/O thread context. */
void pa_agc_set_capture_volume(pa_agc_struct *ec, pa_volume_t v) {
    if (pa_cvolume_avg(&ec->msg->userdata->thread_info.current_volume) != v) {
        pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(ec->msg), AGC_MESSAGE_SET_VOLUME, PA_UINT_TO_PTR(v),
                0, NULL, NULL);
    }
}

uint32_t pa_agc_blocksize_power2(unsigned rate, unsigned ms) {
    unsigned nframes = (rate * ms) / 1000;
    uint32_t y = 1 << ((8 * sizeof(uint32_t)) - 2);

    pa_assert(rate >= 4000);
    pa_assert(ms >= 1);

    /* nframes should be a power of 2, round down to nearest power of two */
    while (y > nframes)
        y >>= 1;

    pa_assert(y >= 1);
    return y;
}


/* Common initialisation bits for agc
 *
 * Called from main context. */
static int init_common(pa_modargs *ma, struct userdata *u, pa_sample_spec *source_ss, pa_channel_map *source_map) {
    const char *ec_string;

    if (pa_modargs_get_sample_spec_and_channel_map(ma, source_ss, source_map, PA_CHANNEL_MAP_DEFAULT) < 0) {
        pa_log("Invalid sample format specification or channel map");
        goto fail;
    }

    u->ec = pa_xnew0(pa_agc_struct, 1);
    if (!u->ec) {
        pa_log("Failed to alloc agc");
        goto fail;
    }


    pa_log_info("Using AGC engine: %s", "webrtc");

    u->ec->init = ec_table[0].init;
    u->ec->play = ec_table[0].play;
    u->ec->record = ec_table[0].record;
    u->ec->set_drift = ec_table[0].set_drift;
    u->ec->run = ec_table[0].run;
    u->ec->done = ec_table[0].done;

    return 0;

fail:
    return -1;
}

/* Called from main context. */
int pa__init(pa_module*m) {
    struct userdata *u;
    pa_sample_spec source_output_ss, source_ss;
    pa_channel_map source_output_map, source_map;
    pa_modargs *ma;
    pa_source *source_master=NULL;
    bool autoloaded;
    pa_source_output_new_data source_output_data;
    pa_source_new_data source_data;
    pa_memchunk silence;
    uint32_t temp;
    uint32_t nframes = 0;
    bool use_master_format;
    pa_usec_t blocksize_usec;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    if (!(source_master = pa_namereg_get(m->core, pa_modargs_get_value(ma, "source_master", NULL), PA_NAMEREG_SOURCE))) {
        pa_log("Master source not found");
        goto fail;
    }
    pa_assert(source_master);

    /* Set to true if we just want to inherit sample spec and channel map from the source master */
    use_master_format = DEFAULT_USE_MASTER_FORMAT;
    if (pa_modargs_get_value_boolean(ma, "use_master_format", &use_master_format) < 0) {
        pa_log("use_master_format= expects a boolean argument");
        goto fail;
    }

    source_ss = source_master->sample_spec;

    if (use_master_format) {
        source_map = source_master->channel_map;
    } else {
        source_ss = source_master->sample_spec;
        source_ss.rate = DEFAULT_RATE;
        source_ss.channels = DEFAULT_CHANNELS;
        pa_channel_map_init_auto(&source_map, source_ss.channels, PA_CHANNEL_MAP_DEFAULT);
    }

    u = pa_xnew0(struct userdata, 1);
    if (!u) {
        pa_log("Failed to alloc userdata");
        goto fail;
    }
    u->core = m->core;
    u->module = m;
    m->userdata = u;
    u->dead = false;

    u->use_volume_sharing = true;
    if (pa_modargs_get_value_boolean(ma, "use_volume_sharing", &u->use_volume_sharing) < 0) {
        pa_log("use_volume_sharing= expects a boolean argument");
        goto fail;
    }

    temp = DEFAULT_ADJUST_TIME_USEC / PA_USEC_PER_SEC;
    if (pa_modargs_get_value_u32(ma, "adjust_time", &temp) < 0) {
        pa_log("Failed to parse adjust_time value");
        goto fail;
    }

    if (temp != DEFAULT_ADJUST_TIME_USEC / PA_USEC_PER_SEC)
        u->adjust_time = temp * PA_USEC_PER_SEC;
    else
        u->adjust_time = DEFAULT_ADJUST_TIME_USEC;

    temp = DEFAULT_ADJUST_TOLERANCE / PA_USEC_PER_MSEC;
    if (pa_modargs_get_value_u32(ma, "adjust_threshold", &temp) < 0) {
        pa_log("Failed to parse adjust_threshold value");
        goto fail;
    }

    if (temp != DEFAULT_ADJUST_TOLERANCE / PA_USEC_PER_MSEC)
        u->adjust_threshold = temp * PA_USEC_PER_MSEC;
    else
        u->adjust_threshold = DEFAULT_ADJUST_TOLERANCE;

    u->save_aec = DEFAULT_SAVE_AEC;
    if (pa_modargs_get_value_boolean(ma, "save_aec", &u->save_aec) < 0) {
        pa_log("Failed to parse save_aec value");
        goto fail;
    }

    autoloaded = DEFAULT_AUTOLOADED;
    if (pa_modargs_get_value_boolean(ma, "autoloaded", &autoloaded) < 0) {
        pa_log("Failed to parse autoloaded value");
        goto fail;
    }

    if (init_common(ma, u, &source_ss, &source_map) < 0)
        goto fail;

    u->asyncmsgq = pa_asyncmsgq_new(0);
    if (!u->asyncmsgq) {
        pa_log("pa_asyncmsgq_new() failed.");
        goto fail;
    }

    u->need_realign = true;

    source_output_ss = source_ss;
    source_output_map = source_map;

    pa_assert(u->ec->init);
    if (!u->ec->init(u->core, u->ec, &source_output_ss, &source_output_map, NULL, NULL, &source_ss, &source_map, &nframes, pa_modargs_get_value(ma, "aec_args", NULL))) {
        pa_log("Failed to init AGC engine");
        goto fail;
    }
    pa_assert(source_output_ss.rate == source_ss.rate);

    u->source_output_blocksize = nframes * pa_frame_size(&source_output_ss);
    u->source_blocksize = nframes * pa_frame_size(&source_ss);

    if (u->ec->params.drift_compensation)
        pa_assert(u->ec->set_drift);

    /* Create source */
    pa_source_new_data_init(&source_data);
    source_data.driver = __FILE__;
    source_data.module = m;
    if (!(source_data.name = pa_xstrdup(pa_modargs_get_value(ma, "source_name", NULL))))
        source_data.name = pa_sprintf_malloc("%s.agc", source_master->name);
    pa_source_new_data_set_sample_spec(&source_data, &source_ss);
    pa_source_new_data_set_channel_map(&source_data, &source_map);
    pa_proplist_sets(source_data.proplist, PA_PROP_DEVICE_MASTER_DEVICE, source_master->name);
    pa_proplist_sets(source_data.proplist, PA_PROP_DEVICE_CLASS, "filter");
    if (!autoloaded)
        pa_proplist_sets(source_data.proplist, PA_PROP_DEVICE_INTENDED_ROLES, "phone");

    if (pa_modargs_get_proplist(ma, "source_properties", source_data.proplist, PA_UPDATE_REPLACE) < 0) {
        pa_log("Invalid properties");
        pa_source_new_data_done(&source_data);
        goto fail;
    }

    if ((u->source_auto_desc = !pa_proplist_contains(source_data.proplist, PA_PROP_DEVICE_DESCRIPTION))) {
        const char *y, *z;

        z = pa_proplist_gets(source_master->proplist, PA_PROP_DEVICE_DESCRIPTION);
        pa_proplist_setf(source_data.proplist, PA_PROP_DEVICE_DESCRIPTION, "%s (AGC)",
                z ? z : source_master->name);
    }

    u->source = pa_source_new(m->core, &source_data, (source_master->flags & (PA_SOURCE_LATENCY | PA_SOURCE_DYNAMIC_LATENCY))
                                                     | (u->use_volume_sharing ? PA_SOURCE_SHARE_VOLUME_WITH_MASTER : 0));

    pa_source_new_data_done(&source_data);

    if (!u->source) {
        pa_log("Failed to create source.");
        goto fail;
    }

    u->source->parent.process_msg = source_process_msg_cb;
    u->source->set_state_in_main_thread = source_set_state_in_main_thread_cb;
    u->source->update_requested_latency = source_update_requested_latency_cb;
    pa_source_set_set_mute_callback(u->source, source_set_mute_cb);
    if (!u->use_volume_sharing) {
        pa_source_set_get_volume_callback(u->source, source_get_volume_cb);
        pa_source_set_set_volume_callback(u->source, source_set_volume_cb);
        pa_source_enable_decibel_volume(u->source, true);
    }
    u->source->userdata = u;

    pa_source_set_asyncmsgq(u->source, source_master->asyncmsgq);

    /* Create source output */
    pa_source_output_new_data_init(&source_output_data);
    source_output_data.driver = __FILE__;
    source_output_data.module = m;
    pa_source_output_new_data_set_source(&source_output_data, source_master, false, true);
    source_output_data.destination_source = u->source;

    pa_proplist_sets(source_output_data.proplist, PA_PROP_MEDIA_NAME, "AGC Source Stream");
    pa_proplist_sets(source_output_data.proplist, PA_PROP_MEDIA_ROLE, "filter");
    pa_source_output_new_data_set_sample_spec(&source_output_data, &source_output_ss);
    pa_source_output_new_data_set_channel_map(&source_output_data, &source_output_map);
    source_output_data.flags |= PA_SOURCE_OUTPUT_START_CORKED;

    if (autoloaded)
        source_output_data.flags |= PA_SOURCE_OUTPUT_DONT_MOVE;

    pa_source_output_new(&u->source_output, m->core, &source_output_data);
    pa_source_output_new_data_done(&source_output_data);

    if (!u->source_output)
        goto fail;

    u->source_output->parent.process_msg = source_output_process_msg_cb;
    u->source_output->push = source_output_push_cb;
    u->source_output->process_rewind = source_output_process_rewind_cb;
    u->source_output->update_max_rewind = source_output_update_max_rewind_cb;
    u->source_output->update_source_requested_latency = source_output_update_source_requested_latency_cb;
    u->source_output->update_source_latency_range = source_output_update_source_latency_range_cb;
    u->source_output->update_source_fixed_latency = source_output_update_source_fixed_latency_cb;
    u->source_output->kill = source_output_kill_cb;
    u->source_output->attach = source_output_attach_cb;
    u->source_output->detach = source_output_detach_cb;
    u->source_output->state_change = source_output_state_change_cb;
    u->source_output->may_move_to = source_output_may_move_to_cb;
    u->source_output->moving = source_output_moving_cb;
    u->source_output->userdata = u;

    u->source->output_from_master = u->source_output;

    //pa_sink_input_get_silence(u->sink_input, &silence);
    pa_silence_memchunk_get(
                &u->source_output->core->silence_cache,
                u->source_output->core->mempool,
                &silence,
                &u->source_output->sample_spec,
                u->source_output->thread_info.resampler ? pa_resampler_max_block_size(u->source_output->thread_info.resampler) : 0);

    pa_log("Sita inside pa__init before source memblockq call");
    u->source_memblockq = pa_memblockq_new("module-agc source_memblockq", 0, MEMBLOCKQ_MAXLENGTH, 0,
        &source_output_ss, 1, 1, 0, &silence);
    pa_log("Sita inside pa__init after source memblockq call");

    pa_memblock_unref(silence.memblock);

    if (!u->source_memblockq) {
        pa_log("Failed to create memblockq.");
        goto fail;
    }

    if (u->adjust_time > 0 && !u->ec->params.drift_compensation)
        u->time_event = pa_core_rttime_new(m->core, pa_rtclock_now() + u->adjust_time, time_callback, u);
    else if (u->ec->params.drift_compensation) {
        pa_log_info("agc does drift compensation -- built-in compensation will be disabled");
        u->adjust_time = 0;
        /* Perform resync just once to give the agc a leg up */
        pa_atomic_store(&u->request_resync, 1);
    }

    if (u->save_aec) {
        pa_log("Creating AEC files in /tmp");
        u->captured_file = fopen("/tmp/aec_rec.sw", "wb");
        if (u->captured_file == NULL)
            perror ("fopen failed");
        u->played_file = fopen("/tmp/aec_play.sw", "wb");
        if (u->played_file == NULL)
            perror ("fopen failed");
        u->canceled_file = fopen("/tmp/aec_out.sw", "wb");
        if (u->canceled_file == NULL)
            perror ("fopen failed");
        if (u->ec->params.drift_compensation) {
            u->drift_file = fopen("/tmp/aec_drift.txt", "w");
            if (u->drift_file == NULL)
                perror ("fopen failed");
        }
    }

    u->ec->msg = pa_msgobject_new(pa_agc_msg);
    u->ec->msg->parent.process_msg = agc_process_msg_cb;
    u->ec->msg->userdata = u;

    u->thread_info.current_volume = u->source->reference_volume;

    /* We don't want to deal with too many chunks at a time */
    blocksize_usec = pa_bytes_to_usec(u->source_blocksize, &u->source->sample_spec);
    if (u->source->flags & PA_SOURCE_DYNAMIC_LATENCY)
        pa_source_set_latency_range(u->source, blocksize_usec, blocksize_usec * MAX_LATENCY_BLOCKS);
    pa_source_output_set_requested_latency(u->source_output, blocksize_usec * MAX_LATENCY_BLOCKS);

    pa_source_output_put(u->source_output);

    pa_source_put(u->source);

    pa_source_output_cork(u->source_output, false);

    pa_modargs_free(ma);

    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);

    pa__done(m);

    return -1;
}

/* Called from main context. */
int pa__get_n_used(pa_module *m) {
    struct userdata *u;

    pa_assert(m);
    pa_assert_se(u = m->userdata);

    return pa_source_linked_by(u->source);
}

/* Called from main context. */
void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    u->dead = true;

    /* See comments in source_output_kill_cb() above regarding
     * destruction order! */

    if (u->time_event)
        u->core->mainloop->time_free(u->time_event);

    if (u->source_output)
        pa_source_output_cork(u->source_output, true);

    if (u->source)
        pa_source_unlink(u->source);

    if (u->source_output) {
        pa_source_output_unlink(u->source_output);
        pa_source_output_unref(u->source_output);
    }

    if (u->source)
        pa_source_unref(u->source);

    if (u->source_memblockq)
        pa_memblockq_free(u->source_memblockq);

    if (u->ec) {
        if (u->ec->done)
            u->ec->done(u->ec);

        if (u->ec->msg) {
            u->ec->msg->dead = true;
            pa_agc_msg_unref(u->ec->msg);
        }

        pa_xfree(u->ec);
    }

    if (u->asyncmsgq)
        pa_asyncmsgq_unref(u->asyncmsgq);

    if (u->save_aec) {
        if (u->played_file)
            fclose(u->played_file);
        if (u->captured_file)
            fclose(u->captured_file);
        if (u->canceled_file)
            fclose(u->canceled_file);
        if (u->drift_file)
            fclose(u->drift_file);
    }

    pa_xfree(u);
}