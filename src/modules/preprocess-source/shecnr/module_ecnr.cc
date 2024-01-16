/*
 * Copyright (c) 2022 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */
#include "module_ecnr.h"

void shECNR::fftExecute(const kiss_fft_cpx in[N], kiss_fft_cpx out[N]) {
    kiss_fft_cfg cfg;

    if ((cfg = kiss_fft_alloc(N, 0, NULL, NULL)) != NULL)
    {
        size_t i;

        kiss_fft(cfg, in, out);
        free(cfg);
    }
    else
    {
        printf("not enough memory?\n");
        exit(-1);
    }
}
void shECNR::ifftExecute(const kiss_fft_cpx in[N], kiss_fft_cpx out[N]) {
    kiss_fft_cfg cfg;

    if ((cfg = kiss_fft_alloc(N, 1, NULL, NULL)) != NULL)
    {
        size_t i;

        kiss_fft(cfg, in, out);
        free(cfg);
    }
    else
    {
        printf("not enough memory?\n");
        exit(-1);
    }
}

void shECNR::init(int mode, char* tfliteFilePath, char* windowFilePath) {

    model = tflite::FlatBufferModel::BuildFromFile(tfliteFilePath);
    //model = tflite::FlatBufferModel::BuildFromFile("/home/seunghun/ECNR/TFlite_convert/model_nsmin3_test.tflite");

    tflite::InterpreterBuilder(*model, resolver)(&interpreter, 4);

    std::ifstream windowFile;
    std::ofstream testfile;

    hann = std::vector<float> (320, 0);
    //windowFile.open("hann.txt");
    //windowFile.open("/home/seunghun/ECNR/220217_FSnet_forTflite/hann.txt");
    windowFile.open(windowFilePath);
    for (int i = 0; i < 320; i++){
        windowFile >> hann[i];
    }
    syslog(LOG_INFO, "Init ECNR Mode: %s, %s", tfliteFilePath, windowFilePath);
    interpreter->SetInputs({0, 1, 2, 22, 23});
    TfLiteQuantization state_quantization;
    state_quantization.type = kTfLiteNoQuantization;
    interpreter->SetTensorParametersReadWrite(22, kTfLiteFloat32, "model/gru1/zeros", {1, 162}, state_quantization);
    interpreter->SetTensorParametersReadWrite(23, kTfLiteFloat32, "model/gru2/zeros", {1, 160}, state_quantization);
    interpreter->AllocateTensors();


    input_data = std::deque<std::deque<float>> (3, std::deque<float>(31, 0.0));
    input_data2 = std::deque<std::deque<float>> (3, std::deque<float>(31, 0.0));
    output_data = std::deque<float> (31, 1.0);
    freq2erb_matrix = std::deque<std::deque<float>> (161, std::deque<float>(31, 0.0));
    freq2erb_matrix_norm = std::deque<std::deque<float>> (161, std::deque<float>(31, 0.0));
    erb2freq_matrix = std::deque<std::deque<float>> (31, std::deque<float>(161, 0.0));
    gru_state1 = std::vector<float> (162, 0.0);
    gru_state2 = std::vector<float> (160, 0.0);


    erb_cutoffs = std::vector<float> (31, 0.0);

    for (int i = 0; i < 5; i++) {
        erb_cutoffs[i] = 50 * i;
    }
    float erb_lims1 = 9.265 * std::log(1 + 250 / (24.7 * 9.265));
    float erb_lims2 = 9.265 * std::log(1 + 8000 / (24.7 * 9.265));

    for (int i = 5; i < 31; i++) {
        float n_erb = erb_lims1 + (erb_lims2 - erb_lims1) * (i-5) / 25.0;
        erb_cutoffs[i] = 24.7 * 9.265 * (std::exp(n_erb / 9.265) - 1);
    }
    erb_cutoffs[30] = 8000;

    int matrix_index = 0;
    for (int i = 0; i < 160; i++) {
        while (i * 50 >= erb_cutoffs[matrix_index+1]) {
            matrix_index += 1;
        }
        freq2erb_matrix[i][matrix_index] = (erb_cutoffs[matrix_index + 1] - i * 50) / (erb_cutoffs[matrix_index + 1] - erb_cutoffs[matrix_index]);
        freq2erb_matrix[i][matrix_index + 1] = (i * 50 - erb_cutoffs[matrix_index]) / (erb_cutoffs[matrix_index + 1] - erb_cutoffs[matrix_index]);
        erb2freq_matrix[matrix_index][i] = (erb_cutoffs[matrix_index + 1] - i * 50) / (erb_cutoffs[matrix_index + 1] - erb_cutoffs[matrix_index]);
        erb2freq_matrix[matrix_index + 1][i] = (i * 50 - erb_cutoffs[matrix_index]) / (erb_cutoffs[matrix_index + 1] - erb_cutoffs[matrix_index]);
    }

    freq2erb_matrix[160][30] = 1.0;
    erb2freq_matrix[30][160] = 1.0;

    for (int i = 0; i < 31; i++) {
        float sum_val = 0.0;
        for (int j = 0; j < 161; j++) {
            sum_val += freq2erb_matrix[j][i];
        }
        for (int j = 0; j < 161; j++) {
            freq2erb_matrix_norm[j][i] = freq2erb_matrix[j][i] / sum_val;
        }
    }
}

