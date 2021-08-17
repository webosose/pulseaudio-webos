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

/***********************************************************************
*
* module-palm-policy.c - implements policy requests received via an SHM section
* as determined by audioD.
**********************************************************************/

#if defined(__i386__) && defined(LOCAL_TEST_BUILD)
#include <pulsecore/config.h>   /* bit of a hack, this, I have a local copy of config.h for x86 in the /usr/include/pulsecore/ dir */
#else
#include <config.h>
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <pulse/xmalloc.h>
#include <pulsecore/thread.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/namereg.h>
#include <pulsecore/idxset.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/log.h>
#include <pulsecore/module.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pulsecore/modargs.h>

#include <alsa/asoundlib.h>

#include "module-palm-policy-symdef.h"
#include "module-palm-policy.h"
#include "module-palm-policy-tables.h"

#define _MEM_ZERO(object) (memset (&(object), '\0', sizeof ((object))))
#define _NAME_STRUCT_OFFSET(struct_type, member) ((long) ((unsigned char*) &((struct_type*) 0)->member))

#ifndef PALM_UP_RAMP_MSEC
#define PALM_UP_RAMP_MSEC 600
#endif

#ifndef PALM_DOWN_RAMP_MSEC
#define PALM_DOWN_RAMP_MSEC 400
#endif

#ifndef CLAMP_VOLUME_TABLE
#define CLAMP_VOLUME_TABLE(a)  (((a) < (1)) ? (a) : (1))
#endif

#define RAMP_DURATION_MSEC 1000
#define PCM_SINK_NAME "pcm_output"
#define PCM_SOURCE_NAME "pcm_input"
#define PCM_HEADPHONE_SINK "pcm_headphone"
#define RTP_SINK_NAME "rtp"
#define SCENARIO_STRING_SIZE 28
#define RTP_IP_ADDRESS_STRING_SIZE 28
#define RTP_CONNECTION_TYPE_STRING_SIZE 12
#define ROUTE_AUTO 0
#define ROUTE_HEADPHONES 1
#define BLUETOOTH_MAC_ADDRESS_SIZE 18
#define BLUETOOTH_SINK_NAME_SIZE 30
#define BLUETOOTH_PROFILE_SIZE 5
#define BLUETOOTH_SINK_INIT_SIZE 12
#define DISPLAY_ONE 1
#define DISPLAY_TWO 2
#define DISPLAY_SINK_COUNT 3
#define DISPLAY_ONE_CARD_NUMBER 1
#define DISPLAY_TWO_CARD_NUMBER 2
#define DISPLAY_ONE_USB_SINK "display_usb1"
#define DISPLAY_TWO_USB_SINK "display_usb2"
#define VOLUMETABLE 0
#define MIN_VOLUME 0
#define MAX_VOLUME 100
#define MUTE 1
#define UNMUTE 0
#define SAVE 0
#define DEVICE_NAME_SIZE 50
#define SOURCE_NAME_LENGTH 18
#define SINK_NAME_LENGTH 16
#define BLUETOOTH_SINK_NAME_LENGTH  20
#define DEVICE_NAME_LENGTH 50

#define DEFAULT_SOURCE_0 "/dev/snd/pcmC0D0c"
#define DEFAULT_SOURCE_1 "/dev/snd/pcmC1D0c"

/* use this to tie an individual sink_input to the
 * virtual sink it was created against */

struct sinkinputnode {
    int32_t sinkinputidx;       /* index of this sink-input */
    int32_t virtualsinkid;      /* index of virtual sink it was created against, this is our index from
                                 * _enum_virtualsinkmap rather than a pulseaudio sink idx */
    pa_sink_input *sinkinput;   /* reference to sink input with this index */

    pa_bool_t paused;

    PA_LLIST_FIELDS(struct sinkinputnode); /* fields that use a pulse defined linked list */
};

struct sourceoutputnode {
    int32_t sourceoutputidx;    /* index of this sink-input */
    int32_t virtualsourceid;    /* index of virtual sink it was created against, this is our index from
                                 * _enum_virtualsourcemap rather than a pulseaudio sink idx */
    pa_source_output *sourceoutput; /* reference to sink input with this index */
    pa_bool_t paused;

    PA_LLIST_FIELDS(struct sourceoutputnode); /* fields that use a pulse defined linked list */
};

/* user data for the pulseaudio module, store this in init so that
 * stuff we need can be accessed when we get callbacks
 */

struct userdata {
    /* cached references to pulse internals */
    pa_core *core;
    pa_module *module;

    /* slots for hook functions, these get called by pulse */
    pa_hook_slot *sink_input_new_hook_slot; /* called prior to creation of new sink-input */
    pa_hook_slot *sink_input_fixate_hook_slot;
    pa_hook_slot *sink_input_put_hook_slot;
    pa_hook_slot *sink_input_state_changed_hook_slot; /* called on state change, play/pause */
    pa_hook_slot *sink_input_unlink_hook_slot; /* called prior to destruction of a sink-input */
    pa_hook_slot *sink_state_changed_hook_slot; /* for BT sink open-close */

    pa_hook_slot *source_output_new_hook_slot; /* called prior to creation of new source-output */
    pa_hook_slot *source_output_fixate_hook_slot;
    pa_hook_slot *source_output_put_hook_slot;
    pa_hook_slot *source_output_state_changed_hook_slot;
    pa_hook_slot *source_output_unlink_hook_slot; /* called prior to destruction of a source-output */
    pa_hook_slot *sink_state_changed_hook;
    pa_hook_slot *source_state_changed_hook_slot;
    pa_hook_slot *module_unload_hook_slot;
    pa_hook_slot *module_load_hook_slot;
    pa_hook_slot *sink_load_hook_slot;
    pa_hook_slot *sink_input_move_finish;
    pa_hook_slot *sink_new;
    pa_hook_slot *sink_unlink;
    pa_hook_slot *sink_unlink_post;
    pa_hook_slot *source_unlink_post;

    /* make sure sink_mapping_table is the same size as
     * defaulmappingtable, since we'll copy that to this */
    struct _virtualsinkmap sink_mapping_table[eVirtualSink_Count];
    struct _virtualsourcemap source_mapping_table[eVirtualSource_Count];

    /* fields for socket - ipc support for audiod */

    int sockfd;                 /* descriptor for socket */
    int newsockfd;              /* descriptor for connections on socket */

    struct sockaddr_un name;    /* filename for socket */
    pa_io_event *sockev;        /* socket event handler */
    pa_io_event *connev;        /* connection event handler */
    pa_bool_t connectionactive; /* do we have an active connection on the socket */

    // Maintain count of sinks opened, as sent to audiod, so that we can re-send data on reconnect for audiod to resync with us
    int32_t audiod_sink_input_opened[eVirtualSink_Count];
    int32_t audiod_source_output_opened[eVirtualSource_Count];
    int32_t n_sink_input_opened;
    int32_t n_source_output_opened;

    /* list of nodes describing sink-input and source-output vs virtual device created against */
    PA_LLIST_HEAD(struct sinkinputnode, sinkinputnodelist);
    PA_LLIST_HEAD(struct sourceoutputnode, sourceoutputnodelist);

    int32_t media_type;    /* store stream type for combined sink */

    pa_module* rtp_module;
    pa_module* alsa_source;
    pa_module* alsa_sink1;
    pa_module* alsa_sink2;
    pa_module* default1_alsa_sink;
    pa_module* default2_alsa_sink;
    pa_module* headphone_sink;
    char *destAddress;
    int connectionPort ;
    char *connectionType;
    char *deviceName;
    char *callback_deviceName;

    int external_soundcard_number;
    int external_device_number;
    int a2dpSource;

    pa_module *combined;
    char *scenario;
    pa_module *btDiscoverModule;
    bool IsBluetoothEnabled;
    bool IsUsbConnected[DISPLAY_SINK_COUNT];
    bool IsDisplay1usbSinkLoaded;
    bool IsDisplay2usbSinkLoaded;
    bool IsHeadphoneConnected;
    int externalSoundCardNumber[DISPLAY_SINK_COUNT];
    char address[BLUETOOTH_MAC_ADDRESS_SIZE];
    char physicalSinkBT[BLUETOOTH_SINK_NAME_SIZE];
    char btProfile[BLUETOOTH_PROFILE_SIZE];
    uint32_t display1UsbIndex;
    uint32_t display2UsbIndex;
};

static void virtual_source_output_move_inputdevice(int virtualsourceid, char* inputdevice, struct userdata *u);

static void virtual_source_set_mute(int sourceid, int mute, struct userdata *u);

static void virtual_source_input_set_volume(int sinkid, int volumetoset, int volumetable, struct userdata *u);

static void virtual_source_input_set_volume_with_ramp(int sourceId, int volumetoset, int volumetable, struct userdata *u);

static void virtual_sink_input_move_outputdevice(int virtualsinkid, char* outputdevice, struct userdata *u);

static void virtual_sink_input_set_volume(int sinkid, int volumetoset, int volumetable, struct userdata *u);

static void virtual_sink_input_set_ramp_volume(int sinkid, int volumetoset, int volumetable, struct userdata *u);

static void virtual_sink_input_set_mute(int sinkid, bool mute, struct userdata *u);

static void sink_set_master_mute(const char* outputdevice, bool mute, struct userdata *u);

static void sink_set_master_volume(const char* outputdevice, int volume, struct userdata *u);

static int sink_suspend_request(struct userdata *u);

static int update_sample_spec(struct userdata *u, int rate);

static void parse_message(char *msgbuf, int bufsize, struct userdata *u);

static void handle_io_event_socket(pa_mainloop_api * ea, pa_io_event * e,
                                   int fd, pa_io_event_flags_t events, void *userdata);

static void handle_io_event_connection(pa_mainloop_api * ea, pa_io_event * e,
                                       int fd, pa_io_event_flags_t events, void *userdata);

static pa_hook_result_t route_sink_input_new_hook_callback(pa_core * c, pa_sink_input_new_data *data,
                                                          struct userdata *u);

static pa_hook_result_t route_sink_input_fixate_hook_callback(pa_core * c, pa_sink_input_new_data *data,
                                                             struct userdata *u);

static pa_hook_result_t route_sink_input_put_hook_callback(pa_core * c, pa_sink_input * si,
                                                           struct userdata *u);

static pa_hook_result_t route_source_output_new_hook_callback(pa_core *c, pa_source_output_new_data *data,
                                                              struct userdata *u);

static pa_hook_result_t route_source_output_fixate_hook_callback(pa_core *c, pa_source_output_new_data *data,
                                                                 struct userdata *u);

static pa_hook_result_t route_source_output_put_hook_callback(pa_core *c, pa_source_output * so,
                                                              struct userdata *u);

static pa_hook_result_t route_sink_input_unlink_hook_callback(pa_core *c, pa_sink_input * data,
                                                             struct userdata *u);

static pa_hook_result_t route_sink_input_state_changed_hook_callback(pa_core *c, pa_sink_input * data,
                                                                    struct userdata *u);
static pa_hook_result_t module_unload_subscription_callback(pa_core *c, pa_module *m, struct userdata *u);

//hook call back to inform output and input device module loading status to audiod
static pa_hook_result_t module_load_subscription_callback(pa_core *c, pa_module *m, struct userdata *u);

/* Hook callback for combined sink routing(sink input move,sink put & unlink) */
static pa_hook_result_t route_sink_input_move_finish_cb(pa_core *c, pa_sink_input *data, struct userdata *u);

static pa_hook_result_t route_sink_unlink_cb(pa_core *c, pa_sink *sink, struct userdata *u);

static pa_hook_result_t sink_load_subscription_callback(pa_core *c, pa_sink_new_data *data, struct userdata *u);

static pa_hook_result_t route_sink_unlink_post_cb(pa_core *c, pa_sink *sink, struct userdata *u);

static pa_hook_result_t route_source_unlink_post_cb(pa_core *c, pa_source *source, struct userdata *u);

static pa_hook_result_t route_source_output_state_changed_hook_callback(pa_core *c, pa_source_output * data,
                                                                        struct userdata *u);

static pa_hook_result_t route_source_output_unlink_hook_callback(pa_core *c, pa_source_output *data,
                                                                struct userdata *u);

static pa_hook_result_t route_sink_state_changed_hook_callback(pa_core *c, pa_object *o,
                                                                 struct userdata *u);

static pa_bool_t sink_input_new_data_is_passthrough(pa_sink_input_new_data *data);

static pa_hook_result_t sink_state_changed_cb(pa_core *c, pa_object *o, struct userdata *u);

static pa_hook_result_t source_state_changed_cb(pa_core *c, pa_object *o, struct userdata *u);

static void unload_alsa_source(struct userdata *u, int status);

static void set_source_inputdevice(struct userdata *u, char* inputdevice, int sourceId);

static void set_sink_outputdevice(struct userdata *u, char* outputdevice, int sinkid);

static void set_sink_outputdevice_on_range(struct userdata *u, char* outputdevice, int startsinkid, int endsinkid);

static void set_source_inputdevice_on_range(struct userdata *u, char* outputdevice, int startsourcekid, int endsourceid);

static void set_default_sink_routing(struct userdata *u, int startsinkid, int endsinkid);

static void set_default_source_routing(struct userdata *u, int startsourceid, int endsourceid);

PA_MODULE_AUTHOR("Palm, Inc.");
PA_MODULE_DESCRIPTION("Implements policy, communication with external app is a socket at /tmp/palmaudio");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);
PA_MODULE_USAGE("No parameters for this module");

/* When headset is connected to Rpi,audio will be routed to headset.
Once it is removed, audio will be routed to HDMI.
*/

static pa_bool_t sink_input_new_data_is_passthrough(pa_sink_input_new_data *data)
{

    if (pa_sink_input_new_data_is_passthrough(data))
        return true;
    else {
        pa_format_info *f = NULL;
        uint32_t idx = 0;

        PA_IDXSET_FOREACH(f, data->req_formats, idx) {
        if (!pa_format_info_is_pcm(f))
            return true;
        }
    }
    return false;
}

