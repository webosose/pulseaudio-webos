/***
  This file is part of PulseAudio.
  Copyright (c) 2023 LG Electronics, Inc.
  All rights reserved.

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/gccmacro.h>
#include <pulse/xmalloc.h>
#include <pulse/timeval.h>

#include <pulsecore/i18n.h>
#include <pulsecore/namereg.h>
#include <pulsecore/sink.h>
#include <pulsecore/module.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/sample-util.h>
#include <pulsecore/ltdl-helper.h>

#include "snd.h"
#include "mem.h"
#include "compressor.h"

PA_MODULE_AUTHOR("LG Electronics");
PA_MODULE_DESCRIPTION(_("Apply DRC on audio to master sink"));
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(false);
PA_MODULE_USAGE(
        _("sink_name=<name for the sink> "
          "sink_properties=<properties for the sink> "
          "master=<name of sink to apply drc> "
          "rate=<sample rate> "
          "channels=<number of channels> "
          "channel_map=<channel map> "
          "use_volume_sharing=<yes or no> "
          "force_flat_volume=<yes or no> "
        ));

#define MEMBLOCKQ_MAXLENGTH (16*1024*1024)

struct userdata {
    pa_module *module;

    /* FIXME: Uncomment this and take "autoloaded" as a modarg if this is a filter */
    /* bool autoloaded; */

    pa_sink *sink;
    pa_sink_input *sink_input;

    pa_memblockq *memblockq;

    bool auto_desc;
    unsigned channels;

    sf_compressor_state_st state;
    int extra_data_len;
    float extra_data[500];
    uint32_t rate;
    uint32_t block_size;
    FILE *config_file;
};

static const char* const valid_modargs[] = {
    "sink_name",
    "sink_properties",
    "master",
    "rate",
    "channels",
    "channel_map",
    "use_volume_sharing",
    "force_flat_volume",
    NULL
};

// initialize sf_malloc/sf_free with the standard malloc/free
sf_malloc_func sf_malloc = malloc;
sf_free_func sf_free = free;

sf_snd sf_snd_new(int size, int rate, bool clear){
    sf_snd snd = sf_malloc(sizeof(sf_snd_st));
    if (snd == NULL)
       return NULL;
    snd->size = size;
    snd->rate = rate;
    snd->samples = sf_malloc(sizeof(sf_sample_st) * size);
    if (snd->samples == NULL){
       sf_free(snd);
       return NULL;
    }
    if (clear && size > 0)
       memset(snd->samples, 0, sizeof(sf_sample_st) * size);
    return snd;
}

void sf_snd_free(sf_snd snd){
	sf_free(snd->samples);
	sf_free(snd);
}
/* Called from I/O thread context */
static int sink_process_msg_cb(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SINK(o)->userdata;

    switch (code) {

        case PA_SINK_MESSAGE_GET_LATENCY:

            /* The sink is _put() before the sink input is, so let's
             * make sure we don't access it in that time. Also, the
             * sink input is first shut down, the sink second. */
            if (!PA_SINK_IS_LINKED(u->sink->thread_info.state) ||
                !PA_SINK_INPUT_IS_LINKED(u->sink_input->thread_info.state)) {
                *((int64_t*) data) = 0;
                return 0;
            }

            *((int64_t*) data) =

                /* Get the latency of the master sink */
                pa_sink_get_latency_within_thread(u->sink_input->sink, true) +

                /* Add the latency internal to our sink input on top */
                pa_bytes_to_usec(pa_memblockq_get_length(u->sink_input->thread_info.render_memblockq), &u->sink_input->sink->sample_spec);

            return 0;
    }

    return pa_sink_process_msg(o, code, data, offset, chunk);
}


/* Called from main context */
static int sink_set_state_in_main_thread_cb(pa_sink *s, pa_sink_state_t state, pa_suspend_cause_t suspend_cause) {
    struct userdata *u;

    pa_sink_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SINK_IS_LINKED(state) ||
        !PA_SINK_INPUT_IS_LINKED(u->sink_input->state))
        return 0;

    pa_sink_input_cork(u->sink_input, state == PA_SINK_SUSPENDED);
    return 0;
}

