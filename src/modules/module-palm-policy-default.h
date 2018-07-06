/***
  This file is part of PulseAudio.
  Copyright (c) 2002-2018 LG Electronics, Inc.
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
#define SIZE_MESG_TO_PULSE  50

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

/* Alsa sinks.  Virtual devices will be remapped to these
 * "actual" alsa devices.  One per alsa sink.
 * eMainSink, eA2DPSink & eWirelessSink must be defined for clients use.
 * They each represent a logical output mapped to a physical sink.
 */
#if defined(__i386__)

enum EPhysicalSink {
    ePhysicalSink_hda = 0,
    ePhysicalSink_usb,
    ePhysicalSink_rtp,
    ePhysicalSink_Count,    /* MUST be the last individual element. Elements below are aliases for one of the above. */

    // define logical outputs
    eMainSink = ePhysicalSink_hda,
    eA2DPSink = ePhysicalSink_usb,
    eRtpsink = ePhysicalSink_rtp,
    eAuxSink = ePhysicalSink_hda,
};

enum EPhysicalSource {
    ePhysicalSource_usb = 0,
    ePhysicalSource_Count,    /* MUST be the last individual element. Elements below are aliases for one of the above. */

    // define logical inputs
    eMainSource = ePhysicalSource_usb,
    eAuxSource = ePhysicalSource_usb
};

#else

enum EPhysicalSink {
    ePhysicalSink_pcm_output = 0,
    ePhysicalSink_a2dp,             /* virtual sink set up as a monitor source for a2dp */
    ePhysicalSink_rtp,
    ePhysicalSink_Count,    /* MUST be the last individual element. Elements below are aliases for one of the above. */

    // define logical outputs
    eMainSink = ePhysicalSink_pcm_output,
    eA2DPSink = ePhysicalSink_a2dp,
    eRtpsink = ePhysicalSink_rtp,
    eAuxSink = ePhysicalSink_pcm_output
};

enum EPhysicalSource {
    ePhysicalSource_pcm_input = 0,
    ePhysicalSource_Count,    /* MUST be the last individual element. Elements below are aliases for one of the above. */

    // define logical inputs
    eMainSource = ePhysicalSource_pcm_input,
    eAuxSource = ePhysicalSource_pcm_input
};
#endif

enum EVirtualSink {
    ealerts = 0,
    enotifications,
    efeedback,
    eringtones,
    ecallertone,
    emedia,
    eflash,
    enavigation,
    evoicedial,
    evvm,
    evoip,
    edefaultapp,
    eeffects,
    eDTMF,
    ecalendar,
    ealarm,
    etimer,
    etts,
    endk,
    evoicerecognition,

    eVirtualSink_Count,   /* MUST be the last element this is used to
                            * define the size of the currentmappingtable
                            * array in our userdata
                            */
    eumiMedia,
    eumiAudio_test,
    eumiAudio_media,
    eumiGame_media,
    eumiMedia_mix,
    eumiSystem_music,
    eumiLivetv,
    eumiAlert,
    eumiNotification,
    eumiText_to_speech,
    eumiPhone,
    eumiGame_directmedia,
    eumiRingtone,
    eumiVoice,
    eumiDefault_app,
    eumiCount,
    eVirtualSink_First = 0,
    eVirtualSink_Last = evoicerecognition,

    eVirtualSink_None = -1,
    eVirtualSink_All = eVirtualSink_Count,
    eumiFirst =  eumiMedia,
    eumiLast = eumiDefault_app,
    eumiAll = eumiCount
};

enum EVirtualSource {
    erecord = 0,
    eqvoice,
    evoiceactivator,
    evoipsource,
    evoicedialsource,
    evoicecallsource,

    eVirtualSource_Count,   /* MUST be the last element this is used to
                           * define the size of the currentmappingtable
                           * array in our userdata
                           */

    eVirtualSource_First = 0,
    eVirtualSource_Last = evoicecallsource,

    eVirtualSource_None = -1,
    eVirtualSource_All = eVirtualSource_Count
};
#endif /* _MODULE_PALM_POLICY_H_ */


