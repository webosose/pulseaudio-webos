/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <string.h>
#include <math.h>

#include <pulse/xmalloc.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/strbuf.h>
#include <pulsecore/core-util.h>

#include "resampler.h"
#include "ffmpeg/avcodec.h"
#include <speex/speex_resampler.h>
#ifdef HAVE_PALM_RESAMPLER
#include "palm/palm-filters.h"
#define PALM_SAMPLE_RATES 11
#endif

/* Number of samples of extra space we allow the resamplers to return */
#define EXTRA_FRAMES 128

struct ffmpeg_data { /* data specific to ffmpeg */
    struct AVResampleContext *state;
};

#ifdef HAVE_PALM_RESAMPLER
static int palm_init(pa_resampler *r); /* Palm resampler */
#endif

static int copy_init(pa_resampler *r);

static void setup_remap(const pa_resampler *r, pa_remap_t *m, bool *lfe_remixed);
static void free_remap(pa_remap_t *m);

static int (* const init_table[])(pa_resampler *r) = {
#ifdef HAVE_LIBSAMPLERATE
    [PA_RESAMPLER_SRC_SINC_BEST_QUALITY]   = pa_resampler_libsamplerate_init,
    [PA_RESAMPLER_SRC_SINC_MEDIUM_QUALITY] = pa_resampler_libsamplerate_init,
    [PA_RESAMPLER_SRC_SINC_FASTEST]        = pa_resampler_libsamplerate_init,
    [PA_RESAMPLER_SRC_ZERO_ORDER_HOLD]     = pa_resampler_libsamplerate_init,
    [PA_RESAMPLER_SRC_LINEAR]              = pa_resampler_libsamplerate_init,
#else
    [PA_RESAMPLER_SRC_SINC_BEST_QUALITY]   = NULL,
    [PA_RESAMPLER_SRC_SINC_MEDIUM_QUALITY] = NULL,
    [PA_RESAMPLER_SRC_SINC_FASTEST]        = NULL,
    [PA_RESAMPLER_SRC_ZERO_ORDER_HOLD]     = NULL,
    [PA_RESAMPLER_SRC_LINEAR]              = NULL,
#endif
    [PA_RESAMPLER_TRIVIAL]                 = pa_resampler_trivial_init,
#ifdef HAVE_SPEEX
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+0]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+1]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+2]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+3]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+4]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+5]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+6]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+7]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+8]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+9]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+10]     = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+0]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+1]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+2]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+3]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+4]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+5]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+6]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+7]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+8]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+9]      = pa_resampler_speex_init,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+10]     = pa_resampler_speex_init,
#else
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+0]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+1]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+2]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+3]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+4]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+5]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+6]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+7]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+8]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+9]      = NULL,
    [PA_RESAMPLER_SPEEX_FLOAT_BASE+10]     = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+0]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+1]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+2]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+3]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+4]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+5]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+6]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+7]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+8]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+9]      = NULL,
    [PA_RESAMPLER_SPEEX_FIXED_BASE+10]     = NULL,
#endif
    [PA_RESAMPLER_FFMPEG]                  = pa_resampler_ffmpeg_init,
    [PA_RESAMPLER_AUTO]                    = NULL,
    [PA_RESAMPLER_COPY]                    = copy_init,
    [PA_RESAMPLER_PEAKS]                   = pa_resampler_peaks_init,
#ifdef HAVE_PALM_RESAMPLER
    [PA_RESAMPLER_PALM]                    = palm_init,
#endif
#ifdef HAVE_SOXR
    [PA_RESAMPLER_SOXR_MQ]                 = pa_resampler_soxr_init,
    [PA_RESAMPLER_SOXR_HQ]                 = pa_resampler_soxr_init,
    [PA_RESAMPLER_SOXR_VHQ]                = pa_resampler_soxr_init,
#else
    [PA_RESAMPLER_SOXR_MQ]                 = NULL,
    [PA_RESAMPLER_SOXR_HQ]                 = NULL,
    [PA_RESAMPLER_SOXR_VHQ]                = NULL,
#endif
};

#ifdef HAVE_PALM_RESAMPLER
static int available_sample_rates[PALM_SAMPLE_RATES] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000
};
#endif

static pa_resample_method_t choose_auto_resampler(pa_resample_flags_t flags) {
    pa_resample_method_t method;

    if (pa_resample_method_supported(PA_RESAMPLER_SPEEX_FLOAT_BASE + 1))
        method = PA_RESAMPLER_SPEEX_FLOAT_BASE + 1;
    else if (flags & PA_RESAMPLER_VARIABLE_RATE)
        method = PA_RESAMPLER_TRIVIAL;
    else
        method = PA_RESAMPLER_FFMPEG;

    return method;
}

static pa_resample_method_t fix_method(
                pa_resample_flags_t flags,
                pa_resample_method_t method,
                const uint32_t rate_a,
                const uint32_t rate_b) {

#ifdef HAVE_PALM_RESAMPLER
    int index, valida = 0, validb = 0;
#endif

    pa_assert(pa_sample_rate_valid(rate_a));
    pa_assert(pa_sample_rate_valid(rate_b));
    pa_assert(method >= 0);
    pa_assert(method < PA_RESAMPLER_MAX);

#ifdef HAVE_PALM_RESAMPLER
    if (method == PA_RESAMPLER_PALM) {
        for (index = 0; index < PALM_SAMPLE_RATES; index++) {
            if (rate_a == available_sample_rates[index]) {
                valida = 1;
                break;
            }
        }

        if ((rate_b == 44100) || (rate_b == 48000)) {
            validb = 1;
        }

        if (!(valida && validb)) {
            pa_log_info("Will try to use 'speex-fixed-0', because sample rate is not supported for palm-resampler");
            method = PA_RESAMPLER_SPEEX_FIXED_BASE;
        }
    }
#endif

    if (!(flags & PA_RESAMPLER_VARIABLE_RATE) && rate_a == rate_b) {
        pa_log_info("Forcing resampler 'copy', because of fixed, identical sample rates.");
        method = PA_RESAMPLER_COPY;
    }

    if (!pa_resample_method_supported(method)) {
        pa_log_warn("Support for resampler '%s' not compiled in, reverting to 'auto'.", pa_resample_method_to_string(method));
        method = PA_RESAMPLER_AUTO;
    }

    switch (method) {
        case PA_RESAMPLER_COPY:
            if (rate_a != rate_b) {
                pa_log_info("Resampler 'copy' cannot change sampling rate, reverting to resampler 'auto'.");
                method = PA_RESAMPLER_AUTO;
                break;
            }
            /* Else fall through */
        case PA_RESAMPLER_FFMPEG:
        case PA_RESAMPLER_SOXR_MQ:
        case PA_RESAMPLER_SOXR_HQ:
        case PA_RESAMPLER_SOXR_VHQ:
            if (flags & PA_RESAMPLER_VARIABLE_RATE) {
                pa_log_info("Resampler '%s' cannot do variable rate, reverting to resampler 'auto'.", pa_resample_method_to_string(method));
                method = PA_RESAMPLER_AUTO;
            }
            break;

        /* The Peaks resampler only supports downsampling.
         * Revert to auto if we are upsampling */
        case PA_RESAMPLER_PEAKS:
            if (rate_a < rate_b) {
                pa_log_warn("The 'peaks' resampler only supports downsampling, reverting to resampler 'auto'.");
                method = PA_RESAMPLER_AUTO;
            }
            break;

        default:
            break;
    }

    if (method == PA_RESAMPLER_AUTO)
        method = choose_auto_resampler(flags);

#ifdef HAVE_SPEEX
    /* At this point, method is supported in the sense that it
     * has an init function and supports the required flags. However,
     * speex-float implementation in PulseAudio relies on the
     * assumption that is invalid if speex has been compiled with
     * --enable-fixed-point. Besides, speex-fixed is more efficient
     * in this configuration. So use it instead.
     */
    if (method >= PA_RESAMPLER_SPEEX_FLOAT_BASE && method <= PA_RESAMPLER_SPEEX_FLOAT_MAX) {
        if (pa_speex_is_fixed_point()) {
            pa_log_info("Speex appears to be compiled with --enable-fixed-point. "
                        "Switching to a fixed-point resampler because it should be faster.");
            method = method - PA_RESAMPLER_SPEEX_FLOAT_BASE + PA_RESAMPLER_SPEEX_FIXED_BASE;
        }
    }
#endif

    return method;
}

/* Return true if a is a more precise sample format than b, else return false */
static bool sample_format_more_precise(pa_sample_format_t a, pa_sample_format_t b) {
    pa_assert(pa_sample_format_valid(a));
    pa_assert(pa_sample_format_valid(b));

    switch (a) {
        case PA_SAMPLE_U8:
        case PA_SAMPLE_ALAW:
        case PA_SAMPLE_ULAW:
            return false;
            break;

        case PA_SAMPLE_S16LE:
        case PA_SAMPLE_S16BE:
            if (b == PA_SAMPLE_ULAW || b == PA_SAMPLE_ALAW || b == PA_SAMPLE_U8)
                return true;
            else
                return false;
            break;

        case PA_SAMPLE_S24LE:
        case PA_SAMPLE_S24BE:
        case PA_SAMPLE_S24_32LE:
        case PA_SAMPLE_S24_32BE:
            if (b == PA_SAMPLE_ULAW || b == PA_SAMPLE_ALAW || b == PA_SAMPLE_U8 ||
                b == PA_SAMPLE_S16LE || b == PA_SAMPLE_S16BE)
                return true;
            else
                return false;
            break;

        case PA_SAMPLE_FLOAT32LE:
        case PA_SAMPLE_FLOAT32BE:
        case PA_SAMPLE_S32LE:
        case PA_SAMPLE_S32BE:
            if (b == PA_SAMPLE_FLOAT32LE || b == PA_SAMPLE_FLOAT32BE ||
                b == PA_SAMPLE_S32LE || b == PA_SAMPLE_S32BE)
                return false;
            else
                return true;
            break;

        default:
            return false;
    }
}