//To set the physical source(input device) for single virtual source
static void set_source_inputdevice(struct userdata *u, char* inputdevice, int sourceId)
{
    pa_log("set_source_inputdevice: inputdevice:%s sourceId:%d", inputdevice, sourceId);
    pa_sink *destsource = NULL;
    struct sourceoutputnode *thelistitem = NULL;
    if (sourceId >= 0 && sourceId < eVirtualSource_Count)
    {
        strncpy(u->source_mapping_table[sourceId].inputdevice, inputdevice, DEVICE_NAME_LENGTH);
        pa_log_info("set_source_inputdevice setting inputdevice:%s for source:%s",\
            u->source_mapping_table[sourceId].inputdevice, u->source_mapping_table[sourceId].virtualsourcename);
        destsource =
        pa_namereg_get(u->core, u->source_mapping_table[sourceId].inputdevice, PA_NAMEREG_SOURCE);
        if (destsource == NULL)
            pa_log_info("set_source_inputdevice destsource is null");
        /* walk the list of siource-inputs we know about and update their sources */
        for (thelistitem = u->sourceoutputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
            if ((int) thelistitem->virtualsourceid == sourceId && !pa_source_output_is_passthrough(thelistitem->sourceoutput))
            {
                pa_log_info("moving the virtual source%d to physical source%s:", sourceId, u->source_mapping_table[sourceId].inputdevice);
                pa_source_output_move_to(thelistitem->sourceoutput, destsource, true);
            }
        }
    }
    else
        pa_log_warn("set_source_inputdevice: sourceId is not valid");
}

//To set the physical sink(output device) for single sink
static void set_sink_outputdevice(struct userdata *u, char* outputdevice, int sinkid)
{
    pa_log("set_sink_outputdevice: outputdevice:%s sinkid:%d", outputdevice, sinkid);
    pa_sink *destsink = NULL;
    struct sinkinputnode *thelistitem = NULL;
    if (sinkid >= 0 && sinkid < eVirtualSink_Count)
    {
        strncpy(u->sink_mapping_table[sinkid].outputdevice, outputdevice, DEVICE_NAME_LENGTH);
        pa_log_info("set_sink_outputdevice setting outputdevice:%s for sink:%s",\
            u->sink_mapping_table[sinkid].outputdevice, u->sink_mapping_table[sinkid].virtualsinkname);
        destsink =
        pa_namereg_get(u->core, u->sink_mapping_table[sinkid].outputdevice, PA_NAMEREG_SINK);
        if (destsink == NULL)
            pa_log_info("set_sink_outputdevice destsink is null");
        /* walk the list of sink-inputs we know about and update their sinks */
        for (thelistitem = u->sinkinputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
            if ((int) thelistitem->virtualsinkid == sinkid && !pa_sink_input_is_passthrough(thelistitem->sinkinput))
            {
                pa_log_info("moving the virtual sink%d to physical sink%s:", sinkid, u->sink_mapping_table[sinkid].outputdevice);
                pa_sink_input_move_to(thelistitem->sinkinput, destsink, true);
            }
        }
    }
    else
        pa_log_warn("set_sink_outputdevice: sinkid is not valid");
}

//To set the physical sink(output device) for the streams with range based, consider this while adding any new virtual sink to the list
static void set_sink_outputdevice_on_range(struct userdata *u, char* outputdevice, int startsinkid, int endsinkid)
{
    pa_log("set_sink_outputdevice_on_range: outputdevice:%s startsinkid:%d, endsinkid:%d", outputdevice, startsinkid, endsinkid);
    pa_sink *destsink = NULL;
    struct sinkinputnode *thelistitem = NULL;
    if (startsinkid >= 0 && endsinkid < eVirtualSink_Count)
    {
        for (int i  = startsinkid; i <= endsinkid; i++)
        {
            strncpy(u->sink_mapping_table[i].outputdevice, outputdevice, DEVICE_NAME_LENGTH);
            destsink =
            pa_namereg_get(u->core, u->sink_mapping_table[i].outputdevice, PA_NAMEREG_SINK);
            if (destsink == NULL) {
                pa_log_info("set_sink_outputdevice_on_range destsink is null");
                return;
            }
            /* walk the list of sink-inputs we know about and update their sinks */
            for (thelistitem = u->sinkinputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
                if ((int) thelistitem->virtualsinkid == i && !pa_sink_input_is_passthrough(thelistitem->sinkinput))
                {
                    pa_log_info("moving the virtual sink%d to physical sink%s:", i, u->sink_mapping_table[i].outputdevice);
                    pa_sink_input_move_to(thelistitem->sinkinput, destsink, true);
                }
            }
        }
    }
    else
        pa_log_warn("set_sink_outputdevice_on_range: start and end sink are not in range");
}

//To set the physical source(input device) for the sources with range based, consider this while adding any new virtual source to the list
static void set_source_inputdevice_on_range(struct userdata *u, char* inputdevice, int startsourceid, int endsourceid)
{
    pa_log("set_source_inputdevice_on_range: inputdevice:%s startsourceid:%d, endsourceid:%d", inputdevice, startsourceid, endsourceid);
    pa_sink *destsource = NULL;
    struct sourceoutputnode *thelistitem = NULL;
    if (startsourceid >= 0 && endsourceid < eVirtualSource_Count)
    {
        for (int i  = startsourceid; i <= endsourceid; i++)
        {
            strncpy(u->source_mapping_table[i].inputdevice, inputdevice, DEVICE_NAME_LENGTH);
            destsource =
            pa_namereg_get(u->core, u->source_mapping_table[i].inputdevice, PA_NAMEREG_SOURCE);
            if (destsource == NULL) {
                pa_log_info("set_default_source_routing destsource is null");
                return;
            }
            /* walk the list of siource-inputs we know about and update their sources */
            for (thelistitem = u->sourceoutputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
                if ((int) thelistitem->virtualsourceid == i && !pa_source_output_is_passthrough(thelistitem->sourceoutput))
                {
                    pa_log_info("moving the virtual source%d to physical source%s:", i, u->source_mapping_table[i].inputdevice);
                    pa_source_output_move_to(thelistitem->sourceoutput, destsource, true);
                }
            }
        }
    }
    else
        pa_log_warn("set_source_inputdevice_on_range: start and end source are not in range");
}

static void set_default_sink_routing(struct userdata *u, int startsinkid, int endsinkid)
{
    pa_log("set_default_sink_routing: startsinkid:%d, endsinkid:%d", startsinkid, endsinkid);
    pa_sink *destsink = NULL;
    struct sinkinputnode *thelistitem = NULL;
    if (startsinkid >= 0 && endsinkid < eVirtualSink_Count)
    {
        for (int i  = startsinkid; i <= endsinkid; i++)
        {
            strncpy(u->sink_mapping_table[i].outputdevice, u->sink_mapping_table[i].virtualsinkname, DEVICE_NAME_LENGTH);
            destsink =
            pa_namereg_get(u->core, u->sink_mapping_table[i].outputdevice, PA_NAMEREG_SINK);
            if (destsink == NULL)
                pa_log_info("set_default_sink_routing destsink is null");
            /* walk the list of sink-inputs we know about and update their sinks */
            for (thelistitem = u->sinkinputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
                if ((int) thelistitem->virtualsinkid == i && !pa_sink_input_is_passthrough(thelistitem->sinkinput))
                {
                    pa_log_info("moving the virtual sink:%d to physical sink:%s:", i, u->sink_mapping_table[i].outputdevice);
                    pa_sink_input_move_to(thelistitem->sinkinput, destsink, true);
                }
            }
        }
    }
    else
        pa_log_warn("set_default_sink_routing: start and end sink are not in range");
}

static void set_default_source_routing(struct userdata *u, int startsourceid, int endsourceid)
{
    pa_log("set_default_source_routing: startsourceid:%d, endsourceid:%d", startsourceid, endsourceid);
    pa_sink *destsource = NULL;
    struct sourceoutputnode *thelistitem = NULL;
    if (startsourceid >= 0 && endsourceid < eVirtualSource_Count)
    {
        for (int i  = startsourceid; i <= endsourceid; i++)
        {
            strncpy(u->source_mapping_table[i].inputdevice, u->source_mapping_table[i].virtualsourcename, DEVICE_NAME_LENGTH);
            destsource =
            pa_namereg_get(u->core, u->source_mapping_table[i].inputdevice, PA_NAMEREG_SOURCE);
            if (destsource == NULL)
                pa_log_info("set_default_source_routing destsource is null");
            /* walk the list of siource-inputs we know about and update their sources */
            for (thelistitem = u->sourceoutputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
                if ((int) thelistitem->virtualsourceid == i && !pa_source_output_is_passthrough(thelistitem->sourceoutput))
                {
                    pa_log_info("moving the virtual source%d to physical source%s:", i, u->source_mapping_table[i].inputdevice);
                    pa_source_output_move_to(thelistitem->sourceoutput, destsource, true);
                }
            }
        }
    }
    else
        pa_log_warn("set_default_source_routing: start and end source are not in range");
}

static void virtual_source_output_move_inputdevice(int virtualsourceid, char* inputdevice, struct userdata *u) {
    pa_log_info("virtual_source_output_move_inputdevice for virtualsourceid = %d to inputdevice = %s",\
        virtualsourceid, inputdevice);
    struct sourceoutputnode *thelistitem = NULL;
    pa_source *destsource = NULL;
    if (virtualsourceid >= 0 && virtualsourceid < eVirtualSource_Count) {
        strncpy(u->source_mapping_table[virtualsourceid].inputdevice, inputdevice, DEVICE_NAME_LENGTH);
        pa_log_info("virtual_source_output_move_inputdevice name = %s", u->source_mapping_table[virtualsourceid].inputdevice);
        destsource =
            pa_namereg_get(u->core, u->source_mapping_table[virtualsourceid].inputdevice, PA_NAMEREG_SOURCE);

        /* walk the list of source-inputs we know about and update their sources */
        for (thelistitem = u->sourceoutputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
            if (thelistitem->virtualsourceid == virtualsourceid) {
                pa_source_output_move_to(thelistitem->sourceoutput, destsource, true);
            }
        }
    }
    else
        pa_log("virtual_source_input_set_physical_source: source ID out of range");
}

/* set the mute for all source-outputs associated with a virtual source,
 * sourceid - virtual source on which to set mute
 * mute - 0 unmuted, 1 muted. */
static void virtual_source_set_mute(int sourceid, int mute, struct userdata *u) {
    pa_log_info("virtual_source_set_mute for sourceid:%d with mute:%d", sourceid, mute);
    struct sourceoutputnode *thelistitem = NULL;
    if (sourceid >= 0 && sourceid < eVirtualSource_Count)
    {
        for (thelistitem = u->sourceoutputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
            pa_log_debug("[%s] Available sourceId:%d name:%s",\
            __func__, thelistitem->virtualsourceid, thelistitem->sourceoutput->source->name);
            if (u->sourceoutputnodelist->virtualsourceid == sourceid)
            {
                pa_source_output_set_mute(thelistitem->sourceoutput, mute, TRUE);
                u->source_mapping_table[sourceid].ismuted = mute;
            }
        }
    }
}

static void virtual_sink_input_move_outputdevice(int virtualsinkid, char* outputdevice, struct userdata *u) {
    pa_log_info("virtual_sink_input_move_outputdevice for virtualsinkid = %d to outputdevice = %s",\
        virtualsinkid, outputdevice);
    struct sinkinputnode *thelistitem = NULL;
    pa_sink *destsink = NULL;
    if (virtualsinkid >= 0 && virtualsinkid < eVirtualSink_Count) {
        strncpy(u->sink_mapping_table[virtualsinkid].outputdevice, outputdevice, DEVICE_NAME_LENGTH);
        pa_log_info("virtual_sink_input_move_outputdevice name = %s", u->sink_mapping_table[virtualsinkid].outputdevice);
        destsink =
            pa_namereg_get(u->core, u->sink_mapping_table[virtualsinkid].outputdevice, PA_NAMEREG_SINK);
        if (destsink == NULL)
        {
            pa_log_info("virtual_sink_input_move_outputdevice  destsink is null");
        }
        /* walk the list of sink-inputs we know about and update their sinks */
        for (thelistitem = u->sinkinputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
            if ((int) thelistitem->virtualsinkid == virtualsinkid && !pa_sink_input_is_passthrough(thelistitem->sinkinput))
            {
                pa_log_info("moving the virtual sink%d to physical sink%s:", virtualsinkid, u->sink_mapping_table[virtualsinkid].outputdevice);
                pa_sink_input_move_to(thelistitem->sinkinput, destsink, true);
            }
        }
    }
    else
        pa_log("virtual_sink_input_move_outputdevice: sink ID out of range");
}

/* set the volume for all sink-inputs associated with a virtual sink,
 * sinkid - virtual sink on which to set volumes
 * volumetoset - 0..65535 gain setting for pulseaudio to use */

static void virtual_sink_input_set_ramp_volume(int sinkid, int volumetoset, int volumetable, struct userdata *u) {
   struct sinkinputnode *thelistitem = NULL;
   struct pa_cvolume cvolume, orig_cvolume;

   if (sinkid >= 0 && sinkid < eVirtualSink_Count) {
       /* set the default volume on new streams created on
        * this sink, update rules table for final ramped volume */

       /* walk the list of sink-inputs we know about and update their volume */
       for (thelistitem = u->sinkinputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
           if(thelistitem->virtualsinkid == sinkid) {
               if (!pa_sink_input_is_passthrough(thelistitem->sinkinput)) {
                   pa_usec_t msec;
                   u->sink_mapping_table[sinkid].volumetable = volumetable;
                   pa_log_debug("volume we are setting is %u, %f db",
                            pa_sw_volume_from_dB(_mapPercentToPulseRamp
                                                 [volumetable][volumetoset]),
                            _mapPercentToPulseRamp[volumetable][volumetoset]);
                   pa_cvolume_set(&cvolume,
                              thelistitem->sinkinput->sample_spec.channels,
                              pa_sw_volume_from_dB(_mapPercentToPulseRamp[volumetable]
                                                   [volumetoset]));

                   if (pa_cvolume_max(&cvolume) >=
                       pa_cvolume_max(pa_sink_input_get_volume(thelistitem->sinkinput, &orig_cvolume, TRUE)))
                       msec = PALM_UP_RAMP_MSEC;
                   else
                       msec = PALM_DOWN_RAMP_MSEC;

                   /* pa_sink_input_set_volume_with_ramping(thelistitem->sinkinput, &cvolume, TRUE, TRUE, msec * PA_USEC_PER_MSEC); */
                   pa_sink_input_set_volume(thelistitem->sinkinput, &cvolume, TRUE, TRUE);
               }
           }
       }
       u->sink_mapping_table[sinkid].volume = volumetoset;
   }
   else
       pa_log("virtual_sink_input_set_volume: sink ID %d out of range", sinkid);
}

/* set the volume for all sink-inputs associated with a virtual sink,
 * sinkid - virtual sink on which to set volumes
 * volumetoset - 0..65535 gain setting for pulseaudio to use */

