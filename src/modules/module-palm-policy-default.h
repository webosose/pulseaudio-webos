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
#include <stdbool.h>

// socket message size, audiod -> pulse
#define SIZE_MESG_TO_PULSE  150

// socket message size, pulse -> audiod
#define SIZE_MESG_TO_AUDIOD 250
#define DEVICE_NAME_LENGTH  50
#define DEVICE_NAME_DETAILS_LENGTH  100
#define SINKNAME 30
#define APP_NAME_LENGTH 100

struct paudiodMsgHdr{
    uint32_t msgType;
    uint8_t msgTmp;             //Old = ' ', New = 0x01  for Supporting Old Way "command param1 param2"
    uint8_t msgVer;             //Message's Version for Future Extension
    uint32_t msgLen;
    uint32_t msgID;             //For Return MSG
}__attribute((packed));


//uint8_t msgType;
#define PAUDIOD_MSGTYPE_ROUTING                 0x0001      // a,d
#define PAUDIOD_MSGTYPE_VOLUME                  0x0002      // b,
#define PAUDIOD_MSGTYPE_DEVICE                  0x0003
#define PAUDIOD_MSGTYPE_MODULE                  0x0004
#define PAUDIOD_MSGTYPE_SETPARAM                0x0005

//TYPE: PAUDIOD_MSGTYPE_ROUTING
#define PAUDIOD_ROUTING_SINKINPUT_MOVE                  0x0010      // 'd'
#define PAUDIOD_ROUTING_SINKINPUT_RANGE                 0x0020      // 'o'
#define PAUDIOD_ROUTING_SINKINPUT_DEFAULT               0x0030      // '2'
#define PAUDIOD_ROUTING_SINKOUTPUT_DEVICE               0x0040      // 'q'
#define PAUDIOD_ROUTING_SOURCEOUTPUT_MOVE               0x1000      // 'e'
#define PAUDIOD_ROUTING_SOURCEOUTPUT_RANGE              0x2000      // 'a'
#define PAUDIOD_ROUTING_SOURCEOUTPUT_DEFAULT            0x3000      // '3'
#define PAUDIOD_ROUTING_SOURCEINPUT_DEVICE              0x4000      // 'y'

struct paRoutingSet {
    uint32_t     Type;
    uint32_t     startID;
    uint32_t     endID;
    uint32_t     id; //added for sinkID/sourceID
    char         device[DEVICE_NAME_LENGTH];
}__attribute((packed));

enum routing {
    eset_source_inputdevice_on_range_reply = 1,
    evirtual_sink_input_move_outputdevice_reply,
    evirtual_source_output_move_inputdevice_reply,
    eset_sink_outputdevice_on_range_reply,
    eset_sink_outputdevice_reply,
    eset_source_inputdevice_reply,
    eset_default_sink_routing_reply,
    eset_default_source_routing_reply,
    eset_default_source_routing_end
};

//TYPE: PAUDIOD_MSGTYPE_VOLUME
#define PAUDIOD_VOLUME_SINK_VOLUME              0x0001  // 'n'
#define PAUDIOD_VOLUME_SINK_MUTE                0x0002  // 'k'
#define PAUDIOD_VOLUME_SINKINPUT_VOLUME         0x0010  // 'b'
#define PAUDIOD_VOLUME_SINKINPUT_MUTE           0x0020  // 'm'
#define PAUDIOD_VOLUME_SINKINPUT_INDEX          0x0030  // '6'
#define PAUDIOD_VOLUME_SINKINPUT_RAMP_VOLUME    0x0040  // 'r'
#define PAUDIOD_VOLUME_SINKINPUT_SET_VOLUME     0x0050  // 'v'
#define PAUDIOD_VOLUME_SOURCE_MUTE              0x0100  // '5'
#define PAUDIOD_VOLUME_SOURCE_MIC_VOLUME        0x0200  // '8'
#define PAUDIOD_VOLUME_SOURCEOUTPUT_VOLUME      0x1000  // 'f'
#define PAUDIOD_VOLUME_SOURCEOUTPUT_MUTE        0x2000  // 'h'

struct paVolumeSet {
    uint32_t    Type;
    uint32_t    id;     //SINKINPUT, SINKID, SOURCEOUTPUT, SOURCEID, SINKINPUTINDEX
    uint32_t    volume;
    uint32_t    table;
    uint32_t    ramp;
    uint32_t    mute;
    uint32_t    parm1; //added
    uint32_t    parm2; //added
    uint32_t    parm3; //added
    uint32_t    index; //added
    char        device[DEVICE_NAME_LENGTH];
}__attribute((packed));

