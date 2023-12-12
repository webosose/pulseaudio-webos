/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef foolgepreprocess
#define foolgepreprocess

#include <config.h>

#include <pulse/cdecl.h>

PA_C_DECL_BEGIN
#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulsecore/core.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>

#include <pulse/timeval.h>
#include <ltdl.h>
PA_C_DECL_END


struct preprocess_params
{
    unsigned int blocksize; /* in frames */
    pa_sample_spec rec_ss, play_ss, out_ss;
    float *rec_buffer[PA_CHANNELS_MAX], *play_buffer[PA_CHANNELS_MAX], *out_buffer;
    short *s_rec_buf, *s_play_buf, *s_out_buf;

};
typedef struct preprocess_params preprocess_params;
typedef struct pa_preprocess_msg pa_preprocess_msg;
PA_C_DECL_BEGIN
bool lge_preprocess_init(preprocess_params *ec,
                     pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                     pa_sample_spec *play_ss, pa_channel_map *play_map,
                     pa_sample_spec *out_ss, pa_channel_map *out_map,
                     uint32_t *nframes, const char *args) ;
bool lge_preprocess_run(preprocess_params *ec, const uint8_t *rec, const uint8_t *play, uint8_t *out);
bool lge_preprocess_done(preprocess_params *ec);
bool lge_preprocess_setParams (preprocess_params *ec, const char* name, bool enable, void *data);
PA_C_DECL_END
#endif