static pa_sample_format_t choose_work_format(
                    pa_resample_method_t method,
                    pa_sample_format_t a,
                    pa_sample_format_t b,
                    bool map_required) {
    pa_sample_format_t work_format;

    pa_assert(pa_sample_format_valid(a));
    pa_assert(pa_sample_format_valid(b));
    pa_assert(method >= 0);
    pa_assert(method < PA_RESAMPLER_MAX);

    if (method >= PA_RESAMPLER_SPEEX_FIXED_BASE && method <= PA_RESAMPLER_SPEEX_FIXED_MAX)
        method = PA_RESAMPLER_SPEEX_FIXED_BASE;

    switch (method) {
        /* This block is for resampling functions that only
         * support the S16 sample format. */
        case PA_RESAMPLER_SPEEX_FIXED_BASE:     /* fall through */
        case PA_RESAMPLER_FFMPEG:
            work_format = PA_SAMPLE_S16NE;
            break;

#ifdef HAVE_PALM_RESAMPLER
        case PA_RESAMPLER_PALM:
            work_format = PA_SAMPLE_S16LE;
            break;
#endif

        /* This block is for resampling functions that support
         * any sample format. */
        case PA_RESAMPLER_COPY:
        case PA_RESAMPLER_TRIVIAL:
            if (!map_required && a == b) {
                work_format = a;
                break;
            }
            /* If both input and output are using S32NE and we don't
             * need any resampling we can use S32NE directly, avoiding
             * converting back and forth between S32NE and
             * FLOAT32NE. */
            if ((a == PA_SAMPLE_S32NE) && (b == PA_SAMPLE_S32NE)) {
                work_format = PA_SAMPLE_S32NE;
                break;
            }
            /* Else fall through */
        case PA_RESAMPLER_PEAKS:
            /* PEAKS, COPY and TRIVIAL do not benefit from increased
             * working precision, so for better performance use s16ne
             * if either input or output fits in it. */
            if (a == PA_SAMPLE_S16NE || b == PA_SAMPLE_S16NE) {
                work_format = PA_SAMPLE_S16NE;
                break;
            }
            /* Else fall through */
        case PA_RESAMPLER_SOXR_MQ:
        case PA_RESAMPLER_SOXR_HQ:
        case PA_RESAMPLER_SOXR_VHQ:
            /* Do processing with max precision of input and output. */
            if (sample_format_more_precise(a, PA_SAMPLE_S16NE) ||
                sample_format_more_precise(b, PA_SAMPLE_S16NE))
                work_format = PA_SAMPLE_FLOAT32NE;
            else
                work_format = PA_SAMPLE_S16NE;
            break;

        default:
            work_format = PA_SAMPLE_FLOAT32NE;
    }

    return work_format;
}

pa_resampler* pa_resampler_new(
        pa_mempool *pool,
        const pa_sample_spec *a,
        const pa_channel_map *am,
        const pa_sample_spec *b,
        const pa_channel_map *bm,
	unsigned crossover_freq,
        pa_resample_method_t method,
        pa_resample_flags_t flags) {

    pa_resampler *r = NULL;
    bool lfe_remixed = false;

    pa_assert(pool);
    pa_assert(a);
    pa_assert(b);
    pa_assert(pa_sample_spec_valid(a));
    pa_assert(pa_sample_spec_valid(b));
    pa_assert(method >= 0);
    pa_assert(method < PA_RESAMPLER_MAX);

    method = fix_method(flags, method, a->rate, b->rate);

    r = pa_xnew0(pa_resampler, 1);
    r->mempool = pool;
    r->method = method;
    r->flags = flags;

    /* Fill sample specs */
    r->i_ss = *a;
    r->o_ss = *b;

    if (am)
        r->i_cm = *am;
    else if (!pa_channel_map_init_auto(&r->i_cm, r->i_ss.channels, PA_CHANNEL_MAP_DEFAULT))
        goto fail;

    if (bm)
        r->o_cm = *bm;
    else if (!pa_channel_map_init_auto(&r->o_cm, r->o_ss.channels, PA_CHANNEL_MAP_DEFAULT))
        goto fail;

    r->i_fz = pa_frame_size(a);
    r->o_fz = pa_frame_size(b);

    r->map_required = (r->i_ss.channels != r->o_ss.channels || (!(r->flags & PA_RESAMPLER_NO_REMAP) &&
        !pa_channel_map_equal(&r->i_cm, &r->o_cm)));

    r->work_format = choose_work_format(method, a->format, b->format, r->map_required);
    r->w_sz = pa_sample_size_of_format(r->work_format);

    if (r->i_ss.format != r->work_format) {
        if (r->work_format == PA_SAMPLE_FLOAT32NE) {
            if (!(r->to_work_format_func = pa_get_convert_to_float32ne_function(r->i_ss.format)))
                goto fail;
        } else {
            pa_assert(r->work_format == PA_SAMPLE_S16NE);
            if (!(r->to_work_format_func = pa_get_convert_to_s16ne_function(r->i_ss.format)))
                goto fail;
        }
    }

    if (r->o_ss.format != r->work_format) {
        if (r->work_format == PA_SAMPLE_FLOAT32NE) {
            if (!(r->from_work_format_func = pa_get_convert_from_float32ne_function(r->o_ss.format)))
                goto fail;
        } else {
            pa_assert(r->work_format == PA_SAMPLE_S16NE);
            if (!(r->from_work_format_func = pa_get_convert_from_s16ne_function(r->o_ss.format)))
                goto fail;
        }
    }

    if (r->o_ss.channels <= r->i_ss.channels) {
        /* pipeline is: format conv. -> remap -> resample -> format conv. */
        r->work_channels = r->o_ss.channels;

        /* leftover buffer is remap output buffer (before resampling) */
        r->leftover_buf = &r->remap_buf;
        r->leftover_buf_size = &r->remap_buf_size;
        r->have_leftover = &r->leftover_in_remap;
    } else {
        /* pipeline is: format conv. -> resample -> remap -> format conv. */
        r->work_channels = r->i_ss.channels;

        /* leftover buffer is to_work output buffer (before resampling) */
        r->leftover_buf = &r->to_work_format_buf;
        r->leftover_buf_size = &r->to_work_format_buf_size;
        r->have_leftover = &r->leftover_in_to_work;
    }
    r->w_fz = pa_sample_size_of_format(r->work_format) * r->work_channels;

    pa_log_debug("Resampler:");
    pa_log_debug("  rate %d -> %d (method %s)", a->rate, b->rate, pa_resample_method_to_string(r->method));
    pa_log_debug("  format %s -> %s (intermediate %s)", pa_sample_format_to_string(a->format),
                 pa_sample_format_to_string(b->format), pa_sample_format_to_string(r->work_format));
    pa_log_debug("  channels %d -> %d (resampling %d)", a->channels, b->channels, r->work_channels);

    /* set up the remap structure */
    if (r->map_required)
        setup_remap(r, &r->remap, &lfe_remixed);

    if (lfe_remixed && crossover_freq > 0) {
        pa_sample_spec wss = r->o_ss;
        wss.format = r->work_format;
        /* FIXME: For now just hardcode maxrewind to 3 seconds */
        r->lfe_filter = pa_lfe_filter_new(&wss, &r->o_cm, (float)crossover_freq, b->rate * 3);
        pa_log_debug("  lfe filter activated (LR4 type), the crossover_freq = %uHz", crossover_freq);
    }

    /* initialize implementation */
    if (init_table[method](r) < 0)
        goto fail;

    return r;

fail:
    if (r->lfe_filter)
      pa_lfe_filter_free(r->lfe_filter);
    pa_xfree(r);

    return NULL;
}

void pa_resampler_free(pa_resampler *r) {
    pa_assert(r);

    if (r->impl.free)
        r->impl.free(r);
    else
        pa_xfree(r->impl.data);

    if (r->lfe_filter)
        pa_lfe_filter_free(r->lfe_filter);

    if (r->to_work_format_buf.memblock)
        pa_memblock_unref(r->to_work_format_buf.memblock);
    if (r->remap_buf.memblock)
        pa_memblock_unref(r->remap_buf.memblock);
    if (r->resample_buf.memblock)
        pa_memblock_unref(r->resample_buf.memblock);
    if (r->from_work_format_buf.memblock)
        pa_memblock_unref(r->from_work_format_buf.memblock);

    free_remap(&r->remap);

    pa_xfree(r);
}

void pa_resampler_set_input_rate(pa_resampler *r, uint32_t rate) {
    pa_assert(r);
    pa_assert(rate > 0);
    pa_assert(r->impl.update_rates);

    if (r->i_ss.rate == rate)
        return;

    r->i_ss.rate = rate;

    r->impl.update_rates(r);
}

void pa_resampler_set_output_rate(pa_resampler *r, uint32_t rate) {
    pa_assert(r);
    pa_assert(rate > 0);
    pa_assert(r->impl.update_rates);

    if (r->o_ss.rate == rate)
        return;

    r->o_ss.rate = rate;

    r->impl.update_rates(r);

    if (r->lfe_filter)
        pa_lfe_filter_update_rate(r->lfe_filter, rate);
}

size_t pa_resampler_request(pa_resampler *r, size_t out_length) {
    pa_assert(r);

    /* Let's round up here to make it more likely that the caller will get at
     * least out_length amount of data from pa_resampler_run().
     *
     * We don't take the leftover into account here. If we did, then it might
     * be in theory possible that this function would return 0 and
     * pa_resampler_run() would also return 0. That could lead to infinite
     * loops. When the leftover is ignored here, such loops would eventually
     * terminate, because the leftover would grow each round, finally
     * surpassing the minimum input threshold of the resampler. */
    return ((((uint64_t) ((out_length + r->o_fz-1) / r->o_fz) * r->i_ss.rate) + r->o_ss.rate-1) / r->o_ss.rate) * r->i_fz;
}

size_t pa_resampler_result(pa_resampler *r, size_t in_length) {
    size_t frames;

    pa_assert(r);

    /* Let's round up here to ensure that the caller will always allocate big
     * enough output buffer. */

    frames = (in_length + r->i_fz - 1) / r->i_fz;
    if (*r->have_leftover)
        frames += r->leftover_buf->length / r->w_fz;

    return (((uint64_t) frames * r->o_ss.rate + r->i_ss.rate - 1) / r->i_ss.rate) * r->o_fz;
}