enum volume {
    evirtual_sink_input_set_ramp_volume_reply = eset_default_source_routing_end,
    evirtual_source_input_set_volume_reply,
    evirtual_source_set_mute_reply,
    esink_set_master_mute_reply,
    evirtual_sink_input_set_mute_reply,
    esink_set_master_volume_reply,
    evirtual_sink_input_set_ramp_volume_headset_reply,
    evirtual_sink_input_set_volume_reply,
    evirtual_sink_input_index_set_volume_reply,
    esource_set_master_mute_reply,
    esource_set_master_volume_reply,
    esource_set_master_volume_end
};
//TYPE: PAUDIOD_MSGTYPE_DEVICE
#define PAUDIOD_DEVICE_LOAD_LINEOUT_ALSA_SINK          0x0001      // 'i'
#define PAUDIOD_DEVICE_LOAD_INTERNAL_CARD              0x0002      // 'I'
#define PAUDIOD_DEVICE_LOAD_PLAYBACK_SINK              0x0010      // 'z'
#define PAUDIOD_DEVICE_LOAD_USB_MULTIPLE_DEVICE        0x0020      // 'Z'
#define PAUDIOD_DEVICE_LOAD_CAPTURE_SOURCE             0x1000      //'j'

struct paDeviceSet {
    uint32_t    Type;
    uint32_t    cardNo;
    uint32_t    deviceNo;
    uint8_t     isLoad;
    uint8_t     isMmap;
    uint8_t     isTsched;
    uint32_t    bufSize;
    uint32_t    status; //added
    uint32_t    isOutput; //added
    uint32_t    maxDeviceCnt; //added
    char        device[DEVICE_NAME_LENGTH];
}__attribute((packed));

enum device {
    eload_lineout_alsa_sink_reply = esource_set_master_volume_end,
    einitialise_internal_card_reply,
    edetect_usb_device_reply,
    einit_multiple_usb_device_info_reply,
    edetect_usb_device_end
};
//TYPE: PAUDIOD_MSGTYPE_MODULE
#define PAUDIOD_MODULE_RTP_LOAD                    0x0001      // 'g'
#define PAUDIOD_MODULE_RTP_SET                     0X0002      // 't'
#define PAUDIOD_MODULE_BLUETOOTH_LOAD              0x0003      // 'l'
#define PAUDIOD_MODULE_BLUETOOTH_A2DPSOURCE        0x0004      // 'O'
#define PAUDIOD_MODULE_BLUETOOTH_UNLOAD            0x0005      // 'u'

#define RTP_IP_ADDRESS_STRING_SIZE  28
#define BLUETOOTH_PROFILE_SIZE      5
#define BLUETOOTH_MAC_ADDRESS_SIZE  18

struct paModuleSet {
    uint16_t    Type;
    uint32_t    id;
    uint32_t    a2dpSource;
    uint32_t    info;
    uint32_t    port;
    char        ip[28];
    char        device[DEVICE_NAME_LENGTH];
    char        address[BLUETOOTH_MAC_ADDRESS_SIZE];
    char        profile[BLUETOOTH_PROFILE_SIZE];

}__attribute((packed));

enum module {
    eunload_rtp_module_reply = edetect_usb_device_end,
    eload_Bluetooth_module_reply,
    ea2dpSource_reply,
    eload_unicast_rtp_module_multicast_reply,
    eunload_BlueTooth_module_reply,
    eunload_BlueTooth_module_end
};

//TYPE: PAUDIOD_MSGTYPE_SETPARAM
#define PAUDIOD_SETPARAM_SUSPEND                    0x0001     // 's'
#define PAUDIOD_SETPARAM_UPDATESAMPLERATE           0x0002     // 'x'
#define PAUDIOD_SETPARAM_CLOSE_PLAYBACK             0x0003     // '7'
#define PAUDIOD_MODULE_SPEECH_ENHANCEMENT_LOAD      0x0006      // '4'
#define PAUDIOD_MODULE_GAIN_CONTROL_LOAD            0x0007
#define PAUDIOD_MODULE_BEAMFORMING_LOAD             0x0008
#define PAUDIOD_MODULE_DYNAMIC_COMPRESSOR_LOAD      0x0009

struct paParamSet {
    uint32_t    Type;
    uint32_t    ID;
    uint32_t    param1;
    uint32_t    param2;
    uint32_t    param3;
}__attribute((packed));

enum setParam {
    esink_suspend_request_reply = eunload_BlueTooth_module_end,
    eupdate_sample_spec_reply,
    eclose_playback_by_sink_input_reply,
    eparse_effect_message_reply,
    eparse_effect_message_end
};
//Reply message format
//uint8_t msgType for AudioD reply from PA;
#define PAUDIOD_REPLY_MSGTYPE_ROUTING                 0x1001    //3,i
#define PAUDIOD_REPLY_MSGTYPE_MODULE                  0x1002    //t
#define PAUDIOD_REPLY_MSGTYPE_POLICY                  0x1003    //O,I,o,d,c,k
#define PAUDIOD_REPLY_MSGTYPE_CALLBACK                0x1004    //For callbacks

//TYPE: PAUDIOD_REPLY_MSGTYPE_ROUTING
#define PAUDIOD_REPLY_MSGTYPE_DEVICE_CONNECTION            0x0010      // 'i'
#define PAUDIOD_REPLY_MSGTYPE_DEVICE_REMOVED             0x0020      // '3'

