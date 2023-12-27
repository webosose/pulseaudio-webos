/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef MODULE_POSTPROCESS_SINK_H
#define MODULE_POSTPROCESS_SINK_H

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

#include "module-palm-policy-util.h"
#include "AudioPostProcess.h"

#include <pulsecore/shared.h>
#include <string.h>
#include <stdlib.h>

struct app_userdata {
    pa_module *module;

    /* FIXME: Uncomment this and take "autoloaded" as a modarg if this is a filter */
    /* bool autoloaded; */

    pa_sink *sink;
    pa_sink_input *sink_input;

    pa_memblockq *memblockq;

    bool auto_desc;
    unsigned channels;

    //  module-palm-policy-util hook
    pa_palm_policy *palm_policy;
    pa_hook_slot *palm_policy_set_parameters_slot;

    /* SOLUTION: memory struct for solution */
    AudioPostProcessMemory *mem;
};

#endif // MODULE_POSTPROCESS_SINK_H