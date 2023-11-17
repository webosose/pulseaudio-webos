/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#ifndef WEBRTC_MODULES_AUDIO_PROCESSING_BEAMFORMER_DOA_H_
#define WEBRTC_MODULES_AUDIO_PROCESSING_BEAMFORMER_DOA_H_

#include <vector>

#include "../../../modules/audio_processing/beamformer/array_util.h"
#include "../../../modules/audio_processing/beamformer/complex_matrix.h"

//  spacialized on 36 direction (0 to 350 degrees with 10 degree interval)
//  spacialized on 16kHz, 256 FFT
namespace webrtc_ecnr {
class Doa {
    public:
        ~Doa();

        //  initialize variables
        void Initialize(std::vector<Point> array_geometry, float initial_aim_radians);

        //  calculate Doa using DOA algorithm
        //  return true if DOA is updated else, return false
        bool ComputeDoa(const std::complex<float>* const* input, int num_input_channels, size_t num_freq_bins);

        //  return current DOA direction as radians
        float GetDirectionRadians();

    private:
        void MemoryAlloc();
        void CalculateModeVectorCovariance();

        void CumulateRMS(const std::complex<float>* const* input);
        void InputPhaseTransform(const std::complex<float>* const* input);
        void CumulateInputCovariance();
        void CalculateSrpCosts();
        void BackgroundNoiseUpdate();
        float FindPeakResult();
        bool SmoothResult(float result_radians);

        const int grid_points_ = 36;
        const float speed_of_sound_ = 343.f;
        const float sample_rate_ = 16000.f;
        const float fft_size_ = 256.f;
        const int freq_bins_ = int(fft_size_ / 2 + 1);
        const int start_bin_ = 4;
        const int end_bin_ = 28;
        const int num_bin_ = end_bin_ - start_bin_;

        std::vector<Point> array_geometry_;
        bool is_array_linear_;
        int channels_;
        int num_pairs_;

        const int doa_interval_ = 10;
        const static int direction_history_size_ = 10;
        int blocks_after_aim_;
        const float srp_cost_threshold_ = 0.3;
        const float threshold_radians_ = M_PI * 10 / 180;
        float direction_history_[direction_history_size_];
        float prev_direction_;

        float input_rms_;
        float input_noise_threshold_;
        float *noise_cost_;                     //  [grid]

        std::complex<float> **input_phase_;     //  [freq, channel]
        std::complex<float> ***mode_cov_;       //  [freq, pairs, grid]
        std::complex<float> **input_cov_;       //  [freq, pairs]
        float *srp_cost_;                       //  [grid]
};

}  //  namespace webrtc_ecnr

#endif