static void virtual_sink_input_set_volume(int sinkid, int volumetoset, int volumetable, struct userdata *u) {
    struct sinkinputnode *thelistitem = NULL;
    struct pa_cvolume cvolume;

    if (sinkid >= 0 && sinkid < eVirtualSink_Count) {
        /* set the default volume on new streams created on
         * this sink, update rules table for final ramped volume */

        /* walk the list of sink-inputs we know about and update their volume */
        for (thelistitem = u->sinkinputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
            if (thelistitem->virtualsinkid == sinkid) {
                if (!pa_sink_input_is_passthrough(thelistitem->sinkinput)) {
                    u->sink_mapping_table[sinkid].volumetable = volumetable;
                    pa_log_debug("volume we are setting is %u, %f db",
                             pa_sw_volume_from_dB(_mapPercentToPulseRamp
                                                  [volumetable][volumetoset]),
                             _mapPercentToPulseRamp[volumetable][volumetoset]);
                    if (volumetoset)
                        pa_cvolume_set(&cvolume,
                                   thelistitem->sinkinput->sample_spec.channels,
                                   pa_sw_volume_from_dB(_mapPercentToPulseRamp[volumetable]
                                                        [volumetoset]));
                    else
                        pa_cvolume_set(&cvolume, thelistitem->sinkinput->sample_spec.channels, 0);
                    //pa_sink_input_set_volume_with_ramping(thelistitem->sinkinput, &cvolume, TRUE, TRUE, 5 * PA_USEC_PER_MSEC);
                    pa_sink_input_set_volume(thelistitem->sinkinput, &cvolume, TRUE, TRUE);
                }
            }
        }
        u->sink_mapping_table[sinkid].volume = volumetoset;
    }
    else
        pa_log("virtual_sink_input_set_volume: sink ID %d out of range", sinkid);
}

