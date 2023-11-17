/*
 * Copyright (c) 2023 LG Electronics Inc.
 * SPDX-License-Identifier: LicenseRef-LGE-Proprietary
 */

#define _USE_MATH_DEFINES

//  pulseaudio log
// #include <config.h>
// #include <pulse/cdecl.h>
// PA_C_DECL_BEGIN
// #include <pulsecore/core-util.h>
// PA_C_DECL_END

#include "doa.h"

#include <vector>

namespace webrtc_ecnr {

Doa::~Doa() {
    //  free
    for (int bin = 0; bin < num_bin_; bin++) {
        for (int pair = 0; pair < num_pairs_; pair++) {
            delete[] mode_cov_[bin][pair];
        }
        delete[] input_phase_[bin];
        delete[] mode_cov_[bin];
        delete[] input_cov_[bin];
    }
    delete[] input_phase_;
    delete[] mode_cov_;
    delete[] input_cov_;

    delete[] srp_cost_;
    delete[] noise_cost_;
}

void Doa::Initialize(std::vector<Point> array_geometry, float initial_aim_radians) {
    array_geometry_ = array_geometry;

    //  if inner product of x and y is 0 than all array is in same line
    float inner_product = 0;
    for (int i = 0; i < array_geometry_.size(); i++)
        inner_product += array_geometry_[i].x() * array_geometry_[i].y();
    if (inner_product == 0) is_array_linear_ = true;
    else is_array_linear_ = false;

    channels_ = array_geometry.size();
    num_pairs_ = channels_ * (channels_ - 1) / 2;

    //  memory initialize
    MemoryAlloc();

    //  calculate mode vector covariance
    CalculateModeVectorCovariance();

    blocks_after_aim_ = 0;
    for (int i = 0; i < 10; i++) {
        direction_history_[i] = initial_aim_radians;
    }
    prev_direction_ = initial_aim_radians;

    input_rms_ = 0.f;
    input_noise_threshold_ = 2000.f;
}

bool Doa::ComputeDoa(const std::complex<float>* const* input, int num_input_channels, size_t num_freq_bins) {
    if ((channels_ != num_input_channels) || (freq_bins_ != num_freq_bins))
        return false;

    //  1. Input RMS update
    CumulateRMS(input);

    //  2. Input Phase Transform
    InputPhaseTransform(input);

    //  3. Cumulate Input Covariance
    CumulateInputCovariance();

    blocks_after_aim_++;
    if (blocks_after_aim_ < doa_interval_) {
        return false;
    }
    blocks_after_aim_ = 0;

    //  1. Calculate SRP costs
    CalculateSrpCosts();

    //  2. Backgroud Noise Update
    BackgroundNoiseUpdate();

    //  3. Find raw Direction
    float result_radians = FindPeakResult();

    //  4. Smoothing and post process result
    bool need_update = SmoothResult(result_radians);

    //  reset interval features (input covariance matrix, input rms)
    for (int bin = 0; bin < num_bin_; bin++) {
        for (int pair = 0; pair < num_pairs_; pair++) {
            input_cov_[bin][pair] = std::complex<float>(0.f, 0.f);
        }
    }
    input_rms_ = 0;

    //  Test Debug log
    // char out[] = "                                                                        ";
    // int idx = int(prev_direction_ / M_PI * 180 / 5);
    // if (idx > 69) idx = 69;
    // out[idx] = '|';
    // pa_log_debug("ECNR: %3.1f[%s]", prev_direction_ / M_PI * 180, out);

    return need_update;
}

float Doa::GetDirectionRadians() {
    //  change to mic view angle
    float direction = prev_direction_ + M_PI;
    if (direction > 2 * M_PI) direction = direction - 2 * M_PI;
    return direction;
}

void Doa::MemoryAlloc() {
    input_phase_ = new std::complex<float> *[num_bin_];
    mode_cov_ = new std::complex<float> **[num_bin_];
    input_cov_ = new std::complex<float> *[num_bin_];
    for (int bin = 0; bin < num_bin_; bin++) {
        input_phase_[bin] = new std::complex<float>[channels_];
        mode_cov_[bin] = new std::complex<float> *[num_pairs_];
        input_cov_[bin] = new std::complex<float>[num_pairs_];
        for (int pair = 0; pair < num_pairs_; pair++) {
            mode_cov_[bin][pair] = new std::complex<float>[grid_points_];
            input_cov_[bin][pair] = std::complex<float>(0.f, 0.f);
        }
    }
    srp_cost_ = new float [grid_points_];
    noise_cost_ = new float [grid_points_];
    for (int grid = 0; grid < grid_points_; grid++) {
        noise_cost_[grid] = 0.75f;
    }
}

void Doa::CalculateModeVectorCovariance() {
    float cos_10[grid_points_] = {
        //  cos(grid_points)
        1.00000000, 0.98480775, 0.93969262, 0.86602540, 0.76604444, 0.64278761,
        0.50000000, 0.34202014, 0.17364818, 0.00000000, -0.17364818, -0.34202014,
        -0.50000000, -0.64278761, -0.76604444, -0.86602540, -0.93969262, -0.98480775,
        -1.00000000, -0.98480775, -0.93969262, -0.86602540, -0.76604444, -0.64278761,
        -0.50000000, -0.34202014, -0.17364818, -0.00000000, 0.17364818, 0.34202014,
        0.50000000, 0.64278761, 0.76604444, 0.86602540, 0.93969262, 0.98480775,
    };
    float sin_10[grid_points_] = {
        //  sin(grid_points)
        0.00000000, 0.17364818, 0.34202014, 0.50000000, 0.64278761, 0.76604444,
        0.86602540, 0.93969262, 0.98480775, 1.00000000, 0.98480775, 0.93969262,
        0.86602540, 0.76604444, 0.64278761, 0.50000000, 0.34202014, 0.17364818,
        0.00000000, -0.17364818, -0.34202014, -0.50000000, -0.64278761, -0.76604444,
        -0.86602540, -0.93969262, -0.98480775, -1.00000000, -0.98480775, -0.93969262,
        -0.86602540, -0.76604444, -0.64278761, -0.50000000, -0.34202014, -0.17364818,
    };

    float tau[channels_][grid_points_];
    for (int ch = 0; ch < channels_; ch++) {
        for (int grid = 0; grid < grid_points_; grid++) {
            // tau = distance / c
            tau[ch][grid] = cos_10[grid] * array_geometry_[ch].x();
            tau[ch][grid] += sin_10[grid] * array_geometry_[ch].y();
            tau[ch][grid] /= speed_of_sound_;
        }
    }

    float omega[num_bin_];
    for (int bin = 0; bin < num_bin_; bin++) {
        // omega = 2 * np.pi * fs * np.arange(nfft // 2 + 1) / nfft
        omega[bin] = 2 * M_PI * sample_rate_ * float(bin + start_bin_) / fft_size_;
    }

    // mode vector size: [num_bin_][channels_][grid_points_]
    for (int bin = 0; bin < num_bin_; bin++) {
        for (int ch = 0; ch < channels_; ch++) {
            for (int grid = 0; grid < grid_points_; grid++) {
                // np.exp(1j * omega[:, None, None] * tau)
                mode_cov_[bin][ch][grid] = std::exp(std::complex<float>(0, omega[bin] * tau[ch][grid]));
            }
        }
    }

    // calculate mode vector covariance matrix (upper triangular)
    // mode vector covariance size: [num_bin_][num_pairs_][grid_points_]
    std::complex<float> mode_vec[channels_];
    int flat_idx;
    for (int bin = 0; bin < num_bin_; bin++) {
        for (int grid = 0; grid < grid_points_; grid++) {
            for (int ch = 0; ch < channels_; ch++) {
                mode_vec[ch] = mode_cov_[bin][ch][grid];
                mode_cov_[bin][ch][grid] = std::complex<float>(0.f, 0.f);
            }
            for (int ch1 = 1; ch1 < channels_; ch1++) {
                for (int ch2 = 0; ch2 < ch1; ch2++) {
                    if (ch2 == 0) flat_idx = ch1 - 1;
                    else flat_idx = ch1 + ch2;
                    // cov[bin, flat_idx, grid] += np.conj(mode_vec[bin, ch2]) * mode_vec[bin, ch1]
                    mode_cov_[bin][flat_idx][grid] += std::conj(mode_vec[ch2]) * mode_vec[ch1];
                }
            }
        }
    }
}

void Doa::CumulateRMS(const std::complex<float>* const* input) {
    float sum = 0;

    for (int bin = start_bin_; bin < end_bin_; bin++) {
        sum += std::abs(input[0][bin]);
    }
    input_rms_ += sum / num_bin_;
}

void Doa::InputPhaseTransform(const std::complex<float>* const* input) {
    // apply phase transform weighting
    float tolerance = 1e-14f;

    // input_phase_ = input[:, freq].T / np.abs(input[:, freq].T)
    // input_phase_[bin][ch]
    for (int ch = 0; ch < channels_; ch++) {
        for (int bin = 0; bin < num_bin_; bin++) {
            float abs = std::abs(input[ch][bin + start_bin_]);
            if (abs < tolerance) abs = tolerance;
            input_phase_[bin][ch] = input[ch][bin + start_bin_] / abs;
        }
    }
}

void Doa::CumulateInputCovariance() {
    int flat_idx;

    for (int bin = 0; bin < num_bin_; bin++) {
        for (int ch1 = 1; ch1 < channels_; ch1++) {
            for (int ch2 = 0; ch2 < ch1; ch2++) {
                if (ch2 == 0) flat_idx = ch1 - 1;
                else flat_idx = ch1 + ch2;
                //  outer product of each frames
                input_cov_[bin][flat_idx] += input_phase_[bin][ch2] * std::conj(input_phase_[bin][ch1]);
            }
        }
    }
}

void Doa::CalculateSrpCosts() {
    float dc_offset = doa_interval_ * channels_ * num_bin_;
    float norm_factor = doa_interval_ * num_bin_ * num_pairs_;

    float linear_compensation[grid_points_] = {
        //  2 - sin(grid_points), mirroring on 180 deg
        1.25000000, 1.20658796, 1.16449496, 1.12500000, 1.08930310, 1.05848889,
        1.03349365, 1.01507684, 1.00379806, 1.00000000, 1.00379806, 1.01507684,
        1.03349365, 1.05848889, 1.08930310, 1.12500000, 1.16449496, 1.20658796,
        1.25000000, 1.20658796, 1.16449496, 1.12500000, 1.08930310, 1.05848889,
        1.03349365, 1.01507684, 1.00379806, 1.00000000, 1.00379806, 1.01507684,
        1.03349365, 1.05848889, 1.08930310, 1.12500000, 1.16449496, 1.20658796,
    };

    // sum_val = 2.0 * np.sum(R) + dC_offset
    // srp_cost[grid] = sum_val / norm_factor
    for (int grid = 0; grid < grid_points_; grid++) {
        // R = input_cov_.real * mode_cov_[:, :, grid].real - input_cov_.imag * mode_cov_[:, :, grid].imag
        srp_cost_[grid] = dc_offset;
        for (int bin = 0; bin < num_bin_; bin++) {
            for (int pair = 0; pair < num_pairs_; pair++) {
                // quadratic form: mode_cov_^H @ input_cov_ @ mode_cov_
                srp_cost_[grid] += 2 * input_cov_[bin][pair].real() * mode_cov_[bin][pair][grid].real();
                srp_cost_[grid] -= 2 * input_cov_[bin][pair].imag() * mode_cov_[bin][pair][grid].imag();
            }
        }
        srp_cost_[grid] /= norm_factor;
        if (is_array_linear_) srp_cost_[grid] *= linear_compensation[grid];
    }
}

void Doa::BackgroundNoiseUpdate() {
    input_rms_ /= doa_interval_;

    // noise cost update
    if (input_rms_ < input_noise_threshold_) {
        for (int grid = 0; grid < grid_points_; grid++) {
            noise_cost_[grid] = (noise_cost_[grid] + srp_cost_[grid]) / 2;
        }
    }
    // remove noise flow cost from srp cost
    for (int grid = 0; grid < grid_points_; grid++) {
        srp_cost_[grid] -= noise_cost_[grid];
    }

    input_noise_threshold_ = (input_noise_threshold_ + input_rms_ * 0.5) * 0.5;
}

float Doa::FindPeakResult() {
    //  find all peaks
    int search_grid = grid_points_;
    if (is_array_linear_) search_grid = grid_points_ / 2 + 1;

    int peaks_idx[search_grid];
    float peaks[search_grid];
    int peaks_size = 0;
    //  find peaks
    if (srp_cost_[0] > srp_cost_[1] && srp_cost_[0] > srp_cost_threshold_) {
        peaks_idx[peaks_size] = 0;
        peaks[peaks_size] = srp_cost_[0];
        peaks_size++;
    }
    if (srp_cost_[search_grid - 1] > srp_cost_[search_grid - 2]
                    && srp_cost_[search_grid - 1] > srp_cost_threshold_) {
        peaks_idx[peaks_size] = search_grid - 1;
        peaks[peaks_size] = srp_cost_[search_grid - 1];
        peaks_size++;
    }
    for (int grid = 1; grid < search_grid - 1; grid++) {
        if (srp_cost_[grid] > srp_cost_[grid - 1] && srp_cost_[grid] > srp_cost_[grid + 1]
                    && srp_cost_[grid] > srp_cost_threshold_) {
            peaks_idx[peaks_size] = grid;
            peaks[peaks_size] = srp_cost_[grid];
            peaks_size++;
        }
    }

    //  if there's no peaks, than return previous direction
    if (peaks_size == 0)
        return direction_history_[0];

    //  sort peaks & index
    float tmp;
    for (int idx1 = 0; idx1 < peaks_size - 1; idx1++) {
        for (int idx2 = 0; idx2 < peaks_size - idx1 - 1; idx2++) {
            if (peaks[idx2] < peaks[idx2 + 1]) {
                tmp = peaks[idx2];
                peaks[idx2] = peaks[idx2 + 1];
                peaks[idx2 + 1] = tmp;

                tmp = peaks_idx[idx2];
                peaks_idx[idx2] = peaks_idx[idx2 + 1];
                peaks_idx[idx2 + 1] = (int) tmp;
            }
        }
    }

    //  select candidate that is close to the last result
    for (int idx = 0; idx < peaks_size; idx++) {
        tmp = 2 * M_PI * (float) peaks_idx[idx] / (float) grid_points_;
        if (std::abs(tmp - direction_history_[0]) <= threshold_radians_) {
            return tmp;
        }
    }
    //  or the largest candidate
    tmp = 2 * M_PI * (float) peaks_idx[0] / (float) grid_points_;

    return tmp;
}

bool Doa::SmoothResult(float result_radians) {
    //  smoothing / median / average filtering on aim angle
    float filtered_radians = result_radians / direction_history_size_;
    for (int i = 9; i > 0 ; i--) {
      direction_history_[i] = direction_history_[i - 1];
      filtered_radians += direction_history_[i + 1] / direction_history_size_;
    }
    direction_history_[0] = result_radians;

    //  trimming 0 ~ 360 degree
    while (filtered_radians > 2 * M_PI) filtered_radians -= (2 * M_PI);
    while (filtered_radians < 0) filtered_radians += (2 * M_PI);

    //  aim update if angle is changed over threshold (10 degree)
    if (fabs(prev_direction_ - filtered_radians) < threshold_radians_)
        return false;

    prev_direction_ = filtered_radians;
    return true;
}

}  //  namespace webrtc_ecnr