size_t pa_resampler_max_block_size(pa_resampler *r) {
    size_t block_size_max;
    pa_sample_spec max_ss;
    size_t max_fs;
    size_t frames;

    pa_assert(r);

    block_size_max = pa_mempool_block_size_max(r->mempool);

    /* We deduce the "largest" sample spec we're using during the
     * conversion */
    max_ss.channels = (uint8_t) (PA_MAX(r->i_ss.channels, r->o_ss.channels));

    /* We silently assume that the format enum is ordered by size */
    max_ss.format = PA_MAX(r->i_ss.format, r->o_ss.format);
    max_ss.format = PA_MAX(max_ss.format, r->work_format);

    max_ss.rate = PA_MAX(r->i_ss.rate, r->o_ss.rate);

    max_fs = pa_frame_size(&max_ss);
    frames = block_size_max / max_fs - EXTRA_FRAMES;

    pa_assert(frames >= (r->leftover_buf->length / r->w_fz));
    if (*r->have_leftover)
        frames -= r->leftover_buf->length / r->w_fz;

    block_size_max = ((uint64_t) frames * r->i_ss.rate / max_ss.rate) * r->i_fz;

    if (block_size_max > 0)
        return block_size_max;
    else
        /* A single input frame may result in so much output that it doesn't
         * fit in one standard memblock (e.g. converting 1 Hz to 44100 Hz). In
         * this case the max block size will be set to one frame, and some
         * memory will be probably be allocated with malloc() instead of using
         * the memory pool.
         *
         * XXX: Should we support this case at all? We could also refuse to
         * create resamplers whose max block size would exceed the memory pool
         * block size. In this case also updating the resampler rate should
         * fail if the new rate would cause an excessive max block size (in
         * which case the stream would probably have to be killed). */
        return r->i_fz;
}

void pa_resampler_reset(pa_resampler *r) {
    pa_assert(r);

    if (r->impl.reset)
        r->impl.reset(r);

    if (r->lfe_filter)
        pa_lfe_filter_reset(r->lfe_filter);

    *r->have_leftover = false;
}

void pa_resampler_rewind(pa_resampler *r, size_t out_frames) {
    pa_assert(r);

    /* For now, we don't have any rewindable resamplers, so we just
       reset the resampler instead (and hope that nobody hears the difference). */
    if (r->impl.reset)
        r->impl.reset(r);

    if (r->lfe_filter)
        pa_lfe_filter_rewind(r->lfe_filter, out_frames);

    *r->have_leftover = false;
}

pa_resample_method_t pa_resampler_get_method(pa_resampler *r) {
    pa_assert(r);

    return r->method;
}

const pa_channel_map* pa_resampler_input_channel_map(pa_resampler *r) {
    pa_assert(r);

    return &r->i_cm;
}

const pa_sample_spec* pa_resampler_input_sample_spec(pa_resampler *r) {
    pa_assert(r);

    return &r->i_ss;
}

const pa_channel_map* pa_resampler_output_channel_map(pa_resampler *r) {
    pa_assert(r);

    return &r->o_cm;
}

const pa_sample_spec* pa_resampler_output_sample_spec(pa_resampler *r) {
    pa_assert(r);

    return &r->o_ss;
}

static const char * const resample_methods[] = {
    "src-sinc-best-quality",
    "src-sinc-medium-quality",
    "src-sinc-fastest",
    "src-zero-order-hold",
    "src-linear",
    "trivial",
    "speex-float-0",
    "speex-float-1",
    "speex-float-2",
    "speex-float-3",
    "speex-float-4",
    "speex-float-5",
    "speex-float-6",
    "speex-float-7",
    "speex-float-8",
    "speex-float-9",
    "speex-float-10",
    "speex-fixed-0",
    "speex-fixed-1",
    "speex-fixed-2",
    "speex-fixed-3",
    "speex-fixed-4",
    "speex-fixed-5",
    "speex-fixed-6",
    "speex-fixed-7",
    "speex-fixed-8",
    "speex-fixed-9",
    "speex-fixed-10",
    "ffmpeg",
    "auto",
    "copy",
    "peaks",
#ifdef HAVE_PALM_RESAMPLER
    "palm",
#endif
   "soxr-mq",
   "soxr-hq",
   "soxr-vhq"
};

const char *pa_resample_method_to_string(pa_resample_method_t m) {

    if (m < 0 || m >= PA_RESAMPLER_MAX)
        return NULL;

    return resample_methods[m];
}

int pa_resample_method_supported(pa_resample_method_t m) {

    if (m < 0 || m >= PA_RESAMPLER_MAX)
        return 0;

#ifndef HAVE_LIBSAMPLERATE
    if (m <= PA_RESAMPLER_SRC_LINEAR)
        return 0;
#endif

#ifndef HAVE_SPEEX
    if (m >= PA_RESAMPLER_SPEEX_FLOAT_BASE && m <= PA_RESAMPLER_SPEEX_FLOAT_MAX)
        return 0;
    if (m >= PA_RESAMPLER_SPEEX_FIXED_BASE && m <= PA_RESAMPLER_SPEEX_FIXED_MAX)
        return 0;
#endif

#ifndef HAVE_SOXR
    if (m >= PA_RESAMPLER_SOXR_MQ && m <= PA_RESAMPLER_SOXR_VHQ)
        return 0;
#endif

    return 1;
}

pa_resample_method_t pa_parse_resample_method(const char *string) {
    pa_resample_method_t m;

    pa_assert(string);

    for (m = 0; m < PA_RESAMPLER_MAX; m++)
        if (pa_streq(string, resample_methods[m]))
            return m;

    if (pa_streq(string, "speex-fixed"))
        return PA_RESAMPLER_SPEEX_FIXED_BASE + 1;

    if (pa_streq(string, "speex-float"))
        return PA_RESAMPLER_SPEEX_FLOAT_BASE + 1;

    return PA_RESAMPLER_INVALID;
}

static bool on_left(pa_channel_position_t p) {

    return
        p == PA_CHANNEL_POSITION_FRONT_LEFT ||
        p == PA_CHANNEL_POSITION_REAR_LEFT ||
        p == PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER ||
        p == PA_CHANNEL_POSITION_SIDE_LEFT ||
        p == PA_CHANNEL_POSITION_TOP_FRONT_LEFT ||
        p == PA_CHANNEL_POSITION_TOP_REAR_LEFT;
}

static bool on_right(pa_channel_position_t p) {

    return
        p == PA_CHANNEL_POSITION_FRONT_RIGHT ||
        p == PA_CHANNEL_POSITION_REAR_RIGHT ||
        p == PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER ||
        p == PA_CHANNEL_POSITION_SIDE_RIGHT ||
        p == PA_CHANNEL_POSITION_TOP_FRONT_RIGHT ||
        p == PA_CHANNEL_POSITION_TOP_REAR_RIGHT;
}

static bool on_center(pa_channel_position_t p) {

    return
        p == PA_CHANNEL_POSITION_FRONT_CENTER ||
        p == PA_CHANNEL_POSITION_REAR_CENTER ||
        p == PA_CHANNEL_POSITION_TOP_CENTER ||
        p == PA_CHANNEL_POSITION_TOP_FRONT_CENTER ||
        p == PA_CHANNEL_POSITION_TOP_REAR_CENTER;
}

static bool on_lfe(pa_channel_position_t p) {
    return
        p == PA_CHANNEL_POSITION_LFE;
}

static bool on_front(pa_channel_position_t p) {
    return
        p == PA_CHANNEL_POSITION_FRONT_LEFT ||
        p == PA_CHANNEL_POSITION_FRONT_RIGHT ||
        p == PA_CHANNEL_POSITION_FRONT_CENTER ||
        p == PA_CHANNEL_POSITION_TOP_FRONT_LEFT ||
        p == PA_CHANNEL_POSITION_TOP_FRONT_RIGHT ||
        p == PA_CHANNEL_POSITION_TOP_FRONT_CENTER ||
        p == PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER ||
        p == PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;
}

static bool on_rear(pa_channel_position_t p) {
    return
        p == PA_CHANNEL_POSITION_REAR_LEFT ||
        p == PA_CHANNEL_POSITION_REAR_RIGHT ||
        p == PA_CHANNEL_POSITION_REAR_CENTER ||
        p == PA_CHANNEL_POSITION_TOP_REAR_LEFT ||
        p == PA_CHANNEL_POSITION_TOP_REAR_RIGHT ||
        p == PA_CHANNEL_POSITION_TOP_REAR_CENTER;
}

static bool on_side(pa_channel_position_t p) {
    return
        p == PA_CHANNEL_POSITION_SIDE_LEFT ||
        p == PA_CHANNEL_POSITION_SIDE_RIGHT ||
        p == PA_CHANNEL_POSITION_TOP_CENTER;
}

enum {
    ON_FRONT,
    ON_REAR,
    ON_SIDE,
    ON_OTHER
};

static int front_rear_side(pa_channel_position_t p) {
    if (on_front(p))
        return ON_FRONT;
    if (on_rear(p))
        return ON_REAR;
    if (on_side(p))
        return ON_SIDE;
    return ON_OTHER;
}

/* Fill a map of which output channels should get mono from input, not including
 * LFE output channels. (The LFE output channels are mapped separately.)
 */