/* Called from the IO thread. */
static int sink_set_state_in_io_thread_cb(pa_sink *s, pa_sink_state_t new_state, pa_suspend_cause_t new_suspend_cause) {
    struct userdata *u;

    pa_assert(s);
    pa_assert_se(u = s->userdata);

    /* When set to running or idle for the first time, request a rewind
     * of the master sink to make sure we are heard immediately */
    if (PA_SINK_IS_OPENED(new_state) && s->thread_info.state == PA_SINK_INIT) {
        pa_log_debug("Requesting rewind due to state change.");
        pa_sink_input_request_rewind(u->sink_input, 0, false, true, true);
    }

    return 0;
}

/* Called from I/O thread context */
static void sink_request_rewind_cb(pa_sink *s) {
    struct userdata *u;

    pa_sink_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SINK_IS_LINKED(u->sink->thread_info.state) ||
        !PA_SINK_INPUT_IS_LINKED(u->sink_input->thread_info.state))
        return;

    /* Just hand this one over to the master sink */
    pa_sink_input_request_rewind(u->sink_input,
                                 s->thread_info.rewind_nbytes +
                                 pa_memblockq_get_length(u->memblockq), true, false, false);
}

/* Called from I/O thread context */
static void sink_update_requested_latency_cb(pa_sink *s) {
    struct userdata *u;

    pa_sink_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SINK_IS_LINKED(u->sink->thread_info.state) ||
        !PA_SINK_INPUT_IS_LINKED(u->sink_input->thread_info.state))
        return;

    /* Just hand this one over to the master sink */
    pa_sink_input_set_requested_latency_within_thread(
            u->sink_input,
            pa_sink_get_requested_latency_within_thread(s));
}

/* Called from main context */
static void sink_set_volume_cb(pa_sink *s) {
    struct userdata *u;

    pa_sink_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SINK_IS_LINKED(s->state) ||
        !PA_SINK_INPUT_IS_LINKED(u->sink_input->state))
        return;

    pa_sink_input_set_volume(u->sink_input, &s->real_volume, s->save_volume, true);
}

/* Called from main context */
static void sink_set_mute_cb(pa_sink *s) {
    struct userdata *u;

    pa_sink_assert_ref(s);
    pa_assert_se(u = s->userdata);

    if (!PA_SINK_IS_LINKED(s->state) ||
        !PA_SINK_INPUT_IS_LINKED(u->sink_input->state))
        return;

    pa_sink_input_set_mute(u->sink_input, s->muted, s->save_muted);
}

