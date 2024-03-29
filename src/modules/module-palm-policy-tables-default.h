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

#ifndef _MODULE_PALM_POLICY_TABLES_H_
#define _MODULE_PALM_POLICY_TABLES_H_
#include <stdbool.h>

#include <pulse/volume.h>
#include "module-palm-policy.h"

/* This stuff is highly system dependent, These tables probably need to be
 * built automatically then communicated back to the policy manager.  For
 * now these are hard coded.  If the pulse config in default.pa or the alsa
 * config in asound.rc change this will likely not work.  This needs to be
 * fixed up in the future
 */


/* maps an enum to the actual pulseaudio sink name so we don't have to keep
 * futzing around with streams, one per virtual sink.
 */
struct _virtualsinkmap {
    const char *virtualsinkname;
    uint32_t virtualsinkidentifier;
    char outputdevice[50];
    int volumetable;
    int volume;
    int ismuted;
};

struct _virtualsourcemap {
    const char *virtualsourcename;
    uint32_t virtualsourceidentifier;
    char inputdevice[50];
    int volumetable;
    int volume;
    int ismuted;
};

/* maps an enum to the actual pulseaudio sink name so we don't have to keep
 * futzing around with streams, one per virtual sink.
 */
static struct _virtualsinkmap virtualsinkmap[] = {
    {"palerts",         (uint32_t)ealerts, "palerts", 0, 0, false},
    {"pfeedback",       (uint32_t)efeedback, "pfeedback", 0, 0, false},
    {"pringtones",      (uint32_t)eringtones, "pringtones", 0, 0, false},
    {"pmedia",          (uint32_t)emedia, "pmedia", 0, 0, false},
    {"pdefaultapp",     (uint32_t)edefaultapp, "pdefaultapp", 0, 0, false},
    {"peffects",        (uint32_t)eeffects, "peffects", 0, 0, false},
    {"ptts",            (uint32_t)etts, "ptts", 0, 0, false},
    {"voipcall",         (uint32_t)evoipcall, "voipcall", 0, 0, false},
    {"pvoicerecognition", (uint32_t)evoicerecognition, "pvoicerecognition", 0, 0, false},
    {"btstream",        (uint32_t)ebtstream, "btstream", 0, 0, false},
    {"btcall",          (uint32_t)ebtcall, "btcall", 0, 0, false},
    {"fm",              (uint32_t)efm, "fm", 0, 0, false},
    {"am",              (uint32_t)eam, "am", 0, 0, false},
    {"hdradio",         (uint32_t)ehdradio, "hdradio", 0, 0, false},
    {"radio",           (uint32_t)eradio, "radio", 0, 0, false},
    {"default1",        (uint32_t)edefault1, "default1", 0, 0, false},
    {"tts1",            (uint32_t)etts1, "tts1", 0, 0, false},
    {"voipcall1",       (uint32_t)evoipcall1, "voipcall1", 0, 0, false},
    {"default2",        (uint32_t)edefault2, "default2", 0, 0, false},
    {"tts2",            (uint32_t)etts2, "tts2", 0, 0, false},
    {"voipcall2",       (uint32_t)evoipcall2, "voipcall2", 0, 0, false},
    {NULL, 0}
};

static struct _virtualsourcemap virtualsourcemap[] = {
    {"record",           (uint32_t)erecord, "record", 0, 0, false},
    {"btcallsource",     (uint32_t)ebtcallsource, "btcallsource", 0, 0, false},
    {"alexa",            (uint32_t)ealexa, "alexa", 0, 0, false},
    {"webcall",          (uint32_t)ewebcall, "webcall", 0, 0, false},
    {"voiceassistance",  (uint32_t)evoiceassistance, "voiceassistance", 0, 0, false},
    {"webcall1",         (uint32_t)ewebcall1, "webcall1", 0, 0, false},
    {"record1",          (uint32_t)erecord1, "record1", 0, 0, false},
    {"alexa1",           (uint32_t)ealexa1, "alexa1", 0, 0, false},
    {"webcall2",         (uint32_t)ewebcall2, "webcall2", 0, 0, false},
    {NULL, 0}
};