void shECNR::close(){
    closelog();
}

void shECNR::process_ecnr(int in_index, int out_index) {
    for (int i = 0; i < 320; i++){
        in[i].r = m_inputBuffer[i + in_index] * hann[i];
        in[i].i = 0;
    }

    for (int i = 0; i < 320; i++){
        fs_t[i].r = m_fsInputBuffer[i + in_index] * hann[i];
        fs_t[i].i = 0;
    }

    fftExecute(in, out);
    fftExecute(fs_t, fs_f);

    std::deque<float> abs_data = std::deque<float> (161, 1.0);
    std::deque<float> abs_data_erb = std::deque<float> (31, 0.0);
    std::deque<float> abs_data_fs = std::deque<float> (161, 1.0);
    std::deque<float> abs_data_erb_fs = std::deque<float> (31, 0.0);

    for (int i = 0; i < 161; i++) {
        abs_data[i] = sqrt(out[i].r * out[i].r + out[i].i * out[i].i);
        abs_data_fs[i] = sqrt(fs_f[i].r * fs_f[i].r + fs_f[i].i * fs_f[i].i);
    }

    //matmul: abs_data * freq2erb_matrix (1, 161) * (161, 30) -> (1, 30)

    for (int i = 0; i < 31; i++) {
        for (int j = 0; j < 161; j++) {
            abs_data_erb[i] += abs_data[j] * freq2erb_matrix_norm[j][i];
            abs_data_erb_fs[i] += abs_data_fs[j] * freq2erb_matrix_norm[j][i];
        }
    }

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 31; j++) {
            input_data[i][j] = input_data[i+1][j];
        }
        for (int j = 0; j < 31; j++) {
            input_data2[i][j] = input_data2[i+1][j];
        }
    }

    for (int i = 0; i < 31; i++){
        input_data[2][i] = 20 * std::log10(abs_data_erb[i] + 1e-15);
        input_data2[2][i] = 20 * std::log10(abs_data_erb_fs[i] + 1e-15);
    }

    float* input = interpreter->typed_tensor<float>(0);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 31; j++) {
            *(input) = input_data[i][j];
            input++;
        }
    }
    for (int j = 0; j < 31; j++) {
        *(input) = input_data2[0][j];
        input++;
    }

    float* state1 = interpreter->typed_tensor<float>(22);
    for (int i = 0; i < 162; i++) {
        *(state1) = gru_state1[i];
        state1++;
    }
    float* state2 = interpreter->typed_tensor<float>(23);
    for (int i = 0; i < 160; i++) {
        *(state2) = gru_state2[i];
        state2++;
    }
