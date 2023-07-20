/***
  This file is part of PulseAudio.
  Copyright (c) 2002-2023 LG Electronics, Inc.
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

#ifndef PALM_POLICY_UTIL_H
#define PALM_POLICY_UTIL_H

#include "module-palm-policy-default.h"

#define PALM_POLICY_SET_PARAM_DATA_SIZE SIZE_MESG_TO_PULSE

typedef struct pa_palm_policy pa_palm_policy;

typedef struct pa_palm_policy_set_param_data {
    char keyValuePairs[PALM_POLICY_SET_PARAM_DATA_SIZE];
} pa_palm_policy_set_param_data_t;

typedef enum pa_palm_policy_hook {
    PA_PALM_POLICY_HOOK_SET_PARAMETERS,          /* Call data: pa_palm_policy_set_param_data_t */
    PA_PALM_POLICY_HOOK_MAX
} pa_palm_policy_hook_t;

pa_hook* pa_palm_policy_hook(pa_palm_policy *pp, pa_palm_policy_hook_t hook);
pa_palm_policy* pa_palm_policy_get(pa_core *core);
pa_palm_policy* pa_palm_policy_ref(pa_palm_policy *pp);
void pa_palm_policy_unref(pa_palm_policy *pp);

void pa_palm_policy_hook_fire_set_parameters(pa_palm_policy *pp, pa_palm_policy_set_param_data_t *spd);

#endif
