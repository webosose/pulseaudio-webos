/***
  This file is part of PulseAudio.
  Copyright (c) 2002-2021 LG Electronics, Inc.
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

#ifndef _MODULE_PALM_POLICY_H_
#define _MODULE_PALM_POLICY_H_

/*
 * data types and enums for pulse module for stream routing.  This contains the
 * types and structures that needs to be shared between the module and other
 * system components such as the audio policy manager.  Also the definitions in
 * here are highly dependent on the definitions in the ALSA and pulseaudio
 * configuration files.
 *
 * It seems likely that this file is best auto generated, along with the config
 * files to avoid errors, this is a to do item.
 *
 * Nick Thompson
 * 5/23/08
 *
 *
 */

#include <stdint.h>
#include <semaphore.h>
#include <sys/stat.h>

// socket message size, audiod -> pulse
#define SIZE_MESG_TO_PULSE  150

// socket message size, pulse -> audiod
#define SIZE_MESG_TO_AUDIOD 50

/* This stuff is highly system dependent, These tables probably need to be
 * built automatically then communicated back to the policy manager.  For
 * now these are hard coded.  If the pulse config in default.pa or the alsa
 * config in asound.rc change this will likely not work.  This needs to be
 * fixed up in the future
 */


/* describes enums for the stream map, these correspond on a one to one basis
 * to the published pulseaudio sinks.  All streams published to apps by
 * pulseaudio need to have a value in this enum.  These values are then
 * tied back to the actual pulseaudio sink in the struct _systemdependantstreammap
 */

#define PALMAUDIO_SOCK_NAME     "palmaudio"
#define _MAX_NAME_LEN 99
#define PALMAUDIO_SOCK_NAME2    "palmaudioo"

#ifdef HAVE_STD_BOOL
typedef bool pa_bool_t;
#else
typedef int pa_bool_t;
#endif

#ifndef FALSE
#define FALSE ((pa_bool_t) 0)
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

enum EVirtualSink {
    ealerts = 0,
    efeedback,
    eringtones,
    emedia,
    edefaultapp,
    eeffects,
    etts,
    evoicerecognition,
    ebtstream,
    ebtcall,
    efm,
    eam,
    ehdradio,
    eradio,
    edefault1,
    etts1,
    edefault2,
    etts2,
    eVirtualSink_Count,   /* MUST be the last element this is used to
                            * define the size of the currentmappingtable
                            * array in our userdata
                            */
    eVirtualSink_First = 0,
    eVirtualSink_Last = etts2,

    eVirtualSink_None = -1,
    eVirtualSink_All = eVirtualSink_Count
};

enum EVirtualSource {
    erecord = 0,
    ebtcallsource,
    ealexa,
    ewebcall,
    evoiceassistance,
    erecord1,
    ealexa1,

    eVirtualSource_Count,   /* MUST be the last element this is used to
                           * define the size of the currentmappingtable
                           * array in our userdata
                           */

    eVirtualSource_First = 0,
    eVirtualSource_Last = ealexa1,

    eVirtualSource_None = -1,
    eVirtualSource_All = eVirtualSource_Count
};
#endif /* _MODULE_PALM_POLICY_H_ */