/*
    interpreter->Invoke();

    float* output = interpreter->typed_output_tensor<float>(0);
    for (int i = 0 ; i < 31; i++) {
        output_data[i] = *output;
        output++;
    }

    float* state1_o = interpreter->typed_output_tensor<float>(1);
    for (int i = 0; i < 162; i++) {
        gru_state1[i] = *state1_o;
        state1_o++;
    }
    float* state2_o = interpreter->typed_output_tensor<float>(2);
    for (int i = 0; i < 160; i++) {
        gru_state2[i] = *state2_o;
        state2_o++;
    }

    std::deque<float> output_data_full = std::deque<float> (161, 0.0);
    for (int i = 0; i < 161; i++) {
        for (int j = 0; j < 31; j++) {
            output_data_full[i] += output_data[j] * erb2freq_matrix[j][i];
        }
    }

    for (int i = 0; i < 2; i++){
        for (int j = 0; j < 320; j++) {
            prev_out[i][j].r = prev_out[i+1][j].r;
            prev_out[i][j].i = prev_out[i+1][j].i;
        }
    }
    for (int i = 0; i < 320; i++){
        prev_out[2][i].r = out[i].r;
        prev_out[2][i].i = out[i].i;
    }
    for (int i = 1; i < 160; i++){
        out[i].r = prev_out[0][i].r * output_data_full[i];
        out[i].i = prev_out[0][i].i * output_data_full[i];

        out[320 - i].r = prev_out[0][320 - i].r * output_data_full[i];
        out[320 - i].i = prev_out[0][320 - i].i * output_data_full[i];
    }
    out[0].r = prev_out[0][0].r * output_data_full[0];
    out[0].i = prev_out[0][0].i * output_data_full[0];
    out[160].r = prev_out[0][160].r * output_data_full[160];
    out[160].i = prev_out[0][160].i * output_data_full[160];
*/
    //for test
    //for (int i = 0; i < 320; i++) {
    //    out[i].r = prev_out[0][i].r;
    //    out[i].i = prev_out[0][i].i;
    //}
    //
    ifftExecute(out, in);

    float normVal = 0.0;
    for (int i = 0; i < 320; i++) {
        if (i < 160) {
            normVal = hann[i] * hann[i] + hann[i + 160] * hann[i + 160];
        }
        else {
            normVal = hann[i] * hann[i] + hann[i - 160] * hann[i - 160];
        }

        if (i + out_index < m_outputBuffer.size()) {
            m_outputBuffer[i + out_index] += (in[i].r / (320.0) * hann[i]) / normVal;
        }
        else {
            m_outputBuffer.push_back((in[i].r / (320.0) * hann[i]) / normVal);
        }
    }

}


void shECNR::process(float *bin, float *bin_fs, float *bout, int32_t sampleFrames) {
    std::vector<float> output_buf(sampleFrames, 0.0);

    int index_ = 0;
    int tmp_check = 0;
    if (m_inputBuffer.size() < 480) {
        while (m_inputBuffer.size() < 480 && index_ < sampleFrames) {
            m_inputBuffer.push_back(bin[index_]);
            m_fsInputBuffer.push_back(bin_fs[index_]);
            output_buf[index_++] = 0.0f;
        }

        if (m_inputBuffer.size() < 480 && index_ == sampleFrames) return;

        process_ecnr(0, 0);
        process_ecnr(160, 160);
    }

    while (index_ < sampleFrames) {
        while (m_outputBuffer.size() > 320 && index_ < sampleFrames) {
            output_buf[index_] = m_outputBuffer[160];
            m_outputBuffer.pop_front();

            m_inputBuffer.push_back(bin[index_]);
            m_inputBuffer.pop_front();
            m_fsInputBuffer.push_back(bin_fs[index_]);
            m_fsInputBuffer.pop_front();
            ++index_;
        }

        if (m_outputBuffer.size() > 320 && index_ == sampleFrames) {

            for (int i = 0; i < sampleFrames; i++){
                bout[i] = output_buf[i];
            }
            return;
        }

        process_ecnr(160, 160);

    }
    for (int i = 0; i < sampleFrames; i++){
        bout[i] = output_buf[i];
    }
}

float shECNR::test(){
    interpreter->Invoke();
    float ttt = hann[150];
    return ttt;

}