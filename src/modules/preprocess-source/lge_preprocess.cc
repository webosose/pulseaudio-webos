/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#include "lge_preprocess.h"
#include "lge_agc.h"
#include "lge_ecnr.h"
#include "lge_beamforming.h"

#include <functional>
#include <vector>
#include <map>
#include <fstream>
#include <pbnjson.hpp>

using initFunc = bool (*) (
                    pa_sample_spec , pa_channel_map ,
                    pa_sample_spec , pa_channel_map ,
                    pa_sample_spec , pa_channel_map ,
                    uint32_t , const char *);
using processFunc  = bool (*) (const uint8_t *, const uint8_t *, uint8_t *);
using doneFunc =  bool (*) ();

struct preproc_table
{
    int priority;
    std::string effectName;
    std::function<bool (
                        pa_sample_spec , pa_channel_map ,
                        pa_sample_spec , pa_channel_map ,
                        pa_sample_spec , pa_channel_map ,
                        uint32_t , const char *)> init;
    std::function<bool (const uint8_t *, const uint8_t *, uint8_t *)> process;
    std::function<bool ()> done;
    bool enabled;
    lt_dlhandle libHandle;
    preproc_table(
            std::function<bool (
                            pa_sample_spec , pa_channel_map ,
                            pa_sample_spec , pa_channel_map ,
                            pa_sample_spec , pa_channel_map ,
                            uint32_t , const char *)> a,
            std::function<bool (const uint8_t *, const uint8_t *, uint8_t *)> b,
            std::function<bool ()> c):init(a),process(b),done(c),enabled(false)
    {

    }
    preproc_table():enabled(false){}
};

std::vector<preproc_table> predata;

bool readConfig(pa_channel_map ch_map)
{
    std::map<std::string,preproc_table> info;
    int channels = ch_map.channels;
    pa_log("channels = %d",channels);
    predata.clear();
    /*info["gain_control"]=preproc_table(agc_getHandle, agc_init, agc_process, agc_done);
    info["speech_enhancement"]=preproc_table(ecnr_getHandle, ecnr_init, ecnr_process, ecnr_done);
    info["beamforming"]=preproc_table(beamforming_getHandle, beamforming_init, beamforming_process, beamforming_done);*/
    //std::ifstream file("/etc/pulse/preproc_config.txt");
    pbnjson::JValue fileInfo =  pbnjson::JDomParser::fromFile("/etc/pulse/preprocessingAudioEffect.json",pbnjson::JSchema::AllSchema());
    if (!fileInfo.isValid() || !fileInfo.isArray()) {
        pa_log("Error opening file: " );

        return false;
    }
    else
    {
        for (const pbnjson::JValue& elements : fileInfo.items())
        {
            std::string name, path;
            int priority;
            elements["name"].asString(name);
            elements["path"].asString(path);
            elements["priority"].asNumber<int>(priority);
            pa_log("%s, %s, %d",name.c_str(),path.c_str(),priority);
            preproc_table temp;
            temp.effectName = name;
            char libmodule_ec_nr_path[100];
            strcpy(libmodule_ec_nr_path,path.c_str());
            temp.libHandle = lt_dlopen(libmodule_ec_nr_path);
            if (temp.libHandle == NULL) {
                pa_log("ECNR: fail to open library: %s %s", lt_dlerror(), libmodule_ec_nr_path);
                return false;
            }
            pa_log_info("ECNR:library open: %s", libmodule_ec_nr_path);

            temp.priority = priority;
            temp.init = (initFunc) lt_dlsym(temp.libHandle, (name+"_init").c_str());
            if (!temp.init)
                pa_log("initFunc not got");
            else
                pa_log_debug("initFunc got");

            temp.process = (processFunc) lt_dlsym(temp.libHandle, (name+"_process").c_str());
            if (!temp.process)
                pa_log("processFunc not got");
            else
                pa_log_debug("processFunc got");

            temp.done = (doneFunc) lt_dlsym(temp.libHandle, (name+"_done").c_str());
            if (!temp.done)
                pa_log("doneFunc not got");
            else
                pa_log_debug("doneFunc got");

            predata.push_back(temp);
            std::sort(predata.begin(),predata.end(), [](preproc_table &a,preproc_table &b){
                return a.priority < b.priority;
            });
        }
    }
    for(auto it: predata)
    {
        pa_log("%s prio:%d enabled:%d",it.effectName.c_str(), it.priority, it.enabled);
    }
    return true;
}


static void lge_fixate_spec(preprocess_params *ec, pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                                  pa_sample_spec *play_ss, pa_channel_map *play_map,
                                  pa_sample_spec *out_ss, pa_channel_map *out_map,  bool beamformer) {

    pa_sample_format_t fixed_format = PA_SAMPLE_FLOAT32NE;
    uint32_t fixed_rate = 16000;

    play_ss->format = fixed_format;
    play_ss->rate = fixed_rate;
    play_ss->channels = 1;
    pa_channel_map_init_mono(play_map);

    *out_ss = *play_ss;
    *out_map = *play_map;

    rec_ss->format = fixed_format;
    rec_ss->rate = fixed_rate;
    if (!beamformer) {
        rec_ss->channels = 1;
        pa_channel_map_init_mono(rec_map);
    }
}

bool lge_preprocess_setParams (preprocess_params *ec,  const char* name, bool enable, void *data)
{
    pa_log_debug("%s",__FUNCTION__);
    auto effectData = std::find_if(predata.begin(), predata.end(), [&](preproc_table &in){return (in.effectName == name);});
    if (effectData == predata.end())
    {
        pa_log("effect not found");
        return false;
    }
    else
    {
        pa_log_debug("Effect found ! %s", effectData->effectName.c_str());
    }
    effectData->enabled = enable;
    return true;
}

bool lge_preprocess_init(preprocess_params *ec,
                     pa_sample_spec *rec_ss, pa_channel_map *rec_map,
                     pa_sample_spec *play_ss, pa_channel_map *play_map,
                     pa_sample_spec *out_ss, pa_channel_map *out_map,
                     uint32_t *nframes, const char *args) {

    //pa_webrtc_agc_init()
    //preproc_table t(agc_getHandle, agc_init, agc_process,agc_done);


    if (!readConfig(*rec_map))
    {
        pa_log("File not found");
        return false;
    }

    lge_fixate_spec(ec, rec_ss, rec_map, play_ss,  play_map,out_ss,  out_map, true);

    for (auto it:predata)
    {
        it.init(*rec_ss, *rec_map, *play_ss, *play_map, *out_ss, *out_map, *nframes, args);
    }
    *nframes=128;
    ec->blocksize = *nframes;
    ec->out_ss = *out_ss;
    ec->rec_ss = *rec_ss;
    ec->play_ss = *play_ss;

    return true;
}

bool lge_preprocess_run(preprocess_params *ec, const uint8_t *rec, const uint8_t *play, uint8_t *out)
{
    int n = ec->blocksize;
    memcpy(out, rec, n*pa_sample_size(&(ec->out_ss))*pa_frame_size(&(ec->out_ss)));
    for (auto it : predata)
    {
        if (it.enabled)
        {
            if(it.process)
                it.process(out, play, out);
            else
                pa_log("fcuntion not valid");
        }
    }
    return true;
}

bool lge_preprocess_done(preprocess_params *ec)
{
    pa_log_debug("lge_preprocess_done");
    for (auto it : predata)
    {
        it.done();
    }
    return  true;
}