/* Called from I/O thread context */
static int sink_input_pop_cb(pa_sink_input *i, size_t nbytes, pa_memchunk *chunk) {
    struct userdata *u;
    float *src, *dst;
    size_t fs;
    unsigned n, c;
    pa_memchunk tchunk;
    pa_usec_t current_latency PA_GCC_UNUSED;
    int ii,jj;

    pa_sink_input_assert_ref(i);
    pa_assert(chunk);
    pa_assert_se(u = i->userdata);

    if (!PA_SINK_IS_LINKED(u->sink->thread_info.state))
        return -1;

    /* Hmm, process any rewind request that might be queued up */
    pa_sink_process_rewind(u->sink, 0);

    /* (1) IF YOU NEED A FIXED BLOCK SIZE USE
     * pa_memblockq_peek_fixed_size() HERE INSTEAD. NOTE THAT FILTERS
     * WHICH CAN DEAL WITH DYNAMIC BLOCK SIZES ARE HIGHLY
     * PREFERRED. */
    while (pa_memblockq_peek(u->memblockq, &tchunk) < 0) {
        pa_memchunk nchunk;

        pa_sink_render(u->sink, nbytes, &nchunk);
        pa_memblockq_push(u->memblockq, &nchunk);
        pa_memblock_unref(nchunk.memblock);
    }

    /* (2) IF YOU NEED A FIXED BLOCK SIZE, THIS NEXT LINE IS NOT
     * NECESSARY */
    tchunk.length = PA_MIN(nbytes, tchunk.length);
    pa_assert(tchunk.length > 0);

    fs = pa_frame_size(&i->sample_spec);
    n = (unsigned) (tchunk.length / fs);

    pa_assert(n > 0);

    /***
     * snd filter processes in multiples of 32 samples (bytes/(channels*sizeof(float)))
     * if the input size is not multiple of 32 samples, data will be dropped.
     * to avoid, buffering is added here
    ***/

    int newSamples = tchunk.length /(u->channels * sizeof(float));
    int toalSamples = newSamples + u->extra_data_len;
    int samplesWeCanProcess = floor(toalSamples/32)*32;
    int leftSamples = toalSamples - samplesWeCanProcess;

    float *tot_buf = (float*)malloc(toalSamples * (u->channels * sizeof(float)));

    if(!samplesWeCanProcess)
    {
        // we don't have enough samples to process sndfilter compressor

        chunk->length=0;
        src = pa_memblock_acquire_chunk(&tchunk);
        for(int jj=0,ii=u->extra_data_len;jj<newSamples;jj++)
        {
            u->extra_data[2*ii] = src[2*jj];
            u->extra_data[2*ii+1] = src[2*jj+1];
        }
        u->extra_data_len = u->extra_data_len + newSamples;
        pa_memblock_release(tchunk.memblock);
        pa_memblockq_drop(u->memblockq, tchunk.length);
        return -1;
    }

    chunk->index = 0;
    chunk->length = samplesWeCanProcess * (sizeof(float)*u->channels);
    chunk->memblock = pa_memblock_new(i->sink->core->mempool, chunk->length);

    pa_memblockq_drop(u->memblockq, tchunk.length);

    src = pa_memblock_acquire_chunk(&tchunk);
    dst = pa_memblock_acquire(chunk->memblock);

    /* (3) PUT YOUR CODE HERE TO DO SOMETHING WITH THE DATA */
    for(ii=0;ii<u->extra_data_len;ii++)
    {
        tot_buf[ii*2] = u->extra_data[ii*2];
        tot_buf[ii*2+1]=u->extra_data[ii*2+1];
    }

    for(int ii=0, jj =u->extra_data_len; ii < newSamples; ii++,jj++)
    {
        tot_buf[jj*2] = src[ii*2];
        tot_buf[jj*2+1]=src[ii*2+1];
    }

    sf_snd input_snd = sf_snd_new(samplesWeCanProcess, u->rate, true);
    sf_snd output_snd = sf_snd_new(input_snd->size, u->rate, true);

    for (ii=0; ii<samplesWeCanProcess; ii++)
    {
        input_snd->samples[ii].L = tot_buf[2*ii];
        input_snd->samples[ii].R = tot_buf[2*ii+1];
    }

    sf_compressor_process(&(u->state), input_snd->size, input_snd->samples, output_snd->samples);

    for(ii =0;ii<samplesWeCanProcess;ii++)
    {
        dst[ii*2] = output_snd->samples[ii].L;
        dst[ii*2+1]=output_snd->samples[ii].R;
    }
    sf_snd_free(input_snd);
    sf_snd_free(output_snd);

    for(int ii=0,jj = samplesWeCanProcess; jj < toalSamples; ii++,jj++)
    {
        u->extra_data[ii*2] = tot_buf[jj*2];
        u->extra_data[ii*2+1] = tot_buf[jj*2+1];
    }
    u->extra_data_len = toalSamples-samplesWeCanProcess;

    pa_memblock_release(tchunk.memblock);
    pa_memblock_release(chunk->memblock);

    pa_memblock_unref(tchunk.memblock);
    free(tot_buf);

    /* (4) IF YOU NEED THE LATENCY FOR SOMETHING ACQUIRE IT LIKE THIS: */
    current_latency =
        /* Get the latency of the master sink */
        pa_sink_get_latency_within_thread(i->sink, false) +

        /* Add the latency internal to our sink input on top */
        pa_bytes_to_usec(pa_memblockq_get_length(i->thread_info.render_memblockq), &i->sink->sample_spec);

    return 0;
}
/* Called from I/O thread context */
static void sink_input_process_rewind_cb(pa_sink_input *i, size_t nbytes) {
    struct userdata *u;
    size_t amount = 0;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    /* If the sink is not yet linked, there is nothing to rewind */
    if (!PA_SINK_IS_LINKED(u->sink->thread_info.state))
        return;

    if (u->sink->thread_info.rewind_nbytes > 0) {
        size_t max_rewrite;

        max_rewrite = nbytes + pa_memblockq_get_length(u->memblockq);
        amount = PA_MIN(u->sink->thread_info.rewind_nbytes, max_rewrite);
        u->sink->thread_info.rewind_nbytes = 0;

        if (amount > 0) {
            pa_memblockq_seek(u->memblockq, - (int64_t) amount, PA_SEEK_RELATIVE, true);

            /* (5) PUT YOUR CODE HERE TO RESET YOUR FILTER  */
        }
    }

    pa_sink_process_rewind(u->sink, amount);
    pa_memblockq_rewind(u->memblockq, nbytes);
}

