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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/module.h>
#include <pulsecore/shared.h>

#include "module-palm-policy-util.h"

struct pa_palm_policy {
    PA_REFCNT_DECLARE;

    pa_core *core;
    pa_hook hooks[PA_PALM_POLICY_HOOK_MAX];
};

pa_hook* pa_palm_policy_hook(pa_palm_policy *pp, pa_palm_policy_hook_t hook) {
    pa_assert(pp);
    pa_assert(PA_REFCNT_VALUE(pp) > 0);

    return &pp->hooks[hook];
}

pa_palm_policy* pa_palm_policy_get(pa_core *c) {
    pa_palm_policy *pp;
    unsigned i;

    pp = pa_xnew0(pa_palm_policy, 1);
    PA_REFCNT_INIT(pp);
    pp->core = c;

    for (i = 0; i < PA_PALM_POLICY_HOOK_MAX; i++)
        pa_hook_init(&pp->hooks[i], pp);

    pa_shared_set(c, "palm-policy", pp);
    return pp;
}

pa_palm_policy* pa_palm_policy_ref(pa_palm_policy *pp) {
    pa_assert(pp);
    pa_assert(PA_REFCNT_VALUE(pp) > 0);

    PA_REFCNT_INC(pp);

    return pp;
}

void pa_palm_policy_unref(pa_palm_policy *pp) {
    pa_assert(pp);
    pa_assert(PA_REFCNT_VALUE(pp) > 0);

    if (PA_REFCNT_DEC(pp) > 0)
        return;

    pa_shared_remove(pp->core, "palm-policy");
    pa_xfree(pp);
}

void pa_palm_policy_hook_fire_set_parameters(pa_palm_policy *pp, pa_palm_policy_set_param_data_t *spd)
{
    pa_assert(pp);
    pa_assert(spd);

    pa_hook_fire(&pp->hooks[PA_PALM_POLICY_HOOK_SET_PARAMETERS], spd);
}
