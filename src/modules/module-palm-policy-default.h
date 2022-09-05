/***
  This file is part of PulseAudio.
  Copyright (c) 2002-2022 LG Electronics, Inc.
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
#define SIZE_MESG_TO_AUDIOD 200
#define DEVICE_NAME_LENGTH  20
#define SINKNAME 30
struct paudiodMsgHdr{
    uint8_t msgType;
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

//SOURCEOUTPUT//SOURCE//SINKINPUT//SINK

//TYPE: PAUDIOD_MSGTYPE_ROUTING 
#define PAUDIOD_ROUTING_SINKINPUT_MOVE                  0x0010      // 'd'  'q'
#define PAUDIOD_ROUTING_SINKINPUT_RANGE                 0x0020      // 'o'
#define PAUDIOD_ROUTING_SINKINPUT_DEFAULT               0x0030      // '2'
#define PAUDIOD_ROUTING_SOURCEOUTPUT_MOVE               0x1000      // 'e'  'y'
#define PAUDIOD_ROUTING_SOURCEOUTPUT_RANGE              0x2000      // 'a'
#define PAUDIOD_ROUTING_SOURCEOUTPUT_DEFAULT            0x3000      // '3'

struct paRoutingSet {
    uint32_t     Type;
    uint32_t     startID;
    uint32_t     endID;
    uint32_t     id; //added for sinkID/sourceID
    char         device[DEVICE_NAME_LENGTH];
};

enum routing {
    eset_source_inputdevice_on_range_reply = 0,
    evirtual_sink_input_move_outputdevice_reply,
    evirtual_source_output_move_inputdevice_reply,
    eset_sink_outputdevice_on_range_reply,
    eset_sink_outputdevice_reply,
    eset_source_inputdevice_reply,
    eset_default_sink_routing_reply,
    eset_default_source_routing_reply
};

//TYPE: PAUDIOD_MSGTYPE_VOLUME
#define PAUDIOD_VOLUME_SINK_VOLUME              0x0001  // 'n'
#define PAUDIOD_VOLUME_SINK_MUTE                0x0002  // 'k'
#define PAUDIOD_VOLUME_SINKINPUT_VOLUME         0x0010  // 'b'  'r' 'v'   
#define PAUDIOD_VOLUME_SINKINPUT_MUTE           0x0020  // 'm'
#define PAUDIOD_VOLUME_SINKINPUT_INDEX          0x0030  // '6'
#define PAUDIOD_VOLUME_SOURCE_VOLUME            0x0100
#define PAUDIOD_VOLUME_SOURCE_MUTE              0x0200  // '5'
#define PAUDIOD_VOLUME_SOURCE_MIC_VOLUME        0x0300  // '8'
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
};

enum volume {
    evirtual_sink_input_set_ramp_volume_reply = 0,
    evirtual_source_input_set_volume_reply,
    evirtual_source_set_mute_reply,
    esink_set_master_mute_reply,
    evirtual_sink_input_set_mute_reply,
    esink_set_master_volume_reply,
    //evirtual_sink_input_set_ramp_volume_reply = 6,
    evirtual_sink_input_set_volume_reply,
    evirtual_sink_input_index_set_volume_reply,
    esource_set_master_mute_reply,
    esource_set_master_volume_reply
};
//TYPE: PAUDIOD_MSGTYPE_DEVICE
                    //EXTERNALSOURCE//INTERNALSOURCE//EXTERNALSINK//INTERNALSINK
#define PAUDIOD_DEVICE_LOAD_INTERNAL_SINK              0x0001      // 'i'
#define PAUDIOD_DEVICE_UNLOAD_INTERNAL_SINK            0x0002      // 'i'
#define PAUDIOD_DEVICE_LOAD_EXTERNAL_SINK              0x0010      // 'z'
#define PAUDIOD_DEVICE_UNLOAD_EXTERNAL_SINK            0x0020      // 'z'
#define PAUDIOD_DEVICE_LOAD_INTERNAL_SOURCE            0x0100    
#define PAUDIOD_DEVICE_UNLOAD_INTERNAL_SOURCE          0x0200
#define PAUDIOD_DEVICE_LOAD_EXTERNAL_SOURCE            0x1000      //'j'
#define PAUDIOD_DEVICE_UNLOAD_EXTERNAL_SOURCE          0x2000      //'j'

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
};

struct audiodDeviceSet {
    uint32_t    Type;
    uint32_t    cardNo;
    uint32_t    deviceNo;
};
enum device {
    eload_lineout_alsa_sink_reply = 0,
    edetect_usb_device_reply
    //edetect_usb_device_reply
};
//TYPE: PAUDIOD_MSGTYPE_MODULE 
#define PAUDIOD_MODULE_RTP_LOAD                    0x0001      // 'g' 'u'
#define PAUDIOD_MODULE_RTP_SET                     0X0002      // 't'
#define PAUDIOD_MODULE_BLUETOOTH_LOAD              0x0003      // 'l'
#define PAUDIOD_MODULE_BLUETOOTH_A2DPSOURCE        0x0004     // 'O'

#define RTP_IP_ADDRESS_STRING_SIZE  28 
#define BLUETOOTH_PROFILE_SIZE      5
#define BLUETOOTH_MAC_ADDRESS_SIZE  18

struct paModuleSet {
    uint16_t    Type;
    uint32_t    id;
    uint32_t    a2dpSource;
    char        address[BLUETOOTH_MAC_ADDRESS_SIZE];
    char        profile[BLUETOOTH_PROFILE_SIZE];
};

enum module {
    eunload_rtp_module_reply = 0,
    eload_Bluetooth_module_reply,
    Oea2dpSource_reply,
    eload_unicast_rtp_module_multicast_reply,
    eunload_BlueTooth_module_reply
};

//TYPE: PAUDIOD_MSGTYPE_SETPARAM              
#define PAUDIOD_SETPARAM_SUSPEND                    0x0001     // 's'
#define PAUDIOD_SETPARAM_UPDATESAMPLERATE           0x0002     // 'x'
#define PAUDIOD_SETPARAM_CLOSE_PLAYBACK             0x0003     // '7'
struct paParamSet {
    uint32_t    Type;
    uint32_t    ID;
    uint32_t    param1;
    uint32_t    param2;
    uint32_t    param3;
};

enum setParam {
    esink_suspend_request_reply = 0,
    eupdate_sample_spec_reply,
    eclose_playback_by_sink_input_reply
};
//Reply message format
//uint8_t msgType for AudioD reply from PA;
#define PAUDIOD_REPLY_MSGTYPE_ROUTING                 0x1001    //a,o,c,y  
#define PAUDIOD_REPLY_MSGTYPE_VOLUME                  0x1002    //b,k,R
#define PAUDIOD_REPLY_MSGTYPE_DEVICE                  0x1003
#define PAUDIOD_REPLY_MSGTYPE_MODULE                  0x1004    //O,I,d,H,t
#define PAUDIOD_REPLY_MSGTYPE_SETPARAM                0x1005    //x,s

//TYPE: PAUDIOD_REPLY_MSGTYPE_ROUTING 
#define PAUDIOD_REPLY_ROUTING_SINKINPUT_RANGE         0x0010      // 'o'
#define PAUDIOD_REPLY_ROUTING_SINKINPUT_DEFAULT       0x0020      // 'c'
#define PAUDIOD_REPLY_ROUTING_SOURCEOUTPUT_MOVE       0x0030      // 'y'
#define PAUDIOD_REPLY_ROUTING_SOURCEOUTPUT_RANGE      0x0040      // 'a'


struct reply_paRoutingSet {
    uint32_t     Type;
    uint32_t     startID;
    uint32_t     endID;
    uint32_t     id; //added for sinkID/sourceID
    char         device[DEVICE_NAME_LENGTH];
};


struct paReplyToAudiod {
    uint32_t    id;
    bool       returnValue;
};

struct pulseReplyToAudiod {
    uint32_t    Type;
    uint32_t    source;
    uint32_t    sourceOutput;
    uint32_t    sink;
    bool       returnValue;
};

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


