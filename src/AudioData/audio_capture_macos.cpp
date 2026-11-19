#include "audio_capture.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr UInt32 SAMPLE_RATE = 48000;
constexpr UInt32 BUFFER_FRAMES = 1024;
constexpr int NUM_BUFFERS = 3;

struct MacAudioContext {
    AudioData* audio{nullptr};
    std::vector<float> accum;
};

void audio_queue_callback(
    void* inUserData,
    AudioQueueRef inAQ,
    AudioQueueBufferRef inBuffer,
    const AudioTimeStamp*,
    UInt32 inNumPackets,
    const AudioStreamPacketDescription*) {

    auto* ctx = static_cast<MacAudioContext*>(inUserData);
    if (!ctx || !ctx->audio) return;

    const auto* samples = static_cast<const float*>(inBuffer->mAudioData);
    std::size_t count = inBuffer->mAudioDataByteSize / sizeof(float);

    if (count > 0 && inNumPackets > 0) {
        ctx->accum.insert(ctx->accum.end(), samples, samples + count);
        while (ctx->accum.size() >= BUFFER_FRAMES) {
            process_audio_block(ctx->accum.data(), BUFFER_FRAMES, *ctx->audio);
            ctx->accum.erase(ctx->accum.begin(), ctx->accum.begin() + BUFFER_FRAMES);
        }
    }

    AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, nullptr);
}

} // namespace

void audio_capture_worker(std::stop_token st, AudioData& audio) {
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = SAMPLE_RATE;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBitsPerChannel = 32;
    fmt.mChannelsPerFrame = 1;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = sizeof(float);
    fmt.mBytesPerPacket = sizeof(float);

    MacAudioContext ctx{&audio, {}};
    ctx.accum.reserve(BUFFER_FRAMES * 2);

    AudioQueueRef queue = nullptr;
    OSStatus status = AudioQueueNewInput(
        &fmt,
        audio_queue_callback,
        &ctx,
        nullptr,
        kCFRunLoopCommonModes,
        0,
        &queue
    );

    if (status != noErr || !queue) {
        std::fprintf(stderr, "CoreAudio AudioQueueNewInput epäonnistui (status: %d)\n",
                     static_cast<int>(status));
        return;
    }

    UInt32 buffer_bytes = BUFFER_FRAMES * sizeof(float);
    std::array<AudioQueueBufferRef, NUM_BUFFERS> buffers{};

    for (int i = 0; i < NUM_BUFFERS; ++i) {
        AudioQueueAllocateBuffer(queue, buffer_bytes, &buffers[i]);
        AudioQueueEnqueueBuffer(queue, buffers[i], 0, nullptr);
    }

    if (AudioQueueStart(queue, nullptr) != noErr) {
        std::fprintf(stderr, "CoreAudio AudioQueueStart epäonnistui\n");
        AudioQueueDispose(queue, true);
        return;
    }

    audio.active.store(true);

    while (!st.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    audio.active.store(false);
    AudioQueueStop(queue, true);
    AudioQueueDispose(queue, true);
}