static void  virtual_source_input_set_volume(int sourceId, int volumetoset, int volumetable, struct userdata *u) {
    struct sourceoutputnode *thelistitem = NULL;
    struct pa_cvolume cvolume;
    pa_log_debug("[%s] Requested to set volume for sourceId:%d volume:%d", __func__, sourceId, volumetoset);
    if (sourceId >= 0 && sourceId < eVirtualSource_Count) {
        /* set the default volume on new streams created on
         * this sink, update rules table for final ramped volume */

        /* walk the list of sink-inputs we know about and update their volume */
        for (thelistitem = u->sourceoutputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {
            pa_log_debug("[%s] Available sourceId:%d name:%s",\
                __func__, thelistitem->virtualsourceid, thelistitem->sourceoutput->source->name);
            if (thelistitem->virtualsourceid == sourceId) {
                if (!pa_source_output_is_passthrough(thelistitem->sourceoutput)) {
                    u->source_mapping_table[sourceId].volumetable = volumetable;
                    pa_log_debug("volume we are setting is %u, %f db",\
                        pa_sw_volume_from_dB(_mapPercentToPulseRamp\
                        [volumetable][volumetoset]),\
                        _mapPercentToPulseRamp[volumetable][volumetoset]);
                    if (volumetoset)
                        pa_cvolume_set(&cvolume,\
                        thelistitem->sourceoutput->sample_spec.channels,\
                        pa_sw_volume_from_dB(_mapPercentToPulseRamp[volumetable]\
                        [volumetoset]));
                    else
                        pa_cvolume_set(&cvolume, thelistitem->sourceoutput->sample_spec.channels, 0);
                        pa_source_output_set_volume(thelistitem->sourceoutput, &cvolume, TRUE, TRUE);
                }
                else {
                    pa_log_debug("setting volume on Compress playback to %d",volumetoset);
                }
            }
        }
        u->source_mapping_table[sourceId].volume = volumetoset;
    }
    else
        pa_log("virtual_source_input_set_volume: sourceId ID %d out of range", sourceId);
}

/* set the volume for all sink-inputs associated with a virtual sink,
 * sinkid - virtual sink on which to set volumes
 * setmuteval - (true : false) setting for pulseaudio to use */
static void virtual_sink_input_set_mute(int sinkid, bool mute, struct userdata *u)
{
    pa_log_info("virtual_sink_input_set_mute for sinkid = %d mute = %d",\
        sinkid, (int) mute);
    struct sinkinputnode *thelistitem = NULL;
    if (sinkid >= 0 && sinkid < eVirtualSink_Count)
    {
        for (thelistitem = u->sinkinputnodelist; thelistitem != NULL; thelistitem = thelistitem->next)
        {
            pa_log_debug("[%s] Available sinkId:%d name:%s : %d",\
                  __func__, thelistitem->virtualsinkid, thelistitem->sinkinput->sink->name, thelistitem->sinkinputidx);
            if (thelistitem->virtualsinkid == sinkid) {
                pa_sink_input_set_mute(thelistitem->sinkinput, mute, TRUE);
                u->sink_mapping_table[sinkid].ismuted = mute;
            }
        }
    }
    else
        pa_log("virtual_sink_input_set_mute: sink ID %d out of range", sinkid);
}

static void sink_set_master_volume(const char* outputdevice, int volume, struct userdata *u)
{
    pa_assert(u);
    struct pa_cvolume cvolume;
    pa_sink *destSink = NULL;
    if (!(MIN_VOLUME <= volume <= MAX_VOLUME))
    {
        pa_log_debug("Invalid volume range. set volume requested for %d", volume);
        return;
    }
    pa_log_debug("Inside sink_set_master_volume : volume requested is %d volume we are setting is %u, %f db",
        volume,
        pa_sw_volume_from_dB(_mapPercentToPulseRamp[VOLUMETABLE][volume]),
        _mapPercentToPulseRamp[VOLUMETABLE][volume]);

    if (pa_streq(outputdevice,PCM_SINK_NAME) || pa_streq(outputdevice,PCM_HEADPHONE_SINK))
    {
        //Volume control is done from umi/alsa
        pa_log_debug("Volume control is done from umi/alsa. retruning from here");
        return;
    }
    destSink = pa_namereg_get(u->core, outputdevice, PA_NAMEREG_SINK);
    if (NULL != destSink)
    {
        pa_cvolume_set(&cvolume, destSink->sample_spec.channels,\
            pa_sw_volume_from_dB(_mapPercentToPulseRamp[VOLUMETABLE][volume]));
        pa_sink_set_volume(destSink, &cvolume, true, false);
    }
    else
        pa_log_warn("sink_set_master_volume destSink is null");
}

static void sink_set_master_mute(const char* outputdevice, bool mute, struct userdata *u) {
    pa_log_debug("Inside sink_set_master_mute with outputdevice %s and mute %d", outputdevice, mute);
    pa_assert(u);
    pa_sink *destSink = NULL;
    destSink = pa_namereg_get(u->core, outputdevice, PA_NAMEREG_SINK);
    if (NULL != destSink)
    {
        if (mute)
            pa_sink_set_mute(destSink, MUTE, SAVE);
        else
            pa_sink_set_mute(destSink, UNMUTE, SAVE);
    }
    else
        pa_log_warn("sink_set_master_mute destSink is null");
}

static void source_set_master_mute(const char* source, bool mute, struct userdata *u)
{
    pa_log_debug("Inside source_set_master_mute with source %s and mute %d", source, mute);
    pa_assert(u);
    char *sourcename;
    pa_source *destSource = NULL;
    destSource = pa_namereg_get(u->core, source, PA_NAMEREG_SOURCE);
    if (NULL != destSource)
    {
        if (mute)
            pa_source_set_mute(destSource, MUTE, SAVE);
        else
            pa_source_set_mute(destSource, UNMUTE, SAVE);
    }
    else
        pa_log("Valid source is not present for source ID %d ", source);
}

static int sink_suspend_request(struct userdata *u) {
    struct sinkinputnode *thesinklistitem = NULL;
    struct sourceoutputnode *thesourcelistitem = NULL;

    for (thesinklistitem = u->sinkinputnodelist; thesinklistitem != NULL; thesinklistitem = thesinklistitem->next) {
        if (thesinklistitem->sinkinput->state == PA_SINK_INPUT_RUNNING) {
            pa_log("%s: sink input (%d) is active and running, close and report error",
                 __FUNCTION__, thesinklistitem->virtualsinkid);
            break;
        }
    }

    pa_sink_suspend_all(u->core, true, PA_SUSPEND_IDLE);

    for (thesourcelistitem = u->sourceoutputnodelist; thesourcelistitem != NULL;
         thesourcelistitem = thesourcelistitem->next) {
        if (thesourcelistitem->sourceoutput->state == PA_SOURCE_OUTPUT_RUNNING) {
            pa_log("%s: source output (%d) is active and running, close and report error",
                 __FUNCTION__, thesourcelistitem->virtualsourceid);
            break;
        }
    }
    pa_source_suspend_all(u->core, true, PA_SUSPEND_IDLE);

    return 0;
}

static int update_sample_spec(struct userdata *u, int rate) {
#if 0
    pa_sink *sink;
    pa_source *source;

    uint32_t idx, i, need_unsuspend = 0;
    pa_sample_spec sample_spec;

    for (i = 0; i < ePhysicalSink_Count; i++) {
        for (sink = PA_SINK(pa_idxset_first(u->core->sinks, &idx)); sink;
             sink = PA_SINK(pa_idxset_next(u->core->sinks, &idx))) {
            if (strcmp(u->source_mapping_table[i].outputdevice, sink->name) == 0) {
                if (sink->update_sample_spec && sink->sample_spec.rate != rate) {
                    if (sink->state == PA_SINK_RUNNING) {
                        pa_log_info("Need to suspend then unsuspend device before changing sampling rate");
                        need_unsuspend = 1;
                    }
                    pa_sink_suspend(sink, 1, PA_SUSPEND_IDLE);

                    pa_log_info
                        ("update_sample_spec is available for sink %s, current sample_spec rate %d, changing to %d",
                         sink->name, sink->sample_spec.rate, rate);
                    sample_spec = sink->sample_spec;
                    sample_spec.rate = rate;
                    sink->update_sample_spec(sink, &sample_spec);

                    if (need_unsuspend == 1) {
                        pa_sink_suspend(sink, 0, PA_SUSPEND_IDLE);
                        need_unsuspend = 0;
                    }
                }
            }
        }
    }

    for (i = 0; i < ePhysicalSource_Count; i++) {
        for (source = PA_SOURCE(pa_idxset_first(u->core->sources, &idx));
             source; source = PA_SOURCE(pa_idxset_next(u->core->sources, &idx))) {
            if (strcmp(source_mapping_table[i].inputdevice, source->name) == 0) {
                if (source->update_sample_spec && source->sample_spec.rate != rate) {
                    if (source->state == PA_SOURCE_RUNNING) {
                        pa_log_info("Need to suspend then unsuspend device before changing sampling rate");
                        need_unsuspend = 1;
                    }
                    pa_source_suspend(source, 1, PA_SUSPEND_IDLE);

                    pa_log_info
                        ("update_sample_spec is available for source %s, current sample_spec rate %d, changing to %d",
                         source->name, source->sample_spec.rate, rate);
                    sample_spec = source->sample_spec;
                    sample_spec.rate = rate;
                    source->update_sample_spec(source, &sample_spec);

                    if (need_unsuspend == 1) {
                        pa_source_suspend(source, 0, PA_SUSPEND_IDLE);
                        need_unsuspend = 0;
                    }
                }
            }
        }
    }
#endif
    return 0;
}

static void load_unicast_rtp_module(struct userdata *u)
{
    char *args = NULL;
    pa_assert(u != NULL);
    char audiodbuf[SIZE_MESG_TO_AUDIOD];
    memset(audiodbuf, 0, sizeof(audiodbuf));

/* Request for Unicast RTP
 * Client Provides Destination IP & Port (Optional)
 * Load RTP Module
 * Send Error To AudioD in case RTP Load fails
 */
    if (strcmp(u->connectionType,"unicast")== 0) {
        pa_log("[rtp loading begins for Unicast RTP] [AudioD sent] port = %u ip_addr = %s",
            u->connectionPort,u->destAddress) ;
        if(u->connectionPort < 1 || u->connectionPort > 0xFFFF) {
            args = pa_sprintf_malloc("source=%s destination_ip=%s","rtp.monitor", u->destAddress);
        } else {
            args = pa_sprintf_malloc("source=%s destination_ip=%s port=%d","rtp.monitor",
                u->destAddress, u->connectionPort);
        }
        u->rtp_module = pa_module_load(u->core, "module-rtp-send", args);
    }

    if (args)
        pa_xfree(args);

    if (!u->rtp_module) {
        pa_log("Error loading in module-rtp-send");
        snprintf(audiodbuf, SIZE_MESG_TO_AUDIOD, "t %d %d %s %d", 0, 1, (char *)NULL, 0);
        if(-1 == send(u->newsockfd, audiodbuf, SIZE_MESG_TO_AUDIOD, 0))
            pa_log("Failed to send message to audiod ");
        else
            pa_log("Error in Loading RTP Module message sent to audiod");
        return;
    }
}

static void load_alsa_source(struct userdata *u, int status)
{
    pa_assert(u);
    char *args = NULL;

    pa_log("[alsa source loading begins for Mic Recording] [AudioD sent] cardno = %d capture device number = %d",\
        u->external_soundcard_number, u->external_device_number);
    if (u->alsa_source != NULL) {
        pa_module_unload(u->alsa_source, true);
        u->alsa_source = NULL;
    }
    if (u->external_soundcard_number >= 0 && (1 == status)) {
       args = pa_sprintf_malloc("device=hw:%d,%d mmap=0 source_name=%s fragment_size=4096 tsched=0",\
           u->external_soundcard_number, u->external_device_number, u->deviceName);
    }
    else if (0 == status) {
        struct stat buff = {0};
        // Loading source on card 0.
        if (0 == stat (DEFAULT_SOURCE_0, &buff)) {
            args = pa_sprintf_malloc("device=hw:0,0 mmap=0 source_name=%s fragment_size=4096 tsched=0", u->deviceName);
        }
        //Loading source on card 1.
        else if(0 == stat (DEFAULT_SOURCE_1, &buff)){
            args = pa_sprintf_malloc("device=hw:1,0 mmap=0 source_name=%s fragment_size=4096 tsched=0", u->deviceName);
        }
        else
            pa_log_info("No source element found to load");
    }
    else return;
    if (NULL != args)
        u->alsa_source = pa_module_load(u->core, "module-alsa-source", args);

   if (args)
       pa_xfree(args);

   if (!u->alsa_source) {
       pa_log("Error loading in module-alsa-source");
       return;
    }
    pa_log_info("module-alsa-source loaded");
}

static void load_alsa_sink(struct userdata *u, int status)
{
    pa_assert(u);

    int sink = 0;
    int i = 0;
    char *args = NULL;
    pa_log("[alsa sink loading begins for Usb haedset routing] [AudioD sent] cardno = %d playback device number = %d",\
        u->external_soundcard_number, u->external_device_number);
    if (!u->IsUsbConnected[DISPLAY_ONE])
    {
        args = pa_sprintf_malloc("device=hw:%d,%d mmap=0 sink_name=%s fragment_size=4096 tsched=0",\
            u->external_soundcard_number, u->external_device_number, u->deviceName);
        /*Loading alsa sink with sink_name*/
        u->default1_alsa_sink = pa_module_load(u->core, "module-alsa-sink", args);
        if (NULL == u->default1_alsa_sink)
            pa_log("Error loading in module-alsa-sink with sink_name%s", u->deviceName);
        else
        {
            pa_log_info("module-alsa-sink with sink_name%s loaded successfully", u->deviceName);
            u->IsDisplay1usbSinkLoaded = true;
            u->display1UsbIndex = u->default1_alsa_sink->index;
            pa_log_info("module is loaded with index %u", u->default1_alsa_sink->index);
            u->externalSoundCardNumber[DISPLAY_ONE] = u->external_soundcard_number;
            u->IsUsbConnected[DISPLAY_ONE] = true;

        }
    }
    else if (!u->IsUsbConnected[DISPLAY_TWO] && u->externalSoundCardNumber[DISPLAY_ONE] != u->external_soundcard_number)
    {
        args = pa_sprintf_malloc("device=hw:%d,%d mmap=0 sink_name=%s fragment_size=4096 tsched=0",\
            u->external_soundcard_number, u->external_device_number, u->deviceName);
        /*Loading alsa sink with sink*/
        u->default2_alsa_sink = pa_module_load(u->core, "module-alsa-sink", args);
        if (!u->default2_alsa_sink)
            pa_log("Error loading in module-alsa-sink with sink_name%s", u->deviceName);
        else
        {
            pa_log_info("module-alsa-sink with sink_name:%s display_usb2 loaded successfully", u->deviceName);
            u->IsDisplay2usbSinkLoaded = true;
            u->display2UsbIndex = u->default2_alsa_sink->index;
            pa_log_info("module is loaded with index %u", u->default2_alsa_sink->index);
            u->externalSoundCardNumber[DISPLAY_TWO] = u->external_soundcard_number;
            u->IsUsbConnected[DISPLAY_TWO] = true;
        }
    }
    if (args)
        pa_xfree(args);

    pa_log_info("module-alsa-sink loaded");
}

static void unload_alsa_source(struct userdata *u, int status)
{
    pa_assert(u);

    if (0 == status) {
        if (u->alsa_source == NULL) {
            load_alsa_source(u,0);
            return;
        }
        pa_module_unload(u->alsa_source, true);
        pa_log_info("module-alsa-source unloaded");
        u->alsa_source = NULL;
        load_alsa_source(u,0);
    }
}

static void unload_alsa_sink(struct userdata *u, int status)
{
    pa_assert(u);
    int i = 0;

    pa_log("[alsa sink unloading begins for Usb haedset routing] [AudioD sent] cardno = %d playback device number = %d",\
        u->external_soundcard_number, u->external_device_number);
    if (u->IsUsbConnected[DISPLAY_ONE] && u->externalSoundCardNumber[DISPLAY_ONE] == u->external_soundcard_number)
    {
        pa_log_info("Un-loading alsa sink");
        if (u->IsDisplay1usbSinkLoaded)
            pa_module_unload(u->default1_alsa_sink, TRUE);
        else
            pa_log_info("Display1 usb alsa sink is already unloaded");
        u->IsUsbConnected[DISPLAY_ONE] = false;
        pa_log_info("Set display1 physical sink as null sink");
        u->externalSoundCardNumber[DISPLAY_ONE] = -1;
        u->default1_alsa_sink = NULL;
    }
    if (u->IsUsbConnected[DISPLAY_TWO] && u->externalSoundCardNumber[DISPLAY_TWO] == u->external_soundcard_number)
    {
        pa_log_info("Un-loading alsa sink with sink_name=display_usb2");
        if (u->IsDisplay2usbSinkLoaded)
            pa_module_unload(u->default2_alsa_sink, TRUE);
        else
            pa_log_info("Display2 usb alsa sink is already unloaded");

        u->default2_alsa_sink = NULL;
        u->IsUsbConnected[DISPLAY_TWO] = false;
        u->externalSoundCardNumber[DISPLAY_TWO] = -1;
    }
    pa_log_info("module-alsa-sink un-loaded");
}

static void load_multicast_rtp_module(struct userdata *u)
{
    char *args = NULL;
    pa_assert(u != NULL);
    char audiodbuf[SIZE_MESG_TO_AUDIOD];
    memset(audiodbuf, 0, sizeof(audiodbuf));

/* Request for Multicast RTP
 * Client Provides Destination IP(Optional) & Port (Optional)
 * Load RTP Module
 * Send Error To AudioD in case RTP Load fails
 */
    if (strcmp(u->connectionType,"multicast")== 0) {
        pa_log("[rtp loading begins for Multicast RTP] [AudioD sent] port = %u ip_addr = %s",
            u->connectionPort,u->destAddress) ;
        if(u->connectionPort < 1 || u->connectionPort > 0xFFFF) {
            if(strcmp(u->destAddress,"default") == 0) {
                args = pa_sprintf_malloc("source=%s","rtp.monitor");
            } else {
                args = pa_sprintf_malloc("source=%s destination_ip=%s","rtp.monitor", u->destAddress);
            }
        } else {
            if(strcmp(u->destAddress,"default") == 0){
                args = pa_sprintf_malloc("source=%s port=%d","rtp.monitor", u->connectionPort);
            } else {
                args = pa_sprintf_malloc("source=%s destination_ip=%s port=%d","rtp.monitor",
                    u->destAddress, u->connectionPort);
            }
        }
        u->rtp_module = pa_module_load(u->core, "module-rtp-send", args);
    }

    if (args)
        pa_xfree(args);

    if (!u->rtp_module) {
        pa_log("Error loading in module-rtp-send");
        snprintf(audiodbuf, SIZE_MESG_TO_AUDIOD, "t %d %d %s %d", 0, 1, (char *)NULL, 0);
        if(-1 == send(u->newsockfd, audiodbuf, SIZE_MESG_TO_AUDIOD, 0))
            pa_log("Failed to send message to audiod ");
        else
            pa_log("Error in Loading RTP Module message sent to audiod");
        return;
    }
}

static void unload_rtp_module(struct userdata *u)
{
    pa_assert(u);
    pa_assert(u->rtp_module);

    pa_module_unload(u->rtp_module, true);
    pa_log_info("module-rtp-sink unloaded");
    u->rtp_module = NULL;
}

void send_rtp_connection_data_to_audiod(char *ip,char *port,struct userdata *u) {
    pa_assert(ip);
    pa_assert(port);
    int port_value = atoi(port) ;
    char audiodbuf[SIZE_MESG_TO_AUDIOD];
    memset(audiodbuf, 0, sizeof(audiodbuf));

    pa_log("[send_rtp_connection_data_to_audiod] ip = %s port = %d",ip,port_value);
    snprintf(audiodbuf, SIZE_MESG_TO_AUDIOD, "t %d %d %s %d", 0 , 0, ip, port_value);
    if(-1 == send(u->newsockfd, audiodbuf, SIZE_MESG_TO_AUDIOD, 0))
        pa_log("Failed to send message to audiod ");
    else
        pa_log("Message sent to audiod");
}

static void load_Bluetooth_module(struct userdata *u)
{
    u->IsBluetoothEnabled = true;
    if (NULL == u->btDiscoverModule)
    {
        u->btDiscoverModule = pa_module_load(u->core, "module-bluetooth-discover", NULL);
        char physicalSinkBT[BLUETOOTH_SINK_NAME_SIZE];
        char btSinkInit[BLUETOOTH_SINK_INIT_SIZE] = "bluez_sink.";
        btSinkInit[BLUETOOTH_SINK_INIT_SIZE-1] = '\0';

        memset(physicalSinkBT, '\0', sizeof(physicalSinkBT));
        strncpy(physicalSinkBT, btSinkInit, sizeof(btSinkInit)-1);
        int index = 0;
        while (u->address[index] != '\0')
        {
          if (u->address[index] >= 'a' && u->address[index] <= 'z')
          {
             u->address[index] = u->address[index] - 32;
          }
          index++;
        }
        strcat(physicalSinkBT,u->address);
        for(int index = 0; index < strlen(physicalSinkBT); index++)
        {
            if(physicalSinkBT[index] == ':')
                physicalSinkBT[index] = '_';
        }
        strncpy(u->physicalSinkBT, physicalSinkBT, sizeof(physicalSinkBT)-1);
        if (NULL == u->btDiscoverModule)
            pa_log_info ("%s :module-bluetooth-discover loading failed", __FUNCTION__);
        else
            pa_log_info ("%s :module-bluetooth-discover loaded", __FUNCTION__);
    }
    else
        pa_log_info ("%s :module-bluetooth-discover already loaded", __FUNCTION__);
}

static void unload_BlueTooth_module(struct userdata *u)
{
    u->IsBluetoothEnabled = false;
    if (u->btDiscoverModule)
    {
        pa_log_info("%s : going to unload BT module ", __FUNCTION__);
        pa_module_unload(u->btDiscoverModule, TRUE);
    }
    else
    {
        pa_log_info ("%s :module already unloaded", __FUNCTION__);
    }
    u->btDiscoverModule = NULL;
}


static void load_lineout_alsa_sink(struct userdata *u, int soundcardNo, int  deviceNo, int status, int  islineout)
{
    pa_assert(u);
    int sink = 0;
    char *args = NULL;
    /* Request for lineout loading
    * Load Alsa Sink Module
    * Send Error To AudioD in case Alsa sink Load fails
    */
    pa_log("[alsa sink loading begins for lineout] [AudioD sent] cardno = %d playback device number = %d deviceName = %s",\
        soundcardNo, deviceNo, u->deviceName);
    if (islineout)
    {
        if (u->alsa_sink1 == NULL)
        {
            char *args = NULL;
            args = pa_sprintf_malloc("device=hw:%d,%d mmap=0 sink_name=%s fragment_size=4096 tsched=0", soundcardNo, 0, u->deviceName);
            u->alsa_sink1 = pa_module_load(u->core, "module-alsa-sink", args);
            if (args)
                pa_xfree(args);
            if (!u->alsa_sink1)
            {
                pa_log("Error loading in module-alsa-sink for pcm_output");
                return;
            }
            pa_log_info("module-alsa-sink loaded for pcm_output");
        }
        else if (u->alsa_sink2 == NULL)
        {
            char *args = NULL;
            args = pa_sprintf_malloc("device=hw:%d,%d mmap=0 sink_name=%s fragment_size=4096 tsched=0", soundcardNo, 0, u->deviceName);
            u->alsa_sink2 = pa_module_load(u->core, "module-alsa-sink", args);
            if (args)
                pa_xfree(args);
            if (!u->alsa_sink2)
            {
                pa_log("Error loading in module-alsa-sink for pcm_output1");
                return;
            }
            pa_log_info("module-alsa-sink loaded for pcm_output1");
        }
        else
        {
            pa_log_info("module-alsa-sink already loaded");
        }
    }
    else
    {
        if (u->headphone_sink == NULL)
        {
            char *args = NULL;
            args = pa_sprintf_malloc("device=hw:%d,%d mmap=0 sink_name=%s fragment_size=4096 tsched=0", soundcardNo, 0, u->deviceName);
            u->headphone_sink = pa_module_load(u->core, "module-alsa-sink", args);
            if (args)
                pa_xfree(args);
            if (!u->headphone_sink)
            {
                pa_log("Error loading in module-alsa-sink for pcm_headphone");
                return;
            }
            pa_log_info("module-alsa-sink loaded for pcm_headphone");
        }
    }

    if (args)
        pa_xfree(args);
    pa_log_info("module-alsa-sink loaded");
}


/* Parse a message sent from audiod and invoke
 * requested changes in pulseaudio
 */
static void parse_message(char *msgbuf, int bufsize, struct userdata *u) {
    char cmd;                                           /* all commands must start with this */
    int sinkid ;                /* and they must have a sink to operate on */
    int sourceid;
    pa_log_info("parse_message: %s", msgbuf);
    /* pick it apart with sscanf */
    if (1 == sscanf(msgbuf, "%c", &cmd)) {
        int parm1, parm2, parm3;

        switch (cmd) {

        case 'a':
            {
                char inputdevice[DEVICE_NAME_LENGTH];
                int startsourceid;
                int endsourceid;
                if (4 == sscanf(msgbuf, "%c %d %d %s", &cmd, &startsourceid, &endsourceid, inputdevice))
                {
                    pa_log_info("received source routing for inputdevice:%s startsourceid:%d,\
                    inputdevice:%d", inputdevice, startsourceid, endsourceid);
                    set_source_inputdevice_on_range(u, inputdevice, startsourceid, endsourceid);
                }
                else
                    pa_log_warn("received source routing for inputdevice with invalid params");
            }
            break;

        case 'b':
            {
                /* volume -  B  <value 0 : 65535> ramup/down */
                /* walk list of sink-inputs on this stream and set their volume */
                if (4 == sscanf(msgbuf, "%c %d %d %d", &cmd, &parm1, &parm2, &parm3))
                {
                    parm2 = CLAMP_VOLUME_TABLE(parm2);
                    virtual_sink_input_set_ramp_volume(parm1, parm2, !!parm3, u);
                    pa_log_info("parse_message: Fade command received, requested volume is %d, headphones:%d, fadeIn:%d", parm1, parm2, parm3);
                }
            }
            break;

        case 'd':
            {
                char outputdevice[DEVICE_NAME_LENGTH];
                /* redirect -  D <virtualsink> <physicalsink> when a sink is in running state*/
                if (3 == sscanf(msgbuf, "%c %d %s", &cmd, &sinkid, outputdevice))
                {
                    /* walk list of sink-inputs on this stream and set
                    * their output sink */
                    virtual_sink_input_move_outputdevice(sinkid, outputdevice, u);
                    pa_log_info("parse_message: virtual_sink_input_move_outputdevice sink is %d, output device %s",\
                        sinkid, outputdevice);
                }
            }
            break;

        case 'e':
            {
                char inputdevice[DEVICE_NAME_LENGTH];
                /* redirect -  E <virtualsource> <physicalsink> when a source is in running state*/
                if (4 == sscanf(msgbuf, "%c %d %s %d", &cmd, &sourceid, inputdevice))
                {
                    /* walk list of sink-inputs on this stream and set
                    * their output sink */
                    virtual_source_output_move_inputdevice(sourceid, inputdevice, u);
                    pa_log_info("parse_message: virtual_source_output_move_inputdevice source is %d and redirect to %s",\
                    sourceid, inputdevice);
                }
            }
            break;

        case 'f':
            {
                /* volume -  V <source> <value 0 : 65535> */
                int ramp;
                int sourceId;
                parm2 = 0;
                pa_log_info("parse_message: %.16s",msgbuf);
                if (4 == sscanf(msgbuf, "%c %d %d %d", &cmd, &sourceId, &parm1, &ramp))
                {
                    /* walk list of sink-inputs on this stream and set
                    * their volume */
                    parm2 = CLAMP_VOLUME_TABLE(parm2);
                    if (!ramp)
                        virtual_source_input_set_volume(sourceId, parm1, parm2, u);
                    /*else
                        virtual_source_input_set_volume_with_ramp(sourceId, parm1, parm2, u);*/
                    pa_log_info("parse_message: volume command received, sourceId is %d, requested volume is %d, volumetable:%d",\
                    sourceId, parm1, parm2);
                }
            }
            break;

        case 'g':
            {
                pa_log_info ("received unload command for RTP module from AudioD");
                unload_rtp_module(u);
            }
            break;

        case 'h':
            {
                int sourceid = -1;
                /* mute source -  H <source> <mute 0 : 1> */
                if (4 == sscanf(msgbuf, "%c %d %d %d", &cmd, &sourceid, &parm1, &parm2))
                {
                    /* walk list of sink-inputs on this stream and set
                    * their volume */
                    pa_log_info("parse_message: source mute command received, source is %d, mute %d",\
                    sourceid, parm1);
                    virtual_source_set_mute(sourceid, parm1, u);
                }
            }
            break;

        case 'i':
            {
                int status = 0;
                int soundcardNo;
                int deviceNo;
                char devicename[50];
                int islineout;
                if (6 == sscanf(msgbuf, "%c %d %d %d %d %s", &cmd, &soundcardNo,
                    &deviceNo, &status, &islineout, u->deviceName))
                {
                    pa_log_info("received lineout loading cmd from Audiod with status:%d %s", status, u->deviceName);
                    if (1 == status) {
                        load_lineout_alsa_sink(u, soundcardNo, deviceNo, status, islineout);
                    }
                }
            }
            break;

        case 'j':
            {
                int status = 0;
                if (5 == sscanf(msgbuf, "%c %d %d %d %s", &cmd, &u->external_soundcard_number, &u->external_device_number, &status, u->deviceName))
                {
                    pa_log_info("received mic recording cmd from Audiod");
                    if (1 == status)
                        load_alsa_source(u, status);
                    else
                        unload_alsa_source(u, status);
                }
            }
            break;

        case 'k':
            {
                /* Mute/Un-mute respective display */
                char outputdevice[DEVICE_NAME_LENGTH];
                bool mute = false;
                if (3 == sscanf(msgbuf, "%c %d %s", &cmd, &mute, outputdevice))
                    sink_set_master_mute(outputdevice, mute, u);
            }
            break;

        case 'l':
            {
                if (4 == sscanf(msgbuf, "%c %d %18s %5s", &cmd, &parm1, u->address, u->btProfile))
                {
                    /* walk list of sink-inputs on this stream and set
                    * their output sink */
                    pa_log_info("Bluetooth connected address %s", u->address);
                }
                load_Bluetooth_module(u);
            }
            break;

        case 'm':
            {
                /* mute -  M <sink> <state true : false> */
                bool mute = false;
                if (4 == sscanf(msgbuf, "%c %d %d %d", &cmd, &sinkid, &mute, &parm2))
                {
                    /* set the mute status for the sink*/
                    virtual_sink_input_set_mute(sinkid, mute, u);
                    pa_log_info
                        ("parse_message: mute command received, sink is %d, muteStatus is %d",
                        sinkid, (int)mute);
                }
            }
            break;

        case 'n':
            {
                /* set volume on respective display */
                char outputdevice[DEVICE_NAME_LENGTH];
                int volume = 0;
                if (3 == sscanf(msgbuf, "%c %d %s", &cmd, &volume, outputdevice))
                    sink_set_master_volume(outputdevice, volume, u);

            }
            break;

        case 'O':
            {
                pa_log_info ("received command to set/reset A2DP source");
                if (2 == sscanf(msgbuf, "%c %d", &cmd, &u->a2dpSource))
                    pa_log_info ("successfully set/reset A2DP source");
            }
            break;

        case 'o':
            {
                char outputdevice[DEVICE_NAME_LENGTH];
                int startsinkid;
                int endsinkid;
                if (4 == sscanf(msgbuf, "%c %d %d %s", &cmd, &startsinkid, &endsinkid, outputdevice))
                {
                    pa_log_info("received sink routing for outputdevice: %s startsinkid:%d, endsinkid:%d",\
                        outputdevice, startsinkid, endsinkid);
                    set_sink_outputdevice_on_range(u, outputdevice, startsinkid, endsinkid);
                }
                else
                    pa_log_warn("received sink routing for outputdevice with invalid params");
            }
            break;

        case 'q':
            {
                char outputdevice[DEVICE_NAME_LENGTH];
                int sinkid;
                if (3 == sscanf(msgbuf, "%c %s %d", &cmd, outputdevice, &sinkid))
                {
                    pa_log_info("received sink routing for outputdevice: %s sinkid:%d",\
                        outputdevice, sinkid);
                    set_sink_outputdevice(u, outputdevice, sinkid);
                }
            }
            break;

        case 'r':
            {
                /* ramp -  R <sink> <volume 0 : 100> */
                if (4 == sscanf(msgbuf, "%c %d %d %d", &cmd, &sinkid, &parm1, &parm2)) {
                    /* walk list of sink-inputs on this stream and set
                    * their volumeramp parms
                    */
                    parm2 = CLAMP_VOLUME_TABLE(parm2);
                    virtual_sink_input_set_ramp_volume(sinkid, parm1, parm2, u);
                    pa_log_info
                        ("parse_message: ramp command received, sink is %d, volumetoset:%d, headphones:%d",
                        sinkid, parm1, parm2);
                }
            }
            break;

        case 's':
            {
                /* suspend -  s */
                if (4 == sscanf(msgbuf, "%c %d %d %d", &cmd, &sinkid, &parm1, &parm2)) {
                    /* System is going to sleep, so suspend active modules */
                    if (-1 == sink_suspend_request(u))
                        pa_log_info("suspend request failed: %s", strerror(errno));
                    pa_log_info("parse_message: suspend command received");
                }
            }
            break;

        case 't':
            {
                pa_log_info("received rtp load cmd from Audiod");
                if (5 == sscanf(msgbuf, "%c %d %10s %28s %u", &cmd, &sinkid, u->connectionType, u->destAddress,&u->connectionPort)) {
                    pa_log_info ("parse_message:received command t FOR RTP module port = %lu",u->connectionPort);
                    if(strcmp(u->connectionType,"unicast") == 0)
                        load_unicast_rtp_module(u);
                    else if (strcmp(u->connectionType,"multicast") == 0)
                        load_multicast_rtp_module(u);
                }
            }
            break;

        case 'u':
            {
                unload_BlueTooth_module(u);
            }
            break;

        case 'v':
            {
                /* volume -  V <sink> <value 0 : 65535> */
                if (4 == sscanf(msgbuf, "%c %d %d %d", &cmd, &sinkid, &parm1, &parm2)) {
                    /* walk list of sink-inputs on this stream and set
                    * their volume */
                    parm2 = CLAMP_VOLUME_TABLE(parm2);
                    virtual_sink_input_set_volume(sinkid, parm1, parm2, u);
                    pa_log_info("parse_message: volume command received, sink is %d, requested volume is %d, headphones:%d",\
                        sinkid, parm1, parm2);
                }
            }
            break;

        case 'x':
            {
                /* update sample rate -  x */
                if (4 == sscanf(msgbuf, "%c %d %d %d", &cmd, &sinkid, &parm1, &parm2)) {
                    if (-1 == update_sample_spec(u, parm1))
                        pa_log_info("suspend request failed: %s", strerror(errno));
                    pa_log_info("parse_message: update sample spec command received");
                }
            }
            break;

        case 'y':
            {
                char inputdevice[DEVICE_NAME_LENGTH];
                int sourceId;
                if (3 == sscanf(msgbuf, "%c %s %d", &cmd, inputdevice, &sourceId))
                {
                    pa_log_info("received Source routing for inputdevice: %s sourceId:%d",\
                        inputdevice, sourceId);
                    set_source_inputdevice(u, inputdevice, sourceId);
                }
            }
            break;

        case 'z':
            {
                int status = 0;
                if (5 == sscanf(msgbuf, "%c %d %d %d %s", &cmd, &u->external_soundcard_number, &u->external_device_number, &status, u->deviceName))
                {
                    pa_log_info("received usb headset routing cmd from Audiod");
                    if (1 == status) {
                        load_alsa_sink(u, status);
                    }
                    else
                        unload_alsa_sink(u, status);
                }
            }
            break;

        case '2':
            {
                int startSinkId;
                int endSinkId;
                if (3 == sscanf(msgbuf, "%c %d %d", &cmd, &startSinkId, &endSinkId))
                {
                    pa_log_info("received default sink routing for startSinkId:%d endSinkId:%d",\
                        startSinkId, endSinkId);
                    set_default_sink_routing(u, startSinkId, endSinkId);
                }
            }
            break;

        case '3':
            {
                int startSourceId;
                int endSourceId;
                if (3 == sscanf(msgbuf, "%c %d %d", &cmd, &startSourceId, &endSourceId))
                {
                    pa_log_info("received default source routing for startSourceId:%d endSourceId:%d",\
                        startSourceId, endSourceId);
                    set_default_source_routing(u, startSourceId, endSourceId);
                }
            }
            break;

        case '5':
            {
                char device[30];
                int mute;
                pa_log_info("received setting %s", msgbuf);
                if (3 == sscanf(msgbuf, "%c %s %d", &cmd, device, &mute))
                {
                    pa_log_info("muting phyiscal sink %s, mute value = %d", device, mute);
                    source_set_master_mute(device, mute, u);
                }
            }
            break;

        default:
            pa_log_info("parse_message: unknown command received");
            break;
        }
    }
}



/* pa_io_event_cb_t - IO event handler for socket,
 * this will create connections and assign an
 * appropriate IO event handler */

static void handle_io_event_socket(pa_mainloop_api * ea, pa_io_event * e, int fd, pa_io_event_flags_t events, void *userdata) {
    struct userdata *u = userdata;
    int itslen;
    int sink;
    int source;
    char audiodbuf[SIZE_MESG_TO_AUDIOD];

    pa_assert(u);
    pa_assert(fd == u->sockfd);

    itslen = SUN_LEN(&u->name);

    if (events & PA_IO_EVENT_NULL) {
        pa_log_info("handle_io_event_socket PA_IO_EVENT_NULL received");
    }
    if (events & PA_IO_EVENT_INPUT) {
        /* do we have a connection on our socket yet? */
        if (-1 == u->newsockfd) {
            if (-1 == (u->newsockfd = accept(u->sockfd, &(u->name), &itslen))) {
                pa_log_info("handle_io_event_socket could not create new connection on socket:%s", strerror(errno));
            }
            else {
                /* create new io handler to deal with data send to this connection */
                u->connev =
                    u->core->mainloop->io_new(u->core->mainloop, u->newsockfd,
                                              PA_IO_EVENT_INPUT |
                                              PA_IO_EVENT_HANGUP | PA_IO_EVENT_ERROR, handle_io_event_connection, u);
                u->connectionactive = true; /* flag that we have an active connection */

                /* Tell audiod how many sink of each category is opened */
                for (sink = eVirtualSink_First; sink <= eVirtualSink_Last; sink++) {
                    if (u->audiod_sink_input_opened[sink] > 0) {
                        sprintf(audiodbuf, "O %d %d", sink, u->audiod_sink_input_opened[sink]);
                        if (-1 == send(u->newsockfd, audiodbuf, SIZE_MESG_TO_AUDIOD, 0))
                            pa_log("handle_io_event_socket: send failed: %s", strerror(errno));
                        else
                            pa_log_info
                                ("handle_io_event_socket: stream count for sink %d (%d)",
                                 sink, u->audiod_sink_input_opened[sink]);
                    }
                }

                /* Tell audiod how many source of each category is opened */
                for (source = eVirtualSource_First; source <= eVirtualSource_Last; source++) {
                    if (u->audiod_source_output_opened[source] > 0) {
                        sprintf(audiodbuf, "I %d %d", source, u->audiod_source_output_opened[source]);
                        if (-1 == send(u->newsockfd, audiodbuf, SIZE_MESG_TO_AUDIOD, 0))
                            pa_log("handle_io_event_socket: send failed: %s", strerror(errno));
                        else
                            pa_log_info
                                ("handle_io_event_socket: stream count for source %d (%d)",
                                 source, u->audiod_source_output_opened[source]);
                    }
                }
            }
        }
        else
            pa_log("handle_io_event_socket could not create new connection on socket");
    }
}

/* pa_io_event_cb_t - IO event handler for socket
 * connections.  We enforce a single connection to the
 * client (audiod).  This routine will createlisten on
 * the socket and parse and act upon messages sent to
 * the socket connection */
static void handle_io_event_connection(pa_mainloop_api * ea, pa_io_event * e, int fd, pa_io_event_flags_t events, void *userdata) {
    struct userdata *u = userdata;
    char buf[SIZE_MESG_TO_PULSE];
    int bytesread;

    pa_assert(u);
    pa_assert(fd == u->newsockfd);

    if (events & PA_IO_EVENT_NULL) {
        pa_log_info("handle_io_event_connection PA_IO_EVENT_NULL received");
    }
    if (events & PA_IO_EVENT_INPUT) {
        if (-1 == (bytesread = recv(u->newsockfd, buf, SIZE_MESG_TO_PULSE, 0))) {
            pa_log_info("handle_io_event_connection Error in recv (%d): %s ", errno, strerror(errno));
        }
        else {
            if (bytesread != 0) { /* the socket connection will return zero bytes on EOF */
                parse_message(buf, SIZE_MESG_TO_PULSE, u);
            }
        }
    }
    if (events & PA_IO_EVENT_OUTPUT) {
        pa_log_info("handle_io_event_connection PA_IO_EVENT_OUTPUT received");
    }
    if (events & PA_IO_EVENT_HANGUP) {
        pa_log_info("handle_io_event_connection PA_IO_EVENT_HANGUP received");
        pa_log_info("handle_io_event_connection Socket is being closed");
        /* remove ourselves from the IO list on the main loop */
        u->core->mainloop->io_free(u->connev);

        /* tear down the connection */
        if (-1 == shutdown(u->newsockfd, SHUT_RDWR)) {
            pa_log_info("Error in shutdown (%d):%s", errno, strerror(errno));
        }
        if (-1 == close(u->newsockfd)) {
            pa_log_info("Error in close (%d):%s", errno, strerror(errno));
        }

        /* reset vars in userdata, allows another connection to be rebuilt */
        u->connectionactive = false;
        u->connev = NULL;
        u->newsockfd = -1;
    }
    if (events & PA_IO_EVENT_ERROR)
        pa_log_info("handle_io_event_connection PA_IO_EVENT_ERROR received");
}

static int make_socket (struct userdata *u) {

    int path_len;

    u->sockfd = -1;
    u->newsockfd = -1;          /* set to -1 to indicate no connection */

    /* create a socket for ipc with audiod the policy manager */
    path_len = strlen(PALMAUDIO_SOCK_NAME);
    _MEM_ZERO(u->name);
    u->name.sun_family = AF_UNIX;

    u->name.sun_path[0] = '\0'; /* this is what says "use abstract" */
    path_len++;                 /* Account for the extra nul byte added to the start of sun_path */

    if (path_len > _MAX_NAME_LEN) {
        pa_log("%s: Path name is too long '%s'\n", __FUNCTION__, strerror(errno));
    }

    strncpy(&u->name.sun_path[1], PALMAUDIO_SOCK_NAME, path_len);

    /* build the socket */
    if (-1 == (u->sockfd = socket(AF_UNIX, SOCK_STREAM, 0))) {
        pa_log("Error in socket (%d) ", errno);
        goto fail;
    }

    /* bind it to a name */
    if (-1 ==
        bind(u->sockfd, (struct sockaddr *) &(u->name), _NAME_STRUCT_OFFSET(struct sockaddr_un, sun_path) + path_len)) {
        pa_log("Error in bind (%d) ", errno);
        goto fail;
    }

    if (-1 == listen(u->sockfd, 5)) {
        pa_log("Error in listen (%d) ", errno);
        goto fail;
    }

    u->connectionactive = FALSE;
    u->sockev = NULL;
    u->connev = NULL;

    /* register an IO event handler for the socket, deal
     * with new connections in this handler */
    u->sockev =
        u->core->mainloop->io_new(u->core->mainloop, u->sockfd,
                                  PA_IO_EVENT_INPUT | PA_IO_EVENT_HANGUP |
                                  PA_IO_EVENT_ERROR, handle_io_event_socket, u);
    return 0;
fail:
    return -1;
}

static void connect_to_hooks(struct userdata *u) {

    pa_assert(u);

    /* bit early than module-stream-restore:
     * module-stream-restore will try to set the sink if the stream doesn't comes with device set
     * Let palm-policy do the routing before module-stream-restore
     */
    u->sink_input_new_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_INPUT_NEW],
                        PA_HOOK_EARLY - 10, (pa_hook_cb_t) route_sink_input_new_hook_callback, u);

    u->sink_input_fixate_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_INPUT_FIXATE],
                        PA_HOOK_EARLY - 10, (pa_hook_cb_t) route_sink_input_fixate_hook_callback, u);

    u->source_output_new_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_NEW],
                        PA_HOOK_EARLY - 10, (pa_hook_cb_t) route_source_output_new_hook_callback, u);

    u->source_output_fixate_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_FIXATE],
                        PA_HOOK_EARLY - 10, (pa_hook_cb_t) route_source_output_fixate_hook_callback, u);

    u->source_output_put_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_PUT],
                        PA_HOOK_EARLY - 10, (pa_hook_cb_t) route_source_output_put_hook_callback, u);

    u->source_output_state_changed_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_STATE_CHANGED], PA_HOOK_EARLY - 10, (pa_hook_cb_t)
                        route_source_output_state_changed_hook_callback, u);

    u->sink_input_put_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_INPUT_PUT],
                        PA_HOOK_EARLY - 10, (pa_hook_cb_t) route_sink_input_put_hook_callback, u);

    u->sink_input_unlink_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK],
                        PA_HOOK_EARLY - 10, (pa_hook_cb_t) route_sink_input_unlink_hook_callback, u);

    u->source_output_unlink_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_UNLINK],
                        PA_HOOK_EARLY, (pa_hook_cb_t) route_source_output_unlink_hook_callback, u);
    u->sink_input_state_changed_hook_slot =
        pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_INPUT_STATE_CHANGED], PA_HOOK_EARLY - 10, (pa_hook_cb_t)
                        route_sink_input_state_changed_hook_callback, u);
    u->sink_input_move_finish = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_FINISH], PA_HOOK_EARLY,
                        (pa_hook_cb_t)route_sink_input_move_finish_cb, u);

    u->sink_unlink_post = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_UNLINK_POST], PA_HOOK_EARLY,
                        (pa_hook_cb_t)route_sink_unlink_post_cb, u);

    u->source_unlink_post = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SOURCE_UNLINK_POST], PA_HOOK_EARLY,
                        (pa_hook_cb_t)route_source_unlink_post_cb, u);
    u->module_unload_hook_slot = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_MODULE_UNLINK],
                        PA_HOOK_EARLY, (pa_hook_cb_t)module_unload_subscription_callback, u);

    u->module_load_hook_slot = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_MODULE_NEW],
                      PA_HOOK_EARLY, (pa_hook_cb_t)module_load_subscription_callback, u);

    u->sink_load_hook_slot = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_NEW],
                      PA_HOOK_EARLY, (pa_hook_cb_t)sink_load_subscription_callback, u);

    u->sink_unlink = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_UNLINK], PA_HOOK_EARLY,
                        (pa_hook_cb_t)route_sink_unlink_cb, u);
}