static void setup_oc_mono_map(const pa_resampler *r, float *oc_mono_map) {
    unsigned oc;
    unsigned n_oc;
    bool found_oc_for_mono = false;

    pa_assert(r);
    pa_assert(oc_mono_map);

    n_oc = r->o_ss.channels;

    if (!(r->flags & PA_RESAMPLER_NO_FILL_SINK)) {
        /* Mono goes to all non-LFE output channels and we're done. */
        for (oc = 0; oc < n_oc; oc++)
            oc_mono_map[oc] = on_lfe(r->o_cm.map[oc]) ? 0.0f : 1.0f;
        return;
    } else {
        /* Initialize to all zero so we can select individual channels below. */
        for (oc = 0; oc < n_oc; oc++)
            oc_mono_map[oc] = 0.0f;
    }

    for (oc = 0; oc < n_oc; oc++) {
        if (r->o_cm.map[oc] == PA_CHANNEL_POSITION_MONO) {
            oc_mono_map[oc] = 1.0f;
            found_oc_for_mono = true;
        }
    }
    if (found_oc_for_mono)
        return;

    for (oc = 0; oc < n_oc; oc++) {
        if (r->o_cm.map[oc] == PA_CHANNEL_POSITION_FRONT_CENTER) {
            oc_mono_map[oc] = 1.0f;
            found_oc_for_mono = true;
        }
    }
    if (found_oc_for_mono)
        return;

    for (oc = 0; oc < n_oc; oc++) {
        if (r->o_cm.map[oc] == PA_CHANNEL_POSITION_FRONT_LEFT || r->o_cm.map[oc] == PA_CHANNEL_POSITION_FRONT_RIGHT) {
            oc_mono_map[oc] = 1.0f;
            found_oc_for_mono = true;
        }
    }
    if (found_oc_for_mono)
        return;

    /* Give up on finding a suitable map for mono, and just send it to all
     * non-LFE output channels.
     */
    for (oc = 0; oc < n_oc; oc++)
        oc_mono_map[oc] = on_lfe(r->o_cm.map[oc]) ? 0.0f : 1.0f;
}

static void setup_remap(const pa_resampler *r, pa_remap_t *m, bool *lfe_remixed) {
    unsigned oc, ic;
    unsigned n_oc, n_ic;
    bool ic_connected[PA_CHANNELS_MAX];
    pa_strbuf *s;
    char *t;

    pa_assert(r);
    pa_assert(m);
    pa_assert(lfe_remixed);

    n_oc = r->o_ss.channels;
    n_ic = r->i_ss.channels;

    m->format = r->work_format;
    m->i_ss = r->i_ss;
    m->o_ss = r->o_ss;

    memset(m->map_table_f, 0, sizeof(m->map_table_f));
    memset(m->map_table_i, 0, sizeof(m->map_table_i));

    memset(ic_connected, 0, sizeof(ic_connected));
    *lfe_remixed = false;

    if (r->flags & PA_RESAMPLER_NO_REMAP) {
        for (oc = 0; oc < PA_MIN(n_ic, n_oc); oc++)
            m->map_table_f[oc][oc] = 1.0f;

    } else if (r->flags & PA_RESAMPLER_NO_REMIX) {
        for (oc = 0; oc < n_oc; oc++) {
            pa_channel_position_t b = r->o_cm.map[oc];

            for (ic = 0; ic < n_ic; ic++) {
                pa_channel_position_t a = r->i_cm.map[ic];

                /* We shall not do any remixing. Hence, just check by name */
                if (a == b)
                    m->map_table_f[oc][ic] = 1.0f;
            }
        }
    } else {

        /* OK, we shall do the full monty: upmixing and downmixing. Our
         * algorithm is relatively simple, does not do spacialization, or delay
         * elements. LFE filters are done after the remap step. Patches are always
         * welcome, though. Oh, and it doesn't do any matrix decoding. (Which
         * probably wouldn't make any sense anyway.)
         *
         * This code is not idempotent: downmixing an upmixed stereo stream is
         * not identical to the original. The volume will not match, and the
         * two channels will be a linear combination of both.
         *
         * This is loosely based on random suggestions found on the Internet,
         * such as this:
         * http://www.halfgaar.net/surround-sound-in-linux and the alsa upmix
         * plugin.
         *
         * The algorithm works basically like this:
         *
         * 1) Connect all channels with matching names.
         *    This also includes fixing confusion between "5.1" and
         *    "5.1 (Side)" layouts, done by mpv.
         *
         * 2) Mono Handling:
         *    S:Mono: See setup_oc_mono_map().
         *    D:Mono: Avg all S:channels
         *
         * 3) Mix D:Left, D:Right (if PA_RESAMPLER_NO_FILL_SINK is clear):
         *    D:Left: If not connected, avg all S:Left
         *    D:Right: If not connected, avg all S:Right
         *
         * 4) Mix D:Center (if PA_RESAMPLER_NO_FILL_SINK is clear):
         *    If not connected, avg all S:Center
         *    If still not connected, avg all S:Left, S:Right
         *
         * 5) Mix D:LFE
         *    If not connected, avg all S:*
         *
         * 6) Make sure S:Left/S:Right is used: S:Left/S:Right: If not
         *    connected, mix into all D:left and all D:right channels. Gain is
         *    1/9.
         *
         * 7) Make sure S:Center, S:LFE is used:
         *
         *    S:Center, S:LFE: If not connected, mix into all D:left, all
         *    D:right, all D:center channels. Gain is 0.5 for center and 0.375
         *    for LFE. C-front is only mixed into L-front/R-front if available,
         *    otherwise into all L/R channels. Similarly for C-rear.
         *
         * 8) Normalize each row in the matrix such that the sum for each row is
         *    not larger than 1.0 in order to avoid clipping.
         *
         * S: and D: shall relate to the source resp. destination channels.
         *
         * Rationale: 1, 2 are probably obvious. For 3: this copies front to
         * rear if needed. For 4: we try to find some suitable C source for C,
         * if we don't find any, we avg L and R. For 5: LFE is mixed from all
         * channels. For 6: the rear channels should not be dropped entirely,
         * however have only minimal impact. For 7: movies usually encode
         * speech on the center channel. Thus we have to make sure this channel
         * is distributed to L and R if not available in the output. Also, LFE
         * is used to achieve a greater dynamic range, and thus we should try
         * to do our best to pass it to L+R.
         */

        unsigned
            ic_left = 0,
            ic_right = 0,
            ic_center = 0,
            ic_unconnected_left = 0,
            ic_unconnected_right = 0,
            ic_unconnected_center = 0,
            ic_unconnected_lfe = 0;
        bool ic_unconnected_center_mixed_in = 0;
        float oc_mono_map[PA_CHANNELS_MAX];

        for (ic = 0; ic < n_ic; ic++) {
            if (on_left(r->i_cm.map[ic]))
                ic_left++;
            if (on_right(r->i_cm.map[ic]))
                ic_right++;
            if (on_center(r->i_cm.map[ic]))
                ic_center++;
        }

        setup_oc_mono_map(r, oc_mono_map);

        for (oc = 0; oc < n_oc; oc++) {
            bool oc_connected = false;
            pa_channel_position_t b = r->o_cm.map[oc];

            for (ic = 0; ic < n_ic; ic++) {
                pa_channel_position_t a = r->i_cm.map[ic];

                if (a == b) {
                    m->map_table_f[oc][ic] = 1.0f;

                    oc_connected = true;
                    ic_connected[ic] = true;
                }
                else if (a == PA_CHANNEL_POSITION_MONO && oc_mono_map[oc] > 0.0f) {
                    m->map_table_f[oc][ic] = oc_mono_map[oc];

                    oc_connected = true;
                    ic_connected[ic] = true;
                }
                else if (b == PA_CHANNEL_POSITION_MONO) {
                    m->map_table_f[oc][ic] = 1.0f / (float) n_ic;

                    oc_connected = true;
                    ic_connected[ic] = true;
                }
            }

            if (!oc_connected) {
                /* Maybe it is due to 5.1 rear/side confustion? */
                for (ic = 0; ic < n_ic; ic++) {
                    pa_channel_position_t a = r->i_cm.map[ic];
                    if (ic_connected[ic])
                        continue;

                    if ((a == PA_CHANNEL_POSITION_REAR_LEFT && b == PA_CHANNEL_POSITION_SIDE_LEFT) ||
                        (a == PA_CHANNEL_POSITION_SIDE_LEFT && b == PA_CHANNEL_POSITION_REAR_LEFT) ||
                        (a == PA_CHANNEL_POSITION_REAR_RIGHT && b == PA_CHANNEL_POSITION_SIDE_RIGHT) ||
                        (a == PA_CHANNEL_POSITION_SIDE_RIGHT && b == PA_CHANNEL_POSITION_REAR_RIGHT)) {

                        m->map_table_f[oc][ic] = 1.0f;

                        oc_connected = true;
                        ic_connected[ic] = true;
                    }
                }
            }

            if (!oc_connected) {
                /* Try to find matching input ports for this output port */

                if (on_left(b) && !(r->flags & PA_RESAMPLER_NO_FILL_SINK)) {

                    /* We are not connected and on the left side, let's
                     * average all left side input channels. */

                    if (ic_left > 0)
                        for (ic = 0; ic < n_ic; ic++)
                            if (on_left(r->i_cm.map[ic])) {
                                m->map_table_f[oc][ic] = 1.0f / (float) ic_left;
                                ic_connected[ic] = true;
                            }

                    /* We ignore the case where there is no left input channel.
                     * Something is really wrong in this case anyway. */

                } else if (on_right(b) && !(r->flags & PA_RESAMPLER_NO_FILL_SINK)) {

                    /* We are not connected and on the right side, let's
                     * average all right side input channels. */

                    if (ic_right > 0)
                        for (ic = 0; ic < n_ic; ic++)
                            if (on_right(r->i_cm.map[ic])) {
                                m->map_table_f[oc][ic] = 1.0f / (float) ic_right;
                                ic_connected[ic] = true;
                            }

                    /* We ignore the case where there is no right input
                     * channel. Something is really wrong in this case anyway.
                     * */

                } else if (on_center(b) && !(r->flags & PA_RESAMPLER_NO_FILL_SINK)) {

                    if (ic_center > 0) {

                        /* We are not connected and at the center. Let's average
                         * all center input channels. */

                        for (ic = 0; ic < n_ic; ic++)
                            if (on_center(r->i_cm.map[ic])) {
                                m->map_table_f[oc][ic] = 1.0f / (float) ic_center;
                                ic_connected[ic] = true;
                            }

                    } else if (ic_left + ic_right > 0) {

                        /* Hmm, no center channel around, let's synthesize it
                         * by mixing L and R.*/

                        for (ic = 0; ic < n_ic; ic++)
                            if (on_left(r->i_cm.map[ic]) || on_right(r->i_cm.map[ic])) {
                                m->map_table_f[oc][ic] = 1.0f / (float) (ic_left + ic_right);
                                ic_connected[ic] = true;
                            }
                    }

                    /* We ignore the case where there is not even a left or
                     * right input channel. Something is really wrong in this
                     * case anyway. */

                } else if (on_lfe(b) && (r->flags & PA_RESAMPLER_PRODUCE_LFE)) {

                    /* We are not connected and an LFE. Let's average all
                     * channels for LFE. */

                    for (ic = 0; ic < n_ic; ic++)
                        m->map_table_f[oc][ic] = 1.0f / (float) n_ic;

                    /* Please note that a channel connected to LFE doesn't
                     * really count as connected. */

                    *lfe_remixed = true;
                }
            }
        }

        for (ic = 0; ic < n_ic; ic++) {
            pa_channel_position_t a = r->i_cm.map[ic];

            if (ic_connected[ic])
                continue;

            if (on_left(a))
                ic_unconnected_left++;
            else if (on_right(a))
                ic_unconnected_right++;
            else if (on_center(a))
                ic_unconnected_center++;
            else if (on_lfe(a))
                ic_unconnected_lfe++;
        }

        for (ic = 0; ic < n_ic; ic++) {
            pa_channel_position_t a = r->i_cm.map[ic];

            if (ic_connected[ic])
                continue;

            for (oc = 0; oc < n_oc; oc++) {
                pa_channel_position_t b = r->o_cm.map[oc];

                if (on_left(a) && on_left(b))
                    m->map_table_f[oc][ic] = (1.f/9.f) / (float) ic_unconnected_left;

                else if (on_right(a) && on_right(b))
                    m->map_table_f[oc][ic] = (1.f/9.f) / (float) ic_unconnected_right;

                else if (on_center(a) && on_center(b)) {
                    m->map_table_f[oc][ic] = (1.f/9.f) / (float) ic_unconnected_center;
                    ic_unconnected_center_mixed_in = true;

                } else if (on_lfe(a) && (r->flags & PA_RESAMPLER_CONSUME_LFE))
                    m->map_table_f[oc][ic] = .375f / (float) ic_unconnected_lfe;
            }
        }

        if (ic_unconnected_center > 0 && !ic_unconnected_center_mixed_in) {
            unsigned ncenter[PA_CHANNELS_MAX];
            bool found_frs[PA_CHANNELS_MAX];

            memset(ncenter, 0, sizeof(ncenter));
            memset(found_frs, 0, sizeof(found_frs));

            /* Hmm, as it appears there was no center channel we
               could mix our center channel in. In this case, mix it into
               left and right. Using .5 as the factor. */

            for (ic = 0; ic < n_ic; ic++) {

                if (ic_connected[ic])
                    continue;

                if (!on_center(r->i_cm.map[ic]))
                    continue;

                for (oc = 0; oc < n_oc; oc++) {

                    if (!on_left(r->o_cm.map[oc]) && !on_right(r->o_cm.map[oc]))
                        continue;

                    if (front_rear_side(r->i_cm.map[ic]) == front_rear_side(r->o_cm.map[oc])) {
                        found_frs[ic] = true;
                        break;
                    }
                }

                for (oc = 0; oc < n_oc; oc++) {

                    if (!on_left(r->o_cm.map[oc]) && !on_right(r->o_cm.map[oc]))
                        continue;

                    if (!found_frs[ic] || front_rear_side(r->i_cm.map[ic]) == front_rear_side(r->o_cm.map[oc]))
                        ncenter[oc]++;
                }
            }

            for (oc = 0; oc < n_oc; oc++) {

                if (!on_left(r->o_cm.map[oc]) && !on_right(r->o_cm.map[oc]))
                    continue;

                if (ncenter[oc] <= 0)
                    continue;

                for (ic = 0; ic < n_ic; ic++) {

                    if (!on_center(r->i_cm.map[ic]))
                        continue;

                    if (!found_frs[ic] || front_rear_side(r->i_cm.map[ic]) == front_rear_side(r->o_cm.map[oc]))
                        m->map_table_f[oc][ic] = .5f / (float) ncenter[oc];
                }
            }
        }
    }

    for (oc = 0; oc < n_oc; oc++) {
        float sum = 0.0f;
        for (ic = 0; ic < n_ic; ic++)
            sum += m->map_table_f[oc][ic];

        if (sum > 1.0f)
            for (ic = 0; ic < n_ic; ic++)
                m->map_table_f[oc][ic] /= sum;
    }

    /* make an 16:16 int version of the matrix */
    for (oc = 0; oc < n_oc; oc++)
        for (ic = 0; ic < n_ic; ic++)
            m->map_table_i[oc][ic] = (int32_t) (m->map_table_f[oc][ic] * 0x10000);

    s = pa_strbuf_new();

    pa_strbuf_printf(s, "     ");
    for (ic = 0; ic < n_ic; ic++)
        pa_strbuf_printf(s, "  I%02u ", ic);
    pa_strbuf_puts(s, "\n    +");

    for (ic = 0; ic < n_ic; ic++)
        pa_strbuf_printf(s, "------");
    pa_strbuf_puts(s, "\n");

    for (oc = 0; oc < n_oc; oc++) {
        pa_strbuf_printf(s, "O%02u |", oc);

        for (ic = 0; ic < n_ic; ic++)
            pa_strbuf_printf(s, " %1.3f", m->map_table_f[oc][ic]);

        pa_strbuf_puts(s, "\n");
    }

    pa_log_debug("Channel matrix:\n%s", t = pa_strbuf_to_string_free(s));
    pa_xfree(t);

    /* initialize the remapping function */
    pa_init_remap_func(m);
}

