#include "audio_capture.hpp"

#include <cstdint>
#include <cstdio>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <vector>

void audio_capture_worker(std::stop_token st, AudioData& audio) {
    constexpr std::uint32_t sample_rate = 48000;
    constexpr std::size_t block_size = 1024;

    pa_sample_spec ss{};
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.rate = sample_rate;
    ss.channels = 1;

    pa_buffer_attr attr{};
    attr.maxlength = static_cast<std::uint32_t>(-1);
    attr.fragsize = block_size * sizeof(float);

    int error = 0;
    pa_simple* s = pa_simple_new(
        nullptr,
        "JoulukuusiRevontulet",
        PA_STREAM_RECORD,
        "@DEFAULT_MONITOR@",
        "Audio & Aurora Sync",
        &ss,
        nullptr,
        &attr,
        &error
    );

    if (!s) {
        std::fprintf(stderr, "PulseAudio monitor-yhteys epäonnistui: %s\n", pa_strerror(error));
        return;
    }

    audio.active.store(true);
    std::vector<float> buffer(block_size);

    while (!st.stop_requested()) {
        if (pa_simple_read(s, buffer.data(), buffer.size() * sizeof(float), &error) < 0) {
            break;
        }
        process_audio_block(buffer.data(), buffer.size(), audio);
    }

    audio.active.store(false);
    pa_simple_free(s);
}