static void disconnect_hooks(struct userdata *u) {

    pa_assert(u);

    if(u->sink_input_new_hook_slot)
        pa_hook_slot_free(u->sink_input_new_hook_slot);

    if (u->sink_input_fixate_hook_slot)
        pa_hook_slot_free(u->sink_input_fixate_hook_slot);

    if (u->sink_input_put_hook_slot)
        pa_hook_slot_free(u->sink_input_put_hook_slot);

    if (u->sink_input_state_changed_hook_slot)
        pa_hook_slot_free(u->sink_input_state_changed_hook_slot);

    if (u->sink_input_unlink_hook_slot)
        pa_hook_slot_free(u->sink_input_unlink_hook_slot);

    if (u->source_output_new_hook_slot)
        pa_hook_slot_free(u->source_output_new_hook_slot);

    if (u->source_output_fixate_hook_slot)
        pa_hook_slot_free(u->source_output_fixate_hook_slot);

    if (u->source_output_put_hook_slot)
        pa_hook_slot_free(u->source_output_put_hook_slot);

    if (u->source_output_state_changed_hook_slot)
        pa_hook_slot_free(u->source_output_state_changed_hook_slot);

    if (u->source_output_unlink_hook_slot)
        pa_hook_slot_free(u->source_output_unlink_hook_slot);

    if (u->sink_input_move_finish)
        pa_hook_slot_free(u->sink_input_move_finish);

    if (u->sink_new)
        pa_hook_slot_free(u->sink_new);

    if (u->sink_unlink)
        pa_hook_slot_free(u->sink_unlink);
}