static void free_remap(pa_remap_t *m) {
    pa_assert(m);

    pa_xfree(m->state);
}

/* check if buf's memblock is large enough to hold 'len' bytes; create a
 * new memblock if necessary and optionally preserve 'copy' data bytes */
static void fit_buf(pa_resampler *r, pa_memchunk *buf, size_t len, size_t *size, size_t copy) {
    pa_assert(size);

    if (!buf->memblock || len > *size) {
        pa_memblock *new_block = pa_memblock_new(r->mempool, len);

        if (buf->memblock) {
            if (copy > 0) {
                void *src = pa_memblock_acquire(buf->memblock);
                void *dst = pa_memblock_acquire(new_block);
                pa_assert(copy <= len);
                memcpy(dst, src, copy);
                pa_memblock_release(new_block);
                pa_memblock_release(buf->memblock);
            }

            pa_memblock_unref(buf->memblock);
        }

        buf->memblock = new_block;
        *size = len;
    }

    buf->length = len;
}

static pa_memchunk* convert_to_work_format(pa_resampler *r, pa_memchunk *input) {
    unsigned in_n_samples, out_n_samples;
    void *src, *dst;
    bool have_leftover;
    size_t leftover_length = 0;

    pa_assert(r);
    pa_assert(input);
    pa_assert(input->memblock);

    /* Convert the incoming sample into the work sample format and place them
     * in to_work_format_buf. The leftover data is already converted, so it's
     * part of the output buffer. */

    have_leftover = r->leftover_in_to_work;
    r->leftover_in_to_work = false;

    if (!have_leftover && (!r->to_work_format_func || !input->length))
        return input;
    else if (input->length <= 0)
        return &r->to_work_format_buf;

    in_n_samples = out_n_samples = (unsigned) ((input->length / r->i_fz) * r->i_ss.channels);

    if (have_leftover) {
        leftover_length = r->to_work_format_buf.length;
        out_n_samples += (unsigned) (leftover_length / r->w_sz);
    }

    fit_buf(r, &r->to_work_format_buf, r->w_sz * out_n_samples, &r->to_work_format_buf_size, leftover_length);

    src = pa_memblock_acquire_chunk(input);
    dst = (uint8_t *) pa_memblock_acquire(r->to_work_format_buf.memblock) + leftover_length;

    if (r->to_work_format_func)
        r->to_work_format_func(in_n_samples, src, dst);
    else
        memcpy(dst, src, input->length);

    pa_memblock_release(input->memblock);
    pa_memblock_release(r->to_work_format_buf.memblock);

    return &r->to_work_format_buf;
}

static pa_memchunk *remap_channels(pa_resampler *r, pa_memchunk *input) {
    unsigned in_n_samples, out_n_samples, in_n_frames, out_n_frames;
    void *src, *dst;
    size_t leftover_length = 0;
    bool have_leftover;

    pa_assert(r);
    pa_assert(input);
    pa_assert(input->memblock);

    /* Remap channels and place the result in remap_buf. There may be leftover
     * data in the beginning of remap_buf. The leftover data is already
     * remapped, so it's not part of the input, it's part of the output. */

    have_leftover = r->leftover_in_remap;
    r->leftover_in_remap = false;

    if (!have_leftover && (!r->map_required || input->length <= 0))
        return input;
    else if (input->length <= 0)
        return &r->remap_buf;

    in_n_samples = (unsigned) (input->length / r->w_sz);
    in_n_frames = out_n_frames = in_n_samples / r->i_ss.channels;

    if (have_leftover) {
        leftover_length = r->remap_buf.length;
        out_n_frames += leftover_length / r->w_fz;
    }

    out_n_samples = out_n_frames * r->o_ss.channels;
    fit_buf(r, &r->remap_buf, out_n_samples * r->w_sz, &r->remap_buf_size, leftover_length);

    src = pa_memblock_acquire_chunk(input);
    dst = (uint8_t *) pa_memblock_acquire(r->remap_buf.memblock) + leftover_length;

    if (r->map_required) {
        pa_remap_t *remap = &r->remap;

        pa_assert(remap->do_remap);
        remap->do_remap(remap, dst, src, in_n_frames);

    } else
        memcpy(dst, src, input->length);

    pa_memblock_release(input->memblock);
    pa_memblock_release(r->remap_buf.memblock);

    return &r->remap_buf;
}

static void save_leftover(pa_resampler *r, void *buf, size_t len) {
    void *dst;

    pa_assert(r);
    pa_assert(buf);
    pa_assert(len > 0);

    /* Store the leftover data. */
    fit_buf(r, r->leftover_buf, len, r->leftover_buf_size, 0);
    *r->have_leftover = true;

    dst = pa_memblock_acquire(r->leftover_buf->memblock);
    memmove(dst, buf, len);
    pa_memblock_release(r->leftover_buf->memblock);
}