/* Called from I/O thread context */
static void sink_input_update_max_rewind_cb(pa_sink_input *i, size_t nbytes) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    /* FIXME: Too small max_rewind:
     * https://bugs.freedesktop.org/show_bug.cgi?id=53709 */
    pa_memblockq_set_maxrewind(u->memblockq, nbytes);
    pa_sink_set_max_rewind_within_thread(u->sink, nbytes);
}

/* Called from I/O thread context */
static void sink_input_update_max_request_cb(pa_sink_input *i, size_t nbytes) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    /* (6) IF YOU NEED A FIXED BLOCK SIZE ROUND nbytes UP TO MULTIPLES
     * OF IT HERE. THE PA_ROUND_UP MACRO IS USEFUL FOR THAT. */

    pa_sink_set_max_request_within_thread(u->sink, nbytes);
}

/* Called from I/O thread context */
static void sink_input_update_sink_latency_range_cb(pa_sink_input *i) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    pa_sink_set_latency_range_within_thread(u->sink, i->sink->thread_info.min_latency, i->sink->thread_info.max_latency);
}

/* Called from I/O thread context */
static void sink_input_update_sink_fixed_latency_cb(pa_sink_input *i) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    /* (7) IF YOU NEED A FIXED BLOCK SIZE ADD THE LATENCY FOR ONE
     * BLOCK MINUS ONE SAMPLE HERE. pa_usec_to_bytes_round_up() IS
     * USEFUL FOR THAT. */

    pa_sink_set_fixed_latency_within_thread(u->sink, i->sink->thread_info.fixed_latency);
}

/* Called from I/O thread context */
static void sink_input_detach_cb(pa_sink_input *i) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    if (PA_SINK_IS_LINKED(u->sink->thread_info.state))
        pa_sink_detach_within_thread(u->sink);

    pa_sink_set_rtpoll(u->sink, NULL);
}

/* Called from I/O thread context */
static void sink_input_attach_cb(pa_sink_input *i) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    pa_sink_set_rtpoll(u->sink, i->sink->thread_info.rtpoll);
    pa_sink_set_latency_range_within_thread(u->sink, i->sink->thread_info.min_latency, i->sink->thread_info.max_latency);

    /* (8.1) IF YOU NEED A FIXED BLOCK SIZE ADD THE LATENCY FOR ONE
     * BLOCK MINUS ONE SAMPLE HERE. SEE (7) */
    pa_sink_set_fixed_latency_within_thread(u->sink, i->sink->thread_info.fixed_latency);

    /* (8.2) IF YOU NEED A FIXED BLOCK SIZE ROUND
     * pa_sink_input_get_max_request(i) UP TO MULTIPLES OF IT
     * HERE. SEE (6) */
    pa_sink_set_max_request_within_thread(u->sink, pa_sink_input_get_max_request(i));

    /* FIXME: Too small max_rewind:
     * https://bugs.freedesktop.org/show_bug.cgi?id=53709 */
    pa_sink_set_max_rewind_within_thread(u->sink, pa_sink_input_get_max_rewind(i));

    if (PA_SINK_IS_LINKED(u->sink->thread_info.state))
        pa_sink_attach_within_thread(u->sink);
}

/* Called from main context */
static void sink_input_kill_cb(pa_sink_input *i) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    /* The order here matters! We first kill the sink so that streams
     * can properly be moved away while the sink input is still connected
     * to the master. */
    pa_sink_input_cork(u->sink_input, true);
    pa_sink_unlink(u->sink);
    pa_sink_input_unlink(u->sink_input);

    pa_sink_input_unref(u->sink_input);
    u->sink_input = NULL;

    pa_sink_unref(u->sink);
    u->sink = NULL;

    pa_module_unload_request(u->module, true);
}