/*
    Volume level curves, [0] for headset, [1] for other paths, like back speaker, front speaker.
 */

static int _mapPercentToPulseVolume[2][101] = {

    {0,     34000,  39586,  39851,  40115,  40380,  40645,  40910,  41175,  41439,
    41704,  41969,  42234,  42498,  42763,  43028,  43293,  43558,  43822,  44087,
    44352,  44617,  44882,  45146,  45411,  45676,  45941,  46205,  46470,  46735,
    47000,  47265,  47529,  47794,  48059,  48324,  48589,  48853,  49118,  49383,
    49648,  49913,  50177,  50442,  50707,  50972,  51236,  51501,  51766,  52031,
    52296,  52560,  52825,  53090,  53355,  53620,  53884,  54149,  54414,  54679,
    54943,  55208,  55473,  55738,  56003,  56267,  56532,  56797,  57062,  57327,
    57591,  57856,  58121,  58386,  58651,  58915,  59180,  59445,  59710,  59974,
    60239,  60504,  60769,  61034,  61298,  61563,  61828,  62093,  62358,  62622,
    62887,  63152,  63417,  63681,  63946,  64211,  64476,  64741,  65005,  65270,  65535},

    {0,     26000,  29892,  30294,  30695,  31096,  31498,  31899,  32301,  32702,
    33103,  33505,  33906,  34308,  34709,  35110,  35512,  35913,  36315,  36716,
    37117,  37519,  37920,  38322,  38723,  39124,  39526,  39927,  40329,  40730,
    41131,  41533,  41934,  42336,  42737,  43138,  43540,  43941,  44343,  44744,
    45145,  45547,  45948,  46350,  46751,  47152,  47554,  47955,  48357,  48758,
    49159,  49561,  49962,  50364,  50765,  51166,  51568,  51969,  52371,  52772,
    53173,  53575,  53976,  54378,  54779,  55180,  55582,  55983,  56385,  56786,
    57187,  57589,  57990,  58392,  58793,  59194,  59596,  59997,  60399,  60800,
    61603,  61800,  61996,  62193,  62389,  62586,  62783,  62979,  63176,  63372,
    63569,  63766,  63962,  64159,  64355,  64552,  64749,  64945,  65142,  65338, 65535}
};