static pa_memchunk *resample(pa_resampler *r, pa_memchunk *input) {
    unsigned in_n_frames, out_n_frames, leftover_n_frames;

    pa_assert(r);
    pa_assert(input);

    /* Resample the data and place the result in resample_buf. */

    if (!r->impl.resample || !input->length)
        return input;

    in_n_frames = (unsigned) (input->length / r->w_fz);

    out_n_frames = ((in_n_frames*r->o_ss.rate)/r->i_ss.rate)+EXTRA_FRAMES;
    fit_buf(r, &r->resample_buf, r->w_fz * out_n_frames, &r->resample_buf_size, 0);

    leftover_n_frames = r->impl.resample(r, input, in_n_frames, &r->resample_buf, &out_n_frames);

    if (leftover_n_frames > 0) {
        void *leftover_data = (uint8_t *) pa_memblock_acquire_chunk(input) + (in_n_frames - leftover_n_frames) * r->w_fz;
        save_leftover(r, leftover_data, leftover_n_frames * r->w_fz);
        pa_memblock_release(input->memblock);
    }

    r->resample_buf.length = out_n_frames * r->w_fz;

    return &r->resample_buf;
}

static pa_memchunk *convert_from_work_format(pa_resampler *r, pa_memchunk *input) {
    unsigned n_samples, n_frames;
    void *src, *dst;

    pa_assert(r);
    pa_assert(input);

    /* Convert the data into the correct sample type and place the result in
     * from_work_format_buf. */

    if (!r->from_work_format_func || !input->length)
        return input;

    n_samples = (unsigned) (input->length / r->w_sz);
    n_frames = n_samples / r->o_ss.channels;
    fit_buf(r, &r->from_work_format_buf, r->o_fz * n_frames, &r->from_work_format_buf_size, 0);

    src = pa_memblock_acquire_chunk(input);
    dst = pa_memblock_acquire(r->from_work_format_buf.memblock);
    r->from_work_format_func(n_samples, src, dst);
    pa_memblock_release(input->memblock);
    pa_memblock_release(r->from_work_format_buf.memblock);

    return &r->from_work_format_buf;
}

void pa_resampler_run(pa_resampler *r, const pa_memchunk *in, pa_memchunk *out) {
    pa_memchunk *buf;

    pa_assert(r);
    pa_assert(in);
    pa_assert(out);
    pa_assert(in->length);
    pa_assert(in->memblock);
    pa_assert(in->length % r->i_fz == 0);

    buf = (pa_memchunk*) in;
    buf = convert_to_work_format(r, buf);

    /* Try to save resampling effort: if we have more output channels than
     * input channels, do resampling first, then remapping. */
    if (r->o_ss.channels <= r->i_ss.channels) {
        buf = remap_channels(r, buf);
        buf = resample(r, buf);
    } else {
        buf = resample(r, buf);
        buf = remap_channels(r, buf);
    }

    if (r->lfe_filter)
        buf = pa_lfe_filter_process(r->lfe_filter, buf);

    if (buf->length) {
        buf = convert_from_work_format(r, buf);
        *out = *buf;

        if (buf == in)
            pa_memblock_ref(buf->memblock);
        else
            pa_memchunk_reset(buf);
    } else
        pa_memchunk_reset(out);
}

#ifdef HAVE_PALM_RESAMPLER
/*** Palm Sample Rate Conversion implementation ***/
static unsigned palm_resample(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {

    pa_assert(r);
    palm_resampler *pr = r->palm.state;
    unsigned int c, i, channels = r->work_channels;
    int out_frames = *out_n_frames, stages = pr->stages;
    int16_t *in, *out;

    /* Acquire a block of memory for input and output buffer. This is
       provided by pulseaudio and a release must be done for every acquire */
    in  = (int16_t *)((uint8_t *)pa_memblock_acquire(input->memblock) + input->index);
    out = (int16_t *)((uint8_t *)pa_memblock_acquire(output->memblock) + output->index);

    for (c = 0; c < channels; c++, in++, out++) {
        int16_t *x, *y;
        pa_memblock *x_memblock, *y_memblock;

        /* Temporary buffers for processing data */
        x_memblock = pa_memblock_new(r->mempool, in_n_frames * sizeof(int16_t));
        x = (int16_t *)((uint8_t *)pa_memblock_acquire(x_memblock));
        y_memblock = pa_memblock_new(r->mempool, out_frames * sizeof(int16_t));
        y = (int16_t *)((uint8_t *)pa_memblock_acquire(y_memblock));

        /* un-interleave data from input buffer*/
        for (i = 0; i < in_n_frames; i++) {
            x[i] = in[i*channels];
        }

        palm_polyphase(x, y, in_n_frames, out_n_frames, pr, c, 0);

        if (stages == 2) {
            memcpy(x, y, *out_n_frames << 1);
            palm_polyphase(x, y, *out_n_frames, out_n_frames, pr, c, 1);
        }

        /* interleave data to output buffer */
        for (i = 0; i < *out_n_frames; i++) {
            out[i*channels] = *y++;
        }

        pa_memblock_release(x_memblock);
        pa_memblock_unref(x_memblock);
        pa_memblock_release(y_memblock);
        pa_memblock_unref(y_memblock);
    }

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);
    return 0;
}

static void palm_update_rates(pa_resampler *r) {
    pa_assert(r);
    pa_assert(r->palm.state);

    int i, j;
    int difference;
    palm_resampler *pr;

    pr = r->palm.state;
    pr->channels = r->o_ss.channels;
    difference = (int)(r->o_ss.rate) - (int)(r->i_ss.rate);

    switch (difference) {
    case -48000:    /* 96 kHz -> 48 kHz */
        set_palm_resampler(pr, 1, 1, 2, 24, poly_fixed_2_1_24, 0, 0, 0, NULL);
        break;
    case -40200:    /* 88.2 kHz -> 48 kHz */
        set_palm_resampler(pr, 2, 49, 160, 24, poly_fixed_160_147_24, 1, 6, 24, poly_fixed_6_1_24);
        break;
    case 3900:      /* 44.1 kHz -> 48 kHz */
        set_palm_resampler(pr, 1, 160, 147, 24, poly_fixed_160_147_24, 0, 0, 0, NULL);
        break;
    case 16000:     /* 32 kHz -> 48 kHz */
        set_palm_resampler(pr, 1, 3, 2, 24, poly_fixed_3_1_24, 0, 0, 0, NULL);
        break;
    case 24000:     /* 24 kHz -> 48 kHz */
        set_palm_resampler(pr, 1, 2, 1, 24, poly_fixed_2_1_24, 0, 0, 0, NULL);
        break;
    case 25950:     /* 22.05 kHz -> 48 kHz */
        set_palm_resampler(pr, 2, 2, 1, 24, poly_fixed_2_1_24, 160, 147, 24, poly_fixed_160_147_24);
        break;
    case 32000:     /* 16 kHz -> 48 kHz */
        set_palm_resampler(pr, 1, 3, 1, 24, poly_fixed_3_1_24, 0, 0, 0, NULL);
        break;
    case 36000:     /* 12 kHz -> 48 kHz */
        set_palm_resampler(pr, 1, 4, 1, 24, poly_fixed_4_1_24, 0, 0, 0, NULL);
        break;
    case 36975:     /* 11.025 kHz -> 48 kHz */
        set_palm_resampler(pr, 2, 4, 3, 24, poly_fixed_4_1_24, 160, 49, 24, poly_fixed_160_147_24);
        break;
    case 40000:     /* 8 kHz -> 48 kHz */
        set_palm_resampler(pr, 1, 6, 1, 24, poly_fixed_6_1_24, 0, 0, 0, NULL);
        break;
    case -51900:    /* 96 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 2, 147, 160, 28, poly_fixed_147_160_28, 1, 2, 24, poly_fixed_2_1_24);
        break;
    case -44100:    /* 88.2 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 1, 1, 2, 24, poly_fixed_2_1_24, 0, 0, 0, NULL);
        break;
    case -3900:     /* 48 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 1, 147, 160, 28, poly_fixed_147_160_28, 0, 0, 0, NULL);
        break;
    case 12100:     /* 32 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 2, 3, 2, 24, poly_fixed_3_1_24, 147, 160, 28, poly_fixed_147_160_28);
        break;
    case 20100:     /* 24 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 1, 147, 80, 24, poly_fixed_147_80_24, 0, 0, 0, NULL);
        break;
    case 22050:     /* 22.05 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 1, 2, 1, 24, poly_fixed_2_1_24, 0, 0, 0, NULL);
        break;
    case 28100:     /* 16 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 2, 3, 2, 24, poly_fixed_3_1_24, 147, 80, 24, poly_fixed_147_80_24);
        break;
    case 32100:     /* 12 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 1, 147, 40, 24, poly_fixed_147_80_24, 0, 0, 0, NULL);
        break;
    case 33075:     /* 11.025 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 2, 2, 1, 24, poly_fixed_2_1_24, 2, 1, 24, poly_fixed_2_1_24);
        break;
    case 36100:     /* 8 kHz -> 44.1 kHz */
        set_palm_resampler(pr, 2, 3, 2, 24, poly_fixed_3_1_24, 147, 40, 24, poly_fixed_147_80_24);
        break;
    default:
        pa_log("sample rate not supported!");
        break;
    }

    for (i = 0; i < pr->channels; i++) {
        for (j = 0; j < pr->stages; j++) {
            pr->poly->phase[j][i] = 0;
            pr->poly->states[j][i] = (int16_t *)pa_xrealloc(pr->poly->states[j][i], sizeof(int16_t) * pr->poly->taps[j]);
        }
    }
}

static void palm_free(pa_resampler *r) {
    pa_assert(r);

    int i, j;
    palm_resampler *pr = r->palm.state;

    if (!(r->palm.state))
        return;
    else {
        for (i = 0; i < pr->channels; i++) {
            for (j = 0; j < pr->stages; j++) {
                if (pr->poly->states[j][i])
                    pa_xfree(pr->poly->states[j][i]);
            }
        }

        for (j = 0; j < pr->stages; j++) {
            if (pr->poly->states[j])
                pa_xfree(pr->poly->states[j]);
            if (pr->poly->phase[j])
                pa_xfree(pr->poly->phase[j]);
        }

        if (pr->poly)
            pa_xfree(pr->poly);

        pa_xfree(r->palm.state);
    }
}

