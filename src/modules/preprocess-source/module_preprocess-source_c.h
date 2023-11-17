/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */
#ifndef __Module_ECNR__
#define __Module_ECNR__

#ifdef __cplusplus
extern "C" {
#endif

struct shECNRInst;
typedef struct shECNRInst shECNRInstT;

shECNRInstT* shECNR_create(int mode);
void shECNR_init(shECNRInstT *handle, char* tfliteFilePath, char* windowFilePath);
void shECNR_free(shECNRInstT *handle);
void shECNR_process(shECNRInstT *handle, float *bin, float *bin_fs, float *bout, int frameLen);
float shECNR_test(shECNRInstT *handle);


#ifdef __cplusplus
}
#endif

#endif // __Module_ECNR__
