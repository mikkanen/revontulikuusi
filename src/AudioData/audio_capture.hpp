#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <stop_token>

constexpr std::size_t AURORA_POINTS = 128;

struct AudioData {
    std::atomic<float> bass{0.0f};
    std::atomic<float> mid{0.0f};
    std::atomic<float> treble{0.0f};
    std::atomic<bool> active{false};

    std::mutex wave_mtx;
    std::array<float, AURORA_POINTS> waveform{};
    std::array<float, AURORA_POINTS> spectrum_env{};
};

// Yhteinen FFT- ja kaista-analyysi raa'alle 48 kHz float32 -puskurille (1024 näytettä)
void process_audio_block(const float* samples, std::size_t count, AudioData& audio);

// Alustakohtainen kaappaussäie
void audio_capture_worker(std::stop_token st, AudioData& audio);