static float _mapPercentToPulseRamp[2][101] = {

    {PA_DECIBEL_MININFTY ,   -48.13 ,    -39.59 ,    -39.20 ,    -38.79 ,    -38.39 ,    -37.98 ,    -37.58 ,    -37.17 ,    -36.77 ,
    -36.36 ,    -35.96 ,    -35.56 ,    -35.15 ,    -34.75 ,    -34.34 ,    -33.94 ,    -33.54 ,    -33.13 ,    -32.73 ,
    -32.32 ,    -31.92 ,    -31.51 ,    -31.11 ,    -30.71 ,    -30.30 ,    -29.90 ,    -29.50 ,    -29.09 ,    -28.69 ,
    -28.28 ,    -27.88 ,    -27.47 ,    -27.07 ,    -26.67 ,    -26.26 ,    -25.86 ,    -25.46 ,    -25.05 ,    -24.65 ,
    -24.24 ,    -23.84 ,    -23.43 ,    -23.03 ,    -22.63 ,    -22.22 ,    -21.82 ,    -21.41 ,    -21.01 ,    -20.61 ,
    -20.20 ,    -19.80 ,    -19.39 ,    -18.99 ,    -18.59 ,    -18.18 ,    -17.78 ,    -17.37 ,    -16.97 ,    -16.57 ,
    -16.16 ,    -15.76 ,    -15.35 ,    -14.95 ,    -14.55 ,    -14.14 ,    -13.74 ,    -13.33 ,    -12.93 ,    -12.53 ,
    -12.12 ,    -11.72 ,    -11.31 ,    -10.91 ,    -10.51 ,    -10.10 ,    -9.70 ,     -9.29 ,     -8.89 ,     -8.48 ,
    -8.08 ,     -7.68 ,     -7.27 ,     -6.87 ,     -6.46 ,     -6.06 ,     -5.66 ,     -5.25 ,     -4.85 ,     -4.44 ,
    -4.04 ,     -3.64 ,     -3.23 ,     -2.83 ,     -2.42 ,     -2.02 ,     -1.62 ,     -1.21 ,     -0.81 ,     -0.40 ,     0.00
    },

    {PA_DECIBEL_MININFTY ,   -60.34 ,    -54.39 ,    -53.79 ,    -53.16 ,    -52.52 ,    -51.93 ,    -51.32 ,    -50.71 ,    -50.09 ,
    -49.48 ,    -48.87 ,    -48.27 ,    -47.64 ,    -47.05 ,    -46.42 ,    -45.80 ,    -45.20 ,    -44.60 ,    -43.97 ,
    -43.36 ,    -42.76 ,    -42.14 ,    -41.52 ,    -40.91 ,    -40.30 ,    -39.69 ,    -39.08 ,    -38.47 ,    -37.85 ,
    -37.24 ,    -36.62 ,    -36.01 ,    -35.40 ,    -34.79 ,    -34.17 ,    -33.56 ,    -32.95 ,    -32.34 ,    -31.73 ,
    -31.11 ,    -30.50 ,    -29.89 ,    -29.27 ,    -28.66 ,    -28.05 ,    -27.44 ,    -26.82 ,    -26.21 ,    -25.60 ,
    -24.99 ,    -24.38 ,    -23.76 ,    -23.15 ,    -22.54 ,    -21.92 ,    -21.31 ,    -20.70 ,    -20.09 ,    -19.47 ,
    -18.86 ,    -18.25 ,    -17.64 ,    -17.03 ,    -16.41 ,    -15.80 ,    -15.19 ,    -14.58 ,    -13.96 ,    -13.35 ,
    -12.74 ,    -12.13 ,    -11.51 ,    -10.90 ,    -10.29 ,    -9.68 ,     -9.06 ,     -8.45 ,     -7.84 ,     -7.23 ,
    -6.00 ,     -5.70 ,     -5.40 ,     -5.10 ,     -4.80 ,     -4.50 ,     -4.20 ,     -3.90 ,     -3.60 ,     -3.30 ,
    -3.00 ,     -2.70 ,     -2.40 ,     -2.10 ,     -1.80 ,     -1.50 ,     -1.20 ,     -0.90 ,     -0.60 ,     -0.30 ,     0.00
    }
};

#define MAX_FILTER_TABLES 5

/* B0, B1, B2, A1, A2 */
static int32_t _filterTable[MAX_FILTER_TABLES][20] = {

    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* placeholding, dummy table */
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    // preset 1
    {15744, -31488, 15744, -31463, 15128, 16218, -26881, 14514, -26881, 14348,
     16384, -6862, 5823, -6862, 5823, 16384, 2287, 5501, 2287, 5501},

    // preset 2
    {15820, -31640, 15820, -31621, 15275, 16263, -29720, 14750, -29720, 14629,
     16384, -6862, 5823, -6862, 5823, 16384, 2287, 5501, 2287, 5501},

    // preset 3
    {-486, 234, 254, -486, 235, -458, 216, 253, -458, 219,
     -224, 238, 248, -224, 246, -182, 102, 188, -80,  67},

    // preset 4
    {-484, 232, 254, -484, 233, -437, 214, 253, -437, 216,
     -290, 151, 244, -290, 162, -201, 105, 190, -101, 70}

};

#endif /* _MODULE_PALM_POLICY_TABLES_H_ */
