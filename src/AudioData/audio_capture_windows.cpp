#include "audio_capture.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t BLOCK_SIZE = 1024;
constexpr REFERENCE_TIME REFTIMES_PER_SEC = 10000000;

struct ComGuard {
    HRESULT hr;
    ComGuard() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComGuard() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
};

template <typename T>
void safe_release(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

} // namespace

void audio_capture_worker(std::stop_token st, AudioData& audio) {
    ComGuard com;
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE) {
        std::fprintf(stderr, "WASAPI: CoInitializeEx epäonnistui (0x%08lx)\n", com.hr);
        return;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audio_client = nullptr;
    IAudioCaptureClient* capture_client = nullptr;
    WAVEFORMATEX* mix_format = nullptr;

    auto cleanup = [&]() {
        audio.active.store(false);
        if (audio_client) {
            audio_client->Stop();
        }
        if (mix_format) {
            CoTaskMemFree(mix_format);
            mix_format = nullptr;
        }
        safe_release(capture_client);
        safe_release(audio_client);
        safe_release(device);
        safe_release(enumerator);
    };

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator)
    );
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: MMDeviceEnumerator luonti epäonnistui (0x%08lx)\n", hr);
        return;
    }

    // Haetaan järjestelmän oletusulostulolaite (kaiuttimet / kuulokkeet)
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: Oletusäänilaitetta ei löytynyt (0x%08lx)\n", hr);
        cleanup();
        return;
    }

    hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(&audio_client)
    );
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: IAudioClient aktivointi epäonnistui (0x%08lx)\n", hr);
        cleanup();
        return;
    }

    // Shared-mode loopback käyttää aina mikserin natiiviformaattia (yleensä 48 kHz float32)
    hr = audio_client->GetMixFormat(&mix_format);
    if (FAILED(hr) || !mix_format) {
        std::fprintf(stderr, "WASAPI: GetMixFormat epäonnistui (0x%08lx)\n", hr);
        cleanup();
        return;
    }

    // Tarkistetaan, että mikseri tuottaa 32-bit float -näytteitä (standardi Windows 10/11)
    bool is_float = (mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix_format->cbSize >= 22) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix_format);
        is_float = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    if (!is_float || mix_format->wBitsPerSample != 32) {
        std::fprintf(stderr, "WASAPI: Odottamaton näyteformaatti (%u-bit)\n", mix_format->wBitsPerSample);
        cleanup();
        return;
    }

    // Alustetaan loopback-kaappaus (50 ms puskuri)
    hr = audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        REFTIMES_PER_SEC / 20,
        0,
        mix_format,
        nullptr
    );
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: IAudioClient::Initialize epäonnistui (0x%08lx)\n", hr);
        cleanup();
        return;
    }

    hr = audio_client->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&capture_client)
    );
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: IAudioCaptureClient haku epäonnistui (0x%08lx)\n", hr);
        cleanup();
        return;
    }

    // Nostetaan säikeen prioriteettia kevyesti Pro Audio -luokkaan
    DWORD task_index = 0;
    HANDLE avrt_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);

    hr = audio_client->Start();
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: AudioClient::Start epäonnistui (0x%08lx)\n", hr);
        if (avrt_handle) AvRevertMmThreadCharacteristics(avrt_handle);
        cleanup();
        return;
    }

    audio.active.store(true);

    const UINT16 channels = mix_format->nChannels;
    std::vector<float> accum;
    accum.reserve(BLOCK_SIZE * 2);

    while (!st.stop_requested()) {
        UINT32 packet_length = 0;
        hr = capture_client->GetNextPacketSize(&packet_length);
        if (FAILED(hr)) {
            break;
        }

        if (packet_length == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        while (packet_length > 0 && !st.stop_requested()) {
            BYTE* data = nullptr;
            UINT32 num_frames = 0;
            DWORD flags = 0;

            hr = capture_client->GetBuffer(&data, &num_frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                break;
            }

            if (num_frames > 0) {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    accum.insert(accum.end(), num_frames, 0.0f);
                } else if (data) {
                    const auto* samples = reinterpret_cast<const float*>(data);
                    for (UINT32 f = 0; f < num_frames; ++f) {
                        float mono = 0.0f;
                        for (UINT16 c = 0; c < channels; ++c) {
                            mono += samples[f * channels + c];
                        }
                        accum.push_back(mono / static_cast<float>(channels));
                    }
                }

                while (accum.size() >= BLOCK_SIZE) {
                    process_audio_block(accum.data(), BLOCK_SIZE, audio);
                    accum.erase(accum.begin(), accum.begin() + BLOCK_SIZE);
                }
            }

            capture_client->ReleaseBuffer(num_frames);

            if (FAILED(capture_client->GetNextPacketSize(&packet_length))) {
                packet_length = 0;
            }
        }
    }

    if (avrt_handle) {
        AvRevertMmThreadCharacteristics(avrt_handle);
    }
    cleanup();
}