static void palm_reset(pa_resampler *r) {
    pa_assert(r);
    int i, j;
    palm_resampler *pr = r->palm.state;

    pa_log_info("resetting palm resampler");
    /* reset data by clearing filter states and phase numbers */

    for (i = 0; i < pr->channels; i++) {
        for (j = 0; j < pr->stages; j++) {
            pr->poly->phase[j][i] = 0;
            memset(pr->poly->states[j][i], 0, sizeof(int16_t)*pr->poly->taps[j]);
        }
    }
}

static int palm_init(pa_resampler *r) {
    pa_assert(r);
    int i, j;
    int difference;
    palm_resampler *pr;

    pa_log_info("initializing palm resampler");

    if (r->method == PA_RESAMPLER_PALM) {
        r->impl.resample = palm_resample;
        r->impl.free = palm_free;
        r->impl.update_rates = palm_update_rates;
        r->impl.reset = palm_reset;
    }

    r->palm.state = (palm_resampler *)(pa_xmalloc(sizeof(palm_resampler)));

    pr = r->palm.state;

    if (r->palm.state != NULL) {

        difference = (int)(r->o_ss.rate) - (int)(r->i_ss.rate);

        pr->channels = r->o_ss.channels;

        pr->poly = (palm_filter *)pa_xmalloc(sizeof(palm_filter));

        switch (difference) {
        case -48000:    /* 96 kHz -> 48 kHz */
            set_palm_resampler(pr, 1, 1, 2, 24, poly_fixed_2_1_24, 0, 0, 0, NULL);
            break;
        case -40200:    /* 88.2 kHz -> 48 kHz */
            set_palm_resampler(pr, 2, 49, 160, 24, poly_fixed_160_147_24, 1, 6, 24, poly_fixed_6_1_24);
            break;
        case 3900:      /* 44.1 kHz -> 48 kHz */
            set_palm_resampler(pr, 1, 160, 147, 24, poly_fixed_160_147_24, 0, 0, 0, NULL);
            break;
        case 16000:     /* 32 kHz -> 48 kHz */
            set_palm_resampler(pr, 1, 3, 2, 24, poly_fixed_3_1_24, 0, 0, 0, NULL);
            break;
        case 24000:     /* 24 kHz -> 48 kHz */
            set_palm_resampler(pr, 1, 2, 1, 24, poly_fixed_2_1_24, 0, 0, 0, NULL);
            break;
        case 25950:     /* 22.05 kHz -> 48 kHz */
            set_palm_resampler(pr, 2, 2, 1, 24, poly_fixed_2_1_24, 160, 147, 24, poly_fixed_160_147_24);
            break;
        case 32000:     /* 16 kHz -> 48 kHz */
            set_palm_resampler(pr, 1, 3, 1, 24, poly_fixed_3_1_24, 0, 0, 0, NULL);
            break;
        case 36000:     /* 12 kHz -> 48 kHz */
            set_palm_resampler(pr, 1, 4, 1, 24, poly_fixed_4_1_24, 0, 0, 0, NULL);
            break;
        case 36975:     /* 11.025 kHz -> 48 kHz */
            set_palm_resampler(pr, 2, 4, 3, 24, poly_fixed_4_1_24, 160, 49, 24, poly_fixed_160_147_24);
            break;
        case 40000:     /* 8 kHz -> 48 kHz */
            set_palm_resampler(pr, 1, 6, 1, 24, poly_fixed_6_1_24, 0, 0, 0, NULL);
            break;
        case -51900:    /* 96 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 2, 147, 160, 28, poly_fixed_147_160_28, 1, 2, 24, poly_fixed_2_1_24);
            break;
        case -44100:    /* 88.2 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 1, 1, 2, 24, poly_fixed_2_1_24, 0, 0, 0, NULL);
            break;
        case -3900:     /* 48 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 1, 147, 160, 28, poly_fixed_147_160_28, 0, 0, 0, NULL);
            break;
        case 12100:     /* 32 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 2, 3, 2, 24, poly_fixed_3_1_24, 147, 160, 28, poly_fixed_147_160_28);
            break;
        case 20100:     /* 24 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 1, 147, 80, 24, poly_fixed_147_80_24, 0, 0, 0, NULL);
            break;
        case 22050:     /* 22.05 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 1, 2, 1, 24, poly_fixed_2_1_24, 0, 0, 0, NULL);
            break;
        case 28100:     /* 16 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 2, 3, 2, 24, poly_fixed_3_1_24, 147, 80, 24, poly_fixed_147_80_24);
            break;
        case 32100:     /* 12 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 1, 147, 40, 24, poly_fixed_147_80_24, 0, 0, 0, NULL);
            break;
        case 33075:     /* 11.025 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 2, 2, 1, 24, poly_fixed_2_1_24, 2, 1, 24, poly_fixed_2_1_24);
            break;
        case 36100:     /* 8 kHz -> 44.1 kHz */
            set_palm_resampler(pr, 2, 3, 2, 24, poly_fixed_3_1_24, 147, 40, 24, poly_fixed_147_80_24);
            break;
        default:
            palm_free(r);
            pa_log("sample rate not supported!");
            return -1;
            break;
        }

        for (j = 0; j < pr->stages; j++) {
            pr->poly->states[j] = (int16_t **)pa_xmalloc(pr->channels*sizeof(int16_t *));
            assert(pr->poly->states[j]);
            pr->poly->phase[j]  = (int16_t *)pa_xmalloc(pr->channels*sizeof(int16_t));
            assert(pr->poly->phase[j]);
        }

        for (i = 0; i < pr->channels; i++) {
            for (j = 0; j < pr->stages; j++) {
                /* pa_xmalloc0 (calloc) used to zero data, this prevents initial pops/clicks */
                pr->poly->states[j][i] = (int16_t *)pa_xmalloc0(pr->poly->taps[j]*sizeof(int16_t));
                assert(pr->poly->states[j][i]);
                pr->poly->phase[j][i] = 0;
            }
        }
    } else {
        palm_free(r);
        return -1;
    }

    pa_log_info("finished initializing palm resampler");

    return 0;
}
#endif

/*** libsamplerate based implementation ***/

#ifdef HAVE_LIBSAMPLERATE
static unsigned libsamplerate_resample(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    SRC_DATA data;
    SRC_STATE *state;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    state = r->impl.data;
    memset(&data, 0, sizeof(data));

    data.data_in = pa_memblock_acquire_chunk(input);
    data.input_frames = (long int) in_n_frames;

    data.data_out = pa_memblock_acquire_chunk(output);
    data.output_frames = (long int) *out_n_frames;

    data.src_ratio = (double) r->o_ss.rate / r->i_ss.rate;
    data.end_of_input = 0;

    pa_assert_se(src_process(state, &data) == 0);

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    *out_n_frames = (unsigned) data.output_frames_gen;

    return in_n_frames - data.input_frames_used;
}

static void libsamplerate_update_rates(pa_resampler *r) {
    SRC_STATE *state;
    pa_assert(r);

    state = r->impl.data;
    pa_assert_se(src_set_ratio(state, (double) r->o_ss.rate / r->i_ss.rate) == 0);
}

static void libsamplerate_reset(pa_resampler *r) {
    SRC_STATE *state;
    pa_assert(r);

    state = r->impl.data;
    pa_assert_se(src_reset(state) == 0);
}

static void libsamplerate_free(pa_resampler *r) {
    SRC_STATE *state;
    pa_assert(r);

    state = r->impl.data;
    if (state)
        src_delete(state);
}

static int libsamplerate_init(pa_resampler *r) {
    int err;
    SRC_STATE *state;

    pa_assert(r);

    if (!(state = src_new(r->method, r->work_channels, &err)))
        return -1;

    r->impl.free = libsamplerate_free;
    r->impl.update_rates = libsamplerate_update_rates;
    r->impl.resample = libsamplerate_resample;
    r->impl.reset = libsamplerate_reset;
    r->impl.data = state;

    return 0;
}
#endif

#ifdef HAVE_SPEEX
/*** speex based implementation ***/

static unsigned speex_resample_float(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    float *in, *out;
    uint32_t inf = in_n_frames, outf = *out_n_frames;
    SpeexResamplerState *state;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    state = r->impl.data;

    in = pa_memblock_acquire_chunk(input);
    out = pa_memblock_acquire_chunk(output);

    pa_assert_se(speex_resampler_process_interleaved_float(state, in, &inf, out, &outf) == 0);

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    pa_assert(inf == in_n_frames);
    *out_n_frames = outf;

    return 0;
}

static unsigned speex_resample_int(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    int16_t *in, *out;
    uint32_t inf = in_n_frames, outf = *out_n_frames;
    SpeexResamplerState *state;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    state = r->impl.data;

    in = pa_memblock_acquire_chunk(input);
    out = pa_memblock_acquire_chunk(output);

    pa_assert_se(speex_resampler_process_interleaved_int(state, in, &inf, out, &outf) == 0);

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    pa_assert(inf == in_n_frames);
    *out_n_frames = outf;

    return 0;
}

static void speex_update_rates(pa_resampler *r) {
    SpeexResamplerState *state;
    pa_assert(r);

    state = r->impl.data;

    pa_assert_se(speex_resampler_set_rate(state, r->i_ss.rate, r->o_ss.rate) == 0);
}

static void speex_reset(pa_resampler *r) {
    SpeexResamplerState *state;
    pa_assert(r);

    state = r->impl.data;

    pa_assert_se(speex_resampler_reset_mem(state) == 0);
}

static void speex_free(pa_resampler *r) {
    SpeexResamplerState *state;
    pa_assert(r);

    state = r->impl.data;
    if (!state)
        return;

    speex_resampler_destroy(state);
}