/* Called from main context */
static void sink_input_moving_cb(pa_sink_input *i, pa_sink *dest) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    if (dest) {
        pa_sink_set_asyncmsgq(u->sink, dest->asyncmsgq);
        pa_sink_update_flags(u->sink, PA_SINK_LATENCY|PA_SINK_DYNAMIC_LATENCY, dest->flags);
    } else
        pa_sink_set_asyncmsgq(u->sink, NULL);

    if (u->auto_desc && dest) {
        const char *z;
        pa_proplist *pl;

        pl = pa_proplist_new();
        z = pa_proplist_gets(dest->proplist, PA_PROP_DEVICE_DESCRIPTION);
        pa_proplist_setf(pl, PA_PROP_DEVICE_DESCRIPTION, "Virtual Sink %s on %s",
                         pa_proplist_gets(u->sink->proplist, "device.vsink.name"), z ? z : dest->name);

        pa_sink_update_proplist(u->sink, PA_UPDATE_REPLACE, pl);
        pa_proplist_free(pl);
    }
}

/* Called from main context */
static void sink_input_volume_changed_cb(pa_sink_input *i) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    pa_sink_volume_changed(u->sink, &i->volume);
}

/* Called from main context */
static void sink_input_mute_changed_cb(pa_sink_input *i) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    pa_sink_mute_changed(u->sink, i->muted);
}

static void sink_input_state_change_cb(pa_sink_input *i, pa_sink_input_state_t state) {
    struct userdata *u;

    pa_sink_input_assert_ref(i);
    pa_assert_se(u = i->userdata);

    if (state == PA_SINK_INPUT_CORKED)
    {
        u->extra_data_len = 0;
        memset(u->extra_data,0,sizeof(u->extra_data));
    }
}

int readParametersFromFile(const char* filename, float* pregain, float* threshold, float* knee, float* ratio, float* attack, float* release, float* predelay, float* releasezone1, float* releasezone2, float* releasezone3, float* releasezone4, float* postgain, float* wet) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("Failed to open the file.\n");
        return -1;
    }

    // Read the parameters from the file
    char line[256];
    char param[256];
    float value;

    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "%s = %f;", param, &value) == 2) {
            if (strcmp(param, "pregain") == 0) {
                *pregain = value;
                pa_log_info("pregain,%f",*pregain);
            } else if (strcmp(param, "threshold") == 0) {
                *threshold = value;
                pa_log_info("threshold,%f",*threshold);
            } else if (strcmp(param, "knee") == 0) {
                *knee = value;
                pa_log_info("knee,%f",*knee);
            } else if (strcmp(param, "ratio") == 0) {
                *ratio = value;
                pa_log_info("ratio,%f",*ratio);
            } else if (strcmp(param, "attack") == 0) {
                *attack = value;
                pa_log_info("attack,%f",*attack);
            } else if (strcmp(param, "release") == 0) {
                *release = value;
                pa_log_info("release,%f",*release);
            } else if (strcmp(param, "predelay") == 0) {
                *predelay = value;
                pa_log_info("predelay,%f",*predelay);
            } else if (strcmp(param, "releasezone1") == 0) {
                *releasezone1 = value;
                pa_log_info("releasezone1,%f",*releasezone1);
            } else if (strcmp(param, "releasezone2") == 0) {
                *releasezone2 = value;
                pa_log_info("releasezone2,%f",*releasezone2);
            } else if (strcmp(param, "releasezone3") == 0) {
                *releasezone3 = value;
                pa_log_info("releasezone3,%f",*releasezone3);
            } else if (strcmp(param, "releasezone4") == 0) {
                *releasezone4 = value;
                pa_log_info("releasezone4,%f",*releasezone4);
            } else if (strcmp(param, "postgain") == 0) {
                *postgain = value;
                pa_log_info("postgain,%f",*postgain);
            } else if (strcmp(param, "wet") == 0) {
                *wet = value;
                pa_log_info("wet,%f",*wet);
            }
        }
    }

    fclose(file);
    return 0;
}