/* entry point for the module*/
int pa__init(pa_module * m) {
    struct userdata *u = NULL;
    int i;

    pa_assert(m);
    u = pa_xnew(struct userdata, 1);

    u->core = m->core;
    u->module = m;
    m->userdata = u;

    u->alsa_sink1 = NULL;
    u->alsa_sink2 = NULL;
    u->headphone_sink = NULL;

    PA_LLIST_HEAD_INIT(struct sinkinputnode, u->sinkinputnodelist);
    PA_LLIST_HEAD_INIT(struct sourceoutputnode, u->sourceoutputnodelist);

    connect_to_hooks(u);

    /* copy the default sink mapping */
    for (i = 0; i < eVirtualSink_Count; i++) {
        u->sink_mapping_table[i].virtualsinkname = virtualsinkmap[i].virtualsinkname;
        u->sink_mapping_table[i].virtualsinkidentifier = virtualsinkmap[i].virtualsinkidentifier;
        strncpy(u->sink_mapping_table[i].outputdevice, virtualsinkmap[i].outputdevice, DEVICE_NAME_LENGTH);
        u->sink_mapping_table[i].volumetable = virtualsinkmap[i].volumetable;
        u->sink_mapping_table[i].volume = virtualsinkmap[i].volume;
        u->sink_mapping_table[i].ismuted = virtualsinkmap[i].ismuted;

        // Clear audiod sink opened count
        u->audiod_sink_input_opened[i] = 0;
    }
    u->n_sink_input_opened = 0;

    /* copy the default source mapping */
    for (i = 0; i < eVirtualSource_Count; i++) {
        u->source_mapping_table[i].virtualsourcename = virtualsourcemap[i].virtualsourcename;
        u->source_mapping_table[i].virtualsourceidentifier = virtualsourcemap[i].virtualsourceidentifier;
        strncpy(u->source_mapping_table[i].inputdevice, virtualsourcemap[i].inputdevice, DEVICE_NAME_LENGTH);
        u->source_mapping_table[i].volume = virtualsourcemap[i].volume;
        u->source_mapping_table[i].ismuted = virtualsourcemap[i].ismuted;
        u->source_mapping_table[i].volumetable = virtualsourcemap[i].volumetable;

        // Clear audiod sink opened count
        u->audiod_source_output_opened[i] = 0;
    }
    u->n_source_output_opened = 0;
    u->media_type = edefaultapp;

    u->rtp_module = NULL;
    u->alsa_source = NULL;
    u->default1_alsa_sink = NULL;
    u->default2_alsa_sink = NULL;
    u->IsUsbConnected[DISPLAY_ONE] = false;
    u->IsUsbConnected[DISPLAY_TWO] = false;
    u->externalSoundCardNumber[DISPLAY_ONE] = -1;
    u->externalSoundCardNumber[DISPLAY_TWO] = -1;
    u->destAddress = (char *)pa_xmalloc0(RTP_IP_ADDRESS_STRING_SIZE);
    u->connectionType = (char *)pa_xmalloc0(RTP_CONNECTION_TYPE_STRING_SIZE);
    u->connectionPort = 0;
    u->deviceName = (char *)pa_xmalloc0(DEVICE_NAME_SIZE);
    u->callback_deviceName = (char *)pa_xmalloc0(DEVICE_NAME_SIZE);

    u->btDiscoverModule = NULL;
    u->IsBluetoothEnabled = false;
    u->IsDisplay1usbSinkLoaded = false;
    u->IsDisplay2usbSinkLoaded = false;
    u->display1UsbIndex = 0;
    u->display1UsbIndex = 0;
    u->a2dpSource = 0;

    return make_socket(u);

  fail:
    return -1;
}

/* callback for stream creation */
static pa_hook_result_t route_sink_input_new_hook_callback(pa_core * c, pa_sink_input_new_data * data,
                                                           struct userdata *u) {
    int i, sink_index = edefaultapp;
    pa_sink *sink = NULL;
    pa_proplist *type = NULL;

    pa_assert(data);
    pa_assert(u);
    pa_assert(c);

    type = pa_proplist_new();

    if (data->sink == NULL) {
        /* redirect everything to the default application stream */
        pa_log_info("THE DEFAULT DEVICE WAS USED TO CREATE THIS STREAM - PLEASE CATEGORIZE USING A VIRTUAL STREAM");
        char *si_type = pa_proplist_gets(data->proplist, "media.role");
        if ((si_type) && (0 == strncmp(si_type, "music", 5)))
        {
            pa_proplist_sets(type, "media.type", "btstream");
            pa_proplist_update(data->proplist, PA_UPDATE_MERGE, type);
            char *si_type = pa_proplist_gets(data->proplist, "media.type");
            sink = pa_namereg_get(c, "btstream", PA_NAMEREG_SINK);
            pa_assert(sink != NULL);
            data->sink = sink;
            sink = NULL;
            sink_index = ebtstream ;
            pa_log_info("A2DP source media type %s sink-name %s", si_type, data->sink->name);

        }
        if ((si_type) && (0 == strncmp(si_type, "phone", 5)))
        {
            pa_proplist_sets(type, "media.type", "btcall");
            pa_proplist_update(data->proplist, PA_UPDATE_MERGE, type);
            char *si_type = pa_proplist_gets(data->proplist, "media.type");
            sink = pa_namereg_get(c, "btcall", PA_NAMEREG_SINK);
            pa_assert(sink != NULL);
            data->sink = sink;
            sink = NULL;
            sink_index = ebtcall;
            pa_log_info("HFP call  media type %s sink-name %s", si_type, data->sink->name);

        }
        else
        {
            sink_index = edefaultapp;
            pa_proplist_sets(type, "media.type", "pdefaultapp");
            pa_proplist_update(data->proplist, PA_UPDATE_MERGE, type);
            sink = pa_namereg_get(c, "pdefaultapp", PA_NAMEREG_SINK);
            pa_assert(sink != NULL);
            data->sink = sink;
            sink = NULL;
        }
        sink = pa_namereg_get(c, u->sink_mapping_table[sink_index].outputdevice, PA_NAMEREG_SINK);
        if (sink && PA_SINK_IS_LINKED(pa_sink_get_state(sink))){
            pa_sink_input_new_data_set_sink(data, sink, TRUE);
        }
    }

    else if ((data->sink != NULL) && sink_index == edefaultapp && strstr(data->sink->name, PCM_SINK_NAME)) {
        pa_log_info("data->sink->name : %s",data->sink->name);
        pa_proplist_sets(type, "media.type", virtualsinkmap[u->media_type].virtualsinkname);
        pa_proplist_update(data->proplist, PA_UPDATE_MERGE, type);

        sink = pa_namereg_get(c, data->sink->name, PA_NAMEREG_SINK);
        if (sink && PA_SINK_IS_LINKED(pa_sink_get_state(sink)))
            pa_sink_input_new_data_set_sink(data, sink, TRUE);
    }

    else if ((data->sink != NULL) && sink_index == edefaultapp && strstr(data->sink->name, PCM_HEADPHONE_SINK)) {
        pa_log_info("data->sink->name : %s",data->sink->name);
        pa_proplist_sets(type, "media.type", virtualsinkmap[u->media_type].virtualsinkname);
        pa_proplist_update(data->proplist, PA_UPDATE_MERGE, type);

        sink = pa_namereg_get(c, data->sink->name, PA_NAMEREG_SINK);
        if (sink && PA_SINK_IS_LINKED(pa_sink_get_state(sink)))
            pa_sink_input_new_data_set_sink(data, sink, TRUE);
    }

    else if ((NULL != data->sink) && sink_index == edefaultapp && (strstr (data->sink->name,"bluez_")))
    {
        pa_log_info("data->sink->name : %s",data->sink->name);
        pa_proplist_sets(type, "media.type", virtualsinkmap[u->media_type].virtualsinkname);
        pa_proplist_update(data->proplist, PA_UPDATE_MERGE, type);
        sink = pa_namereg_get(c, data->sink->name, PA_NAMEREG_SINK);
        if (sink && PA_SINK_IS_LINKED(pa_sink_get_state(sink)))
        {
            pa_sink_input_new_data_set_sink(data, sink, TRUE);
        }
    }
    else
    {
        pa_log_debug("new stream is opened with sink name : %s", data->sink->name);
        pa_proplist_sets(type, "media.type", data->sink->name);
        pa_proplist_update(data->proplist, PA_UPDATE_MERGE, type);
        for (i = eVirtualSink_First; i < eVirtualSink_Count; i++) {
            if (pa_streq(data->sink->name, u->sink_mapping_table[i].virtualsinkname)) {
                pa_log_debug("found virtual sink index on virtual sink %d, name %s, index %d",\
                    virtualsinkmap[i].virtualsinkidentifier, data->sink->name, i);
                u->media_type = i;
                break;
            }
        }


        sink = pa_namereg_get(c, u->sink_mapping_table[i].outputdevice, PA_NAMEREG_SINK);
        pa_log_info("routing to device:%s", u->sink_mapping_table[i].outputdevice);
        if (pa_streq(data->sink->name, u->sink_mapping_table[i].virtualsinkname)) {
            pa_assert(sink != NULL);
            data->sink = sink;
            sink = NULL;
            if (sink && PA_SINK_IS_LINKED(pa_sink_get_state(sink)))
                pa_sink_input_new_data_set_sink(data, sink, FALSE);
        }
    }
    if (type)
        pa_proplist_free(type);

    return PA_HOOK_OK;
}

