/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */
#include "module_ecnr_c.h"
#include "module_ecnr.h"
#include <stdlib.h>

struct shECNRInst{
    void *obj;
    int mode;
};


shECNRInstT* shECNR_create(int mode)
{
    shECNRInstT *m;
    shECNR *obj;

    m = (shECNRInstT*)malloc(sizeof(*m));
    obj = new shECNR();
    m->obj = obj;
    if (mode == 0) {
        m->mode = 0; // nsmin
    }
    else {
        m->mode = 1; // fsnet
    }

    return m;

}

void shECNR_init(shECNRInstT *handle, char* tfliteFilePath, char* windowFilePath)
{
    shECNR *obj;

    if (handle == NULL) return;

    obj = static_cast<shECNR *>(handle->obj);

    obj->init(0, tfliteFilePath, windowFilePath);
    //obj->init(0, "/usr/lib/pulse-15.0/modules/ecnr/model_ecnr.tflite", "/usr/lib/pulse-15.0/modules/ecnr/hann.txt");
    //obj->init(0, "/vendor/ecnr/model_ecnr.tflite", "/vendor/ecnr/hann.txt");
}

void shECNR_free(shECNRInstT *handle)
{
    free(handle);
}


void shECNR_process(shECNRInstT *handle, float *bin, float *bin_fs, float *bout, int frameLen){

    shECNR *obj;

    if (handle == NULL) return;

    obj = static_cast<shECNR *>(handle->obj);
    obj->process(bin, bin_fs, bout, frameLen);
}

float shECNR_test(shECNRInstT *handle){
    shECNR *obj;

    if (handle == NULL) return 11;

    obj = static_cast<shECNR *>(handle->obj);
    return obj->test();

}