static int speex_init(pa_resampler *r) {
    int q, err;
    SpeexResamplerState *state;

    pa_assert(r);

    r->impl.free = speex_free;
    r->impl.update_rates = speex_update_rates;
    r->impl.reset = speex_reset;

    if (r->method >= PA_RESAMPLER_SPEEX_FIXED_BASE && r->method <= PA_RESAMPLER_SPEEX_FIXED_MAX) {

        q = r->method - PA_RESAMPLER_SPEEX_FIXED_BASE;
        r->impl.resample = speex_resample_int;

    } else {
        pa_assert(r->method >= PA_RESAMPLER_SPEEX_FLOAT_BASE && r->method <= PA_RESAMPLER_SPEEX_FLOAT_MAX);

        q = r->method - PA_RESAMPLER_SPEEX_FLOAT_BASE;
        r->impl.resample = speex_resample_float;
    }

    pa_log_info("Choosing speex quality setting %i.", q);

    if (!(state = speex_resampler_init(r->work_channels, r->i_ss.rate, r->o_ss.rate, q, &err)))
        return -1;

    r->impl.data = state;

    return 0;
}
#endif

/* Trivial implementation */

static unsigned trivial_resample(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    unsigned i_index, o_index;
    void *src, *dst;
    struct trivial_data *trivial_data;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    trivial_data = r->impl.data;

    src = pa_memblock_acquire_chunk(input);
    dst = pa_memblock_acquire_chunk(output);

    for (o_index = 0;; o_index++, trivial_data->o_counter++) {
        i_index = ((uint64_t) trivial_data->o_counter * r->i_ss.rate) / r->o_ss.rate;
        i_index = i_index > trivial_data->i_counter ? i_index - trivial_data->i_counter : 0;

        if (i_index >= in_n_frames)
            break;

        pa_assert_fp(o_index * r->w_fz < pa_memblock_get_length(output->memblock));

        memcpy((uint8_t*) dst + r->w_fz * o_index, (uint8_t*) src + r->w_fz * i_index, (int) r->w_fz);
    }

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    *out_n_frames = o_index;

    trivial_data->i_counter += in_n_frames;

    /* Normalize counters */
    while (trivial_data->i_counter >= r->i_ss.rate) {
        pa_assert(trivial_data->o_counter >= r->o_ss.rate);

        trivial_data->i_counter -= r->i_ss.rate;
        trivial_data->o_counter -= r->o_ss.rate;
    }

    return 0;
}

static void trivial_update_rates_or_reset(pa_resampler *r) {
    struct trivial_data *trivial_data;
    pa_assert(r);

    trivial_data = r->impl.data;

    trivial_data->i_counter = 0;
    trivial_data->o_counter = 0;
}

static int trivial_init(pa_resampler*r) {
    struct trivial_data *trivial_data;
    pa_assert(r);

    trivial_data = pa_xnew0(struct trivial_data, 1);

    r->impl.resample = trivial_resample;
    r->impl.update_rates = trivial_update_rates_or_reset;
    r->impl.reset = trivial_update_rates_or_reset;
    r->impl.data = trivial_data;

    return 0;
}

/* Peak finder implementation */

static unsigned peaks_resample(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    unsigned c, o_index = 0;
    unsigned i, i_end = 0;
    void *src, *dst;
    struct peaks_data *peaks_data;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    peaks_data = r->impl.data;
    src = pa_memblock_acquire_chunk(input);
    dst = pa_memblock_acquire_chunk(output);

    i = ((uint64_t) peaks_data->o_counter * r->i_ss.rate) / r->o_ss.rate;
    i = i > peaks_data->i_counter ? i - peaks_data->i_counter : 0;

    while (i_end < in_n_frames) {
        i_end = ((uint64_t) (peaks_data->o_counter + 1) * r->i_ss.rate) / r->o_ss.rate;
        i_end = i_end > peaks_data->i_counter ? i_end - peaks_data->i_counter : 0;

        pa_assert_fp(o_index * r->w_fz < pa_memblock_get_length(output->memblock));

        /* 1ch float is treated separately, because that is the common case */
        if (r->work_channels == 1 && r->work_format == PA_SAMPLE_FLOAT32NE) {
            float *s = (float*) src + i;
            float *d = (float*) dst + o_index;

            for (; i < i_end && i < in_n_frames; i++) {
                float n = fabsf(*s++);

                if (n > peaks_data->max_f[0])
                    peaks_data->max_f[0] = n;
            }

            if (i == i_end) {
                *d = peaks_data->max_f[0];
                peaks_data->max_f[0] = 0;
                o_index++, peaks_data->o_counter++;
            }
        } else if (r->work_format == PA_SAMPLE_S16NE) {
            int16_t *s = (int16_t*) src + r->work_channels * i;
            int16_t *d = (int16_t*) dst + r->work_channels * o_index;

            for (; i < i_end && i < in_n_frames; i++)
                for (c = 0; c < r->work_channels; c++) {
                    int16_t n = abs(*s++);

                    if (n > peaks_data->max_i[c])
                        peaks_data->max_i[c] = n;
                }

            if (i == i_end) {
                for (c = 0; c < r->work_channels; c++, d++) {
                    *d = peaks_data->max_i[c];
                    peaks_data->max_i[c] = 0;
                }
                o_index++, peaks_data->o_counter++;
            }
        } else {
            float *s = (float*) src + r->work_channels * i;
            float *d = (float*) dst + r->work_channels * o_index;

            for (; i < i_end && i < in_n_frames; i++)
                for (c = 0; c < r->work_channels; c++) {
                    float n = fabsf(*s++);

                    if (n > peaks_data->max_f[c])
                        peaks_data->max_f[c] = n;
                }

            if (i == i_end) {
                for (c = 0; c < r->work_channels; c++, d++) {
                    *d = peaks_data->max_f[c];
                    peaks_data->max_f[c] = 0;
                }
                o_index++, peaks_data->o_counter++;
            }
        }
    }

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    *out_n_frames = o_index;

    peaks_data->i_counter += in_n_frames;

    /* Normalize counters */
    while (peaks_data->i_counter >= r->i_ss.rate) {
        pa_assert(peaks_data->o_counter >= r->o_ss.rate);

        peaks_data->i_counter -= r->i_ss.rate;
        peaks_data->o_counter -= r->o_ss.rate;
    }

    return 0;
}

static void peaks_update_rates_or_reset(pa_resampler *r) {
    struct peaks_data *peaks_data;
    pa_assert(r);

    peaks_data = r->impl.data;

    peaks_data->i_counter = 0;
    peaks_data->o_counter = 0;
}

static int peaks_init(pa_resampler*r) {
    struct peaks_data *peaks_data;
    pa_assert(r);
    pa_assert(r->i_ss.rate >= r->o_ss.rate);
    pa_assert(r->work_format == PA_SAMPLE_S16NE || r->work_format == PA_SAMPLE_FLOAT32NE);

    peaks_data = pa_xnew0(struct peaks_data, 1);

    r->impl.resample = peaks_resample;
    r->impl.update_rates = peaks_update_rates_or_reset;
    r->impl.reset = peaks_update_rates_or_reset;
    r->impl.data = peaks_data;

    return 0;
}

/*** ffmpeg based implementation ***/

static unsigned ffmpeg_resample(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    unsigned used_frames = 0, c;
    int previous_consumed_frames = -1;
    struct ffmpeg_data *ffmpeg_data;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    ffmpeg_data = r->impl.data;

    for (c = 0; c < r->work_channels; c++) {
        unsigned u;
        pa_memblock *b, *w;
        int16_t *p, *t, *k, *q, *s;
        int consumed_frames;

        /* Allocate a new block */
        b = pa_memblock_new(r->mempool, in_n_frames * sizeof(int16_t));
        p = pa_memblock_acquire(b);

        /* Now copy the input data, splitting up channels */
        t = (int16_t*) pa_memblock_acquire_chunk(input) + c;
        k = p;
        for (u = 0; u < in_n_frames; u++) {
            *k = *t;
            t += r->work_channels;
            k ++;
        }
        pa_memblock_release(input->memblock);

        /* Allocate buffer for the result */
        w = pa_memblock_new(r->mempool, *out_n_frames * sizeof(int16_t));
        q = pa_memblock_acquire(w);

        /* Now, resample */
        used_frames = (unsigned) av_resample(ffmpeg_data->state,
                                             q, p,
                                             &consumed_frames,
                                             (int) in_n_frames, (int) *out_n_frames,
                                             c >= (unsigned) (r->work_channels-1));

        pa_memblock_release(b);
        pa_memblock_unref(b);

        pa_assert(consumed_frames <= (int) in_n_frames);
        pa_assert(previous_consumed_frames == -1 || consumed_frames == previous_consumed_frames);
        previous_consumed_frames = consumed_frames;

        /* And place the results in the output buffer */
        s = (int16_t *) pa_memblock_acquire_chunk(output) + c;
        for (u = 0; u < used_frames; u++) {
            *s = *q;
            q++;
            s += r->work_channels;
        }
        pa_memblock_release(output->memblock);
        pa_memblock_release(w);
        pa_memblock_unref(w);
    }

    *out_n_frames = used_frames;

    return in_n_frames - previous_consumed_frames;
}

static void ffmpeg_free(pa_resampler *r) {
    struct ffmpeg_data *ffmpeg_data;

    pa_assert(r);

    ffmpeg_data = r->impl.data;
    if (ffmpeg_data->state)
        av_resample_close(ffmpeg_data->state);

    pa_xfree(ffmpeg_data);
}

static int ffmpeg_init(pa_resampler *r) {
    struct ffmpeg_data *ffmpeg_data;

    pa_assert(r);

    ffmpeg_data = pa_xnew(struct ffmpeg_data, 1);

    /* We could probably implement different quality levels by
     * adjusting the filter parameters here. However, ffmpeg
     * internally only uses these hardcoded values, so let's use them
     * here for now as well until ffmpeg makes this configurable. */

    if (!(ffmpeg_data->state = av_resample_init((int) r->o_ss.rate, (int) r->i_ss.rate, 16, 10, 0, 0.8)))
        return -1;

    r->impl.free = ffmpeg_free;
    r->impl.resample = ffmpeg_resample;
    r->impl.data = (void *) ffmpeg_data;

    return 0;
}

/*** copy (noop) implementation ***/

static int copy_init(pa_resampler *r) {
    pa_assert(r);

    pa_assert(r->o_ss.rate == r->i_ss.rate);

    return 0;
}