static pa_hook_result_t route_sink_input_fixate_hook_callback(pa_core * c, pa_sink_input_new_data * data,
                                                              struct userdata *u) {

    int i, sink_index, volumetoset, volumetable;
    int ismute;
    const char *type;
    struct pa_cvolume cvolume;

    pa_assert(c);
    pa_assert(data);
    pa_assert(u);

    type = pa_proplist_gets(data->proplist, "media.type");

    for (i = 0; i < eVirtualSink_Count; i++) {
        if (pa_streq(type, u->sink_mapping_table[i].virtualsinkname)) {
            sink_index = i;
            break;
        }
    }
    pa_assert(sink_index >= eVirtualSink_First);
    pa_assert(sink_index <= eVirtualSink_Last);

    ismute = u->sink_mapping_table[sink_index].ismuted;
    pa_log_debug("setting mute %s for stream type %s", (ismute?"TRUE":"FALSE"), type);
    pa_sink_input_new_data_set_muted(data, ismute);

    volumetable = u->sink_mapping_table[sink_index].volumetable;
    volumetoset = pa_sw_volume_from_dB(_mapPercentToPulseRamp[volumetable]
                                       [u->sink_mapping_table[sink_index].volume]);

    pa_log_debug("Setting volume(%d) for stream type(%s)", volumetoset, type);

    pa_cvolume_set(&cvolume, data->channel_map.channels, volumetoset);
    pa_sink_input_new_data_set_volume(data, &cvolume);

    return PA_HOOK_OK;
}

static pa_hook_result_t route_sink_input_put_hook_callback(pa_core * c, pa_sink_input * data, struct userdata *u) {

    struct sinkinputnode *si_data = NULL;
    const char *si_type;
    pa_sink_input_state_t state;
    int i;

    pa_assert(c);
    pa_assert(u);

    si_data = pa_xnew0(struct sinkinputnode, 1);

    si_data->sinkinput = data;
    si_data->sinkinputidx = data->index;
    si_data->paused = true;
    si_data->virtualsinkid = -1;

    si_type = pa_proplist_gets(data->proplist, "media.type");
    for (i = 0; i < eVirtualSink_Count; i++) {
        if (pa_streq(si_type, u->sink_mapping_table[i].virtualsinkname)) {
            si_data->virtualsinkid = u->sink_mapping_table[i].virtualsinkidentifier;
            break;
        }
    }
    if (si_data->virtualsinkid == -1)
        return PA_HOOK_OK;
    pa_assert(si_data->virtualsinkid != -1);
    pa_assert(si_data->virtualsinkid >= eVirtualSink_First);
    pa_assert(si_data->virtualsinkid <= eVirtualSink_Last);

    u->n_sink_input_opened++;
    PA_LLIST_PREPEND(struct sinkinputnode, u->sinkinputnodelist, si_data);

    state = pa_sink_input_get_state(data);

    /* send notification to audiod only if sink_input is in uncorked state */
    if (state == PA_SINK_INPUT_CORKED) {
        //si_data->paused = true; already done as part of init
        pa_log_debug("stream type (%s) is opened in corked state", si_type);
        return PA_HOOK_OK;
    }

    /* notify audiod of stream open */
    if (u->connectionactive && u->connev) {
        char audiobuf[SIZE_MESG_TO_AUDIOD];
        int ret;
        /* we have a connection send a message to audioD */
        si_data->paused = false;
        /* Currently setsw('s') mode is not supported in TV audiod
        change msg signal back to 's' when platform audiod is merged*/
        sprintf(audiobuf, "o %d %d", si_data->virtualsinkid, si_data->sinkinputidx);
        u->audiod_sink_input_opened[si_data->virtualsinkid]++;

        ret = send(u->newsockfd, audiobuf, SIZE_MESG_TO_AUDIOD, 0);
        if (-1 == ret)
            pa_log("send() failed: %s", strerror(errno));
        else
            pa_log_info("sent playback stream open message to audiod");
    }
    return PA_HOOK_OK;
}

static pa_hook_result_t route_source_output_new_hook_callback(pa_core * c, pa_source_output_new_data * data,
                                                              struct userdata *u) {
    int i, source_index = erecord;
    pa_proplist *stream_type;
    pa_proplist *a;
    char *port,*dest_ip,*prop_name;
    pa_assert(data);
    pa_assert(u);
    pa_assert(c);

    prop_name = pa_strnull(pa_proplist_gets(data->proplist, PA_PROP_MEDIA_NAME));

    if(!strcmp(prop_name,"RTP Monitor Stream")) {
        port = pa_strnull(pa_proplist_gets(data->proplist, "rtp.port"));
        dest_ip = pa_strnull(pa_proplist_gets(data->proplist, "rtp.destination"));
        send_rtp_connection_data_to_audiod(dest_ip,port,u) ;
    }

    if (data->source == NULL) {
        /* redirect everything to the default application stream */
        pa_log("THE DEFAULT DEVICE WAS USED TO CREATE THIS STREAM - PLEASE CATEGORIZE USING A VIRTUAL STREAM");
    }
    else {

        if (strstr(data->source->name, "monitor")) {
            pa_log_info("found a monitor source, do not route to hw sink!");
            return PA_HOOK_OK;
        }

        for (i = 0; i < eVirtualSource_Count; i++) {
            if (pa_streq(data->source->name, virtualsourcemap[i].virtualsourcename)) {
                pa_log_debug
                    ("found virtual source index on virtual source %d, name %s, index %d",
                     virtualsourcemap[i].virtualsourceidentifier, data->source->name, i);
                source_index = i;
                break;
            }
        }
    }

    stream_type = pa_proplist_new();
    pa_proplist_sets(stream_type, "media.type", virtualsourcemap[source_index].virtualsourcename);
    pa_proplist_update(data->proplist, PA_UPDATE_MERGE, stream_type);
    pa_proplist_free(stream_type);

    if ((data->source != NULL) && strstr (data->source->name,"bluez_"))
    {
        return PA_HOOK_OK;
    }

    /* implement policy */
    for (i = 0; i < eVirtualSource_Count; i++) {
        if (i == (int) virtualsourcemap[source_index].virtualsourceidentifier) {
            pa_source *s;

            pa_log_debug
                ("setting data->source (physical) to %s for streams created on %s (virtual)",
                 u->source_mapping_table[i].inputdevice, virtualsourcemap[i].virtualsourcename);

            if (data->source == NULL)
                s = pa_namereg_get(c, PCM_SOURCE_NAME, PA_NAMEREG_SOURCE);
            else
                s = pa_namereg_get(c, u->source_mapping_table[i].inputdevice, PA_NAMEREG_SOURCE);
            if (s && PA_SOURCE_IS_LINKED(pa_source_get_state(s)))
                pa_source_output_new_data_set_source(data, s, false);
            break;
        }
    }
    return PA_HOOK_OK;
}

static pa_hook_result_t route_source_output_fixate_hook_callback(pa_core * c, pa_source_output_new_data * data,
                                                                 struct userdata *u) {

    pa_assert(c);
    pa_assert(data);
    pa_assert(u);

    /* nothing much to to in fixate as of now */
    return PA_HOOK_OK;
}

static pa_hook_result_t route_source_output_put_hook_callback(pa_core * c, pa_source_output * so, struct userdata *u) {
    const char *so_type = NULL;
    struct sourceoutputnode *node = NULL;
    int i, source_index = -1;
    pa_source_output_state_t state;

    pa_assert(c);
    pa_assert(so);
    pa_assert(u);

    if (strstr(so->source->name, "monitor"))
        return PA_HOOK_OK;      /* nothing to be done for monitor source */

    so_type = pa_proplist_gets(so->proplist, "media.type");
    pa_assert(so_type != NULL);

    node = pa_xnew0(struct sourceoutputnode, 1);
    node->virtualsourceid = -1;
    node->sourceoutput = so;
    node->sourceoutputidx = so->index;
    node->paused = false;

    for (i = 0; i < eVirtualSource_Count; i++) {
        if (pa_streq(so_type, u->source_mapping_table[i].virtualsourcename)) {
            source_index = u->source_mapping_table[i].virtualsourceidentifier;
            break;
        }
    }

    pa_assert(source_index != -1);
    node->virtualsourceid = source_index;

    u->n_source_output_opened++;
    PA_LLIST_PREPEND(struct sourceoutputnode, u->sourceoutputnodelist, node);

    state = pa_source_output_get_state(so);
    if (state == PA_SOURCE_OUTPUT_CORKED) {
        node->paused = true;
        pa_log_debug("Record stream of type(%s) is opened in corked state", so_type);
        return PA_HOOK_OK;
    }
    if (u->connectionactive && u->connev) {
        char audiobuf[SIZE_MESG_TO_AUDIOD];
        int ret;

        sprintf(audiobuf, "d %d %d", node->virtualsourceid, node->sourceoutputidx);
        ret = send(u->newsockfd, audiobuf, SIZE_MESG_TO_AUDIOD, 0);
        if (ret == -1)
            pa_log("Record stream type(%s): send failed(%s)", so_type, strerror(errno));

    }
    u->audiod_source_output_opened[source_index]++;

    return PA_HOOK_OK;
}