int pa__init(pa_module*m) {
    struct userdata *u;
    pa_sample_spec ss;
    pa_channel_map map;
    pa_modargs *ma;
    pa_sink *master=NULL;
    pa_sink_input_new_data sink_input_data;
    pa_sink_new_data sink_data;
    bool use_volume_sharing = true;
    bool force_flat_volume = false;
    pa_memchunk silence;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    if (!(master = pa_namereg_get(m->core, pa_modargs_get_value(ma, "master", NULL), PA_NAMEREG_SINK))) {
        pa_log("Master sink not found");
        goto fail;
    }

    pa_assert(master);

    ss = master->sample_spec;
    ss.format = PA_SAMPLE_FLOAT32;
    map = master->channel_map;
    if (pa_modargs_get_sample_spec_and_channel_map(ma, &ss, &map, PA_CHANNEL_MAP_DEFAULT) < 0) {
        pa_log("Invalid sample format specification or channel map");
        goto fail;
    }

    if (pa_modargs_get_value_boolean(ma, "use_volume_sharing", &use_volume_sharing) < 0) {
        pa_log("use_volume_sharing= expects a boolean argument");
        goto fail;
    }

    if (pa_modargs_get_value_boolean(ma, "force_flat_volume", &force_flat_volume) < 0) {
        pa_log("force_flat_volume= expects a boolean argument");
        goto fail;
    }

    if (use_volume_sharing && force_flat_volume) {
        pa_log("Flat volume can't be forced when using volume sharing.");
        goto fail;
    }

    u = pa_xnew0(struct userdata, 1);
    u->module = m;
    m->userdata = u;
    u->channels = ss.channels;
    u->extra_data_len = 0;
    u->block_size = pa_usec_to_bytes(PA_USEC_PER_SEC/20, &ss);
    u->block_size = floor(u->block_size/(32*pa_frame_size(&ss)))*(32*pa_frame_size(&ss));

    /* Create sink */
    pa_sink_new_data_init(&sink_data);
    sink_data.driver = __FILE__;
    sink_data.module = m;
    if (!(sink_data.name = pa_xstrdup(pa_modargs_get_value(ma, "sink_name", NULL))))
        sink_data.name = pa_sprintf_malloc("%s.vsink", master->name);
    pa_sink_new_data_set_sample_spec(&sink_data, &ss);
    pa_sink_new_data_set_channel_map(&sink_data, &map);
    pa_proplist_sets(sink_data.proplist, PA_PROP_DEVICE_MASTER_DEVICE, master->name);
    pa_proplist_sets(sink_data.proplist, PA_PROP_DEVICE_CLASS, "filter");
    pa_proplist_sets(sink_data.proplist, "device.vsink.name", sink_data.name);

    if (pa_modargs_get_proplist(ma, "sink_properties", sink_data.proplist, PA_UPDATE_REPLACE) < 0) {
        pa_log("Invalid properties");
        pa_sink_new_data_done(&sink_data);
        goto fail;
    }

    if ((u->auto_desc = !pa_proplist_contains(sink_data.proplist, PA_PROP_DEVICE_DESCRIPTION))) {
        const char *z;

        z = pa_proplist_gets(master->proplist, PA_PROP_DEVICE_DESCRIPTION);
        pa_proplist_setf(sink_data.proplist, PA_PROP_DEVICE_DESCRIPTION, "DRC %s on %s", sink_data.name, z ? z : master->name);
    }

    u->sink = pa_sink_new(m->core, &sink_data, (master->flags & (PA_SINK_LATENCY|PA_SINK_DYNAMIC_LATENCY))
                                               | (use_volume_sharing ? PA_SINK_SHARE_VOLUME_WITH_MASTER : 0));
    pa_sink_new_data_done(&sink_data);

    if (!u->sink) {
        pa_log("Failed to create sink.");
        goto fail;
    }

    u->sink->parent.process_msg = sink_process_msg_cb;
    u->sink->set_state_in_main_thread = sink_set_state_in_main_thread_cb;
    u->sink->set_state_in_io_thread = sink_set_state_in_io_thread_cb;
    u->sink->update_requested_latency = sink_update_requested_latency_cb;
    u->sink->request_rewind = sink_request_rewind_cb;
    pa_sink_set_set_mute_callback(u->sink, sink_set_mute_cb);
    if (!use_volume_sharing) {
        pa_sink_set_set_volume_callback(u->sink, sink_set_volume_cb);
        pa_sink_enable_decibel_volume(u->sink, true);
    }
    /* Normally this flag would be enabled automatically be we can force it. */
    if (force_flat_volume)
        u->sink->flags |= PA_SINK_FLAT_VOLUME;
    u->sink->userdata = u;

    pa_sink_set_asyncmsgq(u->sink, master->asyncmsgq);

    /* Create sink input */
    pa_sink_input_new_data_init(&sink_input_data);
    sink_input_data.driver = __FILE__;
    sink_input_data.module = m;
    pa_sink_input_new_data_set_sink(&sink_input_data, master, false, true);
    sink_input_data.origin_sink = u->sink;
    pa_proplist_setf(sink_input_data.proplist, PA_PROP_MEDIA_NAME, "DRC  Stream from %s", pa_proplist_gets(u->sink->proplist, PA_PROP_DEVICE_DESCRIPTION));
    pa_proplist_sets(sink_input_data.proplist, PA_PROP_MEDIA_ROLE, "filter");
    pa_sink_input_new_data_set_sample_spec(&sink_input_data, &ss);
    pa_sink_input_new_data_set_channel_map(&sink_input_data, &map);
    sink_input_data.flags |= PA_SINK_INPUT_START_CORKED;

    pa_sink_input_new(&u->sink_input, m->core, &sink_input_data);
    pa_sink_input_new_data_done(&sink_input_data);

    if (!u->sink_input)
        goto fail;

    u->sink_input->pop = sink_input_pop_cb;
    u->sink_input->process_rewind = sink_input_process_rewind_cb;
    u->sink_input->update_max_rewind = sink_input_update_max_rewind_cb;
    u->sink_input->update_max_request = sink_input_update_max_request_cb;
    u->sink_input->update_sink_latency_range = sink_input_update_sink_latency_range_cb;
    u->sink_input->update_sink_fixed_latency = sink_input_update_sink_fixed_latency_cb;
    u->sink_input->kill = sink_input_kill_cb;
    u->sink_input->attach = sink_input_attach_cb;
    u->sink_input->detach = sink_input_detach_cb;
    u->sink_input->moving = sink_input_moving_cb;
    u->sink_input->volume_changed = use_volume_sharing ? NULL : sink_input_volume_changed_cb;
    u->sink_input->mute_changed = sink_input_mute_changed_cb;
    u->sink_input->state_change = sink_input_state_change_cb;
    u->sink_input->userdata = u;

    u->sink->input_to_master = u->sink_input;

    pa_sink_input_get_silence(u->sink_input, &silence);
    u->memblockq = pa_memblockq_new("module-drc memblockq", 0, MEMBLOCKQ_MAXLENGTH, 0, &ss, 1, 1, 0, &silence);
    pa_memblock_unref(silence.memblock);
    u->block_size = pa_usec_to_bytes(PA_USEC_PER_SEC/20, &ss);

    /* (9) INITIALIZE ANYTHING ELSE YOU NEED HERE */
    //Parsing logic to read sndfilter.txt configuration file
    unsigned long rate;
    float pregain, threshold, knee, ratio, attack, release, predelay, releasezone1, releasezone2, releasezone3, releasezone4;
    float postgain, wet;

    if ((u->config_file = fopen("/etc/pulse/sndfilter.txt","r")) == NULL){
       pa_log("Failed to open sndfilter configuration file");
       goto fail;
    }

    char varname[60], c;

    char *configFile = "/etc/pulse/sndfilter.txt";
    if(readParametersFromFile(configFile, &pregain, &threshold, &knee, &ratio, &attack, &release, &predelay, &releasezone1, \
        &releasezone2, &releasezone3, &releasezone4, &postgain, &wet) == -1)
    {
       pa_log("Failed to open sndfilter configuration file");
       goto fail;
    }

    u->rate = ss.rate;
    sf_advancecomp(&(u->state), ss.rate, pregain, threshold, knee, ratio, attack, release, predelay, releasezone1, releasezone2, releasezone3, releasezone4, postgain, wet);
    /* The order here is important. The input must be put first,
     * otherwise streams might attach to the sink before the sink
     * input is attached to the master. */
    pa_sink_input_put(u->sink_input);
    pa_sink_put(u->sink);
    pa_sink_input_cork(u->sink_input, false);

    pa_modargs_free(ma);

    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);

    pa__done(m);

    return -1;
}

int pa__get_n_used(pa_module *m) {
    struct userdata *u;

    pa_assert(m);
    pa_assert_se(u = m->userdata);

    return pa_sink_linked_by(u->sink);
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    /* See comments in sink_input_kill_cb() above regarding
     * destruction order! */

    if (u->sink_input)
        pa_sink_input_cork(u->sink_input, true);

    if (u->sink)
        pa_sink_unlink(u->sink);

    if (u->sink_input) {
        pa_sink_input_unlink(u->sink_input);
        pa_sink_input_unref(u->sink_input);
    }

    if (u->sink)
        pa_sink_unref(u->sink);

    if (u->memblockq)
        pa_memblockq_free(u->memblockq);

    if (u->config_file)
            fclose(u->config_file);

    pa_xfree(u);
}