struct paReplyToRoutingSet {
    uint32_t     Type;
    char         device[DEVICE_NAME_LENGTH];
    char         deviceIcon[DEVICE_NAME_LENGTH];
    char         deviceNameDetail[DEVICE_NAME_DETAILS_LENGTH];
    char        isOutput;
}__attribute((packed));

//TYPE:PAUDIOD_REPLY_MSGTYPE_MODULE
#define PAUDIOD_REPLY_MODULE_CAST_RTP                 0x0010      // 't'

struct paReplyToModuleSet {
    uint32_t     Type;
    uint32_t     sink;
    uint32_t     info;
    uint32_t     port;
    char         ip[28];
    char         device[DEVICE_NAME_LENGTH];
}__attribute((packed));

//TYPE:PAUDIOD_REPLY_MSGTYPE_POLICY
#define PAUDIOD_REPLY_POLICY_SINK_CATEGORY           0x0010      // 'O'
#define PAUDIOD_REPLY_POLICY_SOURCE_CATEGORY         0x0020      // 'I'
#define PAUDIOD_REPLY_MSGTYPE_SINK_OPEN              0x0030      // 'o'
#define PAUDIOD_REPLY_MSGTYPE_SOURCE_OPEN            0x0040      // 'd'
#define PAUDIOD_REPLY_MSGTYPE_SINK_CLOSE             0x0050      // 'c'
#define PAUDIOD_REPLY_MSGTYPE_SOURCE_CLOSE           0x0060      // 'k'

struct paReplyToPolicySet {
    uint32_t     Type;
    uint32_t     stream;
    uint32_t     count;
    uint32_t     index;
    uint32_t     id;
    uint32_t     info;
    char         device[DEVICE_NAME_LENGTH];
    char         appName[APP_NAME_LENGTH];
}__attribute((packed));

struct paReplyToAudiod {
    uint32_t    id;
    bool       returnValue;
}__attribute((packed));


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
    ePhysicalSink_combined, /* both a2dp and pcm_output */
    ePhysicalSink_rtp,
    ePhysicalSink_ptts,
    ePhysicalSink_Count,    /* MUST be the last individual element. Elements below are aliases for one of the above. */

    // define logical outputs
    eMainSink = ePhysicalSink_hda,
    eA2DPSink = ePhysicalSink_usb,
    eCombined = ePhysicalSink_combined, /* both a2dp and pcm_output */
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
    ePhysicalSink_combined, /* both a2dp and pcm_output */
    ePhysicalSink_rtp,
    ePhysicalSink_ptts,
    ePhysicalSink_Count,    /* MUST be the last individual element. Elements below are aliases for one of the above. */

    // define logical outputs
    eMainSink = ePhysicalSink_pcm_output,
    eA2DPSink = ePhysicalSink_a2dp,
    eCombined = ePhysicalSink_combined, /* both a2dp and pcm_output */
    eRtpsink = ePhysicalSink_rtp,
    eAuxSink = ePhysicalSink_pcm_output
};

enum EPhysicalSource {
    ePhysicalSource_pcm_input = 0,
    ePhysicalSource_usb_input,
    ePhysicalSource_record_input,
    ePhysicalSource_voipsource_input,
    ePhysicalSource_remote_input,
    ePhysicalSource_Count,    /* MUST be the last individual element. Elements below are aliases for one of the above. */

    // define logical inputs
    eMainSource = ePhysicalSource_pcm_input,
    eAuxSource = ePhysicalSource_pcm_input
};
#endif

enum EVirtualSink {
    ealerts = 0,
    efeedback,
    eringtones,
    emedia,
    edefaultapp,
    eeffects,
    etts,
    evoipcall,
    evoicerecognition,
    ebtstream,
    ebtcall,
    efm,
    eam,
    ehdradio,
    eradio,
    edefault1,
    etts1,
    evoipcall1,
    edefault2,
    etts2,
    evoipcall2,
    eVirtualSink_Count,   /* MUST be the last element this is used to
                            * define the size of the currentmappingtable
                            * array in our userdata
                            */
    eVirtualSink_First = 0,
    eVirtualSink_Last = evoipcall2,

    eVirtualSink_None = -1,
    eVirtualSink_All = eVirtualSink_Count
};

enum EVirtualSource {
    erecord = 0,
    ebtcallsource,
    ealexa,
    ewebcall,
    evoiceassistance,
    ewebcall1,
    erecord1,
    ealexa1,
    ewebcall2,
    eVirtualSource_Count,   /* MUST be the last element this is used to
                           * define the size of the currentmappingtable
                           * array in our userdata
                           */

    eVirtualSource_First = 0,
    eVirtualSource_Last = ewebcall2,

    eVirtualSource_None = -1,
    eVirtualSource_All = eVirtualSource_Count
};
#endif /* _MODULE_PALM_POLICY_H_ */


