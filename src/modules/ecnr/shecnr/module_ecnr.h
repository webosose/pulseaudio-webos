#ifndef __shECNR_H__
#define __shECNR_H__

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/optional_debug_tools.h"
#include "kiss_fft.h"

#include <fstream>
#include <syslog.h>
#include <vector>
#include <deque>
#include <ctime>

#define N 320
#define N2 161

class shECNR {
private:
    void fftExecute(const kiss_fft_cpx in[N], kiss_fft_cpx out[N]);
    void ifftExecute(const kiss_fft_cpx in[N], kiss_fft_cpx out[N]);
    void process_ecnr(int in_index, int out_index);
    void process_ecnr_fsnet(int in_index, int out_index);
    void process_ecnr_nsmin(int in_index, int out_index);

    std::unique_ptr<tflite::FlatBufferModel> model;
    tflite::ops::builtin::BuiltinOpResolver resolver;
    std::unique_ptr<tflite::Interpreter> interpreter;

    kiss_fft_cpx in[N], out[N], prev_out[3][N];
    kiss_fft_cpx fs_t[N], fs_f[N];
    std::deque<std::deque<float>> input_data, input_data2, freq2erb_matrix, freq2erb_matrix_norm, erb2freq_matrix;
    std::deque<float> output_data;

    std::deque<float> m_inputBuffer, m_fsInputBuffer;
    std::deque<float> m_outputBuffer;
    std::vector<float> hann, erb_cutoffs, gru_state1, gru_state2;

public:
    void init(int mode, char* tfliteFilePath, char* windowFilePath);
    void close();
    void process(float *in, float *in_fs, float *out, int32_t sampleFrames);
    float test();
};


#endif // __shECNR_H__