/* callback for stream deletion */
static pa_hook_result_t route_sink_input_unlink_hook_callback(pa_core * c, pa_sink_input * data, struct userdata *u) {
    char audiodbuf[SIZE_MESG_TO_AUDIOD];
    struct sinkinputnode *thelistitem = NULL;

    pa_assert(data);
    pa_assert(u);

    /* delete the list item */
    for (thelistitem = u->sinkinputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {

        if (thelistitem->sinkinput == data) {

            /* we have a connection send a message to audioD */
            if (!thelistitem->paused) {

                /* notify audiod of stream closure */
                if (u->connectionactive && u->connev != NULL) {

                    sprintf(audiodbuf, "c %d %d", thelistitem->virtualsinkid, thelistitem->sinkinputidx);
                    if (-1 == send(u->newsockfd, audiodbuf, SIZE_MESG_TO_AUDIOD, 0))
                        pa_log_info("route_sink_input_unlink_hook_callback: send failed: %s", strerror(errno));
                    else
                        pa_log_info("route_sink_input_unlink_hook_callback: sending close notification to audiod");
                }

                // decrease sink opened count, even if audiod doesn't hear from it
                if (thelistitem->virtualsinkid >= eVirtualSink_First && thelistitem->virtualsinkid <= eVirtualSink_Last)
                    u->audiod_sink_input_opened[thelistitem->virtualsinkid]--;
            }

            /* remove this node from the list and free */
            PA_LLIST_REMOVE(struct sinkinputnode, u->sinkinputnodelist, thelistitem);

            pa_xfree(thelistitem);
            pa_assert(u->n_sink_input_opened > 0);
            u->n_sink_input_opened--;
            break;
        }
    }

    return PA_HOOK_OK;
}

/* callback for stream pause/unpause */
static pa_hook_result_t
route_sink_input_state_changed_hook_callback(pa_core * c, pa_sink_input * data, struct userdata *u) {
    pa_sink_input_state_t state;
    char audiodbuf[SIZE_MESG_TO_AUDIOD];
    struct sinkinputnode *thelistitem = NULL;

    pa_assert(data);
    pa_assert(u);

    /* delete the list item */
    for (thelistitem = u->sinkinputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {

        if (thelistitem->sinkinput == data) {

            state = pa_sink_input_get_state(thelistitem->sinkinput);

            /* we have a connection send a message to audioD */
            if (!thelistitem->paused && state == PA_SINK_INPUT_CORKED) {
                thelistitem->paused = true;
                sprintf(audiodbuf, "c %d %d", thelistitem->virtualsinkid, thelistitem->sinkinputidx);

                // decrease sink opened count, even if audiod doesn't hear from it
                if (thelistitem->virtualsinkid >= eVirtualSink_First && thelistitem->virtualsinkid <= eVirtualSink_Last)
                    u->audiod_sink_input_opened[thelistitem->virtualsinkid]--;

            }
            else if (thelistitem->paused && state != PA_SINK_INPUT_CORKED) {
                thelistitem->paused = false;
                sprintf(audiodbuf, "o %d %d", thelistitem->virtualsinkid, thelistitem->sinkinputidx);

                // increase sink opened count, even if audiod doesn't hear from it
                if (thelistitem->virtualsinkid >= eVirtualSink_First && thelistitem->virtualsinkid <= eVirtualSink_Last)
                    u->audiod_sink_input_opened[thelistitem->virtualsinkid]++;
            }
            else
                continue;

            /* notify audiod of stream closure */
            if (u->connectionactive && u->connev != NULL) {
                if (-1 == send(u->newsockfd, audiodbuf, SIZE_MESG_TO_AUDIOD, 0))
                    pa_log("route_sink_input_state_changed_hook_callback: send failed: %s", strerror(errno));
                else
                    pa_log_info
                        ("route_sink_input_state_changed_hook_callback: sending state change notification to audiod");
            }
        }
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t route_source_output_state_changed_hook_callback(pa_core * c, pa_source_output * so, struct userdata *u) {

    pa_source_output_state_t state;
    struct sourceoutputnode *node;
    char audiobuf[SIZE_MESG_TO_AUDIOD];
    int ret;

    pa_assert(c);
    pa_assert(so);
    pa_assert(u);

    state = pa_source_output_get_state(so);

    for (node = u->sourceoutputnodelist; node; node = node->next) {
        if (node->sourceoutput == so) {
            if (state == PA_SOURCE_OUTPUT_CORKED) {
                pa_assert(node->paused == false);
                sprintf(audiobuf, "k %d %d", node->virtualsourceid, node->sourceoutputidx);
                node->paused = true;

                /* decrease source opened count, even if audiod doesn't hear from it */
                if (node->virtualsourceid >= eVirtualSource_First && node->virtualsourceid <= eVirtualSource_Last)
                    u->audiod_source_output_opened[node->virtualsourceid]--;
            }
            else if (state == PA_SOURCE_OUTPUT_RUNNING) {
                pa_assert(node->paused == true);
                node->paused = false;
                sprintf(audiobuf, "d %d %d", node->virtualsourceid, node->sourceoutputidx);

                /* increase source opened count, even if audiod doesn't hear from it */
                if (node->virtualsourceid >= eVirtualSource_First && node->virtualsourceid <= eVirtualSource_Last)
                    u->audiod_source_output_opened[node->virtualsourceid]++;
            }
            ret = send(u->newsockfd, audiobuf, SIZE_MESG_TO_AUDIOD, 0);
            if (ret == -1)
                pa_log("Error sending recording stream msg (%s)", audiobuf);
            break;
        }
    }
    return PA_HOOK_OK;
}

/* callback for source deletion */
static pa_hook_result_t route_source_output_unlink_hook_callback(pa_core * c, pa_source_output * data, struct userdata *u) {
    char audiodbuf[SIZE_MESG_TO_AUDIOD];
    struct sourceoutputnode *thelistitem = NULL;

    pa_assert(data);
    pa_assert(u);
    pa_assert(c);

    /* delete the list item */
    for (thelistitem = u->sourceoutputnodelist; thelistitem != NULL; thelistitem = thelistitem->next) {

        if (thelistitem->sourceoutput == data) {

            if (!thelistitem->paused) {
                /* we have a connection send a message to audioD */
                /* notify audiod of stream closure */
                if (u->connectionactive && u->connev != NULL) {
                    sprintf(audiodbuf, "k %d %d", thelistitem->virtualsourceid, thelistitem->sourceoutputidx);

                    if (-1 == send(u->newsockfd, audiodbuf, SIZE_MESG_TO_AUDIOD, 0))
                        pa_log("route_source_output_unlink_hook_callback: send failed: %s", strerror(errno));
                    else
                        pa_log_info("route_source_output_unlink_hook_callback: sending close notification to audiod");
                }

                // decrease source opened count, even if audiod doesn't hear from it
                if (thelistitem->virtualsourceid >= eVirtualSource_First
                    && thelistitem->virtualsourceid <= eVirtualSource_Last)
                    u->audiod_source_output_opened[thelistitem->virtualsourceid]--;
            }

            /* remove this node from the list and free */
            PA_LLIST_REMOVE(struct sourceoutputnode, u->sourceoutputnodelist, thelistitem);

            pa_xfree(thelistitem);

            // maintain count, even if we can't talk to audiod
            pa_assert(u->n_source_output_opened > 0);
            u->n_source_output_opened--;
            break;
        }
    }

    return PA_HOOK_OK;
}

/* exit and cleanup */

void pa__done(pa_module * m) {
    struct userdata *u;
    struct sinkinputnode *thelistitem = NULL;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->connev != NULL) {
        /* a connection exists on the socket, tear down */
        /* remove ourselves from the IO list on the main loop */
        u->core->mainloop->io_free(u->connev);

        /* tear down the connection */
        if (-1 == shutdown(u->newsockfd, SHUT_RDWR)) {
            pa_log_info("Error in shutdown (%d):%s", errno, strerror(errno));
        }
        if (-1 == close(u->newsockfd)) {
            pa_log_info("Error in close (%d):%s", errno, strerror(errno));
        }
    }

    if (u->sockev != NULL) {
        /* a socket still exists, tear it down and remove
         * ourselves from the IO list on the pulseaudio
         * main loop */
        u->core->mainloop->io_free(u->sockev);

        /* tear down the connection */
        if (-1 == shutdown(u->sockfd, SHUT_RDWR)) {
            pa_log_info("Error in shutdown (%d):%s", errno, strerror(errno));
        }
        if (-1 == close(u->sockfd)) {
            pa_log_info("Error in close (%d):%s", errno, strerror(errno));
        }
    }

    disconnect_hooks(u);

    /* free the list of sink-inputs */
    while ((thelistitem = u->sinkinputnodelist) != NULL) {
        PA_LLIST_REMOVE(struct sinkinputnode, u->sinkinputnodelist, thelistitem);
        pa_xfree(thelistitem);
    }

    pa_xfree(u->destAddress);
    pa_xfree(u->connectionType);
    pa_xfree(u);
}

static pa_hook_result_t route_sink_input_move_finish_cb(pa_core *c, pa_sink_input *data, struct userdata *u)
{
    int i;

    pa_assert(c);
    pa_assert(data);
    pa_assert(u);

    for (i = eVirtualSink_First; i<= eVirtualSink_Last; i++)
        virtual_sink_input_set_volume(i, u->sink_mapping_table[i].volume, 0, u);

    pa_log_debug ("moved sink inputs to the destination sink");
    return PA_HOOK_OK;
}

pa_hook_result_t route_sink_unlink_post_cb(pa_core *c, pa_sink *sink, struct userdata *u)
{
    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);

    if (pa_streq(sink->name, PCM_SINK_NAME))
        u->alsa_sink1 = NULL;

    return PA_HOOK_OK;
}

pa_hook_result_t route_source_unlink_post_cb(pa_core *c, pa_source *source, struct userdata *u)
{
    pa_assert(c);
    pa_assert(source);
    pa_assert(u);

    if (strstr(source->name, PCM_SOURCE_NAME))
        u->alsa_source = NULL;

    return PA_HOOK_OK;
}

pa_hook_result_t route_sink_unlink_cb(pa_core *c, pa_sink *sink, struct userdata *u)
{
    pa_log_info("route_sink_unlink_cb");
    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);
    pa_log_debug("BT sink disconnected with name:%s", sink->name);
    if (strstr(sink->name, "bluez_sink."))
    {
        pa_log_debug("BT sink disconnected with name:%s", sink->name);
        u->callback_deviceName = sink->name;
        if (NULL != u->callback_deviceName)
        {
            pa_log_debug("Bt sink disconnected with name:%s", u->callback_deviceName);
            /* notify audiod of device insertion */
            if (u->connectionactive && u->connev) {
                char audiobuf[SIZE_MESG_TO_AUDIOD];
                int ret = -1;
                /* we have a connection send a message to audioD */
                sprintf(audiobuf, "%c %s", '3', u->callback_deviceName);
                pa_log_info("payload:%s", audiobuf);
                ret = send(u->newsockfd, audiobuf, SIZE_MESG_TO_AUDIOD, 0);
                if (-1 == ret)
                    pa_log("send() failed: %s", strerror(errno));
                else
                    pa_log_info("sent device unloaded message to audiod");
           }
           else
               pa_log_warn("connectionactive is not active");
        }
        else
            pa_log_warn("error reading device name");
    }
    return PA_HOOK_OK;
}

pa_hook_result_t sink_load_subscription_callback(pa_core *c, pa_sink_new_data *data, struct userdata *u)
{
    pa_log_info("sink_load_subscription_callback");
    pa_assert(c);
    pa_assert(data);
    pa_assert(u);
    if (strstr(data->name, "bluez_sink."))
    {
        pa_log_debug("BT sink connected with name:%s", data->name);
        u->callback_deviceName = data->name;
        if (NULL != u->callback_deviceName)
        {
            /* notify audiod of device insertion */
            if (u->connectionactive && u->connev) {
                char audiobuf[SIZE_MESG_TO_AUDIOD];
                int ret = -1;
                /* we have a connection send a message to audioD */
                sprintf(audiobuf, "%c %s", 'i', u->callback_deviceName);
                pa_log_info("payload:%s", audiobuf);
                ret = send(u->newsockfd, audiobuf, SIZE_MESG_TO_AUDIOD, 0);
                if (-1 == ret)
                    pa_log("send() failed: %s", strerror(errno));
                else
                    pa_log_info("sent device loaded message to audiod");
           }
           else
               pa_log_warn("connectionactive is not active");
        }
        else
            pa_log_warn("error reading device name");
    }
    else
        pa_log_warn("Sink other than BT is loaded");
    return PA_HOOK_OK;
}

static const char* const device_valid_modargs[] = {
    "name",
    "source_name",
    "source_properties",
    "namereg_fail",
    "device",
    "device_id",
    "format",
    "rate",
    "alternate_rate",
    "channels",
    "channel_map",
    "fragments",
    "fragment_size",
    "mmap",
    "tsched",
    "tsched_buffer_size",
    "tsched_buffer_watermark",
    "ignore_dB",
    "control",
    "deferred_volume",
    "deferred_volume_safety_margin",
    "deferred_volume_extra_delay",
    "fixed_latency_range",
    "sink_name",
    "sink_properties",
    "rewind_safeguard",
    NULL
};

static pa_hook_result_t module_unload_subscription_callback(pa_core *c, pa_module *m, struct userdata *u)
{
    pa_log_info("module_unload_subscription_callback");
    pa_assert(c);
    pa_assert(m);
    pa_assert(u);
    pa_modargs *ma = NULL;
    pa_log_debug("module_unloaded with index#:%u", m->index);
    if (u->display1UsbIndex == m->index)
    {
        pa_log_warn("module with display1UsbIndex is unloaded");
        u->IsDisplay1usbSinkLoaded = false;
    }
    else if (u->display2UsbIndex == m->index)
    {
        pa_log_warn("module with display2UsbIndex is unloaded");
        u->IsDisplay2usbSinkLoaded = false;
    }
    else
        pa_log_warn("module with unknown index is unloaded");
    if (!(ma = pa_modargs_new(m->argument, device_valid_modargs))) {
        pa_log("Failed to parse module arguments.");
    }
    else
    {
        u->callback_deviceName = NULL;
        pa_log_info("module other = %s %d", m->name, m->index);
        if (0 == strncmp(m->name, "module-alsa-source", SOURCE_NAME_LENGTH))
            u->callback_deviceName = pa_xstrdup(pa_modargs_get_value(ma, "source_name", NULL));
        else if (0 == strncmp(m->name, "module-alsa-sink", SINK_NAME_LENGTH))
            u->callback_deviceName = pa_xstrdup(pa_modargs_get_value(ma, "sink_name", NULL));
        else
            pa_log_info("module other than alsa source and sink is unloaded");
        if (NULL != u->callback_deviceName)
        {
            pa_log_debug("module_unloaded with device name:%s", u->callback_deviceName);
            /* notify audiod of device insertion */
            if (u->connectionactive && u->connev) {
                char audiobuf[SIZE_MESG_TO_AUDIOD];
                int ret = -1;
                /* we have a connection send a message to audioD */
                sprintf(audiobuf, "%c %s", '3', u->callback_deviceName);
                pa_log_info("payload:%s", audiobuf);
                ret = send(u->newsockfd, audiobuf, SIZE_MESG_TO_AUDIOD, 0);
                if (-1 == ret)
                    pa_log("send() failed: %s", strerror(errno));
                else
                    pa_log_info("sent device unloaded message to audiod");
           }
           else
               pa_log_warn("connectionactive is not active");
        }
        else
            pa_log_warn("error reading device name");
    }
    return PA_HOOK_OK;
}

static pa_hook_result_t module_load_subscription_callback(pa_core *c, pa_module *m, struct userdata *u)
{
    pa_log_info("module_load_subscription_callback");
    pa_assert(c);
    pa_assert(m);
    pa_assert(u);
    pa_log_debug("module_loaded with name:%s", m->name);
    pa_modargs *ma = NULL;
    if (!(ma = pa_modargs_new(m->argument, device_valid_modargs))) {
        pa_log("Failed to parse module arguments.");
    }
    else
    {
        u->callback_deviceName = NULL;
        if (0 == strncmp(m->name, "module-alsa-source", SOURCE_NAME_LENGTH))
            u->callback_deviceName = pa_xstrdup(pa_modargs_get_value(ma, "source_name", NULL));
        else if (0 == strncmp(m->name, "module-alsa-sink", SINK_NAME_LENGTH))
            u->callback_deviceName = pa_xstrdup(pa_modargs_get_value(ma, "sink_name", NULL));
        else
            pa_log_info("module other than alsa source and sink is loaded");
        if (NULL != u->callback_deviceName)
        {
            pa_log_debug("module_loaded with device name:%s", u->callback_deviceName);
            /* notify audiod of device insertion */
            if (u->connectionactive && u->connev) {
                char audiobuf[SIZE_MESG_TO_AUDIOD];
                int ret = -1;
                /* we have a connection send a message to audioD */
                sprintf(audiobuf, "%c %s", 'i', u->callback_deviceName);
                pa_log_info("payload:%s", audiobuf);
                ret = send(u->newsockfd, audiobuf, SIZE_MESG_TO_AUDIOD, 0);
                if (-1 == ret)
                    pa_log("send() failed: %s", strerror(errno));
                else
                    pa_log_info("sent device loaded message to audiod");
           }
           else
               pa_log_warn("connectionactive is not active");
        }
        else
            pa_log_warn("error reading device name");
    }
    return PA_HOOK_OK;
}

